#include "tag_manager.h"

#include "mmu/entity/ccsplayercontroller.h"
#include "mmu/kv_parser.h"
#include "mmu/log.h"

#include "src/admin/admin_manager.h"
#include "src/compat/foreign_plugins.h"
#include "src/config/config.h"
#include "src/db/database.h"
#include "src/player/player_manager.h"
#include "src/queue/offline_queue.h"

#include <sql_mm.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

CS2ATagManager g_CS2ATagManager;

bool ADMIN_ChatTagsActive()
{
	return g_CS2AConfig.chatTagsEnabled && !g_CS2AForeignPlugins.ChatOwner();
}

bool ADMIN_BoardTagsActive()
{
	return g_CS2AConfig.boardTagsEnabled && !g_CS2AForeignPlugins.ClanTagOwner();
}

namespace
{
	std::string ToLower(const std::string &s)
	{
		std::string out = s;
		std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	// A tag as it appears in the file, before the match value can be interpreted.
	// Keys arrive one at a time and in any order, so "value" may land before the "match" that decides how to read it.
	// Everything stays text until the section is complete.
	struct RawTag
	{
		std::string id;
		std::string match;
		std::string value;
		std::string priority;
		std::string chatTag;
		std::string nameColor;
		std::string msgColor;
		std::string boardTag;
	};

	struct TagLoadCtx
	{
		std::string rootSection;
		std::vector<RawTag> raw;
	};

	RawTag &FindOrCreate(TagLoadCtx &ctx, const std::string &id)
	{
		for (RawTag &t : ctx.raw)
		{
			if (t.id == id)
			{
				return t;
			}
		}
		ctx.raw.push_back(RawTag {});
		ctx.raw.back().id = id;
		return ctx.raw.back();
	}

	void TagHandler(const std::string &section, const std::string &key, const std::string &value, void *userdata)
	{
		TagLoadCtx *ctx = static_cast<TagLoadCtx *>(userdata);

		// Keys sitting directly under the root aren't part of any tag.
		if (section == ctx->rootSection)
		{
			MMU_LOG_WARN("tags.cfg: ignoring '%s' outside of a tag section.\n", key.c_str());
			return;
		}

		RawTag &tag = FindOrCreate(*ctx, section);
		std::string k = ToLower(key);

		if (k == "match")
		{
			tag.match = ToLower(value);
		}
		else if (k == "value")
		{
			tag.value = value;
		}
		else if (k == "priority")
		{
			tag.priority = value;
		}
		else if (k == "chattag")
		{
			tag.chatTag = value;
		}
		else if (k == "namecolor")
		{
			tag.nameColor = value;
		}
		else if (k == "messagecolor")
		{
			tag.msgColor = value;
		}
		else if (k == "boardtag")
		{
			tag.boardTag = value;
		}
		else
		{
			MMU_LOG_WARN("tags.cfg: unknown key '%s' in tag '%s'.\n", key.c_str(), section.c_str());
		}
	}

	// Turn a RawTag into a TagDef, reading `value` the way `match` asks for.
	// Returns false and explains itself when the tag can never match anything.
	bool FinalizeTag(const RawTag &raw, TagDef &out)
	{
		out.id = raw.id;
		out.priority = std::atoi(raw.priority.c_str());
		out.chatTag = mmu::ResolveColorTags(raw.chatTag);
		out.nameColor = mmu::ResolveColorTags(raw.nameColor);
		out.msgColor = mmu::ResolveColorTags(raw.msgColor);
		out.boardTag = raw.boardTag;

		if (raw.match == "steamid")
		{
			out.match = TagMatch::SteamID;
			std::string normalized = CS2AAdminManager::NormalizeSteamID(raw.value.c_str());
			out.steamid64 = normalized.empty() ? 0 : CS2AAdminManager::AuthIdToSteamID64(normalized.c_str());
			if (out.steamid64 == 0)
			{
				MMU_LOG_WARN("tags.cfg: tag '%s' has an unreadable SteamID '%s', skipping.\n", raw.id.c_str(), raw.value.c_str());
				return false;
			}
		}
		else if (raw.match == "ip")
		{
			out.match = TagMatch::IP;
			out.ip = raw.value;
			if (out.ip.empty())
			{
				MMU_LOG_WARN("tags.cfg: tag '%s' matches on ip but has no value, skipping.\n", raw.id.c_str());
				return false;
			}
		}
		else if (raw.match == "flag")
		{
			out.match = TagMatch::Flag;
			out.flags = CS2AAdminManager::FlagsFromString(raw.value.c_str());
			if (out.flags == 0)
			{
				MMU_LOG_WARN("tags.cfg: tag '%s' has no usable flags in '%s', skipping.\n", raw.id.c_str(), raw.value.c_str());
				return false;
			}
		}
		else if (raw.match == "group")
		{
			out.match = TagMatch::Group;
			out.group = raw.value;
			if (out.group.empty())
			{
				MMU_LOG_WARN("tags.cfg: tag '%s' matches on group but has no value, skipping.\n", raw.id.c_str());
				return false;
			}
		}
		else if (raw.match == "immunity")
		{
			out.match = TagMatch::Immunity;
			out.immunity = std::atoi(raw.value.c_str());
		}
		else
		{
			MMU_LOG_WARN("tags.cfg: tag '%s' has unknown match '%s'. Use steamid, ip, flag, group or immunity. Skipping.\n", raw.id.c_str(),
						 raw.match.c_str());
			return false;
		}

		if (out.chatTag.empty() && out.boardTag.empty() && out.nameColor.empty() && out.msgColor.empty())
		{
			MMU_LOG_WARN("tags.cfg: tag '%s' sets nothing to display, skipping.\n", raw.id.c_str());
			return false;
		}

		return true;
	}
} // namespace

void CS2ATagManager::LoadTags()
{
	m_tags.clear();

	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/cs2admin/tags.cfg", g_SMAPI->GetBaseDir());

	std::ifstream file(path);
	if (!file.is_open())
	{
		// Only worth complaining about when the server actually asked for tags.
		if (g_CS2AConfig.chatTagsEnabled || g_CS2AConfig.boardTagsEnabled)
		{
			MMU_LOG_WARN("Tags are enabled but %s could not be opened. No tags loaded.\n", path);
		}
		return;
	}

	kv::Token root = kv::NextToken(file);
	if (root.kind != kv::TokenType::String)
	{
		MMU_LOG_ERROR("tags.cfg: expected a root section name.\n");
		return;
	}
	kv::Token brace = kv::NextToken(file);
	if (brace.kind != kv::TokenType::OpenBrace)
	{
		MMU_LOG_ERROR("tags.cfg: expected '{' after '%s'.\n", root.value.c_str());
		return;
	}

	TagLoadCtx ctx;
	ctx.rootSection = root.value;
	kv::ParseSection(file, root.value, TagHandler, &ctx);

	for (const RawTag &raw : ctx.raw)
	{
		TagDef def;
		if (FinalizeTag(raw, def))
		{
			m_tags.push_back(def);
		}
	}

	// Resolve reads these in order and takes the first match, so sort once here rather than on every chat line.
	// Equal priorities keep their file order.
	std::stable_sort(m_tags.begin(), m_tags.end(), [](const TagDef &a, const TagDef &b) { return a.priority > b.priority; });

	MMU_LOG_INFO("Loaded %d tag(s) from tags.cfg\n", static_cast<int>(m_tags.size()));
}

bool CS2ATagManager::PlayerMatches(int slot, const TagDef &tag) const
{
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (!player || !player->connected)
	{
		return false;
	}

	// Identity matches don't need an admin entry.
	if (tag.match == TagMatch::SteamID)
	{
		return player->steamid64 == tag.steamid64;
	}
	if (tag.match == TagMatch::IP)
	{
		return player->ip == tag.ip;
	}

	const AdminEntry *admin = g_CS2AAdminManager.GetPlayerAdmin(slot);
	if (!admin)
	{
		return false;
	}

	switch (tag.match)
	{
		case TagMatch::Flag:
			// Holding any one of the listed flags is enough, and root passes everything.
			return CS2AAdminManager::HasFlag(admin->flags, tag.flags);
		case TagMatch::Group:
			return ToLower(admin->group) == ToLower(tag.group);
		case TagMatch::Immunity:
			return admin->immunity >= tag.immunity;
		default:
			return false;
	}
}

std::vector<const TagDef *> CS2ATagManager::EligibleFor(int slot) const
{
	std::vector<const TagDef *> out;
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return out;
	}

