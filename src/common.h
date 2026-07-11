#ifndef _INCLUDE_ADMIN_COMMON_H_
#define _INCLUDE_ADMIN_COMMON_H_

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iserver.h>
#include <sh_vector.h>
#include <iplayerinfo.h>

#include "mmu/chat_colors.h"
#include "mmu/plugin_globals.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>

// Plugin-specific engine interfaces. Shared ones live in mmu/plugin_globals.h.
extern IGameEventManager2 *g_pGameEvents;

class INetworkMessages;

class IGameEventSystem;
extern IGameEventSystem *g_pGameEventSystem;

class IFileSystem;
extern IFileSystem *g_pFullFileSystem;

// Schema & entity system
class CGameEntitySystem;
extern CGameEntitySystem *g_pEntitySystem;

// SteamID conversion utilities
#include "mmu/steam_utils.h"

// CGlobalVars accessor, only valid during active game
#include "mmu/print.h"

inline CGlobalVars *GetGameGlobals()
{
	return mmu::GetGameGlobals();
}

#endif // _INCLUDE_ADMIN_COMMON_H_
