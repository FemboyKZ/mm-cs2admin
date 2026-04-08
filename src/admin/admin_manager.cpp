#include "admin_manager.h"
#include "../common.h"
#include "../config/config.h"
#include "../db/database.h"
#include "../player/player_manager.h"

#include <cstring>
#include <cstdio>

CS2AAdminManager g_CS2AAdminManager;

uint32_t CS2AAdminManager::FlagsFromString(const char *flagStr)
{
	if (!flagStr)
		return ADMFLAG_NONE;

	uint32_t flags = 0;
	for (const char *p = flagStr; *p; ++p)
	{
		char c = *p;
		if (c >= 'a' && c <= 't')
			flags |= (1u << (c - 'a'));
		else if (c == 'z')
			flags |= ADMFLAG_ROOT;
	}
	return flags;
}

std::string CS2AAdminManager::FlagsToString(uint32_t flags)
{
	std::string result;
	for (int i = 0; i < 20; i++) // a-t
	{
		if (flags & (1u << i))
			result += static_cast<char>('a' + i);
	}
	if (flags & ADMFLAG_ROOT)
		result += 'z';
	return result;
}

bool CS2AAdminManager::HasFlag(uint32_t playerFlags, uint32_t requiredFlag)
{
	if (playerFlags & ADMFLAG_ROOT)
		return true;
	return (playerFlags & requiredFlag) != 0;
}

uint64_t CS2AAdminManager::AuthIdToSteamID64(const char *authid)
{
	// Parse STEAM_X:Y:Z format
	if (!authid)
		return 0;

	unsigned int x, y, z;
	if (sscanf(authid, "STEAM_%u:%u:%u", &x, &y, &z) != 3)
		return 0;

	// AccountID = Z * 2 + Y
	uint32_t accountId = z * 2 + y;
	// SteamID64 = AccountID + 76561197960265728 (the base for individual accounts)
	return static_cast<uint64_t>(accountId) + 76561197960265728ULL;
}

std::string CS2AAdminManager::NormalizeSteamID(const char *input)
{
	if (!input || !*input)
		return "";

	// If it's STEAM_X:Y:Z, normalize to STEAM_0:Y:Z
	unsigned int x, y, z;
	if (sscanf(input, "STEAM_%u:%u:%u", &x, &y, &z) == 3)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "STEAM_0:%u:%u", y, z);
		return buf;
	}

	// If it's a raw SteamID64 number
	if (strlen(input) >= 15)
	{
		char *end;
		uint64_t id64 = strtoull(input, &end, 10);
		if (*end == '\0' && id64 > 0)
			return SteamID64ToAuthId(id64);
	}

	return input;
}

// Flat file loading is in admin_flatfile.cpp
// Database loading is in admin_db.cpp

void CS2AAdminManager::ReloadAdmins()
{
	// Clear per-player admin state
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		m_playerHasAdmin[i] = false;
		m_playerAdmins[i] = {};
	}

	// Load flat file groups and overrides first (synchronous, needed for group resolution)
	LoadFlatFileGroups();
	LoadFlatFileOverrides();

	// Load flat file admins (synchronous)
	LoadFlatFileAdmins();
	LoadSimpleAdmins();

	// Then load from DB (async)
	if (g_CS2AConfig.enableAdmins && g_CS2ADatabase.IsConnected())
	{
		LoadDatabaseAdmins([]() {
			META_CONPRINTF("[ADMIN] Admin reload complete.\n");
		});
	}
	else
	{
		// No DB, just apply flat file admins to connected players
		MergeAndApplyAll();
		META_CONPRINTF("[ADMIN] Admin reload complete (flat file only).\n");
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
			AssignAdminToPlayer(i);
	}
}

