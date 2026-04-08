#ifndef _INCLUDE_ADMIN_BAN_MANAGER_H_
#define _INCLUDE_ADMIN_BAN_MANAGER_H_

#include "../common.h"
#include <string>
#include <functional>

class ISQLQuery;

class CS2ABanManager
{
public:
	// Check if a connecting player is banned. Called from OnClientConnected.
	// Callback receives (isBanned, banReason).
	void VerifyBan(int slot, uint64_t steamid64, const char *ip,
		std::function<void(bool banned, const std::string &reason)> callback);

	// Ban a player by SteamID.
	// time = ban duration in minutes (0 = permanent).
	// adminSlot = slot of admin issuing ban (-1 for console/rcon).
	void BanPlayer(int targetSlot, int time, const char *reason, int adminSlot);

	// Ban by SteamID string (for offline bans / addban).
	void AddBan(const char *authid, int time, const char *reason, int adminSlot);

	// Ban by IP address.
	void BanIP(const char *ip, int time, const char *reason, int adminSlot);

	// Unban by SteamID.
	void Unban(const char *authid, int adminSlot);

	// Unban by IP.
	void UnbanIP(const char *ip, int adminSlot);

	// Get the server ID for ban insertions.
	int GetServerID() const;

	// Check player's ban/comm history for connect-time admin notifications.
	void CheckHistory(int slot, uint64_t steamid64, const char *ip,
		std::function<void(int banCount, int commCount, int muteCount, int gagCount)> callback);

	// IP sleuth: check if connecting player's IP matches banned IPs.
	void CheckSleuth(int slot, uint64_t steamid64, const char *ip);

	// Display ban history for a player ingame (!listbans)
	void ListBans(int callerSlot, const char *authid);

	// Display comm history for a player ingame (!listcomms)
	void ListComms(int callerSlot, const char *authid);

private:
	// Insert a ban row into the database.
	void InsertBan(const char *ip, const char *authid, const char *name,
		int timeMinutes, const char *reason, int adminSlot);
};

extern CS2ABanManager g_CS2ABanManager;

#endif // _INCLUDE_ADMIN_BAN_MANAGER_H_
