#include "player_manager.h"
#include "../admin/admin_manager.h"
#include "../utils/print_utils.h"
#include "../entity/ccsplayercontroller.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

CS2APlayerManager g_CS2APlayerManager;

void CS2APlayerManager::OnClientConnected(int slot, const char *name, uint64_t xuid,
	const char *networkID, const char *address, bool fakePlayer)
{
	if (slot < 0 || slot > MAXPLAYERS)
		return;

	PlayerInfo &player = m_players[slot];
	player.Reset();
	player.connected = true;
	player.steamid64 = xuid;
	player.authid = SteamID64ToAuthId(xuid);
	player.name = name ? name : "";
	player.fakePlayer = fakePlayer;

	// Extract IP from "ip:port" format
	if (address)
	{
		std::string addr(address);
		size_t colon = addr.find(':');
		if (colon != std::string::npos)
			player.ip = addr.substr(0, colon);
		else
			player.ip = addr;
	}
}

void CS2APlayerManager::OnClientDisconnect(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
		return;

	PlayerInfo &player = m_players[slot];
	if (player.connected && !player.fakePlayer && player.steamid64 != 0)
		AddDisconnectedPlayer(player);

	player.Reset();
}

PlayerInfo *CS2APlayerManager::GetPlayer(int slot)
{
	if (slot < 0 || slot > MAXPLAYERS)
		return nullptr;

	if (!m_players[slot].connected)
		return nullptr;

	return &m_players[slot];
}

PlayerInfo *CS2APlayerManager::FindPlayerBySteamID64(uint64_t steamid64)
{
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		if (m_players[i].connected && m_players[i].steamid64 == steamid64)
			return &m_players[i];
	}
	return nullptr;
}

int CS2APlayerManager::FindSlotBySteamID64(uint64_t steamid64)
{
	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		if (m_players[i].connected && m_players[i].steamid64 == steamid64)
			return i;
	}
	return -1;
}

void CS2APlayerManager::AddDisconnectedPlayer(const PlayerInfo &player)
{
	DisconnectedPlayer dc;
	dc.name = player.name;
	dc.steamid64 = player.steamid64;
	dc.ip = player.ip;

	CGlobalVars *globals = GetGameGlobals();
	dc.disconnectTime = globals ? globals->curtime : 0.0;

	// Check for duplicates
	for (auto &existing : m_disconnected)
	{
		if (existing.steamid64 == dc.steamid64)
		{
			existing = dc;
			return;
		}
	}

	if ((int)m_disconnected.size() >= MAX_DISCONNECTED)
		m_disconnected.erase(m_disconnected.begin());

	m_disconnected.push_back(dc);
}

// Shared admin identity helpers

std::string GetAdminAuthId(int adminSlot)
{
	if (adminSlot < 0)
		return "STEAM_ID_SERVER";

	PlayerInfo *admin = g_CS2APlayerManager.GetPlayer(adminSlot);
	if (admin)
		return admin->authid;

	return "STEAM_ID_SERVER";
}

std::string GetAdminIP(int adminSlot)
{
	if (adminSlot < 0)
		return "";

	PlayerInfo *admin = g_CS2APlayerManager.GetPlayer(adminSlot);
	if (admin)
		return admin->ip;

	return "";
}

// Targeting System

