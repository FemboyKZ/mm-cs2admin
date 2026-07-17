#ifndef _INCLUDE_ADMIN_TAG_MANAGER_H_
#define _INCLUDE_ADMIN_TAG_MANAGER_H_

#include "src/common.h"

#include <cstdint>
#include <string>
#include <vector>

// What a tag matches a player on.
enum class TagMatch
{
	SteamID,  // exact SteamID, given in any format
	IP,       // exact IP, no mask support
	Flag,     // holds any one of the admin flags in the value
	Group,    // admin's group name
	Immunity, // immunity at or above the value
};

struct TagDef
{
	std::string id; // section name, what !tag takes and what the DB stores
	TagMatch match = TagMatch::Flag;
	// Highest wins when a player matches several tags and hasn't picked one.
	int priority = 0;

	// Parsed match value. Only the field matching `match` is meaningful.
	uint64_t steamid64 = 0;
	std::string ip;
	uint32_t flags = 0;
	std::string group;
	int immunity = 0;

	std::string chatTag;   // {tag}, color tags allowed
	std::string nameColor; // {namecolor}, empty falls back to the player's team color
	std::string msgColor;  // {msgcolor}, empty falls back to {default}
	std::string boardTag;  // scoreboard clan tag
};

class CS2ATagManager
{
public:
	// Load configs/tags.cfg, replacing whatever was loaded before.
	void LoadTags();

	// Tags this player matches, best first. Empty when they match none.
	std::vector<const TagDef *> EligibleFor(int slot) const;

	// The tag to show for this player, or null for none.
	// Honours their pick when they have one, else the highest priority match.
	const TagDef *Resolve(int slot) const;

	// Look up a loaded tag by id, or null.
	const TagDef *FindTag(const char *id) const;

	// Pick a tag by id, or "" to show none. Returns false when the player isn't eligible for it.
	// Persists the pick and refreshes the scoreboard tag.
	bool SelectTag(int slot, const char *id);

	// Load this player's stored pick. Call once they have a SteamID.
	void LoadPlayerPref(int slot, uint64_t steamid64);

	// Apply a pick read back from the DB. Public only for the LoadPlayerPref callback.
	void ApplyLoadedPref(int slot, const char *tagId, bool disabled);

	// Push the resolved tag to the scoreboard. No-op unless LeaderboardTags is on.
	void UpdateClanTag(int slot);

	// Re-apply every connected player's clan tag, e.g. after a reload.
	void UpdateAllClanTags();

	void OnClientDisconnect(int slot);

	// Create the prefs table when missing. Call once the DB is connected.
	void EnsureSchema();

private:
	bool PlayerMatches(int slot, const TagDef &tag) const;
	void SavePlayerPref(int slot);

	std::vector<TagDef> m_tags;

	// Per slot pick. Empty means unset, so Resolve falls back to priority.
	std::string m_selected[MAXPLAYERS + 1];
	// Player explicitly chose to show no tag, which is not the same as unset.
	bool m_tagDisabled[MAXPLAYERS + 1] = {};

	// Backing storage for the clan tag. See CCSPlayerController::SetClan.
	char m_clanTag[MAXPLAYERS + 1][64] = {};
};

extern CS2ATagManager g_CS2ATagManager;

// Whether tags actually reach players right now, rather than merely being asked for in the config.
// Each is off when another plugin owns that surface, and the two answer separately because a plugin can own one without the other.
bool ADMIN_ChatTagsActive();
bool ADMIN_BoardTagsActive();

#endif // _INCLUDE_ADMIN_TAG_MANAGER_H_
