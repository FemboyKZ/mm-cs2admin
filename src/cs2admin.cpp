#include <stdio.h>
#include <cstdarg>
#include <ctime>
#include "cs2admin.h"

#include "config/config.h"
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
#include "interfaces/cs2admin/forwards.h"
#include "queue/offline_queue.h"
#include "utils/print_utils.h"
#include "utils/discord_notify.h"

#include "utils/chat_command.h"
#include "game/cvarquery.h"
#include "game/say.h"
#include "sdk/entity/ccsplayercontroller.h"
#include "utils/log.h"
#include "game/voice_block.h"

#include "interfaces/sql_mm/sql_mm.h"

#include <schemasystem/schemasystem.h>
#include <interfaces/interfaces.h>
#include <entity2/entitysystem.h>
#include <networksystem/inetworkmessages.h>
#include <engine2/igameeventsystem.h>
#include <filesystem/ifilesystem.h>

// Entity system global (declared extern in common.h)
// Note: g_pSchemaSystem and g_pGameResourceServiceServer are already defined by the SDK's interfaces.lib
CGameEntitySystem *g_pEntitySystem = nullptr;

CS2APlugin g_ThisPlugin;
CS2AForwards g_CS2AForwards;

IServerGameDLL *g_pServerGameDLL = nullptr;
IServerGameClients *g_pGameClients = nullptr;
IVEngineServer *g_pEngine = nullptr;
ICvar *g_pICvar = nullptr;
IGameEventSystem *g_pGameEventSystem = nullptr;

// g_pFullFileSystem is defined by interfaces.lib

void CS2A_LoadTranslations()
{
	g_CS2ATranslations.Load(g_SMAPI->GetBaseDir(), "cs2admin");
	g_CS2ATranslations.SetDefaultLanguage(g_CS2AConfig.general.defaultLanguage);
}

PLUGIN_EXPOSE(CS2APlugin, g_ThisPlugin);

CS2APlugin::CS2APlugin()
	: m_GameFrame(&IServerGameDLL::GameFrame, this, nullptr, &CS2APlugin::Hook_GameFrame),
	  m_ClientActive(&IServerGameClients::ClientActive, this, nullptr, &CS2APlugin::Hook_ClientActive),
	  m_ClientDisconnect(&IServerGameClients::ClientDisconnect, this, nullptr, &CS2APlugin::Hook_ClientDisconnect),
	  m_ClientPutInServer(&IServerGameClients::ClientPutInServer, this, nullptr, &CS2APlugin::Hook_ClientPutInServer),
	  m_ClientSettingsChanged(&IServerGameClients::ClientSettingsChanged, this, nullptr, &CS2APlugin::Hook_ClientSettingsChanged),
	  m_OnClientConnected(&IServerGameClients::OnClientConnected, this, &CS2APlugin::Hook_OnClientConnected, nullptr),
	  m_ClientConnect(&IServerGameClients::ClientConnect, this, &CS2APlugin::Hook_ClientConnect, nullptr),
	  m_DispatchConCommand(&ICvar::DispatchConCommand, this, &CS2APlugin::Hook_DispatchConCommand, &CS2APlugin::Hook_DispatchConCommandPost),
	  m_PostEvent(static_cast<void (IGameEventSystem::*)(CSplitScreenSlot, bool, int, const uint64 *, INetworkMessageInternal *, const CNetMessage *,
														 unsigned long, NetChannelBufType_t)>(&IGameEventSystem::PostEventAbstract),
				  this, &CS2APlugin::Hook_PostEvent, nullptr),
	  m_PostEventFilter(static_cast<void (IGameEventSystem::*)(CSplitScreenSlot, bool, IRecipientFilter *, INetworkMessageInternal *,
															   const CNetMessage *, unsigned long)>(&IGameEventSystem::PostEventAbstract),
						this, &CS2APlugin::Hook_PostEventFilter, nullptr)
{
}

