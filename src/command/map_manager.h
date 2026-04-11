#ifndef _INCLUDE_ADMIN_MAP_MANAGER_H_
#define _INCLUDE_ADMIN_MAP_MANAGER_H_

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
	bool ChangeMap(const char *input, std::string &error);

	// Get number of loaded maps.
	int GetMapCount() const { return (int)m_maps.size(); }

	const std::vector<MapEntry> &GetMaps() const { return m_maps; }

private:
	std::vector<MapEntry> m_maps;
};

extern CS2AMapManager g_CS2AMapManager;

#endif // _INCLUDE_ADMIN_MAP_MANAGER_H_
