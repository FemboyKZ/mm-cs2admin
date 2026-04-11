#include <stdio.h>
#include <cstdarg>
#include <ctime>
#include "cs2admin.h"
#include "config/config.h"
#include "config/gamedata.h"
#include "db/database.h"
#include "player/player_manager.h"
#include "ban/ban_manager.h"
#include "comm/comm_manager.h"
#include "command/command_system.h"
#include "command/map_manager.h"
#include "admin/admin_manager.h"
#include "public/forwards.h"
#include "queue/offline_queue.h"
#include "utils/print_utils.h"
#include "utils/discord.h"

#include <sql_mm.h>

#include <schemasystem/schemasystem.h>
#include <interfaces/interfaces.h>
#include <entity2/entitysystem.h>
#include <networksystem/inetworkmessages.h>
#include <engine/igameeventsystem.h>

// Entity system global (declared extern in common.h)
// Note: g_pSchemaSystem and g_pGameResourceServiceServer are already defined by the SDK's interfaces.lib
CGameEntitySystem *g_pEntitySystem = nullptr;

CGameEntitySystem *GameEntitySystem()
{
	if (!g_pGameResourceServiceServer)
		return nullptr;

	int offset = g_CS2AGameData.GetOffset("GameEntitySystem");
	if (offset < 0)
		return nullptr;

	return *reinterpret_cast<CGameEntitySystem **>(
		reinterpret_cast<uintptr_t>(g_pGameResourceServiceServer) + offset);
}

// SourceHook hook declarations - must match interface method signatures exactly
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientActive, SH_NOATTRIB, 0, CPlayerSlot, bool, const char *, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64, const char *);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const *, int, uint64);
SH_DECL_HOOK1_void(IServerGameClients, ClientSettingsChanged, SH_NOATTRIB, 0, CPlayerSlot);
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char*, uint64, const char *, const char *, bool);
SH_DECL_HOOK6(IServerGameClients, ClientConnect, SH_NOATTRIB, 0, bool, CPlayerSlot, const char*, uint64, const char *, bool, CBufferString *);
SH_DECL_HOOK2_void(IServerGameClients, ClientCommand, SH_NOATTRIB, 0, CPlayerSlot, const CCommand &);
SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandRef, const CCommandContext&, const CCommand&);

// Plugin instance
CS2APlugin g_CS2APlugin;

// Forwards API
CS2AForwards g_CS2AForwards;

// Engine interface pointers (declared extern in common.h)
IServerGameDLL *g_pServerGameDLL = nullptr;
IServerGameClients *g_pGameClients = nullptr;
IVEngineServer *g_pEngine = nullptr;
IGameEventManager2 *g_pGameEvents = nullptr;
ICvar *g_pICvar = nullptr;
IGameEventSystem *g_pGameEventSystem = nullptr;

CGlobalVars *GetGameGlobals()
{
	INetworkGameServer *server = g_pNetworkServerService->GetIGameServer();
	if (!server)
		return nullptr;
	return server->GetGlobals();
}

// Expose the plugin to Metamod
PLUGIN_EXPOSE(CS2APlugin, g_CS2APlugin);

