#include "config.h"
#include "kv_parser.h"
#include "../common.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

CS2AConfig g_CS2AConfig;

static std::string ToLower(const std::string &s)
{
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

// Parse a section body (after the opening brace).
// Calls handler(sectionName, key, value) for each keyvalue pair.
// Recurses into subsections.
typedef kv::Handler KVHandler;

static void ConfigHandler(const std::string &section, const std::string &key,
	const std::string &value, void *userdata)
{
	CS2AConfig *cfg = static_cast<CS2AConfig *>(userdata);
	std::string sec = ToLower(section);
	std::string k = ToLower(key);

	if (sec == "config")
	{
		if (k == "website")
			cfg->website = value;
		else if (k == "chatprefix")
			cfg->chatPrefix = ADMIN_ResolveColorTags(value);
		else if (k == "commandprefix")
			cfg->commandPrefix = value;
		else if (k == "silentcommandprefix")
			cfg->silentCommandPrefix = value;
		else if (k == "databaseprefix")
			cfg->databasePrefix = value;
		else if (k == "addban")
			cfg->addban = (value != "0");
		else if (k == "unban")
			cfg->unban = (value != "0");
		else if (k == "retrytime")
		{
			float v = static_cast<float>(std::atof(value.c_str()));
			if (v < 15.0f) v = 15.0f;
			if (v > 60.0f) v = 60.0f;
			cfg->retryTime = v;
		}
		else if (k == "processqueuetime")
			cfg->processQueueTime = std::atoi(value.c_str());
		else if (k == "autoaddserver")
			cfg->autoAddServer = std::atoi(value.c_str());
		else if (k == "backupconfigs")
			cfg->backupConfigs = (value != "0");
		else if (k == "enableadmins")
			cfg->enableAdmins = (value != "0");
		else if (k == "requiresitelogin")
			cfg->requireSiteLogin = (value != "0");
		else if (k == "serverid")
			cfg->serverID = std::atoi(value.c_str());
	}
	else if (sec == "database")
	{
		if (k == "type")
			cfg->dbType = value;
		else if (k == "host")
			cfg->dbHost = value;
		else if (k == "user")
			cfg->dbUser = value;
		else if (k == "pass")
			cfg->dbPass = value;
		else if (k == "database" || k == "name")
			cfg->dbName = value;
		else if (k == "port")
			cfg->dbPort = std::atoi(value.c_str());
		else if (k == "path")
			cfg->dbPath = value;
	}
	else if (sec == "commsconfig")
	{
		if (k == "defaulttime")
			cfg->commsDefaultTime = std::atoi(value.c_str());
		else if (k == "disableunblockimmunitycheck")
			cfg->disableUnblockImmunityCheck = (value != "0");
		else if (k == "consoleimmunity")
			cfg->consoleImmunity = std::atoi(value.c_str());
		else if (k == "maxlength")
			cfg->commsMaxLength = std::atoi(value.c_str());
	}
	else if (sec == "checkerconfig")
	{
		if (k == "printcheckonconnect")
			cfg->printCheckOnConnect = (value != "0");
	}
	else if (sec == "reportconfig")
	{
		if (k == "cooldown")
			cfg->reportCooldown = static_cast<float>(std::atof(value.c_str()));
		else if (k == "minlength")
			cfg->reportMinLength = std::atoi(value.c_str());
	}
	else if (sec == "sleuthconfig")
	{
		if (k == "actions")
			cfg->sleuthActions = std::atoi(value.c_str());
		else if (k == "duration")
			cfg->sleuthDuration = std::atoi(value.c_str());
		else if (k == "bansallowed")
			cfg->sleuthBansAllowed = std::atoi(value.c_str());
		else if (k == "bantype")
			cfg->sleuthBanType = std::atoi(value.c_str());
		else if (k == "adminbypass")
			cfg->sleuthAdminBypass = (value != "0");
		else if (k == "excludeold")
			cfg->sleuthExcludeOld = (value != "0");
		else if (k == "excludetime")
			cfg->sleuthExcludeTime = std::atoi(value.c_str());
	}
	else if (sec == "discordconfig")
	{
		if (k == "webhookurl")
			cfg->discordWebhookUrl = value;
		else if (k == "footertext")
			cfg->discordFooterText = value;
	}
	else if (sec == "chatfloodconfig")
	{
		if (k == "cooldown")
			cfg->chatFloodCooldown = static_cast<float>(std::atof(value.c_str()));
		else if (k == "maxmessages")
			cfg->chatFloodMaxMessages = std::atoi(value.c_str());
		else if (k == "muteduration")
			cfg->chatFloodMuteDuration = std::atoi(value.c_str());
	}
}

bool ADMIN_LoadConfig(const char *path, CS2AConfig &config)
{
	std::ifstream file(path);
	if (!file.is_open())
		return false;

	// Expect: "cs2admin" { ... }
	kv::Token root = kv::NextToken(file);
	if (root.kind != kv::TokenType::String)
		return false;

	kv::Token brace = kv::NextToken(file);
	if (brace.kind != kv::TokenType::OpenBrace)
		return false;

	kv::ParseSection(file, root.value, ConfigHandler, &config);

	// Validate databasePrefix: only alphanumeric and underscore allowed
	for (char c : config.databasePrefix)
	{
		if (!isalnum(static_cast<unsigned char>(c)) && c != '_')
		{
			META_CONPRINTF("[ADMIN] ERROR: Invalid character '%c' in databasePrefix. Only alphanumeric and underscore allowed.\n", c);
			return false;
		}
	}
	if (config.databasePrefix.empty())
	{
		META_CONPRINTF("[ADMIN] ERROR: databasePrefix cannot be empty.\n");
		return false;
	}

	// Clamp chatFloodMaxMessages to sane range
	if (config.chatFloodMaxMessages < 1)
		config.chatFloodMaxMessages = 1;

	// Ensure command prefixes are different
	if (!config.commandPrefix.empty() && config.commandPrefix == config.silentCommandPrefix)
	{
		META_CONPRINTF("[ADMIN] WARNING: commandPrefix and silentCommandPrefix are the same ('%s'). Silent commands will not work. Resetting silentCommandPrefix to '/'.\n", config.commandPrefix.c_str());
		config.silentCommandPrefix = "/";
	}

	return true;
}

std::string ADMIN_ResolveColorTags(const std::string &input)
{
	struct ColorTag { const char *tag; const char *code; };
	static const ColorTag tags[] = {
		{ "{default}",  "\x01" },
		{ "{darkred}",  "\x02" },
		{ "{purple}",   "\x03" },
		{ "{green}",    "\x04" },
		{ "{olive}",    "\x05" },
		{ "{lime}",     "\x06" },
		{ "{red}",      "\x07" },
		{ "{grey}",     "\x08" },
		{ "{yellow}",   "\x09" },
		{ "{bluegrey}", "\x0A" },
		{ "{blue}",     "\x0B" },
		{ "{darkblue}", "\x0C" },
		{ "{grey2}",    "\x0D" },
		{ "{orchid}",   "\x0E" },
		{ "{lightred}", "\x0F" },
		{ "{gold}",     "\x10" },
	};

	std::string result = input;
	for (const auto &t : tags)
	{
		std::string tag(t.tag);
		size_t pos = 0;
		while ((pos = result.find(tag, pos)) != std::string::npos)
			result.replace(pos, tag.size(), t.code);
	}
	return result;
}