	for (const TagDef &tag : m_tags)
	{
		if (PlayerMatches(slot, tag))
		{
			out.push_back(&tag);
		}
	}
	return out;
}

const TagDef *CS2ATagManager::FindTag(const char *id) const
{
	if (!id || !*id)
	{
		return nullptr;
	}
	for (const TagDef &tag : m_tags)
	{
		if (ToLower(tag.id) == ToLower(id))
		{
			return &tag;
		}
	}
	return nullptr;
}

const TagDef *CS2ATagManager::Resolve(int slot) const
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return nullptr;
	}
	if (m_tagDisabled[slot])
	{
		return nullptr;
	}

	if (!m_selected[slot].empty())
	{
		const TagDef *picked = FindTag(m_selected[slot].c_str());
		// A pick can go stale when tags.cfg changes or admin access is revoked.
		// Fall through to the priority default rather than showing nothing.
		if (picked && PlayerMatches(slot, *picked))
		{
			return picked;
		}
	}

	// m_tags is kept sorted by descending priority, so the first match wins.
	for (const TagDef &tag : m_tags)
	{
		if (PlayerMatches(slot, tag))
		{
			return &tag;
		}
	}
	return nullptr;
}

bool CS2ATagManager::SelectTag(int slot, const char *id)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return false;
	}

	// Empty id means "show nothing", which is a real choice and not the same as never having picked.
	if (!id || !*id)
	{
		m_selected[slot].clear();
		m_tagDisabled[slot] = true;
		SavePlayerPref(slot);
		UpdateClanTag(slot);
		return true;
	}

	const TagDef *tag = FindTag(id);
	if (!tag || !PlayerMatches(slot, *tag))
	{
		return false;
	}

	m_selected[slot] = tag->id;
	m_tagDisabled[slot] = false;
	SavePlayerPref(slot);
	UpdateClanTag(slot);
	return true;
}

