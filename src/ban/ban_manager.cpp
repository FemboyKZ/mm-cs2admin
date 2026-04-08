#include "ban_manager.h"
#include "../common.h"
#include "../config/config.h"
#include "../db/database.h"
#include "../player/player_manager.h"
#include "../public/forwards.h"
#include "../queue/offline_queue.h"
#include "../admin/admin_manager.h"
#include "../utils/print_utils.h"

#include <sql_mm.h>
#include <algorithm>
#include <climits>
#include <ctime>

CS2ABanManager g_CS2ABanManager;

int CS2ABanManager::GetServerID() const
{
	return g_CS2AConfig.serverID;
}

void CS2ABanManager::VerifyBan(int slot, uint64_t steamid64, const char *ip,
	std::function<void(bool banned, const std::string &reason)> callback)
{
	if (!g_CS2ADatabase.IsConnected())
	{
		if (callback)
			callback(false, "");
		return;
	}

	std::string suffix = g_CS2ADatabase.Escape(SteamID64ToSuffix(steamid64).c_str());
	std::string escapedIP = ip ? g_CS2ADatabase.Escape(ip) : "";
	std::string prefix = g_CS2AConfig.databasePrefix;

	long long now = (long long)std::time(nullptr);
	std::string authCond = CS2ADatabase::AuthMatch("authid", suffix);

	char query[1024];
	snprintf(query, sizeof(query),
		"SELECT bid, ip, name, reason, length "
		"FROM %s_bans "
		"WHERE ((type = 0 AND %s) "
		"OR (type = 1 AND ip = '%s')) "
		"AND (length = '0' OR ends > %lld) "
		"AND RemoveType IS NULL",
		prefix.c_str(), authCond.c_str(), escapedIP.c_str(), now);

	g_CS2ADatabase.Query(query, [callback](ISQLQuery *result) {
		if (!result)
		{
			if (callback)
				callback(false, "");
			return;
		}

		ISQLResult *rs = result->GetResultSet();
		if (!rs || rs->GetRowCount() == 0)
		{
			if (callback)
				callback(false, "");
			return;
		}

		ISQLRow *row = rs->FetchRow();
		if (!row)
		{
			if (callback)
				callback(false, "");
			return;
		}

		const char *reason = rs->GetString(3);
		if (callback)
			callback(true, reason ? reason : "Banned");
	});
}

void CS2ABanManager::BanPlayer(int targetSlot, int time, const char *reason, int adminSlot)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		META_CONPRINTF("[ADMIN] BanPlayer: invalid target slot %d\n", targetSlot);
		return;
	}

	if (g_CS2AForwards.FireOnBanPlayer(targetSlot, adminSlot, time, reason))
		return;

	// Capture player info before the kick, DisconnectClient triggers
	// Hook_ClientDisconnect synchronously which resets the PlayerInfo.
	std::string targetName = target->name;
	std::string targetAuth = target->authid;
	std::string targetIP   = target->ip;

	InsertBan(targetIP.c_str(), targetAuth.c_str(), targetName.c_str(),
		time, reason, adminSlot);

	// Notify target player before kick
	if (time == 0)
		ADMIN_PrintToClient(targetSlot, "[ADMIN] You have been permanently banned. Reason: %s\n", reason ? reason : "No reason");
	else
		ADMIN_PrintToClient(targetSlot, "[ADMIN] You have been banned for %d minutes. Reason: %s\n", time, reason ? reason : "No reason");

	// Announce to all players
	std::string adminName = "Console";
	if (adminSlot >= 0)
	{
		PlayerInfo *admin = g_CS2APlayerManager.GetPlayer(adminSlot);
		if (admin) adminName = admin->name;
	}
	ADMIN_PrintToAll("[ADMIN] %s banned %s (%d min). Reason: %s\n",
		adminName.c_str(), targetName.c_str(), time, reason ? reason : "No reason");

	// Log admin action
	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Banned \"%s\" (%s) for %d min. Reason: %s",
		targetName.c_str(), targetAuth.c_str(), time, reason ? reason : "No reason");
	ADMIN_LogAction(adminSlot, logMsg);

	// Kick the player
	g_pEngine->DisconnectClient(CPlayerSlot(targetSlot), NETWORK_DISCONNECT_KICKBANADDED);
	META_CONPRINTF("[ADMIN] Banned player \"%s\" (%s) for %d min. Reason: %s\n",
		targetName.c_str(), targetAuth.c_str(), time, reason ? reason : "No reason");
}

