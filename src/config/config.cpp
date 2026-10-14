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

// {name} is the old form.
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
			cfg->general.website = (value == "http://www.yourwebsite.net/sourcebans") ? "" : value;
		}
		else if (k == "chatprefix")
		{
			cfg->general.chatPrefix = mmu::ResolveColorTags(value);
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
			cfg->general.addban = (value != "0");
		}
		else if (k == "unban")
		{
			cfg->general.unban = (value != "0");
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
			cfg->general.retryTime = v;
		}
		else if (k == "maxreconnectattempts")
		{
			int v = std::atoi(value.c_str());
			if (v < 0)
			{
				v = 0;
			}
			cfg->general.maxReconnectAttempts = v;
		}
		else if (k == "processqueueminutes" || k == "processqueuetime")
		{
			// Zero or less would rescan the queue every frame.
			int v = std::atoi(value.c_str());
			if (v < 1)
			{
				v = 1;
			}
			cfg->general.processQueueMinutes = v;
		}
		else if (k == "workshopdownloadtimeout")
		{
			cfg->general.workshopDownloadTimeout = std::atoi(value.c_str());
		}
		else if (k == "autoaddserver")
		{
			cfg->general.autoAddServer = std::atoi(value.c_str());
		}
		else if (k == "backupconfigs")
		{
			cfg->general.backupConfigs = (value != "0");
		}
		else if (k == "adminsenabled" || k == "enableadmins")
		{
			cfg->general.adminsEnabled = (value != "0");
		}
		else if (k == "requiresitelogin")
		{
			cfg->general.requireSiteLogin = (value != "0");
		}
		else if (k == "serverid")
		{
			cfg->general.serverId = std::atoi(value.c_str());
		}
		else
		{
			known = false;
		}
	}
	else if (sec == "database")
	{
		// Raw, so a bad one fails the load.
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
			cfg->comms.defaultMinutes = std::atoi(value.c_str());
		}
		else if (k == "disableunblockimmunitycheck")
		{
			cfg->comms.disableUnblockImmunityCheck = (value != "0");
		}
		else if (k == "consoleimmunity")
		{
			cfg->comms.consoleImmunity = std::atoi(value.c_str());
		}
		else if (k == "maxminutes" || k == "maxlength")
		{
			cfg->comms.maxMinutes = std::atoi(value.c_str());
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
			cfg->checker.printCheckOnConnect = (value != "0");
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
			cfg->report.cooldown = static_cast<float>(std::atof(value.c_str()));
		}
		else if (k == "minlength")
		{
			cfg->report.minLength = std::atoi(value.c_str());
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
			cfg->sleuth.action = std::atoi(value.c_str());
		}
		else if (k == "banminutes" || k == "duration")
		{
			cfg->sleuth.banMinutes = std::atoi(value.c_str());
		}
		else if (k == "maxbans" || k == "bansallowed")
		{
			cfg->sleuth.maxBans = std::atoi(value.c_str());
		}
		else if (k == "bantype")
		{
			cfg->sleuth.banType = std::atoi(value.c_str());
		}
		else if (k == "adminbypass")
		{
			cfg->sleuth.adminBypass = (value != "0");
		}
		else if (k == "excludeold")
		{
			cfg->sleuth.excludeOld = (value != "0");
		}
		else if (k == "excludetime")
		{
			cfg->sleuth.excludeTime = std::atoi(value.c_str());
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
			cfg->discord.webhookUrl = value;
		}
		else if (k == "footertext")
		{
			cfg->discord.footerText = value;
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
			cfg->chatFlood.cooldown = static_cast<float>(std::atof(value.c_str()));
		}
		else if (k == "maxmessages")
		{
			cfg->chatFlood.maxMessages = std::atoi(value.c_str());
		}
		else if (k == "muteminutes" || k == "muteduration")
		{
			cfg->chatFlood.muteMinutes = std::atoi(value.c_str());
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
			cfg->tags.chatOwnership = (value != "0");
		}
		else if (k == "chattags")
		{
			cfg->tags.chatTags = (value != "0");
		}
		else if (k == "leaderboardtags")
		{
			cfg->tags.leaderboardTags = (value != "0");
		}
		else if (k == "format")
		{
			cfg->tags.format = value;
		}
		else if (k == "formatdead")
		{
			cfg->tags.formatDead = value;
		}
		else if (k == "formatteam")
		{
			cfg->tags.formatTeam = value;
		}
		else if (k == "formatteamdead")
		{
			cfg->tags.formatTeamDead = value;
		}
		else if (k == "teamcolorct")
		{
			cfg->tags.teamColorCT = ColorTag(value);
		}
		else if (k == "teamcolort")
		{
			cfg->tags.teamColorT = ColorTag(value);
		}
		else if (k == "teamcolorspec")
		{
			cfg->tags.teamColorSpec = ColorTag(value);
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
			cfg->menu.durations = value;
		}
		else if (k == "reasons")
		{
			cfg->menu.reasons = value;
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
	// Fresh, so a removed key reverts to its default and a file that fails validation leaves `config` as it was.
	CS2AConfig fresh;
	if (!kv::LoadFile(path, ConfigHandler, &fresh))
	{
		return false;
	}

	// Validate databasePrefix: only alphanumeric and underscore allowed
	for (char c : fresh.database.prefix)
	{
		if (!isalnum(static_cast<unsigned char>(c)) && c != '_')
		{
			MMU_LOG_ERROR("Invalid character '%c' in databasePrefix. Only alphanumeric and underscore allowed.\n", c);
			return false;
		}
	}
	if (fresh.database.prefix.empty())
	{
		MMU_LOG_ERROR("databasePrefix cannot be empty.\n");
		return false;
	}

	// Clamp chatFloodMaxMessages to sane range
	if (fresh.chatFlood.maxMessages < 1)
	{
		fresh.chatFlood.maxMessages = 1;
	}

	// Ensure command prefixes are different
	if (!fresh.general.commandPrefix.empty() && fresh.general.commandPrefix == fresh.general.silentCommandPrefix)
	{
		MMU_LOG_WARN("commandPrefix and silentCommandPrefix are the same ('%s'). Silent commands will not work. Resetting "
					 "silentCommandPrefix to '/'.\n",
					 fresh.general.commandPrefix.c_str());
		fresh.general.silentCommandPrefix = "/";
	}

	// A tag can only be injected into a line we render ourselves.
	if (fresh.tags.chatTags && !fresh.tags.chatOwnership)
	{
		MMU_LOG_WARN("Tags: ChatTags needs ChatOwnership to render the tag. Enabling ChatOwnership.\n");
		fresh.tags.chatOwnership = true;
	}

	config = fresh;
	mmu::config::ApplyLogBlock(config.log);
	return true;
}
