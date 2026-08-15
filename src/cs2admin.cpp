#include <stdio.h>
#include <cstdarg>
#include <ctime>
#include "cs2admin.h"

void ShutdownConsoleCommands();
#include "config/config.h"
#include "config/gamedata.h"
#include "db/database.h"
#include "player/player_manager.h"
#include "ban/ban_manager.h"
#include "comm/comm_manager.h"
#include "command/command_system.h"
#include "command/map_manager.h"
#include "chat/chat_processor.h"
#include "compat/foreign_plugins.h"
#include "lang/translations.h"
#include "menu/menu_bridge.h"
#include "tags/tag_manager.h"
#include "admin/admin_manager.h"
#include "public/forwards.h"
#include "queue/offline_queue.h"
#include "utils/print_utils.h"
#include "utils/discord.h"

#include "mmu/chat_command.h"
#include "mmu/gamesystem.h"
#include "mmu/log.h"

#include <sql_mm.h>

#include <schemasystem/schemasystem.h>
#include <interfaces/interfaces.h>
#include <entity2/entitysystem.h>
#include <networksystem/inetworkmessages.h>
#include <engine/igameeventsystem.h>
#include <filesystem.h>
#include "steam/steam_gameserver.h"

#include "iclientcvarvalue.h"

// Entity system global (declared extern in common.h)
// Note: g_pSchemaSystem and g_pGameResourceServiceServer are already defined by the SDK's interfaces.lib
CGameEntitySystem *g_pEntitySystem = nullptr;

CGameEntitySystem *GameEntitySystem()
{
	if (!g_pGameResourceServiceServer)
	{
		return nullptr;
	}

	return *reinterpret_cast<CGameEntitySystem **>(reinterpret_cast<uintptr_t>(g_pGameResourceServiceServer) + gamedata::kGameEntitySystemOffset);
}

// SourceHook hook declarations - must match interface method signatures exactly
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientActive, SH_NOATTRIB, 0, CPlayerSlot, bool, const char *, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64,
				   const char *);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const *, int, uint64);
SH_DECL_HOOK1_void(IServerGameClients, ClientSettingsChanged, SH_NOATTRIB, 0, CPlayerSlot);
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char *, uint64, const char *, const char *, bool);
SH_DECL_HOOK6(IServerGameClients, ClientConnect, SH_NOATTRIB, 0, bool, CPlayerSlot, const char *, uint64, const char *, bool, CBufferString *);
SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandRef, const CCommandContext &, const CCommand &);
SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIActivated, SH_NOATTRIB, 0);

CS2APlugin g_CS2APlugin;
CS2AForwards g_CS2AForwards;

IServerGameDLL *g_pServerGameDLL = nullptr;
IServerGameClients *g_pGameClients = nullptr;
IVEngineServer *g_pEngine = nullptr;
IGameEventManager2 *g_pGameEvents = nullptr;
ICvar *g_pICvar = nullptr;
IGameEventSystem *g_pGameEventSystem = nullptr;
// g_pFullFileSystem is defined by interfaces.lib

// Optional. Provides each client's cl_language for phrase translation.
// May load after us, so it is re-acquired whenever translations reload.
static IClientCvarValue *g_pClientCvarValue = nullptr;

// Steam game-server API context used for workshop validation (ISteamUGC).
CSteamGameServerAPIContext g_AdminSteamAPI;

std::string ADMIN_SlotLanguage(int slot)
{
	const char *raw = g_pClientCvarValue ? g_pClientCvarValue->GetClientLanguage(CPlayerSlot(slot)) : nullptr;
	return g_CS2ATranslations.MapClientLanguage(raw);
}

