#ifndef _INCLUDE_CS2A_CONFIG_H_
#define _INCLUDE_CS2A_CONFIG_H_

#include "interfaces/cs2menus/menu_style.h"
#include "utils/config_blocks.h"

#include <string>

struct GeneralCfg : mmu::config::GeneralBlock
{
	// Shown to banned players on kick. Empty = not shown.
	std::string website;
	std::string chatPrefix = "\x03[CS2 Admin]\x01 ";

	int serverId = -1;
	bool addban = true;
	bool unban = true;
	float retryTime = 45.0f;
	// Max consecutive reconnect attempts before giving up (0 = unlimited).
	int maxReconnectAttempts = 5;
	int processQueueMinutes = 5;
	// Seconds to wait for an absent workshop map to download before giving up on the change.
	int workshopDownloadTimeout = 120;
	int autoAddServer = 0;
	bool backupConfigs = true;
	bool adminsEnabled = true;
	bool requireSiteLogin = false;
};

struct CommsCfg
{
	int defaultMinutes = 30;
	bool disableUnblockImmunityCheck = false;
	int consoleImmunity = 20;
	int maxMinutes = 0;
};

struct CheckerCfg
{
	bool printCheckOnConnect = true;
};

struct ReportCfg
{
	float cooldown = 60.0f;
	int minLength = 10;
};

struct SleuthCfg
{
	int action = 0;           // 0=disabled, 1=ban orig, 2=ban custom, 3=ban double, 4=notify, 5=kick
	int banMinutes = 0;       // Ban time for action 2 (0=permanent)
	int maxBans = 0;          // IP ban count threshold before acting
	int banType = 0;          // 0=all, 1=permanent only
	bool adminBypass = false; // ADMFLAG_BAN users exempt
	bool excludeOld = false;
	int excludeTime = 31536000; // 1 year in seconds
};

struct DiscordCfg
{
	std::string webhookUrl;
	std::string footerText = "CS2Admin";
};

struct ChatFloodCfg
{
	float cooldown = 0.75f; // Minimum seconds between messages
	int maxMessages = 5;    // Messages in window before action
	int muteMinutes = 0;    // Auto-mute length (0 = block only)
};

struct TagsCfg
{
	// chatOwnership makes us render every player chat line ourselves and suppress the game's own.
	// It is off by default because only one plugin on a server can do this, a second one produces duplicate lines.
	// These three say what the server asked for, not what happens: another plugin may own chat or the clan tag.
	// CS2A_ChatTagsActive / CS2A_BoardTagsActive answer that, and the two differ, since a plugin can own one surface without the other.
	bool chatOwnership = false;
	// Show tags in chat. Implies chatOwnership, since a tag can't be injected into a line the game renders.
	bool chatTags = false;
	// Show tags on the scoreboard via the clan tag. Independent of chat ownership.
	bool leaderboardTags = false;

	// Chat line layout. Tokens: {tag} {name} {msg} {namecolor} {msgcolor} {teamcolor} {teamname}, plus the usual {color} tags.
	// Dead/team variants pick up the same tokens.
	std::string format = "{tag}{namecolor}{name}{default}: {msgcolor}{msg}";
	std::string formatDead = "{grey}*{default} {tag}{namecolor}{name}{default}: {msgcolor}{msg}";
	std::string formatTeam = "{grey}({teamname}){default} {tag}{namecolor}{name}{default}: {msgcolor}{msg}";
	std::string formatTeamDead = "{grey}*({teamname}){default} {tag}{namecolor}{name}{default}: {msgcolor}{msg}";

	// Which palette color stands in for each team.
	std::string teamColorCT = "{blue}";
	std::string teamColorT = "{gold}";
	std::string teamColorSpec = "{grey}";
};

// Only used when the mm-cs2menus plugin is loaded, no-arg targeting commands then open a picker instead of printing usage.
struct MenuCfg : MenuStyleBlock
{
	// Comma-separated duration presets (minutes, 0 = permanent) for the ban/mute/gag/silence picker.
	std::string durations = "30,60,180,1440,10080,0";
	// Comma-separated reason presets for the punish forms. "Other", typed in chat, is always added.
	std::string reasons = "Cheating,Toxicity,Spam,Advertising,Ban Evasion";
};

struct CS2AConfig
{
	GeneralCfg general;
	CommsCfg comms;
	CheckerCfg checker;
	ReportCfg report;
	SleuthCfg sleuth;
	DiscordCfg discord;
	ChatFloodCfg chatFlood;
	TagsCfg tags;
	MenuCfg menu;

	mmu::config::DatabaseBlock database = mmu::config::DatabaseBlock::Defaults("sourcemod", "addons/cs2admin/data/cs2admin.db", "sb");
	mmu::config::LogBlock log;
};

// Load and parse core.cfg from the given path.
// Returns true on success, false if file couldn't be opened/parsed.
bool CS2A_LoadConfig(const char *path, CS2AConfig &config);

// Global config instance
extern CS2AConfig g_CS2AConfig;

#endif // _INCLUDE_CS2A_CONFIG_H_
