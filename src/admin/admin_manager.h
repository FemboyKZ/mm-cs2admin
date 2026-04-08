#ifndef _INCLUDE_ADMIN_ADMIN_MANAGER_H_
#define _INCLUDE_ADMIN_ADMIN_MANAGER_H_

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include "../common.h"

// Admin flag bits, mirrors SourceMod's admin flag letters a-z
enum AdminFlag : uint32_t
{
	ADMFLAG_NONE          = 0,
	ADMFLAG_RESERVATION   = (1 << 0),   // a - Reserved slot
	ADMFLAG_GENERIC       = (1 << 1),   // b - Generic admin
	ADMFLAG_KICK          = (1 << 2),   // c - Kick
	ADMFLAG_BAN           = (1 << 3),   // d - Ban
	ADMFLAG_UNBAN         = (1 << 4),   // e - Unban
	ADMFLAG_SLAY          = (1 << 5),   // f - Slay
	ADMFLAG_CHANGEMAP     = (1 << 6),   // g - Map change
	ADMFLAG_CONVARS       = (1 << 7),   // h - ConVar access
	ADMFLAG_CONFIG        = (1 << 8),   // i - Config
	ADMFLAG_CHAT          = (1 << 9),   // j - Chat commands
	ADMFLAG_VOTE          = (1 << 10),  // k - Vote
	ADMFLAG_PASSWORD      = (1 << 11),  // l - Password
	ADMFLAG_RCON          = (1 << 12),  // m - RCON
	ADMFLAG_CHEATS        = (1 << 13),  // n - Cheats
	ADMFLAG_CUSTOM1       = (1 << 14),  // o - Custom 1
	ADMFLAG_CUSTOM2       = (1 << 15),  // p - Custom 2
	ADMFLAG_CUSTOM3       = (1 << 16),  // q - Custom 3
	ADMFLAG_CUSTOM4       = (1 << 17),  // r - Custom 4
	ADMFLAG_CUSTOM5       = (1 << 18),  // s - Custom 5
	ADMFLAG_CUSTOM6       = (1 << 19),  // t - Custom 6
	ADMFLAG_ROOT          = (1 << 25),  // z - Root (all access)
};

// Override types, mirrors SourceMod's OverrideType
enum OverrideType
{
	Override_Command = 0,      // A specific command (e.g. "sm_ban")
	Override_CommandGroup = 1, // A command group (e.g. "@admin")
};

// Override rule, mirrors SourceMod's OverrideRule
enum OverrideRule
{
	Command_Allow = 0,
	Command_Deny = 1,
};

// Info about a single admin entry (from DB or flat file)
struct AdminEntry
{
	std::string identity;      // SteamID "STEAM_0:X:Y"
	uint64_t steamid64 = 0;
	int adminId = 0;           // SBPP database admin ID (aid)
	std::string name;
	std::string group;         // Server group name (from DB or flat file)
	uint32_t flags = 0;        // Bitfield of AdminFlag
	int immunity = 0;
	std::string password;
	bool fromDatabase = false; // true = loaded from SBPP DB, false = from flat file
};

// Info about a server group
struct AdminGroup
{
	int id = 0;                // DB primary key (sb_srvgroups.id)
	std::string name;
	uint32_t flags = 0;
	int immunity = 0;

	// Per group command overrides: key = "cmd:<name>" or "grp:<name>", value = allow/deny
	std::unordered_map<std::string, OverrideRule> overrides;
};

class CS2AAdminManager
{
public:
	// Convert a flag string like "abcde" to a bitmask
	static uint32_t FlagsFromString(const char *flagStr);

	// Convert a bitmask to a flag string like "abcde"
	static std::string FlagsToString(uint32_t flags);

	// Check if a flag set includes a specific flag (root always passes)
	static bool HasFlag(uint32_t playerFlags, uint32_t requiredFlag);

	// Convert a SteamID in any format to STEAM_0:X:Y normalized form
	static std::string NormalizeSteamID(const char *input);

	// Convert STEAM_X:Y:Z to SteamID64
	static uint64_t AuthIdToSteamID64(const char *authid);

	// Admin management

	// Load flatfile admins from cfg/cs2admin/admins.cfg
	void LoadFlatFileAdmins();

	// Load SM-compatible admins_simple.ini from cfg/cs2admin/admins_simple.ini
	void LoadSimpleAdmins();

	// Load flat-file groups from cfg/cs2admin/admin_groups.cfg (SM-compatible KeyValues)
	void LoadFlatFileGroups();

	// Load flat-file global overrides from cfg/cs2admin/admin_overrides.cfg (SM-compatible KeyValues)
	void LoadFlatFileOverrides();

	// Load groups from the SBPP database, then load admins.
	// Calls the callback when all DB loading is done.
	void LoadDatabaseAdmins(std::function<void()> onComplete = nullptr);

	// Reload everything (flatfile + DB). Called on mm_reload, map change, etc.
	void ReloadAdmins();

	// Apply loaded admin data to all connected players.
	// Called after loading completes, or when DB is unavailable.
	void MergeAndApplyAll();

	// Apply loaded admin data to a connected player by slot.
	// Merges DB + flat-file entries for the same SteamID.
	void AssignAdminToPlayer(int slot);

	// Check if a connected player has a specific admin flag.
	bool PlayerHasFlag(int slot, uint32_t flag);

	// Check if a player can use a specific command, considering:
	// 1. Global overrides (sb_overrides), redefine required flags
	// 2. Group overrides (sb_srvgroups_overrides), per-group allow/deny
	// 3. Default flag check
	// commandName should be the command without prefix (e.g. "ban", "mute").
	// commandGroup is an optional command group name (e.g. "admin").
	// defaultFlag is the default required flag if no override applies.
	bool CanPlayerUseCommand(int slot, const char *commandName,
		const char *commandGroup, uint32_t defaultFlag);

	// Get the admin entry for a connected player (merged), or nullptr.
	const AdminEntry *GetPlayerAdmin(int slot);

	// Get a group by name
	const AdminGroup *GetGroup(const char *name) const;

	// Clear all loaded admin data
	void Clear();

private:
	void LoadGroups(std::function<void()> onComplete);
	void LoadGroupOverrides(std::function<void()> onComplete);
	void LoadGlobalOverrides(std::function<void()> onComplete);
	void LoadAdminsFromDB(std::function<void()> onComplete);

	// Flatfile admins (keyed by normalized SteamID)
	std::unordered_map<std::string, AdminEntry> m_flatFileAdmins;

	// DB admins (keyed by normalized SteamID)
	std::unordered_map<std::string, AdminEntry> m_dbAdmins;

	// Groups (keyed by group name)
	std::unordered_map<std::string, AdminGroup> m_groups;

	// Groups by DB id (for override loading)
	std::unordered_map<int, std::string> m_groupIdToName;

	// Global command overrides: key = "cmd:<name>" or "grp:<name>", value = required flags
	std::unordered_map<std::string, uint32_t> m_globalOverrides;

	// Merged admin entries per connected player slot
	AdminEntry m_playerAdmins[MAXPLAYERS + 1];
	bool m_playerHasAdmin[MAXPLAYERS + 1] = {};

	bool m_dbGroupsLoaded = false;
	bool m_dbAdminsLoaded = false;
};

extern CS2AAdminManager g_CS2AAdminManager;

#endif // _INCLUDE_ADMIN_ADMIN_MANAGER_H_
