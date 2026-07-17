#include "foreign_plugins.h"

#include "mmu/kv_parser.h"
#include "mmu/log.h"

#include "src/common.h"
#include "src/config/config.h"

#include <tier1/convar.h>

#include <cstdio>
#include <string>

CS2AForeignPlugins g_CS2AForeignPlugins;

namespace
{
	const char *const kCs2kz = "cs2kz-metamod";

	// cs2kz registers no interface to ask, so probe a convar it always creates.
	// This one is unrelated to chat on purpose, it only answers "is cs2kz here".
	const char *const kCs2kzProbeConVar = "kz_profile_rating_badge_enabled";

	bool Cs2kzLoaded()
	{
		return g_pICvar && g_pICvar->FindConVar(kCs2kzProbeConVar).IsValidRef();
	}

	// What cs2kz's config says it takes over.
	// Both default on, which is what cs2kz defaults to, so an absent key has to mean on rather than off.
	struct Cs2kzOptions
	{
		bool overridesChat = true;
		bool overridesClantag = true;
	};

	void OptionHandler(const std::string &, const std::string &key, const std::string &value, void *userdata)
	{
		Cs2kzOptions *options = static_cast<Cs2kzOptions *>(userdata);
		const bool on = (value == "true" || value == "1");

		if (key == "overridePlayerChat")
		{
			options->overridesChat = on;
		}
		else if (key == "overridePlayerClantag")
		{
			options->overridesClantag = on;
		}
	}

	// Read what cs2kz is configured to take over.
	//
	// These are KeyValues options in its own config rather than convars, so reading the file is the only way to ask.
	// An unreadable file leaves both on, conceding chat and the clan tag rather than fighting it over them.
	Cs2kzOptions ReadCs2kzOptions()
	{
		char path[512];
		snprintf(path, sizeof(path), "%s/cfg/cs2kz-server-config.txt", g_SMAPI->GetBaseDir());

		Cs2kzOptions options;
		if (!kv::LoadFile(path, OptionHandler, &options))
		{
			MMU_LOG_WARN("%s is loaded but %s could not be read. Assuming it handles both chat and the clan tag.\n", kCs2kz, path);
		}
		return options;
	}
} // namespace

void CS2AForeignPlugins::Refresh()
{
	const char *previousChat = m_chatOwner;
	const char *previousClanTag = m_clanTagOwner;

	m_chatOwner = nullptr;
	m_clanTagOwner = nullptr;

	if (Cs2kzLoaded())
	{
		const Cs2kzOptions options = ReadCs2kzOptions();
		if (options.overridesChat)
		{
			m_chatOwner = kCs2kz;
		}
		if (options.overridesClantag)
		{
			m_clanTagOwner = kCs2kz;
		}
	}

	if (m_chatOwner != previousChat)
	{
		if (m_chatOwner && g_CS2AConfig.chatOwnership)
		{
			MMU_LOG_WARN("%s renders player chat (its overridePlayerChat is on). Set overridePlayerChat to false in its config to hand chat to us.\n",
						 m_chatOwner);
		}
		else if (!m_chatOwner && previousChat)
		{
			MMU_LOG_INFO("%s no longer renders chat. Chat tags follow TagsConfig again.\n", previousChat);
		}
	}

	if (m_clanTagOwner != previousClanTag)
	{
		if (m_clanTagOwner && g_CS2AConfig.boardTagsEnabled)
		{
			MMU_LOG_WARN("%s writes the scoreboard clan tag for its ranks, so leaderboard tags stay off. Set overridePlayerClantag to false in its "
						 "config to hand the clan tag to us.\n" m_clanTagOwner);
		}
		else if (!m_clanTagOwner && previousClanTag)
		{
			MMU_LOG_INFO("%s no longer writes the clan tag. Leaderboard tags follow TagsConfig again.\n", previousClanTag);
		}
	}
}
