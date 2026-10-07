#include "discord_notify.h"
#include "utils/discord.h"
#include "utils/http_client.h"
#include "utils/json.h"
#include "utils/log.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/player/player_manager.h"
#include "version_gen.h"

#include "tier1/convar.h"

#include <cstdio>
#include <cstring>
#include <string>

CS2ADiscord g_CS2ADiscord;

// Player-controlled text as an inline code span.
// A backtick would close the span early and let the rest render as markdown, and a newline would start a fake field.
static std::string Code(const std::string &text)
{
	std::string out = "``";
	for (char c : text)
	{
		if (c == '`')
		{
			out += '\'';
		}
		else if (c == '\n' || c == '\r')
		{
			out += ' ';
		}
		else
		{
			out += c;
		}
	}
	return out + "``";
}

// Empty when the hostname is unset.
static std::string ServerLine()
{
	ConVarRefAbstract hn("hostname");
	if (!hn.IsConVarDataValid())
	{
		return "";
	}
	CUtlString name = hn.GetString();
	return name.Get() && *name.Get() ? "**Server:** " + Code(name.Get()) + "\n" : "";
}

// The SteamID is left out when 0.
static std::string PersonLine(const char *label, const char *name, uint64_t steamid64)
{
	std::string line = std::string("**") + label + ":** " + Code(name ? name : "");
	if (steamid64 != 0)
	{
		line += " - ``" + std::to_string(steamid64) + "``";
	}
	return line + "\n";
}

void CS2ADiscord::Init()
{
	mmu::http::SetUserAgent((std::string("CS2Admin/") + PLUGIN_FULL_VERSION).c_str());
	mmu::http::ResetShutdownLatch();
	MMU_LOG_INFO("Discord: webhook sender ready.\n");
}

void CS2ADiscord::Shutdown()
{
	mmu::http::Shutdown();
}

bool CS2ADiscord::IsEnabled() const
{
	return !g_CS2AConfig.discordWebhookUrl.empty();
}

void CS2ADiscord::SendEmbedMessage(const char *title, const char *description, int color, const char *footer)
{
	if (!IsEnabled())
	{
		return;
	}

	mmu::discord::SendEmbed(g_CS2AConfig.discordWebhookUrl, title, description, color, footer);
}

void CS2ADiscord::NotifyAdminAction(const char *adminName, const char *action, const char *targetName, const char *reason, int durationMinutes,
									uint64_t adminSteamid64, uint64_t targetSteamid64, const char *output)
{
	if (!IsEnabled())
	{
		return;
	}

	std::string desc = ServerLine();
	desc += PersonLine("Admin", adminName ? adminName : "Console", adminSteamid64);
	desc += "**Action:** " + Code(action ? action : "") + "\n";
	desc += PersonLine("Target", targetName, targetSteamid64);

	if (durationMinutes >= 0)
	{
		std::string dur = (durationMinutes == 0) ? "Permanent" : ADMIN_FormatDuration(-1, durationMinutes);
		desc += "**Duration:** ``" + dur + "``\n";
	}

	if (reason && *reason)
	{
		desc += "**Reason:** " + Code(reason) + "\n";
	}

	if (output && *output)
	{
		// Cap output to keep embed under Discord's 4096-char description limit,
		// and break any backtick fence sequences that could escape the code block.
		std::string capped(output);
		const size_t kMax = 1500;
		if (capped.size() > kMax)
		{
			// A cut in the middle of a UTF-8 sequence would make the whole JSON body invalid.
			ADMIN_TruncateUtf8(capped, kMax);
			capped += "\n... (truncated)";
		}
		size_t pos = 0;
		while ((pos = capped.find("```", pos)) != std::string::npos)
		{
			capped.replace(pos, 3, "''`");
			pos += 3;
		}
		desc += "**Output:**\n```\n" + capped + "\n```";
	}

	int color = 0xE74C3C; // red default

	if (action)
	{
		std::string act(action);
		if (act.find("Mute") != std::string::npos || act.find("Gag") != std::string::npos || act.find("Silence") != std::string::npos)
		{
			color = 0xE67E22; // orange
		}
		else if (act.find("Unmute") != std::string::npos || act.find("Ungag") != std::string::npos || act.find("Unsilence") != std::string::npos
				 || act.find("Unban") != std::string::npos)
		{
			color = 0x2ECC71; // green
		}
	}

	SendEmbedMessage("Admin Action", desc.c_str(), color, g_CS2AConfig.discordFooterText.c_str());
}

void CS2ADiscord::NotifyReport(const char *reporterName, const char *targetName, const char *reason, uint64_t reporterSteamid64,
							   uint64_t targetSteamid64)
{
	if (!IsEnabled())
	{
		return;
	}

	std::string desc = ServerLine();
	desc += PersonLine("Reporter", reporterName, reporterSteamid64);
	desc += PersonLine("Target", targetName, targetSteamid64);
	desc += "**Reason:** " + Code(reason ? reason : "") + "\n";

	SendEmbedMessage("Player Report", desc.c_str(), 0xF39C12, g_CS2AConfig.discordFooterText.c_str());
}
