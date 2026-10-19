#ifndef _INCLUDE_CS2A_PLAYER_MANAGER_H_
#define _INCLUDE_CS2A_PLAYER_MANAGER_H_

#include "../common.h"
#include "game/players.h"
#include <string>
#include <vector>
#include <tuple>

struct PlayerInfo : mmu::Player
{
	std::string authid; // "STEAM_0:X:Y"

	// Comm state
	bool isMuted = false;
	bool isGagged = false;
	bool isSessionMuted = false; // Session-only (no DB record)
	bool isSessionGagged = false;
	double muteExpireTime = 0.0; // Plat_FloatTime when mute expires (0 = permanent/session)
	double gagExpireTime = 0.0;
	std::string muteReason;
	std::string gagReason;
	// Who placed each block, for the unblock immunity check. A steamid64 of 0 is the console.
	int muteIssuerImmunity = 0;
	int gagIssuerImmunity = 0;
	uint64_t muteIssuerSteamid64 = 0;
	uint64_t gagIssuerSteamid64 = 0;

	// Report system
	double lastReportTime = 0.0;

	// Chat flood tracking
	double lastChatTime = 0.0;
	int chatMessageCount = 0;
};

// Disconnected player record for !listdc
struct DisconnectedPlayer
{
	std::string name;
	uint64_t steamid64 = 0;
	std::string ip;
	double disconnectTime = 0.0;
};

class CS2APlayerManager : public mmu::Players<PlayerInfo>
{
public:
	void OnClientConnected(int slot, const char *name, uint64_t xuid, const char *address, bool fakePlayer);
	void OnClientDisconnect(int slot);

	PlayerInfo *FindPlayerBySteamID64(uint64_t steamid64);
	int FindSlotBySteamID64(uint64_t steamid64);

	// Get admin's display name for announcements (returns "Console" for console)
	std::string GetAdminName(int adminSlot);

	// Disconnected player tracking
	void AddDisconnectedPlayer(const PlayerInfo &player);

	const std::vector<DisconnectedPlayer> &GetDisconnectedPlayers() const
	{
		return m_disconnected;
	}

private:
	// Circular buffer of last 20 disconnected players
	std::vector<DisconnectedPlayer> m_disconnected;
	static const int MAX_DISCONNECTED = 20;
};

extern CS2APlayerManager g_CS2APlayerManager;

// Shared utility: get admin's authid for DB logging (returns "STEAM_ID_SERVER" for console)
std::string GetAdminAuthId(int adminSlot);

// Shared utility: get admin's IP for DB logging (returns "" for console)
std::string GetAdminIP(int adminSlot);

// Target result: a list of matched player slots.
struct TargetResult
{
	std::vector<int> slots;
	std::string error;          // Nonempty if targeting failed
	bool isMultiTarget = false; // True if @all, @t, @ct etc.
};

// Player(s) by pattern, see mmu::FindTargets.
TargetResult CS2A_FindTargets(int callerSlot, const char *pattern);

// Single target function. Returns slot or -1.
int CS2A_FindTarget(int callerSlot, const char *pattern);

// Duration Parsing

// Parse a duration string with optional suffix: "30" (minutes), "2h", "1d", "1w", "1m" (months).
// Returns duration in minutes. Returns -1 on invalid input. 0 = permanent.
int CS2A_ParseDuration(const char *input);

// Format a duration in minutes to a human-readable string.
// e.g., 90 -> "1 hour", 1500 -> "1 day", 0 -> "permanent"
std::string CS2A_FormatDuration(int slot, int minutes);

#endif // _INCLUDE_CS2A_PLAYER_MANAGER_H_