// Re-acquire ClientCvarValue and reload phrase tables.
void ADMIN_LoadTranslations()
{
	g_pClientCvarValue = static_cast<IClientCvarValue *>(g_SMAPI->MetaFactory(CLIENTCVARVALUE_INTERFACE, nullptr, nullptr));
	g_CS2ATranslations.Load(g_SMAPI->GetBaseDir(), "cs2admin");
	g_CS2ATranslations.SetDefaultLanguage(g_CS2AConfig.defaultLanguage);
}

PLUGIN_EXPOSE(CS2APlugin, g_CS2APlugin);

bool CS2APlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	mmu::log::Setup logSetup;
	logSetup.channelName = "CS2Admin";
	logSetup.addonName = "cs2admin";
	logSetup.toFile = true;
	mmu::log::Init(logSetup);

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pICvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pServerGameDLL, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, g_pGameClients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

	g_SMAPI->AddListener(this, this);

	m_bLateLoaded = late;

	MMU_LOG_INFO("CS2Admin %s loading...%s\n", PLUGIN_FULL_VERSION, late ? " (late)" : "");

	// Engine-native workshop map checks.
	// On failure EnsureWorkshopMapReady silently falls back to the .vpk folder scan + ACF prune path.
	if (!mmu::gamesystem::Resolve(reinterpret_cast<const void *>(g_pServerGameDLL), gamedata::kGameSystemFactorySig,
								  gamedata::kGameSystemFactorySigLen))
	{
		MMU_LOG_WARN("Game system list unresolved; workshop map checks fall back to ACF pruning.\n");
	}

	char configPath[512];
	snprintf(configPath, sizeof(configPath), "%s/cfg/cs2admin/core.cfg", g_SMAPI->GetBaseDir());

	if (!ADMIN_LoadConfig(configPath, g_CS2AConfig))
	{
		MMU_LOG_ERROR("Could not load config from %s\n", configPath);
		MMU_LOG_INFO("Make sure the file exists at: <game_root>/cfg/cs2admin/core.cfg\n");
		MMU_LOG_WARN("Database features will be disabled. Only flat-file admins will be loaded.\n");
		m_bConfigLoaded = false;
	}
	else
	{
		m_bConfigLoaded = true;
		MMU_LOG_INFO("Config loaded. DB: %s@%s:%d/%s, Prefix: %s\n", g_CS2AConfig.dbUser.c_str(), g_CS2AConfig.dbHost.c_str(), g_CS2AConfig.dbPort,
					 g_CS2AConfig.dbName.c_str(), g_CS2AConfig.databasePrefix.c_str());
		mmu::log::SetToFile(g_CS2AConfig.logToFile);
		mmu::log::SetRetentionDays(g_CS2AConfig.logRetentionDays);
	}

	ADMIN_LoadTranslations();

	SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pServerGameDLL, SH_MEMBER(this, &CS2APlugin::Hook_GameFrame), true);
	SH_ADD_HOOK(IServerGameClients, ClientActive, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientActive), true);
	SH_ADD_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientDisconnect), true);
	SH_ADD_HOOK(IServerGameClients, ClientPutInServer, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientPutInServer), true);
	SH_ADD_HOOK(IServerGameClients, ClientSettingsChanged, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientSettingsChanged), false);
	SH_ADD_HOOK(IServerGameClients, OnClientConnected, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_OnClientConnected), false);
	SH_ADD_HOOK(IServerGameClients, ClientConnect, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientConnect), false);
	SH_ADD_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &CS2APlugin::Hook_DispatchConCommand), false);
	SH_ADD_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pServerGameDLL, SH_MEMBER(this, &CS2APlugin::Hook_GameServerSteamAPIActivated), true);

	g_pCVar = g_pICvar;
	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);

	g_CS2ACommandSystem.RegisterBuiltinCommands();

	g_CS2ACommandSystem.RegisterConsoleCommands();

	g_CS2AMapManager.LoadMapList();

	g_CS2ADiscord.Init();

	MMU_LOG_INFO("Plugin loaded successfully.\n");
	return true;
}