bool CS2APlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	// Acquire engine interfaces
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pICvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pServerGameDLL, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, g_pGameClients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION);

	g_SMAPI->AddListener(this, this);

	m_bLateLoaded = late;

	META_CONPRINTF("[ADMIN] CS2Admin %s loading...%s\n", PLUGIN_FULL_VERSION,
		late ? " (late)" : "");

	// Load gamedata
	char gamedataPath[512];
	snprintf(gamedataPath, sizeof(gamedataPath), "%s/addons/cs2admin/gamedata/cs2admin.txt",
		g_SMAPI->GetBaseDir());

	if (!g_CS2AGameData.Load(gamedataPath))
	{
		META_CONPRINTF("[ADMIN] ERROR: Could not load gamedata from %s\n", gamedataPath);
		META_CONPRINTF("[ADMIN] Entity access (team/alive targeting) will not work.\n");
	}
	else
	{
		META_CONPRINTF("[ADMIN] Gamedata loaded. GameEntitySystem offset: %d\n",
			g_CS2AGameData.GetOffset("GameEntitySystem"));
	}

	// Load config
	char configPath[512];
	snprintf(configPath, sizeof(configPath), "%s/cfg/cs2admin/core.cfg",
		g_SMAPI->GetBaseDir());

	if (!ADMIN_LoadConfig(configPath, g_CS2AConfig))
	{
		META_CONPRINTF("[ADMIN] ERROR: Could not load config from %s\n", configPath);
		META_CONPRINTF("[ADMIN] Make sure the file exists at: <game_root>/cfg/cs2admin/core.cfg\n");
		META_CONPRINTF("[ADMIN] Database features will be disabled. Only flat-file admins will be loaded.\n");
		m_bConfigLoaded = false;
	}
	else
	{
		m_bConfigLoaded = true;
		META_CONPRINTF("[ADMIN] Config loaded. DB: %s@%s:%d/%s, Prefix: %s\n",
			g_CS2AConfig.dbUser.c_str(), g_CS2AConfig.dbHost.c_str(),
			g_CS2AConfig.dbPort, g_CS2AConfig.dbName.c_str(),
			g_CS2AConfig.databasePrefix.c_str());
	}

	// Register hooks
	SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pServerGameDLL, SH_MEMBER(this, &CS2APlugin::Hook_GameFrame), true);
	SH_ADD_HOOK(IServerGameClients, ClientActive, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientActive), true);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientDisconnect), true);
	SH_ADD_HOOK(IServerGameClients, ClientPutInServer, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientPutInServer), true);
	SH_ADD_HOOK(IServerGameClients, ClientSettingsChanged, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientSettingsChanged), false);
	SH_ADD_HOOK(IServerGameClients, OnClientConnected, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_OnClientConnected), false);
	SH_ADD_HOOK(IServerGameClients, ClientConnect, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientConnect), false);
	SH_ADD_HOOK(IServerGameClients, ClientCommand, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientCommand), false);
	SH_ADD_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &CS2APlugin::Hook_DispatchConCommand), false);

	// Register ConVars
	g_pCVar = g_pICvar;
	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);

	// Register chat commands
	g_CS2ACommandSystem.RegisterBuiltinCommands();

	// Register mm_ console command mirrors for all chat commands
	g_CS2ACommandSystem.RegisterConsoleCommands();

	// Load maplist
	g_CS2AMapManager.LoadMapList();

	// Start Discord webhook worker thread
	g_CS2ADiscord.Init();

	META_CONPRINTF("[ADMIN] Plugin loaded successfully.\n");
	return true;
}

bool CS2APlugin::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pServerGameDLL, SH_MEMBER(this, &CS2APlugin::Hook_GameFrame), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientActive, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientActive), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientDisconnect), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientPutInServer, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientPutInServer), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientSettingsChanged, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientSettingsChanged), false);
	SH_REMOVE_HOOK(IServerGameClients, OnClientConnected, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_OnClientConnected), false);
	SH_REMOVE_HOOK(IServerGameClients, ClientConnect, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientConnect), false);
	SH_REMOVE_HOOK(IServerGameClients, ClientCommand, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientCommand), false);
	SH_REMOVE_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &CS2APlugin::Hook_DispatchConCommand), false);

	// Shut down Discord worker thread
	g_CS2ADiscord.Shutdown();

	// Save any pending offline queries before shutdown
	if (g_CS2AOfflineQueue.HasItems())
		g_CS2AOfflineQueue.SaveToFile();

	g_CS2ADatabase.Shutdown();

	META_CONPRINTF("[ADMIN] Plugin unloaded.\n");
	return true;
}

// public ICS2Admin interface
class CS2AdminAPI : public ICS2Admin
{
public:
	bool IsAdmin(int slot) override
	{
		return g_CS2AAdminManager.GetPlayerAdmin(slot) != nullptr;
	}

	uint32_t GetAdminFlags(int slot) override
	{
		const AdminEntry *e = g_CS2AAdminManager.GetPlayerAdmin(slot);
		return e ? e->flags : 0;
	}

	bool HasFlag(int slot, uint32_t flag) override
	{
		return g_CS2AAdminManager.PlayerHasFlag(slot, flag);
	}

	bool CanUseCommand(int slot, const char *commandName,
		const char *commandGroup, uint32_t defaultFlag) override
	{
		return g_CS2AAdminManager.CanPlayerUseCommand(slot, commandName,
			commandGroup, defaultFlag);
	}

	int GetAdminImmunity(int slot) override
	{
		const AdminEntry *e = g_CS2AAdminManager.GetPlayerAdmin(slot);
		return e ? e->immunity : 0;
	}

