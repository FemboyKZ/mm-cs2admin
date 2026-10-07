#include "map_manager.h"
#include "utils/str.h"
#include "utils/log.h"
#include "utils/maplist.h"
#include "src/common.h"
#include "game/workshop.h"
#include "interfaces/cs2rockthevote/ics2rtv.h"
#include "src/config/config.h"
#include "src/utils/print_utils.h"

extern CSteamGameServerAPIContext g_AdminSteamAPI;

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

CS2AMapManager g_CS2AMapManager;

static const char *MapLabel(const MapEntry &entry)
{
	return entry.displayName.empty() ? entry.mapName.c_str() : entry.displayName.c_str();
}

static void IssueWorkshopChange(const std::string &workshopId)
{
	mmu::EnsureWorkshopMapReady(workshopId, g_AdminSteamAPI);
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "host_workshop_map %s\n", workshopId.c_str());
	g_pEngine->ServerCommand(cmd);
}

// Scan <gamedir>/maps for *.vpk map files.
// These are not necessarily in the maplist but can still be changed to
// via ChangeLevel, so we keep them for the same partial-name matching.
void CS2AMapManager::ScanLocalMaps()
{
	m_localMaps.clear();

	char path[512];
	snprintf(path, sizeof(path), "%s/maps", g_SMAPI->GetBaseDir());

	std::error_code ec;
	fs::directory_iterator it(fs::path(path), ec);
	if (ec)
	{
		MMU_LOG_WARN("Could not scan maps folder: %s\n", path);
		return;
	}

	for (const auto &entry : it)
	{
		std::error_code ec2;
		if (!entry.is_regular_file(ec2))
		{
			continue;
		}

		const fs::path &p = entry.path();
		if (!p.has_extension() || p.extension() != ".vpk")
		{
			continue;
		}

		std::string name = p.stem().string();

		// Skip non-playable vpks
		if (name.find("_vanity") != std::string::npos || name.find("workshop_preview") != std::string::npos || name == "graphics_settings"
			|| name == "lobby_mapveto")
		{
			continue;
		}

		m_localMaps.push_back(name);
	}

	MMU_LOG_INFO("Found %d local map(s) in maps folder\n", (int)m_localMaps.size());
}

std::string CS2AMapManager::MatchLocalMap(const std::string &input, std::string &error) const
{
	std::string search = str::ToLower(input);

	for (const std::string &name : m_localMaps)
	{
		if (str::ToLower(name) == search)
		{
			return name;
		}
	}

	std::vector<const std::string *> matches;
	for (const std::string &name : m_localMaps)
	{
		if (str::ToLower(name).find(search) != std::string::npos)
		{
			matches.push_back(&name);
		}
	}

	if (matches.size() == 1)
	{
		return *matches[0];
	}

	if (matches.size() > 1)
	{
		error = "Multiple maps match '";
		error += input;
		error += "':";
		for (size_t i = 0; i < matches.size() && i < 5; i++)
		{
			error += " ";
			error += *matches[i];
		}
		if (matches.size() > 5)
		{
			error += " ...";
		}
	}

	return "";
}

std::vector<const MapEntry *> CS2AMapManager::GetSortedMaps() const
{
	std::vector<const MapEntry *> sorted;
	sorted.reserve(m_maps.size());
	for (const MapEntry &map : m_maps)
	{
		sorted.push_back(&map);
	}
	auto name = [](const MapEntry *e) -> const std::string & { return e->displayName.empty() ? e->mapName : e->displayName; };
	std::stable_sort(sorted.begin(), sorted.end(), [&](const MapEntry *a, const MapEntry *b) { return mmu::MapNameLess(name(a), name(b)); });
	return sorted;
}

bool CS2AMapManager::LoadMapList()
{
	m_maps.clear();

	ScanLocalMaps();

	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/maplist.txt", g_SMAPI->GetBaseDir());

	std::ifstream file(path);
	if (!file.is_open())
	{
		MMU_LOG_WARN("Could not open maplist file: %s\n", path);
		return false;
	}

	std::string line;
	while (std::getline(file, line))
	{
		mmu::MapListEntry parsed;
		if (!mmu::ParseMapListLine(line, parsed))
		{
			continue;
		}

		MapEntry entry;
		entry.displayName = parsed.displayName;
		entry.mapName = parsed.mapName;
		entry.workshopId = parsed.workshopId;
		entry.isWorkshop = parsed.isWorkshop;

		if (!entry.mapName.empty())
		{
			m_maps.push_back(entry);
		}
	}

	MMU_LOG_INFO("Loaded %d maps from maplist\n", (int)m_maps.size());
	return true;
}

