#include "comm_manager.h"
#include "utils/log.h"
#include "src/common.h"
#include "src/admin/admin_manager.h"
#include "src/config/config.h"
#include "src/db/database.h"
#include "interfaces/cs2admin/forwards.h"
#include "src/queue/offline_queue.h"
#include "src/lang/translations.h"
#include "src/utils/print_utils.h"

#include "interfaces/sql_mm/sql_mm.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <ctime>
#include <sstream>

CS2ACommManager g_CS2ACommManager;

// Bypasses MaxMinutes and the unblock immunity check, as in SourceComms.
static const uint32_t COMM_BYPASS_FLAG = ADMFLAG_CHEATS;

static void RecordIssuer(int adminSlot, int &immunity, uint64_t &steamid64)
{
	if (adminSlot < 0)
	{
		immunity = g_CS2AConfig.consoleImmunity;
		steamid64 = 0;
		return;
	}
	const AdminEntry *admin = g_CS2AAdminManager.GetPlayerAdmin(adminSlot);
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(adminSlot);
	immunity = admin ? admin->immunity : 0;
	steamid64 = player ? player->steamid64 : 0;
}

bool CS2ACommManager::CanLiftBlock(int callerSlot, int targetSlot, int type) const
{
	if (callerSlot < 0)
	{
		return true;
	}

	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		return true;
	}

	bool active = (type == COMM_MUTE) ? target->isMuted : target->isGagged;
	if (!active)
	{
		return true;
	}

	uint64_t issuer = (type == COMM_MUTE) ? target->muteIssuerSteamid64 : target->gagIssuerSteamid64;
	int issuerImmunity = (type == COMM_MUTE) ? target->muteIssuerImmunity : target->gagIssuerImmunity;

	PlayerInfo *caller = g_CS2APlayerManager.GetPlayer(callerSlot);
	if (caller && issuer != 0 && caller->steamid64 == issuer)
	{
		return true;
	}

	if (g_CS2AAdminManager.PlayerHasFlag(callerSlot, COMM_BYPASS_FLAG))
	{
		return true;
	}

	if (g_CS2AConfig.disableUnblockImmunityCheck)
	{
		return false;
	}

	const AdminEntry *admin = g_CS2AAdminManager.GetPlayerAdmin(callerSlot);
	// At least equal rather than SourceComms' strictly higher, so peers at the same level can lift each other's blocks.
	return (admin ? admin->immunity : 0) >= issuerImmunity;
}

bool CS2ACommManager::IsAllowedLength(int callerSlot, int minutes) const
{
	int maxLength = g_CS2AConfig.commsMaxMinutes;
	if (maxLength <= 0 || callerSlot < 0 || g_CS2AAdminManager.PlayerHasFlag(callerSlot, COMM_BYPASS_FLAG))
	{
		return true;
	}
	return minutes != 0 && minutes <= maxLength;
}

// A block from a comms row or a pending write. remaining is in seconds, 0 for permanent.
static void ApplyStoredBlock(int slot, PlayerInfo *player, int type, int remaining, const char *reason, int issuerImmunity, uint64_t issuerSteamid64)
{
	const bool mute = type == COMM_MUTE;
	bool &active = mute ? player->isMuted : player->isGagged;
	std::string &why = mute ? player->muteReason : player->gagReason;

	// A reload re-verifies everyone, so only announce a block the player didn't already have.
	const bool announce = !active;
	active = true;
	(mute ? player->isSessionMuted : player->isSessionGagged) = false;
	why = reason ? reason : "";
	(mute ? player->muteExpireTime : player->gagExpireTime) = remaining > 0 ? Plat_FloatTime() + remaining : 0.0;
	(mute ? player->muteIssuerImmunity : player->gagIssuerImmunity) = issuerImmunity;
	(mute ? player->muteIssuerSteamid64 : player->gagIssuerSteamid64) = issuerSteamid64;
	if (!announce)
	{
		return;
	}

	if (remaining <= 0)
	{
		CS2A_PrintToChatT(slot, mute ? "You are permanently muted. Reason: %s\n" : "You are permanently gagged. Reason: %s\n", why.c_str());
	}
	else
	{
		CS2A_PrintToChatT(slot, mute ? "You are muted (%d seconds remaining). Reason: %s\n" : "You are gagged (%d seconds remaining). Reason: %s\n",
						   remaining, why.c_str());
	}
	MMU_LOG_INFO("Player \"%s\" has active %s. Reason: %s\n", player->name.c_str(), mute ? "mute" : "gag", why.c_str());
}

