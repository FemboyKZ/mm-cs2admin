#ifndef _INCLUDE_ADMIN_COMMON_H_
#define _INCLUDE_ADMIN_COMMON_H_

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <iserver.h>

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

// Shorten to at most maxBytes without cutting a UTF-8 sequence in half,
// which would leave invalid text in a JSON body or a strict-mode SQL column.
inline void ADMIN_TruncateUtf8(std::string &s, size_t maxBytes)
{
	if (s.size() <= maxBytes)
	{
		return;
	}
	size_t cut = maxBytes;
	// Continuation bytes are 10xxxxxx, so step back to the lead byte that starts the sequence being cut.
	while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
	{
		cut--;
	}
	s.resize(cut);
}

#endif // _INCLUDE_ADMIN_COMMON_H_