void CS2ABanManager::AddBan(const char *authid, int time, const char *reason, int adminSlot)
{
	InsertBan("", authid, "", time, reason, adminSlot);

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Added offline ban for %s (%d min). Reason: %s",
		authid, time, reason ? reason : "No reason");
	ADMIN_LogAction(adminSlot, logMsg);

	META_CONPRINTF("[ADMIN] Added ban for %s, %d min. Reason: %s\n",
		authid, time, reason ? reason : "No reason");
}

void CS2ABanManager::BanIP(const char *ip, int time, const char *reason, int adminSlot)
{
	if (!g_CS2ADatabase.IsConnected())
		return;

	std::string escapedIP = g_CS2ADatabase.Escape(ip);
	std::string escapedReason = g_CS2ADatabase.Escape(reason ? reason : "");
	std::string adminAuth = g_CS2ADatabase.Escape(GetAdminAuthId(adminSlot).c_str());
	std::string adminIP = g_CS2ADatabase.Escape(GetAdminIP(adminSlot).c_str());
	std::string adminSuffix = ExtractAuthSuffix(adminAuth);
	std::string prefix = g_CS2AConfig.databasePrefix;
	int lengthSec = (time > 0 && time <= INT_MAX / 60) ? time * 60 : 0;

	long long now = (long long)std::time(nullptr);
	std::string adminMatch = CS2ADatabase::AuthMatch("authid", adminSuffix);

	char query[2048];
	int sid = g_CS2AConfig.serverID != -1 ? g_CS2AConfig.serverID : 0;
	snprintf(query, sizeof(query),
		"INSERT INTO %s_bans (ip, authid, name, created, ends, length, reason, aid, adminIp, sid, country, type) "
		"VALUES ('%s', '', '', %lld, %lld, %d, '%s', "
		"IFNULL((SELECT aid FROM %s_admins WHERE authid = '%s' OR %s), 0), "
		"'%s', %d, ' ', 1)",
		prefix.c_str(), escapedIP.c_str(), now, now + lengthSec, lengthSec,
		escapedReason.c_str(), prefix.c_str(),
		adminAuth.c_str(), adminMatch.c_str(),
		adminIP.c_str(), sid);

	g_CS2ADatabase.Query(query, [](ISQLQuery *result) {
		if (result && result->GetAffectedRows() > 0)
			META_CONPRINTF("[ADMIN] IP ban inserted successfully.\n");
		else
			META_CONPRINTF("[ADMIN] Failed to insert IP ban.\n");
	});
}