void CS2ACommManager::VerifyComms(int slot, uint64_t steamid64)
{
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (!player)
	{
		return;
	}

	// Blocks the database does not have yet.
	const std::string ownSuffix = SteamID64ToSuffix(steamid64);
	long long now = (long long)std::time(nullptr);
	for (const PendingComm &comm : m_pendingComms)
	{
		if (comm.authSuffix == ownSuffix && (comm.ends == 0 || comm.ends > now))
		{
			ApplyStoredBlock(slot, player, comm.type, comm.ends == 0 ? 0 : static_cast<int>(comm.ends - now), comm.reason.c_str(),
							 comm.issuerImmunity, comm.issuerSteamid64);
		}
	}

	if (!g_CS2ADatabase.IsConnected())
	{
		return;
	}

	std::string suffix = g_CS2ADatabase.Escape(ownSuffix.c_str());
	std::string prefix = g_CS2AConfig.database.prefix;

	std::string authCond = CS2ADatabase::AuthMatch("c.authid", suffix);

	char query[1024];
	snprintf(query, sizeof(query),
			 "SELECT (c.ends - %lld) AS remaining, "
			 "c.length, c.type, c.created, c.reason, a.user, "
			 // Without IFNULL an admin with no group compares against NULL and loses their own immunity.
			 "CASE WHEN IFNULL(a.immunity, 0) >= IFNULL(g.immunity, 0) THEN IFNULL(a.immunity, 0) ELSE g.immunity END AS immunity, "
			 "c.aid, c.sid, a.authid, c.adminIp "
			 "FROM %s_comms AS c "
			 "LEFT JOIN %s_admins AS a ON a.aid = c.aid "
			 "LEFT JOIN %s_srvgroups AS g ON g.name = a.srv_group "
			 "WHERE c.RemoveType IS NULL "
			 "AND %s "
			 "AND (c.length = '0' OR c.ends > %lld) "
			 // The loop below keeps the last row of each type, so order the strongest last: a permanent row wins, otherwise the one ending last.
			 "ORDER BY CASE WHEN c.length = '0' THEN 1 ELSE 0 END ASC, c.ends ASC",
			 now, prefix.c_str(), prefix.c_str(), prefix.c_str(), authCond.c_str(), now);

	g_CS2ADatabase.Query(query,
						 [slot, steamid64, generation = m_commGeneration[slot]](ISQLQuery *result)
						 {
							 if (!result)
							 {
								 return;
							 }

							 PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
							 if (!player || !player->connected || player->steamid64 != steamid64)
							 {
								 return;
							 }

							 // An admin changed this player's blocks while the query was in flight, so this answer is already out of date.
							 if (generation != g_CS2ACommManager.Generation(slot))
							 {
								 return;
							 }

							 ISQLResult *rs = result->GetResultSet();
							 if (!rs)
							 {
								 return;
							 }

							 bool foundMute = false;
							 bool foundGag = false;
							 while (rs->MoreRows())
							 {
								 ISQLRow *row = rs->FetchRow();
								 if (!row)
								 {
									 break;
								 }

								 // Column 0 = remaining seconds
								 // Column 1 = length (0 = permanent)
								 // Column 2 = type (1 = mute, 2 = gag)
								 // Column 4 = reason
								 int remaining = rs->GetInt(0);
								 int length = rs->GetInt(1);
								 int type = rs->GetInt(2);
								 const char *reason = rs->GetString(4);

								 // Column 6 = issuer immunity, 7 = issuer aid, 9 = issuer authid, 10 = issuer IP
								 int issuerImmunity = rs->GetInt(6);
								 const char *issuerIP = rs->GetString(10);
								 bool noAid = rs->GetInt(7) == 0;
								 // aid 0 is also any admin missing from the admins table, like a flat-file one. Only the console has no IP.
								 bool byConsole = noAid && (!issuerIP || !*issuerIP);
								 if (byConsole && g_CS2AConfig.consoleImmunity > issuerImmunity)
								 {
									 issuerImmunity = g_CS2AConfig.consoleImmunity;
								 }
								 else if (noAid && !byConsole)
								 {
									 // Unknown issuer, so any admin may lift it.
									 issuerImmunity = -1;
								 }
								 const char *issuerAuth = rs->GetString(9);
								 uint64_t issuerSteamid64 =
									 (noAid || !issuerAuth)
										 ? 0
										 : CS2AAdminManager::AuthIdToSteamID64(CS2AAdminManager::NormalizeSteamID(issuerAuth).c_str());

								 if (type != COMM_MUTE && type != COMM_GAG)
								 {
									 continue;
								 }
								 (type == COMM_MUTE ? foundMute : foundGag) = true;
								 ApplyStoredBlock(slot, player, type, length == 0 ? 0 : remaining, reason, issuerImmunity, issuerSteamid64);
							 }

							 // A block lifted elsewhere (web panel, another server) has no row any more.
							 // Session blocks never had one, so they stay.
							 // Nor has a block whose insert is still queued.
							 if (!foundMute && player->isMuted && !player->isSessionMuted && !g_CS2ACommManager.HasPendingComm(steamid64, COMM_MUTE))
							 {
								 player->isMuted = false;
								 player->muteExpireTime = 0.0;
								 player->muteReason.clear();
								 CS2A_PrintToChatT(slot, "You have been unmuted.\n");
								 MMU_LOG_INFO("Mute for \"%s\" no longer in the database, lifted.\n", player->name.c_str());
							 }
							 if (!foundGag && player->isGagged && !player->isSessionGagged && !g_CS2ACommManager.HasPendingComm(steamid64, COMM_GAG))
							 {
								 player->isGagged = false;
								 player->gagExpireTime = 0.0;
								 player->gagReason.clear();
								 CS2A_PrintToChatT(slot, "You have been ungagged.\n");
								 MMU_LOG_INFO("Gag for \"%s\" no longer in the database, lifted.\n", player->name.c_str());
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

void CS2ACommManager::InsertComm(const char *authid, const char *name, int timeMinutes, const char *reason, int adminSlot, int type)
{
	if (!g_CS2ADatabase.IsInitialized())
	{
		MMU_LOG_WARN("Cannot insert comm: no database configured.\n");
		return;
	}

	std::string prefix = g_CS2AConfig.database.prefix;
	std::string escapedAuth = g_CS2ADatabase.Escape(authid);
	std::string escapedName = g_CS2ADatabase.Escape(name ? name : "");
	std::string escapedReason = g_CS2ADatabase.Escape(reason ? reason : "");
	std::string rawAdminAuth = GetAdminAuthId(adminSlot);
	std::string adminAuth = g_CS2ADatabase.Escape(rawAdminAuth.c_str());
	std::string adminIP = g_CS2ADatabase.Escape(GetAdminIP(adminSlot).c_str());
	std::string adminSuffix = g_CS2ADatabase.Escape(ExtractAuthSuffix(rawAdminAuth).c_str());

	int lengthSec = (timeMinutes > 0 && timeMinutes <= INT_MAX / 60) ? timeMinutes * 60 : 0;
	int sid = g_CS2AConfig.serverId != -1 ? g_CS2AConfig.serverId : 0;
	long long now = (long long)std::time(nullptr);
	std::string adminMatch = CS2ADatabase::AuthMatch("authid", adminSuffix);

	char query[2048];
	snprintf(query, sizeof(query),
			 "INSERT INTO %s_comms (authid, name, created, ends, length, reason, aid, adminIp, sid, type) "
			 "VALUES ('%s', '%s', %lld, %lld, %d, '%s', "
			 "IFNULL((SELECT aid FROM %s_admins WHERE authid = '%s' OR %s), 0), "
			 "'%s', %d, %d)",
			 prefix.c_str(), escapedAuth.c_str(), escapedName.c_str(), now, now + lengthSec, lengthSec, escapedReason.c_str(), prefix.c_str(),
			 adminAuth.c_str(), adminMatch.c_str(), adminIP.c_str(), sid, type);

	PendingComm pending;
	pending.authSuffix = ExtractAuthSuffix(authid);
	pending.type = type;
	pending.ends = lengthSec > 0 ? now + lengthSec : 0;
	pending.reason = reason ? reason : "";
	RecordIssuer(adminSlot, pending.issuerImmunity, pending.issuerSteamid64);

	const std::string note = "comm\t" + pending.authSuffix + "\t" + std::to_string(type) + "\t" + std::to_string(pending.ends) + "\t"
							 + std::to_string(pending.issuerImmunity) + "\t" + std::to_string(pending.issuerSteamid64) + "\t" + pending.reason;

	// Otherwise the block only lives until the player reconnects.
	g_CS2AOfflineQueue.Submit(query, TrackPending(std::move(pending)), note);
}

std::function<void()> CS2ACommManager::TrackPending(PendingComm comm)
{
	const uint64_t id = m_nextPendingId++;
	comm.id = id;
	m_pendingComms.push_back(std::move(comm));
	return [this, id]()
	{
		m_pendingComms.erase(std::remove_if(m_pendingComms.begin(), m_pendingComms.end(), [id](const PendingComm &c) { return c.id == id; }),
							 m_pendingComms.end());
	};
}

std::function<void()> CS2ACommManager::RestorePending(const std::string &note)
{
	std::stringstream ss(note);
	std::string kind, type, ends, immunity, issuer;
	PendingComm comm;
	if (!std::getline(ss, kind, '\t') || (kind != "comm" && kind != "uncomm") || !std::getline(ss, comm.authSuffix, '\t')
		|| !std::getline(ss, type, '\t'))
	{
		return nullptr;
	}
	comm.type = std::atoi(type.c_str());

	// Notes replay in queue order, so a later unblock undoes its block.
	if (kind == "uncomm")
	{
		m_pendingComms.erase(std::remove_if(m_pendingComms.begin(), m_pendingComms.end(),
											[&](const PendingComm &c) { return c.type == comm.type && c.authSuffix == comm.authSuffix; }),
							 m_pendingComms.end());
		return nullptr;
	}

	if (!std::getline(ss, ends, '\t') || !std::getline(ss, immunity, '\t') || !std::getline(ss, issuer, '\t'))
	{
		return nullptr;
	}
	std::getline(ss, comm.reason);
	comm.ends = std::atoll(ends.c_str());
	comm.issuerImmunity = std::atoi(immunity.c_str());
	comm.issuerSteamid64 = std::strtoull(issuer.c_str(), nullptr, 10);
	return TrackPending(std::move(comm));
}

bool CS2ACommManager::HasPendingComm(uint64_t steamid64, int type) const
{
	const std::string suffix = SteamID64ToSuffix(steamid64);
	return std::any_of(m_pendingComms.begin(), m_pendingComms.end(), [&](const PendingComm &c) { return c.type == type && c.authSuffix == suffix; });
}

void CS2ACommManager::RemoveComm(const char *authid, int adminSlot, int type)
{
	if (!g_CS2ADatabase.IsInitialized() || !authid)
	{
		return;
	}

	std::string prefix = g_CS2AConfig.database.prefix;

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
			 prefix.c_str(), prefix.c_str(), adminAuth.c_str(), adminMatch.c_str(), now, targetMatch.c_str(), type, now);

	const std::string rawSuffix = ExtractAuthSuffix(std::string(authid));
	m_pendingComms.erase(std::remove_if(m_pendingComms.begin(), m_pendingComms.end(),
										[&](const PendingComm &c) { return c.type == type && c.authSuffix == rawSuffix; }),
						 m_pendingComms.end());

	// Otherwise the next reload or reconnect reads the row back and puts the block on again.
	g_CS2AOfflineQueue.Submit(query, nullptr, "uncomm\t" + rawSuffix + "\t" + std::to_string(type));
}

bool CS2ACommManager::MutePlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot, bool persist)
{
	if (!ApplyMute(targetSlot, timeMinutes, reason, adminSlot, persist))
	{
		return false;
	}
	AnnounceBlock(targetSlot, adminSlot, timeMinutes, reason, "You have been permanently muted. Reason: %s\n",
				  "You have been muted for %d minutes. Reason: %s\n", "%s muted %s (%d min). Reason: %s\n", "%s permanently muted %s. Reason: %s\n");
	return true;
}

bool CS2ACommManager::GagPlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot, bool persist)
{
	if (!ApplyGag(targetSlot, timeMinutes, reason, adminSlot, persist))
	{
		return false;
	}
	AnnounceBlock(targetSlot, adminSlot, timeMinutes, reason, "You have been permanently gagged. Reason: %s\n",
				  "You have been gagged for %d minutes. Reason: %s\n", "%s gagged %s (%d min). Reason: %s\n",
				  "%s permanently gagged %s. Reason: %s\n");
	return true;
}

void CS2ACommManager::AnnounceBlock(int targetSlot, int adminSlot, int timeMinutes, const char *reason, const char *permanentPhrase,
									const char *timedPhrase, const char *allPhrase, const char *allPermanentPhrase)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		return;
	}
	const char *why = reason ? reason : "No reason";
	std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);
	if (timeMinutes == 0)
	{
		CS2A_PrintToChatT(targetSlot, permanentPhrase, why);
		CS2A_ChatToAllT(allPermanentPhrase, adminName.c_str(), target->name.c_str(), why);
	}
	else
	{
		CS2A_PrintToChatT(targetSlot, timedPhrase, timeMinutes, why);
		CS2A_ChatToAllT(allPhrase, adminName.c_str(), target->name.c_str(), timeMinutes, why);
	}
}