static std::string ToLowerStr(const std::string &s)
{
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

TargetResult ADMIN_FindTargets(int callerSlot, const char *pattern)
{
	TargetResult result;

	if (!pattern || !*pattern)
	{
		result.error = "No target specified.";
		return result;
	}

	CGlobalVars *globals = GetGameGlobals();
	int maxClients = globals ? globals->maxClients : MAXPLAYERS;

	std::string pat(pattern);

	// @ group targets
	if (pat[0] == '@')
	{
		std::string group = ToLowerStr(pat.substr(1));
		result.isMultiTarget = true;

		if (group == "me")
		{
			result.isMultiTarget = false;
			if (callerSlot >= 0)
			{
				PlayerInfo *p = g_CS2APlayerManager.GetPlayer(callerSlot);
				if (p && p->connected)
					result.slots.push_back(callerSlot);
			}
			if (result.slots.empty())
				result.error = "You are not in-game.";
			return result;
		}

		for (int i = 0; i < maxClients; i++)
		{
			PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
			if (!p || !p->connected)
				continue;

			if (group == "all")
			{
				if (!p->fakePlayer)
					result.slots.push_back(i);
			}
			else if (group == "bot")
			{
				if (p->fakePlayer)
					result.slots.push_back(i);
			}
			else if (group == "human")
			{
				if (!p->fakePlayer)
					result.slots.push_back(i);
			}
			else if (group == "t")
			{
				if (p->fakePlayer)
					continue;
				CCSPlayerController *ctrl = CCSPlayerController::FromSlot(i);
				if (ctrl && ctrl->m_iTeamNum() == CS_TEAM_T)
					result.slots.push_back(i);
			}
			else if (group == "ct")
			{
				if (p->fakePlayer)
					continue;
				CCSPlayerController *ctrl = CCSPlayerController::FromSlot(i);
				if (ctrl && ctrl->m_iTeamNum() == CS_TEAM_CT)
					result.slots.push_back(i);
			}
			else if (group == "spec")
			{
				if (p->fakePlayer)
					continue;
				CCSPlayerController *ctrl = CCSPlayerController::FromSlot(i);
				if (ctrl && ctrl->m_iTeamNum() == CS_TEAM_SPECTATOR)
					result.slots.push_back(i);
			}
			else if (group == "alive")
			{
				if (p->fakePlayer)
					continue;
				CCSPlayerController *ctrl = CCSPlayerController::FromSlot(i);
				if (ctrl && ctrl->m_bPawnIsAlive())
					result.slots.push_back(i);
			}
			else if (group == "dead")
			{
				if (p->fakePlayer)
					continue;
				CCSPlayerController *ctrl = CCSPlayerController::FromSlot(i);
				if (ctrl && !ctrl->m_bPawnIsAlive())
					result.slots.push_back(i);
			}
			else if (group == "random")
			{
				if (!p->fakePlayer)
					result.slots.push_back(i);
			}
		}

		// For @random, pick one at random from the collected slots
		if (group == "random" && !result.slots.empty())
		{
			int idx = rand() % result.slots.size();
			int picked = result.slots[idx];
			result.slots.clear();
			result.slots.push_back(picked);
			result.isMultiTarget = false;
		}

		if (result.slots.empty())
			result.error = "No matching players found.";

		return result;
	}

	// $ SteamID64 targeting
	if (pat[0] == '$')
	{
		std::string steamStr = pat.substr(1);
		char *end;
		uint64_t steamid64 = strtoull(steamStr.c_str(), &end, 10);
		if (*end != '\0' || steamid64 == 0)
		{
			result.error = "Invalid SteamID64.";
			return result;
		}

		int slot = g_CS2APlayerManager.FindSlotBySteamID64(steamid64);
		if (slot >= 0)
			result.slots.push_back(slot);
		else
			result.error = "Player with that SteamID64 not found.";

		return result;
	}

	// & exact name targeting (case insensitive)
	if (pat[0] == '&')
	{
		std::string exactName = ToLowerStr(pat.substr(1));
		for (int i = 0; i < maxClients; i++)
		{
			PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
			if (!p || !p->connected)
				continue;

			if (ToLowerStr(p->name) == exactName)
			{
				result.slots.push_back(i);
				return result;
			}
		}
		result.error = "No player found with exact name.";
		return result;
	}

	// # slot/userid targeting
	if (pat[0] == '#')
	{
		int slot = std::atoi(pat.c_str() + 1);
		if (slot >= 0 && slot <= MAXPLAYERS)
		{
			PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slot);
			if (p && p->connected)
			{
				result.slots.push_back(slot);
				return result;
			}
		}
		result.error = "Player not found with that slot/userid.";
		return result;
	}

	// Try exact slot number
	bool isNumber = true;
	for (const char *p = pattern; *p; p++)
	{
		if (!std::isdigit(static_cast<unsigned char>(*p)))
		{
			isNumber = false;
			break;
		}
	}

	if (isNumber)
	{
		int slot = std::atoi(pattern);
		if (slot >= 0 && slot <= MAXPLAYERS)
		{
			PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slot);
			if (p && p->connected)
			{
				result.slots.push_back(slot);
				return result;
			}
		}
	}

	// Partial name match (single target only)
	std::string search = ToLowerStr(pat);
	int found = -1;
	int matches = 0;

	for (int i = 0; i < maxClients; i++)
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
		if (!p || !p->connected)
			continue;

		std::string name = ToLowerStr(p->name);
		if (name.find(search) != std::string::npos)
		{
			found = i;
			matches++;
		}
	}

	if (matches == 1)
	{
		result.slots.push_back(found);
		return result;
	}

	if (matches > 1)
		result.error = "Multiple players match that name. Be more specific.";
	else
		result.error = "No player found matching that name.";

	return result;
}

