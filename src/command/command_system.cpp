#include "command_system.h"
#include "map_manager.h"
#include "../player/player_manager.h"
#include "../ban/ban_manager.h"
#include "../comm/comm_manager.h"
#include "../admin/admin_manager.h"
#include "../config/config.h"
#include "../db/database.h"
#include "../public/forwards.h"
#include "../utils/print_utils.h"
#include "../utils/discord.h"
#include "../entity/ccsplayercontroller.h"
#include "../entity/ccsplayerpawn.h"

#include <algorithm>
#include <ctime>
#include <cctype>

// Helpers

// Join args from startIdx into a single string, or return defaultVal if not enough args.
static std::string JoinArgs(const std::vector<std::string> &args, size_t start, const char *defaultVal)
{
	if (args.size() <= start)
		return defaultVal;
	std::string result;
	for (size_t i = start; i < args.size(); i++)
	{
		if (i > start) result += " ";
		result += args[i];
	}
	return result;
}

// Strip characters that could be interpreted as command separators by the engine.
static std::string SanitizeForServerCommand(const std::string &input)
{
	std::string result;
	result.reserve(input.size());
	for (char c : input)
	{
		if (c != ';' && c != '\n' && c != '\r')
			result += c;
	}
	return result;
}

// Validate an IPv4 address string (e.g. "192.168.1.1").
static bool IsValidIPv4(const char *ip)
{
	if (!ip || !*ip)
		return false;
	int parts = 0;
	int num = 0;
	bool hasDigit = false;
	for (const char *p = ip; ; p++)
	{
		if (*p >= '0' && *p <= '9')
		{
			num = num * 10 + (*p - '0');
			if (num > 255) return false;
			hasDigit = true;
		}
		else if (*p == '.' || *p == '\0')
		{
			if (!hasDigit) return false;
			parts++;
			if (*p == '\0') break;
			num = 0;
			hasDigit = false;
		}
		else
		{
			return false;
		}
	}
	return parts == 4;
}

// Check if caller has higher immunity than target. Returns true if action is allowed.
// Console (slot < 0) always passes. Non-admin targets always pass.
static bool CheckImmunity(int callerSlot, int targetSlot)
{
	if (callerSlot < 0)
		return true; // Console bypasses immunity

	if (callerSlot == targetSlot)
		return true; // Can always target self

	// Root flag bypasses immunity
	if (g_CS2AAdminManager.PlayerHasFlag(callerSlot, ADMFLAG_ROOT))
		return true;

	const AdminEntry *targetAdmin = g_CS2AAdminManager.GetPlayerAdmin(targetSlot);
	if (!targetAdmin)
		return true; // Non-admin target, always allowed

	const AdminEntry *callerAdmin = g_CS2AAdminManager.GetPlayerAdmin(callerSlot);
	int callerImm = callerAdmin ? callerAdmin->immunity : 0;
	int targetImm = targetAdmin->immunity;

	if (targetImm > 0 && callerImm <= targetImm)
	{
		ADMIN_ReplyToCommand(callerSlot, "Cannot target this player (higher immunity).\n");
		return false;
	}
	return true;
}

CS2ACommandSystem g_CS2ACommandSystem;

void CS2ACommandSystem::RegisterCommand(const char *name, ChatCommandCallback callback)
{
	std::string lower(name);
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	m_commands[lower] = callback;
}

bool CS2ACommandSystem::ShouldBlockChat(int slot)
{
	return g_CS2ACommManager.IsGagged(slot);
}

