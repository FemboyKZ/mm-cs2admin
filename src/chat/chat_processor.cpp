#include "chat_processor.h"

#include "mmu/chat_colors.h"
#include "mmu/entity/ccsplayercontroller.h"
#include "mmu/log.h"
#include "mmu/print.h"
#include "mmu/recipient_filter.h"

#include "src/compat/foreign_plugins.h"
#include "src/config/config.h"
#include "src/player/player_manager.h"
#include "src/tags/tag_manager.h"
#include "src/utils/print_utils.h"

#include <networksystem/inetworkmessages.h>
#include <networksystem/inetworkserializer.h>
#include <networksystem/netmessage.h>
#include <usermessages.pb.h>

#include <string>
#include <vector>

CS2AChatProcessor g_CS2AChatProcessor;

namespace
{
	struct FormatToken
	{
		const char *name;
		std::string value;
	};

	// Expand every {token} in `format` in one left-to-right pass. Single pass on purpose.
	std::string ExpandTokens(const std::string &format, const std::vector<FormatToken> &tokens)
	{
		std::string out;
		out.reserve(format.size() + 64);

		for (size_t i = 0; i < format.size();)
		{
			if (format[i] != '{')
			{
				out += format[i++];
				continue;
			}

			const size_t end = format.find('}', i);
			if (end == std::string::npos)
			{
				out += format[i++];
				continue;
			}

			const std::string name = format.substr(i + 1, end - i - 1);
			const FormatToken *match = nullptr;
			for (const FormatToken &token : tokens)
			{
				if (name == token.name)
				{
					match = &token;
					break;
				}
			}

			if (!match)
			{
				// Not one of ours. Leave it as typed rather than eating it.
				out += format[i++];
				continue;
			}

			out += match->value;
			i = end + 1;
		}

		return out;
	}

	// CS2 rewrites a quote the player typed into U+200B before it reaches the server.
	// The game's own chat renders it back as a quote, so a line we render has to do the same or every quote shows up as an invisible character.
	void RestoreTypedQuotes(std::string &text)
	{
		const std::string zeroWidthSpace = "\xE2\x80\x8B";
		size_t pos = 0;
		while ((pos = text.find(zeroWidthSpace, pos)) != std::string::npos)
		{
			text.replace(pos, zeroWidthSpace.size(), "\"");
			pos += 1;
		}
	}

	// The palette color standing in for a team, straight from config.
	std::string TeamColor(int team)
	{
		switch (team)
		{
			case CS_TEAM_CT:
				return mmu::ResolveColorTags(g_CS2AConfig.teamColorCT);
			case CS_TEAM_T:
				return mmu::ResolveColorTags(g_CS2AConfig.teamColorT);
			default:
				return mmu::ResolveColorTags(g_CS2AConfig.teamColorSpec);
		}
	}

	const char *TeamName(int team)
	{
		switch (team)
		{
			case CS_TEAM_CT:
				return "Counter-Terrorist";
			case CS_TEAM_T:
				return "Terrorist";
			case CS_TEAM_SPECTATOR:
				return "Spectator";
			default:
				return "Unassigned";
		}
	}

	int PlayerTeam(int slot)
	{
		CCSPlayerController *controller = CCSPlayerController::FromSlot(slot);
		return controller ? controller->m_iTeamNum() : CS_TEAM_NONE;
	}

	bool PlayerIsAlive(int slot)
	{
		CCSPlayerController *controller = CCSPlayerController::FromSlot(slot);
		return controller && controller->m_bPawnIsAlive();
	}
} // namespace

bool CS2AChatProcessor::ShouldRender(int slot) const
{
	if (!g_CS2AConfig.chatOwnership || g_CS2AForeignPlugins.ChatOwner())
	{
		return false;
	}

	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	return player && player->connected && !player->fakePlayer;
}

void CS2AChatProcessor::BeginSay(int slot, const char *message, bool teamOnly)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	m_slot = slot;
	m_teamOnly = teamOnly;
	m_message = message ? message : "";
	m_rendered = false;
	m_strippedLine.clear();
	m_sentTo.ClearAll();
}

void CS2AChatProcessor::EndSay(int slot)
{
	if (slot < 0 || slot != m_slot)
	{
		return;
	}
	// Server console copy, for admins watching and for logs. Once per say, however many events the game split it into.
	if (m_rendered)
	{
		ADMIN_PrintToClient(-1, "%s\n", m_strippedLine.c_str());
	}
	else
	{
		// Expected when a plugin superseded the say. For plain chat it means the game no longer posts a chat line we recognize.
		MMU_LOG_INFO("Say from slot %d finished with no game chat line replaced: \"%s\"\n", slot, m_message.c_str());
	}
	m_slot = -1;
}