int ADMIN_FindTarget(int callerSlot, const char *pattern)
{
	TargetResult result = ADMIN_FindTargets(callerSlot, pattern);
	if (!result.error.empty())
	{
		ADMIN_ReplyToCommand(callerSlot, "%s\n", result.error.c_str());
		return -1;
	}
	if (result.slots.size() != 1)
	{
		if (result.slots.size() > 1)
			ADMIN_ReplyToCommand(callerSlot, "Multiple players matched. Use a more specific target.\n");
		else
			ADMIN_ReplyToCommand(callerSlot, "No player found.\n");
		return -1;
	}
	return result.slots[0];
}

// Duration Parsing

int ADMIN_ParseDuration(const char *input)
{
	if (!input || !*input)
		return -1;

	std::string str(input);

	// Check for negative
	if (str[0] == '-')
		return -1;

	// Extract numeric part
	std::string digits;
	char suffix = 0;
	for (size_t i = 0; i < str.size(); i++)
	{
		if (std::isdigit(static_cast<unsigned char>(str[i])))
			digits += str[i];
		else if (i == str.size() - 1)
			suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(str[i])));
		else
			return -1; // invalid character in middle
	}

	if (digits.empty())
		return -1;

	// Prevent overflow with very large numbers
	if (digits.size() > 9)
		return 0; // treat as permanent

	int value = std::atoi(digits.c_str());
	if (value == 0)
		return 0; // permanent

	switch (suffix)
	{
		case 'h': // hours
		{
			double mins = (double)value * 60.0;
			return mins > INT_MAX ? 0 : (int)mins;
		}
		case 'd': // days
		{
			double mins = (double)value * 60.0 * 24.0;
			return mins > INT_MAX ? 0 : (int)mins;
		}
		case 'w': // weeks
		{
			double mins = (double)value * 60.0 * 24.0 * 7.0;
			return mins > INT_MAX ? 0 : (int)mins;
		}
		case 'm': // months (30 days)
		{
			double mins = (double)value * 60.0 * 24.0 * 30.0;
			return mins > INT_MAX ? 0 : (int)mins;
		}
		case 0: // no suffix = minutes
			return value;
		default:
			return -1;
	}
}

std::string ADMIN_FormatDuration(int minutes)
{
	if (minutes == 0)
		return "permanent";

	if (minutes < 60)
		return std::to_string(minutes) + " minute" + (minutes != 1 ? "s" : "");

	int hours = minutes / 60;
	if (hours < 24)
		return std::to_string(hours) + " hour" + (hours != 1 ? "s" : "");

	int days = hours / 24;
	if (days < 7)
		return std::to_string(days) + " day" + (days != 1 ? "s" : "");

	int weeks = days / 7;
	if (weeks < 4)
		return std::to_string(weeks) + " week" + (weeks != 1 ? "s" : "");

	int months = days / 30;
	return std::to_string(months) + " month" + (months != 1 ? "s" : "");
}