void CS2ATagManager::UpdateClanTag(int slot)
{
	// ADMIN_BoardTagsActive is false when another plugin owns the clan tag.
	if (!ADMIN_BoardTagsActive() || slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}

	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (!player || !player->connected || player->fakePlayer)
	{
		return;
	}

	CCSPlayerController *controller = CCSPlayerController::FromSlot(slot);
	if (!controller)
	{
		return;
	}

	const TagDef *tag = Resolve(slot);
	const char *text = (tag && !tag->boardTag.empty()) ? tag->boardTag.c_str() : "";

	// m_clanTag is what the engine ends up holding a pointer to,
	// so the string has to live here rather than in the TagDef, which a reload would free.
	snprintf(m_clanTag[slot], sizeof(m_clanTag[slot]), "%s", text);
	controller->SetClan(m_clanTag[slot]);
}

void CS2ATagManager::UpdateAllClanTags()
{
	for (int slot = 0; slot <= MAXPLAYERS; slot++)
	{
		UpdateClanTag(slot);
	}
}

void CS2ATagManager::OnClientDisconnect(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	m_selected[slot].clear();
	m_tagDisabled[slot] = false;
	m_clanTag[slot][0] = '\0';
}

void CS2ATagManager::EnsureSchema()
{
	if (!g_CS2ADatabase.IsConnected())
	{
		return;
	}

	const char *prefix = g_CS2AConfig.databasePrefix.c_str();
	char query[1024];

	// Ours rather than SBPP's, so it has to be created on both backends.
	if (g_CS2ADatabase.IsSQLite())
	{
		snprintf(query, sizeof(query),
				 "CREATE TABLE IF NOT EXISTS %s_cs2a_tagprefs ("
				 "steamid64 INTEGER PRIMARY KEY, "
				 "tag_id TEXT NOT NULL DEFAULT '', "
				 "disabled INTEGER NOT NULL DEFAULT 0"
				 ")",
				 prefix);
	}
	else
	{
		snprintf(query, sizeof(query),
				 "CREATE TABLE IF NOT EXISTS %s_cs2a_tagprefs ("
				 "steamid64 BIGINT UNSIGNED NOT NULL PRIMARY KEY, "
				 "tag_id VARCHAR(64) NOT NULL DEFAULT '', "
				 "disabled TINYINT NOT NULL DEFAULT 0"
				 ")",
				 prefix);
	}

	g_CS2ADatabase.Query(query, [](ISQLQuery *) {});
}