bool CS2ACommandSystem::ProcessChatMessage(int slot, const char *message, bool teamOnly)
{
	if (!message || !*message)
		return false;

	// Strip quotes if present (CS2 say command sends the message in quotes)
	std::string msg(message);
	if (msg.size() >= 2 && msg.front() == '"' && msg.back() == '"')
		msg = msg.substr(1, msg.size() - 2);

	if (msg.empty())
		return false;

	// Check for command prefix (each character in the prefix string is a valid trigger)
	bool silent = false;
	char prefix = msg[0];
	bool isNormalPrefix = (!g_CS2AConfig.commandPrefix.empty() &&
		g_CS2AConfig.commandPrefix.find(prefix) != std::string::npos);
	bool isSilentPrefix = (!g_CS2AConfig.silentCommandPrefix.empty() &&
		g_CS2AConfig.silentCommandPrefix.find(prefix) != std::string::npos);

	if (isSilentPrefix)
		silent = true;
	else if (isNormalPrefix)
		silent = false;
	else
		return false; // Not a command

	// Parse command name and args
	std::string rest = msg.substr(1);
	if (rest.empty())
		return false;

	std::vector<std::string> parts;
	std::string current;
	bool inQuotes = false;

	for (size_t i = 0; i < rest.size(); i++)
	{
		char c = rest[i];
		if (c == '"')
		{
			inQuotes = !inQuotes;
		}
		else if (c == ' ' && !inQuotes)
		{
			if (!current.empty())
			{
				parts.push_back(current);
				current.clear();
			}
		}
		else
		{
			current += c;
		}
	}
	if (!current.empty())
		parts.push_back(current);

	if (parts.empty())
		return false;

	// Lookup command
	std::string cmdName = parts[0];
	std::transform(cmdName.begin(), cmdName.end(), cmdName.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	auto it = m_commands.find(cmdName);
	if (it == m_commands.end())
		return false;

	// Build args (everything after command name)
	std::vector<std::string> args(parts.begin() + 1, parts.end());

	it->second(slot, args, silent);
	return true;
}

void CS2ACommandSystem::RegisterBuiltinCommands()
{
	// !ban <target> <time> [reason]
	RegisterCommand("ban", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "ban", "banning", ADMFLAG_BAN))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !ban <target> <time> [reason] (time: minutes, or 1h/2d/1w/1m)\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		if (!CheckImmunity(slot, target))
			return;

		int time = ADMIN_ParseDuration(args[1].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Invalid time. Use minutes (e.g. 30) or suffixes: h(ours), d(ays), w(eeks), m(onths). 0 = permanent.\n");
			return;
		}
		std::string reason = JoinArgs(args, 2, "Banned");

		PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(target);
		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
		if (targetPlayer)
		{
			g_CS2ADiscord.NotifyAdminAction(
				adminName.c_str(),
				"Ban", targetPlayer->name.c_str(), reason.c_str(), time);
		}

		g_CS2ABanManager.BanPlayer(target, time, reason.c_str(), slot);
	});

	// !unban <steamid>
	RegisterCommand("unban", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "unban", "banning", ADMFLAG_UNBAN))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !unban <steamid>\n");
			return;
		}

		g_CS2ABanManager.Unban(args[0].c_str(), slot);
	});

	// !addban <time> <steamid> [reason] - offline ban by SteamID
	RegisterCommand("addban", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "addban", "banning", ADMFLAG_BAN))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !addban <time> <steamid> [reason] (time: minutes, or 1h/2d/1w/1m)\n");
			return;
		}

		int time = ADMIN_ParseDuration(args[0].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Invalid time. Use minutes (e.g. 30) or suffixes: h(ours), d(ays), w(eeks), m(onths). 0 = permanent.\n");
			return;
		}

		const char *authid = args[1].c_str();
		std::string reason = JoinArgs(args, 2, "Banned");
		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);

		g_CS2ADiscord.NotifyAdminAction(adminName.c_str(), "AddBan", authid, reason.c_str(), time);
		g_CS2ABanManager.AddBan(authid, time, reason.c_str(), slot);
	});

	// !mute <target> <time> [reason]
	RegisterCommand("mute", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "mute", "comms", ADMFLAG_CHAT))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !mute <target> <time> [reason] (time: minutes, or 1h/2d/1w/1m)\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		if (!CheckImmunity(slot, target))
			return;

		int time = ADMIN_ParseDuration(args[1].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Invalid time. Use minutes (e.g. 30) or suffixes: h(ours), d(ays), w(eeks), m(onths). 0 = permanent.\n");
			return;
		}
		std::string reason = JoinArgs(args, 2, "Muted");

		PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(target);
		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
		if (targetPlayer)
		{
			g_CS2ADiscord.NotifyAdminAction(
				adminName.c_str(),
				"Mute", targetPlayer->name.c_str(), reason.c_str(), time);
		}

		g_CS2ACommManager.MutePlayer(target, time, reason.c_str(), slot);
	});

	// !unmute <target>
	RegisterCommand("unmute", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "unmute", "comms", ADMFLAG_CHAT))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !unmute <target>\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		g_CS2ACommManager.UnmutePlayer(target, slot);
	});

	// !gag <target> <time> [reason]
	RegisterCommand("gag", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "gag", "comms", ADMFLAG_CHAT))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !gag <target> <time> [reason] (time: minutes, or 1h/2d/1w/1m)\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		if (!CheckImmunity(slot, target))
			return;

		int time = ADMIN_ParseDuration(args[1].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Invalid time. Use minutes (e.g. 30) or suffixes: h(ours), d(ays), w(eeks), m(onths). 0 = permanent.\n");
			return;
		}
		std::string reason = JoinArgs(args, 2, "Gagged");
		g_CS2ACommManager.GagPlayer(target, time, reason.c_str(), slot);
	});

	// !ungag <target>
	RegisterCommand("ungag", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "ungag", "comms", ADMFLAG_CHAT))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !ungag <target>\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		g_CS2ACommManager.UngagPlayer(target, slot);
	});

	// !silence <target> <time> [reason]
	RegisterCommand("silence", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "silence", "comms", ADMFLAG_CHAT))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !silence <target> <time> [reason] (time: minutes, or 1h/2d/1w/1m)\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		if (!CheckImmunity(slot, target))
			return;

		int time = ADMIN_ParseDuration(args[1].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Invalid time. Use minutes (e.g. 30) or suffixes: h(ours), d(ays), w(eeks), m(onths). 0 = permanent.\n");
			return;
		}
		std::string reason = JoinArgs(args, 2, "Silenced");
		g_CS2ACommManager.SilencePlayer(target, time, reason.c_str(), slot);
	});

	// !unsilence <target>
	RegisterCommand("unsilence", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "unsilence", "comms", ADMFLAG_CHAT))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !unsilence <target>\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		g_CS2ACommManager.UnsilencePlayer(target, slot);
	});

	// !banip <ip> <time> [reason]
	RegisterCommand("banip", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "banip", "banning", ADMFLAG_BAN))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !banip <ip> <time> [reason] (time: minutes, or 1h/2d/1w/1m)\n");
			return;
		}

		const char *ip = args[0].c_str();
		if (!IsValidIPv4(ip))
		{
			ADMIN_ReplyToCommand(slot, "Invalid IP address format.\n");
			return;
		}

		int time = ADMIN_ParseDuration(args[1].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Invalid time. Use minutes (e.g. 30) or suffixes: h(ours), d(ays), w(eeks), m(onths). 0 = permanent.\n");
			return;
		}
		std::string reason = JoinArgs(args, 2, "Banned");

		g_CS2ABanManager.BanIP(ip, time, reason.c_str(), slot);
	});

	// !comms [target] - check comm status
	RegisterCommand("comms", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "comms", "comms", ADMFLAG_CHAT))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		int target = slot;
		if (!args.empty())
		{
			target = ADMIN_FindTarget(slot, args[0].c_str());
			if (target < 0)
				return;
		}

		g_CS2ACommManager.PrintCommsStatus(target, slot);
	});

	// !listbans <target>
	RegisterCommand("listbans", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "listbans", "banning", ADMFLAG_BAN))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !listbans <target>\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(target);
		if (!player || !player->connected)
			return;

		g_CS2ABanManager.ListBans(slot, player->authid.c_str());
	});

	// !listcomms <target>
	RegisterCommand("listcomms", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "listcomms", "comms", ADMFLAG_CHAT))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !listcomms <target>\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(target);
		if (!player || !player->connected)
			return;

		g_CS2ABanManager.ListComms(slot, player->authid.c_str());
	});

	// !report <target> <reason>
	RegisterCommand("report", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (slot < 0)
		{
			ADMIN_ReplyToCommand(slot, "This command cannot be used from console.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !report <target> <reason>\n");
			return;
		}

		PlayerInfo *reporter = g_CS2APlayerManager.GetPlayer(slot);
		if (!reporter || !reporter->connected)
			return;

		// Cooldown check
		CGlobalVars *globals = GetGameGlobals();
		if (globals && reporter->lastReportTime > 0.0 &&
			(globals->curtime - reporter->lastReportTime) < g_CS2AConfig.reportCooldown)
		{
			int remaining = (int)(g_CS2AConfig.reportCooldown - (globals->curtime - reporter->lastReportTime));
			ADMIN_ReplyToCommand(slot, "You must wait %d seconds before reporting again.\n", remaining);
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		if (target == slot)
		{
			ADMIN_ReplyToCommand(slot, "You cannot report yourself.\n");
			return;
		}

		PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(target);
		if (!targetPlayer || !targetPlayer->connected)
			return;

		// Build reason from remaining args
		std::string reason;
		for (size_t i = 1; i < args.size(); i++)
		{
			if (i > 1) reason += " ";
			reason += args[i];
		}

		if ((int)reason.size() < g_CS2AConfig.reportMinLength)
		{
			ADMIN_ReplyToCommand(slot, "Report reason must be at least %d characters.\n", g_CS2AConfig.reportMinLength);
			return;
		}

		// Cap reason length to prevent buffer overflow in query
		if (reason.size() > 512)
			reason.resize(512);

		// Fire forward
		g_CS2AForwards.FireOnReportPlayer(slot, target, reason.c_str());

		// Record cooldown
		if (globals)
			reporter->lastReportTime = globals->curtime;

		// Insert into submissions table
		if (g_CS2ADatabase.IsConnected())
		{
			std::string escAuth = g_CS2ADatabase.Escape(reporter->authid.c_str());
			std::string escName = g_CS2ADatabase.Escape(reporter->name.c_str());
			std::string escReason = g_CS2ADatabase.Escape(reason.c_str());
			std::string escTargetInfo = g_CS2ADatabase.Escape(
				(targetPlayer->authid + ":" + targetPlayer->name).c_str());

			long long now = (long long)std::time(nullptr);
			char query[4096];
			snprintf(query, sizeof(query),
				"INSERT INTO %s_submissions (submitted, SteamId, name, email, reason, ip, server) "
				"VALUES (%lld, '%s', '%s', '%s', '%s', '', %d)",
				g_CS2AConfig.databasePrefix.c_str(),
				now, escAuth.c_str(), escName.c_str(), escTargetInfo.c_str(),
				escReason.c_str(), g_CS2ABanManager.GetServerID());

			g_CS2ADatabase.Query(query, [](ISQLQuery *) {});
		}

		// Notify admins
		ADMIN_ChatToAdmins("[ADMIN] %s reported %s: %s\n",
			reporter->name.c_str(), targetPlayer->name.c_str(), reason.c_str());
		ADMIN_ReplyToCommand(slot, "Report submitted against %s.\n", targetPlayer->name.c_str());

		g_CS2ADiscord.NotifyReport(reporter->name.c_str(), targetPlayer->name.c_str(), reason.c_str());

		ADMIN_LogAction(slot, (std::string("Reported ") + targetPlayer->name + ": " + reason).c_str());
	});

	// !kick <target> [reason]
	RegisterCommand("kick", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "kick", "kicking", ADMFLAG_KICK))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !kick <target> [reason]\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		if (target == slot)
		{
			ADMIN_ReplyToCommand(slot, "You cannot kick yourself.\n");
			return;
		}

		if (!CheckImmunity(slot, target))
			return;

		PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(target);
		if (!targetPlayer || !targetPlayer->connected)
			return;

		std::string reason = JoinArgs(args, 1, "Kicked by admin");

		if (g_CS2AForwards.FireOnKickPlayer(target, slot, reason.c_str()))
			return;

		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);

		ADMIN_ChatToAll("[ADMIN] %s kicked %s. Reason: %s\n",
			adminName.c_str(), targetPlayer->name.c_str(), reason.c_str());

		g_CS2ADiscord.NotifyAdminAction(adminName.c_str(), "Kick",
			targetPlayer->name.c_str(), reason.c_str(), -1);

		ADMIN_LogAction(slot, (std::string("Kicked ") + targetPlayer->name + ": " + reason).c_str());

		g_pEngine->DisconnectClient(CPlayerSlot(target), NETWORK_DISCONNECT_KICKED);
	});

	// !slay <target>
	RegisterCommand("slay", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "slay", "slaying", ADMFLAG_SLAY))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !slay <target>\n");
			return;
		}

		TargetResult targets = ADMIN_FindTargets(slot, args[0].c_str());
		if (!targets.error.empty())
		{
			ADMIN_ReplyToCommand(slot, "%s\n", targets.error.c_str());
			return;
		}

		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);

		int slayed = 0;
		for (int targetSlot : targets.slots)
		{
			if (!CheckImmunity(slot, targetSlot))
				continue;

			PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(targetSlot);
			if (!targetPlayer || !targetPlayer->connected)
				continue;

			if (g_CS2AForwards.FireOnSlayPlayer(targetSlot, slot))
				continue;

			ConCommandRef killCmd("kill");
			if (killCmd.IsValidRef())
			{
				CCommand killArgs;
				CCommandContext killCtx(CT_NO_TARGET, CPlayerSlot(targetSlot));
				g_pICvar->DispatchConCommand(killCmd, killCtx, killArgs);
			}
			slayed++;
		}

		if (slayed > 0)
		{
			if (targets.isMultiTarget)
			{
				ADMIN_ChatToAll("[ADMIN] %s slayed %d players.\n", adminName.c_str(), slayed);
				ADMIN_LogAction(slot, (std::string("Slayed ") + std::to_string(slayed) + " players").c_str());
			}
			else
			{
				PlayerInfo *tp = g_CS2APlayerManager.GetPlayer(targets.slots[0]);
				std::string targetName = tp ? tp->name : "Unknown";
				ADMIN_ChatToAll("[ADMIN] %s slayed %s.\n", adminName.c_str(), targetName.c_str());
				ADMIN_LogAction(slot, (std::string("Slayed ") + targetName).c_str());
			}
		}
	});

	// !who - List all online admins and their flags
	RegisterCommand("who", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "who", "admin", ADMFLAG_GENERIC))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		CGlobalVars *globals = GetGameGlobals();
		int maxClients = globals ? globals->maxClients : MAXPLAYERS;

		ADMIN_ReplyToCommand(slot, "Online Admins:\n");
		int count = 0;

		for (int i = 0; i < maxClients; i++)
		{
			PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
			if (!p || !p->connected || p->fakePlayer)
				continue;

			const AdminEntry *admin = g_CS2AAdminManager.GetPlayerAdmin(i);
			if (!admin)
				continue;

			std::string flags = CS2AAdminManager::FlagsToString(admin->flags);
			std::string group = admin->group.empty() ? "(no group)" : admin->group;
			int immunity = admin->immunity;

			ADMIN_PrintToClient(slot, "  %s [%s] flags: %s imm: %d\n",
				p->name.c_str(), group.c_str(), flags.c_str(), immunity);
			count++;
		}

		if (count == 0)
			ADMIN_PrintToClient(slot, "  No admins currently online.\n");
		else
			ADMIN_PrintToClient(slot, "%d admin(s) online\n", count);
	});

	// !listdc - Show recently disconnected players
	RegisterCommand("listdc", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "listdc", "admin", ADMFLAG_BAN))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		const auto &disconnected = g_CS2APlayerManager.GetDisconnectedPlayers();

		if (disconnected.empty())
		{
			ADMIN_ReplyToCommand(slot, "No recently disconnected players.\n");
			return;
		}

		CGlobalVars *globals = GetGameGlobals();
		double curtime = globals ? globals->curtime : 0.0;

		ADMIN_ReplyToCommand(slot, "Recently Disconnected Players:\n");

		// Show most recent first
		for (int i = (int)disconnected.size() - 1; i >= 0; i--)
		{
			const DisconnectedPlayer &dc = disconnected[i];

			std::string authid = SteamID64ToAuthId(dc.steamid64);

			int secsAgo = 0;
			if (curtime > 0.0 && dc.disconnectTime > 0.0)
				secsAgo = (int)(curtime - dc.disconnectTime);

			std::string timeAgo;
			if (secsAgo < 60)
				timeAgo = std::to_string(secsAgo) + "s ago";
			else if (secsAgo < 3600)
				timeAgo = std::to_string(secsAgo / 60) + "m ago";
			else
				timeAgo = std::to_string(secsAgo / 3600) + "h ago";

			ADMIN_PrintToClient(slot, "  %s (%s) [%s] - %s\n",
				dc.name.c_str(), authid.c_str(), dc.ip.c_str(), timeAgo.c_str());
		}

		ADMIN_PrintToClient(slot, "%d player(s) recently disconnected\n", (int)disconnected.size());
	});

	// !help [page] - List all available commands
	RegisterCommand("help", [this](int slot, const std::vector<std::string> &args, bool silent) {
		int page = 1;
		if (!args.empty())
		{
			page = std::atoi(args[0].c_str());
			if (page < 1) page = 1;
		}

		std::vector<std::string> cmds;
		cmds.reserve(m_commands.size());
		for (const auto &pair : m_commands)
			cmds.push_back(pair.first);

		std::sort(cmds.begin(), cmds.end());

		const int perPage = 8;
		int totalPages = ((int)cmds.size() + perPage - 1) / perPage;
		if (page > totalPages) page = totalPages;

		int startIdx = (page - 1) * perPage;
		int endIdx = startIdx + perPage;
		if (endIdx > (int)cmds.size()) endIdx = (int)cmds.size();

		ADMIN_ReplyToCommand(slot, "Commands (page %d/%d):\n", page, totalPages);
		for (int i = startIdx; i < endIdx; i++)
			ADMIN_PrintToClient(slot, "  %s%s\n", g_CS2AConfig.commandPrefix.c_str(), cmds[i].c_str());
		if (page < totalPages)
			ADMIN_PrintToClient(slot, "Use !help %d for next page.\n", page + 1);
	});

	// !find <text> - Search commands by name
	RegisterCommand("find", [this](int slot, const std::vector<std::string> &args, bool silent) {
		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !find <text>\n");
			return;
		}

		std::string search = args[0];
		std::transform(search.begin(), search.end(), search.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		std::vector<std::string> matches;
		for (const auto &pair : m_commands)
		{
			if (pair.first.find(search) != std::string::npos)
				matches.push_back(pair.first);
		}

		if (matches.empty())
		{
			ADMIN_ReplyToCommand(slot, "No commands found matching '%s'.\n", args[0].c_str());
			return;
		}

		std::sort(matches.begin(), matches.end());
		ADMIN_ReplyToCommand(slot, "Commands matching '%s':\n", args[0].c_str());
		for (const auto &cmd : matches)
			ADMIN_PrintToClient(slot, "  %s%s\n", g_CS2AConfig.commandPrefix.c_str(), cmd.c_str());
	});

	// !rcon <command> - Execute a server console command
	RegisterCommand("rcon", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "rcon", "admin", ADMFLAG_RCON))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !rcon <command>\n");
			return;
		}

		std::string cmd = JoinArgs(args, 0, "");

		// Strip newlines to prevent command injection after the terminator
		cmd.erase(std::remove(cmd.begin(), cmd.end(), '\n'), cmd.end());
		cmd.erase(std::remove(cmd.begin(), cmd.end(), '\r'), cmd.end());

		// Append newline for ServerCommand
		cmd += "\n";
		g_pEngine->ServerCommand(cmd.c_str());

		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);

		ADMIN_ReplyToCommand(slot, "Executed: %s", cmd.c_str());
		ADMIN_LogAction(slot, (std::string("RCON: ") + cmd).c_str());

		g_CS2ADiscord.NotifyAdminAction(adminName.c_str(), "RCON",
			cmd.c_str(), "", -1);
	});

	// !pm <target> <message> - Private message a player
	RegisterCommand("pm", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "pm", "admin", ADMFLAG_CHAT))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !pm <target> <message>\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		std::string message;
		for (size_t i = 1; i < args.size(); i++)
		{
			if (i > 1) message += " ";
			message += args[i];
		}

		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
		PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(target);
		std::string targetName = targetPlayer ? targetPlayer->name : "Unknown";

		// Send to the target player
		ADMIN_PrintToChat(target, "\x0E[PM from %s]\x01 %s\n", adminName.c_str(), message.c_str());

		// Echo to the sender
		if (slot >= 0)
			ADMIN_PrintToChat(slot, "\x0E[PM to %s]\x01 %s\n", targetName.c_str(), message.c_str());

		// Echo to all other admins
		CGlobalVars *globals = GetGameGlobals();
		int maxClients = globals ? globals->maxClients : MAXPLAYERS;
		for (int i = 0; i < maxClients; i++)
		{
			if (i == slot || i == target)
				continue;

			if (g_CS2AAdminManager.PlayerHasFlag(i, ADMFLAG_GENERIC))
			{
				ADMIN_PrintToChat(i, "\x09[PM %s -> %s]\x01 %s\n",
					adminName.c_str(), targetName.c_str(), message.c_str());
			}
		}

		ADMIN_LogAction(slot, (std::string("PM to ") + targetName + ": " + message).c_str());
	});

	// !map <mapname|workshopid> - Change the current map
	RegisterCommand("map", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "map", "admin", ADMFLAG_CHANGEMAP))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !map <mapname|workshopid>\n");
			return;
		}

		std::string error;
		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);

		if (g_CS2AForwards.FireOnMapChange(args[0].c_str(), slot))
			return;

		if (!g_CS2AMapManager.ChangeMap(args[0].c_str(), error))
		{
			ADMIN_ReplyToCommand(slot, "%s\n", error.c_str());
			return;
		}

		ADMIN_ChatToAll("[ADMIN] %s changed map to %s.\n", adminName.c_str(), args[0].c_str());
		ADMIN_LogAction(slot, (std::string("Changed map to ") + args[0]).c_str());

		g_CS2ADiscord.NotifyAdminAction(adminName.c_str(), "Map Change",
			args[0].c_str(), "", -1);
	});

	// !maps [page] - List available maps from maplist
	RegisterCommand("maps", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "maps", "admin", ADMFLAG_CHANGEMAP))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		const auto &maps = g_CS2AMapManager.GetMaps();
		if (maps.empty())
		{
			ADMIN_ReplyToCommand(slot, "No maps loaded. Check cfg/maplist.txt\n");
			return;
		}

		int page = 1;
		if (!args.empty())
		{
			page = std::atoi(args[0].c_str());
			if (page < 1) page = 1;
		}

		const int perPage = 8;
		int totalPages = ((int)maps.size() + perPage - 1) / perPage;
		if (page > totalPages) page = totalPages;

		int startIdx = (page - 1) * perPage;
		int endIdx = startIdx + perPage;
		if (endIdx > (int)maps.size()) endIdx = (int)maps.size();

		ADMIN_ReplyToCommand(slot, "Maps (page %d/%d):\n", page, totalPages);
		for (int i = startIdx; i < endIdx; i++)
		{
			if (maps[i].isWorkshop)
				ADMIN_PrintToClient(slot, "  %s [ws:%s]\n", maps[i].displayName.c_str(), maps[i].workshopId.c_str());
			else
				ADMIN_PrintToClient(slot, "  %s\n", maps[i].mapName.c_str());
		}
		if (page < totalPages)
			ADMIN_PrintToClient(slot, "Use !maps %d for next page.\n", page + 1);
	});

	// !entfire <entity> <input> [value] - Fire an input on an entity
	RegisterCommand("entfire", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "entfire", "admin", ADMFLAG_CHEATS))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !entfire <entity> <input> [value]\n");
			return;
		}

		// Build the ent_fire command with sanitized args
		std::string cmd = "ent_fire";
		for (const auto &arg : args)
		{
			cmd += " ";
			cmd += SanitizeForServerCommand(arg);
		}
		cmd += "\n";

		g_pEngine->ServerCommand(cmd.c_str());

		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);

		ADMIN_ReplyToCommand(slot, "Fired: %s %s%s\n",
			args[0].c_str(), args[1].c_str(),
			args.size() > 2 ? (" " + args[2]).c_str() : "");
		ADMIN_LogAction(slot, (std::string("EntFire: ") + cmd).c_str());
	});

	// !give <target> <weapon> - Give a weapon to a player
	RegisterCommand("give", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "give", "admin", ADMFLAG_CHEATS))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !give <target> <weapon>\n");
			return;
		}

		TargetResult targets = ADMIN_FindTargets(slot, args[0].c_str());
		if (!targets.error.empty())
		{
			ADMIN_ReplyToCommand(slot, "%s\n", targets.error.c_str());
			return;
		}

		// Normalize weapon name: prepend weapon_ if not present
		std::string weapon = args[1];
		std::transform(weapon.begin(), weapon.end(), weapon.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (weapon.find("weapon_") != 0 && weapon.find("item_") != 0)
			weapon = "weapon_" + weapon;

		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
		int given = 0;

		for (int targetSlot : targets.slots)
		{
			if (!CheckImmunity(slot, targetSlot))
				continue;

			PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(targetSlot);
			if (!targetPlayer || !targetPlayer->connected)
				continue;

			CCSPlayerController *controller = CCSPlayerController::FromSlot(targetSlot);
			if (!controller || !controller->m_bPawnIsAlive())
				continue;

			CEntityHandle &hPawn = controller->m_hPawn();
			CEntityInstance *pawnInst = ResolveEntityHandle(hPawn);
			if (!pawnInst)
				continue;

			CBasePlayerPawn *pawn = reinterpret_cast<CBasePlayerPawn *>(pawnInst);
			CCSPlayer_ItemServices *itemServices = pawn->m_pItemServices();
			if (!itemServices)
				continue;

			itemServices->GiveNamedItem(weapon.c_str());
			given++;
		}

		if (given > 0)
		{
			if (targets.isMultiTarget)
			{
				ADMIN_ChatToAll("[ADMIN] %s gave %s to %d players.\n",
					adminName.c_str(), weapon.c_str(), given);
				ADMIN_LogAction(slot, (std::string("Gave ") + weapon + " to " +
					std::to_string(given) + " players").c_str());
			}
			else
			{
				PlayerInfo *tp = g_CS2APlayerManager.GetPlayer(targets.slots[0]);
				std::string targetName = tp ? tp->name : "Unknown";
				ADMIN_ChatToAll("[ADMIN] %s gave %s to %s.\n",
					adminName.c_str(), weapon.c_str(), targetName.c_str());
				ADMIN_LogAction(slot, (std::string("Gave ") + weapon + " to " + targetName).c_str());
			}
		}
		else
		{
			ADMIN_ReplyToCommand(slot, "No valid alive targets found.\n");
		}
	});

	// !strip <target> - Strip all weapons from a player
	RegisterCommand("strip", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "strip", "admin", ADMFLAG_CHEATS))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.empty())
		{
			ADMIN_ReplyToCommand(slot, "Usage: !strip <target>\n");
			return;
		}

		TargetResult targets = ADMIN_FindTargets(slot, args[0].c_str());
		if (!targets.error.empty())
		{
			ADMIN_ReplyToCommand(slot, "%s\n", targets.error.c_str());
			return;
		}

		std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
		int stripped = 0;

		for (int targetSlot : targets.slots)
		{
			if (!CheckImmunity(slot, targetSlot))
				continue;

			PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(targetSlot);
			if (!targetPlayer || !targetPlayer->connected)
				continue;

			CCSPlayerController *controller = CCSPlayerController::FromSlot(targetSlot);
			if (!controller || !controller->m_bPawnIsAlive())
				continue;

			CEntityHandle &hPawn = controller->m_hPawn();
			CEntityInstance *pawnInst = ResolveEntityHandle(hPawn);
			if (!pawnInst)
				continue;

			CBasePlayerPawn *pawn = reinterpret_cast<CBasePlayerPawn *>(pawnInst);
			CCSPlayer_ItemServices *itemServices = pawn->m_pItemServices();
			if (!itemServices)
				continue;

			itemServices->StripPlayerWeapons(true);
			stripped++;
		}

		if (stripped > 0)
		{
			if (targets.isMultiTarget)
			{
				ADMIN_ChatToAll("[ADMIN] %s stripped weapons from %d players.\n",
					adminName.c_str(), stripped);
				ADMIN_LogAction(slot, (std::string("Stripped weapons from ") +
					std::to_string(stripped) + " players").c_str());
			}
			else
			{
				PlayerInfo *tp = g_CS2APlayerManager.GetPlayer(targets.slots[0]);
				std::string targetName = tp ? tp->name : "Unknown";
				ADMIN_ChatToAll("[ADMIN] %s stripped weapons from %s.\n",
					adminName.c_str(), targetName.c_str());
				ADMIN_LogAction(slot, (std::string("Stripped weapons from ") + targetName).c_str());
			}
		}
		else
		{
			ADMIN_ReplyToCommand(slot, "No valid alive targets found.\n");
		}
	});
}

