#include "config.h"
#include "utils/str.h"
#include "utils/log.h"
#include "utils/chat_colors.h"
#include "utils/kv_parser.h"
#include "src/common.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

CS2AConfig g_CS2AConfig;

// Parse a section body (after the opening brace).
// Calls handler(sectionName, key, value) for each keyvalue pair.
// Recurses into subsections.
typedef kv::Handler KVHandler;

// A color name, or the {name} tag these keys took before.
static std::string ColorTag(const std::string &value)
{
	return (value.empty() || value[0] == '{') ? value : "{" + value + "}";
}

static void ConfigHandler(const std::string &section, const std::string &key, const std::string &value, void *userdata)
{
	CS2AConfig *cfg = static_cast<CS2AConfig *>(userdata);
	std::string sec = str::ToLower(section);
	std::string k = str::ToLower(key);
	bool known = true;

	// Old section names.
	if (sec == "config")
	{
		sec = "general";
	}
	else if (sec.size() > 6 && sec.compare(sec.size() - 6, 6, "config") == 0)
	{
		sec.resize(sec.size() - 6);
	}

	if (sec == "general")
	{
		if (k == "website")
		{
			// Older example configs shipped this placeholder, which banned players must not be sent to.
			cfg->website = (value == "http://www.yourwebsite.net/sourcebans") ? "" : value;
		}
		else if (k == "chatprefix")
		{
			cfg->chatPrefix = mmu::ResolveColorTags(value);
		}
		else if (mmu::config::ApplyGeneralKey(cfg->general, k, value) || mmu::config::ApplyLogKey(cfg->log, k, value))
		{
			// consumed
		}
		// Old name.
		else if (k == "databaseprefix")
		{
			cfg->database.prefix = value;
		}
		else if (k == "addban")
		{
			cfg->addban = (value != "0");
		}
		else if (k == "unban")
		{
			cfg->unban = (value != "0");
		}
		else if (k == "retrytime")
		{
			float v = static_cast<float>(std::atof(value.c_str()));
			if (v < 15.0f)
			{
				v = 15.0f;
			}
			if (v > 60.0f)
			{
				v = 60.0f;
			}
			cfg->retryTime = v;
		}
		else if (k == "maxreconnectattempts")
		{
			int v = std::atoi(value.c_str());
			if (v < 0)
			{
				v = 0;
			}
			cfg->maxReconnectAttempts = v;
		}
		else if (k == "processqueueminutes" || k == "processqueuetime")
		{
			// Zero or less would rescan the queue every frame.
			int v = std::atoi(value.c_str());
			if (v < 1)
			{
				v = 1;
			}
			cfg->processQueueMinutes = v;
		}
		else if (k == "workshopdownloadtimeout")
		{
			cfg->workshopDownloadTimeout = std::atoi(value.c_str());
		}
		else if (k == "autoaddserver")
		{
			cfg->autoAddServer = std::atoi(value.c_str());
		}
		else if (k == "backupconfigs")
		{
			cfg->backupConfigs = (value != "0");
		}
		else if (k == "adminsenabled" || k == "enableadmins")
		{
			cfg->adminsEnabled = (value != "0");
		}
		else if (k == "requiresitelogin")
		{
			cfg->requireSiteLogin = (value != "0");
		}
		else if (k == "serverid")
		{
			cfg->serverId = std::atoi(value.c_str());
		}
		else
		{
			known = false;
		}
	}
	else if (sec == "database")
	{
		// Not cleaned up like ApplyDatabaseKey would, a bad one fails the load instead of pointing at other tables.
		if (k == "prefix")
		{
			cfg->database.prefix = value;
		}
		else
		{
			known = mmu::config::ApplyDatabaseKey(cfg->database, k, value);
		}
	}
	else if (sec == "comms")
	{
		if (k == "defaultminutes" || k == "defaulttime")
		{
			cfg->commsDefaultMinutes = std::atoi(value.c_str());
		}
		else if (k == "disableunblockimmunitycheck")
		{
			cfg->disableUnblockImmunityCheck = (value != "0");
		}
		else if (k == "consoleimmunity")
		{
			cfg->consoleImmunity = std::atoi(value.c_str());
		}
		else if (k == "maxminutes" || k == "maxlength")
		{
			cfg->commsMaxMinutes = std::atoi(value.c_str());
		}
		else
		{
			known = false;
		}
	}
	else if (sec == "checker")
	{
		if (k == "printcheckonconnect")
		{
			cfg->printCheckOnConnect = (value != "0");
		}
		else
		{
			known = false;
		}
	}
	else if (sec == "report")
	{
		if (k == "cooldown")
		{
			cfg->reportCooldown = static_cast<float>(std::atof(value.c_str()));
		}
		else if (k == "minlength")
		{
			cfg->reportMinLength = std::atoi(value.c_str());
		}
		else
		{
			known = false;
		}
	}
	else if (sec == "sleuth")
	{
		if (k == "action" || k == "actions")
		{
			cfg->sleuthAction = std::atoi(value.c_str());
		}
		else if (k == "banminutes" || k == "duration")
		{
			cfg->sleuthBanMinutes = std::atoi(value.c_str());
		}
		else if (k == "maxbans" || k == "bansallowed")
		{
			cfg->sleuthMaxBans = std::atoi(value.c_str());
		}
		else if (k == "bantype")
		{
			cfg->sleuthBanType = std::atoi(value.c_str());
		}
		else if (k == "adminbypass")
		{
			cfg->sleuthAdminBypass = (value != "0");
		}
		else if (k == "excludeold")
		{
			cfg->sleuthExcludeOld = (value != "0");
		}
		else if (k == "excludetime")
		{
			cfg->sleuthExcludeTime = std::atoi(value.c_str());
		}
		else
		{
			known = false;
		}
	}
	else if (sec == "discord")
	{
		if (k == "webhookurl")
		{
			cfg->discordWebhookUrl = value;
		}
		else if (k == "footertext")
		{
			cfg->discordFooterText = value;
		}
		else
		{
			known = false;
		}
	}
	else if (sec == "chatflood")
	{
		if (k == "cooldown")
		{
			cfg->chatFloodCooldown = static_cast<float>(std::atof(value.c_str()));
		}
		else if (k == "maxmessages")
		{
			cfg->chatFloodMaxMessages = std::atoi(value.c_str());
		}
		else if (k == "muteminutes" || k == "muteduration")
		{
			cfg->chatFloodMuteMinutes = std::atoi(value.c_str());
		}
		else
		{
			known = false;
		}
	}
	else if (sec == "tags")
	{
		if (k == "chatownership")
		{
			cfg->chatOwnership = (value != "0");
		}
		else if (k == "chattags")
		{
			cfg->chatTags = (value != "0");
		}
		else if (k == "leaderboardtags")
		{
			cfg->leaderboardTags = (value != "0");
		}
		else if (k == "format")
		{
			cfg->chatFormat = value;
		}
		else if (k == "formatdead")
		{
			cfg->chatFormatDead = value;
		}
		else if (k == "formatteam")
		{
			cfg->chatFormatTeam = value;
		}
		else if (k == "formatteamdead")
		{
			cfg->chatFormatTeamDead = value;
		}
		else if (k == "teamcolorct")
		{
			cfg->teamColorCT = ColorTag(value);
		}
		else if (k == "teamcolort")
		{
			cfg->teamColorT = ColorTag(value);
		}
		else if (k == "teamcolorspec")
		{
			cfg->teamColorSpec = ColorTag(value);
		}
		else
		{
			known = false;
		}
	}
	else if (sec == "menu")
	{
		if (cfg->menu.ApplyKey(k, value))
		{
			// consumed
		}
		else if (k == "durations")
		{
			cfg->menuDurations = value;
		}
		else if (k == "reasons")
		{
			cfg->menuReasons = value;
		}
		else
		{
			known = false;
		}
	}
	else
	{
		known = false;
	}
	if (!known)
	{
		mmu::config::WarnUnknownKey(section, key);
	}
}