void CS2AAdminManager::AssignAdminToPlayer(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
		return;

	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (!player || !player->connected)
		return;

	std::string normalized = SteamID64ToAuthId(player->steamid64);
	if (normalized.empty())
		return;

	AdminEntry merged;
	merged.identity = normalized;
	merged.steamid64 = player->steamid64;
	merged.name = player->name;
	bool found = false;

	// Check flatfile admins
	auto flatIt = m_flatFileAdmins.find(normalized);
	if (flatIt != m_flatFileAdmins.end())
	{
		merged.flags |= flatIt->second.flags;
		if (flatIt->second.immunity > merged.immunity)
			merged.immunity = flatIt->second.immunity;
		if (!flatIt->second.group.empty())
		{
			merged.group = flatIt->second.group;
			// Resolve group flags
			auto grpIt = m_groups.find(flatIt->second.group);
			if (grpIt != m_groups.end())
			{
				merged.flags |= grpIt->second.flags;
				if (grpIt->second.immunity > merged.immunity)
					merged.immunity = grpIt->second.immunity;
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
			merged.immunity = dbIt->second.immunity;
		if (merged.group.empty() && !dbIt->second.group.empty())
			merged.group = dbIt->second.group;
		merged.adminId = dbIt->second.adminId;
		found = true;
	}

	m_playerHasAdmin[slot] = found;
	m_playerAdmins[slot] = merged;

	if (found)
	{
		META_CONPRINTF("[ADMIN] Admin assigned: \"%s\" (%s) flags=%s immunity=%d\n",
			player->name.c_str(), normalized.c_str(),
			FlagsToString(merged.flags).c_str(), merged.immunity);
	}
}

bool CS2AAdminManager::PlayerHasFlag(int slot, uint32_t flag)
{
	if (slot < 0 || slot > MAXPLAYERS)
		return false;

	if (!m_playerHasAdmin[slot])
		return false;

	return HasFlag(m_playerAdmins[slot].flags, flag);
}

bool CS2AAdminManager::CanPlayerUseCommand(int slot, const char *commandName,
	const char *commandGroup, uint32_t defaultFlag)
{
	if (slot < 0 || slot > MAXPLAYERS)
		return false;

	if (!m_playerHasAdmin[slot])
		return false;

	const AdminEntry &admin = m_playerAdmins[slot];

	// Root flag always passes
	if (admin.flags & ADMFLAG_ROOT)
		return true;

	// Step 1: Check per group overrides (sb_srvgroups_overrides)
	if (!admin.group.empty())
	{
		auto grpIt = m_groups.find(admin.group);
		if (grpIt != m_groups.end())
		{
			const auto &overrides = grpIt->second.overrides;

			// Check command level override first (more specific)
			if (commandName && *commandName)
			{
				std::string cmdKey = "cmd:" + std::string(commandName);
				auto ovIt = overrides.find(cmdKey);
				if (ovIt != overrides.end())
					return ovIt->second == Command_Allow;
			}

			// Check command group override
			if (commandGroup && *commandGroup)
			{
				std::string grpKey = "grp:" + std::string(commandGroup);
				auto ovIt = overrides.find(grpKey);
				if (ovIt != overrides.end())
					return ovIt->second == Command_Allow;
			}
		}
	}

	// Step 2: Check global overrides (sb_overrides)
	if (commandName && *commandName)
	{
		std::string cmdKey = "cmd:" + std::string(commandName);
		auto ovIt = m_globalOverrides.find(cmdKey);
		if (ovIt != m_globalOverrides.end())
			return HasFlag(admin.flags, ovIt->second);
	}

	if (commandGroup && *commandGroup)
	{
		std::string grpKey = "grp:" + std::string(commandGroup);
		auto ovIt = m_globalOverrides.find(grpKey);
		if (ovIt != m_globalOverrides.end())
			return HasFlag(admin.flags, ovIt->second);
	}

	// Step 3: Fall back to default flag check
	return HasFlag(admin.flags, defaultFlag);
}

const AdminEntry *CS2AAdminManager::GetPlayerAdmin(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
		return nullptr;

	if (!m_playerHasAdmin[slot])
		return nullptr;

	return &m_playerAdmins[slot];
}

const AdminGroup *CS2AAdminManager::GetGroup(const char *name) const
{
	if (!name || !*name)
		return nullptr;

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
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		m_playerHasAdmin[i] = false;
		m_playerAdmins[i] = {};
	}
	m_dbGroupsLoaded = false;
	m_dbAdminsLoaded = false;
}
