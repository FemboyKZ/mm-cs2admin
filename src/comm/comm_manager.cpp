#include "comm_manager.h"
#include "../common.h"
#include "../config/config.h"
#include "../db/database.h"
#include "../public/forwards.h"
#include "../queue/offline_queue.h"
#include "../utils/print_utils.h"

#include <sql_mm.h>
#include <climits>
#include <ctime>

CS2ACommManager g_CS2ACommManager;

void CS2ACommManager::VerifyComms(int slot, uint64_t steamid64)
{
	if (!g_CS2ADatabase.IsConnected())
		return;

	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (!player)
		return;

	std::string suffix = g_CS2ADatabase.Escape(SteamID64ToSuffix(steamid64).c_str());
	std::string prefix = g_CS2AConfig.databasePrefix;

	long long now = (long long)std::time(nullptr);
	std::string authCond = CS2ADatabase::AuthMatch("c.authid", suffix);

	char query[1024];
	snprintf(query, sizeof(query),
		"SELECT (c.ends - %lld) AS remaining, "
		"c.length, c.type, c.created, c.reason, a.user, "
		"CASE WHEN a.immunity >= g.immunity THEN a.immunity ELSE IFNULL(g.immunity, 0) END AS immunity, "
		"c.aid, c.sid, a.authid "
		"FROM %s_comms AS c "
		"LEFT JOIN %s_admins AS a ON a.aid = c.aid "
		"LEFT JOIN %s_srvgroups AS g ON g.name = a.srv_group "
		"WHERE c.RemoveType IS NULL "
		"AND %s "
		"AND (c.length = '0' OR c.ends > %lld)",
		now,
		prefix.c_str(), prefix.c_str(), prefix.c_str(),
		authCond.c_str(),
		now);

	g_CS2ADatabase.Query(query, [slot, steamid64](ISQLQuery *result) {
		if (!result)
			return;

		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
		if (!player || !player->connected || player->steamid64 != steamid64)
			return;

		ISQLResult *rs = result->GetResultSet();
		if (!rs)
			return;

		while (rs->MoreRows())
		{
			ISQLRow *row = rs->FetchRow();
			if (!row)
				break;

			// Column 0 = remaining seconds
			// Column 1 = length (0 = permanent)
			// Column 2 = type (1 = mute, 2 = gag)
			// Column 4 = reason
			int remaining = rs->GetInt(0);
			int length = rs->GetInt(1);
			int type = rs->GetInt(2);
			const char *reason = rs->GetString(4);

			if (type == COMM_MUTE)
			{
				player->isMuted = true;
				player->muteRemaining = (length == 0) ? 0 : remaining;
				player->muteReason = reason ? reason : "";
				if (length != 0 && remaining > 0)
				{
					CGlobalVars *globals = GetGameGlobals();
					if (globals) player->muteExpireTime = globals->curtime + remaining;
				}
				if (length == 0)
					ADMIN_PrintToClient(slot, "[ADMIN] You are permanently muted. Reason: %s\n", player->muteReason.c_str());
				else
					ADMIN_PrintToClient(slot, "[ADMIN] You are muted (%d seconds remaining). Reason: %s\n", remaining, player->muteReason.c_str());
				META_CONPRINTF("[ADMIN] Player \"%s\" has active mute. Reason: %s\n",
					player->name.c_str(), player->muteReason.c_str());
			}
			else if (type == COMM_GAG)
			{
				player->isGagged = true;
				player->gagRemaining = (length == 0) ? 0 : remaining;
				player->gagReason = reason ? reason : "";
				if (length != 0 && remaining > 0)
				{
					CGlobalVars *globals = GetGameGlobals();
					if (globals) player->gagExpireTime = globals->curtime + remaining;
				}
				if (length == 0)
					ADMIN_PrintToClient(slot, "[ADMIN] You are permanently gagged. Reason: %s\n", player->gagReason.c_str());
				else
					ADMIN_PrintToClient(slot, "[ADMIN] You are gagged (%d seconds remaining). Reason: %s\n", remaining, player->gagReason.c_str());
				META_CONPRINTF("[ADMIN] Player \"%s\" has active gag. Reason: %s\n",
					player->name.c_str(), player->gagReason.c_str());
			}
		}
	});
}

