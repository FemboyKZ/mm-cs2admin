#include "player_manager.h"

#include <algorithm>
#include <cctype>

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

	m_players[slot].Reset();
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

int ADMIN_FindTarget(int callerSlot, const char *pattern)
{
	if (!pattern || !*pattern)
		return -1;

	// Try exact slot number first
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
			PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
			if (player)
				return slot;
		}
	}

	// Try "#userid" format
	if (pattern[0] == '#')
	{
		int slot = std::atoi(pattern + 1);
		if (slot >= 0 && slot <= MAXPLAYERS)
		{
			PlayerInfo *player = g_CS2APlayerManager.GetPlayer(slot);
			if (player)
				return slot;
		}
	}

	// Partial name match
	std::string search(pattern);
	std::transform(search.begin(), search.end(), search.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	int found = -1;
	int matches = 0;

	for (int i = 0; i <= MAXPLAYERS; i++)
	{
		PlayerInfo *player = g_CS2APlayerManager.GetPlayer(i);
		if (!player)
			continue;

		std::string name = player->name;
		std::transform(name.begin(), name.end(), name.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (name.find(search) != std::string::npos)
		{
			found = i;
			matches++;
		}
	}

	if (matches == 1)
		return found;

	if (matches > 1)
	{
		META_CONPRINTF("[ADMIN] Ambiguous target \"%s\" (%d matches).\n", pattern, matches);
		return -1;
	}

	return -1;
}