void CS2ABanManager::InsertBan(const char *ip, const char *authid, const char *name,
	int timeMinutes, const char *reason, int adminSlot)
{
	if (!g_CS2ADatabase.IsConnected())
	{
		META_CONPRINTF("[ADMIN] Cannot insert ban: database not connected.\n");
		return;
	}

	std::string prefix = g_CS2AConfig.databasePrefix;
	std::string escapedIP = g_CS2ADatabase.Escape(ip ? ip : "");
	std::string escapedAuthId = g_CS2ADatabase.Escape(authid ? authid : "");
	std::string escapedName = g_CS2ADatabase.Escape(name ? name : "");
	std::string escapedReason = g_CS2ADatabase.Escape(reason ? reason : "");
	std::string adminAuth = g_CS2ADatabase.Escape(GetAdminAuthId(adminSlot).c_str());
	std::string adminIP = g_CS2ADatabase.Escape(GetAdminIP(adminSlot).c_str());
	std::string adminAuthSuffix = ExtractAuthSuffix(adminAuth);

	int lengthSec = (timeMinutes > 0 && timeMinutes <= INT_MAX / 60) ? timeMinutes * 60 : 0;
	long long now = (long long)std::time(nullptr);
	std::string adminMatch = CS2ADatabase::AuthMatch("authid", adminAuthSuffix);

	char query[2048];
	if (g_CS2AConfig.serverID != -1)
	{
		snprintf(query, sizeof(query),
			"INSERT INTO %s_bans (ip, authid, name, created, ends, length, reason, aid, adminIp, sid, country) "
			"VALUES ('%s', '%s', '%s', %lld, %lld, %d, '%s', "
			"IFNULL((SELECT aid FROM %s_admins WHERE authid = '%s' OR %s), 0), "
			"'%s', %d, ' ')",
			prefix.c_str(), escapedIP.c_str(), escapedAuthId.c_str(), escapedName.c_str(),
			now, now + lengthSec, lengthSec, escapedReason.c_str(),
			prefix.c_str(), adminAuth.c_str(), adminMatch.c_str(),
			adminIP.c_str(), g_CS2AConfig.serverID);
	}
	else
	{
		snprintf(query, sizeof(query),
			"INSERT INTO %s_bans (ip, authid, name, created, ends, length, reason, aid, adminIp, sid, country) "
			"VALUES ('%s', '%s', '%s', %lld, %lld, %d, '%s', "
			"IFNULL((SELECT aid FROM %s_admins WHERE authid = '%s' OR %s), 0), "
			"'%s', 0, ' ')",
			prefix.c_str(), escapedIP.c_str(), escapedAuthId.c_str(), escapedName.c_str(),
			now, now + lengthSec, lengthSec, escapedReason.c_str(),
			prefix.c_str(), adminAuth.c_str(), adminMatch.c_str(),
			adminIP.c_str());
	}

	g_CS2ADatabase.Query(query, [queryStr = std::string(query)](ISQLQuery *result) {
		if (!result)
		{
			// Query dispatch failed, queue the already escaped query for retry
			g_CS2AOfflineQueue.Enqueue(queryStr);
			return;
		}
		if (result->GetAffectedRows() > 0)
			META_CONPRINTF("[ADMIN] Ban inserted successfully.\n");
		else
			META_CONPRINTF("[ADMIN] Failed to insert ban.\n");
	});
}

void CS2ABanManager::Unban(const char *authid, int adminSlot)
{
	if (!g_CS2ADatabase.IsConnected() || !authid)
		return;

	g_CS2AForwards.FireOnUnbanPlayer(authid, adminSlot);

	std::string suffix = g_CS2ADatabase.Escape(ExtractAuthSuffix(std::string(authid)).c_str());
	std::string adminAuth = g_CS2ADatabase.Escape(GetAdminAuthId(adminSlot).c_str());
	std::string adminSuffix = g_CS2ADatabase.Escape(ExtractAuthSuffix(adminAuth).c_str());
	std::string prefix = g_CS2AConfig.databasePrefix;

	long long now = (long long)std::time(nullptr);
	std::string adminMatch = CS2ADatabase::AuthMatch("authid", adminSuffix);
	std::string targetMatch = CS2ADatabase::AuthMatch("authid", suffix);

	char query[1024];
	snprintf(query, sizeof(query),
		"UPDATE %s_bans SET RemovedBy = "
		"IFNULL((SELECT aid FROM %s_admins WHERE authid = '%s' OR %s), 0), "
		"RemoveType = 'U', RemovedOn = %lld "
		"WHERE %s "
		"AND (length = '0' OR ends > %lld) "
		"AND RemoveType IS NULL",
		prefix.c_str(), prefix.c_str(),
		adminAuth.c_str(), adminMatch.c_str(),
		now,
		targetMatch.c_str(),
		now);

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Unbanned %s", authid);
	ADMIN_LogAction(adminSlot, logMsg);

	g_CS2ADatabase.Query(query, [authid = std::string(authid)](ISQLQuery *result) {
		if (result && result->GetAffectedRows() > 0)
			META_CONPRINTF("[ADMIN] Unbanned %s successfully.\n", authid.c_str());
		else
			META_CONPRINTF("[ADMIN] No active ban found for %s.\n", authid.c_str());
	});
}