void CS2ATagManager::LoadPlayerPref(int slot, uint64_t steamid64)
{
	if (slot < 0 || slot > MAXPLAYERS || steamid64 == 0)
	{
		return;
	}
	if (!g_CS2ADatabase.IsConnected())
	{
		return;
	}

	g_CS2ADatabase.QueryFmt(
		[slot, steamid64](ISQLQuery *result)
		{
			if (!result)
			{
				return;
			}
			ISQLResult *rs = result->GetResultSet();
			if (!rs || !rs->MoreRows() || !rs->FetchRow())
			{
				return;
			}

			// The slot may have been recycled by another player while the query was in flight,
			// so confirm it still holds the one we asked about.
			PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
			if (!player || !player->connected || player->steamid64 != steamid64)
			{
				return;
			}

			g_CS2ATagManager.ApplyLoadedPref(slot, rs->GetString(0), rs->GetInt(1) != 0);
		},
		"SELECT tag_id, disabled FROM %s_cs2a_tagprefs WHERE steamid64 = %llu", g_CS2AConfig.databasePrefix.c_str(), (unsigned long long)steamid64);
}

void CS2ATagManager::ApplyLoadedPref(int slot, const char *tagId, bool disabled)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	m_selected[slot] = tagId ? tagId : "";
	m_tagDisabled[slot] = disabled;
	UpdateClanTag(slot);
}

void CS2ATagManager::SavePlayerPref(int slot)
{
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	if (!player || !player->connected || player->fakePlayer || player->steamid64 == 0)
	{
		return;
	}

	const char *prefix = g_CS2AConfig.databasePrefix.c_str();
	std::string escapedId = g_CS2ADatabase.Escape(m_selected[slot].c_str());

	char query[1024];
	if (g_CS2ADatabase.IsSQLite())
	{
		snprintf(query, sizeof(query),
				 "INSERT INTO %s_cs2a_tagprefs (steamid64, tag_id, disabled) VALUES (%llu, '%s', %d) "
				 "ON CONFLICT(steamid64) DO UPDATE SET tag_id = excluded.tag_id, disabled = excluded.disabled",
				 prefix, (unsigned long long)player->steamid64, escapedId.c_str(), m_tagDisabled[slot] ? 1 : 0);
	}
	else
	{
		snprintf(query, sizeof(query),
				 "INSERT INTO %s_cs2a_tagprefs (steamid64, tag_id, disabled) VALUES (%llu, '%s', %d) "
				 "ON DUPLICATE KEY UPDATE tag_id = VALUES(tag_id), disabled = VALUES(disabled)",
				 prefix, (unsigned long long)player->steamid64, escapedId.c_str(), m_tagDisabled[slot] ? 1 : 0);
	}

	if (!g_CS2ADatabase.IsConnected())
	{
		g_CS2AOfflineQueue.Enqueue(query);
		return;
	}
	g_CS2ADatabase.Query(query, nullptr);
}
