#include "admin_manager.h"
#include "../common.h"
#include "../config/config.h"
#include "../db/database.h"

#include <sql_mm.h>

#include <cstring>
#include <cstdio>

void CS2AAdminManager::LoadGroups(std::function<void()> onComplete)
{
	m_groupIdToName.clear();
	m_dbGroupsLoaded = false;

	if (!g_CS2ADatabase.IsConnected())
	{
		m_dbGroupsLoaded = true;
		if (onComplete) onComplete();
		return;
	}

	std::string prefix = g_CS2AConfig.databasePrefix;
	char query[512];
	snprintf(query, sizeof(query),
		"SELECT id, name, flags, immunity FROM %s_srvgroups ORDER BY id",
		prefix.c_str());

	g_CS2ADatabase.Query(query, [this, onComplete](ISQLQuery *result) {
		if (!result)
		{
			META_CONPRINTF("[ADMIN] Failed to load admin groups from database.\n");
			m_dbGroupsLoaded = true;
			if (onComplete) onComplete();
			return;
		}

		ISQLResult *rs = result->GetResultSet();
		if (!rs)
		{
			m_dbGroupsLoaded = true;
			if (onComplete) onComplete();
			return;
		}

		while (rs->MoreRows())
		{
			ISQLRow *row = rs->FetchRow();
			if (!row)
				break;

			AdminGroup group;
			group.id = rs->GetInt(0);
			const char *name = rs->GetString(1);
			const char *flags = rs->GetString(2);
			group.name = name ? name : "";
			group.flags = FlagsFromString(flags);
			group.immunity = rs->GetInt(3);

			if (!group.name.empty())
			{
				// Merge with existing group (flat-file groups may already exist)
				auto existing = m_groups.find(group.name);
				if (existing != m_groups.end())
				{
					existing->second.flags |= group.flags;
					if (group.immunity > existing->second.immunity)
						existing->second.immunity = group.immunity;
					existing->second.id = group.id;
				}
				else
				{
					m_groups[group.name] = group;
				}
				m_groupIdToName[group.id] = group.name;
			}
		}

		META_CONPRINTF("[ADMIN] Loaded %zu admin group(s) from database.\n", m_groups.size());
		m_dbGroupsLoaded = true;
		if (onComplete) onComplete();
	});
}

void CS2AAdminManager::LoadGroupOverrides(std::function<void()> onComplete)
{
	if (!g_CS2ADatabase.IsConnected())
	{
		if (onComplete) onComplete();
		return;
	}

	std::string prefix = g_CS2AConfig.databasePrefix;
	char query[512];
	snprintf(query, sizeof(query),
		"SELECT so.group_id, so.type, so.name, so.access "
		"FROM %s_srvgroups_overrides so "
		"ORDER BY so.group_id",
		prefix.c_str());

	g_CS2ADatabase.Query(query, [this, onComplete](ISQLQuery *result) {
		if (!result)
		{
			META_CONPRINTF("[ADMIN] Failed to load group overrides from database.\n");
			if (onComplete) onComplete();
			return;
		}

		ISQLResult *rs = result->GetResultSet();
		if (!rs)
		{
			if (onComplete) onComplete();
			return;
		}

		int count = 0;
		while (rs->MoreRows())
		{
			ISQLRow *row = rs->FetchRow();
			if (!row)
				break;

			int groupId = rs->GetInt(0);
			const char *type = rs->GetString(1);
			const char *name = rs->GetString(2);
			const char *access = rs->GetString(3);

			if (!type || !name || !access)
				continue;

			// Find the group by DB id
			auto it = m_groupIdToName.find(groupId);
			if (it == m_groupIdToName.end())
				continue;

			auto grpIt = m_groups.find(it->second);
			if (grpIt == m_groups.end())
				continue;

			// Build override key: "cmd:<name>" or "grp:<name>"
			std::string key;
			if (strcmp(type, "command") == 0)
				key = "cmd:" + std::string(name);
			else if (strcmp(type, "group") == 0)
				key = "grp:" + std::string(name);
			else
				continue;

			OverrideRule rule = (strcmp(access, "allow") == 0) ? Command_Allow : Command_Deny;
			grpIt->second.overrides[key] = rule;
			count++;
		}

		META_CONPRINTF("[ADMIN] Loaded %d group override(s) from database.\n", count);
		if (onComplete) onComplete();
	});
}

