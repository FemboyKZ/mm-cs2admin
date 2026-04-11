#include "print_utils.h"
#include "../common.h"
#include "../config/config.h"
#include "../db/database.h"
#include "../admin/admin_manager.h"
#include "../player/player_manager.h"

#include <networksystem/inetworkmessages.h>
#include <networksystem/inetworkserializer.h>
#include <networksystem/netmessage.h>
#include <engine/igameeventsystem.h>
#include <usermessages.pb.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

// Cache the SayText2 network message pointer (lazy init)
static INetworkMessageInternal *GetSayText2Message()
{
	static INetworkMessageInternal *s_pSayText2 = nullptr;
	if (!s_pSayText2 && g_pNetworkMessages)
		s_pSayText2 = g_pNetworkMessages->FindNetworkMessage("CUserMessageSayText2");
	return s_pSayText2;
}

// Send a HUD chat message to specific clients via bitmask
static void SendChat(const uint64 *clients, int clientCount, const char *text)
{
	INetworkMessageInternal *pMsg = GetSayText2Message();
	if (!pMsg || !g_pGameEventSystem)
		return;

	CNetMessage *pData = pMsg->AllocateMessage();
	if (!pData)
		return;

	auto *pSayText2 = pData->ToPB<CUserMessageSayText2>();
	pSayText2->set_entityindex(-1);
	pSayText2->set_chat(false);
	pSayText2->set_messagename(text);
	pSayText2->set_param1("");
	pSayText2->set_param2("");

	g_pGameEventSystem->PostEventAbstract(
		CSplitScreenSlot(0), false, clientCount, clients,
		pMsg, pData, 0, BUF_DEFAULT);

	g_pNetworkMessages->DeallocateNetMessageAbstract(pMsg, pData);
}

void ADMIN_PrintToClient(int slot, const char *fmt, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (slot < 0)
	{
		META_CONPRINTF("%s", buffer);
		return;
	}

	if (slot > MAXPLAYERS || !g_pEngine)
		return;

	g_pEngine->ClientPrintf(CPlayerSlot(slot), buffer);
}

void ADMIN_PrintToAll(const char *fmt, ...)
{
	if (!g_pEngine)
		return;

	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
		return;

	for (int i = 0; i < globals->maxClients; i++)
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
		if (p && p->connected && !p->fakePlayer)
			g_pEngine->ClientPrintf(CPlayerSlot(i), buffer);
	}
}

void ADMIN_LogAction(int adminSlot, const char *message)
{
	if (!g_CS2ADatabase.IsConnected())
	{
		META_CONPRINTF("[ADMIN] Log (no DB): %s\n", message ? message : "");
		return;
	}

	std::string prefix = g_CS2AConfig.databasePrefix;
	std::string escapedMsg = g_CS2ADatabase.Escape(message ? message : "");

	int aid = 0;
	if (adminSlot >= 0)
	{
		const AdminEntry *admin = g_CS2AAdminManager.GetPlayerAdmin(adminSlot);
		if (admin)
			aid = admin->adminId;
	}

	char query[2048];
	long long now = (long long)std::time(nullptr);
	snprintf(query, sizeof(query),
		"INSERT INTO %s_log (type, title, message, function, query, aid, host, created) "
		"VALUES ('m', 'Admin Command', '%s', 'cs2admin', '', %d, '', %lld)",
		prefix.c_str(), escapedMsg.c_str(), aid, now);

	g_CS2ADatabase.Query(query, nullptr);
}

void ADMIN_PrintToChat(int slot, const char *fmt, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (slot < 0)
	{
		META_CONPRINTF("[ADMIN] %s", buffer);
		return;
	}

	if (slot > MAXPLAYERS)
		return;

	// Build the chat-formatted message with prefix
	char chatBuf[512];
	snprintf(chatBuf, sizeof(chatBuf), " %s%s%s",
		g_CS2AConfig.chatPrefix.c_str(),
		CHAT_COLOR_DEFAULT,
		buffer);

	uint64 clientBit = (1ull << slot);
	SendChat(&clientBit, 1, chatBuf);
}

void ADMIN_ChatToAll(const char *fmt, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	// Build the chat message (callers already include chatPrefix)
	char chatBuf[512];
	snprintf(chatBuf, sizeof(chatBuf), " %s", buffer);

	// Send to all clients
	SendChat(nullptr, -1, chatBuf);

	// Also log to server console
	META_CONPRINTF("[ADMIN] %s", buffer);
}

void ADMIN_ChatToAdmins(const char *fmt, ...)
{
	if (!g_pEngine)
		return;

	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	// Callers already include chatPrefix in format string
	char chatBuf[512];
	snprintf(chatBuf, sizeof(chatBuf), " %s", buffer);

	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
		return;

	// Build bitmask of admin clients
	uint64 adminBits = 0;
	int adminCount = 0;
	for (int i = 0; i < globals->maxClients; i++)
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
		if (p && p->connected && !p->fakePlayer)
		{
			if (g_CS2AAdminManager.GetPlayerAdmin(i) != nullptr)
			{
				adminBits |= (1ull << i);
				adminCount++;
			}
		}
	}

	if (adminCount > 0)
		SendChat(&adminBits, adminCount, chatBuf);
}

void ADMIN_ReplyToCommand(int slot, const char *fmt, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (slot < 0)
	{
		// Server console
		META_CONPRINTF("[ADMIN] %s", buffer);
		return;
	}

	if (slot > MAXPLAYERS)
		return;

	// Send to player's console
	if (g_pEngine)
	{
		char consoleBuffer[512];
		snprintf(consoleBuffer, sizeof(consoleBuffer), "[ADMIN] %s", buffer);
		g_pEngine->ClientPrintf(CPlayerSlot(slot), consoleBuffer);
	}

	// Also send to player's chat HUD
	char chatBuf[512];
	snprintf(chatBuf, sizeof(chatBuf), " %s%s%s",
		g_CS2AConfig.chatPrefix.c_str(),
		CHAT_COLOR_DEFAULT,
		buffer);

	uint64 clientBit = (1ull << slot);
	SendChat(&clientBit, 1, chatBuf);
}