bool CS2APlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	mmu::log::Init("CS2Admin", "cs2admin");

	MMU_GET_CORE_INTERFACES();
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

	g_SMAPI->AddListener(this, this);

	// Non fatal, translations fall back to the default language.
	mmu::cvarquery::Init(g_pEngine);
	// Non fatal too, it logs and leaves voice mutes unenforced.
	mmu::voiceblock::Init(g_pEngine, [](int slot) { return g_CS2ACommManager.IsMuted(slot); });

	m_bLateLoaded = late;
	m_bSkipLevelInitReload = !late;

	MMU_LOG_INFO("CS2Admin %s loading...%s\n", PLUGIN_FULL_VERSION, late ? " (late)" : "");

	char configPath[512];
	snprintf(configPath, sizeof(configPath), "%s/cfg/cs2admin/core.cfg", g_SMAPI->GetBaseDir());

	if (!CS2A_LoadConfig(configPath, g_CS2AConfig))
	{
		MMU_LOG_ERROR("Could not load config from %s\n", configPath);
		MMU_LOG_WARN("Make sure the file exists at: <game_root>/cfg/cs2admin/core.cfg\n");
		MMU_LOG_WARN("Database features will be disabled. Only flat-file admins will be loaded.\n");
		m_bConfigLoaded = false;
	}
	else
	{
		m_bConfigLoaded = true;
		MMU_LOG_INFO("Config loaded. DB: %s@%s:%d/%s, Prefix: %s\n", g_CS2AConfig.database.user.c_str(), g_CS2AConfig.database.host.c_str(),
					 g_CS2AConfig.database.port, g_CS2AConfig.database.name.c_str(), g_CS2AConfig.database.prefix.c_str());
	}

	CS2A_LoadTranslations();

	m_GameFrame.Add(g_pServerGameDLL);
	m_ClientActive.Add(g_pGameClients);
	m_ClientDisconnect.Add(g_pGameClients);
	m_ClientPutInServer.Add(g_pGameClients);
	m_ClientSettingsChanged.Add(g_pGameClients);
	m_OnClientConnected.Add(g_pGameClients);
	m_ClientConnect.Add(g_pGameClients);
	m_DispatchConCommand.Add(g_pICvar);
	m_PostEvent.Add(g_pGameEventSystem);
	m_PostEventFilter.Add(g_pGameEventSystem);

	g_pCVar = g_pICvar;
	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);

	g_CS2AForeignPlugins.Init();

	g_CS2ACommandSystem.RegisterBuiltinCommands();

	// First, so mm_maps is left to a loaded mm-cs2rockthevote.
	g_CS2AMapManager.RefreshRtv();
	g_CS2ACommandSystem.RegisterConsoleCommands();

	g_CS2AMapManager.LoadMapList();

	g_CS2ADiscord.Init();

	MMU_LOG_INFO("Plugin loaded successfully.\n");
	return true;
}

