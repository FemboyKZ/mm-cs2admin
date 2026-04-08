#ifndef _INCLUDE_ADMIN_PLAYER_MANAGER_H_
#define _INCLUDE_ADMIN_PLAYER_MANAGER_H_

#include "../common.h"
#include <string>

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
	}
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

private:
	PlayerInfo m_players[MAXPLAYERS + 1];
};

extern CS2APlayerManager g_CS2APlayerManager;

// Shared utility: get admin's authid for DB logging (returns "STEAM_ID_SERVER" for console)
std::string GetAdminAuthId(int adminSlot);

// Shared utility: get admin's IP for DB logging (returns "" for console)
std::string GetAdminIP(int adminSlot);

// Find a player slot by partial name match, #userid, or slot number.
// Returns the slot index, or -1 if no match or ambiguous.
int ADMIN_FindTarget(int callerSlot, const char *pattern);

#endif // _INCLUDE_ADMIN_PLAYER_MANAGER_H_