void CS2ACommManager::AnnounceLift(int targetSlot, int adminSlot, const char *selfPhrase, const char *allPhrase)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		return;
	}
	CS2A_PrintToChatT(targetSlot, selfPhrase);
	std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);
	CS2A_ChatToAllT(allPhrase, adminName.c_str(), target->name.c_str());
}

bool CS2ACommManager::ApplyMute(int targetSlot, int timeMinutes, const char *reason, int adminSlot, bool persist)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		return false;
	}

	if (g_CS2AForwards.FireOnMutePlayer(targetSlot, adminSlot, timeMinutes, reason))
	{
		return false;
	}

	BumpGeneration(targetSlot);
	target->isMuted = true;
	target->isSessionMuted = false;
	target->muteReason = reason ? reason : "";
	if (timeMinutes > 0)
	{
		target->muteExpireTime = Plat_FloatTime() + ((double)timeMinutes * 60.0);
	}
	else
	{
		target->muteExpireTime = 0.0;
	}
	RecordIssuer(adminSlot, target->muteIssuerImmunity, target->muteIssuerSteamid64);

	if (persist)
	{
		InsertComm(target->authid.c_str(), target->name.c_str(), timeMinutes, reason, adminSlot, COMM_MUTE);
	}

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Muted \"%s\" (%s) for %d min. Reason: %s", target->name.c_str(), target->authid.c_str(), timeMinutes,
			 reason ? reason : "No reason");
	CS2A_LogAction(adminSlot, logMsg);

	MMU_LOG_INFO("Muted \"%s\" for %d min. Reason: %s\n", target->name.c_str(), timeMinutes, reason ? reason : "No reason");
	return true;
}