bool CS2ACommManager::IsGagged(int slot)
{
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	return player && player->isGagged;
}

bool CS2ACommManager::IsMuted(int slot)
{
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	return player && player->isMuted;
}

void CS2ACommManager::InsertComm(const char *authid, const char *name,
	int timeMinutes, const char *reason, int adminSlot, int type)
{
	if (!g_CS2ADatabase.IsConnected())
	{
		META_CONPRINTF("[ADMIN] Cannot insert comm: database not connected.\n");
		return;
	}

	std::string prefix = g_CS2AConfig.databasePrefix;
	std::string escapedAuth = g_CS2ADatabase.Escape(authid);
	std::string escapedName = g_CS2ADatabase.Escape(name ? name : "");
	std::string escapedReason = g_CS2ADatabase.Escape(reason ? reason : "");
	std::string rawAdminAuth = GetAdminAuthId(adminSlot);
	std::string adminAuth = g_CS2ADatabase.Escape(rawAdminAuth.c_str());
	std::string adminIP = g_CS2ADatabase.Escape(GetAdminIP(adminSlot).c_str());
	std::string adminSuffix = g_CS2ADatabase.Escape(ExtractAuthSuffix(rawAdminAuth).c_str());

	int lengthSec = (timeMinutes > 0 && timeMinutes <= INT_MAX / 60) ? timeMinutes * 60 : 0;
	int sid = g_CS2AConfig.serverID != -1 ? g_CS2AConfig.serverID : 0;
	long long now = (long long)std::time(nullptr);
	std::string adminMatch = CS2ADatabase::AuthMatch("authid", adminSuffix);

	char query[2048];
	snprintf(query, sizeof(query),
		"INSERT INTO %s_comms (authid, name, created, ends, length, reason, aid, adminIp, sid, type) "
		"VALUES ('%s', '%s', %lld, %lld, %d, '%s', "
		"IFNULL((SELECT aid FROM %s_admins WHERE authid = '%s' OR %s), 0), "
		"'%s', %d, %d)",
		prefix.c_str(), escapedAuth.c_str(), escapedName.c_str(),
		now, now + lengthSec, lengthSec, escapedReason.c_str(),
		prefix.c_str(), adminAuth.c_str(), adminMatch.c_str(),
		adminIP.c_str(), sid, type);

	g_CS2ADatabase.Query(query, [type, queryStr = std::string(query)](ISQLQuery *result) {
		const char *typeName = (type == COMM_MUTE) ? "mute" : "gag";
		if (!result)
		{
			g_CS2AOfflineQueue.Enqueue(queryStr);
			return;
		}
		if (result->GetAffectedRows() > 0)
			META_CONPRINTF("[ADMIN] %s inserted successfully.\n", typeName);
		else
			META_CONPRINTF("[ADMIN] Failed to insert %s.\n", typeName);
	});
}