bool CS2APlugin::Unload(char *error, size_t maxlen)
{
	// Remove all SourceHook hooks first so no new requests come in.
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pServerGameDLL, SH_MEMBER(this, &CS2APlugin::Hook_GameFrame), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientActive, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientActive), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientDisconnect), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientPutInServer, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientPutInServer), true);
	SH_REMOVE_HOOK(IServerGameClients, ClientSettingsChanged, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientSettingsChanged), false);
	SH_REMOVE_HOOK(IServerGameClients, OnClientConnected, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_OnClientConnected), false);
	SH_REMOVE_HOOK(IServerGameClients, ClientConnect, g_pGameClients, SH_MEMBER(this, &CS2APlugin::Hook_ClientConnect), false);
	SH_REMOVE_HOOK(ICvar, DispatchConCommand, g_pICvar, SH_MEMBER(this, &CS2APlugin::Hook_DispatchConCommand), false);
	SH_REMOVE_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pServerGameDLL, SH_MEMBER(this, &CS2APlugin::Hook_GameServerSteamAPIActivated),
				   true);

	// Cancel any open external menus and drop the ICS2Menus pointer before our code unloads,
	//  so mm-cs2menus never invokes a lambda inside this DLL.
	g_AdminMenus.Shutdown();

	// Unregister and delete all dynamically created mm_* ConCommands.
	g_CS2ACommandSystem.Shutdown();

	// Explicitly unregister the static CON_COMMAND_F commands (mm_reload, etc.)
	ShutdownConsoleCommands();

	// Drop all registered forward callbacks so lambdas captured from other
	g_CS2AForwards.Shutdown();

	// Flush and join the Discord worker thread before touching other state.
	g_CS2ADiscord.Shutdown();

	if (g_CS2AOfflineQueue.HasItems())
	{
		g_CS2AOfflineQueue.SaveToFile();
	}

	// Mark DB as shutting down (blocks new Query() calls) then destroy the
	// connection, which cancels any pending sql_mm callbacks in its queue.
	g_CS2ADatabase.Shutdown();

	// Clear the cached Steam API context
	g_AdminSteamAPI.Clear();

	MMU_LOG_INFO("Plugin unloaded.\n");

	// Last, the engine must not keep a listener pointing into this DLL.
	mmu::log::Shutdown();
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

	bool CanUseCommand(int slot, const char *commandName, const char *commandGroup, uint32_t defaultFlag) override
	{
		return g_CS2AAdminManager.CanPlayerUseCommand(slot, commandName, commandGroup, defaultFlag);
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
		{
			return;
		}

		if (g_CS2AForwards.FireOnKickPlayer(targetSlot, adminSlot, reason ? reason : "Kicked"))
		{
			return;
		}

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
		if (ret)
		{
			*ret = META_IFACE_OK;
		}
		return static_cast<ICS2Admin *>(&g_CS2AdminAPI);
	}
	if (!strcmp(iface, CS2ADMIN_FORWARDS_INTERFACE))
	{
		if (ret)
		{
			*ret = META_IFACE_OK;
		}
		return static_cast<ICS2AdminForwards *>(&g_CS2AForwards);
	}

	if (ret)
	{
		*ret = META_IFACE_FAILED;
	}
	return nullptr;
}