bool CS2APlugin::Unload(char *error, size_t maxlen)
{
	// Remove all hooks first so no new requests come in.
	m_GameFrame.Remove(g_pServerGameDLL);
	m_ClientActive.Remove(g_pGameClients);
	m_ClientDisconnect.Remove(g_pGameClients);
	m_ClientPutInServer.Remove(g_pGameClients);
	m_ClientSettingsChanged.Remove(g_pGameClients);
	m_OnClientConnected.Remove(g_pGameClients);
	m_ClientConnect.Remove(g_pGameClients);
	m_DispatchConCommand.Remove(g_pICvar);
	m_PostEvent.Remove(g_pGameEventSystem);
	m_PostEventFilter.Remove(g_pGameEventSystem);

	g_CS2AForeignPlugins.Shutdown();
	g_CS2ATagManager.ReleaseClanTags();

	mmu::voiceblock::Shutdown();

	// Drops pending callbacks pointing into this binary.
	mmu::cvarquery::Shutdown();

	// Cancel any open external menus and drop the ICS2Menus pointer before our code unloads,
	// so mm-cs2menus never invokes a lambda inside this binary.
	g_AdminMenus.Shutdown();
	g_CS2AMapManager.ShutdownRtv();
	g_CS2AMapManager.ClearPendingChange();

	// Unregister and delete all dynamically created mm_* ConCommands.
	g_CS2ACommandSystem.Shutdown();

	ConVar_Unregister();

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
		PlayerInfo *p = g_CS2APlayerManager.Get(slot);
		return p && p->connected;
	}

	const char *GetPlayerName(int slot) override
	{
		PlayerInfo *p = g_CS2APlayerManager.Get(slot);
		return (p && p->connected) ? p->name.c_str() : nullptr;
	}

	const char *GetPlayerAuthId(int slot) override
	{
		PlayerInfo *p = g_CS2APlayerManager.Get(slot);
		return (p && p->connected) ? p->authid.c_str() : nullptr;
	}

	uint64_t GetPlayerSteamID64(int slot) override
	{
		PlayerInfo *p = g_CS2APlayerManager.Get(slot);
		return (p && p->connected) ? p->steamid64 : 0;
	}

	const char *GetPlayerIP(int slot) override
	{
		PlayerInfo *p = g_CS2APlayerManager.Get(slot);
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

	bool IsChatHidden(int slot) override
	{
		return g_CS2ACommandSystem.IsChatHidden(slot);
	}

	// Programmatic admin actions

	void BanPlayer(int targetSlot, int adminSlot, int timeMinutes, const char *reason) override
	{
		g_CS2ABanManager.BanPlayer(targetSlot, timeMinutes, reason ? reason : "Banned", adminSlot);
	}

	void KickPlayer(int targetSlot, int adminSlot, const char *reason) override
	{
		PlayerInfo *p = g_CS2APlayerManager.Get(targetSlot);
		if (!p || !p->connected)
		{
			return;
		}

		if (g_CS2AForwards.FireOnKickPlayer(targetSlot, adminSlot, reason ? reason : "Kicked"))
		{
			return;
		}

		CS2A_LogAction(adminSlot, (std::string("Kicked ") + p->name + ": " + (reason ? reason : "Kicked")).c_str());
		CS2A_PrintToClientT(targetSlot, "[ADMIN] You have been kicked. Reason: %s\n", reason ? reason : "Kicked");
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

void CS2A_RecheckConnectedPlayers()
{
	for (int i = 0; i < MAXPLAYERS; i++)
	{
		PlayerInfo *p = g_CS2APlayerManager.Get(i);
		if (!p || !p->connected || p->fakePlayer || !p->inGame)
		{
			continue;
		}

		// A player who joined while the DB was down has no stored tag pick either.
		g_CS2ATagManager.LoadPlayerPref(i, p->steamid64);

		g_CS2ABanManager.VerifyBan(i, p->steamid64, p->ip.c_str(),
								   [i, steamid64 = p->steamid64](bool banned, const std::string &reason)
								   {
									   PlayerInfo *pp = g_CS2APlayerManager.Get(i);
									   if (!pp || !pp->connected || pp->steamid64 != steamid64)
									   {
										   return;
									   }
									   if (!banned)
									   {
										   g_CS2ACommManager.VerifyComms(i, steamid64);
										   return;
									   }
									   CS2ABanManager::PrintBanNotice(i, reason.c_str());
									   MMU_LOG_INFO("Kicking banned player \"%s\" (%s). Reason: %s\n", pp->name.c_str(), pp->authid.c_str(),
													reason.c_str());
									   g_pEngine->DisconnectClient(CPlayerSlot(i), NETWORK_DISCONNECT_KICKED_CONVICTEDACCOUNT);
								   });
	}
}

void CS2APlugin::AllPluginsLoaded()
{
	// Resolve the optional mm-cs2menus interface now that all plugins are up.
	g_AdminMenus.Init();
	g_CS2AMapManager.RefreshRtv();

	// Everything is loaded, so any other chat owner has registered its convars by now.
	g_CS2AForeignPlugins.Refresh();
	g_CS2ATagManager.LoadTags();

	// Always load flat-file admins regardless of DB state.
	// ReloadAdmins takes its flat-file-only path when the DB is not connected,
	// and its flat-file half is synchronous, so the players the late-load pass below registers are authorized against those admins.
	g_CS2AAdminManager.ReloadAdmins();

	// Registering existing players cannot wait for the DB: without a PlayerInfo they cannot chat or run commands at all.
	// The ban and comm checks are picked up by the connect callback below.
	if (m_bLateLoaded)
	{
		OnLateLoad();
	}

	// If config wasn't loaded, skip DB entirely
	if (!m_bConfigLoaded)
	{
		MMU_LOG_WARN("No config loaded - skipping database. Only flat-file admins will be used.\n");
		return;
	}

	StartDatabase();
}

void CS2APlugin::StartDatabase()
{
	// This is where sql_mm should be available
	if (!g_CS2ADatabase.Init())
	{
		MMU_LOG_WARN("Failed to initialize database interface. Is sql_mm loaded?\n");
		MMU_LOG_WARN("Ban checking disabled. Only flat-file admins will be used.\n");
		return;
	}

	// Read once, before anything can be queued.
	// Enqueue rewrites the file from memory, so a later read would either come too late to save the old entries or load duplicates of new ones.
	// Still the first read, since nothing is queued before Init succeeds and it only succeeds once.
	// Queued bans and blocks go back to being enforced from memory.
	g_CS2AOfflineQueue.LoadFromFile(
		[](const std::string &note)
		{
			std::function<void()> written = g_CS2ABanManager.RestorePending(note);
			return written ? written : g_CS2ACommManager.RestorePending(note);
		});

	g_CS2ADatabase.Connect(
		[this](bool success)
		{
			if (success)
			{
				MMU_LOG_INFO("Database ready.\n");
				OnDatabaseReady();
			}
			else
			{
				MMU_LOG_WARN("Database connection failed! Bans will not be enforced.\n");
				// The startup reload ran before Init and skipped the admin backup.
				g_CS2AAdminManager.ReloadAdmins();
			}
		});
}

void CS2APlugin::OnDatabaseReady()
{
	m_iReconnectAttempts = 0;
	m_bReconnectGaveUp = false;

	// Auto server ID (MySQL/SBPP only). Without it every new row carries sid 0.
	if (g_CS2AConfig.general.serverId == -1 && g_CS2ADatabase.IsMySQL())
	{
		LookupServerID(g_CS2AConfig.general.autoAddServer != 0);
	}
	else if (g_CS2AConfig.general.serverId == -1)
	{
		g_CS2AConfig.general.serverId = 0;
	}

	g_CS2ATagManager.EnsureSchema();

	if (g_CS2AOfflineQueue.HasItems())
	{
		g_CS2AOfflineQueue.ProcessQueue();
	}

	// Admin data is whatever the last reload could reach. Pick up the DB side now.
	g_CS2AAdminManager.ReloadAdmins();

	// Anyone who connected while the DB was unreachable was never ban or comm checked.
	CS2A_RecheckConnectedPlayers();
}

void CS2APlugin::ReloadConfig()
{
	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/cs2admin/core.cfg", g_SMAPI->GetBaseDir());

	// The server ID lookup only runs on connect, so an auto-detected one would otherwise be lost.
	const int serverId = g_CS2AConfig.general.serverId;
	if (!CS2A_LoadConfig(path, g_CS2AConfig))
	{
		MMU_LOG_WARN("Failed to reload config from %s, keeping the loaded one.\n", path);
		return;
	}
	if (g_CS2AConfig.general.serverId == -1)
	{
		g_CS2AConfig.general.serverId = serverId;
	}
	MMU_LOG_INFO("Config reloaded from %s\n", path);
	OnConfigReloaded();
}

void CS2APlugin::OnConfigReloaded()
{
	if (!m_bConfigLoaded)
	{
		// Startup skipped the DB for want of a config.
		m_bConfigLoaded = true;
		StartDatabase();
		return;
	}

	// Also how an admin gets the DB back after the reconnect loop gave up.
	if (m_bReconnectGaveUp)
	{
		MMU_LOG_INFO("Resuming database reconnection attempts.\n");
	}
	m_iReconnectAttempts = 0;
	m_bReconnectGaveUp = false;
	m_flNextReconnect = 0.0;

	if (!g_CS2ADatabase.IsInitialized())
	{
		StartDatabase();
	}
}

void CS2APlugin::OnPluginLoad(PluginId /*id*/)
{
	// mm-cs2menus may have just loaded; re-resolve so menus light up without a restart.
	g_AdminMenus.Refresh();
	// A chat owner may have just loaded, in which case we have to stop owning chat.
	g_CS2AForeignPlugins.Refresh();
	g_CS2AMapManager.RefreshRtv();
	g_CS2ADatabase.Refresh();
}

void CS2APlugin::OnPluginUnload(PluginId id)
{
	g_CS2ADatabase.Refresh();
	// Drop our cached pointer if mm-cs2menus is the one going away.
	g_AdminMenus.Refresh();
	// The departing plugin's convars and interface may still be registered at this point.
	g_CS2AForeignPlugins.Refresh(id);
	g_CS2AMapManager.RefreshRtv(id);
	g_CS2AForwards.DropOwnedBy(id);
}

void CS2APlugin::LookupServerID(bool allowAutoAdd)
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

	std::string prefix = g_CS2AConfig.database.prefix;
	char query[512];
	snprintf(query, sizeof(query), "SELECT sid FROM %s_servers WHERE ip = '%s' AND port = '%d' LIMIT 1", prefix.c_str(),
			 g_CS2ADatabase.Escape(ipStr).c_str(), hostport);

	g_CS2ADatabase.Query(query,
						 [this, ipStr = std::string(ipStr), hostport, allowAutoAdd](ISQLQuery *result)
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
									 g_CS2AConfig.general.serverId = rs->GetInt(0);
									 MMU_LOG_INFO("Auto-detected server ID: %d\n", g_CS2AConfig.general.serverId);
								 }
							 }
							 // hostip 0 is no usable address to register.
							 else if (allowAutoAdd && ipStr != "0.0.0.0")
							 {
								 AddServerRow(ipStr, hostport);
							 }
							 else
							 {
								 MMU_LOG_WARN("Server not found in database (%s:%d). Using serverID=0.\n", ipStr.c_str(), hostport);
								 g_CS2AConfig.general.serverId = 0;
							 }
						 });
}