void CS2ACommManager::RemoveComm(const char *authid, int adminSlot, int type)
{
	if (!g_CS2ADatabase.IsConnected() || !authid)
		return;

	std::string prefix = g_CS2AConfig.databasePrefix;

	std::string suffix = g_CS2ADatabase.Escape(ExtractAuthSuffix(std::string(authid)).c_str());

	std::string rawAdminAuth = GetAdminAuthId(adminSlot);
	std::string adminAuth = g_CS2ADatabase.Escape(rawAdminAuth.c_str());
	std::string adminSuffix = g_CS2ADatabase.Escape(ExtractAuthSuffix(rawAdminAuth).c_str());

	long long now = (long long)std::time(nullptr);
	std::string adminMatch = CS2ADatabase::AuthMatch("authid", adminSuffix);
	std::string targetMatch = CS2ADatabase::AuthMatch("authid", suffix);

	char query[1024];
	snprintf(query, sizeof(query),
		"UPDATE %s_comms SET RemovedBy = "
		"IFNULL((SELECT aid FROM %s_admins WHERE authid = '%s' OR %s), 0), "
		"RemoveType = 'U', RemovedOn = %lld "
		"WHERE %s AND type = %d "
		"AND (length = '0' OR ends > %lld) "
		"AND RemoveType IS NULL",
		prefix.c_str(), prefix.c_str(),
		adminAuth.c_str(), adminMatch.c_str(),
		now,
		targetMatch.c_str(), type,
		now);

	g_CS2ADatabase.Query(query, [type](ISQLQuery *result) {
		const char *typeName = (type == COMM_MUTE) ? "mute" : "gag";
		if (result && result->GetAffectedRows() > 0)
			META_CONPRINTF("[ADMIN] Removed %s successfully.\n", typeName);
		else
			META_CONPRINTF("[ADMIN] No active %s found to remove.\n", typeName);
	});
}

void CS2ACommManager::MutePlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
		return;

	if (g_CS2AForwards.FireOnMutePlayer(targetSlot, adminSlot, timeMinutes, reason))
		return;

	target->isMuted = true;
	target->isSessionMuted = false;
	target->muteReason = reason ? reason : "";
	if (timeMinutes > 0)
	{
		CGlobalVars *globals = GetGameGlobals();
		if (globals) target->muteExpireTime = globals->curtime + ((double)timeMinutes * 60.0);
	}
	else
		target->muteExpireTime = 0.0;

	InsertComm(target->authid.c_str(), target->name.c_str(), timeMinutes, reason, adminSlot, COMM_MUTE);

	// Notify target
	if (timeMinutes == 0)
		ADMIN_PrintToClient(targetSlot, "[ADMIN] You have been permanently muted. Reason: %s\n", reason ? reason : "No reason");
	else
		ADMIN_PrintToClient(targetSlot, "[ADMIN] You have been muted for %d minutes. Reason: %s\n", timeMinutes, reason ? reason : "No reason");

	// Announce
	std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);
	ADMIN_PrintToAll("[ADMIN] %s muted %s (%d min). Reason: %s\n",
		adminName.c_str(), target->name.c_str(), timeMinutes, reason ? reason : "No reason");

	// Log
	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Muted \"%s\" (%s) for %d min. Reason: %s",
		target->name.c_str(), target->authid.c_str(), timeMinutes, reason ? reason : "No reason");
	ADMIN_LogAction(adminSlot, logMsg);

	META_CONPRINTF("[ADMIN] Muted \"%s\" for %d min. Reason: %s\n",
		target->name.c_str(), timeMinutes, reason ? reason : "No reason");
}

void CS2ACommManager::GagPlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
		return;

	if (g_CS2AForwards.FireOnGagPlayer(targetSlot, adminSlot, timeMinutes, reason))
		return;

	target->isGagged = true;
	target->isSessionGagged = false;
	target->gagReason = reason ? reason : "";
	if (timeMinutes > 0)
	{
		CGlobalVars *globals = GetGameGlobals();
		if (globals) target->gagExpireTime = globals->curtime + ((double)timeMinutes * 60.0);
	}
	else
		target->gagExpireTime = 0.0;

	InsertComm(target->authid.c_str(), target->name.c_str(), timeMinutes, reason, adminSlot, COMM_GAG);

	// Notify target
	if (timeMinutes == 0)
		ADMIN_PrintToClient(targetSlot, "[ADMIN] You have been permanently gagged. Reason: %s\n", reason ? reason : "No reason");
	else
		ADMIN_PrintToClient(targetSlot, "[ADMIN] You have been gagged for %d minutes. Reason: %s\n", timeMinutes, reason ? reason : "No reason");

	// Announce
	std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);
	ADMIN_PrintToAll("[ADMIN] %s gagged %s (%d min). Reason: %s\n",
		adminName.c_str(), target->name.c_str(), timeMinutes, reason ? reason : "No reason");

	// Log
	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Gagged \"%s\" (%s) for %d min. Reason: %s",
		target->name.c_str(), target->authid.c_str(), timeMinutes, reason ? reason : "No reason");
	ADMIN_LogAction(adminSlot, logMsg);

	META_CONPRINTF("[ADMIN] Gagged \"%s\" for %d min. Reason: %s\n",
		target->name.c_str(), timeMinutes, reason ? reason : "No reason");
}

