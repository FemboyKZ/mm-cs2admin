#include "chat_processor.h"

#include "mmu/chat_colors.h"
#include "mmu/entity/ccsplayercontroller.h"
#include "mmu/log.h"
#include "mmu/print.h"
#include "mmu/recipient_filter.h"

#include "src/compat/foreign_plugins.h"
#include "src/config/config.h"
#include "src/menu/menu_bridge.h"
#include "src/player/player_manager.h"
#include "src/tags/tag_manager.h"
#include "src/utils/print_utils.h"

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
	if (!player || !player->connected || player->fakePlayer)
	{
		return false;
	}

	// A chat menu eats the player's numeric input to drive itself and suppresses the line.
	// cs2menus hooks the same ICvar chain we do, and SourceHook runs every pre-hook regardless of who supersedes,
	// so without this we'd render the keypress as a chat message.
	// HTML menus navigate on buttons and never touch say.
	if (g_AdminMenus.EatsChatInput(slot))
	{
		return false;
	}

	return true;
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

bool CS2AChatProcessor::CanHear(int listener, int speaker, bool teamOnly) const
{
	PlayerInfo *player = g_CS2APlayerManager.GetPlayer(listener);
	if (!player || !player->connected || player->fakePlayer)
	{
		return false;
	}

	if (!g_pEngine || !g_pEngine->GetPlayerNetInfo(CPlayerSlot(listener)))
	{
		return false;
	}

	if (teamOnly && PlayerTeam(listener) != PlayerTeam(speaker))
	{
		return false;
	}

	return true;
}

void CS2AChatProcessor::RenderPlayerChat(int slot, const char *message, bool teamOnly)
{
	const std::string line = ComposeLine(slot, message, teamOnly);

	CMultiRecipientFilter filter;
	std::vector<int> recipients;
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		if (CanHear(i, slot, teamOnly))
		{
			filter.AddRecipient(i);
			recipients.push_back(i);
		}
	}

	// The whole line is composed already, colors and all, so it just prints as-is.
	// SayText2 would attribute the line to a player entity, which turns CHAT_COLOR_PURPLE into that player's team color.
	std::string chatLine = " ";
	chatLine += line;
	mmu::SendChatToFilter(&filter, chatLine.c_str());

	// Chat normally shows up in the console too, and superseding say took that away along with the chat line.
	// It goes to the same recipients rather than everyone, or team chat would leak through the console.
	char stripped[512];
	mmu::StripChatColors(line.c_str(), stripped, sizeof(stripped));
	for (int i : recipients)
	{
		ADMIN_PrintToClient(i, "%s\n", stripped);
	}
	// Server console copy, for admins watching and for logs.
	ADMIN_PrintToClient(-1, "%s\n", stripped);
}