void CS2ABanManager::UnbanIP(const char *ip, int adminSlot)
{
	if (!g_CS2ADatabase.IsConnected() || !ip)
		return;

	std::string escapedIP = g_CS2ADatabase.Escape(ip);
	std::string adminAuth = g_CS2ADatabase.Escape(GetAdminAuthId(adminSlot).c_str());
	std::string adminSuffix = g_CS2ADatabase.Escape(ExtractAuthSuffix(adminAuth).c_str());
	std::string prefix = g_CS2AConfig.databasePrefix;

	long long now = (long long)std::time(nullptr);
	std::string adminMatch = CS2ADatabase::AuthMatch("authid", adminSuffix);

	char query[1024];
	snprintf(query, sizeof(query),
		"UPDATE %s_bans SET RemovedBy = "
		"IFNULL((SELECT aid FROM %s_admins WHERE authid = '%s' OR %s), 0), "
		"RemoveType = 'U', RemovedOn = %lld "
		"WHERE type = 1 AND ip = '%s' "
		"AND (length = '0' OR ends > %lld) "
		"AND RemoveType IS NULL",
		prefix.c_str(), prefix.c_str(),
		adminAuth.c_str(), adminMatch.c_str(),
		now,
		escapedIP.c_str(),
		now);

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Unbanned IP %s", ip);
	ADMIN_LogAction(adminSlot, logMsg);

	g_CS2ADatabase.Query(query, [ip = std::string(ip)](ISQLQuery *result) {
		if (result && result->GetAffectedRows() > 0)
			META_CONPRINTF("[ADMIN] Unbanned IP %s successfully.\n", ip.c_str());
		else
			META_CONPRINTF("[ADMIN] No active IP ban found for %s.\n", ip.c_str());
	});
}

void CS2ABanManager::CheckHistory(int slot, uint64_t steamid64, const char *ip,
	std::function<void(int banCount, int commCount, int muteCount, int gagCount)> callback)
{
	if (!g_CS2ADatabase.IsConnected())
	{
		if (callback) callback(0, 0, 0, 0);
		return;
	}

	std::string suffix = g_CS2ADatabase.Escape(SteamID64ToSuffix(steamid64).c_str());
	std::string escapedIP = ip ? g_CS2ADatabase.Escape(ip) : "";
	std::string prefix = g_CS2AConfig.databasePrefix;

	std::string authCond = CS2ADatabase::AuthMatch("authid", suffix);

	char query[2048];
	snprintf(query, sizeof(query),
		"SELECT "
		"(SELECT COUNT(*) FROM %s_bans WHERE (type=0 AND %s) OR (type=1 AND ip='%s')) AS ban_count, "
		"(SELECT COUNT(*) FROM %s_comms WHERE %s AND type=1 AND RemoveType IS NULL) AS mute_count, "
		"(SELECT COUNT(*) FROM %s_comms WHERE %s AND type=2 AND RemoveType IS NULL) AS gag_count",
		prefix.c_str(), authCond.c_str(), escapedIP.c_str(),
		prefix.c_str(), authCond.c_str(),
		prefix.c_str(), authCond.c_str());

	g_CS2ADatabase.Query(query, [callback](ISQLQuery *result) {
		if (!result)
		{
			if (callback) callback(0, 0, 0, 0);
			return;
		}

		ISQLResult *rs = result->GetResultSet();
		if (!rs || rs->GetRowCount() == 0)
		{
			if (callback) callback(0, 0, 0, 0);
			return;
		}

		ISQLRow *row = rs->FetchRow();
		if (!row)
		{
			if (callback) callback(0, 0, 0, 0);
			return;
		}

		int banCount = rs->GetInt(0);
		int muteCount = rs->GetInt(1);
		int gagCount = rs->GetInt(2);
		if (callback) callback(banCount, muteCount + gagCount, muteCount, gagCount);
	});
}

