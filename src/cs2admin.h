#ifndef _INCLUDE_ADMIN_PLUGIN_H_
#define _INCLUDE_ADMIN_PLUGIN_H_

#include "common.h"
#include "version_gen.h"
#include "public/ics2admin.h"

class CS2APlugin : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late);
	bool Unload(char *error, size_t maxlen);
	void AllPluginsLoaded();

public: // IMetamodListener
	void OnLevelInit(char const *pMapName, char const *pMapEntities, char const *pOldLevel, char const *pLandmarkName, bool loadGame,
					 bool background);
	void OnLevelShutdown();
	void *OnMetamodQuery(const char *iface, int *ret);
	// Re-resolve optional sibling-plugin interfaces (mm-cs2menus) when plugins load/unload at runtime.
	void OnPluginLoad(PluginId id);
	void OnPluginUnload(PluginId id);

public:
	CS2APlugin();

public: // Hooks
	KHook::Return<void> Hook_GameFrame(IServerGameDLL *, bool simulating, bool bFirstTick, bool bLastTick);
	KHook::Return<bool> Hook_ClientConnect(IServerGameClients *, CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID,
										   bool unk1, CBufferString *pRejectReason);
	KHook::Return<void> Hook_OnClientConnected(IServerGameClients *, CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID,
											   const char *pszAddress, bool bFakePlayer);
	KHook::Return<void> Hook_ClientActive(IServerGameClients *, CPlayerSlot slot, bool bLoadGame, const char *pszName, uint64 xuid);
	KHook::Return<void> Hook_ClientDisconnect(IServerGameClients *, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName,
											  uint64 xuid, const char *pszNetworkID);
	KHook::Return<void> Hook_ClientPutInServer(IServerGameClients *, CPlayerSlot slot, char const *pszName, int type, uint64 xuid);
	KHook::Return<void> Hook_ClientSettingsChanged(IServerGameClients *, CPlayerSlot slot);
	KHook::Return<void> Hook_DispatchConCommand(ICvar *, ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args);
	KHook::Return<void> Hook_GameServerSteamAPIActivated(IServerGameDLL *);

public:
	const char *GetAuthor()
	{
		return PLUGIN_AUTHOR;
	}

	const char *GetName()
	{
		return PLUGIN_DISPLAY_NAME;
	}

	const char *GetDescription()
	{
		return PLUGIN_DESCRIPTION;
	}

	const char *GetURL()
	{
		return PLUGIN_URL;
	}

	const char *GetLicense()
	{
		return PLUGIN_LICENSE;
	}

	const char *GetVersion()
	{
		return PLUGIN_FULL_VERSION;
	}

	const char *GetDate()
	{
		return __DATE__;
	}

	const char *GetLogTag()
	{
		return PLUGIN_LOGTAG;
	}

private:
	void LookupServerID();
	void OnLateLoad();

	bool m_bLateLoaded = false;
	bool m_bConfigLoaded = false;

	// On a normal load the startup path already reloads admins, and the first level init lands right on top of it.
	// Only later level inits are real map changes.
	bool m_bSkipLevelInitReload = false;
	float m_flNextExpiryCheck = 0.0f;
	float m_flNextQueueProcess = 0.0f;
	float m_flNextReconnect = 0.0f;
	int m_iReconnectAttempts = 0;
	bool m_bReconnectGaveUp = false;

	KHook::Virtual<IServerGameDLL, void, bool, bool, bool> m_GameFrame;
	KHook::Virtual<IServerGameClients, void, CPlayerSlot, bool, const char *, uint64> m_ClientActive;
	KHook::Virtual<IServerGameClients, void, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64, const char *> m_ClientDisconnect;
	KHook::Virtual<IServerGameClients, void, CPlayerSlot, char const *, int, uint64> m_ClientPutInServer;
	KHook::Virtual<IServerGameClients, void, CPlayerSlot> m_ClientSettingsChanged;
	KHook::Virtual<IServerGameClients, void, CPlayerSlot, const char *, uint64, const char *, const char *, bool> m_OnClientConnected;
	KHook::Virtual<IServerGameClients, bool, CPlayerSlot, const char *, uint64, const char *, bool, CBufferString *> m_ClientConnect;
	KHook::Virtual<ICvar, void, ConCommandRef, const CCommandContext &, const CCommand &> m_DispatchConCommand;
	KHook::Virtual<IServerGameDLL, void> m_GameServerSteamAPIActivated;
};

extern CS2APlugin g_CS2APlugin;

PLUGIN_GLOBALVARS();

#endif //_INCLUDE_ADMIN_PLUGIN_H_
