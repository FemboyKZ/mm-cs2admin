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

void CS2AChatProcessor::PushSay(int slot)
{
	PendingSay say;
	say.slot = slot;
	m_says.push_back(std::move(say));
}

void CS2AChatProcessor::RenderSay(const char *message, bool teamOnly)
{
	if (m_says.empty())
	{
		return;
	}
	PendingSay &say = m_says.back();
	say.render = true;
	say.teamOnly = teamOnly;
	say.message = message ? message : "";
}

void CS2AChatProcessor::PopSay()
{
	// No server console copy here, the game still prints its own "[All Chat]" line since say is no longer superseded.
	if (!m_says.empty())
	{
		m_says.pop_back();
	}
}

int CS2AChatProcessor::MatchGameLine(INetworkMessageInternal *event, const CNetMessage *data) const
{
	if (m_says.empty() || !m_says.back().render || !event || !data)
	{
		return -1;
	}
	const int slot = m_says.back().slot;

	// Plugin lines carry the whole text in messagename or text with no sender, the game's carry the sender and message as params.
	// Same test cs2kz uses to block the game's line, plus the sender, so a line some other plugin triggers mid-say is left alone.
	// The game's chat SayText2 was seen carrying entityindex = slot + 1.
	NetworkMessageId id = event->GetNetMessageInfo()->m_MessageId;
	if (id == UM_SayText2)
	{
		auto *sayText = const_cast<CNetMessage *>(data)->ToPB<CUserMessageSayText2>();
		if (sayText->entityindex() == slot + 1 && (!sayText->param1().empty() || !sayText->param2().empty()))
		{
			return slot;
		}
	}
	else if (id == UM_SayText)
	{
		// Never seen for player chat, so how playerindex numbers players is unknown. Either convention counts.
		auto *sayText = const_cast<CNetMessage *>(data)->ToPB<CUserMessageSayText>();
		if (sayText->playerindex() == slot + 1 || sayText->playerindex() == slot)
		{
			return slot;
		}
	}
	return -1;
}

void CS2AChatProcessor::RenderPending(int slot, const CPlayerBitVec &recipients)
{
	if (m_says.empty() || m_says.back().slot != slot)
	{
		return;
	}
	PendingSay &say = m_says.back();

	CMultiRecipientFilter filter;
	std::vector<int> fresh;
	for (int i = 0; i < recipients.GetNumBits() && i < say.sentTo.GetNumBits(); i++)
	{
		if (recipients.IsBitSet(i) && !say.sentTo.IsBitSet(i))
		{
			filter.AddRecipient(i);
			say.sentTo.Set(i);
			fresh.push_back(i);
		}
	}
	if (fresh.empty())
	{
		return;
	}

	const std::string line = ComposeLine(slot, say.message.c_str(), say.teamOnly);

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