bool CS2ACommManager::ApplyGag(int targetSlot, int timeMinutes, const char *reason, int adminSlot, bool persist)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		return false;
	}

	if (g_CS2AForwards.FireOnGagPlayer(targetSlot, adminSlot, timeMinutes, reason))
	{
		return false;
	}

	BumpGeneration(targetSlot);
	target->isGagged = true;
	target->isSessionGagged = false;
	target->gagReason = reason ? reason : "";
	if (timeMinutes > 0)
	{
		target->gagExpireTime = Plat_FloatTime() + ((double)timeMinutes * 60.0);
	}
	else
	{
		target->gagExpireTime = 0.0;
	}
	RecordIssuer(adminSlot, target->gagIssuerImmunity, target->gagIssuerSteamid64);

	if (persist)
	{
		InsertComm(target->authid.c_str(), target->name.c_str(), timeMinutes, reason, adminSlot, COMM_GAG);
	}

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Gagged \"%s\" (%s) for %d min. Reason: %s", target->name.c_str(), target->authid.c_str(), timeMinutes,
			 reason ? reason : "No reason");
	CS2A_LogAction(adminSlot, logMsg);

	MMU_LOG_INFO("Gagged \"%s\" for %d min. Reason: %s\n", target->name.c_str(), timeMinutes, reason ? reason : "No reason");
	return true;
}