	bool HasHigherImmunity(int sourceSlot, int targetSlot) override
	{
		const AdminEntry *src = g_CS2AAdminManager.GetPlayerAdmin(sourceSlot);
		const AdminEntry *tgt = g_CS2AAdminManager.GetPlayerAdmin(targetSlot);

		int srcImm = src ? src->immunity : 0;
		int tgtImm = tgt ? tgt->immunity : 0;

		return srcImm > tgtImm;
	}

	// Player state queries

	bool IsPlayerConnected(int slot) override
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slot);
		return p && p->connected;
	}

	const char *GetPlayerName(int slot) override
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slot);
		return (p && p->connected) ? p->name.c_str() : nullptr;
	}

	const char *GetPlayerAuthId(int slot) override
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slot);
		return (p && p->connected) ? p->authid.c_str() : nullptr;
	}

	uint64_t GetPlayerSteamID64(int slot) override
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slot);
		return (p && p->connected) ? p->steamid64 : 0;
	}

	const char *GetPlayerIP(int slot) override
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slot);
		return (p && p->connected) ? p->ip.c_str() : nullptr;
	}

	bool IsMuted(int slot) override
	{
		return g_CS2ACommManager.IsMuted(slot);
	}

	bool IsGagged(int slot) override
	{
		return g_CS2ACommManager.IsGagged(slot);
	}

	// Programmatic admin actions

	void BanPlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason) override
	{
		g_CS2ABanManager.BanPlayer(targetSlot, timeMinutes, reason ? reason : "Banned", adminSlot);
	}

	void KickPlayer(int targetSlot, int adminSlot, const char *reason) override
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(targetSlot);
		if (!p || !p->connected)
			return;

		if (g_CS2AForwards.FireOnKickPlayer(targetSlot, adminSlot, reason ? reason : "Kicked"))
			return;

		std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);
		ADMIN_LogAction(adminSlot, (std::string("Kicked ") + p->name + ": " + (reason ? reason : "Kicked")).c_str());
		g_pEngine->DisconnectClient(CPlayerSlot(targetSlot), NETWORK_DISCONNECT_KICKED);
	}

	void MutePlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason) override
	{
		g_CS2ACommManager.MutePlayer(targetSlot, timeMinutes, reason ? reason : "Muted", adminSlot);
	}

	void GagPlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason) override
	{
		g_CS2ACommManager.GagPlayer(targetSlot, timeMinutes, reason ? reason : "Gagged", adminSlot);
	}

	void SilencePlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason) override
	{
		g_CS2ACommManager.SilencePlayer(targetSlot, timeMinutes, reason ? reason : "Silenced", adminSlot);
	}

	void UnmutePlayer(int targetSlot, int adminSlot) override
	{
		g_CS2ACommManager.UnmutePlayer(targetSlot, adminSlot);
	}

	void UngagPlayer(int targetSlot, int adminSlot) override
	{
		g_CS2ACommManager.UngagPlayer(targetSlot, adminSlot);
	}

	void UnsilencePlayer(int targetSlot, int adminSlot) override
	{
		g_CS2ACommManager.UnsilencePlayer(targetSlot, adminSlot);
	}
};

static CS2AdminAPI g_CS2AdminAPI;

void *CS2APlugin::OnMetamodQuery(const char *iface, int *ret)
{
	if (!strcmp(iface, CS2ADMIN_INTERFACE))
	{
		if (ret) *ret = META_IFACE_OK;
		return static_cast<ICS2Admin *>(&g_CS2AdminAPI);
	}
	if (!strcmp(iface, CS2ADMIN_FORWARDS_INTERFACE))
	{
		if (ret) *ret = META_IFACE_OK;
		return static_cast<ICS2AdminForwards *>(&g_CS2AForwards);
	}

	if (ret) *ret = META_IFACE_FAILED;
	return nullptr;
}

