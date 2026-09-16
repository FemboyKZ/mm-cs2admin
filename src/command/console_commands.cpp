#include "src/common.h"
#include "mmu/log.h"
#include "src/config/config.h"
#include "src/db/database.h"
#include "src/player/player_manager.h"
#include "src/ban/ban_manager.h"
#include "src/comm/comm_manager.h"
#include "src/admin/admin_manager.h"
#include "src/compat/foreign_plugins.h"
#include "src/lang/translations.h"
#include "src/tags/tag_manager.h"
#include "src/utils/print_utils.h"
#include "src/cs2admin.h"

// Console commands (server-side)
// Note: most mm_ commands are auto-registered by CS2ACommandSystem::RegisterConsoleCommands() as mirrors of chat commands.
// Only console-only commands are defined here.

CON_COMMAND_F(mm_reload, "Reload CS2Admin config and admins", FCVAR_NONE)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/cs2admin/core.cfg", g_SMAPI->GetBaseDir());

	// Parsed into a temporary, so a file that fails validation leaves the live config untouched.
	// Starting from defaults also means a key removed from the file goes back to its default rather than keeping the old value.
	CS2AConfig fresh;
	if (ADMIN_LoadConfig(path, fresh))
	{
		// The server ID lookup only runs on connect, so an auto-detected one would otherwise be lost.
		if (fresh.serverID == -1 && g_CS2AConfig.serverID != -1)
		{
			fresh.serverID = g_CS2AConfig.serverID;
		}
		g_CS2AConfig = fresh;
		MMU_LOG_INFO("Config reloaded from %s\n", path);
		mmu::config::ApplyLogBlock(g_CS2AConfig.log);
		g_CS2APlugin.OnConfigReloaded();
	}
	else
	{
		MMU_LOG_WARN("Failed to reload config from %s, keeping the loaded one.\n", path);
	}

	ADMIN_LoadTranslations();

	// Reload admins (flat file + database)
	g_CS2AAdminManager.ReloadAdmins();

	// Tags first, then clan tags: a reload can drop a tag someone was wearing,
	// and UpdateAllClanTags is what walks that back off the scoreboard.
	g_CS2ATagManager.LoadTags();
	g_CS2AForeignPlugins.Refresh();
	g_CS2ATagManager.UpdateAllClanTags();

	ADMIN_RecheckConnectedPlayers();
}

CON_COMMAND_F(mm_rehash, "Rebuild admin cache from database and flat files", FCVAR_NONE)
{
	MMU_LOG_INFO("Rehashing admin cache...\n");
	g_CS2AAdminManager.ReloadAdmins();
	MMU_LOG_INFO("Admin cache rebuilt.\n");
}

CON_COMMAND_F(cs2admin_version, "Display CS2Admin version", FCVAR_NONE)
{
	META_CONPRINTF("CS2Admin version %s (%s)\n", PLUGIN_FULL_VERSION, __DATE__);
}

// SourceBans++ web panel commands, sent over RCON.
// The panel already wrote the DB row, so these only update the game.

// The tokenizer splits on ':', which breaks SteamIDs like [U:1:X].
static std::vector<std::string> RawArgs(const CCommand &args)
{
	std::vector<std::string> out;
	std::string current;
	for (const char *p = args.ArgS(); *p; p++)
	{
		if (*p == ' ' || *p == '\t' || *p == '"')
		{
			if (!current.empty())
			{
				out.push_back(std::move(current));
				current.clear();
			}
			continue;
		}
		current += *p;
	}
	if (!current.empty())
	{
		out.push_back(std::move(current));
	}
	return out;
}

// Human player slot for a SteamID in any format, or -1.
static int FindWebTarget(const std::string &steamid)
{
	uint64_t steamid64 = CS2AAdminManager::AuthIdToSteamID64(CS2AAdminManager::NormalizeSteamID(steamid.c_str()).c_str());
	if (steamid64 == 0)
	{
		return -1;
	}
	int slot = g_CS2APlayerManager.FindSlotBySteamID64(steamid64);
	PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slot);
	return (p && !p->fakePlayer) ? slot : -1;
}

