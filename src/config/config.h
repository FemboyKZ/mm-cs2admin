#ifndef _INCLUDE_ADMIN_CONFIG_H_
#define _INCLUDE_ADMIN_CONFIG_H_

#include <string>

struct CS2AConfig
{
	std::string website = "http://www.yourwebsite.net/sourcebans";
	std::string chatPrefix = "\x10[CS2 Admin]\x01 ";
	std::string commandPrefix = "!";
	std::string silentCommandPrefix = "/";
	std::string databasePrefix = "sb";

	// Database type: "mysql" or "sqlite"
	std::string dbType = "mysql";

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
	int processQueueTime = 5;
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
	bool sleuthAdminBypass = false;  // ADMFLAG_BAN users exempt
	bool sleuthExcludeOld = false;
	int sleuthExcludeTime = 31536000; // 1 year in seconds

	// Discord config
	std::string discordWebhookUrl = "";
	std::string discordFooterText = "CS2Admin";

	// Chat flood config
	float chatFloodCooldown = 0.75f;   // Minimum seconds between messages
	int chatFloodMaxMessages = 5;      // Messages in window before action
	int chatFloodMuteDuration = 0;     // Minutes to auto-mute (0 = block only)
};

// Load and parse core.cfg from the given path.
// Returns true on success, false if file couldn't be opened/parsed.
bool ADMIN_LoadConfig(const char *path, CS2AConfig &config);

// Resolve color tags like {purple}, {green}, etc. to chat color codes.
std::string ADMIN_ResolveColorTags(const std::string &input);

// Global config instance
extern CS2AConfig g_CS2AConfig;

#endif // _INCLUDE_ADMIN_CONFIG_H_