void CS2APlugin::AllPluginsLoaded()
{
	// Always load flat-file admins regardless of DB state
	auto loadFlatFileOnly = [this]() {
		g_CS2AAdminManager.LoadFlatFileAdmins();
		g_CS2AAdminManager.MergeAndApplyAll();
		if (m_bLateLoaded)
			OnLateLoad();
	};

	// If config wasn't loaded, skip DB entirely
	if (!m_bConfigLoaded)
	{
		META_CONPRINTF("[ADMIN] No config loaded - skipping database. Only flat-file admins will be used.\n");
		loadFlatFileOnly();
		return;
	}

	// This is where sql_mm should be available
	if (!g_CS2ADatabase.Init())
	{
		META_CONPRINTF("[ADMIN] Failed to initialize database interface. Is sql_mm loaded?\n");
		META_CONPRINTF("[ADMIN] Ban checking disabled. Only flat-file admins will be used.\n");
		loadFlatFileOnly();
		return;
	}

	g_CS2ADatabase.Connect([this](bool success) {
		if (success)
		{
			META_CONPRINTF("[ADMIN] Database ready.\n");

			// Lookup server ID if set to auto (MySQL/SBPP only)
			if (g_CS2AConfig.serverID == -1 && g_CS2ADatabase.IsMySQL())
				LookupServerID();
			else if (g_CS2AConfig.serverID == -1)
				g_CS2AConfig.serverID = 0;

			// Load and process offline queue
			g_CS2AOfflineQueue.LoadFromFile();
			if (g_CS2AOfflineQueue.HasItems())
				g_CS2AOfflineQueue.ProcessQueue();

			// Load admins from both DB and flat file
			g_CS2AAdminManager.ReloadAdmins();

			// Handle late load after DB is ready
			if (m_bLateLoaded)
				OnLateLoad();
		}
		else
		{
			META_CONPRINTF("[ADMIN] Database connection failed! Bans will not be enforced.\n");
			// Load and apply flat-file admins only
			g_CS2AAdminManager.LoadFlatFileAdmins();
			g_CS2AAdminManager.MergeAndApplyAll();
		}
	});
}

void CS2APlugin::LookupServerID()
{
	// Try to find our server in the database by IP:port
	// Get the server's IP from the hostip convar
	ConVarRefAbstract hostip_ref("hostip");
	ConVarRefAbstract hostport_ref("hostport");

	if (!hostip_ref.IsValidRef() || !hostport_ref.IsValidRef())
	{
		META_CONPRINTF("[ADMIN] Cannot auto-detect server ID: hostip/hostport cvars not available.\n");
		return;
	}

	int hostip = hostip_ref.GetInt();
	int hostport = hostport_ref.GetInt();

	char ipStr[32];
	snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
		(hostip >> 24) & 0xFF, (hostip >> 16) & 0xFF,
		(hostip >> 8) & 0xFF, hostip & 0xFF);

	std::string prefix = g_CS2AConfig.databasePrefix;
	char query[512];
	snprintf(query, sizeof(query),
		"SELECT sid FROM %s_servers WHERE ip = '%s' AND port = '%d' LIMIT 1",
		prefix.c_str(), g_CS2ADatabase.Escape(ipStr).c_str(), hostport);

	g_CS2ADatabase.Query(query, [ipStr = std::string(ipStr), hostport](ISQLQuery *result) {
		if (!result)
			return;

		ISQLResult *rs = result->GetResultSet();
		if (rs && rs->GetRowCount() > 0)
		{
			ISQLRow *row = rs->FetchRow();
			if (row)
			{
				g_CS2AConfig.serverID = rs->GetInt(0);
				META_CONPRINTF("[ADMIN] Auto-detected server ID: %d\n", g_CS2AConfig.serverID);
			}
		}
		else
		{
			META_CONPRINTF("[ADMIN] Server not found in database (%s:%d). Using serverID=0.\n",
				ipStr.c_str(), hostport);
			g_CS2AConfig.serverID = 0;
		}
	});
}

// Hooks

bool CS2APlugin::Hook_ClientConnect(CPlayerSlot slot, const char *pszName, uint64 xuid,
	const char *pszNetworkID, bool unk1, CBufferString *pRejectReason)
{
	META_CONPRINTF("[ADMIN] ClientConnect: slot=%d name=\"%s\" xuid=%llu\n",
		slot.Get(), pszName, (unsigned long long)xuid);

	// Note: We cannot do async DB ban checks here and block, so IP-based bans
	// are checked in ClientPutInServer along with SteamID bans. ClientConnect
	// must return synchronously.

	RETURN_META_VALUE(MRES_IGNORED, true);
}