const MapEntry *CS2AMapManager::FindMap(const char *input, std::string &error, std::vector<const MapEntry *> *outMatches) const
{
	if (!input || !*input)
	{
		error = "No map specified.";
		return nullptr;
	}

	std::string search = str::ToLower(input);

	for (const auto &entry : m_maps)
	{
		if (str::ToLower(entry.mapName) == search)
		{
			return &entry;
		}
	}

	for (const auto &entry : m_maps)
	{
		if (entry.isWorkshop && entry.workshopId == input)
		{
			return &entry;
		}
	}

	std::vector<const MapEntry *> matches;
	for (const auto &entry : m_maps)
	{
		if (str::ToLower(entry.mapName).find(search) != std::string::npos)
		{
			matches.push_back(&entry);
		}
	}

	if (matches.size() == 1)
	{
		return matches[0];
	}

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
		{
			error += " ...";
		}
		if (outMatches)
		{
			*outMatches = std::move(matches);
		}
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

	std::string inputStr(input);
	bool isRawWorkshopId =
		inputStr.length() >= 6 && std::all_of(inputStr.begin(), inputStr.end(), [](unsigned char c) { return std::isdigit(c) != 0; });

	if (isRawWorkshopId)
	{
		const MapEntry *entry = FindMap(input, error);
		if (entry && entry->isWorkshop)
		{
			error.clear();
			return BeginWorkshopChange(entry->workshopId, MapLabel(*entry), error);
		}

		error.clear();
		return BeginWorkshopChange(inputStr, inputStr, error);
	}

	const MapEntry *entry = FindMap(input, error);
	if (!entry)
	{
		std::string resolved = MatchLocalMap(inputStr, error);
		if (resolved.empty())
		{
			return false; // error is the ambiguous list, or FindMap's "No map found"
		}

		if (!g_pEngine->IsMapValid(resolved.c_str()))
		{
			return false;
		}

		error.clear();
		ClearPendingChange();
		g_pEngine->ChangeLevel(resolved.c_str(), nullptr);
		return true;
	}

	if (entry->isWorkshop)
	{
		return BeginWorkshopChange(entry->workshopId, MapLabel(*entry), error);
	}

	// A workshop change still waiting on its download would otherwise fire later on top of this one.
	ClearPendingChange();
	g_pEngine->ChangeLevel(entry->mapName.c_str(), nullptr);
	return true;
}

bool CS2AMapManager::BeginWorkshopChange(const std::string &workshopId, const std::string &label, std::string &error)
{
	uint64_t fileId = std::strtoull(workshopId.c_str(), nullptr, 10);

	if (m_pending.Busy())
	{
		error = "Already working on '" + m_pendingLabel + "', try again once it is loaded.";
		return false;
	}

	if (fileId == 0 || mmu::workshop::IsReady(fileId))
	{
		IssueWorkshopChange(workshopId);
		m_pending.WatchEngine(fileId);
		m_pendingWorkshopId = workshopId;
		m_pendingLabel = label;
		return true;
	}

	if (g_CS2AConfig.workshopDownloadTimeout <= 0)
	{
		error = "Map '" + label + "' is not installed on this server.";
		return false;
	}

	// The download itself starts from Tick, once Steam has confirmed the id is a CS2 map.
	if (!m_pending.Begin(fileId, static_cast<float>(g_CS2AConfig.workshopDownloadTimeout), g_AdminSteamAPI))
	{
		error = "Map '" + label + "' is not installed and no download could be started.";
		return false;
	}
	m_pendingWorkshopId = workshopId;
	m_pendingLabel = label;
	return true;
}

void CS2AMapManager::ClearPendingChange()
{
	m_pending.Clear();
	m_pendingWorkshopId.clear();
	m_pendingLabel.clear();
}

void CS2AMapManager::OnMapStart()
{
	ClearPendingChange();
}

void CS2AMapManager::CancelRtvVote()
{
	// Not cached, rtv may unload.
	if (ICS2RTV *rtv = static_cast<ICS2RTV *>(g_SMAPI->MetaFactory(CS2RTV_INTERFACE, nullptr, nullptr)))
	{
		rtv->CancelVote();
	}
}

