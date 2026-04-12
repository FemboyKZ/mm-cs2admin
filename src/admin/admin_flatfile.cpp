#include "admin_manager.h"
#include "../common.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>

// Parse a quoted string from a line at position pos.
// Returns the unquoted content, or "" if no quoted string found.
// Advances pos past the closing quote.
static std::string ParseQuotedToken(const std::string &line, size_t &pos)
{
	while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
		pos++;
	if (pos >= line.size() || line[pos] != '"')
		return "";
	pos++; // skip opening quote
	size_t start = pos;
	while (pos < line.size() && line[pos] != '"')
		pos++;
	std::string val = line.substr(start, pos - start);
	if (pos < line.size())
		pos++; // skip closing quote
	return val;
}

// Simple KeyValues-like parser for admins.cfg
// Format:
// "Admins"
// {
//     "AdminName"
//     {
//         "identity"    "STEAM_0:1:12345"
//         "flags"       "abcde"
//         "immunity"    "100"
//         "group"       "Full Admin"
//     }
// }
void CS2AAdminManager::LoadFlatFileAdmins()
{
	m_flatFileAdmins.clear();

	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/cs2admin/admins.cfg",
		g_SMAPI->GetBaseDir());

	std::ifstream file(path);
	if (!file.is_open())
	{
		META_CONPRINTF("[ADMIN] No flat-file admins found at %s (this is normal if you only use DB admins).\n", path);
		return;
	}

	std::string line;
	int depth = 0;
	std::string currentAdminName;
	AdminEntry currentEntry;
	bool inAdmin = false;

	while (std::getline(file, line))
	{
		// Strip leading/trailing whitespace
		size_t start = line.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			continue;
		line = line.substr(start);

		// Skip comments
		if (line.empty() || line[0] == '/' || line[0] == '#')
			continue;

		if (line[0] == '{')
		{
			depth++;
			continue;
		}

		if (line[0] == '}')
		{
			depth--;
			if (depth == 1 && inAdmin)
			{
				// Finished parsing one admin block
				if (!currentEntry.identity.empty())
				{
					std::string normalized = NormalizeSteamID(currentEntry.identity.c_str());
					if (!normalized.empty())
					{
						currentEntry.identity = normalized;
						currentEntry.steamid64 = AuthIdToSteamID64(normalized.c_str());
						currentEntry.name = currentAdminName;
						currentEntry.fromDatabase = false;
						m_flatFileAdmins[normalized] = currentEntry;
					}
				}
				inAdmin = false;
				currentEntry = {};
			}
			continue;
		}

		// Parse keyvalue pairs: "key" "value"
		if (depth == 1)
		{
			// Admin name line, just a quoted string before a {
			// Strip quotes
			std::string stripped = line;
			if (stripped.front() == '"')
			{
				size_t end = stripped.find('"', 1);
				if (end != std::string::npos)
				{
					currentAdminName = stripped.substr(1, end - 1);
					inAdmin = true;
					currentEntry = {};
				}
			}
		}
		else if (depth == 2 && inAdmin)
		{
			// Parse key "value" pairs
			size_t pos = 0;
			std::string key = ParseQuotedToken(line, pos);
			std::string value = ParseQuotedToken(line, pos);

			if (key.empty())
				continue;

			// Lowercase key for comparison
			std::string keyLower = key;
			std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			if (keyLower == "identity" || keyLower == "auth" || keyLower == "steam")
				currentEntry.identity = value;
			else if (keyLower == "flags")
				currentEntry.flags = FlagsFromString(value.c_str());
			else if (keyLower == "immunity")
				currentEntry.immunity = std::atoi(value.c_str());
			else if (keyLower == "group")
				currentEntry.group = value;
			else if (keyLower == "password")
				currentEntry.password = value;
		}
	}

	META_CONPRINTF("[ADMIN] Loaded %zu admin(s) from flat file.\n", m_flatFileAdmins.size());
}

