#include "command_system.h"
#include "../player/player_manager.h"
#include "../ban/ban_manager.h"
#include "../comm/comm_manager.h"
#include "../admin/admin_manager.h"
#include "../config/config.h"
#include "../db/database.h"
#include "../public/forwards.h"
#include "../utils/print_utils.h"

#include <algorithm>
#include <ctime>
#include <cctype>

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
			ADMIN_ReplyToCommand(slot, "Usage: !ban <target> <time> [reason]\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		int time = std::atoi(args[1].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Time must be 0 (permanent) or positive.\n");
			return;
		}
		std::string reason = "Banned";
		if (args.size() > 2)
		{
			reason.clear();
			for (size_t i = 2; i < args.size(); i++)
			{
				if (i > 2) reason += " ";
				reason += args[i];
			}
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

	// !mute <target> <time> [reason]
	RegisterCommand("mute", [](int slot, const std::vector<std::string> &args, bool silent) {
		if (!g_CS2AAdminManager.CanPlayerUseCommand(slot, "mute", "comms", ADMFLAG_CHAT))
		{
			ADMIN_ReplyToCommand(slot, "You do not have permission to use this command.\n");
			return;
		}

		if (args.size() < 2)
		{
			ADMIN_ReplyToCommand(slot, "Usage: !mute <target> <time> [reason]\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		int time = std::atoi(args[1].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Time must be 0 (permanent) or positive.\n");
			return;
		}
		std::string reason = "Muted";
		if (args.size() > 2)
		{
			reason.clear();
			for (size_t i = 2; i < args.size(); i++)
			{
				if (i > 2) reason += " ";
				reason += args[i];
			}
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
			ADMIN_ReplyToCommand(slot, "Usage: !gag <target> <time> [reason]\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		int time = std::atoi(args[1].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Time must be 0 (permanent) or positive.\n");
			return;
		}
		std::string reason = "Gagged";
		if (args.size() > 2)
		{
			reason.clear();
			for (size_t i = 2; i < args.size(); i++)
			{
				if (i > 2) reason += " ";
				reason += args[i];
			}
		}
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
			ADMIN_ReplyToCommand(slot, "Usage: !silence <target> <time> [reason]\n");
			return;
		}

		int target = ADMIN_FindTarget(slot, args[0].c_str());
		if (target < 0)
			return;

		int time = std::atoi(args[1].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Time must be 0 (permanent) or positive.\n");
			return;
		}
		std::string reason = "Silenced";
		if (args.size() > 2)
		{
			reason.clear();
			for (size_t i = 2; i < args.size(); i++)
			{
				if (i > 2) reason += " ";
				reason += args[i];
			}
		}
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
			ADMIN_ReplyToCommand(slot, "Usage: !banip <ip> <time> [reason]\n");
			return;
		}

		const char *ip = args[0].c_str();
		int time = std::atoi(args[1].c_str());
		if (time < 0)
		{
			ADMIN_ReplyToCommand(slot, "Time must be 0 (permanent) or positive.\n");
			return;
		}
		std::string reason = "Banned";
		if (args.size() > 2)
		{
			reason.clear();
			for (size_t i = 2; i < args.size(); i++)
			{
				if (i > 2) reason += " ";
				reason += args[i];
			}
		}

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

		ADMIN_LogAction(slot, (std::string("Reported ") + targetPlayer->name + ": " + reason).c_str());
	});
}
