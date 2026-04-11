#ifndef _INCLUDE_ADMIN_PLAYER_MANAGER_H_
#define _INCLUDE_ADMIN_PLAYER_MANAGER_H_

#include "../common.h"
#include <string>
#include <vector>
#include <tuple>

struct PlayerInfo
{
	bool connected = false;
	uint64_t steamid64 = 0;
	std::string authid;      // "STEAM_0:X:Y"
	std::string name;
	std::string ip;
	bool fakePlayer = false;

	// Auth state
	bool authenticated = false;

	// Ban state
	bool banChecked = false;

	// Comm state
	bool isMuted = false;
	bool isGagged = false;
	bool isSessionMuted = false;  // Session-only (no DB record)
	bool isSessionGagged = false;
	int muteRemaining = 0;   // Seconds remaining, 0 = permanent
	int gagRemaining = 0;
	double muteExpireTime = 0.0;  // Game time when mute expires (0 = permanent/session)
	double gagExpireTime = 0.0;
	std::string muteReason;
	std::string gagReason;

	// Report system
	double lastReportTime = 0.0;
	int reportTargetSlot = -1;
	bool pendingReport = false;

	// Chat flood tracking
	double lastChatTime = 0.0;
	int chatMessageCount = 0;

	void Reset()
	{
		connected = false;
		steamid64 = 0;
		authid.clear();
		name.clear();
		ip.clear();
		fakePlayer = false;
		authenticated = false;
		banChecked = false;
		isMuted = false;
		isGagged = false;
		isSessionMuted = false;
		isSessionGagged = false;
		muteRemaining = 0;
		gagRemaining = 0;
		muteExpireTime = 0.0;
		gagExpireTime = 0.0;
		muteReason.clear();
		gagReason.clear();
		lastReportTime = 0.0;
		reportTargetSlot = -1;
		pendingReport = false;
		lastChatTime = 0.0;
		chatMessageCount = 0;
	}
};

// Disconnected player record for !listdc
struct DisconnectedPlayer
{
	std::string name;
	uint64_t steamid64 = 0;
	std::string ip;
	double disconnectTime = 0.0;
};

class CS2APlayerManager
{
public:
	void OnClientConnected(int slot, const char *name, uint64_t xuid,
		const char *networkID, const char *address, bool fakePlayer);
	void OnClientDisconnect(int slot);

	PlayerInfo *GetPlayer(int slot);
	PlayerInfo *FindPlayerBySteamID64(uint64_t steamid64);
	int FindSlotBySteamID64(uint64_t steamid64);

	// Disconnected player tracking
	void AddDisconnectedPlayer(const PlayerInfo &player);
	const std::vector<DisconnectedPlayer> &GetDisconnectedPlayers() const { return m_disconnected; }

private:
	PlayerInfo m_players[MAXPLAYERS + 1];

	// Circular buffer of last 20 disconnected players
	std::vector<DisconnectedPlayer> m_disconnected;
	static const int MAX_DISCONNECTED = 20;
};

extern CS2APlayerManager g_CS2APlayerManager;

// Shared utility: get admin's authid for DB logging (returns "STEAM_ID_SERVER" for console)
std::string GetAdminAuthId(int adminSlot);

// Shared utility: get admin's IP for DB logging (returns "" for console)
std::string GetAdminIP(int adminSlot);

// Targeting System

// Target result: a list of matched player slots.
struct TargetResult
{
	std::vector<int> slots;
	std::string error;       // Nonempty if targeting failed
	bool isMultiTarget = false;  // True if @all, @t, @ct etc.
};

// Find player(s) by pattern. Supports:
//   @me, @all, @t, @ct, @spec, @dead, @alive, @random, @bot, @human
//   $<steamid64>   - target by SteamID64
//   &<exact name>  - target by exact name (case insensitive)
//   #<userid/slot> - target by slot number
//   <partial name> - partial name match (single target only)
TargetResult ADMIN_FindTargets(int callerSlot, const char *pattern);

// Single target function. Returns slot or -1.
int ADMIN_FindTarget(int callerSlot, const char *pattern);

// Duration Parsing

// Parse a duration string with optional suffix: "30" (minutes), "2h", "1d", "1w", "1m" (months).
// Returns duration in minutes. Returns -1 on invalid input. 0 = permanent.
int ADMIN_ParseDuration(const char *input);

// Format a duration in minutes to a human-readable string.
// e.g., 90 -> "1 hour", 1500 -> "1 day", 0 -> "permanent"
std::string ADMIN_FormatDuration(int minutes);

#endif // _INCLUDE_ADMIN_PLAYER_MANAGER_H_