// Single static callback for all dynamically registered mm_ console commands.
// Extracts command name, strips "mm_" prefix, and dispatches to the chat command handler.
static void ConsoleCommandCallback(const CCommandContext &context, const CCommand &args)
{
	if (args.ArgC() < 1)
		return;

	const char *fullName = args[0]; // e.g. "mm_who"
	const char *cmdName = fullName;

	// Strip "mm_" prefix
	if (strncmp(cmdName, "mm_", 3) == 0)
		cmdName += 3;

	// Build args vector from CCommand (skip arg 0 which is the command name)
	std::vector<std::string> cmdArgs;
	for (int i = 1; i < args.ArgC(); i++)
		cmdArgs.push_back(args[i]);

	// Use the player slot from the command context (-1 for server console)
	int slot = context.GetPlayerSlot().Get();

	// Dispatch with the caller's slot, silent = false
	g_CS2ACommandSystem.DispatchConsoleCommand(cmdName, cmdArgs, slot);
}

void CS2ACommandSystem::DispatchConsoleCommand(const char *cmdName, const std::vector<std::string> &args, int slot)
{
	std::string lower(cmdName);
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	auto it = m_commands.find(lower);
	if (it == m_commands.end())
	{
		META_CONPRINTF("[ADMIN] Unknown command: %s\n", cmdName);
		return;
	}

	it->second(slot, args, false);
}

void CS2ACommandSystem::RegisterConsoleCommands()
{
	for (auto &kv : m_commands)
	{
		ConsoleCmd entry;
		entry.name = "mm_" + kv.first;
		entry.desc = "CS2Admin: " + kv.first;
		m_consoleCommands.push_back(std::move(entry));
	}

	// Create ConCommand objects after all entries are in the vector (stable pointers).
	for (auto &entry : m_consoleCommands)
	{
		entry.cmd = new ConCommand(entry.name.c_str(), ConsoleCommandCallback,
			entry.desc.c_str(), FCVAR_CLIENT_CAN_EXECUTE);
	}
}
