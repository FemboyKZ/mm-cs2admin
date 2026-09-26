#include "command_system.h"
#include "mmu/chat_command.h"
#include "mmu/kv_parser.h"
#include "mmu/log.h"
#include "map_manager.h"
#include "src/chat/chat_processor.h"
#include "src/compat/foreign_plugins.h"
#include "src/menu/menu_bridge.h"
#include "src/player/player_manager.h"
#include "src/tags/tag_manager.h"
#include "src/ban/ban_manager.h"
#include "src/comm/comm_manager.h"
#include "src/admin/admin_manager.h"
#include "src/config/config.h"
#include "src/db/database.h"
#include "interfaces/cs2admin/forwards.h"
#include "interfaces/cs2rockthevote/ics2rtv.h"
#include "mmu/maplist.h"
#include "src/lang/translations.h"
#include "src/utils/print_utils.h"
#include "src/utils/discord.h"
#include "mmu/entity/ccsplayercontroller.h"
#include "mmu/entity/ccsplayerpawn.h"

#include "tier0/logging.h"

#include <algorithm>
#include <ctime>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>

// Join args from startIdx into a single string, or return defaultVal if not enough args.
static std::string JoinArgs(const std::vector<std::string> &args, size_t start, const char *defaultVal)
{
	if (args.size() <= start)
	{
		return defaultVal;
	}
	std::string result;
	for (size_t i = start; i < args.size(); i++)
	{
		if (i > start)
		{
			result += " ";
		}
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
		{
			result += c;
		}
	}
	return result;
}

namespace
{
	class CCaptureLoggingListener : public ILoggingListener
	{
	public:
		void Log(const LoggingContext_t * /*pContext*/, const tchar *pMessage) override
		{
			if (!pMessage || !*pMessage)
			{
				return;
			}
			// Cap to avoid runaway growth from a chatty command.
			if (m_buffer.size() >= kMaxBytes)
			{
				m_truncated = true;
				return;
			}
			size_t remaining = kMaxBytes - m_buffer.size();
			size_t len = strlen(pMessage);
			if (len > remaining)
			{
				m_buffer.append(pMessage, remaining);
				m_truncated = true;
			}
			else
			{
				m_buffer.append(pMessage, len);
			}
		}

		const std::string &Buffer() const
		{
			return m_buffer;
		}

		bool Truncated() const
		{
			return m_truncated;
		}

	private:
		static constexpr size_t kMaxBytes = 8192;
		std::string m_buffer;
		bool m_truncated = false;
	};
} // namespace

