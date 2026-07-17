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

	struct OptionSearch
	{
		const char *wanted;
		bool found = false;
		bool value = false;
	};

	void OptionHandler(const std::string &, const std::string &key, const std::string &value, void *userdata)
	{
		OptionSearch *search = static_cast<OptionSearch *>(userdata);
		if (search->found || key != search->wanted)
		{
			return;
		}
		search->found = true;
		search->value = (value == "true" || value == "1");
	}

	// Whether cs2kz is set to render player chat.
	//
	// overridePlayerChat is a KeyValues option in its own config rather than a convar, so reading the file is the only way to ask.
	// Missing file or missing key both mean on, matching the default cs2kz passes to GetOptionInt.
	bool Cs2kzOverridesChat()
	{
		char path[512];
		snprintf(path, sizeof(path), "%s/cfg/cs2kz-server-config.txt", g_SMAPI->GetBaseDir());

		OptionSearch search;
		search.wanted = "overridePlayerChat";

		if (!kv::LoadFile(path, OptionHandler, &search))
		{
			MMU_LOG_WARN("%s is loaded but %s could not be read. Assuming it renders chat.\n", kCs2kz, path);
			return true;
		}

		return search.found ? search.value : true;
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
		// It writes the clan tag whenever it's loaded, with nothing to turn that off.
		m_clanTagOwner = kCs2kz;
		if (Cs2kzOverridesChat())
		{
			m_chatOwner = kCs2kz;
		}
	}

	if (m_chatOwner != previousChat)
	{
		if (m_chatOwner && g_CS2AConfig.chatOwnership)
		{
			MMU_LOG_WARN("%s renders player chat (its overridePlayerChat is on), so ours stays off to avoid every message "
						 "appearing twice. Set overridePlayerChat to false in its config to hand chat to us.\n",
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
			MMU_LOG_WARN("%s writes the scoreboard clan tag for its ranks and cannot be told not to, so leaderboard tags "
						 "stay off. Both plugins writing it would leave whichever wrote last on screen.\n",
						 m_clanTagOwner);
		}
		else if (!m_clanTagOwner && previousClanTag)
		{
			MMU_LOG_INFO("%s unloaded. Leaderboard tags follow TagsConfig again.\n", previousClanTag);
		}
	}
}
