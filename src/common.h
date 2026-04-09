#ifndef _INCLUDE_ADMIN_COMMON_H_
#define _INCLUDE_ADMIN_COMMON_H_

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iserver.h>
#include <sh_vector.h>
#include <iplayerinfo.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>

#define MAXPLAYERS 64

#define CHAT_COLOR_DEFAULT  "\x01"
#define CHAT_COLOR_RED      "\x02"
#define CHAT_COLOR_TEAM     "\x03"
#define CHAT_COLOR_GREEN    "\x04"
#define CHAT_COLOR_OLIVE    "\x05"
#define CHAT_COLOR_LIME     "\x06"
#define CHAT_COLOR_GOLD     "\x09"
#define CHAT_COLOR_GREY     "\x0A"
#define CHAT_COLOR_BLUE     "\x0C"
#define CHAT_COLOR_PURPLE   "\x10"

// Engine interfaces
extern IServerGameDLL *g_pServerGameDLL;
extern IServerGameClients *g_pGameClients;
extern IVEngineServer *g_pEngine;
extern IGameEventManager2 *g_pGameEvents;
extern ICvar *g_pICvar;

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