// Send captured spew text back to the player's console, line by line.
static void ReplyConsoleOutput(int slot, const std::string &text, bool truncated)
{
	if (text.empty())
	{
		ADMIN_ReplyToCommandT(slot, "(no output)\n");
		return;
	}

	const size_t kMaxLines = 40;
	size_t printed = 0;
	size_t pos = 0;
	bool tooMany = false;
	while (pos < text.size())
	{
		size_t nl = text.find('\n', pos);
		std::string line = (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		if (!line.empty())
		{
			if (printed >= kMaxLines)
			{
				tooMany = true;
				break;
			}
			ADMIN_PrintToClientT(slot, "%s\n", line.c_str());
			printed++;
		}
		if (nl == std::string::npos)
		{
			break;
		}
		pos = nl + 1;
	}

	if (tooMany)
	{
		ADMIN_PrintToClientT(slot, "[output truncated: too many lines]\n");
	}
	else if (truncated)
	{
		ADMIN_PrintToClientT(slot, "[output truncated]\n");
	}
}

// Validate an IPv4 address string (e.g. "192.168.1.1").
static bool IsValidIPv4(const char *ip)
{
	if (!ip || !*ip)
	{
		return false;
	}
	int parts = 0;
	int num = 0;
	bool hasDigit = false;
	for (const char *p = ip;; p++)
	{
		if (*p >= '0' && *p <= '9')
		{
			num = num * 10 + (*p - '0');
			if (num > 255)
			{
				return false;
			}
			hasDigit = true;
		}
		else if (*p == '.' || *p == '\0')
		{
			if (!hasDigit)
			{
				return false;
			}
			parts++;
			if (*p == '\0')
			{
				break;
			}
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
	{
		return true; // Console bypasses immunity
	}

	if (callerSlot == targetSlot)
	{
		return true; // Can always target self
	}

	// Root flag bypasses immunity
	if (g_CS2AAdminManager.PlayerHasFlag(callerSlot, ADMFLAG_ROOT))
	{
		return true;
	}

	const AdminEntry *targetAdmin = g_CS2AAdminManager.GetPlayerAdmin(targetSlot);
	if (!targetAdmin)
	{
		return true; // Non-admin target, always allowed
	}

	const AdminEntry *callerAdmin = g_CS2AAdminManager.GetPlayerAdmin(callerSlot);
	int callerImm = callerAdmin ? callerAdmin->immunity : 0;
	int targetImm = targetAdmin->immunity;

	if (targetImm > 0 && callerImm <= targetImm)
	{
		ADMIN_ReplyToCommandT(callerSlot, "Cannot target this player (higher immunity).\n");
		return false;
	}
	return true;
}

// Same check against an admin record rather than a live slot, for a target who is not connected.
static bool CheckImmunityAgainst(int callerSlot, int targetImmunity)
{
	if (callerSlot < 0 || g_CS2AAdminManager.PlayerHasFlag(callerSlot, ADMFLAG_ROOT))
	{
		return true;
	}

	const AdminEntry *callerAdmin = g_CS2AAdminManager.GetPlayerAdmin(callerSlot);
	int callerImm = callerAdmin ? callerAdmin->immunity : 0;

	if (targetImmunity > 0 && callerImm <= targetImmunity)
	{
		ADMIN_ReplyToCommandT(callerSlot, "Cannot target this player (higher immunity).\n");
		return false;
	}
	return true;
}

// Read an argument as a SteamID in any accepted format and hand back the normalized STEAM_0:Y:Z form.
// Anything else is rejected, since the unban and addban queries match on the suffix and a LIKE wildcard would match every row.
static bool ParseSteamIDArg(const std::string &arg, std::string &out)
{
	std::string normalized = CS2AAdminManager::NormalizeSteamID(arg.c_str());

	unsigned int y = 0;
	unsigned int z = 0;
	int consumed = 0;
	if (sscanf(normalized.c_str(), "STEAM_0:%u:%u%n", &y, &z, &consumed) != 2 || consumed != (int)normalized.size())
	{
		return false;
	}

	out = std::move(normalized);
	return true;
}

// Who a Discord notice is about and who did it.
// Read before the action runs, since a ban's kick resets the target's PlayerInfo, and the admin's too when they ban themselves.
struct DiscordTarget
{
	std::string adminName;
	uint64_t adminSteamid64 = 0;
	std::string name;
	uint64_t steamid64 = 0;
};

static DiscordTarget CaptureDiscordTarget(int adminSlot, int targetSlot)
{
	DiscordTarget target;
	target.adminName = g_CS2APlayerManager.GetAdminName(adminSlot);
	if (PlayerInfo *admin = g_CS2APlayerManager.GetPlayer(adminSlot))
	{
		target.adminSteamid64 = admin->steamid64;
	}
	if (PlayerInfo *player = g_CS2APlayerManager.GetPlayer(targetSlot))
	{
		target.name = player->name;
		target.steamid64 = player->steamid64;
	}
	return target;
}

// Sent only once the action went through, so a forward that blocks it doesn't leave a notice for something that never happened.
// durationMinutes -1 leaves the duration line out.
static void NotifyDiscordOnPlayer(const char *action, const DiscordTarget &target, const char *reason = nullptr, int durationMinutes = -1)
{
	g_CS2ADiscord.NotifyAdminAction(target.adminName.c_str(), action, target.name.c_str(), reason, durationMinutes, target.adminSteamid64,
									target.steamid64);
}

// Names what a silence or unsilence actually did, given the COMM_MUTE/COMM_GAG bits it returned. Null when it did nothing.
static const char *CommActionName(int applied, const char *both, const char *mute, const char *gag)
{
	if (applied == (COMM_MUTE | COMM_GAG))
	{
		return both;
	}
	if (applied == COMM_MUTE)
	{
		return mute;
	}
	if (applied == COMM_GAG)
	{
		return gag;
	}
	return nullptr;
}

// Parses "[time] [reason]" after a block command's target.
// No time, or a word in its place, uses DefaultTime and the word starts the reason. A malformed number is still an error.
// minutes is 0 for permanent, -1 for session. False after replying.
static bool ParseBlockArgs(int slot, const std::vector<std::string> &args, const char *defaultReason, int &minutes, std::string &reason)
{
	size_t reasonStart = 1;
	if (args.size() >= 2 && std::isdigit(static_cast<unsigned char>(args[1][0])))
	{
		minutes = ADMIN_ParseDuration(args[1].c_str());
		if (minutes < 0)
		{
			ADMIN_ReplyToCommandT(slot, "Invalid time. Use minutes (e.g. 30) or suffixes: h(ours), d(ays), w(eeks), m(onths). 0 = permanent.\n");
			return false;
		}
		reasonStart = 2;
	}
	else
	{
		int defaultTime = g_CS2AConfig.commsDefaultTime;
		minutes = defaultTime < 0 ? -1 : (defaultTime == 0 ? 30 : defaultTime);
	}

	if (minutes >= 0 && !g_CS2ACommManager.IsAllowedLength(slot, minutes))
	{
		ADMIN_ReplyToCommandT(slot, "You cannot issue blocks longer than %s.\n", ADMIN_FormatDuration(g_CS2AConfig.commsMaxLength).c_str());
		return false;
	}

	reason = JoinArgs(args, reasonStart, defaultReason);
	return true;
}

// A SteamID in any format also covers offline players, anything else is an online target. False after replying.
static bool ResolveHistoryTarget(int slot, const std::string &arg, std::string &authid)
{
	if (ParseSteamIDArg(arg, authid))
	{
		return true;
	}
	int target = ADMIN_FindTarget(slot, arg.c_str());
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(target);
	if (!player)
	{
		return false;
	}
	authid = player->authid;
	return true;
}

// False after replying when the caller may not lift an active block of `types` (COMM_MUTE/COMM_GAG bits).
static bool CheckCanLift(int slot, int target, int types)
{
	if (((types & COMM_MUTE) && !g_CS2ACommManager.CanLiftBlock(slot, target, COMM_MUTE))
		|| ((types & COMM_GAG) && !g_CS2ACommManager.CanLiftBlock(slot, target, COMM_GAG)))
	{
		ADMIN_ReplyToCommandT(slot, "You cannot lift a block placed by an admin with higher immunity.\n");
		return false;
	}
	return true;
}

CS2ACommandSystem g_CS2ACommandSystem;

void CS2ACommandSystem::RegisterCommand(const char *name, const char *group, uint32_t defaultFlag, ChatCommandCallback callback)
{
	std::string lower(name);
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	m_commands[lower] = {group, defaultFlag, std::move(callback)};
}

bool CS2ACommandSystem::CanUse(int slot, const std::string &name, const Command &command) const
{
	return g_CS2AAdminManager.CanPlayerUseCommand(slot, name.c_str(), command.group, command.defaultFlag);
}

bool CS2ACommandSystem::CanRun(int slot, const char *name) const
{
	auto it = m_commands.find(name);
	return it != m_commands.end() && CanUse(slot, it->first, it->second);
}

void CS2ACommandSystem::Run(const std::string &name, const Command &command, int slot, const std::vector<std::string> &args, bool silent)
{
	if (!CanUse(slot, name, command))
	{
		ADMIN_ReplyToCommandT(slot, "You do not have permission to use this command.\n");
		return;
	}
	command.callback(slot, args, silent);
}

bool CS2ACommandSystem::ShouldBlockChat(int slot)
{
	return g_CS2ACommManager.IsGagged(slot);
}

void CS2ACommandSystem::PromptText(int slot, std::function<void(int slot, const std::string &text)> onText)
{
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (slot < 0 || slot > MAXPLAYERS || !player)
	{
		return;
	}
	m_prompts[slot] = {player->steamid64, Plat_FloatTime() + 60.0, std::move(onText)};
}

bool CS2ACommandSystem::PromptWaiting(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS || !m_prompts[slot].onText)
	{
		return false;
	}
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (!player || player->steamid64 != m_prompts[slot].steamid64 || Plat_FloatTime() > m_prompts[slot].expires)
	{
		m_prompts[slot] = {};
		return false;
	}
	return true;
}

bool CS2ACommandSystem::IsChatHidden(int slot)
{
	return PromptWaiting(slot) || (slot >= 0 && slot <= MAXPLAYERS && m_sayHidden[slot]);
}

void CS2ACommandSystem::EndSay(int slot)
{
	if (slot >= 0 && slot <= MAXPLAYERS)
	{
		m_sayHidden[slot] = false;
	}
}

bool CS2ACommandSystem::ConsumePromptedText(int slot, const char *message)
{
	if (!PromptWaiting(slot))
	{
		return false;
	}

	std::string text = mmu::StripSayQuotes(message);
	if (text.empty())
	{
		return false;
	}
	const bool prefixed =
		g_CS2AConfig.commandPrefix.find(text[0]) != std::string::npos || g_CS2AConfig.silentCommandPrefix.find(text[0]) != std::string::npos;
	std::string word = prefixed ? text.substr(1) : text;
	std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (word == "cancel")
	{
		m_prompts[slot] = {};
		m_sayHidden[slot] = true;
		ADMIN_ReplyToCommandT(slot, "Cancelled.\n");
		return true;
	}
	if (prefixed)
	{
		return false;
	}

	// Cleared first, since the callback may prompt again.
	auto onText = std::move(m_prompts[slot].onText);
	m_prompts[slot] = {};
	m_sayHidden[slot] = true;
	onText(slot, text);
	return true;
}

bool CS2ACommandSystem::ProcessChatMessage(int slot, const char *message, bool teamOnly, bool &silent)
{
	std::string msg = mmu::StripSayQuotes(message);
	if (msg.empty())
	{
		return false;
	}

	mmu::ChatCommand cmd;
	if (!mmu::ParseChatCommand(msg, g_CS2AConfig.commandPrefix, g_CS2AConfig.silentCommandPrefix, cmd))
	{
		return false;
	}

	auto it = m_commands.find(cmd.name);
	if (it == m_commands.end())
	{
		return false;
	}

	silent = cmd.silent;
	Run(it->first, it->second, slot, cmd.args, cmd.silent);
	return true;
}

// Menu flows (only reachable when mm-cs2menus is loaded).
//
// Each flow ends by re-dispatching the matching chat command with synthesized text args,
// so all permission / immunity / logging / Discord logic is reused.
// Targets are encoded as "$<steamid64>" (or "#<slot>" for bots) which ADMIN_FindTarget resolves back to a live slot.

namespace
{
	// Split a comma-separated config list, trimming whitespace around each entry.
	std::vector<std::string> SplitCsv(const std::string &raw)
	{
		std::vector<std::string> out;
		size_t pos = 0;
		while (pos <= raw.size())
		{
			size_t comma = raw.find(',', pos);
			std::string tok = (comma == std::string::npos) ? raw.substr(pos) : raw.substr(pos, comma - pos);

			size_t b = tok.find_first_not_of(" \t");
			size_t e = tok.find_last_not_of(" \t");
			if (b != std::string::npos)
			{
				out.push_back(tok.substr(b, e - b + 1));
			}

			if (comma == std::string::npos)
			{
				break;
			}
			pos = comma + 1;
		}
		return out;
	}

	// "12 minutes left", "permanent" or "session" for an active comm block.
	std::string BlockRemaining(bool session, double expires)
	{
		if (session)
		{
			return "session";
		}
		if (expires <= 0.0)
		{
			return "permanent";
		}
		int minutes = static_cast<int>(std::ceil((expires - Plat_FloatTime()) / 60.0));
		return ADMIN_FormatDuration((std::max)(1, minutes)) + " left";
	}

	struct TeamSection
	{
		int team;
		const char *name;
	};

	constexpr TeamSection kTeamSections[] = {{3, "Counter-Terrorists"}, {2, "Terrorists"}, {1, "Spectators"}, {0, "Unassigned"}};

	// Picker items for connected players, a section per team.
	// info = "$<steamid64>" for real players, "#<slot>" for bots.
	// commFilter (COMM_MUTE / COMM_GAG bits) greys out players without those blocks and shows the time left.
	std::vector<AdminMenuItem> BuildPlayerItems(int callerSlot, bool includeBots, bool excludeSelf, int commFilter = 0)
	{
		std::vector<AdminMenuItem> items;
		CGlobalVars *globals = GetGameGlobals();
		int maxClients = globals ? globals->maxClients : MAXPLAYERS;

		for (const TeamSection &section : kTeamSections)
		{
			for (int i = 0; i < maxClients; i++)
			{
				PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
				if (!p || !p->connected)
				{
					continue;
				}
				if (p->fakePlayer && !includeBots)
				{
					continue;
				}
				if (excludeSelf && i == callerSlot)
				{
					continue;
				}
				CCSPlayerController *controller = CCSPlayerController::FromSlot(i);
				int team = controller ? controller->m_iTeamNum() : 0;
				if (team != section.team && !(section.team == 0 && (team < 0 || team > 3)))
				{
					continue;
				}

				AdminMenuItem item;
				item.text = p->name;
				item.section = section.name;
				if (p->fakePlayer || p->steamid64 == 0)
				{
					item.info = "#" + std::to_string(i);
				}
				else
				{
					item.info = "$" + std::to_string(p->steamid64);
				}
				if (commFilter != 0)
				{
					std::vector<std::string> blocks;
					if ((commFilter & COMM_MUTE) && (p->isMuted || p->isSessionMuted))
					{
						blocks.push_back("mute " + BlockRemaining(p->isSessionMuted, p->muteExpireTime));
					}
					if ((commFilter & COMM_GAG) && (p->isGagged || p->isSessionGagged))
					{
						blocks.push_back("gag " + BlockRemaining(p->isSessionGagged, p->gagExpireTime));
					}
					item.disabled = blocks.empty();
					for (const std::string &block : blocks)
					{
						item.subtext += (item.subtext.empty() ? "" : ", ") + block;
					}
				}
				else if (team >= 2 && controller && !controller->m_bPawnIsAlive())
				{
					item.subtext = "Dead";
				}
				items.push_back(std::move(item));
			}
		}
		return items;
	}

	// Duration menu items, labelled via ADMIN_FormatDuration, info = the minutes value.
	std::vector<AdminMenuItem> BuildDurationItems()
	{
		std::vector<AdminMenuItem> items;
		for (const std::string &tok : SplitCsv(g_CS2AConfig.menuDurations))
		{
			int mins = ADMIN_ParseDuration(tok.c_str());
			if (mins < 0)
			{
				continue;
			}
			AdminMenuItem item;
			item.text = ADMIN_FormatDuration(mins);
			item.info = std::to_string(mins);
			items.push_back(std::move(item));
		}
		return items;
	}

	// Reason presets with "Other" last, which asks in chat.
	std::vector<std::string> ReasonOptions()
	{
		std::vector<std::string> reasons;
		for (const std::string &reason : SplitCsv(g_CS2AConfig.menuReasons))
		{
			std::string lower = reason;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (lower != "other")
			{
				reasons.push_back(reason);
			}
		}
		reasons.push_back("Other");
		return reasons;
	}

	// minutes is "" for an untimed action.
	using PunishFn = std::function<void(int slot, const std::string &minutes, const std::string &reason)>;

	// Duration when timed, Reason and a confirm row. "Other" takes the reason from the admin's next chat line.
	void ShowPunishForm(int slot, const std::string &title, const std::string &confirm, bool timed, PunishFn apply)
	{
		std::vector<AdminMenuItem> durations = timed ? BuildDurationItems() : std::vector<AdminMenuItem>();
		if (timed && durations.empty())
		{
			ADMIN_ReplyToCommandT(slot, "No menu durations configured. Check Menu > Durations in core.cfg\n");
			return;
		}
		std::vector<std::string> reasons = ReasonOptions();

		std::vector<AdminFormField> fields;
		if (timed)
		{
			AdminFormField duration;
			duration.text = "Duration";
			for (const AdminMenuItem &d : durations)
			{
				duration.options.push_back(d.text);
			}
			fields.push_back(std::move(duration));
		}
		AdminFormField reason;
		reason.text = "Reason";
		reason.options = reasons;
		fields.push_back(std::move(reason));

		g_AdminMenus.ShowForm(slot, title.c_str(), fields, confirm.c_str(),
							  [timed, durations, reasons, apply](int s, const std::vector<int> &values)
							  {
								  const std::string minutes = timed ? durations[values[0]].info : std::string();
								  const int choice = values.back();
								  if (choice < static_cast<int>(reasons.size()) - 1)
								  {
									  apply(s, minutes, reasons[choice]);
									  return;
								  }
								  ADMIN_ReplyToCommandT(s, "Type the reason in chat, or cancel to stop.\n");
								  g_CS2ACommandSystem.PromptText(s, [apply, minutes](int s2, const std::string &text) { apply(s2, minutes, text); });
							  });
	}

	// player -> duration and reason -> !<cmd> <target> <minutes> <reason>
	void StartTimedActionFlow(int slot, std::string cmd, std::string verb)
	{
		std::vector<AdminMenuItem> players = BuildPlayerItems(slot, false, false);
		if (players.empty())
		{
			ADMIN_ReplyToCommandT(slot, "No valid targets online.\n");
			return;
		}

		std::string pickTitle = verb + ": select player";
		g_AdminMenus.ShowMenu(slot, pickTitle.c_str(), players,
							  [cmd, verb, players](int s, int item, const std::string &target)
							  {
								  ShowPunishForm(s, verb + ": " + players[item].text, verb, true,
												 [cmd, target](int s2, const std::string &minutes, const std::string &reason)
												 {
													 std::vector<std::string> args = {target, minutes, reason};
													 g_CS2ACommandSystem.DispatchConsoleCommand(cmd.c_str(), args, s2);
												 });
							  });
	}

	// player -> reason -> !kick <target> <reason>
	void StartKickFlow(int slot)
	{
		std::vector<AdminMenuItem> players = BuildPlayerItems(slot, false, true);
		if (players.empty())
		{
			ADMIN_ReplyToCommandT(slot, "No valid targets online.\n");
			return;
		}

		g_AdminMenus.ShowMenu(slot, "Kick: select player", players,
							  [players](int s, int item, const std::string &target)
							  {
								  ShowPunishForm(s, "Kick: " + players[item].text, "Kick", false,
												 [target](int s2, const std::string &, const std::string &reason)
												 {
													 std::vector<std::string> args = {target, reason};
													 g_CS2ACommandSystem.DispatchConsoleCommand("kick", args, s2);
												 });
							  });
	}

	// player -> reason -> !report <target> <reason>
	void StartReportFlow(int slot)
	{
		std::vector<AdminMenuItem> players = BuildPlayerItems(slot, false, true);
		if (players.empty())
		{
			ADMIN_ReplyToCommandT(slot, "No valid targets online.\n");
			return;
		}

		g_AdminMenus.ShowMenu(slot, "Report: select player", players,
							  [players](int s, int item, const std::string &target)
							  {
								  ShowPunishForm(s, "Report: " + players[item].text, "Report", false,
												 [target](int s2, const std::string &, const std::string &reason)
												 {
													 std::vector<std::string> args = {target, reason};
													 g_CS2ACommandSystem.DispatchConsoleCommand("report", args, s2);
												 });
							  });
	}

	// "42s ago", "12m ago" or "3h ago".
	std::string FormatAgo(double seconds)
	{
		int secs = seconds > 0.0 ? static_cast<int>(seconds) : 0;
		if (secs < 60)
		{
			return std::to_string(secs) + "s ago";
		}
		if (secs < 3600)
		{
			return std::to_string(secs / 60) + "m ago";
		}
		return std::to_string(secs / 3600) + "h ago";
	}

	// recently disconnected -> duration and reason -> !addban <minutes> <steamid64> <reason>
	void StartOfflineBanFlow(int slot)
	{
		const auto &disconnected = g_CS2APlayerManager.GetDisconnectedPlayers();
		double now = Plat_FloatTime();
		std::vector<AdminMenuItem> items;
		// Most recent first.
		for (int i = static_cast<int>(disconnected.size()) - 1; i >= 0; i--)
		{
			const DisconnectedPlayer &dc = disconnected[i];
			if (dc.steamid64 == 0)
			{
				continue;
			}
			AdminMenuItem item;
			item.text = dc.name;
			item.info = std::to_string(dc.steamid64);
			item.subtext = dc.disconnectTime > 0.0 ? FormatAgo(now - dc.disconnectTime) : std::string();
			items.push_back(std::move(item));
		}
		if (items.empty())
		{
			ADMIN_ReplyToCommandT(slot, "No recently disconnected players.\n");
			return;
		}

		g_AdminMenus.ShowMenu(slot, "Add ban: select player", items,
							  [items](int s, int item, const std::string &steamid)
							  {
								  ShowPunishForm(s, "Ban: " + items[item].text, "Ban", true,
												 [steamid](int s2, const std::string &minutes, const std::string &reason)
												 {
													 std::vector<std::string> args = {minutes, steamid, reason};
													 g_CS2ACommandSystem.DispatchConsoleCommand("addban", args, s2);
												 });
							  });
	}

	// player -> !<cmd> <target>
	// commFilter as in BuildPlayerItems.
	void StartTargetOnlyFlow(int slot, std::string cmd, std::string verb, bool includeBots, bool excludeSelf, int commFilter = 0)
	{
		std::vector<AdminMenuItem> players = BuildPlayerItems(slot, includeBots, excludeSelf, commFilter);
		if (std::none_of(players.begin(), players.end(), [](const AdminMenuItem &p) { return !p.disabled; }))
		{
			ADMIN_ReplyToCommandT(slot, "No valid targets online.\n");
			return;
		}

		std::string title = verb + ": select player";
		g_AdminMenus.ShowMenu(slot, title.c_str(), players,
							  [cmd](int s, int, const std::string &target)
							  {
								  std::vector<std::string> args = {target};
								  g_CS2ACommandSystem.DispatchConsoleCommand(cmd.c_str(), args, s);
							  });
	}

	// rtv's !nominate list, empty without rtv.
	std::vector<AdminMenuItem> BuildRtvMapItems(int slot)
	{
		// Not cached, rtv may unload.
		ICS2RTV *rtv = static_cast<ICS2RTV *>(g_SMAPI->MetaFactory(CS2RTV_INTERFACE, nullptr, nullptr));
		std::vector<AdminMenuItem> items;
		if (!rtv)
		{
			return items;
		}
		const int count = rtv->GetMapCount();
		std::vector<std::string> names;
		std::vector<int> order;
		for (int i = 0; i < count; i++)
		{
			const char *display = rtv->GetMapDisplayName(i);
			names.emplace_back(display[0] ? display : rtv->GetMapName(i));
			order.push_back(i);
		}
		// Same order as rtv's SortMapsByName.
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return mmu::MapNameLess(names[a], names[b]); });

		const std::string current = rtv->GetCurrentMap();
		for (int i : order)
		{
			const std::string mapName = rtv->GetMapName(i);
			const std::string workshopId = rtv->GetMapWorkshopId(i);
			AdminMenuItem item;
			item.disabled = mapName == current;
			item.text = rtv->GetMapMenuLabel(i, item.disabled);
			if (item.disabled)
			{
				item.text += " " + ADMIN_Translate(slot, "[current]");
			}
			item.info = workshopId.empty() ? mapName : workshopId;
			items.push_back(std::move(item));
		}
		return items;
	}

	// map list -> !map <mapname|workshopid>
	void StartMapFlow(int slot)
	{
		std::vector<AdminMenuItem> items = BuildRtvMapItems(slot);
		if (items.empty())
		{
			for (const MapEntry *m : g_CS2AMapManager.GetSortedMaps())
			{
				AdminMenuItem item;
				item.text = m->displayName.empty() ? m->mapName : m->displayName;
				item.info = (m->isWorkshop && !m->workshopId.empty()) ? m->workshopId : m->mapName;
				items.push_back(std::move(item));
			}
		}
		if (items.empty())
		{
			ADMIN_ReplyToCommandT(slot, "No maps loaded. Check cfg/maplist.txt\n");
			return;
		}

		g_AdminMenus.ShowMenu(
			slot, "Change Map", items,
			[](int s, int, const std::string &mapArg)
			{
				std::vector<std::string> args = {mapArg};
				g_CS2ACommandSystem.DispatchConsoleCommand("map", args, s);
			},
			true);
	}

	// Giveable items grouped by category for the !give picker.
	// Classnames and display names follow CS2Fixes' weapon table.
	// Cosmetic knife variants are collapsed to a single "Knife" since GiveNamedItem("weapon_knife") covers them.
	struct WeaponEntry
	{
		const char *classname;
		const char *display;
		const char *category;
	};

	const WeaponEntry kWeapons[] = {
		{"weapon_deagle", "Desert Eagle", "Pistols"},
		{"weapon_elite", "Dual Berettas", "Pistols"},
		{"weapon_fiveseven", "Five-SeveN", "Pistols"},
		{"weapon_glock", "Glock-18", "Pistols"},
		{"weapon_tec9", "Tec-9", "Pistols"},
		{"weapon_hkp2000", "P2000", "Pistols"},
		{"weapon_p250", "P250", "Pistols"},
		{"weapon_cz75a", "CZ75-Auto", "Pistols"},
		{"weapon_usp_silencer", "USP-S", "Pistols"},
		{"weapon_revolver", "R8 Revolver", "Pistols"},

		{"weapon_ak47", "AK-47", "Rifles"},
		{"weapon_m4a1", "M4A4", "Rifles"},
		{"weapon_m4a1_silencer", "M4A1-S", "Rifles"},
		{"weapon_aug", "AUG", "Rifles"},
		{"weapon_sg556", "SG 553", "Rifles"},
		{"weapon_famas", "Famas", "Rifles"},
		{"weapon_galilar", "Galil AR", "Rifles"},

		{"weapon_awp", "AWP", "Snipers"},
		{"weapon_ssg08", "SSG 08", "Snipers"},
		{"weapon_scar20", "SCAR-20", "Snipers"},
		{"weapon_g3sg1", "G3SG1", "Snipers"},

		{"weapon_mac10", "MAC-10", "SMGs"},
		{"weapon_mp9", "MP9", "SMGs"},
		{"weapon_mp7", "MP7", "SMGs"},
		{"weapon_mp5sd", "MP5-SD", "SMGs"},
		{"weapon_ump45", "UMP-45", "SMGs"},
		{"weapon_p90", "P90", "SMGs"},
		{"weapon_bizon", "PP-Bizon", "SMGs"},

		{"weapon_nova", "Nova", "Heavy"},
		{"weapon_xm1014", "XM1014", "Heavy"},
		{"weapon_mag7", "MAG-7", "Heavy"},
		{"weapon_sawedoff", "Sawed-Off", "Heavy"},
		{"weapon_m249", "M249", "Heavy"},
		{"weapon_negev", "Negev", "Heavy"},

		{"weapon_hegrenade", "HE Grenade", "Grenades"},
		{"weapon_flashbang", "Flashbang", "Grenades"},
		{"weapon_smokegrenade", "Smoke Grenade", "Grenades"},
		{"weapon_molotov", "Molotov", "Grenades"},
		{"weapon_incgrenade", "Incendiary", "Grenades"},
		{"weapon_decoy", "Decoy", "Grenades"},

		{"item_kevlar", "Kevlar Vest", "Equipment"},
		{"item_assaultsuit", "Kevlar + Helmet", "Equipment"},
		{"item_defuser", "Defuser", "Equipment"},
		{"weapon_taser", "Zeus x27", "Equipment"},
		{"weapon_knife", "Knife", "Equipment"},
	};

	// 0 primary, 1 pistol, -1 anything else.
	int WeaponGearSlot(const char *classname)
	{
		for (const WeaponEntry &w : kWeapons)
		{
			if (strcmp(w.classname, classname) != 0)
			{
				continue;
			}
			if (!strcmp(w.category, "Pistols"))
			{
				return 1;
			}
			if (!strcmp(w.category, "Rifles") || !strcmp(w.category, "Snipers") || !strcmp(w.category, "SMGs") || !strcmp(w.category, "Heavy"))
			{
				return 0;
			}
			return -1;
		}
		return -1;
	}

	// A section per category, kWeapons keeps each one contiguous.
	std::vector<AdminMenuItem> BuildWeaponItems()
	{
		std::vector<AdminMenuItem> items;
		for (const WeaponEntry &w : kWeapons)
		{
			AdminMenuItem item;
			item.text = w.display;
			item.info = w.classname;
			item.section = w.category;
			// Icons are named after the classname without its prefix.
			const std::string classname = w.classname;
			item.image = classname.substr(classname.find('_') + 1);
			items.push_back(std::move(item));
		}
		return items;
	}

	// player -> weapon -> !give <target> <classname>
	void StartGiveFlow(int slot)
	{
		std::vector<AdminMenuItem> players = BuildPlayerItems(slot, true, false);
		if (players.empty())
		{
			ADMIN_ReplyToCommandT(slot, "No valid targets online.\n");
			return;
		}

		g_AdminMenus.ShowMenu(slot, "Give: select player", players,
							  [](int s, int, const std::string &target)
							  {
								  g_AdminMenus.ShowMenu(
									  s, "Give: select weapon", BuildWeaponItems(),
									  [target](int s2, int, const std::string &classname)
									  {
										  std::vector<std::string> args = {target, classname};
										  g_CS2ACommandSystem.DispatchConsoleCommand("give", args, s2);
									  },
									  false, true);
							  });
	}

	// The slot behind a picker's info, or -1 once that player is gone.
	int SlotFromTargetInfo(const std::string &info)
	{
		if (info.size() < 2)
		{
			return -1;
		}
		if (info[0] == '$')
		{
			return g_CS2APlayerManager.FindSlotBySteamID64(strtoull(info.c_str() + 1, nullptr, 10));
		}
		if (info[0] == '#')
		{
			int slot = atoi(info.c_str() + 1);
			PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slot);
			return p && p->connected ? slot : -1;
		}
		return -1;
	}

	// player -> applicable comm actions -> punish form, or the lift directly.
	void StartCommsFlow(int slot)
	{
		std::vector<AdminMenuItem> players = BuildPlayerItems(slot, false, false);
		if (players.empty())
		{
			ADMIN_ReplyToCommandT(slot, "No valid targets online.\n");
			return;
		}

		g_AdminMenus.ShowMenu(slot, "Gag / Mute: select player", players,
							  [players](int s, int item, const std::string &target)
							  {
								  PlayerInfo *p = g_CS2APlayerManager.GetPlayer(SlotFromTargetInfo(target));
								  if (!p)
								  {
									  ADMIN_ReplyToCommandT(s, "No valid targets online.\n");
									  return;
								  }
								  const bool muted = p->isMuted || p->isSessionMuted;
								  const bool gagged = p->isGagged || p->isSessionGagged;

								  struct Action
								  {
									  const char *cmd;
									  const char *verb;
									  bool timed;
									  bool show;
								  };
								  const Action actions[] = {
									  {"mute", "Mute", true, !muted},
									  {"unmute", "Unmute", false, muted},
									  {"gag", "Gag", true, !gagged},
									  {"ungag", "Ungag", false, gagged},
									  {"silence", "Silence", true, !muted || !gagged},
									  {"unsilence", "Unsilence", false, muted && gagged},
								  };
								  std::vector<AdminMenuItem> items;
								  for (const Action &action : actions)
								  {
									  if (action.show && g_CS2ACommandSystem.CanRun(s, action.cmd))
									  {
										  items.push_back({action.verb, action.cmd, false});
									  }
								  }
								  if (items.empty())
								  {
									  ADMIN_ReplyToCommandT(s, "You do not have permission to use this command.\n");
									  return;
								  }

								  const std::string name = players[item].text;
								  g_AdminMenus.ShowMenu(s, ("Gag / Mute: " + name).c_str(), items,
														[name, target](int s2, int, const std::string &cmd)
														{
															if (cmd != "mute" && cmd != "gag" && cmd != "silence")
															{
																std::vector<std::string> args = {target};
																g_CS2ACommandSystem.DispatchConsoleCommand(cmd.c_str(), args, s2);
																return;
															}
															std::string verb = cmd == "mute" ? "Mute" : (cmd == "gag" ? "Gag" : "Silence");
															ShowPunishForm(
																s2, verb + ": " + name, verb, true,
																[cmd, target](int s3, const std::string &minutes, const std::string &reason)
																{
																	std::vector<std::string> args = {target, minutes, reason};
																	g_CS2ACommandSystem.DispatchConsoleCommand(cmd.c_str(), args, s3);
																});
														});
							  });
	}

	// cfg/cs2admin/adminmenu_cfgs.txt, SourceMod's format.
	std::vector<AdminMenuItem> LoadMenuConfigs()
	{
		std::vector<AdminMenuItem> items;
		char path[512];
		snprintf(path, sizeof(path), "%s/cfg/cs2admin/adminmenu_cfgs.txt", g_SMAPI->GetBaseDir());
		kv::LoadFile(
			path,
			[](const std::string &, const std::string &key, const std::string &value, void *userdata)
			{
				// Entries include cfg/, exec runs from there.
				std::string file = key.rfind("cfg/", 0) == 0 ? key.substr(4) : key;
				static_cast<std::vector<AdminMenuItem> *>(userdata)->push_back({value.empty() ? key : value, file, false});
			},
			&items);
		return items;
	}

	// config list -> !execcfg <file>
	void StartExecCfgFlow(int slot)
	{
		std::vector<AdminMenuItem> items = LoadMenuConfigs();
		if (items.empty())
		{
			ADMIN_ReplyToCommandT(slot, "No configs listed. Add them to cfg/cs2admin/adminmenu_cfgs.txt\n");
			return;
		}
		g_AdminMenus.ShowMenu(slot, "Execute Config", items,
							  [](int s, int, const std::string &file)
							  {
								  std::vector<std::string> args = {file};
								  g_CS2ACommandSystem.DispatchConsoleCommand("execcfg", args, s);
							  });
	}

	// SourceMod's sm_admin: every admin tool the caller may use, in SourceMod's categories.
	struct AdminMenuEntry
	{
		const char *section;
		const char *label;
		const char *command; // its permission shows or hides the entry
		void (*open)(int slot);
	};

	const AdminMenuEntry kAdminMenu[] = {
		{"Player Commands", "Ban", "ban", [](int s) { StartTimedActionFlow(s, "ban", "Ban"); }},
		{"Player Commands", "Kick", "kick", [](int s) { StartKickFlow(s); }},
		{"Player Commands", "Slay", "slay", [](int s) { StartTargetOnlyFlow(s, "slay", "Slay", true, false); }},
		{"Player Commands", "Gag / Mute", "gag", [](int s) { StartCommsFlow(s); }},
		{"Player Commands", "Who", "who", [](int s) { StartTargetOnlyFlow(s, "who", "Who", false, false); }},
		{"Player Commands", "Give Weapon", "give", [](int s) { StartGiveFlow(s); }},
		{"Player Commands", "Strip Weapons", "strip", [](int s) { StartTargetOnlyFlow(s, "strip", "Strip", true, false); }},
		{"Player Commands", "Ban History", "listbans", [](int s) { StartTargetOnlyFlow(s, "listbans", "List Bans", false, false); }},
		{"Player Commands", "Comm History", "listcomms", [](int s) { StartTargetOnlyFlow(s, "listcomms", "List Comms", false, false); }},
		{"Player Commands", "Ban Disconnected", "addban", [](int s) { StartOfflineBanFlow(s); }},
		{"Server Commands", "Change Map", "map", [](int s) { StartMapFlow(s); }},
		{"Server Commands", "Reload Admins", "reloadadmins",
		 [](int s) { g_CS2ACommandSystem.DispatchConsoleCommand("reloadadmins", std::vector<std::string>(), s); }},
		{"Server Commands", "Execute Config", "execcfg", [](int s) { StartExecCfgFlow(s); }},
	};

	void StartAdminMenu(int slot)
	{
		std::vector<AdminMenuItem> items;
		for (int i = 0; i < static_cast<int>(sizeof(kAdminMenu) / sizeof(kAdminMenu[0])); i++)
		{
			const AdminMenuEntry &entry = kAdminMenu[i];
			// Offline bans can be switched off server-wide.
			if (!g_CS2ACommandSystem.CanRun(slot, entry.command) || (!strcmp(entry.command, "addban") && !g_CS2AConfig.addban))
			{
				continue;
			}
			AdminMenuItem item;
			item.text = entry.label;
			item.info = std::to_string(i);
			item.section = entry.section;
			items.push_back(std::move(item));
		}
		if (items.empty())
		{
			ADMIN_ReplyToCommandT(slot, "You do not have permission to use this command.\n");
			return;
		}
		g_AdminMenus.ShowMenu(slot, "Admin Menu", items, [](int s, int, const std::string &info) { kAdminMenu[atoi(info.c_str())].open(s); });
	}

	// True when slot is a real player and the menu plugin can render a picker.
	bool MenuPickerAvailable(int slot)
	{
		return slot >= 0 && slot <= MAXPLAYERS && g_AdminMenus.Available();
	}
} // namespace

