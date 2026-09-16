#include "admin_manager.h"
#include "mmu/log.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/db/database.h"
#include "src/player/player_manager.h"
#include "src/tags/tag_manager.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>

CS2AAdminManager g_CS2AAdminManager;

uint32_t CS2AAdminManager::FlagsFromString(const char *flagStr)
{
	if (!flagStr)
	{
		return ADMFLAG_NONE;
	}

	uint32_t flags = 0;
	for (const char *p = flagStr; *p; ++p)
	{
		char c = *p;
		if (c >= 'a' && c <= 't')
		{
			flags |= (1u << (c - 'a'));
		}
		else if (c == 'z')
		{
			flags |= ADMFLAG_ROOT;
		}
	}
	return flags;
}

std::string CS2AAdminManager::FlagsToString(uint32_t flags)
{
	std::string result;
	for (int i = 0; i < 20; i++) // a-t
	{
		if (flags & (1u << i))
		{
			result += static_cast<char>('a' + i);
		}
	}
	if (flags & ADMFLAG_ROOT)
	{
		result += 'z';
	}
	return result;
}

bool CS2AAdminManager::HasFlag(uint32_t playerFlags, uint32_t requiredFlag)
{
	if (playerFlags & ADMFLAG_ROOT)
	{
		return true;
	}
	return (playerFlags & requiredFlag) != 0;
}

static std::string ToLowerCopy(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

std::string CS2AAdminManager::CommandOverrideKey(const std::string &name)
{
	std::string lower = ToLowerCopy(name);
	if (lower.size() > 3 && (lower.compare(0, 3, "sm_") == 0 || lower.compare(0, 3, "mm_") == 0))
	{
		lower.erase(0, 3);
	}
	return "cmd:" + lower;
}

std::string CS2AAdminManager::GroupOverrideKey(const std::string &name)
{
	return "grp:" + ToLowerCopy(name);
}

void CS2AAdminManager::AddGroup(std::vector<std::string> &groups, const std::string &name)
{
	if (!name.empty() && std::find(groups.begin(), groups.end(), name) == groups.end())
	{
		groups.push_back(name);
	}
}

uint64_t CS2AAdminManager::AuthIdToSteamID64(const char *authid)
{
	// Parse STEAM_X:Y:Z format
	if (!authid)
	{
		return 0;
	}

	unsigned int x, y, z;
	if (sscanf(authid, "STEAM_%u:%u:%u", &x, &y, &z) != 3)
	{
		return 0;
	}

	// AccountID = Z * 2 + Y
	uint32_t accountId = z * 2 + y;
	// SteamID64 = AccountID + 76561197960265728 (the base for individual accounts)
	return static_cast<uint64_t>(accountId) + 76561197960265728ULL;
}

std::string CS2AAdminManager::NormalizeSteamID(const char *input)
{
	if (!input || !*input)
	{
		return "";
	}

	// If it's STEAM_X:Y:Z, normalize to STEAM_0:Y:Z
	unsigned int x, y, z;
	if (sscanf(input, "STEAM_%u:%u:%u", &x, &y, &z) == 3)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "STEAM_0:%u:%u", y, z);
		return buf;
	}

	// If it's SteamID3 format [U:1:AccountID]
	unsigned int universe, accountId;
	if (sscanf(input, "[U:%u:%u]", &universe, &accountId) == 2)
	{
		unsigned int authY = accountId & 1;
		unsigned int authZ = accountId >> 1;
		char buf[64];
		snprintf(buf, sizeof(buf), "STEAM_0:%u:%u", authY, authZ);
		return buf;
	}

	// If it's a raw SteamID64 number
	if (strlen(input) >= 15)
	{
		char *end;
		uint64_t id64 = strtoull(input, &end, 10);
		if (*end == '\0' && id64 > 0)
		{
			return SteamID64ToAuthId(id64);
		}
	}

	return input;
}

// Flat file loading is in admin_flatfile.cpp
// Database loading is in admin_db.cpp

