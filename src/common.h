#ifndef _INCLUDE_ADMIN_COMMON_H_
#define _INCLUDE_ADMIN_COMMON_H_

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iserver.h>
#include <sh_vector.h>
#include <iplayerinfo.h>

#include "mmu/chat_colors.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>

#define MAXPLAYERS 64

// Engine interfaces
extern IServerGameDLL *g_pServerGameDLL;
extern IServerGameClients *g_pGameClients;
extern IVEngineServer *g_pEngine;
extern IGameEventManager2 *g_pGameEvents;
extern ICvar *g_pICvar;

class INetworkMessages;

class IGameEventSystem;
extern IGameEventSystem *g_pGameEventSystem;

class IFileSystem;
extern IFileSystem *g_pFullFileSystem;

// Schema & entity system
class CGameEntitySystem;
extern CGameEntitySystem *g_pEntitySystem;

// Metamod globals
extern ISmmAPI *g_SMAPI;
extern ISmmPlugin *g_PLAPI;
extern PluginId g_PLID;
extern SourceHook::ISourceHook *g_SHPtr;

// SteamID conversion utilities
#include "utils/steam_utils.h"

// CGlobalVars accessor, only valid during active game
CGlobalVars *GetGameGlobals();

#endif // _INCLUDE_ADMIN_COMMON_H_