int CS2ACommManager::SilencePlayer(int targetSlot, int timeMinutes, const char *reason, int adminSlot, bool persist)
{
	if (g_CS2AForwards.FireOnSilencePlayer(targetSlot, adminSlot, timeMinutes, reason))
	{
		return 0;
	}

	// The mute and gag forwards still fire, so one of them can block its half.
	// The announcement then names whatever actually applied instead of claiming a full silence.
	bool muted = ApplyMute(targetSlot, timeMinutes, reason, adminSlot, persist);
	bool gagged = ApplyGag(targetSlot, timeMinutes, reason, adminSlot, persist);
	if (muted && gagged)
	{
		AnnounceBlock(targetSlot, adminSlot, timeMinutes, reason, "You have been permanently silenced. Reason: %s\n",
					  "You have been silenced for %d minutes. Reason: %s\n", "%s silenced %s (%d min). Reason: %s\n",
					  "%s permanently silenced %s. Reason: %s\n");
	}
	else if (muted)
	{
		AnnounceBlock(targetSlot, adminSlot, timeMinutes, reason, "You have been permanently muted. Reason: %s\n",
					  "You have been muted for %d minutes. Reason: %s\n", "%s muted %s (%d min). Reason: %s\n",
					  "%s permanently muted %s. Reason: %s\n");
	}
	else if (gagged)
	{
		AnnounceBlock(targetSlot, adminSlot, timeMinutes, reason, "You have been permanently gagged. Reason: %s\n",
					  "You have been gagged for %d minutes. Reason: %s\n", "%s gagged %s (%d min). Reason: %s\n",
					  "%s permanently gagged %s. Reason: %s\n");
	}
	return (muted ? COMM_MUTE : 0) | (gagged ? COMM_GAG : 0);
}