void CS2ACommandSystem::RegisterBuiltinCommands()
{
	// !ban <target> <time> [reason]
	RegisterCommand("ban", "basebans", ADMFLAG_BAN,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty() && MenuPickerAvailable(slot))
						{
							StartTimedActionFlow(slot, "ban", "Ban");
							return;
						}

						if (args.size() < 2)
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !ban <target> <time> [reason] (time: minutes, or 1h/2d/1w/1m)\n");
							return;
						}

						int target = ADMIN_FindTarget(slot, args[0].c_str());
						if (target < 0)
						{
							return;
						}

						if (target == slot)
						{
							ADMIN_ReplyToCommandT(slot, "You cannot ban yourself.\n");
							return;
						}

						PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(target);
						if (targetPlayer && targetPlayer->fakePlayer)
						{
							ADMIN_ReplyToCommandT(slot, "Bots cannot be banned.\n");
							return;
						}

						if (!CheckImmunity(slot, target))
						{
							return;
						}

						int time = ADMIN_ParseDuration(args[1].c_str());
						if (time < 0)
						{
							ADMIN_ReplyToCommandT(
								slot, "Invalid time. Use minutes (e.g. 30) or suffixes: h(ours), d(ays), w(eeks), m(onths). 0 = permanent.\n");
							return;
						}
						std::string reason = JoinArgs(args, 2, "Banned");

						DiscordTarget discordTarget = CaptureDiscordTarget(slot, target);
						if (g_CS2ABanManager.BanPlayer(target, time, reason.c_str(), slot))
						{
							NotifyDiscordOnPlayer("Ban", discordTarget, reason.c_str(), time);
						}
					});

	// !unban <steamid>
	RegisterCommand("unban", "basebans", ADMFLAG_UNBAN,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (!g_CS2AConfig.unban)
						{
							ADMIN_ReplyToCommandT(slot, "This command is disabled on this server. Use the web panel instead.\n");
							return;
						}

						std::string authid;
						if (args.empty() || !ParseSteamIDArg(args[0], authid))
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !unban <steamid> (STEAM_0:X:Y, SteamID64 or [U:1:X])\n");
							return;
						}

						if (!g_CS2ABanManager.Unban(authid.c_str(), slot))
						{
							ADMIN_ReplyToCommandT(slot, "Database not available.\n");
							return;
						}

						PlayerInfo *adminPlayer = g_CS2APlayerManager.GetPlayer(slot);
						std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
						g_CS2ADiscord.NotifyAdminAction(adminName.c_str(), "Unban", authid.c_str(), nullptr, -1,
														adminPlayer ? adminPlayer->steamid64 : 0);
					});

	// !addban <time> <steamid> [reason] - offline ban by SteamID
	RegisterCommand("addban", "basebans", ADMFLAG_BAN,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (!g_CS2AConfig.addban)
						{
							ADMIN_ReplyToCommandT(slot, "This command is disabled on this server. Use the web panel instead.\n");
							return;
						}

						if (args.empty() && MenuPickerAvailable(slot))
						{
							StartOfflineBanFlow(slot);
							return;
						}

						if (args.size() < 2)
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !addban <time> <steamid> [reason] (time: minutes, or 1h/2d/1w/1m)\n");
							return;
						}

						int time = ADMIN_ParseDuration(args[0].c_str());
						if (time < 0)
						{
							ADMIN_ReplyToCommandT(
								slot, "Invalid time. Use minutes (e.g. 30) or suffixes: h(ours), d(ays), w(eeks), m(onths). 0 = permanent.\n");
							return;
						}

						std::string authid;
						if (!ParseSteamIDArg(args[1], authid))
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !addban <time> <steamid> [reason] (STEAM_0:X:Y, SteamID64 or [U:1:X])\n");
							return;
						}

						// An offline target still has an admin record to respect, so check whichever of the two we can see.
						int online = g_CS2APlayerManager.FindSlotBySteamID64(CS2AAdminManager::AuthIdToSteamID64(authid.c_str()));
						if (online >= 0)
						{
							if (!CheckImmunity(slot, online))
							{
								return;
							}
						}
						else
						{
							AdminEntry target;
							if (g_CS2AAdminManager.LookupAdmin(authid.c_str(), target) && !CheckImmunityAgainst(slot, target.immunity))
							{
								return;
							}
						}

						std::string reason = JoinArgs(args, 2, "Banned");
						std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
						PlayerInfo *adminPlayer = g_CS2APlayerManager.GetPlayer(slot);

						if (!g_CS2ABanManager.AddBan(authid.c_str(), time, reason.c_str(), slot))
						{
							ADMIN_ReplyToCommandT(slot, "Database not available.\n");
							return;
						}

						g_CS2ADiscord.NotifyAdminAction(adminName.c_str(), "AddBan", authid.c_str(), reason.c_str(), time,
														adminPlayer ? adminPlayer->steamid64 : 0);

						// The ban row alone would not touch them until they reconnect.
						if (online >= 0)
						{
							CS2ABanManager::PrintBanNotice(online, reason.c_str());
							g_pEngine->DisconnectClient(CPlayerSlot(online), NETWORK_DISCONNECT_KICKED_CONVICTEDACCOUNT);
						}
					});

	// !mute <target> <time> [reason]
	RegisterCommand("mute", "basecomm", ADMFLAG_CHAT,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty() && MenuPickerAvailable(slot))
						{
							StartTimedActionFlow(slot, "mute", "Mute");
							return;
						}

						if (args.empty())
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !mute <target> [time] [reason] (time: minutes, or 1h/2d/1w/1m)\n");
							return;
						}

						int target = ADMIN_FindTarget(slot, args[0].c_str());
						if (target < 0)
						{
							return;
						}

						if (!CheckImmunity(slot, target))
						{
							return;
						}

						int time = 0;
						std::string reason;
						if (!ParseBlockArgs(slot, args, "Muted", time, reason))
						{
							return;
						}

						DiscordTarget discordTarget = CaptureDiscordTarget(slot, target);
						if (time < 0)
						{
							g_CS2ACommManager.SessionMutePlayer(target, slot);
							NotifyDiscordOnPlayer("Session Mute", discordTarget, reason.c_str());
							return;
						}
						if (g_CS2ACommManager.MutePlayer(target, time, reason.c_str(), slot))
						{
							NotifyDiscordOnPlayer("Mute", discordTarget, reason.c_str(), time);
						}
					});

	// !unmute <target>
	RegisterCommand("unmute", "basecomm", ADMFLAG_CHAT,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							if (MenuPickerAvailable(slot))
							{
								StartTargetOnlyFlow(slot, "unmute", "Unmute", false, false, COMM_MUTE);
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Usage: !unmute <target>\n");
							return;
						}

						int target = ADMIN_FindTarget(slot, args[0].c_str());
						if (target < 0)
						{
							return;
						}

						if (!CheckImmunity(slot, target) || !CheckCanLift(slot, target, COMM_MUTE))
						{
							return;
						}

						DiscordTarget discordTarget = CaptureDiscordTarget(slot, target);
						if (!g_CS2ACommManager.UnmutePlayer(target, slot))
						{
							ADMIN_ReplyToCommandT(slot, "%s is not muted.\n", discordTarget.name.c_str());
							return;
						}
						NotifyDiscordOnPlayer("Unmute", discordTarget);
					});

	// !gag <target> <time> [reason]
	RegisterCommand("gag", "basecomm", ADMFLAG_CHAT,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty() && MenuPickerAvailable(slot))
						{
							StartTimedActionFlow(slot, "gag", "Gag");
							return;
						}

						if (args.empty())
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !gag <target> [time] [reason] (time: minutes, or 1h/2d/1w/1m)\n");
							return;
						}

						int target = ADMIN_FindTarget(slot, args[0].c_str());
						if (target < 0)
						{
							return;
						}

						if (!CheckImmunity(slot, target))
						{
							return;
						}

						int time = 0;
						std::string reason;
						if (!ParseBlockArgs(slot, args, "Gagged", time, reason))
						{
							return;
						}

						DiscordTarget discordTarget = CaptureDiscordTarget(slot, target);
						if (time < 0)
						{
							g_CS2ACommManager.SessionGagPlayer(target, slot);
							NotifyDiscordOnPlayer("Session Gag", discordTarget, reason.c_str());
							return;
						}
						if (g_CS2ACommManager.GagPlayer(target, time, reason.c_str(), slot))
						{
							NotifyDiscordOnPlayer("Gag", discordTarget, reason.c_str(), time);
						}
					});

	// !ungag <target>
	RegisterCommand("ungag", "basecomm", ADMFLAG_CHAT,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							if (MenuPickerAvailable(slot))
							{
								StartTargetOnlyFlow(slot, "ungag", "Ungag", false, false, COMM_GAG);
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Usage: !ungag <target>\n");
							return;
						}

						int target = ADMIN_FindTarget(slot, args[0].c_str());
						if (target < 0)
						{
							return;
						}

						if (!CheckImmunity(slot, target) || !CheckCanLift(slot, target, COMM_GAG))
						{
							return;
						}

						DiscordTarget discordTarget = CaptureDiscordTarget(slot, target);
						if (!g_CS2ACommManager.UngagPlayer(target, slot))
						{
							ADMIN_ReplyToCommandT(slot, "%s is not gagged.\n", discordTarget.name.c_str());
							return;
						}
						NotifyDiscordOnPlayer("Ungag", discordTarget);
					});

	// !silence <target> <time> [reason]
	RegisterCommand("silence", "basecomm", ADMFLAG_CHAT,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty() && MenuPickerAvailable(slot))
						{
							StartTimedActionFlow(slot, "silence", "Silence");
							return;
						}

						if (args.empty())
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !silence <target> [time] [reason] (time: minutes, or 1h/2d/1w/1m)\n");
							return;
						}

						int target = ADMIN_FindTarget(slot, args[0].c_str());
						if (target < 0)
						{
							return;
						}

						if (!CheckImmunity(slot, target))
						{
							return;
						}

						int time = 0;
						std::string reason;
						if (!ParseBlockArgs(slot, args, "Silenced", time, reason))
						{
							return;
						}

						DiscordTarget discordTarget = CaptureDiscordTarget(slot, target);
						if (time < 0)
						{
							g_CS2ACommManager.SessionMutePlayer(target, slot);
							g_CS2ACommManager.SessionGagPlayer(target, slot);
							NotifyDiscordOnPlayer("Session Silence", discordTarget, reason.c_str());
							return;
						}
						int applied = g_CS2ACommManager.SilencePlayer(target, time, reason.c_str(), slot);
						if (const char *action = CommActionName(applied, "Silence", "Mute", "Gag"))
						{
							NotifyDiscordOnPlayer(action, discordTarget, reason.c_str(), time);
						}
					});

	// !unsilence <target>
	RegisterCommand("unsilence", "basecomm", ADMFLAG_CHAT,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							if (MenuPickerAvailable(slot))
							{
								StartTargetOnlyFlow(slot, "unsilence", "Unsilence", false, false, COMM_MUTE | COMM_GAG);
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Usage: !unsilence <target>\n");
							return;
						}

						int target = ADMIN_FindTarget(slot, args[0].c_str());
						if (target < 0)
						{
							return;
						}

						if (!CheckImmunity(slot, target) || !CheckCanLift(slot, target, COMM_MUTE | COMM_GAG))
						{
							return;
						}

						DiscordTarget discordTarget = CaptureDiscordTarget(slot, target);
						int lifted = g_CS2ACommManager.UnsilencePlayer(target, slot);
						const char *action = CommActionName(lifted, "Unsilence", "Unmute", "Ungag");
						if (!action)
						{
							ADMIN_ReplyToCommandT(slot, "%s is not muted or gagged.\n", discordTarget.name.c_str());
							return;
						}
						NotifyDiscordOnPlayer(action, discordTarget);
					});

	// !banip <ip> <time> [reason]
	RegisterCommand(
		"banip", "basebans", ADMFLAG_BAN,
		[](int slot, const std::vector<std::string> &args, bool silent)
		{
			if (args.size() < 2)
			{
				ADMIN_ReplyToCommandT(slot, "Usage: !banip <ip> <time> [reason] (time: minutes, or 1h/2d/1w/1m)\n");
				return;
			}

			const char *ip = args[0].c_str();
			if (!IsValidIPv4(ip))
			{
				ADMIN_ReplyToCommandT(slot, "Invalid IP address format.\n");
				return;
			}

			int time = ADMIN_ParseDuration(args[1].c_str());
			if (time < 0)
			{
				ADMIN_ReplyToCommandT(slot, "Invalid time. Use minutes (e.g. 30) or suffixes: h(ours), d(ays), w(eeks), m(onths). 0 = permanent.\n");
				return;
			}
			std::string reason = JoinArgs(args, 2, "Banned");

			// The reconnect ban check runs before admin rights are assigned, so an IP ban locks a higher-immunity admin out permanently.
			// One protected player on that address refuses the whole command.
			for (int i = 0; i <= MAXPLAYERS; i++)
			{
				PlayerInfo *player = g_CS2APlayerManager.GetPlayer(i);
				if (!player || player->fakePlayer || player->ip != ip)
				{
					continue;
				}
				if (!CheckImmunity(slot, i))
				{
					return;
				}
			}

			if (!g_CS2ABanManager.BanIP(ip, time, reason.c_str(), slot))
			{
				ADMIN_ReplyToCommandT(slot, "Database not available.\n");
				return;
			}

			PlayerInfo *adminPlayer = g_CS2APlayerManager.GetPlayer(slot);
			std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
			g_CS2ADiscord.NotifyAdminAction(adminName.c_str(), "BanIP", ip, reason.c_str(), time, adminPlayer ? adminPlayer->steamid64 : 0);
		});

	// !comms [target] - check comm status
	RegisterCommand("comms", "basecomm", ADMFLAG_CHAT,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						int target = slot;
						if (!args.empty())
						{
							target = ADMIN_FindTarget(slot, args[0].c_str());
							if (target < 0)
							{
								return;
							}
						}

						g_CS2ACommManager.PrintCommsStatus(target, slot);
					});

	// !listbans <target>
	RegisterCommand("listbans", "basebans", ADMFLAG_BAN,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							if (MenuPickerAvailable(slot))
							{
								StartTargetOnlyFlow(slot, "listbans", "List Bans", false, false);
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Usage: !listbans <target|steamid>\n");
							return;
						}

						std::string authid;
						if (ResolveHistoryTarget(slot, args[0], authid))
						{
							g_CS2ABanManager.ListBans(slot, authid.c_str());
						}
					});

	// !listcomms <target>
	RegisterCommand("listcomms", "basecomm", ADMFLAG_CHAT,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							if (MenuPickerAvailable(slot))
							{
								StartTargetOnlyFlow(slot, "listcomms", "List Comms", false, false);
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Usage: !listcomms <target|steamid>\n");
							return;
						}

						std::string authid;
						if (ResolveHistoryTarget(slot, args[0], authid))
						{
							g_CS2ABanManager.ListComms(slot, authid.c_str());
						}
					});

	// !report <target> <reason>
	RegisterCommand("report", "cs2admin", ADMFLAG_NONE,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (slot < 0)
						{
							ADMIN_ReplyToCommandT(slot, "This command cannot be used from console.\n");
							return;
						}

						if (args.empty() && MenuPickerAvailable(slot))
						{
							StartReportFlow(slot);
							return;
						}

						if (args.size() < 2)
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !report <target> <reason>\n");
							return;
						}

						PlayerInfo *reporter = g_CS2APlayerManager.GetPlayer(slot);
						if (!reporter || !reporter->connected)
						{
							return;
						}

						// Cooldown check
						double now = Plat_FloatTime();
						if (reporter->lastReportTime > 0.0 && (now - reporter->lastReportTime) < g_CS2AConfig.reportCooldown)
						{
							int remaining = (int)(g_CS2AConfig.reportCooldown - (now - reporter->lastReportTime));
							ADMIN_ReplyToCommandT(slot, "You must wait %d seconds before reporting again.\n", remaining);
							return;
						}

						int target = ADMIN_FindTarget(slot, args[0].c_str());
						if (target < 0)
						{
							return;
						}

						if (target == slot)
						{
							ADMIN_ReplyToCommandT(slot, "You cannot report yourself.\n");
							return;
						}

						PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(target);
						if (!targetPlayer || !targetPlayer->connected)
						{
							return;
						}

						// Build reason from remaining args
						std::string reason;
						for (size_t i = 1; i < args.size(); i++)
						{
							if (i > 1)
							{
								reason += " ";
							}
							reason += args[i];
						}

						if ((int)reason.size() < g_CS2AConfig.reportMinLength)
						{
							ADMIN_ReplyToCommandT(slot, "Report reason must be at least %d characters.\n", g_CS2AConfig.reportMinLength);
							return;
						}

						// Cap reason length to prevent buffer overflow in query
						if (reason.size() > 512)
						{
							reason.resize(512);
						}

						g_CS2AForwards.FireOnReportPlayer(slot, target, reason.c_str());

						reporter->lastReportTime = now;

						if (g_CS2ADatabase.IsConnected())
						{
							// SteamId/name are the suspect and subname/ip the submitter, the way the SourceBans panel writes them.
							// ModID is NOT NULL in both schemas, so leaving it out makes strict mode reject the row.
							std::string submitter = reporter->name + " (" + reporter->authid + ")";
							ADMIN_TruncateUtf8(submitter, 128);

							std::string escTargetAuth = g_CS2ADatabase.Escape(targetPlayer->authid.c_str());
							std::string escTargetName = g_CS2ADatabase.Escape(targetPlayer->name.c_str());
							std::string escTargetIP = g_CS2ADatabase.Escape(targetPlayer->ip.c_str());
							std::string escSubmitter = g_CS2ADatabase.Escape(submitter.c_str());
							std::string escSubmitterIP = g_CS2ADatabase.Escape(reporter->ip.c_str());
							std::string escReason = g_CS2ADatabase.Escape(reason.c_str());

							long long now = (long long)std::time(nullptr);
							char query[4096];
							snprintf(query, sizeof(query),
									 "INSERT INTO %s_submissions (submitted, ModID, SteamId, name, email, reason, ip, subname, sip, server) "
									 "VALUES (%lld, 0, '%s', '%s', '', '%s', '%s', '%s', '%s', %d)",
									 g_CS2AConfig.database.prefix.c_str(), now, escTargetAuth.c_str(), escTargetName.c_str(), escReason.c_str(),
									 escSubmitterIP.c_str(), escSubmitter.c_str(), escTargetIP.c_str(), g_CS2ABanManager.GetServerID());

							g_CS2ADatabase.Query(query, [](ISQLQuery *) {});
						}

						ADMIN_ChatToAdminsT("%s reported %s: %s\n", reporter->name.c_str(), targetPlayer->name.c_str(), reason.c_str());
						ADMIN_ReplyToCommandT(slot, "Report submitted against %s.\n", targetPlayer->name.c_str());

						g_CS2ADiscord.NotifyReport(reporter->name.c_str(), targetPlayer->name.c_str(), reason.c_str(), reporter->steamid64,
												   targetPlayer->steamid64);

						ADMIN_LogAction(slot, (std::string("Reported ") + targetPlayer->name + ": " + reason).c_str());
					});

	// !kick <target> [reason]
	RegisterCommand("kick", "basecommands", ADMFLAG_KICK,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							if (MenuPickerAvailable(slot))
							{
								StartKickFlow(slot);
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Usage: !kick <target> [reason]\n");
							return;
						}

						int target = ADMIN_FindTarget(slot, args[0].c_str());
						if (target < 0)
						{
							return;
						}

						if (target == slot)
						{
							ADMIN_ReplyToCommandT(slot, "You cannot kick yourself.\n");
							return;
						}

						if (!CheckImmunity(slot, target))
						{
							return;
						}

						PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(target);
						if (!targetPlayer || !targetPlayer->connected)
						{
							return;
						}

						std::string reason = JoinArgs(args, 1, "Kicked by admin");

						if (g_CS2AForwards.FireOnKickPlayer(target, slot, reason.c_str()))
						{
							return;
						}

						std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
						PlayerInfo *adminPlayer = g_CS2APlayerManager.GetPlayer(slot);

						ADMIN_ChatToAllT("%s kicked %s. Reason: %s\n", adminName.c_str(), targetPlayer->name.c_str(), reason.c_str());

						g_CS2ADiscord.NotifyAdminAction(adminName.c_str(), "Kick", targetPlayer->name.c_str(), reason.c_str(), -1,
														adminPlayer ? adminPlayer->steamid64 : 0, targetPlayer->steamid64);

						ADMIN_LogAction(slot, (std::string("Kicked ") + targetPlayer->name + ": " + reason).c_str());

						ADMIN_PrintToClientT(target, "[ADMIN] You have been kicked. Reason: %s\n", reason.c_str());
						g_pEngine->DisconnectClient(CPlayerSlot(target), NETWORK_DISCONNECT_KICKED);
					});

	// !slay <target>
	RegisterCommand("slay", "playercommands", ADMFLAG_SLAY,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							if (MenuPickerAvailable(slot))
							{
								StartTargetOnlyFlow(slot, "slay", "Slay", true, false);
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Usage: !slay <target>\n");
							return;
						}

						TargetResult targets = ADMIN_FindTargets(slot, args[0].c_str());
						if (!targets.error.empty())
						{
							ADMIN_ReplyToCommandT(slot, "%s\n", ADMIN_Translate(slot, targets.error.c_str()).c_str());
							return;
						}

						std::string adminName = g_CS2APlayerManager.GetAdminName(slot);

						int slayed = 0;
						for (int targetSlot : targets.slots)
						{
							if (!CheckImmunity(slot, targetSlot))
							{
								continue;
							}

							PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(targetSlot);
							if (!targetPlayer || !targetPlayer->connected)
							{
								continue;
							}

							if (g_CS2AForwards.FireOnSlayPlayer(targetSlot, slot))
							{
								continue;
							}

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
								ADMIN_ChatToAllT("%s slayed %d players.\n", adminName.c_str(), slayed);
								ADMIN_LogAction(slot, (std::string("Slayed ") + std::to_string(slayed) + " players").c_str());
							}
							else
							{
								PlayerInfo *tp = g_CS2APlayerManager.GetPlayer(targets.slots[0]);
								std::string targetName = tp ? tp->name : "Unknown";
								ADMIN_ChatToAllT("%s slayed %s.\n", adminName.c_str(), targetName.c_str());
								ADMIN_LogAction(slot, (std::string("Slayed ") + targetName).c_str());
							}
						}
					});

	// !admin - Admin menu
	RegisterCommand("admin", "adminmenu", ADMFLAG_GENERIC,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (!MenuPickerAvailable(slot))
						{
							ADMIN_ReplyToCommandT(slot, "The admin menu needs the mm-cs2menus plugin.\n");
							return;
						}
						StartAdminMenu(slot);
					});

	// !reloadadmins - Rebuild the admin cache
	RegisterCommand("reloadadmins", "basecommands", ADMFLAG_BAN,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						g_CS2AAdminManager.ReloadAdmins();
						ADMIN_ReplyToCommandT(slot, "Admin cache has been refreshed.\n");
						ADMIN_LogAction(slot, "Reloaded admins");
					});

	// !execcfg [file] - Execute a config under cfg/
	RegisterCommand("execcfg", "basecommands", ADMFLAG_CONFIG,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							if (MenuPickerAvailable(slot))
							{
								StartExecCfgFlow(slot);
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Usage: !execcfg <file>\n");
							return;
						}
						// Only a plain relative path, it goes on a server command line.
						const std::string &file = args[0];
						bool valid = !file.empty() && file.find("..") == std::string::npos && file[0] != '/' && file[0] != '\\';
						for (char c : file)
						{
							valid = valid && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/');
						}
						if (!valid)
						{
							ADMIN_ReplyToCommandT(slot, "Invalid config file '%s'.\n", file.c_str());
							return;
						}
						// exec only reports to the server console.
						if (!std::ifstream(std::string(g_SMAPI->GetBaseDir()) + "/cfg/" + file).good())
						{
							ADMIN_ReplyToCommandT(slot, "Config not found: %s\n", file.c_str());
							return;
						}
						g_pEngine->ServerCommand(("exec " + file + "\n").c_str());
						ADMIN_ReplyToCommandT(slot, "Executed config %s.\n", file.c_str());
						ADMIN_LogAction(slot, ("Executed config " + file).c_str());
					});

	// !who [target] - List online admins, or one player's admin info
	RegisterCommand("who", "basecommands", ADMFLAG_GENERIC,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (!args.empty())
						{
							int target = ADMIN_FindTarget(slot, args[0].c_str());
							PlayerInfo *p = target >= 0 ? g_CS2APlayerManager.GetPlayer(target) : nullptr;
							if (!p)
							{
								return;
							}
							const AdminEntry *admin = g_CS2AAdminManager.GetPlayerAdmin(target);
							if (!admin)
							{
								ADMIN_ReplyToCommandT(slot, "%s is not an admin.\n", p->name.c_str());
								return;
							}
							std::string group;
							for (const std::string &groupName : admin->groups)
							{
								group += (group.empty() ? "" : ", ") + groupName;
							}
							ADMIN_ReplyToCommandT(slot, "  %s [%s] flags: %s imm: %d\n", p->name.c_str(),
												  group.empty() ? "(no group)" : group.c_str(), CS2AAdminManager::FlagsToString(admin->flags).c_str(),
												  admin->immunity);
							return;
						}

						CGlobalVars *globals = GetGameGlobals();
						int maxClients = globals ? globals->maxClients : MAXPLAYERS;

						ADMIN_ReplyToCommandT(slot, "Online Admins:\n");
						int count = 0;

						for (int i = 0; i < maxClients; i++)
						{
							PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
							if (!p || !p->connected || p->fakePlayer)
							{
								continue;
							}

							const AdminEntry *admin = g_CS2AAdminManager.GetPlayerAdmin(i);
							if (!admin)
							{
								continue;
							}

							std::string flags = CS2AAdminManager::FlagsToString(admin->flags);
							std::string group;
							for (const std::string &groupName : admin->groups)
							{
								group += (group.empty() ? "" : ", ") + groupName;
							}
							if (group.empty())
							{
								group = "(no group)";
							}
							int immunity = admin->immunity;

							ADMIN_ReplyToCommandT(slot, "  %s [%s] flags: %s imm: %d\n", p->name.c_str(), group.c_str(), flags.c_str(), immunity);
							count++;
						}

						if (count == 0)
						{
							ADMIN_ReplyToCommandT(slot, "  No admins currently online.\n");
						}
						else
						{
							ADMIN_ReplyToCommandT(slot, "%d admin(s) online\n", count);
						}
					});

	// !tag [id] - Pick which of your tags is displayed, or open a picker
	RegisterCommand("tag", "cs2admin", ADMFLAG_NONE,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (slot < 0)
						{
							ADMIN_ReplyToCommandT(slot, "This command cannot be used from the server console.\n");
							return;
						}

						// Nothing to pick when neither surface can show a tag.
						// Config asking for them isn't enough, another plugin may own both.
						if (!ADMIN_ChatTagsActive() && !ADMIN_BoardTagsActive())
						{
							const char *chatOwner = g_CS2AForeignPlugins.ChatOwner();
							const char *boardOwner = g_CS2AForeignPlugins.ClanTagOwner();
							const char *blocker = chatOwner ? chatOwner : boardOwner;

							if (blocker && (g_CS2AConfig.chatTagsEnabled || g_CS2AConfig.boardTagsEnabled))
							{
								ADMIN_ReplyToCommandT(slot, "Tags are unavailable while %s is loaded.\n", blocker);
							}
							else
							{
								ADMIN_ReplyToCommandT(slot, "Tags are disabled on this server.\n");
							}
							return;
						}

						std::vector<const TagDef *> eligible = g_CS2ATagManager.EligibleFor(slot);
						if (eligible.empty())
						{
							ADMIN_ReplyToCommandT(slot, "You do not have any tags.\n");
							return;
						}

						// Text form: !tag <id>, or !tag none to show nothing.
						if (!args.empty())
						{
							const std::string &want = args[0];
							if (want == "none" || want == "off")
							{
								g_CS2ATagManager.SelectTag(slot, "");
								ADMIN_ReplyToCommandT(slot, "Your tag is now hidden.\n");
								return;
							}

							if (!g_CS2ATagManager.SelectTag(slot, want.c_str()))
							{
								ADMIN_ReplyToCommandT(slot, "You do not have a tag called \"%s\".\n", want.c_str());
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Your tag is now \"%s\".\n", want.c_str());
							return;
						}

						if (!g_AdminMenus.Available())
						{
							ADMIN_ReplyToCommandT(slot, "Your tags:\n");
							for (const TagDef *tag : eligible)
							{
								ADMIN_ReplyToCommandT(slot, "  %s\n", tag->id.c_str());
							}
							ADMIN_ReplyToCommandT(slot, "Use %stag <name>, or %stag none to hide it.\n", g_CS2AConfig.commandPrefix.c_str(),
												  g_CS2AConfig.commandPrefix.c_str());
							return;
						}

						const TagDef *shown = g_CS2ATagManager.Resolve(slot);
						std::vector<AdminMenuItem> items;
						for (const TagDef *tag : eligible)
						{
							AdminMenuItem item;
							item.text = tag->id;
							item.info = tag->id;
							item.subtext = tag == shown ? "current" : "";
							items.push_back(item);
						}
						// Empty info is what SelectTag reads as "show nothing".
						AdminMenuItem none;
						none.text = "No tag";
						none.info = "";
						none.subtext = shown ? "" : "current";
						items.push_back(none);

						g_AdminMenus.ShowMenu(slot, "Choose your tag", items,
											  [](int s, int, const std::string &info)
											  {
												  if (!g_CS2ATagManager.SelectTag(s, info.c_str()))
												  {
													  ADMIN_ReplyToCommandT(s, "That tag is no longer available to you.\n");
													  return;
												  }
												  if (info.empty())
												  {
													  ADMIN_ReplyToCommandT(s, "Your tag is now hidden.\n");
												  }
												  else
												  {
													  ADMIN_ReplyToCommandT(s, "Your tag is now \"%s\".\n", info.c_str());
												  }
											  });
					});

	// !listdc - Show recently disconnected players
	RegisterCommand("listdc", "basecommands", ADMFLAG_BAN,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						const auto &disconnected = g_CS2APlayerManager.GetDisconnectedPlayers();

						if (disconnected.empty())
						{
							ADMIN_ReplyToCommandT(slot, "No recently disconnected players.\n");
							return;
						}

						double curtime = Plat_FloatTime();

						ADMIN_ReplyToCommandT(slot, "Recently Disconnected Players:\n");

						// Show most recent first
						for (int i = (int)disconnected.size() - 1; i >= 0; i--)
						{
							const DisconnectedPlayer &dc = disconnected[i];

							std::string authid = SteamID64ToAuthId(dc.steamid64);
							std::string timeAgo = FormatAgo(curtime > 0.0 && dc.disconnectTime > 0.0 ? curtime - dc.disconnectTime : 0.0);

							ADMIN_ReplyToCommandT(slot, "  %s (%s) [%s] - %s\n", dc.name.c_str(), authid.c_str(), dc.ip.c_str(), timeAgo.c_str());
						}

						ADMIN_ReplyToCommandT(slot, "%d player(s) recently disconnected\n", (int)disconnected.size());
					});

	// !adminhelp [page] - List all available commands
	RegisterCommand("adminhelp", "cs2admin", ADMFLAG_NONE,
					[this](int slot, const std::vector<std::string> &args, bool silent)
					{
						int page = 1;
						if (!args.empty())
						{
							page = std::atoi(args[0].c_str());
							if (page < 1)
							{
								page = 1;
							}
						}

						std::vector<std::string> cmds;
						cmds.reserve(m_commands.size());
						for (const auto &pair : m_commands)
						{
							if (CanUse(slot, pair.first, pair.second))
							{
								cmds.push_back(pair.first);
							}
						}

						std::sort(cmds.begin(), cmds.end());

						const int perPage = 8;
						int totalPages = ((int)cmds.size() + perPage - 1) / perPage;
						if (page > totalPages)
						{
							page = totalPages;
						}

						int startIdx = (page - 1) * perPage;
						int endIdx = startIdx + perPage;
						if (endIdx > (int)cmds.size())
						{
							endIdx = (int)cmds.size();
						}

						ADMIN_ReplyToCommandT(slot, "Commands (page %d/%d):\n", page, totalPages);
						for (int i = startIdx; i < endIdx; i++)
						{
							ADMIN_ReplyToCommandT(slot, "  %s%s\n", g_CS2AConfig.commandPrefix.c_str(), cmds[i].c_str());
						}
						if (page < totalPages)
						{
							ADMIN_ReplyToCommandT(slot, "Use !adminhelp %d for next page.\n", page + 1);
						}
					});

	// !find <text> - Search commands by name
	RegisterCommand("find", "cs2admin", ADMFLAG_NONE,
					[this](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !find <text>\n");
							return;
						}

						std::string search = args[0];
						std::transform(search.begin(), search.end(), search.begin(),
									   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

						std::vector<std::string> matches;
						for (const auto &pair : m_commands)
						{
							if (pair.first.find(search) != std::string::npos && CanUse(slot, pair.first, pair.second))
							{
								matches.push_back(pair.first);
							}
						}

						if (matches.empty())
						{
							ADMIN_ReplyToCommandT(slot, "No commands found matching '%s'.\n", args[0].c_str());
							return;
						}

						std::sort(matches.begin(), matches.end());
						ADMIN_ReplyToCommandT(slot, "Commands matching '%s':\n", args[0].c_str());
						for (const auto &cmd : matches)
						{
							ADMIN_ReplyToCommandT(slot, "  %s%s\n", g_CS2AConfig.commandPrefix.c_str(), cmd.c_str());
						}
					});

	// !rcon <command> - Execute a server console command
	RegisterCommand("rcon", "basecommands", ADMFLAG_RCON,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !rcon <command>\n");
							return;
						}

						std::string cmd = JoinArgs(args, 0, "");

						// Strip newlines to prevent command injection after the terminator
						cmd.erase(std::remove(cmd.begin(), cmd.end(), '\n'), cmd.end());
						cmd.erase(std::remove(cmd.begin(), cmd.end(), '\r'), cmd.end());

						if (cmd.empty())
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !rcon <command>\n");
							return;
						}

						// Tokenize and dispatch synchronously so we can capture the command's
						// console output via a temporary logging listener and reply with it.
						CCommand cc;
						bool dispatched = false;
						CCaptureLoggingListener listener;

						if (cc.Tokenize(cmd.c_str()) && cc.ArgC() > 0)
						{
							const char *name = cc.Arg(0);
							ConCommandRef cmdRef(name);
							if (cmdRef.IsValidRef() && g_pICvar)
							{
								CCommandContext ctx(CT_NO_TARGET, CPlayerSlot(-1));
								LoggingSystem_RegisterLoggingListener(&listener);
								g_pICvar->DispatchConCommand(cmdRef, ctx, cc);
								LoggingSystem_UnregisterLoggingListener(&listener);
								dispatched = true;
							}
							else
							{
								// Not a ConCommand; try ConVar (e.g. "tv_record_immediate" prints
								// value, "mp_friendlyfire 1" sets value).
								ConVarRefAbstract cvar(name);
								if (cvar.IsConVarDataValid())
								{
									if (cc.ArgC() >= 2)
									{
										LoggingSystem_RegisterLoggingListener(&listener);
										cvar.SetString(cc.Arg(1));
										LoggingSystem_UnregisterLoggingListener(&listener);
										CUtlString cur = cvar.GetString();
										ADMIN_PrintToClientT(slot, "%s = %s\n", cvar.GetName(), cur.Get());
									}
									else
									{
										CUtlString cur = cvar.GetString();
										ADMIN_PrintToClientT(slot, "%s = %s\n", cvar.GetName(), cur.Get());
									}
									if (!listener.Buffer().empty())
									{
										ReplyConsoleOutput(slot, listener.Buffer(), listener.Truncated());
									}

									std::string adminName2 = g_CS2APlayerManager.GetAdminName(slot);
									PlayerInfo *adminPlayer2 = g_CS2APlayerManager.GetPlayer(slot);
									ADMIN_LogAction(slot, (std::string("RCON: ") + cmd).c_str());
									g_CS2ADiscord.NotifyAdminAction(adminName2.c_str(), "RCON", cmd.c_str(), "", -1,
																	adminPlayer2 ? adminPlayer2->steamid64 : 0);
									return;
								}
							}
						}

						if (!dispatched)
						{
							// Fallback: queue via engine. Output cannot be captured because the command is executed at the next frame boundary.
							std::string queued = cmd + "\n";
							g_pEngine->ServerCommand(queued.c_str());
							ADMIN_ReplyToCommandT(slot, "Queued: %s\n", cmd.c_str());
						}
						else
						{
							ADMIN_ReplyToCommandT(slot, "Executed: %s, check console for output.\n", cmd.c_str());
							ReplyConsoleOutput(slot, listener.Buffer(), listener.Truncated());
						}

						std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
						PlayerInfo *adminPlayer = g_CS2APlayerManager.GetPlayer(slot);

						ADMIN_LogAction(slot, (std::string("RCON: ") + cmd).c_str());

						const char *discordOutput = dispatched && !listener.Buffer().empty() ? listener.Buffer().c_str() : nullptr;
						g_CS2ADiscord.NotifyAdminAction(adminName.c_str(), "RCON", cmd.c_str(), "", -1, adminPlayer ? adminPlayer->steamid64 : 0, 0,
														discordOutput);
					});

	// !pm <target> <message> - Private message a player
	RegisterCommand("pm", "basechat", ADMFLAG_CHAT,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.size() < 2)
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !pm <target> <message>\n");
							return;
						}

						int target = ADMIN_FindTarget(slot, args[0].c_str());
						if (target < 0)
						{
							return;
						}

						std::string message;
						for (size_t i = 1; i < args.size(); i++)
						{
							if (i > 1)
							{
								message += " ";
							}
							message += args[i];
						}

						std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
						PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(target);
						std::string targetName = targetPlayer ? targetPlayer->name : "Unknown";

						ADMIN_PrintToChatT(target, "[PM from %s] %s\n", adminName.c_str(), message.c_str());

						if (slot >= 0)
						{
							ADMIN_PrintToChatT(slot, "[PM to %s] %s\n", targetName.c_str(), message.c_str());
						}

						CGlobalVars *globals = GetGameGlobals();
						int maxClients = globals ? globals->maxClients : MAXPLAYERS;
						for (int i = 0; i < maxClients; i++)
						{
							if (i == slot || i == target)
							{
								continue;
							}

							if (g_CS2AAdminManager.PlayerHasFlag(i, ADMFLAG_GENERIC))
							{
								ADMIN_PrintToChatT(i, "[PM %s -> %s] %s\n", adminName.c_str(), targetName.c_str(), message.c_str());
							}
						}

						ADMIN_LogAction(slot, (std::string("PM to ") + targetName + ": " + message).c_str());
					});

	// !map <mapname|workshopid> - Change the current map
	RegisterCommand("map", "basecommands", ADMFLAG_CHANGEMAP,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							if (MenuPickerAvailable(slot))
							{
								StartMapFlow(slot);
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Usage: !map <mapname|workshopid>\n");
							return;
						}

						std::string error;
						std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
						PlayerInfo *adminPlayer = g_CS2APlayerManager.GetPlayer(slot);

						if (g_CS2AForwards.FireOnMapChange(args[0].c_str(), slot))
						{
							return;
						}

						if (!g_CS2AMapManager.ChangeMap(args[0].c_str(), error))
						{
							ADMIN_ReplyToCommandT(slot, "%s\n", error.c_str());
							return;
						}

						// A workshop map still downloading has already announced itself.
						if (!g_CS2AMapManager.IsChangePending())
						{
							ADMIN_ChatToAllT("%s changed map to %s.\n", adminName.c_str(), args[0].c_str());
						}
						ADMIN_LogAction(slot, (std::string("Changed map to ") + args[0]).c_str());

						g_CS2ADiscord.NotifyAdminAction(adminName.c_str(), "Map Change", args[0].c_str(), "", -1,
														adminPlayer ? adminPlayer->steamid64 : 0);
					});

	// !maps [page] - List available maps from maplist
	RegisterCommand("maps", "basecommands", ADMFLAG_CHANGEMAP,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						// No page given: open the interactive map picker if menus are available,
						// otherwise fall back to the paged text listing below.
						if (args.empty() && MenuPickerAvailable(slot))
						{
							StartMapFlow(slot);
							return;
						}

						const std::vector<const MapEntry *> maps = g_CS2AMapManager.GetSortedMaps();
						if (maps.empty())
						{
							ADMIN_ReplyToCommandT(slot, "No maps loaded. Check cfg/maplist.txt\n");
							return;
						}

						int page = 1;
						if (!args.empty())
						{
							page = std::atoi(args[0].c_str());
							if (page < 1)
							{
								page = 1;
							}
						}

						const int perPage = 8;
						int totalPages = ((int)maps.size() + perPage - 1) / perPage;
						if (page > totalPages)
						{
							page = totalPages;
						}

						int startIdx = (page - 1) * perPage;
						int endIdx = startIdx + perPage;
						if (endIdx > (int)maps.size())
						{
							endIdx = (int)maps.size();
						}

						ADMIN_ReplyToCommandT(slot, "Maps (page %d/%d):\n", page, totalPages);
						for (int i = startIdx; i < endIdx; i++)
						{
							if (maps[i]->isWorkshop)
							{
								ADMIN_ReplyToCommandT(slot, "  %s [ws:%s]\n", maps[i]->displayName.c_str(), maps[i]->workshopId.c_str());
							}
							else
							{
								ADMIN_ReplyToCommandT(slot, "  %s\n", maps[i]->mapName.c_str());
							}
						}
						if (page < totalPages)
						{
							ADMIN_ReplyToCommandT(slot, "Use !maps %d for next page.\n", page + 1);
						}
					});

	// !entfire <entity> <input> [value] - Fire an input on an entity
	RegisterCommand("entfire", "cs2admin", ADMFLAG_CHEATS,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.size() < 2)
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !entfire <entity> <input> [value]\n");
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

						ADMIN_ReplyToCommandT(slot, "Fired: %s %s%s\n", args[0].c_str(), args[1].c_str(),
											  args.size() > 2 ? (" " + args[2]).c_str() : "");
						ADMIN_LogAction(slot, (std::string("EntFire: ") + cmd).c_str());
					});

	// !give <target> <weapon> - Give a weapon to a player
	RegisterCommand("give", "funcommands", ADMFLAG_CHEATS,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty() && MenuPickerAvailable(slot))
						{
							StartGiveFlow(slot);
							return;
						}

						if (args.size() < 2)
						{
							ADMIN_ReplyToCommandT(slot, "Usage: !give <target> <weapon>\n");
							return;
						}

						TargetResult targets = ADMIN_FindTargets(slot, args[0].c_str());
						if (!targets.error.empty())
						{
							ADMIN_ReplyToCommandT(slot, "%s\n", ADMIN_Translate(slot, targets.error.c_str()).c_str());
							return;
						}

						// Normalize weapon name: prepend weapon_ if not present
						std::string weapon = args[1];
						std::transform(weapon.begin(), weapon.end(), weapon.begin(),
									   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						if (weapon.find("weapon_") != 0 && weapon.find("item_") != 0)
						{
							weapon = "weapon_" + weapon;
						}

						// Only classnames from the picker table, so a typo or a non-item entity never reaches GiveNamedItem.
						bool known = false;
						for (const WeaponEntry &w : kWeapons)
						{
							if (weapon == w.classname)
							{
								known = true;
								break;
							}
						}
						if (!known)
						{
							ADMIN_ReplyToCommandT(slot, "Unknown weapon '%s'.\n", args[1].c_str());
							return;
						}

						std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
						int given = 0;

						for (int targetSlot : targets.slots)
						{
							if (!CheckImmunity(slot, targetSlot))
							{
								continue;
							}

							PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(targetSlot);
							if (!targetPlayer || !targetPlayer->connected)
							{
								continue;
							}

							CCSPlayerController *controller = CCSPlayerController::FromSlot(targetSlot);
							if (!controller || !controller->m_bPawnIsAlive())
							{
								continue;
							}

							CCSPlayerPawn *pawn = controller->GetPlayerPawn();
							if (!pawn)
							{
								continue;
							}

							CCSPlayer_ItemServices *itemServices = pawn->m_pItemServices();
							if (!itemServices)
							{
								continue;
							}

							// A weapon given into a taken slot drops to the floor, so drop the held one first like buying does.
							const int gearSlot = WeaponGearSlot(weapon.c_str());
							CPlayer_WeaponServices *weaponServices = pawn->m_pWeaponServices();
							if (gearSlot >= 0 && weaponServices && g_pEntitySystem)
							{
								const CUtlVector<CEntityHandle> &held = weaponServices->m_hMyWeapons();
								for (int i = 0; i < held.Count(); i++)
								{
									CEntityInstance *heldWeapon = g_pEntitySystem->GetEntityInstance(held[i]);
									if (heldWeapon && WeaponGearSlot(heldWeapon->GetClassname()) == gearSlot)
									{
										// Changes m_hMyWeapons, so stop here.
										weaponServices->DropWeapon(heldWeapon);
										break;
									}
								}
							}

							itemServices->GiveNamedItem(weapon.c_str());
							given++;
						}

						if (given > 0)
						{
							if (targets.isMultiTarget)
							{
								ADMIN_ChatToAllT("%s gave %s to %d players.\n", adminName.c_str(), weapon.c_str(), given);
								ADMIN_LogAction(slot, (std::string("Gave ") + weapon + " to " + std::to_string(given) + " players").c_str());
							}
							else
							{
								PlayerInfo *tp = g_CS2APlayerManager.GetPlayer(targets.slots[0]);
								std::string targetName = tp ? tp->name : "Unknown";
								ADMIN_ChatToAllT("%s gave %s to %s.\n", adminName.c_str(), weapon.c_str(), targetName.c_str());
								ADMIN_LogAction(slot, (std::string("Gave ") + weapon + " to " + targetName).c_str());
							}
						}
						else
						{
							ADMIN_ReplyToCommandT(slot, "No valid alive targets found.\n");
						}
					});

	// !strip <target> - Strip all weapons from a player
	RegisterCommand("strip", "funcommands", ADMFLAG_CHEATS,
					[](int slot, const std::vector<std::string> &args, bool silent)
					{
						if (args.empty())
						{
							if (MenuPickerAvailable(slot))
							{
								StartTargetOnlyFlow(slot, "strip", "Strip", true, false);
								return;
							}
							ADMIN_ReplyToCommandT(slot, "Usage: !strip <target>\n");
							return;
						}

						TargetResult targets = ADMIN_FindTargets(slot, args[0].c_str());
						if (!targets.error.empty())
						{
							ADMIN_ReplyToCommandT(slot, "%s\n", ADMIN_Translate(slot, targets.error.c_str()).c_str());
							return;
						}

						std::string adminName = g_CS2APlayerManager.GetAdminName(slot);
						int stripped = 0;

						for (int targetSlot : targets.slots)
						{
							if (!CheckImmunity(slot, targetSlot))
							{
								continue;
							}

							PlayerInfo *targetPlayer = g_CS2APlayerManager.GetPlayer(targetSlot);
							if (!targetPlayer || !targetPlayer->connected)
							{
								continue;
							}

							CCSPlayerController *controller = CCSPlayerController::FromSlot(targetSlot);
							if (!controller || !controller->m_bPawnIsAlive())
							{
								continue;
							}

							CCSPlayerPawn *pawn = controller->GetPlayerPawn();
							if (!pawn)
							{
								continue;
							}

							CCSPlayer_ItemServices *itemServices = pawn->m_pItemServices();
							if (!itemServices)
							{
								continue;
							}

							itemServices->StripPlayerWeapons(true);
							stripped++;
						}

						if (stripped > 0)
						{
							if (targets.isMultiTarget)
							{
								ADMIN_ChatToAllT("%s stripped weapons from %d players.\n", adminName.c_str(), stripped);
								ADMIN_LogAction(slot, (std::string("Stripped weapons from ") + std::to_string(stripped) + " players").c_str());
							}
							else
							{
								PlayerInfo *tp = g_CS2APlayerManager.GetPlayer(targets.slots[0]);
								std::string targetName = tp ? tp->name : "Unknown";
								ADMIN_ChatToAllT("%s stripped weapons from %s.\n", adminName.c_str(), targetName.c_str());
								ADMIN_LogAction(slot, (std::string("Stripped weapons from ") + targetName).c_str());
							}
						}
						else
						{
							ADMIN_ReplyToCommandT(slot, "No valid alive targets found.\n");
						}
					});
}

