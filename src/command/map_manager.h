#ifndef _INCLUDE_ADMIN_MAP_MANAGER_H_
#define _INCLUDE_ADMIN_MAP_MANAGER_H_

#include <cstdint>
#include <string>
#include <vector>

struct MapEntry
{
	std::string displayName; // What the user sees (e.g. "de_dust2" or "surf_nyx (Tier 1, Linear)")
	std::string mapName;     // Actual map name for ChangeLevel (e.g. "de_dust2", "surf_nyx")
	std::string workshopId;  // Workshop ID if any (empty for stock maps)
	bool isWorkshop;
};

class CS2AMapManager
{
public:
	// Load maplist from cfg/maplist.txt
	bool LoadMapList();

	// Find a map by partial name match. Returns nullptr if not found or ambiguous.
	const MapEntry *FindMap(const char *input, std::string &error) const;

	// Execute the map change. Returns true on success.
	// A workshop map that isn't on disk is downloaded first,
	// so success here can mean the change was accepted rather than already issued.
	bool ChangeMap(const char *input, std::string &error);

	// Drives a deferred workshop change. Call once per frame.
	void Tick(float curtime);

	// True while a change is waiting on a workshop download.
	bool IsChangePending() const
	{
		return m_pendingChange;
	}

	// Drops a deferred change. The map already moved, so honouring it would be a surprise.
	void OnMapStart();

	// Get number of loaded maps.
	int GetMapCount() const
	{
		return (int)m_maps.size();
	}

	const std::vector<MapEntry> &GetMaps() const
	{
		return m_maps;
	}

private:
	// Scan <gamedir>/maps for local map files populating m_localMaps.
	// Used as a fallback when the maplist misses.
	void ScanLocalMaps();

	// Partial-match input against m_localMaps, same rules as FindMap.
	// Returns the full map name, "" if no match.
	std::string MatchLocalMap(const std::string &input, std::string &error) const;

	// host_workshop_map on an addon that isn't on disk drops the server onto the "error" map,
	// so an absent one is downloaded before the change is issued.
	bool BeginWorkshopChange(const std::string &workshopId, const std::string &label, std::string &error);
	void ClearPendingChange();

	std::vector<MapEntry> m_maps;
	std::vector<std::string> m_localMaps;

	bool m_pendingChange = false;
	uint64_t m_pendingFileId = 0;
	std::string m_pendingWorkshopId;
	std::string m_pendingLabel;
	float m_pendingDeadline = 0.0f;
	float m_pendingNextAnnounce = 0.0f;
};

extern CS2AMapManager g_CS2AMapManager;

#endif // _INCLUDE_ADMIN_MAP_MANAGER_H_