void CS2APlugin::Hook_OnClientConnected(CPlayerSlot slot, const char *pszName, uint64 xuid,
	const char *pszNetworkID, const char *pszAddress, bool bFakePlayer)
{
	int slotIdx = slot.Get();

	// Track player - but defer ban/admin checks until authentication is confirmed (ClientPutInServer)
	g_CS2APlayerManager.OnClientConnected(slotIdx, pszName, xuid, pszNetworkID, pszAddress, bFakePlayer);

	if (bFakePlayer)
		return;

	META_CONPRINTF("[ADMIN] Client connected: \"%s\" (%s) [%s] slot=%d\n",
		pszName, SteamID64ToAuthId(xuid).c_str(), pszAddress, slotIdx);

	g_CS2AForwards.FireOnClientConnected(slotIdx, pszName, xuid, pszAddress);
}

void CS2APlugin::Hook_ClientActive(CPlayerSlot slot, bool bLoadGame, const char *pszName, uint64 xuid)
{
	// Player is fully in-game
}

void CS2APlugin::Hook_ClientPutInServer(CPlayerSlot slot, char const *pszName, int type, uint64 xuid)
{
	int slotIdx = slot.Get();
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slotIdx);
	if (!player || player->fakePlayer)
		return;

	// Player entity is created and Steam auth is confirmed - safe to check bans/admins
	player->authenticated = true;

	META_CONPRINTF("[ADMIN] Client authenticated: \"%s\" (%s) slot=%d\n",
		player->name.c_str(), player->authid.c_str(), slotIdx);

	g_CS2AForwards.FireOnClientAuthorized(slotIdx, player->authid.c_str(), player->steamid64);

	// Check for active bans
	std::string playerIP = player->ip;
	uint64_t steamid64 = player->steamid64;

	g_CS2ABanManager.VerifyBan(slotIdx, steamid64, playerIP.c_str(),
		[slotIdx, steamid64](bool banned, const std::string &reason) {
		if (banned)
		{
			PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slotIdx);
			if (p && p->connected && p->steamid64 == steamid64)
			{
				ADMIN_PrintToClient(slotIdx, "[ADMIN] You are banned from this server. Reason: %s\n", reason.c_str());
				META_CONPRINTF("[ADMIN] Kicking banned player \"%s\" (%s). Reason: %s\n",
					p->name.c_str(), p->authid.c_str(), reason.c_str());
				g_pEngine->DisconnectClient(CPlayerSlot(slotIdx), NETWORK_DISCONNECT_KICKED_CONVICTEDACCOUNT);
			}
		}
		else
		{
			// Not banned, check comms
			g_CS2ACommManager.VerifyComms(slotIdx, steamid64);
		}
	});

	// Assign admin permissions (merges DB + flat file entries)
	g_CS2AAdminManager.AssignAdminToPlayer(slotIdx);

	// Check ban/comm history to notify admins on connect
	if (g_CS2AConfig.printCheckOnConnect && g_CS2ADatabase.IsConnected())
	{
		g_CS2ABanManager.CheckHistory(slotIdx, steamid64, playerIP.c_str(),
			[slotIdx, steamid64](int banCount, int commCount, int muteCount, int gagCount) {
			if (banCount > 0 || commCount > 0)
			{
				PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slotIdx);
				if (p && p->connected && p->steamid64 == steamid64)
				{
					ADMIN_ChatToAdmins("[ADMIN] Player \"%s\" (%s) has %d ban(s), %d mute(s), %d gag(s) on record.\n",
						p->name.c_str(), p->authid.c_str(), banCount, muteCount, gagCount);
				}
			}
		});
	}

	// check for ban evasion 
	if (g_CS2AConfig.sleuthActions > 0 && g_CS2ADatabase.IsConnected())
	{
		g_CS2ABanManager.CheckSleuth(slotIdx, steamid64, playerIP.c_str());
	}
}

void CS2APlugin::Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason,
	const char *pszName, uint64 xuid, const char *pszNetworkID)
{
	int slotIdx = slot.Get();
	g_CS2AForwards.FireOnClientDisconnect(slotIdx);
	g_CS2ACommManager.OnClientDisconnect(slotIdx);
	g_CS2APlayerManager.OnClientDisconnect(slotIdx);
}

void CS2APlugin::Hook_ClientCommand(CPlayerSlot nSlot, const CCommand &_cmd)
{
	// Chat (say/say_team) is intercepted in Hook_DispatchConCommand instead.
	// This hook remains for potential future use with other client commands.
}