void CS2ACommManager::SilencePlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot)
{
	MutePlayer(targetSlot, timeMinutes, reason, adminSlot);
	GagPlayer(targetSlot, timeMinutes, reason, adminSlot);
}

void CS2ACommManager::UnmutePlayer(int targetSlot, int adminSlot)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
		return;

	g_CS2AForwards.FireOnUnmutePlayer(targetSlot, adminSlot);

	target->isMuted = false;
	target->isSessionMuted = false;
	target->muteReason.clear();
	target->muteExpireTime = 0.0;

	RemoveComm(target->authid.c_str(), adminSlot, COMM_MUTE);

	ADMIN_PrintToClient(targetSlot, "[ADMIN] You have been unmuted.\n");

	std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);
	ADMIN_PrintToAll("[ADMIN] %s unmuted %s.\n", adminName.c_str(), target->name.c_str());

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Unmuted \"%s\" (%s)", target->name.c_str(), target->authid.c_str());
	ADMIN_LogAction(adminSlot, logMsg);
}

void CS2ACommManager::UngagPlayer(int targetSlot, int adminSlot)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
		return;

	g_CS2AForwards.FireOnUngagPlayer(targetSlot, adminSlot);

	target->isGagged = false;
	target->isSessionGagged = false;
	target->gagReason.clear();
	target->gagExpireTime = 0.0;

	RemoveComm(target->authid.c_str(), adminSlot, COMM_GAG);

	ADMIN_PrintToClient(targetSlot, "[ADMIN] You have been ungagged.\n");

	std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);
	ADMIN_PrintToAll("[ADMIN] %s ungagged %s.\n", adminName.c_str(), target->name.c_str());

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Ungagged \"%s\" (%s)", target->name.c_str(), target->authid.c_str());
	ADMIN_LogAction(adminSlot, logMsg);
}

void CS2ACommManager::UnsilencePlayer(int targetSlot, int adminSlot)
{
	UnmutePlayer(targetSlot, adminSlot);
	UngagPlayer(targetSlot, adminSlot);
}

void CS2ACommManager::SessionMutePlayer(int targetSlot, int adminSlot)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
		return;

	target->isMuted = true;
	target->isSessionMuted = true;
	target->muteExpireTime = 0.0;
	target->muteReason = "Session mute";

	std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);

	ADMIN_PrintToClient(targetSlot, "[ADMIN] You have been muted for this session.\n");
	ADMIN_ChatToAll("%s%s session-muted %s.\n", g_CS2AConfig.chatPrefix.c_str(), adminName.c_str(), target->name.c_str());

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Session-muted \"%s\" (%s)", target->name.c_str(), target->authid.c_str());
	ADMIN_LogAction(adminSlot, logMsg);
}

void CS2ACommManager::SessionGagPlayer(int targetSlot, int adminSlot)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
		return;

	target->isGagged = true;
	target->isSessionGagged = true;
	target->gagExpireTime = 0.0;
	target->gagReason = "Session gag";

	std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);

	ADMIN_PrintToClient(targetSlot, "[ADMIN] You have been gagged for this session.\n");
	ADMIN_ChatToAll("%s%s session-gagged %s.\n", g_CS2AConfig.chatPrefix.c_str(), adminName.c_str(), target->name.c_str());

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Session-gagged \"%s\" (%s)", target->name.c_str(), target->authid.c_str());
	ADMIN_LogAction(adminSlot, logMsg);
}