void CS2AAdminManager::LoadGlobalOverrides(std::function<void()> onComplete)
{
	m_globalOverrides.clear();

	if (!g_CS2ADatabase.IsConnected())
	{
		if (onComplete) onComplete();
		return;
	}

	std::string prefix = g_CS2AConfig.databasePrefix;
	char query[512];
	snprintf(query, sizeof(query),
		"SELECT type, name, flags FROM %s_overrides ORDER BY id",
		prefix.c_str());

	g_CS2ADatabase.Query(query, [this, onComplete](ISQLQuery *result) {
		if (!result)
		{
			META_CONPRINTF("[ADMIN] Failed to load global overrides from database.\n");
			if (onComplete) onComplete();
			return;
		}

		ISQLResult *rs = result->GetResultSet();
		if (!rs)
		{
			if (onComplete) onComplete();
			return;
		}

		while (rs->MoreRows())
		{
			ISQLRow *row = rs->FetchRow();
			if (!row)
				break;

			const char *type = rs->GetString(0);
			const char *name = rs->GetString(1);
			const char *flags = rs->GetString(2);

			if (!type || !name || !flags)
				continue;

			std::string key;
			if (strcmp(type, "command") == 0)
				key = "cmd:" + std::string(name);
			else if (strcmp(type, "group") == 0)
				key = "grp:" + std::string(name);
			else
				continue;

			m_globalOverrides[key] = FlagsFromString(flags);
		}

		META_CONPRINTF("[ADMIN] Loaded %zu global override(s) from database.\n", m_globalOverrides.size());
		if (onComplete) onComplete();
	});
}