bool CS2ACommManager::UnmutePlayer(int targetSlot, int adminSlot, bool persist)
{
	if (!LiftMute(targetSlot, adminSlot, persist))
	{
		return false;
	}
	AnnounceLift(targetSlot, adminSlot, "You have been unmuted.\n", "%s unmuted %s.\n");
	return true;
}

bool CS2ACommManager::UngagPlayer(int targetSlot, int adminSlot, bool persist)
{
	if (!LiftGag(targetSlot, adminSlot, persist))
	{
		return false;
	}
	AnnounceLift(targetSlot, adminSlot, "You have been ungagged.\n", "%s ungagged %s.\n");
	return true;
}

int CS2ACommManager::UnsilencePlayer(int targetSlot, int adminSlot)
{
	g_CS2AForwards.FireOnUnsilencePlayer(targetSlot, adminSlot);
	bool unmuted = LiftMute(targetSlot, adminSlot, true);
	bool ungagged = LiftGag(targetSlot, adminSlot, true);
	if (unmuted && ungagged)
	{
		AnnounceLift(targetSlot, adminSlot, "You have been unsilenced.\n", "%s unsilenced %s.\n");
	}
	else if (unmuted)
	{
		AnnounceLift(targetSlot, adminSlot, "You have been unmuted.\n", "%s unmuted %s.\n");
	}
	else if (ungagged)
	{
		AnnounceLift(targetSlot, adminSlot, "You have been ungagged.\n", "%s ungagged %s.\n");
	}
	return (unmuted ? COMM_MUTE : 0) | (ungagged ? COMM_GAG : 0);
}