void CS2APlugin::Hook_DispatchConCommand(ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args)
{
	// All console commands (including say and say_team) go through ICvar::DispatchConCommand in CS2.
	// We hook here to intercept chat messages for command parsing and gag enforcement.
	if (!cmd.IsValidRef())
		RETURN_META(MRES_IGNORED);

	CPlayerSlot slot = ctx.GetPlayerSlot();
	int slotIdx = slot.Get();

	// Ignore server console commands (slot -1)
	if (slotIdx < 0 || slotIdx > MAXPLAYERS)
		RETURN_META(MRES_IGNORED);

	const char *cmdName = cmd.GetName();
	if (!cmdName)
		RETURN_META(MRES_IGNORED);

	bool isSay = (strcmp(cmdName, "say") == 0);
	bool isSayTeam = (strcmp(cmdName, "say_team") == 0);

	if (!isSay && !isSayTeam)
		RETURN_META(MRES_IGNORED);

	const char *message = args.ArgC() > 1 ? args.Arg(1) : "";

	// Chat flood detection (before command processing)
	if (g_CS2AConfig.chatFloodCooldown > 0.0f)
	{
		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slotIdx);
		if (player && player->connected && !player->fakePlayer)
		{
			CGlobalVars *globals = GetGameGlobals();
			double curtime = globals ? globals->curtime : 0.0;

			if (curtime > 0.0)
			{
				double timeSince = curtime - player->lastChatTime;

				if (timeSince < g_CS2AConfig.chatFloodCooldown)
				{
					player->chatMessageCount++;

					if (player->chatMessageCount >= g_CS2AConfig.chatFloodMaxMessages)
					{
						if (g_CS2AConfig.chatFloodMuteDuration > 0 && !player->isGagged)
						{
							// Auto gag the player
							g_CS2ACommManager.GagPlayer(slotIdx, g_CS2AConfig.chatFloodMuteDuration,
								"Chat flood", -1);
							ADMIN_PrintToChat(slotIdx,
								"You have been gagged for %d minute(s) for chat flooding.\n",
								g_CS2AConfig.chatFloodMuteDuration);
							ADMIN_ChatToAdmins("[ADMIN] %s was auto-gagged for chat flooding.\n",
								player->name.c_str());
						}
						else
						{
							ADMIN_PrintToChat(slotIdx, "Slow down! You are sending messages too fast.\n");
						}

						player->chatMessageCount = 0;
						player->lastChatTime = curtime;
						RETURN_META(MRES_SUPERCEDE);
					}
				}
				else
				{
					// Reset counter if enough time passed
					player->chatMessageCount = 0;
				}

				player->lastChatTime = curtime;
			}
		}
	}

	// Process chat commands (! and / prefixed) BEFORE checking gag.
	// This allows gagged players to still run commands like !ungag requests.
	if (g_CS2ACommandSystem.ProcessChatMessage(slotIdx, message, isSayTeam))
	{
		// Command was handled - suppress the chat message from appearing in chat
		RETURN_META(MRES_SUPERCEDE);
	}

	// Block gagged players from sending normal chat
	if (g_CS2ACommandSystem.ShouldBlockChat(slotIdx))
	{
		META_CONPRINTF("[ADMIN] Blocked chat from gagged player in slot %d\n", slotIdx);
		RETURN_META(MRES_SUPERCEDE);
	}
}

void CS2APlugin::Hook_ClientSettingsChanged(CPlayerSlot slot)
{
	// Could track name changes here if needed
}

