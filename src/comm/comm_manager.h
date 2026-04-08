#ifndef _INCLUDE_ADMIN_COMM_MANAGER_H_
#define _INCLUDE_ADMIN_COMM_MANAGER_H_

#include "../common.h"
#include "../player/player_manager.h"

#include <string>
#include <functional>

// Comm punishment types for mute/gag DB entries
enum CommPunishType
{
	COMM_NONE = 0,
	COMM_MUTE = 1,   // Voice mute
	COMM_GAG = 2,    // Chat gag
};

class ISQLQuery;

class CS2ACommManager
{
public:
	// Check if a connecting player has active mute/gag.
	// Called after player is connected and DB is available.
	void VerifyComms(int slot, uint64_t steamid64);

	// Mute a player (voice).
	void MutePlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot);

	// Gag a player (chat).
	void GagPlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot);

	// Silence a player (both mute + gag).
	void SilencePlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot);

	// Remove mute.
	void UnmutePlayer(int targetSlot, int adminSlot);

	// Remove gag.
	void UngagPlayer(int targetSlot, int adminSlot);

	// Remove silence (both).
	void UnsilencePlayer(int targetSlot, int adminSlot);

	// Session-only mute (no DB record, clears on disconnect)
	void SessionMutePlayer(int targetSlot, int adminSlot);

	// Session-only gag (no DB record, clears on disconnect)
	void SessionGagPlayer(int targetSlot, int adminSlot);

	// Check and auto-expire timed comm blocks. Called periodically from GameFrame.
	void CheckExpiredComms();

	// Clear session blocks on disconnect
	void OnClientDisconnect(int slot);

	// Print comm status for a player (used by !comms command)
	void PrintCommsStatus(int targetSlot, int callerSlot);

	// Check if a player is currently gagged (used to block chat messages).
	bool IsGagged(int slot);

	// Check if a player is currently muted.
	bool IsMuted(int slot);

private:
	void InsertComm(const char *authid, const char *name, int timeMinutes,
		const char *reason, int adminSlot, int type);
	void RemoveComm(const char *authid, int adminSlot, int type);
};

extern CS2ACommManager g_CS2ACommManager;

#endif // _INCLUDE_ADMIN_COMM_MANAGER_H_
