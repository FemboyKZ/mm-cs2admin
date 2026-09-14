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
	PendingSay &say = m_pending[slot];
	say.active = true;
	say.teamOnly = teamOnly;
	say.rendered = false;
	say.message = message ? message : "";
	say.strippedLine.clear();
}

void CS2AChatProcessor::EndSay(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
	{
		return;
	}
	PendingSay &say = m_pending[slot];
	// Server console copy, for admins watching and for logs. Once per say, however many events the game split it into.
	if (say.rendered)
	{
		ADMIN_PrintToClient(-1, "%s\n", say.strippedLine.c_str());
	}
	say.active = false;
}

int CS2AChatProcessor::MatchGameLine(INetworkMessageInternal *event, const CNetMessage *data) const
{
	if (!event || !data || event->GetNetMessageInfo()->m_MessageId != UM_SayText2)
	{
		return -1;
	}

	auto *sayText = const_cast<CNetMessage *>(data)->ToPB<CUserMessageSayText2>();
	// mmu's own SayText2 lines are sent with chat off, so they never match.
	if (!sayText->chat())
	{
		return -1;
	}

	int slot = sayText->entityindex() - 1;
	if (slot < 0 || slot > MAXPLAYERS || !m_pending[slot].active)
	{
		return -1;
	}
	return slot;
}

void CS2AChatProcessor::RenderPending(int slot, const CPlayerBitVec &recipients)
{
	PendingSay &say = m_pending[slot];
	const std::string line = ComposeLine(slot, say.message.c_str(), say.teamOnly);

	CMultiRecipientFilter filter;
	for (int i = 0; i < recipients.GetNumBits() && i <= MAXPLAYERS; i++)
	{
		if (recipients.IsBitSet(i))
		{
			filter.AddRecipient(i);
		}
	}

	// The whole line is composed already, colors and all, so it just prints as-is.
	// SayText2 would attribute the line to a player entity, which turns CHAT_COLOR_PURPLE into that player's team color.
	std::string chatLine = " ";
	chatLine += line;
	mmu::SendChatToFilter(&filter, chatLine.c_str());

	// The game's SayText2 would have echoed into each recipient's console, and TextMsg does not.
	char stripped[512];
	mmu::StripChatColors(line.c_str(), stripped, sizeof(stripped));
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		if (filter.GetRecipients().IsBitSet(i) && g_pEngine && g_pEngine->GetPlayerNetInfo(CPlayerSlot(i)))
		{
			ADMIN_PrintToClient(i, "%s\n", stripped);
		}
	}

	say.strippedLine = stripped;
	say.rendered = true;
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