void CS2APlugin::Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
		return;

	float curtime = globals->curtime;

	// Enforce voice muting every tick.
	// SetClientListening must be called repeatedly because the engine resets
	// the listen matrix each frame.
	for (int i = 0; i < globals->maxClients; i++)
	{
		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(i);
		if (!player || !player->isMuted)
			continue;

		// Block all other players from hearing this muted player
		for (int j = 0; j < globals->maxClients; j++)
		{
			if (i == j)
				continue;

			PlayerInfo *listener = g_CS2APlayerManager.GetPlayer(j);
			if (listener && listener->connected)
				g_pEngine->SetClientListening(CPlayerSlot(j), CPlayerSlot(i), false);
		}
	}

	// Check for expired timed comm blocks (every 1 second)
	if (curtime >= m_flNextExpiryCheck)
	{
		m_flNextExpiryCheck = curtime + 1.0f;
		g_CS2ACommManager.CheckExpiredComms();
	}

	// Process offline queue (every processQueueTime minutes)
	if (curtime >= m_flNextQueueProcess)
	{
		m_flNextQueueProcess = curtime + (g_CS2AConfig.processQueueTime * 60.0f);
		if (g_CS2ADatabase.IsConnected() && g_CS2AOfflineQueue.HasItems())
			g_CS2AOfflineQueue.ProcessQueue();
	}

	// DB reconnection timer
	if (m_bConfigLoaded && g_CS2ADatabase.IsInitialized() &&
		!g_CS2ADatabase.IsConnected() && curtime >= m_flNextReconnect)
	{
		m_flNextReconnect = curtime + g_CS2AConfig.retryTime;
		META_CONPRINTF("[ADMIN] Attempting database reconnection...\n");
		g_CS2ADatabase.Reconnect([](bool success) {
			if (success)
			{
				META_CONPRINTF("[ADMIN] Database reconnected!\n");
				if (g_CS2AOfflineQueue.HasItems())
					g_CS2AOfflineQueue.ProcessQueue();
			}
		});
	}
}

void CS2APlugin::OnLateLoad()
{
	META_CONPRINTF("[ADMIN] Late load detected - processing existing players...\n");

	// Grab entity system pointer on late load (map already active)
	if (!g_pEntitySystem)
		g_pEntitySystem = GameEntitySystem();

	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
		return;

	for (int i = 0; i < globals->maxClients; i++)
	{
		// For late load, OnClientConnected was never called so PlayerInfo is empty.
		// We need to populate it from the engine.
		CPlayerSlot slot(i);
		uint64 xuid = g_pEngine->GetClientXUID(slot);
		if (xuid == 0)
			continue; // No player in this slot

		// Check if the engine considers this player fully authenticated
		if (!g_pEngine->IsClientFullyAuthenticated(slot))
			continue;

		// Populate player info since OnClientConnected was never called
		PlayerInfo *existing = g_CS2APlayerManager.GetPlayer(i);
		if (!existing)
		{
			// Player not tracked yet - populate from engine
			// We can't easily get name/address from engine at this point,
			// so use what we can get from the xuid
			g_CS2APlayerManager.OnClientConnected(i, "", xuid, "", "", false);
		}

		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(i);
		if (!player || player->fakePlayer)
			continue;

		player->authenticated = true;

		// Fill in authid if missing
		if (player->authid.empty() && player->steamid64 != 0)
			player->authid = SteamID64ToAuthId(player->steamid64);

		META_CONPRINTF("[ADMIN] Late load: processing player (%s) in slot %d\n",
			player->authid.c_str(), i);

		// Check bans
		std::string ip = player->ip;
		uint64_t steamid64 = player->steamid64;
		g_CS2ABanManager.VerifyBan(i, steamid64, ip.c_str(),
			[i, steamid64](bool banned, const std::string &reason) {
			if (banned)
			{
				PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
				if (p && p->connected && p->steamid64 == steamid64)
				{
					ADMIN_PrintToClient(i, "[ADMIN] You are banned from this server. Reason: %s\n", reason.c_str());
					META_CONPRINTF("[ADMIN] Late load: kicking banned player (%s). Reason: %s\n",
						p->authid.c_str(), reason.c_str());
					g_pEngine->DisconnectClient(CPlayerSlot(i), NETWORK_DISCONNECT_KICKED_CONVICTEDACCOUNT);
				}
			}
			else
			{
				g_CS2ACommManager.VerifyComms(i, steamid64);
			}
		});

		// Assign admin
		g_CS2AAdminManager.AssignAdminToPlayer(i);
	}
}

void CS2APlugin::OnLevelInit(char const *pMapName, char const *pMapEntities,
	char const *pOldLevel, char const *pLandmarkName,
	bool loadGame, bool background)
{
	META_CONPRINTF("[ADMIN] Map loading: %s\n", pMapName);

	// Grab entity system pointer (available once a map is loaded)
	g_pEntitySystem = GameEntitySystem();
	if (!g_pEntitySystem)
		META_CONPRINTF("[ADMIN] WARNING: Could not acquire CGameEntitySystem\n");

	// Reload admins on map change (matches SM behavior)
	g_CS2AAdminManager.ReloadAdmins();
}

void CS2APlugin::OnLevelShutdown()
{
	META_CONPRINTF("[ADMIN] Map unloading.\n");
}