void CS2AAdminManager::LoadAdminsFromDB(std::function<void()> onComplete)
{
	m_dbAdmins.clear();
	m_dbAdminsLoaded = false;

	if (!g_CS2ADatabase.IsConnected())
	{
		m_dbAdminsLoaded = true;
		if (onComplete) onComplete();
		return;
	}

	std::string prefix = g_CS2AConfig.databasePrefix;
	char query[2048];

	if (g_CS2ADatabase.IsSQLite())
	{
		// Standalone SQLite mode: load ALL admins directly
		snprintf(query, sizeof(query),
			"SELECT a.aid, a.authid, a.srv_flags, a.srv_group, a.user, a.immunity "
			"FROM %s_admins AS a",
			prefix.c_str());
	}
	else if (g_CS2AConfig.serverID == -1)
	{
		ConVarRefAbstract hostip_ref("hostip");
		ConVarRefAbstract hostport_ref("hostport");

		if (!hostip_ref.IsValidRef() || !hostport_ref.IsValidRef())
		{
			META_CONPRINTF("[ADMIN] Cannot load admins by IP: hostip/hostport cvars not available.\n");
			m_dbAdminsLoaded = true;
			if (onComplete) onComplete();
			return;
		}

		int hostip = hostip_ref.GetInt();
		int hostport = hostport_ref.GetInt();

		char ipStr[32];
		snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
			(hostip >> 24) & 0xFF, (hostip >> 16) & 0xFF,
			(hostip >> 8) & 0xFF, hostip & 0xFF);

		snprintf(query, sizeof(query),
			"SELECT a.aid, a.authid, a.srv_flags, a.srv_group, a.user, a.immunity "
			"FROM %s_admins_servers_groups AS asg "
			"LEFT JOIN %s_admins AS a ON a.aid = asg.admin_id "
			"WHERE (server_id = (SELECT sid FROM %s_servers WHERE ip = '%s' AND port = '%d' LIMIT 1) "
			"OR srv_group_id IN (SELECT group_id FROM %s_servers_groups "
			"WHERE server_id = (SELECT sid FROM %s_servers WHERE ip = '%s' AND port = '%d' LIMIT 1))) "
			"%s"
			"GROUP BY a.aid, a.authid, a.srv_flags, a.srv_group, a.user, a.immunity",
			prefix.c_str(), prefix.c_str(), prefix.c_str(),
			g_CS2ADatabase.Escape(ipStr).c_str(), hostport,
			prefix.c_str(), prefix.c_str(),
			g_CS2ADatabase.Escape(ipStr).c_str(), hostport,
			g_CS2AConfig.requireSiteLogin ? "AND a.lastvisit IS NOT NULL AND a.lastvisit != '' " : "");
	}
	else
	{
		snprintf(query, sizeof(query),
			"SELECT a.aid, a.authid, a.srv_flags, a.srv_group, a.user, a.immunity "
			"FROM %s_admins_servers_groups AS asg "
			"LEFT JOIN %s_admins AS a ON a.aid = asg.admin_id "
			"WHERE (server_id = %d "
			"OR srv_group_id IN (SELECT group_id FROM %s_servers_groups WHERE server_id = %d)) "
			"%s"
			"GROUP BY a.aid, a.authid, a.srv_flags, a.srv_group, a.user, a.immunity",
			prefix.c_str(), prefix.c_str(), g_CS2AConfig.serverID,
			prefix.c_str(), g_CS2AConfig.serverID,
			g_CS2AConfig.requireSiteLogin ? "AND a.lastvisit IS NOT NULL AND a.lastvisit != '' " : "");
	}

	g_CS2ADatabase.Query(query, [this, onComplete](ISQLQuery *result) {
		if (!result)
		{
			META_CONPRINTF("[ADMIN] Failed to load admins from database.\n");
			m_dbAdminsLoaded = true;
			if (onComplete) onComplete();
			return;
		}

		ISQLResult *rs = result->GetResultSet();
		if (!rs)
		{
			m_dbAdminsLoaded = true;
			if (onComplete) onComplete();
			return;
		}

		while (rs->MoreRows())
		{
			ISQLRow *row = rs->FetchRow();
			if (!row)
				break;

			AdminEntry entry;
			entry.fromDatabase = true;
			entry.adminId = rs->GetInt(0);

			const char *authid = rs->GetString(1);
			if (!authid || !*authid)
				continue;

			entry.identity = NormalizeSteamID(authid);
			entry.steamid64 = AuthIdToSteamID64(entry.identity.c_str());

			const char *flags = rs->GetString(2);
			entry.flags = FlagsFromString(flags);

			const char *group = rs->GetString(3);
			entry.group = group ? group : "";

			const char *name = rs->GetString(4);
			entry.name = name ? name : "";

			entry.immunity = rs->GetInt(5);

			// Inherit group flags and immunity
			if (!entry.group.empty())
			{
				auto it = m_groups.find(entry.group);
				if (it != m_groups.end())
				{
					entry.flags |= it->second.flags;
					if (it->second.immunity > entry.immunity)
						entry.immunity = it->second.immunity;
				}
			}

			if (!entry.identity.empty())
			{
				// If there's already a DB entry for this identity (e.g., multiple
				// group assignments), merge flags additively
				auto existing = m_dbAdmins.find(entry.identity);
				if (existing != m_dbAdmins.end())
				{
					existing->second.flags |= entry.flags;
					if (entry.immunity > existing->second.immunity)
						existing->second.immunity = entry.immunity;
				}
				else
				{
					m_dbAdmins[entry.identity] = entry;
				}
			}
		}

		META_CONPRINTF("[ADMIN] Loaded %zu admin(s) from database.\n", m_dbAdmins.size());
		m_dbAdminsLoaded = true;
		if (onComplete) onComplete();
	});
}

void CS2AAdminManager::LoadDatabaseAdmins(std::function<void()> onComplete)
{
	LoadGroups([this, onComplete]() {
		LoadGroupOverrides([this, onComplete]() {
			LoadGlobalOverrides([this, onComplete]() {
				LoadAdminsFromDB([this, onComplete]() {
					MergeAndApplyAll();
					if (onComplete) onComplete();
				});
			});
		});
	});
}