void CS2APlugin::AllPluginsLoaded()
{
	// Resolve the optional mm-cs2menus interface now that all plugins are up.
	g_AdminMenus.Init();

	// Everything is loaded, so any other chat owner has registered its convars by now.
	g_CS2AForeignPlugins.Refresh();
	g_CS2ATagManager.LoadTags();

	// Always load flat-file admins regardless of DB state
	auto loadFlatFileOnly = [this]()
	{
		g_CS2AAdminManager.LoadFlatFileAdmins();
		g_CS2AAdminManager.MergeAndApplyAll();
		if (m_bLateLoaded)
		{
			OnLateLoad();
		}
	};

	// If config wasn't loaded, skip DB entirely
	if (!m_bConfigLoaded)
	{
		MMU_LOG_WARN("No config loaded - skipping database. Only flat-file admins will be used.\n");
		loadFlatFileOnly();
		return;
	}

	// This is where sql_mm should be available
	if (!g_CS2ADatabase.Init())
	{
		MMU_LOG_WARN("Failed to initialize database interface. Is sql_mm loaded?\n");
		MMU_LOG_WARN("Ban checking disabled. Only flat-file admins will be used.\n");
		loadFlatFileOnly();
		return;
	}

	g_CS2ADatabase.Connect(
		[this](bool success)
		{
			if (success)
			{
				MMU_LOG_INFO("Database ready.\n");

				// Lookup server ID if set to auto (MySQL/SBPP only)
				if (g_CS2AConfig.serverID == -1 && g_CS2ADatabase.IsMySQL())
				{
					LookupServerID();
				}
				else if (g_CS2AConfig.serverID == -1)
				{
					g_CS2AConfig.serverID = 0;
				}

				// Load and process offline queue
				g_CS2AOfflineQueue.LoadFromFile();
				if (g_CS2AOfflineQueue.HasItems())
				{
					g_CS2AOfflineQueue.ProcessQueue();
				}

				// Load admins from both DB and flat file
				g_CS2AAdminManager.ReloadAdmins();

				g_CS2ATagManager.EnsureSchema();

				// Handle late load after DB is ready
				if (m_bLateLoaded)
				{
					OnLateLoad();
				}
			}
			else
			{
				MMU_LOG_WARN("Database connection failed! Bans will not be enforced.\n");
				// Load and apply flat-file admins only
				g_CS2AAdminManager.LoadFlatFileAdmins();
				g_CS2AAdminManager.MergeAndApplyAll();
			}
		});
}

void CS2APlugin::OnPluginLoad(PluginId /*id*/)
{
	// mm-cs2menus may have just loaded; re-resolve so menus light up without a restart.
	g_AdminMenus.Refresh();
	// A chat owner may have just loaded, in which case we have to stop owning chat.
	g_CS2AForeignPlugins.Refresh();
}

void CS2APlugin::OnPluginUnload(PluginId /*id*/)
{
	// Drop our cached pointer if mm-cs2menus is the one going away.
	g_AdminMenus.Refresh();
	// The departing plugin's convars may still be registered at this point.
	g_CS2AForeignPlugins.Refresh();
}

void CS2APlugin::LookupServerID()
{
	ConVarRefAbstract hostip_ref("hostip");
	ConVarRefAbstract hostport_ref("hostport");

	if (!hostip_ref.IsValidRef() || !hostport_ref.IsValidRef())
	{
		MMU_LOG_WARN("Cannot auto-detect server ID: hostip/hostport cvars not available.\n");
		return;
	}

	int hostip = hostip_ref.GetInt();
	int hostport = hostport_ref.GetInt();

	char ipStr[32];
	snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", (hostip >> 24) & 0xFF, (hostip >> 16) & 0xFF, (hostip >> 8) & 0xFF, hostip & 0xFF);

	std::string prefix = g_CS2AConfig.databasePrefix;
	char query[512];
	snprintf(query, sizeof(query), "SELECT sid FROM %s_servers WHERE ip = '%s' AND port = '%d' LIMIT 1", prefix.c_str(),
			 g_CS2ADatabase.Escape(ipStr).c_str(), hostport);

	g_CS2ADatabase.Query(query,
						 [ipStr = std::string(ipStr), hostport](ISQLQuery *result)
						 {
							 if (!result)
							 {
								 return;
							 }

							 ISQLResult *rs = result->GetResultSet();
							 if (rs && rs->GetRowCount() > 0)
							 {
								 ISQLRow *row = rs->FetchRow();
								 if (row)
								 {
									 g_CS2AConfig.serverID = rs->GetInt(0);
									 MMU_LOG_INFO("Auto-detected server ID: %d\n", g_CS2AConfig.serverID);
								 }
							 }
							 else
							 {
								 MMU_LOG_WARN("Server not found in database (%s:%d). Using serverID=0.\n", ipStr.c_str(), hostport);
								 g_CS2AConfig.serverID = 0;
							 }
						 });
}