CON_COMMAND_F(sc_fw_block, "Web panel: mute/gag/silence a player (RCON)", FCVAR_NONE)
{
	// type 1 = mute, 2 = gag, 3 = silence. length in seconds, 0 = permanent, negative = session.
	std::vector<std::string> raw = RawArgs(args);
	if (raw.size() < 3)
	{
		META_CONPRINTF("Usage: sc_fw_block <type> <length> <steamid>\n");
		return;
	}

	int type = atoi(raw[0].c_str());
	int seconds = atoi(raw[1].c_str());
	const std::string &steamid = raw[2];

	if (type < 1 || type > 3)
	{
		MMU_LOG_WARN("sc_fw_block: invalid type %d.\n", type);
		return;
	}

	int targetSlot = FindWebTarget(steamid);
	if (targetSlot < 0)
	{
		MMU_LOG_INFO("sc_fw_block: Player %s not on this server.\n", steamid.c_str());
		return;
	}

	bool mute = type == COMM_MUTE || type == 3;
	bool gag = type == COMM_GAG || type == 3;

	if (seconds < 0)
	{
		if (mute)
		{
			g_CS2ACommManager.SessionMutePlayer(targetSlot, -1);
		}
		if (gag)
		{
			g_CS2ACommManager.SessionGagPlayer(targetSlot, -1);
		}
	}
	else
	{
		// Rounded up, so a sub-minute block does not become 0 (permanent).
		int minutes = (seconds + 59) / 60;
		if (type == 3)
		{
			g_CS2ACommManager.SilencePlayer(targetSlot, minutes, "Blocked from web panel", -1, false);
		}
		else if (mute)
		{
			g_CS2ACommManager.MutePlayer(targetSlot, minutes, "Blocked from web panel", -1, false);
		}
		else
		{
			g_CS2ACommManager.GagPlayer(targetSlot, minutes, "Blocked from web panel", -1, false);
		}
	}

	MMU_LOG_INFO("sc_fw_block: Applied type=%d to %s for %d sec.\n", type, steamid.c_str(), seconds);
}

CON_COMMAND_F(sc_fw_ungag, "Web panel: ungag a player (RCON)", FCVAR_NONE)
{
	std::vector<std::string> raw = RawArgs(args);
	if (raw.empty())
	{
		META_CONPRINTF("Usage: sc_fw_ungag <steamid>\n");
		return;
	}

	int targetSlot = FindWebTarget(raw[0]);
	if (targetSlot < 0)
	{
		MMU_LOG_INFO("sc_fw_ungag: Player %s not on this server.\n", raw[0].c_str());
		return;
	}
	g_CS2ACommManager.UngagPlayer(targetSlot, -1, false);
	MMU_LOG_INFO("sc_fw_ungag: Ungagged %s.\n", raw[0].c_str());
}

CON_COMMAND_F(sc_fw_unmute, "Web panel: unmute a player (RCON)", FCVAR_NONE)
{
	std::vector<std::string> raw = RawArgs(args);
	if (raw.empty())
	{
		META_CONPRINTF("Usage: sc_fw_unmute <steamid>\n");
		return;
	}

	int targetSlot = FindWebTarget(raw[0]);
	if (targetSlot < 0)
	{
		MMU_LOG_INFO("sc_fw_unmute: Player %s not on this server.\n", raw[0].c_str());
		return;
	}
	g_CS2ACommManager.UnmutePlayer(targetSlot, -1, false);
	MMU_LOG_INFO("sc_fw_unmute: Unmuted %s.\n", raw[0].c_str());
}

void ShutdownConsoleCommands()
{
	if (!g_pICvar)
	{
		return;
	}

	g_pICvar->UnregisterConCommandCallbacks(mm_reload_command);
	g_pICvar->UnregisterConCommandCallbacks(mm_rehash_command);
	g_pICvar->UnregisterConCommandCallbacks(cs2admin_version_command);
	g_pICvar->UnregisterConCommandCallbacks(sc_fw_block_command);
	g_pICvar->UnregisterConCommandCallbacks(sc_fw_ungag_command);
	g_pICvar->UnregisterConCommandCallbacks(sc_fw_unmute_command);
}
