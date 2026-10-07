#ifndef _INCLUDE_ADMIN_MAP_MANAGER_H_
#define _INCLUDE_ADMIN_MAP_MANAGER_H_

#include "game/workshop.h"
#include "interfaces/cs2rockthevote/forwards.h"

#include <ISmmPlugin.h>

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

	// Find a map by partial name match. Returns nullptr if not found or ambiguous, outMatches then gets the ambiguous ones.
	const MapEntry *FindMap(int slot, const char *input, std::string &error, std::vector<const MapEntry *> *outMatches = nullptr) const;

	// Execute the map change. Returns true on success.
	// A workshop map that isn't on disk is downloaded first,
	// so success here can mean the change was accepted rather than already issued.
	// error comes back in the language of `slot`, as it does from the lookups below.
	bool ChangeMap(int slot, const char *input, std::string &error);

	// Drives a deferred workshop change. Call once per frame.
	void Tick();

	// True while a change is waiting on a workshop download.
	bool IsChangePending() const
	{
		return m_pending.Active();
	}

	// Drops a deferred change. The map already moved, so honouring it would be a surprise.
	void OnMapStart();

	// An admin's map change outranks rtv. This drops its running vote or scheduled change,
	// and RefreshRtv's vote start hook keeps a new one from starting while ours is busy.
	void CancelRtvVote();

	// Call from AllPluginsLoaded, OnPluginLoad and, with the departing plugin's id, OnPluginUnload.
	void RefreshRtv(PluginId unloading = 0);

	// Call from Unload, rtv must not keep a callback into this binary.
	void ShutdownRtv();

	// Get number of loaded maps.
	int GetMapCount() const
	{
		return (int)m_maps.size();
	}

	const std::vector<MapEntry> &GetMaps() const
	{
		return m_maps;
	}

	// GetMaps in menu order, see mmu::MapNameLess. The pointers go stale on the next LoadMapList.
	std::vector<const MapEntry *> GetSortedMaps() const;

private:
	// Scan <gamedir>/maps for local map files populating m_localMaps.
	// Used as a fallback when the maplist misses.
	void ScanLocalMaps();

	// Partial-match input against m_localMaps, same rules as FindMap.
	// Returns the full map name, "" if no match.
	std::string MatchLocalMap(int slot, const std::string &input, std::string &error) const;

	// host_workshop_map on an addon that isn't on disk drops the server onto the "error" map,
	// so an absent one is downloaded before the change is issued.
	// Refused while an earlier workshop change is still being worked on.
	bool BeginWorkshopChange(int slot, const std::string &workshopId, const std::string &label, std::string &error);
	void ClearPendingChange();

	std::vector<MapEntry> m_maps;
	std::vector<std::string> m_localMaps;

	mmu::workshop::PendingDownload m_pending;
	std::string m_pendingWorkshopId;
	std::string m_pendingLabel;

	ICS2RTVForwards *m_rtvForwards = nullptr;
	RTVForwardHandle m_rtvVoteStartHandle = kInvalidRTVForwardHandle;
};

extern CS2AMapManager g_CS2AMapManager;

#endif // _INCLUDE_ADMIN_MAP_MANAGER_H_