bool CS2ACommManager::LiftMute(int targetSlot, int adminSlot, bool persist)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		return false;
	}

	// Nothing to lift here, but the DB can still hold a row this server never loaded (added elsewhere while the player was on), so clear it quietly.
	if (!target->isMuted)
	{
		if (persist)
		{
			RemoveComm(target->authid.c_str(), adminSlot, COMM_MUTE);
		}
		return false;
	}

	g_CS2AForwards.FireOnUnmutePlayer(targetSlot, adminSlot);

	BumpGeneration(targetSlot);
	target->isMuted = false;
	target->isSessionMuted = false;
	target->muteReason.clear();
	target->muteExpireTime = 0.0;

	if (persist)
	{
		RemoveComm(target->authid.c_str(), adminSlot, COMM_MUTE);
	}

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Unmuted \"%s\" (%s)", target->name.c_str(), target->authid.c_str());
	CS2A_LogAction(adminSlot, logMsg);
	return true;
}

bool CS2ACommManager::LiftGag(int targetSlot, int adminSlot, bool persist)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		return false;
	}

	if (!target->isGagged)
	{
		if (persist)
		{
			RemoveComm(target->authid.c_str(), adminSlot, COMM_GAG);
		}
		return false;
	}

	g_CS2AForwards.FireOnUngagPlayer(targetSlot, adminSlot);

	BumpGeneration(targetSlot);
	target->isGagged = false;
	target->isSessionGagged = false;
	target->gagReason.clear();
	target->gagExpireTime = 0.0;

	if (persist)
	{
		RemoveComm(target->authid.c_str(), adminSlot, COMM_GAG);
	}

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Ungagged \"%s\" (%s)", target->name.c_str(), target->authid.c_str());
	CS2A_LogAction(adminSlot, logMsg);
	return true;
}

bool CS2ACommManager::SessionMutePlayer(int targetSlot, int adminSlot)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		return false;
	}

	if (g_CS2AForwards.FireOnMutePlayer(targetSlot, adminSlot, -1, "Session mute"))
	{
		return false;
	}

	BumpGeneration(targetSlot);
	target->isMuted = true;
	target->isSessionMuted = true;
	target->muteExpireTime = 0.0;
	target->muteReason = "Session mute";
	RecordIssuer(adminSlot, target->muteIssuerImmunity, target->muteIssuerSteamid64);

	std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);

	CS2A_PrintToChatT(targetSlot, "You have been muted for this session.\n");
	CS2A_ChatToAllT("%s session-muted %s.\n", adminName.c_str(), target->name.c_str());

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Session-muted \"%s\" (%s)", target->name.c_str(), target->authid.c_str());
	CS2A_LogAction(adminSlot, logMsg);
	return true;
}

bool CS2ACommManager::SessionGagPlayer(int targetSlot, int adminSlot)
{
	PlayerInfo *target = g_CS2APlayerManager.GetPlayer(targetSlot);
	if (!target)
	{
		return false;
	}

	if (g_CS2AForwards.FireOnGagPlayer(targetSlot, adminSlot, -1, "Session gag"))
	{
		return false;
	}

	BumpGeneration(targetSlot);
	target->isGagged = true;
	target->isSessionGagged = true;
	target->gagExpireTime = 0.0;
	target->gagReason = "Session gag";
	RecordIssuer(adminSlot, target->gagIssuerImmunity, target->gagIssuerSteamid64);

	std::string adminName = g_CS2APlayerManager.GetAdminName(adminSlot);

	CS2A_PrintToChatT(targetSlot, "You have been gagged for this session.\n");
	CS2A_ChatToAllT("%s session-gagged %s.\n", adminName.c_str(), target->name.c_str());

	char logMsg[512];
	snprintf(logMsg, sizeof(logMsg), "Session-gagged \"%s\" (%s)", target->name.c_str(), target->authid.c_str());
	CS2A_LogAction(adminSlot, logMsg);
	return true;
}

