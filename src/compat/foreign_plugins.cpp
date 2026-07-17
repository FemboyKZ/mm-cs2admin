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

	struct ChatOptionSearch
	{
		bool found = false;
		bool value = false;
	};

	void ChatOptionHandler(const std::string &, const std::string &key, const std::string &value, void *userdata)
	{
		if (key != "overridePlayerChat")
		{
			return;
		}
		ChatOptionSearch *search = static_cast<ChatOptionSearch *>(userdata);
		search->found = true;
		search->value = (value == "true" || value == "1");
	}

	// Whether cs2kz is set to render player chat.
	// overridePlayerChat is a KeyValues option in its config, not a convar, so reading the file is the only way to ask.
	// Missing file or missing key both mean on, matching the default cs2kz itself uses.
	bool Cs2kzOverridesChat()
	{
		char path[512];
		snprintf(path, sizeof(path), "%s/cfg/cs2kz-server-config.txt", g_SMAPI->GetBaseDir());

		ChatOptionSearch search;
		if (!kv::LoadFile(path, ChatOptionHandler, &search))
		{
			MMU_LOG_WARN("%s is loaded but %s could not be read. Assuming it renders chat.\n", kCs2kz, path);
			return true;
		}
		return search.found ? search.value : true;
	}

	// Whether cs2kz is set to write the scoreboard clan tag.
	bool Cs2kzOverridesClantag()
	{
		if (!g_pICvar)
		{
			return true;
		}
		ConVarRefAbstract ref("kz_profile_clantag_enabled");
		if (!ref.IsValidRef())
		{
			return true;
		}
		return ref.GetBool();
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
		if (Cs2kzOverridesChat())
		{
			m_chatOwner = kCs2kz;
		}
		if (Cs2kzOverridesClantag())
		{
			m_clanTagOwner = kCs2kz;
		}
	}

	if (m_chatOwner != previousChat)
	{
		if (m_chatOwner && g_CS2AConfig.chatOwnership)
		{
			MMU_LOG_WARN("%s renders player chat. Set overridePlayerChat to false in its config to disable it.\n", m_chatOwner);
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
			MMU_LOG_WARN("%s writes the scoreboard clan tag for its ranks, so leaderboard tags stay off. Set the "
						 "kz_profile_clantag_enabled convar to 0 to disable it.\n",
						 m_clanTagOwner);
		}
		else if (!m_clanTagOwner && previousClanTag)
		{
			MMU_LOG_INFO("%s no longer writes the clan tag. Leaderboard tags follow TagsConfig again.\n", previousClanTag);
		}
	}
}