void CS2APlugin::AddServerRow(const std::string &ip, int port)
{
	MMU_LOG_INFO("Server %s:%d not in database, adding it (AutoAddServer).\n", ip.c_str(), port);

	// The panel's mod folder may be cs2 or csgo. No match leaves modid 0.
	const char *prefix = g_CS2AConfig.database.prefix.c_str();
	char query[512];
	snprintf(
		query, sizeof(query),
		"INSERT INTO %s_servers (ip, port, rcon, modid, enabled) "
		"VALUES ('%s', %d, '', IFNULL((SELECT mid FROM %s_mods WHERE modfolder IN ('cs2', 'csgo') ORDER BY modfolder = 'cs2' DESC LIMIT 1), 0), 1)",
		prefix, g_CS2ADatabase.Escape(ip.c_str()).c_str(), port, prefix);

	g_CS2ADatabase.Query(query,
						 [this](ISQLQuery *result)
						 {
							 if (!result)
							 {
								 return;
							 }
							 // Re-read the row rather than trust an insert id. false so it never inserts twice.
							 LookupServerID(false);
						 });
}

KHook::Return<bool> CS2APlugin::Hook_ClientConnect(IServerGameClients *, CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID,
												   bool unk1, CBufferString *pRejectReason)
{
	MMU_LOG_INFO("ClientConnect: slot=%d name=\"%s\" xuid=%llu\n", slot.Get(), pszName, (unsigned long long)xuid);

	// Note: We cannot do async DB ban checks here and block, so IP-based bans
	// are checked in ClientPutInServer along with SteamID bans. ClientConnect must return synchronously.

	return {KHook::Action::Ignore, true};
}

