#ifndef _INCLUDE_ADMIN_COMM_MANAGER_H_
#define _INCLUDE_ADMIN_COMM_MANAGER_H_

#include "src/common.h"
#include "src/player/player_manager.h"

#include <string>
#include <functional>

// Comm punishment types for mute/gag DB entries
enum CommPunishType
{
	COMM_NONE = 0,
	COMM_MUTE = 1, // Voice mute
	COMM_GAG = 2,  // Chat gag
};

class ISQLQuery;

class CS2ACommManager
{
public:
	// Check if a connecting player has active mute/gag.
	// Called after player is connected and DB is available.
	void VerifyComms(int slot, uint64_t steamid64);

	// persist false skips the DB write, for web panel commands whose row already exists.

	// Mute a player (voice). False when the target is gone or a forward blocked it.
	bool MutePlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot, bool persist = true);

	// Gag a player (chat). False when the target is gone or a forward blocked it.
	bool GagPlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot, bool persist = true);

	// Silence a player (both mute + gag). Returns the COMM_MUTE/COMM_GAG bits that actually applied, since a forward can block either half.
	int SilencePlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot, bool persist = true);

	// Remove mute. False when the player wasn't muted, in which case nothing is announced.
	bool UnmutePlayer(int targetSlot, int adminSlot, bool persist = true);

	// Remove gag. False when the player wasn't gagged, in which case nothing is announced.
	bool UngagPlayer(int targetSlot, int adminSlot, bool persist = true);

	// Remove silence (both). Returns the COMM_MUTE/COMM_GAG bits that were actually lifted.
	int UnsilencePlayer(int targetSlot, int adminSlot);

	// Session-only mute (no DB record, clears on disconnect)
	void SessionMutePlayer(int targetSlot, int adminSlot);

	// Session-only gag (no DB record, clears on disconnect)
	void SessionGagPlayer(int targetSlot, int adminSlot);

	// SourceComms' unblock rule. The issuer, the console, the cheats flag,
	// or strictly higher immunity than the issuer unless DisableUnblockImmunityCheck is set.
	bool CanLiftBlock(int callerSlot, int targetSlot, int type) const;

	// MaxLength check, 0 = permanent. The console and the cheats flag are exempt.
	bool IsAllowedLength(int callerSlot, int minutes) const;

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

	// Bumped whenever an admin applies or lifts a block, so an older VerifyComms answer can be dropped.
	uint32_t Generation(int slot) const
	{
		return (slot >= 0 && slot <= MAXPLAYERS) ? m_commGeneration[slot] : 0;
	}

private:
	void BumpGeneration(int slot)
	{
		if (slot >= 0 && slot <= MAXPLAYERS)
		{
			m_commGeneration[slot]++;
		}
	}

	// Apply or lift without announcing, so silence can announce once for both halves. Apply returns false when a forward blocked it.
	bool ApplyMute(int targetSlot, int timeMinutes, const char *reason, int adminSlot, bool persist);
	bool ApplyGag(int targetSlot, int timeMinutes, const char *reason, int adminSlot, bool persist);
	bool LiftMute(int targetSlot, int adminSlot, bool persist);
	bool LiftGag(int targetSlot, int adminSlot, bool persist);
	void AnnounceBlock(int targetSlot, int adminSlot, int timeMinutes, const char *reason, const char *permanentPhrase, const char *timedPhrase,
					   const char *allPhrase);
	void AnnounceLift(int targetSlot, int adminSlot, const char *selfPhrase, const char *allPhrase);

	void InsertComm(const char *authid, const char *name, int timeMinutes, const char *reason, int adminSlot, int type);
	void RemoveComm(const char *authid, int adminSlot, int type);

	uint32_t m_commGeneration[MAXPLAYERS + 1] = {};
};

extern CS2ACommManager g_CS2ACommManager;

#endif // _INCLUDE_ADMIN_COMM_MANAGER_H_