// Load SM-compatible admins_simple.ini
// Format (one admin per line):
//   "STEAM_0:1:12345" "abcde" "99"          // flags + immunity
//   "STEAM_0:1:12345" "abcde"               // flags only
//   "STEAM_0:1:12345" "99:group"            // immunity:group (inherit flags from group)
void CS2AAdminManager::LoadSimpleAdmins()
{
	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/cs2admin/admins_simple.ini",
		g_SMAPI->GetBaseDir());

	std::ifstream file(path);
	if (!file.is_open())
		return;

	std::string line;
	int count = 0;

	while (std::getline(file, line))
	{
		// Strip leading whitespace
		size_t start = line.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			continue;
		line = line.substr(start);

		// Skip comments and empty lines
		if (line.empty() || line[0] == '/' || line[0] == ';' || line[0] == '#')
			continue;

		// Parse quoted tokens
		std::vector<std::string> tokens;
		size_t pos = 0;
		while (pos < line.size() && tokens.size() < 4)
		{
			// Skip whitespace
			while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
				pos++;
			if (pos >= line.size())
				break;

			if (line[pos] == '"')
			{
				pos++; // skip opening quote
				size_t qstart = pos;
				while (pos < line.size() && line[pos] != '"')
					pos++;
				tokens.push_back(line.substr(qstart, pos - qstart));
				if (pos < line.size())
					pos++; // skip closing quote
			}
			else
			{
				// Unquoted token
				size_t tstart = pos;
				while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t')
					pos++;
				tokens.push_back(line.substr(tstart, pos - tstart));
			}
		}

		if (tokens.empty())
			continue;

		// Token 0: identity (SteamID)
		std::string identity = tokens[0];
		std::string normalized = NormalizeSteamID(identity.c_str());
		if (normalized.empty())
			continue;

		AdminEntry entry;
		entry.identity = normalized;
		entry.steamid64 = AuthIdToSteamID64(normalized.c_str());
		entry.fromDatabase = false;

		if (tokens.size() >= 2)
		{
			const std::string &flagToken = tokens[1];

			// Check for "@GroupName" format (pure group reference)
			if (!flagToken.empty() && flagToken[0] == '@')
			{
				entry.group = flagToken.substr(1);
			}
			// Check if it's "immunity:flags" or "immunity:@group" format
			else
			{
				size_t colonPos = flagToken.find(':');
				if (colonPos != std::string::npos)
				{
					// Everything before colon could be immunity or flags
					std::string before = flagToken.substr(0, colonPos);
					std::string after = flagToken.substr(colonPos + 1);

					// If 'before' is all digits, it's immunity; otherwise flags
					bool allDigits = !before.empty();
					for (char c : before)
					{
						if (!std::isdigit(static_cast<unsigned char>(c)))
						{
							allDigits = false;
							break;
						}
					}

					if (allDigits)
					{
						entry.immunity = std::atoi(before.c_str());
					}
					else
					{
						entry.flags = FlagsFromString(before.c_str());
					}

					// After colon: '@' prefix means group, otherwise it's flags
					if (!after.empty() && after[0] == '@')
					{
						entry.group = after.substr(1);
					}
					else if (!after.empty())
					{
						entry.flags |= FlagsFromString(after.c_str());
					}
				}
				else
				{
					entry.flags = FlagsFromString(flagToken.c_str());
				}
			}
		}

		// Token 2: immunity (if present and not already set via group format)
		if (tokens.size() >= 3 && entry.group.empty())
		{
			entry.immunity = std::atoi(tokens[2].c_str());
		}

		// Token 3: password (if present)
		if (tokens.size() >= 4)
		{
			entry.password = tokens[3];
		}

		// Merge with existing flat-file entry (additive)
		auto existing = m_flatFileAdmins.find(normalized);
		if (existing != m_flatFileAdmins.end())
		{
			existing->second.flags |= entry.flags;
			if (entry.immunity > existing->second.immunity)
				existing->second.immunity = entry.immunity;
			if (existing->second.group.empty() && !entry.group.empty())
				existing->second.group = entry.group;
		}
		else
		{
			m_flatFileAdmins[normalized] = entry;
		}

		count++;
	}

	if (count > 0)
		META_CONPRINTF("[ADMIN] Loaded %d admin(s) from admins_simple.ini.\n", count);
}

