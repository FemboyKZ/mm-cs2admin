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

public: // Hooks
	void Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick);
	bool Hook_ClientConnect(CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, bool unk1, CBufferString *pRejectReason);
	void Hook_OnClientConnected(CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, const char *pszAddress,
								bool bFakePlayer);
	void Hook_ClientActive(CPlayerSlot slot, bool bLoadGame, const char *pszName, uint64 xuid);
	void Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName, uint64 xuid, const char *pszNetworkID);
	void Hook_ClientPutInServer(CPlayerSlot slot, char const *pszName, int type, uint64 xuid);
	void Hook_ClientSettingsChanged(CPlayerSlot slot);
	void Hook_DispatchConCommand(ConCommandRef cmd, const CCommandContext &ctx, const CCommand &args);
	void Hook_GameServerSteamAPIActivated();

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
};

extern CS2APlugin g_CS2APlugin;

PLUGIN_GLOBALVARS();

#endif //_INCLUDE_ADMIN_PLUGIN_H_