void CS2AAdminManager::ReloadAdmins()
{
	// Startup fires this from both the DB connect callback and the first level init,
	// and a map change can land on top of an mm_reload. Run one chain at a time.
	double now = Plat_FloatTime();
	if (m_reloadInFlight)
	{
		if (now - m_reloadStartTime < RELOAD_TIMEOUT)
		{
			m_reloadPending = true;
			return;
		}
		// A query that errors never calls its callback, so the chain died without releasing the lock.
		MMU_LOG_WARN("Admin reload stuck for %.0f seconds, abandoning it and starting over.\n", now - m_reloadStartTime);
		m_reloadGeneration++;
	}
	m_reloadInFlight = true;
	m_reloadStartTime = now;
	uint32_t generation = m_reloadGeneration;

	// Per-player admin state is left alone until the commit below, so admins keep their rights for the duration.

	m_loadingDbAdmins.clear();
	m_loadingGroups.clear();
	m_loadingGroupIdToName.clear();
	m_loadingGlobalOverrides.clear();

	// Load flat file groups and overrides first (synchronous, needed for group resolution)
	LoadFlatFileGroups();
	LoadFlatFileOverrides();

	// Load flat file admins (synchronous)
	LoadFlatFileAdmins();
	LoadSimpleAdmins();

	// Then load from DB (async)
	if (g_CS2AConfig.enableAdmins && g_CS2ADatabase.IsConnected())
	{
		LoadDatabaseAdmins(generation,
						   [this]()
						   {
							   MMU_LOG_INFO("Admin reload complete.\n");
							   FinishReload();
						   });
	}
	else
	{
		// No DB, just apply flat file admins to connected players
		CommitLoadedData();
		MergeAndApplyAll();
		g_CS2ATagManager.UpdateAllClanTags();
		MMU_LOG_INFO("Admin reload complete (flat file only).\n");
		FinishReload();
	}
}

void CS2AAdminManager::CommitLoadedData()
{
	m_dbAdmins = std::move(m_loadingDbAdmins);
	m_groups = std::move(m_loadingGroups);
	m_groupIdToName = std::move(m_loadingGroupIdToName);
	m_globalOverrides = std::move(m_loadingGlobalOverrides);

	m_loadingDbAdmins.clear();
	m_loadingGroups.clear();
	m_loadingGroupIdToName.clear();
	m_loadingGlobalOverrides.clear();
}

void CS2AAdminManager::FinishReload()
{
	m_reloadInFlight = false;

	if (m_reloadPending)
	{
		m_reloadPending = false;
		ReloadAdmins();
	}
}

// Merging and assignment

void CS2AAdminManager::MergeAndApplyAll()
{
	// Apply to all currently connected players
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
		if (p && p->connected)
		{
			AssignAdminToPlayer(i);
		}
	}
}

bool CS2AAdminManager::LookupAdmin(const char *authid, AdminEntry &merged) const
{
	std::string normalized = NormalizeSteamID(authid);
	if (normalized.empty())
	{
		return false;
	}

	merged = AdminEntry {};
	merged.identity = normalized;
	merged.steamid64 = AuthIdToSteamID64(normalized.c_str());
	bool found = false;

	// Check flatfile admins
	auto flatIt = m_flatFileAdmins.find(normalized);
	if (flatIt != m_flatFileAdmins.end())
	{
		merged.flags |= flatIt->second.flags;
		if (flatIt->second.immunity > merged.immunity)
		{
			merged.immunity = flatIt->second.immunity;
		}
		for (const std::string &groupName : flatIt->second.groups)
		{
			AddGroup(merged.groups, groupName);
			// Resolve group flags
			auto grpIt = m_groups.find(groupName);
			if (grpIt != m_groups.end())
			{
				merged.flags |= grpIt->second.flags;
				if (grpIt->second.immunity > merged.immunity)
				{
					merged.immunity = grpIt->second.immunity;
				}
			}
			else
			{
				MMU_LOG_INFO("Warning: admin \"%s\" references group \"%s\" which does not exist in admin_groups.cfg.\n", normalized.c_str(),
							 groupName.c_str());
			}
		}
		found = true;
	}

	// Check DB admins (additive merge)
	auto dbIt = m_dbAdmins.find(normalized);
	if (dbIt != m_dbAdmins.end())
	{
		merged.flags |= dbIt->second.flags;
		if (dbIt->second.immunity > merged.immunity)
		{
			merged.immunity = dbIt->second.immunity;
		}
		// DB flags already include the DB groups' flags.
		for (const std::string &groupName : dbIt->second.groups)
		{
			AddGroup(merged.groups, groupName);
		}
		merged.adminId = dbIt->second.adminId;
		found = true;
	}

	return found;
}

