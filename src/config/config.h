#ifndef _INCLUDE_ADMIN_CONFIG_H_
#define _INCLUDE_ADMIN_CONFIG_H_

#include <string>

struct CS2AConfig
{
	std::string website = "http://www.yourwebsite.net/sourcebans";
	std::string chatPrefix = "\x03[CS2 Admin]\x01 ";
	std::string commandPrefix = "!";
	std::string silentCommandPrefix = "/";
	std::string databasePrefix = "sb";
	std::string defaultLanguage = "en";

	// Logging
	bool logToFile = true;
	int logRetentionDays = 30;

	// Database type: "mysql" or "sqlite"
	std::string dbType = "sqlite";

	// MySQL settings
	std::string dbHost = "localhost";
	std::string dbUser = "root";
	std::string dbPass = "";
	std::string dbName = "sourcemod";
	int dbPort = 3306;

	// SQLite settings (path relative to game root, e.g. "addons/cs2admin/cs2admin.db")
	std::string dbPath = "addons/cs2admin/cs2admin.db";

	int serverID = -1;
	bool addban = true;
	bool unban = true;
	float retryTime = 45.0f;
	// Max consecutive reconnect attempts before giving up (0 = unlimited).
	int maxReconnectAttempts = 5;
	int processQueueTime = 5;
	// Seconds to wait for an absent workshop map to download before giving up on the change.
	int workshopDownloadTimeout = 120;
	int autoAddServer = 0;
	bool backupConfigs = true;
	bool enableAdmins = true;
	bool requireSiteLogin = false;

	// Comms config
	int commsDefaultTime = 30;
	bool disableUnblockImmunityCheck = false;
	int consoleImmunity = 20;
	int commsMaxLength = 0;

	// Checker config
	bool printCheckOnConnect = true;

	// Report config
	float reportCooldown = 60.0f;
	int reportMinLength = 10;

	// Sleuth config (0 = disabled)
	int sleuthActions = 0;          // 1=ban orig, 2=ban custom, 3=ban double, 4=notify, 5=kick
	int sleuthDuration = 0;         // Ban time for action 2 (min, 0=permanent)
	int sleuthBansAllowed = 0;      // IP ban count threshold before acting
	int sleuthBanType = 0;          // 0=all, 1=permanent only
	bool sleuthAdminBypass = false; // ADMFLAG_BAN users exempt
	bool sleuthExcludeOld = false;
	int sleuthExcludeTime = 31536000; // 1 year in seconds

	// Discord config
	std::string discordWebhookUrl = "";
	std::string discordFooterText = "CS2Admin";

	// Chat flood config
	float chatFloodCooldown = 0.75f; // Minimum seconds between messages
	int chatFloodMaxMessages = 5;    // Messages in window before action
	int chatFloodMuteDuration = 0;   // Minutes to auto-mute (0 = block only)

	// Tags config.
	//
	// chatOwnership makes us render every player chat line ourselves and suppress the game's own.
	// It is off by default because only one plugin on a server can do this, a second one produces duplicate lines.
	// These three say what the server asked for, not what happens: another plugin may own chat or the clan tag.
	// ADMIN_ChatTagsActive / ADMIN_BoardTagsActive answer that, and the two differ, since a plugin can own one surface without the other.
	bool chatOwnership = false;
	// Show tags in chat. Implies chatOwnership, since a tag can't be injected into a line the game renders.
	bool chatTagsEnabled = false;
	// Show tags on the scoreboard via the clan tag. Independent of chat ownership.
	bool boardTagsEnabled = false;

	// Chat line layout. Tokens: {tag} {name} {msg} {namecolor} {msgcolor} {teamcolor} {teamname}, plus the usual {color} tags.
	// Dead/team variants pick up the same tokens.
	std::string chatFormat = "{tag}{namecolor}{name}{default}: {msgcolor}{msg}";
	std::string chatFormatDead = "{grey}*{default} {tag}{namecolor}{name}{default}: {msgcolor}{msg}";
	std::string chatFormatTeam = "{grey}({teamname}){default} {tag}{namecolor}{name}{default}: {msgcolor}{msg}";
	std::string chatFormatTeamDead = "{grey}*({teamname}){default} {tag}{namecolor}{name}{default}: {msgcolor}{msg}";

	// Which palette color stands in for each team.
	std::string teamColorCT = "{blue}";
	std::string teamColorT = "{gold}";
	std::string teamColorSpec = "{grey}";

	// Menu config (only used when the mm-cs2menus plugin is loaded; no-arg
	// targeting commands open a picker instead of printing usage).
	std::string menuType = "default"; // "default" (let cs2menus decide) | "chat" | "html"
	// Comma-separated duration presets (minutes, 0 = permanent) for the ban/mute/gag/silence picker.
	std::string menuDurations = "30,60,180,1440,10080,0";
	// Comma-separated reason presets for the ban/mute/gag/silence/kick picker.
	std::string menuReasons = "Cheating,Toxicity,Spam,Advertising,Ban Evasion,Other";
	// Per-menu HTML nav-key overrides (only used with mm-cs2menus, html menus).
	// "default" delegates to the menu plugin's configured key, "none" disables the action,
	// otherwise a key name (w/a/s/d, e/use, shift/speed, ctrl/duck, space/jump, r/reload, mouse1, mouse2, tab, f/inspect).
	std::string menuNavUp = "default";
	std::string menuNavDown = "default";
	std::string menuNavSelect = "default";
	std::string menuNavBack = "default";
};

// Load and parse core.cfg from the given path.
// Returns true on success, false if file couldn't be opened/parsed.
bool ADMIN_LoadConfig(const char *path, CS2AConfig &config);

// Global config instance
extern CS2AConfig g_CS2AConfig;

#endif // _INCLUDE_ADMIN_CONFIG_H_