bool CS2A_LoadConfig(const char *path, CS2AConfig &config)
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		return false;
	}

	// Expect: "cs2admin" { ... }
	kv::Token root = kv::NextToken(file);
	if (root.kind != kv::TokenType::String)
	{
		return false;
	}

	kv::Token brace = kv::NextToken(file);
	if (brace.kind != kv::TokenType::OpenBrace)
	{
		return false;
	}

	kv::ParseSection(file, root.value, ConfigHandler, &config);

	// Validate databasePrefix: only alphanumeric and underscore allowed
	for (char c : config.database.prefix)
	{
		if (!isalnum(static_cast<unsigned char>(c)) && c != '_')
		{
			MMU_LOG_ERROR("Invalid character '%c' in databasePrefix. Only alphanumeric and underscore allowed.\n", c);
			return false;
		}
	}
	if (config.database.prefix.empty())
	{
		MMU_LOG_ERROR("databasePrefix cannot be empty.\n");
		return false;
	}

	// Clamp chatFloodMaxMessages to sane range
	if (config.chatFloodMaxMessages < 1)
	{
		config.chatFloodMaxMessages = 1;
	}

	// Ensure command prefixes are different
	if (!config.general.commandPrefix.empty() && config.general.commandPrefix == config.general.silentCommandPrefix)
	{
		MMU_LOG_WARN("commandPrefix and silentCommandPrefix are the same ('%s'). Silent commands will not work. Resetting "
					 "silentCommandPrefix to '/'.\n",
					 config.general.commandPrefix.c_str());
		config.general.silentCommandPrefix = "/";
	}

	// A tag can only be injected into a line we render ourselves.
	if (config.chatTags && !config.chatOwnership)
	{
		MMU_LOG_WARN("Tags: ChatTags needs ChatOwnership to render the tag. Enabling ChatOwnership.\n");
		config.chatOwnership = true;
	}

	return true;
}