void CS2ACommManager::CheckExpiredComms()
{
	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
		return;

	double curtime = globals->curtime;

	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(i);
		if (!player)
			continue;

		if (player->isMuted && !player->isSessionMuted && player->muteExpireTime > 0.0 && curtime >= player->muteExpireTime)
		{
			player->isMuted = false;
			player->muteExpireTime = 0.0;
			player->muteReason.clear();
			ADMIN_PrintToClient(i, "[ADMIN] Your mute has expired.\n");
			META_CONPRINTF("[ADMIN] Mute expired for \"%s\" (%s).\n", player->name.c_str(), player->authid.c_str());
		}

		if (player->isGagged && !player->isSessionGagged && player->gagExpireTime > 0.0 && curtime >= player->gagExpireTime)
		{
			player->isGagged = false;
			player->gagExpireTime = 0.0;
			player->gagReason.clear();
			ADMIN_PrintToClient(i, "[ADMIN] Your gag has expired.\n");
			META_CONPRINTF("[ADMIN] Gag expired for \"%s\" (%s).\n", player->name.c_str(), player->authid.c_str());
		}
	}
}

void CS2ACommManager::OnClientDisconnect(int slot)
{
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (!player)
		return;

	// Session blocks are cleared on disconnect (they have no DB record)
	if (player->isSessionMuted)
	{
		player->isMuted = false;
		player->isSessionMuted = false;
		player->muteReason.clear();
	}
	if (player->isSessionGagged)
	{
		player->isGagged = false;
		player->isSessionGagged = false;
		player->gagReason.clear();
	}
}

void CS2ACommManager::PrintCommsStatus(int targetSlot, int callerSlot)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		ADMIN_ReplyToCommand(callerSlot, "Invalid target.\n");
		return;
	}

	ADMIN_ReplyToCommand(callerSlot, "Comm status for \"%s\" (%s):\n",
		target->name.c_str(), target->authid.c_str());

	if (target->isMuted)
	{
		if (target->isSessionMuted)
			ADMIN_ReplyToCommand(callerSlot, "  Muted: Session only\n");
		else if (target->muteExpireTime > 0.0)
		{
			CGlobalVars *globals = GetGameGlobals();
			int remaining = globals ? (int)(target->muteExpireTime - globals->curtime) : 0;
			if (remaining < 0) remaining = 0;
			ADMIN_ReplyToCommand(callerSlot, "  Muted: %d seconds remaining. Reason: %s\n", remaining, target->muteReason.c_str());
		}
		else
			ADMIN_ReplyToCommand(callerSlot, "  Muted: Permanent. Reason: %s\n", target->muteReason.c_str());
	}
	else
	{
		ADMIN_ReplyToCommand(callerSlot, "  Muted: No\n");
	}

	if (target->isGagged)
	{
		if (target->isSessionGagged)
			ADMIN_ReplyToCommand(callerSlot, "  Gagged: Session only\n");
		else if (target->gagExpireTime > 0.0)
		{
			CGlobalVars *globals = GetGameGlobals();
			int remaining = globals ? (int)(target->gagExpireTime - globals->curtime) : 0;
			if (remaining < 0) remaining = 0;
			ADMIN_ReplyToCommand(callerSlot, "  Gagged: %d seconds remaining. Reason: %s\n", remaining, target->gagReason.c_str());
		}
		else
			ADMIN_ReplyToCommand(callerSlot, "  Gagged: Permanent. Reason: %s\n", target->gagReason.c_str());
	}
	else
	{
		ADMIN_ReplyToCommand(callerSlot, "  Gagged: No\n");
	}
}
