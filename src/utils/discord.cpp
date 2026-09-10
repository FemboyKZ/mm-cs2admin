#include "discord.h"
#include "mmu/discord.h"
#include "mmu/http_client.h"
#include "mmu/json.h"
#include "mmu/log.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/player/player_manager.h"

#include "tier1/convar.h"

#include <cstdio>
#include <cstring>
#include <string>

CS2ADiscord g_CS2ADiscord;

void CS2ADiscord::Init()
{
	mmu::http::SetUserAgent("CS2Admin/1.0");
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

void CS2ADiscord::SendTextMessage(const char *content)
{
	if (!IsEnabled() || !content || !*content)
	{
		return;
	}

	mmu::discord::SendText(g_CS2AConfig.discordWebhookUrl, content);
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

	std::string desc;

	{
		ConVarRefAbstract hn("hostname");
		if (hn.IsConVarDataValid())
		{
			CUtlString s = hn.GetString();
			if (s.Get() && *s.Get())
			{
				desc += "**Server:** ``" + mmu::json::Escape(s.Get()) + "``\n";
			}
		}
	}

	desc += "**Admin:** ``" + mmu::json::Escape(adminName ? adminName : "Console") + "``";
	if (adminSteamid64 != 0)
	{
		desc += " - ``" + std::to_string(adminSteamid64) + "``";
	}
	desc += "\n";
	desc += "**Action:** ``" + mmu::json::Escape(action ? action : "") + "``\n";
	desc += "**Target:** ``" + mmu::json::Escape(targetName ? targetName : "") + "``";
	if (targetSteamid64 != 0)
	{
		desc += " - ``" + std::to_string(targetSteamid64) + "``";
	}
	desc += "\n";

	if (durationMinutes >= 0)
	{
		std::string dur = (durationMinutes == 0) ? "Permanent" : ADMIN_FormatDuration(durationMinutes);
		desc += "**Duration:** ``" + mmu::json::Escape(dur) + "``\n";
	}

	if (reason && *reason)
	{
		desc += "**Reason:** ``" + mmu::json::Escape(reason) + "``\n";
	}

	if (output && *output)
	{
		// Cap output to keep embed under Discord's 4096-char description limit,
		// and break any backtick fence sequences that could escape the code block.
		std::string capped(output);
		const size_t kMax = 1500;
		if (capped.size() > kMax)
		{
			capped.resize(kMax);
			capped += "\n... (truncated)";
		}
		size_t pos = 0;
		while ((pos = capped.find("```", pos)) != std::string::npos)
		{
			capped.replace(pos, 3, "''`");
			pos += 3;
		}
		desc += "**Output:**\n```\n" + mmu::json::Escape(capped) + "\n```";
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

	std::string desc;
	{
		ConVarRefAbstract hn("hostname");
		if (hn.IsConVarDataValid())
		{
			CUtlString s = hn.GetString();
			if (s.Get() && *s.Get())
			{
				desc += "**Server:** ``" + mmu::json::Escape(s.Get()) + "``\n";
			}
		}
	}
	desc += "**Reporter:** ``" + mmu::json::Escape(reporterName ? reporterName : "") + "``";
	if (reporterSteamid64 != 0)
	{
		desc += " - ``" + std::to_string(reporterSteamid64) + "``";
	}
	desc += "\n";
	desc += "**Target:** ``" + mmu::json::Escape(targetName ? targetName : "") + "``";
	if (targetSteamid64 != 0)
	{
		desc += " - ``" + std::to_string(targetSteamid64) + "``";
	}
	desc += "\n";
	desc += "**Reason:** ``" + mmu::json::Escape(reason ? reason : "") + "``\n";

	SendEmbedMessage("Player Report", desc.c_str(), 0xF39C12, g_CS2AConfig.discordFooterText.c_str());
}

void CS2ADiscord::SendPayload(const std::string &json)
{
	mmu::discord::SendPayload(g_CS2AConfig.discordWebhookUrl, json);
}