int CS2AChatProcessor::MatchGameLine(INetworkMessageInternal *event, const CNetMessage *data) const
{
	if (m_slot < 0 || !event || !data)
	{
		return -1;
	}

	// Plugin lines carry the whole text in messagename or text with no sender, the game's carry the sender and message as params.
	// Same test cs2kz uses to block the game's line.
	NetworkMessageId id = event->GetNetMessageInfo()->m_MessageId;
	if (id == UM_SayText2)
	{
		auto *sayText = const_cast<CNetMessage *>(data)->ToPB<CUserMessageSayText2>();
		MMU_LOG_INFO("SayText2 during say from slot %d: entityindex=%d chat=%d messagename=\"%s\" param1=\"%s\" param2=\"%s\"\n", m_slot,
					 sayText->entityindex(), sayText->chat(), sayText->messagename().c_str(), sayText->param1().c_str(), sayText->param2().c_str());
		if (sayText->entityindex() != -1 && (!sayText->param1().empty() || !sayText->param2().empty()))
		{
			return m_slot;
		}
	}
	else if (id == UM_SayText)
	{
		auto *sayText = const_cast<CNetMessage *>(data)->ToPB<CUserMessageSayText>();
		MMU_LOG_INFO("SayText during say from slot %d: playerindex=%d chat=%d text=\"%s\"\n", m_slot, sayText->playerindex(), sayText->chat(),
					 sayText->text().c_str());
		if (sayText->playerindex() != -1)
		{
			return m_slot;
		}
	}
	return -1;
}

void CS2AChatProcessor::RenderPending(int slot, const CPlayerBitVec &recipients)
{
	CMultiRecipientFilter filter;
	std::vector<int> fresh;
	for (int i = 0; i < recipients.GetNumBits() && i < m_sentTo.GetNumBits(); i++)
	{
		if (recipients.IsBitSet(i) && !m_sentTo.IsBitSet(i))
		{
			filter.AddRecipient(i);
			m_sentTo.Set(i);
			fresh.push_back(i);
		}
	}
	if (fresh.empty())
	{
		return;
	}

	const std::string line = ComposeLine(slot, m_message.c_str(), m_teamOnly);

	// The whole line is composed already, colors and all, so it just prints as-is.
	// SayText2 would attribute the line to a player entity, which turns CHAT_COLOR_PURPLE into that player's team color.
	std::string chatLine = " ";
	chatLine += line;
	mmu::SendChatToFilter(&filter, chatLine.c_str());

	// The game's line would have echoed into each recipient's console, and TextMsg does not.
	char stripped[512];
	mmu::StripChatColors(line.c_str(), stripped, sizeof(stripped));
	for (int i : fresh)
	{
		if (g_pEngine && g_pEngine->GetPlayerNetInfo(CPlayerSlot(i)))
		{
			ADMIN_PrintToClient(i, "%s\n", stripped);
		}
	}

	m_strippedLine = stripped;
	m_rendered = true;
}

std::string CS2AChatProcessor::ComposeLine(int slot, const char *message, bool teamOnly) const
{
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
	const int team = PlayerTeam(slot);
	const bool alive = PlayerIsAlive(slot);

	const std::string *format = nullptr;
	if (teamOnly)
	{
		format = alive ? &g_CS2AConfig.chatFormatTeam : &g_CS2AConfig.chatFormatTeamDead;
	}
	else
	{
		format = alive ? &g_CS2AConfig.chatFormat : &g_CS2AConfig.chatFormatDead;
	}

	const TagDef *tag = g_CS2AConfig.chatTagsEnabled ? g_CS2ATagManager.Resolve(slot) : nullptr;
	const std::string teamColor = TeamColor(team);

	std::string body = message ? message : "";
	RestoreTypedQuotes(body);

	// Colors are resolved on the format itself first. The tag's own colors were resolved at load.
	const std::vector<FormatToken> tokens = {
		{"tag", tag ? tag->chatTag : ""},
		{"namecolor", (tag && !tag->nameColor.empty()) ? tag->nameColor : teamColor},
		{"msgcolor", (tag && !tag->msgColor.empty()) ? tag->msgColor : CHAT_COLOR_DEFAULT},
		{"teamcolor", teamColor},
		{"teamname", TeamName(team)},
		{"name", player ? player->name : ""},
		{"msg", body},
	};

	return ExpandTokens(mmu::ResolveColorTags(*format), tokens);
}