bool CS2APlugin::Hook_ClientConnect(CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, bool unk1,
									CBufferString *pRejectReason)
{
	MMU_LOG_INFO("ClientConnect: slot=%d name=\"%s\" xuid=%llu\n", slot.Get(), pszName, (unsigned long long)xuid);

	// Note: We cannot do async DB ban checks here and block, so IP-based bans
	// are checked in ClientPutInServer along with SteamID bans. ClientConnect must return synchronously.

	RETURN_META_VALUE(MRES_IGNORED, true);
}

void CS2APlugin::Hook_OnClientConnected(CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, const char *pszAddress,
										bool bFakePlayer)
{
	int slotIdx = slot.Get();

	// Track player - but defer ban/admin checks until authentication is confirmed (ClientPutInServer)
	g_CS2APlayerManager.OnClientConnected(slotIdx, pszName, xuid, pszNetworkID, pszAddress, bFakePlayer);

	if (bFakePlayer)
	{
		return;
	}

	MMU_LOG_INFO("Client connected: \"%s\" (%s) [%s] slot=%d\n", pszName, SteamID64ToAuthId(xuid).c_str(), pszAddress, slotIdx);

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
	{
		return;
	}

	// Player entity is created and Steam auth is confirmed - safe to check bans/admins
	player->authenticated = true;

	MMU_LOG_INFO("Client authenticated: \"%s\" (%s) slot=%d\n", player->name.c_str(), player->authid.c_str(), slotIdx);

	g_CS2AForwards.FireOnClientAuthorized(slotIdx, player->authid.c_str(), player->steamid64);

	// Check for active bans
	std::string playerIP = player->ip;
	uint64_t steamid64 = player->steamid64;

	g_CS2ABanManager.VerifyBan(slotIdx, steamid64, playerIP.c_str(),
							   [slotIdx, steamid64](bool banned, const std::string &reason)
							   {
								   if (banned)
								   {
									   PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slotIdx);
									   if (p && p->connected && p->steamid64 == steamid64)
									   {
										   ADMIN_PrintToClientT(slotIdx, "[ADMIN] You are banned from this server. Reason: %s\n", reason.c_str());
										   MMU_LOG_INFO("Kicking banned player \"%s\" (%s). Reason: %s\n", p->name.c_str(), p->authid.c_str(),
														reason.c_str());
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

	// Needs the admin entry above, since eligibility reads flags/group/immunity.
	g_CS2ATagManager.LoadPlayerPref(slotIdx, steamid64);
	g_CS2ATagManager.UpdateClanTag(slotIdx);

	// Check ban/comm history to notify admins on connect
	if (g_CS2AConfig.printCheckOnConnect && g_CS2ADatabase.IsConnected())
	{
		g_CS2ABanManager.CheckHistory(slotIdx, steamid64, playerIP.c_str(),
									  [slotIdx, steamid64](int banCount, int commCount, int muteCount, int gagCount)
									  {
										  if (banCount > 0 || commCount > 0)
										  {
											  PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slotIdx);
											  if (p && p->connected && p->steamid64 == steamid64)
											  {
												  ADMIN_ChatToAdminsT("Player \"%s\" (%s) has %d ban(s), %d mute(s), %d gag(s) on record.\n",
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

void CS2APlugin::Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName, uint64 xuid,
									   const char *pszNetworkID)
{
	int slotIdx = slot.Get();
	g_CS2AForwards.FireOnClientDisconnect(slotIdx);
	g_CS2ACommManager.OnClientDisconnect(slotIdx);
	g_CS2ATagManager.OnClientDisconnect(slotIdx);
	g_CS2APlayerManager.OnClientDisconnect(slotIdx);
}

void CS2APlugin::Hook_DispatchConCommand(ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args)
{
	if (!cmd.IsValidRef())
	{
		RETURN_META(MRES_IGNORED);
	}

	CPlayerSlot slot = ctx.GetPlayerSlot();
	int slotIdx = slot.Get();

	// Ignore server console commands (slot -1)
	if (slotIdx < 0 || slotIdx > MAXPLAYERS)
	{
		RETURN_META(MRES_IGNORED);
	}

	const char *cmdName = cmd.GetName();
	if (!cmdName)
	{
		RETURN_META(MRES_IGNORED);
	}

	bool isSay = (strcmp(cmdName, "say") == 0);
	bool isSayTeam = (strcmp(cmdName, "say_team") == 0);

	if (!isSay && !isSayTeam)
	{
		RETURN_META(MRES_IGNORED);
	}

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
							g_CS2ACommManager.GagPlayer(slotIdx, g_CS2AConfig.chatFloodMuteDuration, "Chat flood", -1);
							ADMIN_PrintToChatT(slotIdx, "You have been gagged for %d minute(s) for chat flooding.\n",
											   g_CS2AConfig.chatFloodMuteDuration);
							ADMIN_ChatToAdminsT("%s was auto-gagged for chat flooding.\n", player->name.c_str());
						}
						else
						{
							ADMIN_PrintToChatT(slotIdx, "Slow down! You are sending messages too fast.\n");
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
		MMU_LOG_INFO("Blocked chat from gagged player in slot %d\n", slotIdx);
		RETURN_META(MRES_SUPERCEDE);
	}

	// Render the line ourselves and stop the game from rendering its own.
	if (g_CS2AChatProcessor.ShouldRender(slotIdx))
	{
		g_CS2AChatProcessor.RenderPlayerChat(slotIdx, mmu::StripSayQuotes(message).c_str(), isSayTeam);
		RETURN_META(MRES_SUPERCEDE);
	}

	RETURN_META(MRES_IGNORED);
}

void CS2APlugin::Hook_ClientSettingsChanged(CPlayerSlot slot)
{
	// Could track name changes here if needed
}

void CS2APlugin::Hook_GameServerSteamAPIActivated()
{
	if (g_AdminSteamAPI.SteamUGC())
	{
		RETURN_META(MRES_IGNORED);
	}
	g_AdminSteamAPI.Init();
	RETURN_META(MRES_IGNORED);
}

void CS2APlugin::Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
	{
		return;
	}

	float curtime = globals->curtime;

	// Enforce voice muting every tick.
	// SetClientListening must be called repeatedly because the engine resets the listen matrix each frame.
	for (int i = 0; i < globals->maxClients; i++)
	{
		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(i);
		if (!player || !player->isMuted)
		{
			continue;
		}

		// Block all other players from hearing this muted player
		for (int j = 0; j < globals->maxClients; j++)
		{
			if (i == j)
			{
				continue;
			}

			PlayerInfo *listener = g_CS2APlayerManager.GetPlayer(j);
			if (listener && listener->connected)
			{
				g_pEngine->SetClientListening(CPlayerSlot(j), CPlayerSlot(i), false);
			}
		}
	}

	g_CS2AMapManager.Tick(curtime);

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
		{
			g_CS2AOfflineQueue.ProcessQueue();
		}
	}

	// DB reconnection timer.
	// IsConnecting() must be checked.
	if (m_bConfigLoaded && g_CS2ADatabase.IsInitialized() && !g_CS2ADatabase.IsConnected() && !g_CS2ADatabase.IsConnecting() && !m_bReconnectGaveUp
		&& curtime >= m_flNextReconnect)
	{
		m_flNextReconnect = curtime + g_CS2AConfig.retryTime;
		m_iReconnectAttempts++;

		const int maxAttempts = g_CS2AConfig.maxReconnectAttempts;
		if (maxAttempts > 0)
		{
			MMU_LOG_INFO("Attempting database reconnection (attempt %d/%d)...\n", m_iReconnectAttempts, maxAttempts);
		}
		else
		{
			MMU_LOG_INFO("Attempting database reconnection (attempt %d)...\n", m_iReconnectAttempts);
		}

		g_CS2ADatabase.Reconnect(
			[this](bool success)
			{
				if (success)
				{
					MMU_LOG_INFO("Database reconnected!\n");
					m_iReconnectAttempts = 0;
					m_bReconnectGaveUp = false;
					if (g_CS2AOfflineQueue.HasItems())
					{
						g_CS2AOfflineQueue.ProcessQueue();
					}
				}
			});

		// Give up after the configured number of attempts (0 = unlimited) to stop log spam.
		if (maxAttempts > 0 && m_iReconnectAttempts >= maxAttempts)
		{
			m_bReconnectGaveUp = true;
			MMU_LOG_WARN("Database reconnection failed after %d attempts. Giving up and running offline.\n", maxAttempts);
		}
	}
}

void CS2APlugin::OnLateLoad()
{
	MMU_LOG_INFO("Late load detected - processing existing players...\n");

	// Grab entity system pointer on late load (map already active)
	if (!g_pEntitySystem)
	{
		g_pEntitySystem = GameEntitySystem();
	}

	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
	{
		return;
	}

	for (int i = 0; i < globals->maxClients; i++)
	{
		CPlayerSlot slot(i);
		uint64 xuid = g_pEngine->GetClientXUID(slot);
		if (xuid == 0)
		{
			continue; // No player in this slot
		}

		if (!g_pEngine->IsClientFullyAuthenticated(slot))
		{
			continue;
		}

		PlayerInfo *existing = g_CS2APlayerManager.GetPlayer(i);
		if (!existing)
		{
			g_CS2APlayerManager.OnClientConnected(i, "", xuid, "", "", false);
		}

		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(i);
		if (!player || player->fakePlayer)
		{
			continue;
		}

		player->authenticated = true;

		if (player->authid.empty() && player->steamid64 != 0)
		{
			player->authid = SteamID64ToAuthId(player->steamid64);
		}

		MMU_LOG_INFO("Late load: processing player (%s) in slot %d\n", player->authid.c_str(), i);

		std::string ip = player->ip;
		uint64_t steamid64 = player->steamid64;
		g_CS2ABanManager.VerifyBan(i, steamid64, ip.c_str(),
								   [i, steamid64](bool banned, const std::string &reason)
								   {
									   if (banned)
									   {
										   PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
										   if (p && p->connected && p->steamid64 == steamid64)
										   {
											   ADMIN_PrintToClientT(i, "[ADMIN] You are banned from this server. Reason: %s\n", reason.c_str());
											   MMU_LOG_INFO("Late load: kicking banned player (%s). Reason: %s\n", p->authid.c_str(), reason.c_str());
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

void CS2APlugin::OnLevelInit(char const *pMapName, char const *pMapEntities, char const *pOldLevel, char const *pLandmarkName, bool loadGame,
							 bool background)
{
	MMU_LOG_INFO("Map loading: %s\n", pMapName);

	// Grab entity system pointer (available once a map is loaded)
	g_pEntitySystem = GameEntitySystem();
	if (!g_pEntitySystem)
	{
		MMU_LOG_WARN("Could not acquire CGameEntitySystem\n");
	}

	g_CS2AMapManager.OnMapStart();

	// Reload admins on map change
	g_CS2AAdminManager.ReloadAdmins();
}

void CS2APlugin::OnLevelShutdown()
{
	MMU_LOG_INFO("Map unloading.\n");
}
