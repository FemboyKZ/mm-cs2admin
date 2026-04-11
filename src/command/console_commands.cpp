#include "../common.h"
#include "../config/config.h"
#include "../db/database.h"
#include "../player/player_manager.h"
#include "../ban/ban_manager.h"
#include "../comm/comm_manager.h"
#include "../admin/admin_manager.h"
#include "../utils/print_utils.h"
#include "../cs2admin.h"

// Console commands (server-side)
// Note: most mm_ commands are auto-registered by CS2ACommandSystem::RegisterConsoleCommands()
// as mirrors of chat commands. Only console-only commands are defined here.

CON_COMMAND_F(mm_reload, "Reload CS2Admin config and admins", FCVAR_NONE)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/cs2admin/core.cfg",
		g_SMAPI->GetBaseDir());

	if (ADMIN_LoadConfig(path, g_CS2AConfig))
		META_CONPRINTF("[ADMIN] Config reloaded from %s\n", path);
	else
		META_CONPRINTF("[ADMIN] Failed to reload config from %s\n", path);

	// Reload admins (flat file + database)
	g_CS2AAdminManager.ReloadAdmins();

	// Re-verify bans and comms for all connected players
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
		if (p && p->connected && !p->fakePlayer && p->authenticated)
		{
			g_CS2ABanManager.VerifyBan(i, p->steamid64, p->ip.c_str(),
				[i, steamid64 = p->steamid64](bool banned, const std::string &reason) {
				if (banned)
				{
					PlayerInfo *pp = g_CS2APlayerManager.GetPlayer(i);
					if (pp && pp->connected && pp->steamid64 == steamid64)
					{
						META_CONPRINTF("[ADMIN] Reload: kicking banned player \"%s\" (%s).\n",
							pp->name.c_str(), pp->authid.c_str());
						g_pEngine->DisconnectClient(CPlayerSlot(i), NETWORK_DISCONNECT_KICKED_CONVICTEDACCOUNT);
					}
				}
				else
				{
					g_CS2ACommManager.VerifyComms(i, steamid64);
				}
			});
		}
	}
}

CON_COMMAND_F(mm_rehash, "Rebuild admin cache from database and flat files", FCVAR_NONE)
{
	META_CONPRINTF("[ADMIN] Rehashing admin cache...\n");
	g_CS2AAdminManager.ReloadAdmins();
	META_CONPRINTF("[ADMIN] Admin cache rebuilt.\n");
}

CON_COMMAND_F(cs2admin_version, "Display CS2Admin version", FCVAR_NONE)
{
	META_CONPRINTF("CS2Admin version %s (%s)\n", PLUGIN_FULL_VERSION, __DATE__);
}

// Web panel integration commands, called by SourceBans web panel via RCON
CON_COMMAND_F(sc_fw_block, "Web panel: mute/gag a player (RCON)", FCVAR_NONE)
{
	// Format: sc_fw_block <authid> <time> <type> <reason>
	// type: 1 = mute, 2 = gag
	if (args.ArgC() < 5)
	{
		META_CONPRINTF("Usage: sc_fw_block <authid> <time> <type> <reason>\n");
		return;
	}

	const char *authid = args[1];
	int time = atoi(args[2]);
	int type = atoi(args[3]);
	const char *reason = args[4];

	int targetSlot = -1;
	CGlobalVars *globals = GetGameGlobals();
	if (globals)
	{
		for (int i = 0; i < globals->maxClients; i++)
		{
			PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
			if (p && p->connected && !p->fakePlayer && p->authid == authid)
			{
				targetSlot = i;
				break;
			}
		}
	}

	if (targetSlot < 0)
	{
		META_CONPRINTF("[ADMIN] sc_fw_block: Player %s not found on server.\n", authid);
		return;
	}

	if (type == COMM_MUTE)
		g_CS2ACommManager.MutePlayer(targetSlot, time, reason, -1);
	else if (type == COMM_GAG)
		g_CS2ACommManager.GagPlayer(targetSlot, time, reason, -1);

	META_CONPRINTF("[ADMIN] sc_fw_block: Applied type=%d to %s for %d min.\n", type, authid, time);
}

CON_COMMAND_F(sc_fw_ungag, "Web panel: ungag a player (RCON)", FCVAR_NONE)
{
	if (args.ArgC() < 2)
	{
		META_CONPRINTF("Usage: sc_fw_ungag <authid>\n");
		return;
	}

	const char *authid = args[1];
	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
		return;

	for (int i = 0; i < globals->maxClients; i++)
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
		if (p && p->connected && !p->fakePlayer && p->authid == authid)
		{
			g_CS2ACommManager.UngagPlayer(i, -1);
			META_CONPRINTF("[ADMIN] sc_fw_ungag: Ungagged %s.\n", authid);
			return;
		}
	}
	META_CONPRINTF("[ADMIN] sc_fw_ungag: Player %s not found on server.\n", authid);
}

CON_COMMAND_F(sc_fw_unmute, "Web panel: unmute a player (RCON)", FCVAR_NONE)
{
	if (args.ArgC() < 2)
	{
		META_CONPRINTF("Usage: sc_fw_unmute <authid>\n");
		return;
	}

	const char *authid = args[1];
	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
		return;

	for (int i = 0; i < globals->maxClients; i++)
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
		if (p && p->connected && !p->fakePlayer && p->authid == authid)
		{
			g_CS2ACommManager.UnmutePlayer(i, -1);
			META_CONPRINTF("[ADMIN] sc_fw_unmute: Unmuted %s.\n", authid);
			return;
		}
	}
	META_CONPRINTF("[ADMIN] sc_fw_unmute: Player %s not found on server.\n", authid);
}