void CS2ABanManager::CheckSleuth(int slot, uint64_t steamid64, const char *ip)
{
	if (!g_CS2ADatabase.IsConnected() || g_CS2AConfig.sleuthActions <= 0 || !ip || !*ip)
		return;

	// Check if player is admin and bypass is enabled
	if (g_CS2AConfig.sleuthAdminBypass && g_CS2AAdminManager.PlayerHasFlag(slot, ADMFLAG_BAN))
		return;

	std::string escapedIP = g_CS2ADatabase.Escape(ip);
	std::string prefix = g_CS2AConfig.databasePrefix;

	long long now = (long long)std::time(nullptr);

	// Build time filter
	std::string timeFilter;
	if (g_CS2AConfig.sleuthExcludeOld)
	{
		char timeBuf[128];
		snprintf(timeBuf, sizeof(timeBuf),
			" AND created > %lld", now - (long long)g_CS2AConfig.sleuthExcludeTime);
		timeFilter = timeBuf;
	}

	// Ban type filter
	std::string typeFilter;
	if (g_CS2AConfig.sleuthBanType == 1)
		typeFilter = " AND length = 0";

	char query[1024];
	snprintf(query, sizeof(query),
		"SELECT COUNT(*), length FROM %s_bans WHERE ip = '%s' AND RemoveType IS NULL "
		"AND (length = '0' OR ends > %lld)%s%s",
		prefix.c_str(), escapedIP.c_str(), now, typeFilter.c_str(), timeFilter.c_str());

	g_CS2ADatabase.Query(query, [slot, steamid64](ISQLQuery *result) {
		if (!result) return;

		ISQLResult *rs = result->GetResultSet();
		if (!rs || rs->GetRowCount() == 0) return;

		ISQLRow *row = rs->FetchRow();
		if (!row) return;

		int count = rs->GetInt(0);
		int originalLength = rs->GetInt(1);

		if (count <= g_CS2AConfig.sleuthBansAllowed) return;

		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
		if (!player || !player->connected || player->steamid64 != steamid64) return;

		int action = g_CS2AConfig.sleuthActions;

		if (action == 4)
		{
			// Notify admins only
			ADMIN_ChatToAdmins("[CS2Admin] WARNING: Player \"%s\" (%s) has %d matching IP ban(s).\n",
				player->name.c_str(), player->authid.c_str(), count);
			return;
		}

		if (action == 5)
		{
			// Kick
			META_CONPRINTF("[ADMIN] Sleuth: kicking \"%s\" - %d IP bans found.\n",
				player->name.c_str(), count);
			g_pEngine->DisconnectClient(CPlayerSlot(slot), NETWORK_DISCONNECT_KICKBANADDED);
			return;
		}

		// Actions 1-3: ban
		int banTime = 0;
		if (action == 1) banTime = originalLength / 60; // original length
		else if (action == 2) banTime = g_CS2AConfig.sleuthDuration;
		else if (action == 3) banTime = (originalLength / 60) * 2; // double

		char reason[256];
		snprintf(reason, sizeof(reason), "Sleuth auto-ban: %d matching IP ban(s)", count);

		ADMIN_ChatToAdmins("[CS2Admin] Sleuth auto-banned \"%s\" (%s) - %d IP ban(s) found.\n",
			player->name.c_str(), player->authid.c_str(), count);

		g_CS2ABanManager.InsertBan(player->ip.c_str(), player->authid.c_str(),
			player->name.c_str(), banTime, reason, -1);
		g_pEngine->DisconnectClient(CPlayerSlot(slot), NETWORK_DISCONNECT_KICKBANADDED);
	});
}

