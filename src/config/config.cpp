#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

CS2AConfig g_CS2AConfig;

// Minimal Valve KeyValues parser, handles nested sections and key-value pairs.
// Only supports the subset used by core.cfg.

enum class TokenType
{
	String,
	OpenBrace,
	CloseBrace,
	EndOfFile
};

struct Token
{
	TokenType kind;
	std::string value;
};

static Token NextToken(std::istream &in)
{
	Token tok;
	while (in.good())
	{
		int ch = in.get();
		if (ch == EOF)
		{
			tok.kind = TokenType::EndOfFile;
			return tok;
		}

		// Skip whitespace
		if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
			continue;

		// Skip single-line comments
		if (ch == '/')
		{
			int next = in.peek();
			if (next == '/')
			{
				// Consume until end of line
				while (in.good() && in.get() != '\n')
					;
				continue;
			}
		}

		if (ch == '{')
		{
			tok.kind = TokenType::OpenBrace;
			return tok;
		}
		if (ch == '}')
		{
			tok.kind = TokenType::CloseBrace;
			return tok;
		}

		// Quoted string
		if (ch == '"')
		{
			tok.kind = TokenType::String;
			tok.value.clear();
			while (in.good())
			{
				ch = in.get();
				if (ch == '"' || ch == EOF)
					break;
				if (ch == '\\')
				{
					int esc = in.get();
					if (esc == '"')
						tok.value += '"';
					else if (esc == '\\')
						tok.value += '\\';
					else if (esc == 'n')
						tok.value += '\n';
					else if (esc == 't')
						tok.value += '\t';
					else
					{
						tok.value += '\\';
						tok.value += static_cast<char>(esc);
					}
				}
				else
				{
					tok.value += static_cast<char>(ch);
				}
			}
			return tok;
		}

		// Unquoted string (token until whitespace or brace)
		tok.kind = TokenType::String;
		tok.value.clear();
		tok.value += static_cast<char>(ch);
		while (in.good())
		{
			int next = in.peek();
			if (next == ' ' || next == '\t' || next == '\r' || next == '\n' ||
				next == '{' || next == '}' || next == '"' || next == EOF)
				break;
			tok.value += static_cast<char>(in.get());
		}
		return tok;
	}

	tok.kind = TokenType::EndOfFile;
	return tok;
}

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
typedef void (*KVHandler)(const std::string &section, const std::string &key,
	const std::string &value, void *userdata);

static bool ParseSection(std::istream &in, const std::string &sectionName,
	KVHandler handler, void *userdata)
{
	while (true)
	{
		Token tok = NextToken(in);
		if (tok.kind == TokenType::CloseBrace || tok.kind == TokenType::EndOfFile)
			return true;

		if (tok.kind != TokenType::String)
			continue;

		std::string key = tok.value;
		Token next = NextToken(in);

		if (next.kind == TokenType::OpenBrace)
		{
			// Subsection
			ParseSection(in, key, handler, userdata);
		}
		else if (next.kind == TokenType::String)
		{
			// Keyvalue pair
			handler(sectionName, key, next.value, userdata);
		}
	}
}

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
}

bool ADMIN_LoadConfig(const char *path, CS2AConfig &config)
{
	std::ifstream file(path);
	if (!file.is_open())
		return false;

	// Expect: "cs2admin" { ... }
	Token root = NextToken(file);
	if (root.kind != TokenType::String)
		return false;

	Token brace = NextToken(file);
	if (brace.kind != TokenType::OpenBrace)
		return false;

	ParseSection(file, root.value, ConfigHandler, &config);
	return true;
}

std::string ADMIN_ResolveColorTags(const std::string &input)
{
	struct ColorTag { const char *tag; const char *code; };
	static const ColorTag tags[] = {
		{ "{default}",  "\x01" },
		{ "{red}",      "\x02" },
		{ "{team}",     "\x03" },
		{ "{green}",    "\x04" },
		{ "{olive}",    "\x05" },
		{ "{lime}",     "\x06" },
		{ "{gold}",     "\x09" },
		{ "{grey}",     "\x0A" },
		{ "{blue}",     "\x0C" },
		{ "{purple}",   "\x10" },
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