// Load SM-compatible admin_groups.cfg (KeyValues format)
// Format:
// "Groups"
// {
//     "Full Admins"
//     {
//         "flags"       "abcde"
//         "immunity"    "99"
//
//         "Overrides"
//         {
//             "sm_ban"    "allow"
//             "sm_kick"   "deny"
//         }
//     }
// }
void CS2AAdminManager::LoadFlatFileGroups()
{
	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/cs2admin/admin_groups.cfg",
		g_SMAPI->GetBaseDir());

	std::ifstream file(path);
	if (!file.is_open())
		return;

	std::string line;
	int depth = 0;
	std::string currentGroupName;
	AdminGroup currentGroup;
	bool inGroup = false;
	bool inOverrides = false;

	while (std::getline(file, line))
	{
		size_t start = line.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			continue;
		line = line.substr(start);

		if (line.empty() || line[0] == '/' || line[0] == '#')
			continue;

		if (line[0] == '{')
		{
			depth++;
			continue;
		}

		if (line[0] == '}')
		{
			depth--;
			if (depth == 2 && inOverrides)
			{
				// Closing the Overrides sub block
				inOverrides = false;
			}
			else if (depth == 1 && inGroup)
			{
				// Finished parsing one group block
				if (!currentGroupName.empty())
				{
					// Merge with existing group (DB groups take priority, but flat file
					// can add groups that don't exist in DB)
					auto it = m_groups.find(currentGroupName);
					if (it == m_groups.end())
					{
						currentGroup.name = currentGroupName;
						m_groups[currentGroupName] = currentGroup;
					}
					else
					{
						// Merge: flat-file flags additive, immunity takes max
						it->second.flags |= currentGroup.flags;
						if (currentGroup.immunity > it->second.immunity)
							it->second.immunity = currentGroup.immunity;
						// Merge overrides (flat-file overrides don't clobber DB ones)
						for (auto &ov : currentGroup.overrides)
						{
							if (it->second.overrides.find(ov.first) == it->second.overrides.end())
								it->second.overrides[ov.first] = ov.second;
						}
					}
				}
				inGroup = false;
				currentGroup = {};
			}
			continue;
		}

		// Parse keyvalue pairs
		size_t pos = 0;

		if (depth == 1)
		{
			// Group name
			std::string name = ParseQuotedToken(line, pos);
			if (!name.empty())
			{
				currentGroupName = name;
				inGroup = true;
				currentGroup = {};
			}
		}
		else if (depth == 2 && inGroup && !inOverrides)
		{
			std::string key = ParseQuotedToken(line, pos);
			if (key.empty())
				continue;

			// Check if this is the "Overrides" sub section header
			std::string keyLower = key;
			std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			if (keyLower == "overrides")
			{
				inOverrides = true;
				// The next '{' will bump depth to 3
				continue;
			}

			std::string value = ParseQuotedToken(line, pos);

			if (keyLower == "flags")
				currentGroup.flags = FlagsFromString(value.c_str());
			else if (keyLower == "immunity")
				currentGroup.immunity = std::atoi(value.c_str());
		}
		else if (depth == 3 && inGroup && inOverrides)
		{
			// Override entries: "command_name" "allow/deny"
			std::string cmdName = ParseQuotedToken(line, pos);
			std::string access = ParseQuotedToken(line, pos);

			if (cmdName.empty() || access.empty())
				continue;

			std::string accessLower = access;
			std::transform(accessLower.begin(), accessLower.end(), accessLower.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			OverrideRule rule = (accessLower == "allow") ? Command_Allow : Command_Deny;

			// Determine type: if name starts with '@', it's a command group
			if (cmdName[0] == '@')
			{
				std::string key = "grp:" + cmdName.substr(1);
				currentGroup.overrides[key] = rule;
			}
			else
			{
				std::string key = "cmd:" + cmdName;
				currentGroup.overrides[key] = rule;
			}
		}
	}

	if (!m_groups.empty())
		META_CONPRINTF("[ADMIN] Loaded %zu group(s) from admin_groups.cfg.\n", m_groups.size());
}

// Load SM-compatible admin_overrides.cfg (KeyValues format)
// Format:
// "Overrides"
// {
//     "sm_ban"
//     {
//         "type"    "command"
//         "flag"    "d"
//     }
//
//     // Or simple form: "command_name" "flag_string"
//     "sm_kick"    "c"
// }
void CS2AAdminManager::LoadFlatFileOverrides()
{
	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/cs2admin/admin_overrides.cfg",
		g_SMAPI->GetBaseDir());

	std::ifstream file(path);
	if (!file.is_open())
		return;

	std::string line;
	int depth = 0;
	std::string currentName;
	std::string currentType;
	std::string currentFlag;
	bool inOverride = false;

	int count = 0;

	while (std::getline(file, line))
	{
		size_t start = line.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			continue;
		line = line.substr(start);

		if (line.empty() || line[0] == '/' || line[0] == '#')
			continue;

		if (line[0] == '{')
		{
			depth++;
			continue;
		}

		if (line[0] == '}')
		{
			depth--;
			if (depth == 1 && inOverride)
			{
				// Finished parsing a block style override entry
				if (!currentName.empty() && !currentFlag.empty())
				{
					std::string typeLower = currentType;
					std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

					std::string key;
					if (typeLower == "group")
						key = "grp:" + currentName;
					else
						key = "cmd:" + currentName;

					// Only add if not already set by DB
					if (m_globalOverrides.find(key) == m_globalOverrides.end())
					{
						m_globalOverrides[key] = FlagsFromString(currentFlag.c_str());
						count++;
					}
				}
				inOverride = false;
				currentName.clear();
				currentType = "command";
				currentFlag.clear();
			}
			continue;
		}

		size_t pos = 0;

		if (depth == 1)
		{
			std::string name = ParseQuotedToken(line, pos);
			if (name.empty())
				continue;

			// Check if there's a value on the same line (simple form)
			std::string value = ParseQuotedToken(line, pos);
			if (!value.empty())
			{
				// Simple form: "command_name" "flag"
				// Determine type by @ prefix
				std::string key;
				if (name[0] == '@')
					key = "grp:" + name.substr(1);
				else
					key = "cmd:" + name;

				if (m_globalOverrides.find(key) == m_globalOverrides.end())
				{
					m_globalOverrides[key] = FlagsFromString(value.c_str());
					count++;
				}
			}
			else
			{
				// Block form. name only, expect { on next line
				currentName = name;
				currentType = "command";
				currentFlag.clear();
				inOverride = true;
			}
		}
		else if (depth == 2 && inOverride)
		{
			std::string key = ParseQuotedToken(line, pos);
			std::string value = ParseQuotedToken(line, pos);

			if (key.empty())
				continue;

			std::string keyLower = key;
			std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			if (keyLower == "type")
				currentType = value;
			else if (keyLower == "flag" || keyLower == "flags")
				currentFlag = value;
		}
	}

	if (count > 0)
		META_CONPRINTF("[ADMIN] Loaded %d global override(s) from admin_overrides.cfg.\n", count);
}