void CS2AMapManager::RefreshRtv(PluginId unloading)
{
	int ret = META_IFACE_FAILED;
	PluginId id = 0;
	void *iface = g_SMAPI->MetaFactory(CS2RTV_FORWARDS_INTERFACE, &ret, &id);
	bool usable = iface && ret == META_IFACE_OK && (unloading == 0 || id != unloading);
	ICS2RTVForwards *forwards = usable ? static_cast<ICS2RTVForwards *>(iface) : nullptr;
	if (forwards == m_rtvForwards)
	{
		return;
	}

	// An rtv that went away took its forward list with it, so there is nothing to unregister on the old one.
	m_rtvForwards = forwards;
	m_rtvVoteStartHandle = kInvalidRTVForwardHandle;
	if (forwards)
	{
		m_rtvVoteStartHandle = forwards->RegisterOnMapVoteStart(
			[](bool)
			{
				if (!g_CS2AMapManager.m_pending.Busy())
				{
					return false;
				}
				ADMIN_ChatToAllT("Vote skipped, the map is already changing to %s.", g_CS2AMapManager.m_pendingLabel.c_str());
				return true;
			});
	}
}

void CS2AMapManager::ShutdownRtv()
{
	if (m_rtvForwards)
	{
		m_rtvForwards->UnregisterOnMapVoteStart(m_rtvVoteStartHandle);
	}
	m_rtvForwards = nullptr;
	m_rtvVoteStartHandle = kInvalidRTVForwardHandle;
}

void CS2AMapManager::Tick()
{
	int percent = 0;

	switch (m_pending.Poll(g_AdminSteamAPI))
	{
		case mmu::workshop::PendingDownload::Status::Started:
			// A raw id from the command has no name of its own.
			if (m_pendingLabel == m_pendingWorkshopId && !m_pending.Title().empty())
			{
				m_pendingLabel = m_pending.Title();
			}
			// Held back until here so a mistyped id does not cost the players their vote.
			CancelRtvVote();
			MMU_LOG_INFO("Downloading workshop map '%s' (%s) before changing.\n", m_pendingLabel.c_str(), m_pendingWorkshopId.c_str());
			ADMIN_ChatToAllT("Downloading %s, the map will change once it finishes.", m_pendingLabel.c_str());
			break;
		case mmu::workshop::PendingDownload::Status::Settled:
			// The id and label stay for the engine watch that takes over from here.
			IssueWorkshopChange(m_pendingWorkshopId);
			m_pending.WatchEngine(std::strtoull(m_pendingWorkshopId.c_str(), nullptr, 10));
			break;
		case mmu::workshop::PendingDownload::Status::Rejected:
			MMU_LOG_WARN("Workshop item %s was not found or is not a CS2 map.\n", m_pendingWorkshopId.c_str());
			ADMIN_ChatToAllT("Workshop item %s was not found or is not a CS2 map.", m_pendingLabel.c_str());
			ClearPendingChange();
			break;
		case mmu::workshop::PendingDownload::Status::StartFailed:
			MMU_LOG_WARN("Workshop map '%s' (%s) is not installed and no download could be started.\n", m_pendingLabel.c_str(),
						 m_pendingWorkshopId.c_str());
			ADMIN_ChatToAllT("%s is not installed and no download could be started.", m_pendingLabel.c_str());
			ClearPendingChange();
			break;
		case mmu::workshop::PendingDownload::Status::DownloadFailed:
			MMU_LOG_WARN("Workshop map '%s' (%s) failed to download, staying on the current map.\n", m_pendingLabel.c_str(),
						 m_pendingWorkshopId.c_str());
			ADMIN_ChatToAllT("%s could not be downloaded. Staying on the current map.", m_pendingLabel.c_str());
			ClearPendingChange();
			break;
		case mmu::workshop::PendingDownload::Status::ChangeFailed:
			MMU_LOG_WARN("The engine dropped the change to workshop map '%s' (%s).\n", m_pendingLabel.c_str(), m_pendingWorkshopId.c_str());
			ADMIN_ChatToAllT("Map change to %s failed.", m_pendingLabel.c_str());
			ClearPendingChange();
			break;
		case mmu::workshop::PendingDownload::Status::TimedOut:
			MMU_LOG_WARN("Workshop map '%s' (%s) did not download in time, staying on the current map.\n", m_pendingLabel.c_str(),
						 m_pendingWorkshopId.c_str());
			ADMIN_ChatToAllT("%s could not be downloaded in time. Staying on the current map.", m_pendingLabel.c_str());
			ClearPendingChange();
			break;
		case mmu::workshop::PendingDownload::Status::Announce:
			if (m_pending.Percent(g_AdminSteamAPI, percent))
			{
				ADMIN_ChatToAllT("Downloading %s... %d%%", m_pendingLabel.c_str(), percent);
			}
			break;
		case mmu::workshop::PendingDownload::Status::Waiting:
		case mmu::workshop::PendingDownload::Status::Idle:
			break;
	}
}