KHook::Return<void> CS2APlugin::Hook_OnClientConnected(IServerGameClients *, CPlayerSlot slot, const char *pszName, uint64 xuid,
													   const char *pszNetworkID, const char *pszAddress, bool bFakePlayer)
{
	int slotIdx = slot.Get();

	// Whoever had this slot before may have been an admin, and their flags must not carry over.
	g_CS2AAdminManager.ClearPlayerAdmin(slotIdx);

	// Track player - but defer the ban check until ClientPutInServer, and admin until Steam confirms the SteamID
	g_CS2APlayerManager.OnClientConnected(slotIdx, pszName, xuid, pszAddress, bFakePlayer);
	mmu::cvarquery::OnClientConnected(slotIdx, bFakePlayer);

	if (bFakePlayer)
	{
		return {KHook::Action::Ignore};
	}

	MMU_LOG_INFO("Client connected: \"%s\" (%s) [%s] slot=%d\n", pszName, SteamID64ToAuthId(xuid).c_str(), pszAddress, slotIdx);

	g_CS2AForwards.FireOnClientConnected(slotIdx, pszName, xuid, pszAddress);
	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2APlugin::Hook_ClientActive(IServerGameClients *, CPlayerSlot slot, bool bLoadGame, const char *pszName, uint64 xuid)
{
	// Player is fully in-game.
	// cs2kz drops clan tag overrides for slots that are not in game, so the one sent from ClientPutInServer may not have landed.
	g_CS2ATagManager.UpdateClanTag(slot.Get());
	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2APlugin::Hook_ClientPutInServer(IServerGameClients *, CPlayerSlot slot, char const *pszName, ClientPutInServerType_t type, uint64 xuid)
{
	int slotIdx = slot.Get();
	g_CS2APlayerManager.OnClientPutInServer(slotIdx);
	PlayerInfo *player = g_CS2APlayerManager.Get(slotIdx);
	if (!player || player->fakePlayer)
	{
		return {KHook::Action::Ignore};
	}

	// Check for active bans
	// On the SteamID the client claims: Steam may not have confirmed it yet, and a ban must not wait on that.
	std::string playerIP = player->ip;
	uint64_t steamid64 = player->steamid64;

	g_CS2ABanManager.VerifyBan(slotIdx, steamid64, playerIP.c_str(),
							   [slotIdx, steamid64](bool banned, const std::string &reason)
							   {
								   if (banned)
								   {
									   PlayerInfo *p = g_CS2APlayerManager.Get(slotIdx);
									   if (p && p->connected && p->steamid64 == steamid64)
									   {
										   CS2ABanManager::PrintBanNotice(slotIdx, reason.c_str());
										   MMU_LOG_INFO("Kicking banned player \"%s\" (%s). Reason: %s\n", p->name.c_str(), p->authid.c_str(),
														reason.c_str());
										   g_pEngine->DisconnectClient(CPlayerSlot(slotIdx), NETWORK_DISCONNECT_KICKED_CONVICTEDACCOUNT);
									   }
								   }
							   });

	// Not chained behind the ban result, so a gag lands as early as the DB allows.
	// If the player turns out banned, the comm result is dropped once their slot is gone.
	g_CS2ACommManager.VerifyComms(slotIdx, steamid64);

	// Check ban/comm history to notify admins on connect
	if (g_CS2AConfig.checker.printCheckOnConnect && g_CS2ADatabase.IsConnected())
	{
		g_CS2ABanManager.CheckHistory(slotIdx, steamid64, playerIP.c_str(),
									  [slotIdx, steamid64](int banCount, int commCount, int muteCount, int gagCount)
									  {
										  if (banCount > 0 || commCount > 0)
										  {
											  PlayerInfo *p = g_CS2APlayerManager.Get(slotIdx);
											  if (p && p->connected && p->steamid64 == steamid64)
											  {
												  CS2A_ChatToAdminsT("Player \"%s\" (%s) has %d ban(s), %d mute(s), %d gag(s) on record.\n",
																	 p->name.c_str(), p->authid.c_str(), banCount, muteCount, gagCount);
											  }
										  }
									  });
	}

	// Steam was faster than the client's loading screen.
	if (player->authenticated)
	{
		OnClientAuthorized(slotIdx);
	}
	return {KHook::Action::Ignore};
}

// In server and confirmed by Steam, whichever came last.
void CS2APlugin::OnClientAuthorized(int slot)
{
	PlayerInfo *player = g_CS2APlayerManager.Get(slot);
	if (!player)
	{
		return;
	}

	MMU_LOG_INFO("Client authenticated: \"%s\" (%s) slot=%d\n", player->name.c_str(), player->authid.c_str(), slot);

	g_CS2AForwards.FireOnClientAuthorized(slot, player->authid.c_str(), player->steamid64);
	g_CS2AForwards.FireOnClientPreAdminCheck(slot);

	// Assign admin permissions (merges DB + flat file entries)
	g_CS2AAdminManager.AssignAdminToPlayer(slot);

	// Needs the admin entry above, since eligibility reads flags/group/immunity.
	g_CS2ATagManager.LoadPlayerPref(slot, player->steamid64);
	g_CS2ATagManager.UpdateClanTag(slot);

	// check for ban evasion
	if (g_CS2AConfig.sleuth.action > 0 && g_CS2ADatabase.IsConnected())
	{
		g_CS2ABanManager.CheckSleuth(slot, player->steamid64, player->ip.c_str());
	}
}

KHook::Return<void> CS2APlugin::Hook_ClientDisconnect(IServerGameClients *, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName,
													  uint64 xuid, const char *pszNetworkID)
{
	int slotIdx = slot.Get();
	g_CS2AForwards.FireOnClientDisconnect(slotIdx);
	g_CS2ACommManager.OnClientDisconnect(slotIdx);
	g_CS2ATagManager.OnClientDisconnect(slotIdx);
	g_CS2AAdminManager.ClearPlayerAdmin(slotIdx);
	g_CS2APlayerManager.OnClientDisconnect(slotIdx);
	mmu::cvarquery::OnClientDisconnect(slotIdx);
	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2APlugin::Hook_DispatchConCommand(ICvar *, ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args)
{
	bool isSayTeam = false;
	int slotIdx = mmu::SaySlot(cmd, ctx, &isSayTeam);
	if (slotIdx < 0)
	{
		return {KHook::Action::Ignore};
	}

	// Before any early return, the post hook pops this no matter how the pre hooks end.
	g_CS2AChatProcessor.PushSay(slotIdx);

	// A client that isn't put in server yet can't legitimately chat, and the game can print its line as a console message.
	PlayerInfo *speaker = g_CS2APlayerManager.Get(slotIdx);
	if (!speaker || (!speaker->fakePlayer && !speaker->inGame))
	{
		MMU_LOG_INFO("Blocked chat from slot %d, not fully in game.\n", slotIdx);
		return {KHook::Action::Supersede};
	}

	const char *message = args.ArgC() > 1 ? args.Arg(1) : "";

	// Prompt answers, like a typed ban reason, are hidden.
	if (g_CS2ACommandSystem.ConsumePromptedText(slotIdx, message))
	{
		return {KHook::Action::Supersede};
	}

	// Chat flood detection (before command processing)
	if (g_CS2AConfig.chatFlood.cooldown > 0.0f)
	{
		PlayerInfo *player = g_CS2APlayerManager.Get(slotIdx);
		if (player && player->connected && !player->fakePlayer)
		{
			// Not curtime, which restarts every map and would make every message after a map change look like flooding.
			double now = Plat_FloatTime();
			double timeSince = now - player->lastChatTime;

			if (timeSince < g_CS2AConfig.chatFlood.cooldown)
			{
				player->chatMessageCount++;

				if (player->chatMessageCount >= g_CS2AConfig.chatFlood.maxMessages)
				{
					// GagPlayer announces the gag itself, and returns false when a forward blocked it.
					bool autoGagged = g_CS2AConfig.chatFlood.muteMinutes > 0 && !player->isGagged
									  && g_CS2ACommManager.GagPlayer(slotIdx, g_CS2AConfig.chatFlood.muteMinutes, "Chat flood", -1);
					if (!autoGagged)
					{
						CS2A_PrintToChatT(slotIdx, "Slow down! You are sending messages too fast.\n");
					}

					player->chatMessageCount = 0;
					player->lastChatTime = now;
					return {KHook::Action::Supersede};
				}
			}
			else
			{
				// Reset counter if enough time passed
				player->chatMessageCount = 0;
			}

			player->lastChatTime = now;
		}
	}

	// Process chat commands (! and / prefixed) BEFORE checking gag.
	// This allows gagged players to still run commands like !ungag requests.
	// A command with the normal prefix stays visible, so it falls through to the gag check and chat rendering below.
	bool silent = false;
	if (g_CS2ACommandSystem.ProcessChatMessage(slotIdx, message, isSayTeam, silent) && silent)
	{
		return {KHook::Action::Supersede};
	}

	// Block gagged players from sending normal chat
	if (g_CS2ACommandSystem.ShouldBlockChat(slotIdx))
	{
		MMU_LOG_INFO("Blocked chat from gagged player in slot %d\n", slotIdx);
		return {KHook::Action::Supersede};
	}

	// Render the line ourselves and stop the game from rendering its own.
	// The line itself is swapped in Hook_PostEvent, and only if no plugin supersedes this say.
	if (g_CS2AChatProcessor.ShouldRender(slotIdx))
	{
		g_CS2AChatProcessor.RenderSay(mmu::StripSayQuotes(message).c_str(), isSayTeam);
	}

	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2APlugin::Hook_DispatchConCommandPost(ICvar *, ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args)
{
	// Must match the pre hook, which pushes each say popped here.
	if (mmu::SaySlot(cmd, ctx) >= 0)
	{
		g_CS2AChatProcessor.PopSay();
		g_CS2ACommandSystem.EndSay(ctx.GetPlayerSlot().Get());
	}
	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2APlugin::Hook_PostEvent(IGameEventSystem *, CSplitScreenSlot nSlot, bool bLocalOnly, int nClientCount, const uint64 *clients,
											   INetworkMessageInternal *pEvent, const CNetMessage *pData, unsigned long nSize,
											   NetChannelBufType_t bufType)
{
	int speaker = g_CS2AChatProcessor.MatchGameLine(pEvent, pData);
	if (speaker < 0)
	{
		return {KHook::Action::Ignore};
	}

	// A null mask means every client. Otherwise bit N is slot N, and 64 slots fit in the first word.
	CPlayerBitVec recipients;
	for (int i = 0; i < 64; i++)
	{
		if (!clients || (clients[0] & (1ull << i)))
		{
			recipients.Set(i);
		}
	}
	g_CS2AChatProcessor.RenderPending(speaker, recipients);
	return {KHook::Action::Supersede};
}

KHook::Return<void> CS2APlugin::Hook_PostEventFilter(IGameEventSystem *, CSplitScreenSlot nSlot, bool bLocalOnly, IRecipientFilter *pFilter,
													 INetworkMessageInternal *pEvent, const CNetMessage *pData, unsigned long nSize)
{
	int speaker = g_CS2AChatProcessor.MatchGameLine(pEvent, pData);
	if (speaker < 0 || !pFilter)
	{
		return {KHook::Action::Ignore};
	}
	g_CS2AChatProcessor.RenderPending(speaker, pFilter->GetRecipients());
	return {KHook::Action::Supersede};
}

KHook::Return<void> CS2APlugin::Hook_ClientSettingsChanged(IServerGameClients *, CPlayerSlot slot)
{
	// Without this, announcements, logs and ban rows keep the name the player joined with.
	PlayerInfo *player = g_CS2APlayerManager.Get(slot.Get());
	if (!player || player->fakePlayer)
	{
		return {KHook::Action::Ignore};
	}
	// A post hook, so the game has applied the new settings to the controller by now.
	CCSPlayerController *controller = CCSPlayerController::FromSlot(slot.Get());
	const char *name = controller ? controller->GetPlayerName() : nullptr;
	if (name && *name && player->name != name)
	{
		MMU_LOG_INFO("Player \"%s\" (%s) is now \"%s\".\n", player->name.c_str(), player->authid.c_str(), name);
		player->name = name;
	}
	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2APlugin::Hook_GameFrame(IServerGameDLL *, bool simulating, bool bFirstTick, bool bLastTick)
{
	CGlobalVars *globals = mmu::GetGameGlobals();
	if (!globals)
	{
		return {KHook::Action::Ignore};
	}

	g_CS2AMapManager.Tick();
	g_CS2ATagManager.ReassertClanTags();
	g_CS2ABanManager.RunFrame();

	// Steam confirms a SteamID some time after the client connects, before or after ClientPutInServer.
	g_CS2APlayerManager.RunFrame(
		[this](int slot)
		{
			if (g_CS2APlayerManager.Get(slot)->inGame)
			{
				OnClientAuthorized(slot);
			}
		});

	const double now = Plat_FloatTime();

	// Check for expired timed comm blocks (every 1 second)
	if (now >= m_flNextExpiryCheck)
	{
		m_flNextExpiryCheck = now + 1.0;
		g_CS2ACommManager.CheckExpiredComms();
	}

	// Process offline queue (every processQueueMinutes minutes)
	if (now >= m_flNextQueueProcess)
	{
		m_flNextQueueProcess = now + (g_CS2AConfig.general.processQueueMinutes * 60.0);
		if (g_CS2ADatabase.IsConnected() && g_CS2AOfflineQueue.HasItems())
		{
			g_CS2AOfflineQueue.ProcessQueue();
		}
	}

	// DB reconnection timer.
	// IsConnecting() must be checked.
	if (m_bConfigLoaded && g_CS2ADatabase.IsInitialized() && !g_CS2ADatabase.IsConnected() && !g_CS2ADatabase.IsConnecting() && !m_bReconnectGaveUp
		&& now >= m_flNextReconnect)
	{
		m_flNextReconnect = now + g_CS2AConfig.general.retryTime;
		m_iReconnectAttempts++;

		const int maxAttempts = g_CS2AConfig.general.maxReconnectAttempts;
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
					OnDatabaseReady();
				}
			});

		// Give up after the configured number of attempts (0 = unlimited) to stop log spam.
		if (maxAttempts > 0 && m_iReconnectAttempts >= maxAttempts)
		{
			m_bReconnectGaveUp = true;
			MMU_LOG_WARN("Database reconnection failed after %d attempts. Giving up and running offline.\n", maxAttempts);
		}
	}
	return {KHook::Action::Ignore};
}

// A bot has no XUID, only FL_FAKECLIENT on its controller.
static bool ControllerIsBot(CCSPlayerController *controller)
{
	const int16_t offset = schema::GetOffset("CBaseEntity", "m_fFlags");
	return controller && offset > 0 && (*reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(controller) + offset) & (1u << 8));
}

void CS2APlugin::OnLateLoad()
{
	MMU_LOG_INFO("Late load detected - processing existing players...\n");

	// Grab entity system pointer on late load (map already active)
	if (!g_pEntitySystem)
	{
		g_pEntitySystem = GameEntitySystem();
	}

	CGlobalVars *globals = mmu::GetGameGlobals();
	if (!globals)
	{
		return;
	}

	g_CS2APlayerManager.OnLateLoad();
	for (int i = 0; i < globals->maxClients; i++)
	{
		PlayerInfo *player = g_CS2APlayerManager.Get(i);
		if (!player)
		{
			CCSPlayerController *bot = CCSPlayerController::FromSlot(i);
			if (ControllerIsBot(bot))
			{
				g_CS2APlayerManager.OnClientConnected(i, bot->GetPlayerName(), 0, "", true);
			}
			continue;
		}

		// Hook_GameFrame authorizes the ones Steam has confirmed.
		// The ban and comm checks need the DB, so they run from CS2A_RecheckConnectedPlayers once it is ready.
		// Their language was never queried either.
		mmu::cvarquery::OnClientConnected(i, false);
		player->authid = SteamID64ToAuthId(player->steamid64);

		// Never handed to us, and an empty one breaks rendered chat, ban and comm log rows and sleuth.
		if (CCSPlayerController *controller = CCSPlayerController::FromSlot(i))
		{
			player->name = controller->GetPlayerName();
		}

		MMU_LOG_INFO("Late load: processing player (%s) in slot %d\n", player->authid.c_str(), i);
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

	// Reload config and admins on map change
	if (m_bSkipLevelInitReload)
	{
		m_bSkipLevelInitReload = false;
	}
	else
	{
		ReloadConfig();
		g_CS2AAdminManager.ReloadAdmins();
	}
}

void CS2APlugin::OnLevelShutdown()
{
	MMU_LOG_INFO("Map unloading.\n");
}