void CS2AAdminManager::AssignAdminToPlayer(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}

	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (!player || !player->connected)
	{
		return;
	}

	std::string normalized = SteamID64ToAuthId(player->steamid64);
	if (normalized.empty())
	{
		return;
	}

	AdminEntry merged;
	bool found = LookupAdmin(normalized.c_str(), merged);
	merged.name = player->name;

	m_playerHasAdmin[slot] = found;
	m_playerAdmins[slot] = merged;

	if (found)
	{
		MMU_LOG_INFO("Admin assigned: \"%s\" (%s) flags=%s immunity=%d\n", player->name.c_str(), normalized.c_str(),
					 FlagsToString(merged.flags).c_str(), merged.immunity);
	}
}

void CS2AAdminManager::ClearPlayerAdmin(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	m_playerHasAdmin[slot] = false;
	m_playerAdmins[slot] = {};
}

bool CS2AAdminManager::PlayerHasFlag(int slot, uint32_t flag)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}

	if (!m_playerHasAdmin[slot])
	{
		return false;
	}

	return HasFlag(m_playerAdmins[slot].flags, flag);
}

bool CS2AAdminManager::CanPlayerUseCommand(int slot, const char *commandName, const char *commandGroup, uint32_t defaultFlag)
{
	// Server console always has full access
	if (slot < 0)
	{
		return true;
	}

	if (slot > MAXPLAYERS)
	{
		return false;
	}

	// A required flag of 0 means the command is open to everyone.
	auto flagPasses = [](uint32_t flags, uint32_t req) -> bool { return req == 0 ? true : CS2AAdminManager::HasFlag(flags, req); };

	// Non-admins have no flags, but global overrides and open commands (defaultFlag 0) can still apply.
	const AdminEntry &admin = m_playerAdmins[slot];
	uint32_t playerFlags = m_playerHasAdmin[slot] ? admin.flags : 0u;

	// Root flag always passes
	if (playerFlags & ADMFLAG_ROOT)
	{
		return true;
	}

	// Step 1: Check per group overrides (sb_srvgroups_overrides)
	// First group with a matching override decides, as in SourceMod.
	const std::string cmdKey = (commandName && *commandName) ? CommandOverrideKey(commandName) : std::string();
	const std::string grpKey = (commandGroup && *commandGroup) ? GroupOverrideKey(commandGroup) : std::string();
	if (m_playerHasAdmin[slot])
	{
		for (const std::string &groupName : admin.groups)
		{
			auto grpIt = m_groups.find(groupName);
			if (grpIt == m_groups.end())
			{
				continue;
			}
			const auto &overrides = grpIt->second.overrides;

			// Check command level override first (more specific)
			if (!cmdKey.empty())
			{
				auto ovIt = overrides.find(cmdKey);
				if (ovIt != overrides.end())
				{
					return ovIt->second == Command_Allow;
				}
			}

			// Check command group override
			if (!grpKey.empty())
			{
				auto ovIt = overrides.find(grpKey);
				if (ovIt != overrides.end())
				{
					return ovIt->second == Command_Allow;
				}
			}
		}
	}

	// Step 2: Check global overrides (sb_overrides)
	if (!cmdKey.empty())
	{
		auto ovIt = m_globalOverrides.find(cmdKey);
		if (ovIt != m_globalOverrides.end())
		{
			return flagPasses(playerFlags, ovIt->second);
		}
	}

	if (!grpKey.empty())
	{
		auto ovIt = m_globalOverrides.find(grpKey);
		if (ovIt != m_globalOverrides.end())
		{
			return flagPasses(playerFlags, ovIt->second);
		}
	}

	// Step 3: Fall back to default flag check. defaultFlag 0 = open to all.
	return flagPasses(playerFlags, defaultFlag);
}

const AdminEntry *CS2AAdminManager::GetPlayerAdmin(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return nullptr;
	}

	if (!m_playerHasAdmin[slot])
	{
		return nullptr;
	}

	return &m_playerAdmins[slot];
}

const AdminGroup *CS2AAdminManager::GetGroup(const char *name) const
{
	if (!name || !*name)
	{
		return nullptr;
	}

	auto it = m_groups.find(name);
	return it != m_groups.end() ? &it->second : nullptr;
}

void CS2AAdminManager::Clear()
{
	m_flatFileAdmins.clear();
	m_dbAdmins.clear();
	m_groups.clear();
	m_groupIdToName.clear();
	m_globalOverrides.clear();
	m_loadingDbAdmins.clear();
	m_loadingGroups.clear();
	m_loadingGroupIdToName.clear();
	m_loadingGlobalOverrides.clear();
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		m_playerHasAdmin[i] = false;
		m_playerAdmins[i] = {};
	}
}