void CS2ABanManager::ListBans(int callerSlot, const char *authid)
{
	if (!g_CS2ADatabase.IsConnected())
	{
		ADMIN_PrintToClient(callerSlot, "[ADMIN] Database not connected.\n");
		return;
	}

	std::string suffix = g_CS2ADatabase.Escape(ExtractAuthSuffix(std::string(authid)).c_str());
	std::string prefix = g_CS2AConfig.databasePrefix;

	std::string authCond = CS2ADatabase::AuthMatch("b.authid", suffix);

	char query[1024];
	snprintf(query, sizeof(query),
		"SELECT b.created, IFNULL(a.user, 'Console'), b.length, b.ends, "
		"b.RemoveType, b.reason "
		"FROM %s_bans b LEFT JOIN %s_admins a ON a.aid = b.aid "
		"WHERE %s ORDER BY b.created DESC LIMIT 10",
		prefix.c_str(), prefix.c_str(), authCond.c_str());

	g_CS2ADatabase.Query(query, [callerSlot, authid = std::string(authid)](ISQLQuery *result) {
		if (!result)
		{
			ADMIN_PrintToClient(callerSlot, "[ADMIN] Query failed.\n");
			return;
		}

		ISQLResult *rs = result->GetResultSet();
		if (!rs || rs->GetRowCount() == 0)
		{
			ADMIN_PrintToClient(callerSlot, "[ADMIN] No bans found for %s.\n", authid.c_str());
			return;
		}

		ADMIN_PrintToClient(callerSlot, "[ADMIN] Ban history for %s (last 10):\n", authid.c_str());
		ADMIN_PrintToClient(callerSlot, "  %-12s %-16s %-12s %-4s %s\n",
			"Date", "Banned By", "Length", "R", "Reason");

		while (rs->MoreRows())
		{
			ISQLRow *row = rs->FetchRow();
			if (!row) break;

			int created = rs->GetInt(0);
			const char *admin = rs->GetString(1);
			int length = rs->GetInt(2);
			const char *removeType = rs->GetString(4);
			const char *reason = rs->GetString(5);

			const char *lengthStr = (length == 0) ? "Permanent" : "Temp";
			const char *status = (removeType && *removeType) ? removeType : " ";

			ADMIN_PrintToClient(callerSlot, "  %-12d %-16s %-12s %-4s %s\n",
				created, admin ? admin : "Unknown", lengthStr, status,
				reason ? reason : "");
		}
	});
}

void CS2ABanManager::ListComms(int callerSlot, const char *authid)
{
	if (!g_CS2ADatabase.IsConnected())
	{
		ADMIN_PrintToClient(callerSlot, "[ADMIN] Database not connected.\n");
		return;
	}

	std::string suffix = g_CS2ADatabase.Escape(ExtractAuthSuffix(std::string(authid)).c_str());
	std::string prefix = g_CS2AConfig.databasePrefix;

	std::string authCond = CS2ADatabase::AuthMatch("c.authid", suffix);

	char query[1024];
	snprintf(query, sizeof(query),
		"SELECT c.created, IFNULL(a.user, 'Console'), c.length, c.type, "
		"c.RemoveType, c.reason "
		"FROM %s_comms c LEFT JOIN %s_admins a ON a.aid = c.aid "
		"WHERE %s ORDER BY c.created DESC LIMIT 10",
		prefix.c_str(), prefix.c_str(), authCond.c_str());

	g_CS2ADatabase.Query(query, [callerSlot, authid = std::string(authid)](ISQLQuery *result) {
		if (!result)
		{
			ADMIN_PrintToClient(callerSlot, "[ADMIN] Query failed.\n");
			return;
		}

		ISQLResult *rs = result->GetResultSet();
		if (!rs || rs->GetRowCount() == 0)
		{
			ADMIN_PrintToClient(callerSlot, "[ADMIN] No comm blocks found for %s.\n", authid.c_str());
			return;
		}

		ADMIN_PrintToClient(callerSlot, "[ADMIN] Comm history for %s (last 10):\n", authid.c_str());
		ADMIN_PrintToClient(callerSlot, "  %-12s %-16s %-6s %-12s %-4s %s\n",
			"Date", "Admin", "Type", "Length", "R", "Reason");

		while (rs->MoreRows())
		{
			ISQLRow *row = rs->FetchRow();
			if (!row) break;

			int created = rs->GetInt(0);
			const char *admin = rs->GetString(1);
			int length = rs->GetInt(2);
			int type = rs->GetInt(3);
			const char *removeType = rs->GetString(4);
			const char *reason = rs->GetString(5);

			const char *typeStr = (type == 1) ? "Mute" : (type == 2) ? "Gag" : "?";
			const char *lengthStr = (length == 0) ? "Permanent" : "Temp";
			const char *status = (removeType && *removeType) ? removeType : " ";

			ADMIN_PrintToClient(callerSlot, "  %-12d %-16s %-6s %-12s %-4s %s\n",
				created, admin ? admin : "Unknown", typeStr, lengthStr, status,
				reason ? reason : "");
		}
	});
}