int CS2ACommManager::SessionSilencePlayer(int targetSlot, int adminSlot)
{
	if (g_CS2AForwards.FireOnSilencePlayer(targetSlot, adminSlot, -1, "Session silence"))
	{
		return 0;
	}
	bool muted = SessionMutePlayer(targetSlot, adminSlot);
	bool gagged = SessionGagPlayer(targetSlot, adminSlot);
	return (muted ? COMM_MUTE : 0) | (gagged ? COMM_GAG : 0);
}

void CS2ACommManager::CheckExpiredComms()
{
	double curtime = Plat_FloatTime();

	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(i);
		if (!player)
		{
			continue;
		}

		if (player->isMuted && !player->isSessionMuted && player->muteExpireTime > 0.0 && curtime >= player->muteExpireTime)
		{
			player->isMuted = false;
			player->muteExpireTime = 0.0;
			player->muteReason.clear();
			CS2A_PrintToChatT(i, "Your mute has expired.\n");
			MMU_LOG_INFO("Mute expired for \"%s\" (%s).\n", player->name.c_str(), player->authid.c_str());
		}

		if (player->isGagged && !player->isSessionGagged && player->gagExpireTime > 0.0 && curtime >= player->gagExpireTime)
		{
			player->isGagged = false;
			player->gagExpireTime = 0.0;
			player->gagReason.clear();
			CS2A_PrintToChatT(i, "Your gag has expired.\n");
			MMU_LOG_INFO("Gag expired for \"%s\" (%s).\n", player->name.c_str(), player->authid.c_str());
		}
	}
}

void CS2ACommManager::OnClientDisconnect(int slot)
{
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (!player)
	{
		return;
	}

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
		CS2A_ReplyToCommandT(callerSlot, "Invalid target.\n");
		return;
	}

	CS2A_ReplyToCommandT(callerSlot, "Comm status for \"%s\" (%s):\n", target->name.c_str(), target->authid.c_str());

	if (target->isMuted)
	{
		if (target->isSessionMuted)
		{
			CS2A_ReplyToCommandT(callerSlot, "  Muted: Session only\n");
		}
		else if (target->muteExpireTime > 0.0)
		{
			int remaining = (int)(target->muteExpireTime - Plat_FloatTime());
			if (remaining < 0)
			{
				remaining = 0;
			}
			CS2A_ReplyToCommandT(callerSlot, "  Muted: %d seconds remaining. Reason: %s\n", remaining, target->muteReason.c_str());
		}
		else
		{
			CS2A_ReplyToCommandT(callerSlot, "  Muted: Permanent. Reason: %s\n", target->muteReason.c_str());
		}
	}
	else
	{
		CS2A_ReplyToCommandT(callerSlot, "  Muted: No\n");
	}

	if (target->isGagged)
	{
		if (target->isSessionGagged)
		{
			CS2A_ReplyToCommandT(callerSlot, "  Gagged: Session only\n");
		}
		else if (target->gagExpireTime > 0.0)
		{
			int remaining = (int)(target->gagExpireTime - Plat_FloatTime());
			if (remaining < 0)
			{
				remaining = 0;
			}
			CS2A_ReplyToCommandT(callerSlot, "  Gagged: %d seconds remaining. Reason: %s\n", remaining, target->gagReason.c_str());
		}
		else
		{
			CS2A_ReplyToCommandT(callerSlot, "  Gagged: Permanent. Reason: %s\n", target->gagReason.c_str());
		}
	}
	else
	{
		CS2A_ReplyToCommandT(callerSlot, "  Gagged: No\n");
	}
}
