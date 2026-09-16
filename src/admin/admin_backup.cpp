#include "admin_manager.h"
#include "mmu/log.h"
#include "src/common.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// BackupConfigs. The last complete DB admin load, used while the database is unreachable.
// One tab-separated record per line.
//   admin  <identity> <aid> <flags> <immunity> <name> [group...]
//   group  <name> <id> <flags> <immunity>
//   gover  <group> <override key> <allow|deny>
//   over   <override key> <flags>

static std::string BackupPath()
{
	char path[512];
	snprintf(path, sizeof(path), "%s/addons/cs2admin/admins_backup.txt", g_SMAPI->GetBaseDir());
	return path;
}

// Tabs and newlines are record syntax.
static std::string Field(const std::string &value)
{
	std::string out;
	out.reserve(value.size());
	for (char c : value)
	{
		out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
	}
	return out;
}

void CS2AAdminManager::SaveBackup() const
{
	std::string path = BackupPath();
	std::string tmpPath = path + ".tmp";

	{
		std::ofstream file(tmpPath, std::ios::trunc);
		if (!file.is_open())
		{
			MMU_LOG_WARN("Could not write admin backup to %s\n", tmpPath.c_str());
			return;
		}

		for (const auto &[name, group] : m_groups)
		{
			file << "group\t" << Field(name) << "\t" << group.id << "\t" << FlagsToString(group.flags) << "\t" << group.immunity << "\n";
			for (const auto &[key, rule] : group.overrides)
			{
				file << "gover\t" << Field(name) << "\t" << Field(key) << "\t" << (rule == Command_Allow ? "allow" : "deny") << "\n";
			}
		}

		for (const auto &[key, flags] : m_globalOverrides)
		{
			file << "over\t" << Field(key) << "\t" << FlagsToString(flags) << "\n";
		}

		for (const auto &[identity, admin] : m_dbAdmins)
		{
			file << "admin\t" << Field(identity) << "\t" << admin.adminId << "\t" << FlagsToString(admin.flags) << "\t" << admin.immunity << "\t"
				 << Field(admin.name);
			for (const std::string &group : admin.groups)
			{
				file << "\t" << Field(group);
			}
			file << "\n";
		}

		if (!file)
		{
			MMU_LOG_WARN("Could not write admin backup to %s\n", tmpPath.c_str());
			return;
		}
	}

	// Via rename, so a crash mid-write can't leave a truncated backup.
	std::remove(path.c_str());
	if (std::rename(tmpPath.c_str(), path.c_str()) != 0)
	{
		MMU_LOG_WARN("Could not replace admin backup %s\n", path.c_str());
	}
}

bool CS2AAdminManager::LoadBackup()
{
	std::ifstream file(BackupPath());
	if (!file.is_open())
	{
		return false;
	}

	int admins = 0;
	std::string line;
	while (std::getline(file, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}

		std::vector<std::string> fields;
		std::stringstream ss(line);
		std::string field;
		while (std::getline(ss, field, '\t'))
		{
			fields.push_back(field);
		}
		if (fields.empty())
		{
			continue;
		}

		const std::string &kind = fields[0];
		if (kind == "group" && fields.size() >= 5)
		{
			// Additive, like the DB load.
			AdminGroup &group = m_loadingGroups[fields[1]];
			group.name = fields[1];
			group.id = std::atoi(fields[2].c_str());
			group.flags |= FlagsFromString(fields[3].c_str());
			group.immunity = std::max(group.immunity, std::atoi(fields[4].c_str()));
			m_loadingGroupIdToName[group.id] = group.name;
		}
		else if (kind == "gover" && fields.size() >= 4)
		{
			auto it = m_loadingGroups.find(fields[1]);
			if (it != m_loadingGroups.end())
			{
				it->second.overrides[fields[2]] = (fields[3] == "allow") ? Command_Allow : Command_Deny;
			}
		}
		else if (kind == "over" && fields.size() >= 3)
		{
			m_loadingGlobalOverrides[fields[1]] = FlagsFromString(fields[2].c_str());
		}
		else if (kind == "admin" && fields.size() >= 6)
		{
			AdminEntry entry;
			entry.identity = fields[1];
			entry.steamid64 = AuthIdToSteamID64(entry.identity.c_str());
			entry.adminId = std::atoi(fields[2].c_str());
			entry.flags = FlagsFromString(fields[3].c_str());
			entry.immunity = std::atoi(fields[4].c_str());
			entry.name = fields[5];
			entry.fromDatabase = true;
			for (size_t i = 6; i < fields.size(); i++)
			{
				AddGroup(entry.groups, fields[i]);
			}
			if (!entry.identity.empty())
			{
				m_loadingDbAdmins[entry.identity] = entry;
				admins++;
			}
		}
	}

	MMU_LOG_INFO("Database unavailable, loaded %d admin(s) from the admin backup.\n", admins);
	return true;
}