// Single static callback for all dynamically registered mm_ console commands.
// Extracts command name, strips "mm_" prefix, and dispatches to the chat command handler.
static void ConsoleCommandCallback(const CCommandContext &context, const CCommand &args)
{
	if (args.ArgC() < 1)
	{
		return;
	}

	const char *fullName = args[0]; // e.g. "mm_who"
	const char *cmdName = fullName;

	// Strip "mm_" prefix
	if (strncmp(cmdName, "mm_", 3) == 0)
	{
		cmdName += 3;
	}

	// Not args[i], the engine tokenizer would cut a SteamID apart at its colons.
	std::vector<std::string> cmdArgs = mmu::SplitArgs(args.ArgS());

	// Use the player slot from the command context (-1 for server console)
	int slot = context.GetPlayerSlot().Get();

	// Dispatch with the caller's slot, silent = false
	g_CS2ACommandSystem.DispatchConsoleCommand(cmdName, cmdArgs, slot);
}

void CS2ACommandSystem::DispatchConsoleCommand(const char *cmdName, const std::vector<std::string> &args, int slot)
{
	std::string lower(cmdName);
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	auto it = m_commands.find(lower);
	if (it == m_commands.end())
	{
		MMU_LOG_INFO("Unknown command: %s\n", cmdName);
		return;
	}

	Run(it->first, it->second, slot, args, false);
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

	for (auto &entry : m_consoleCommands)
	{
		entry.cmd = new ConCommand(entry.name.c_str(), ConsoleCommandCallback, entry.desc.c_str(), FCVAR_CLIENT_CAN_EXECUTE);
	}
}

void CS2ACommandSystem::Shutdown()
{
	for (auto &entry : m_consoleCommands)
	{
		delete entry.cmd;
		entry.cmd = nullptr;
	}
	m_consoleCommands.clear();
	m_commands.clear();
}
