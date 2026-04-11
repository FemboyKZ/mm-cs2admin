#include "map_manager.h"
#include "../common.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

CS2AMapManager g_CS2AMapManager;

static std::string TrimString(const std::string &s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

static std::string ToLower(const std::string &s)
{
	std::string result = s;
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

bool CS2AMapManager::LoadMapList()
{
	m_maps.clear();

	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/maplist.txt",
		g_SMAPI->GetBaseDir());

	std::ifstream file(path);
	if (!file.is_open())
	{
		META_CONPRINTF("[ADMIN] Could not open maplist file: %s\n", path);
		return false;
	}

	std::string line;
	while (std::getline(file, line))
	{
		line = TrimString(line);

		// Skip empty lines and comments
		if (line.empty() || line[0] == '/' || line[0] == '#')
			continue;

		MapEntry entry;

		// Format: mapname or mapname:workshopid or displayname:workshopid
		size_t colonPos = line.rfind(':');
		if (colonPos != std::string::npos)
		{
			std::string beforeColon = TrimString(line.substr(0, colonPos));
			std::string afterColon = TrimString(line.substr(colonPos + 1));

			// Check if after colon is all digits (workshop ID)
			bool isWorkshopId = !afterColon.empty() &&
				std::all_of(afterColon.begin(), afterColon.end(), ::isdigit);

			if (isWorkshopId)
			{
				entry.workshopId = afterColon;
				entry.isWorkshop = true;
				entry.displayName = beforeColon;

				// Extract actual map name from display name
				// e.g. "surf_nyx (Tier 1, Linear)" -> "surf_nyx"
				size_t spacePos = beforeColon.find(' ');
				if (spacePos != std::string::npos)
					entry.mapName = beforeColon.substr(0, spacePos);
				else
					entry.mapName = beforeColon;
			}
			else
			{
				// No valid workshop ID, treat entire line as map name
				entry.mapName = line;
				entry.displayName = line;
				entry.isWorkshop = false;
			}
		}
		else
		{
			// Plain map name
			entry.mapName = line;
			entry.displayName = line;
			entry.isWorkshop = false;
		}

		if (!entry.mapName.empty())
			m_maps.push_back(entry);
	}

	META_CONPRINTF("[ADMIN] Loaded %d maps from maplist\n", (int)m_maps.size());
	return true;
}

const MapEntry *CS2AMapManager::FindMap(const char *input, std::string &error) const
{
	if (!input || !*input)
	{
		error = "No map specified.";
		return nullptr;
	}

	std::string search = ToLower(input);

	// First: try exact match on map name
	for (const auto &entry : m_maps)
	{
		if (ToLower(entry.mapName) == search)
			return &entry;
	}

	// Second: try exact match on workshop ID
	for (const auto &entry : m_maps)
	{
		if (entry.isWorkshop && entry.workshopId == input)
			return &entry;
	}

	// Third: partial match on map name
	std::vector<const MapEntry *> matches;
	for (const auto &entry : m_maps)
	{
		if (ToLower(entry.mapName).find(search) != std::string::npos)
			matches.push_back(&entry);
	}

	if (matches.size() == 1)
		return matches[0];

	if (matches.size() > 1)
	{
		error = "Multiple maps match '";
		error += input;
		error += "':";
		for (size_t i = 0; i < matches.size() && i < 5; i++)
		{
			error += " ";
			error += matches[i]->mapName;
		}
		if (matches.size() > 5)
			error += " ...";
		return nullptr;
	}

	error = "No map found matching '";
	error += input;
	error += "'.";
	return nullptr;
}

bool CS2AMapManager::ChangeMap(const char *input, std::string &error)
{
	if (!g_pEngine)
	{
		error = "Engine not available.";
		return false;
	}

	// Check if input is a raw workshop ID (all digits, reasonable length)
	std::string inputStr(input);
	bool isRawWorkshopId = inputStr.length() >= 6 &&
		std::all_of(inputStr.begin(), inputStr.end(), ::isdigit);

	if (isRawWorkshopId)
	{
		// Check if it's in our maplist first
		const MapEntry *entry = FindMap(input, error);
		if (entry && entry->isWorkshop)
		{
			char cmd[256];
			snprintf(cmd, sizeof(cmd), "host_workshop_map %s\n", entry->workshopId.c_str());
			g_pEngine->ServerCommand(cmd);
			return true;
		}

		// Not in maplist but is a valid workshop ID format, use directly
		error.clear();
		char cmd[256];
		snprintf(cmd, sizeof(cmd), "host_workshop_map %s\n", input);
		g_pEngine->ServerCommand(cmd);
		return true;
	}

	// Try to find in maplist
	const MapEntry *entry = FindMap(input, error);
	if (!entry)
		return false;

	if (entry->isWorkshop)
	{
		char cmd[256];
		snprintf(cmd, sizeof(cmd), "host_workshop_map %s\n", entry->workshopId.c_str());
		g_pEngine->ServerCommand(cmd);
	}
	else
	{
		g_pEngine->ChangeLevel(entry->mapName.c_str(), nullptr);
	}

	return true;
}
