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
#include <irecipientfilter.h>
#include <usermessages.pb.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

#define HUD_PRINTTALK 3

// Simple recipient filter for a single player slot
class CSingleRecipientFilter : public IRecipientFilter
{
public:
	CSingleRecipientFilter(int slot) { m_Recipients.Set(slot); }
	~CSingleRecipientFilter() override {}
	NetChannelBufType_t GetNetworkBufType(void) const override { return BUF_RELIABLE; }
	bool IsInitMessage(void) const override { return false; }
	const CPlayerBitVec &GetRecipients(void) const override { return m_Recipients; }
	CPlayerSlot GetPredictedPlayerSlot(void) const override { return CPlayerSlot(-1); }
private:
	CPlayerBitVec m_Recipients;
};

// Recipient filter with manually added slots
class CAdminRecipientFilter : public IRecipientFilter
{
public:
	CAdminRecipientFilter() {}
	~CAdminRecipientFilter() override {}
	void AddRecipient(int slot) { m_Recipients.Set(slot); }
	NetChannelBufType_t GetNetworkBufType(void) const override { return BUF_RELIABLE; }
	bool IsInitMessage(void) const override { return false; }
	const CPlayerBitVec &GetRecipients(void) const override { return m_Recipients; }
	CPlayerSlot GetPredictedPlayerSlot(void) const override { return CPlayerSlot(-1); }
private:
	CPlayerBitVec m_Recipients;
};

// Cache the TextMsg network message pointer (lazy init)
static INetworkMessageInternal *GetTextMsgMessage()
{
	static INetworkMessageInternal *s_pTextMsg = nullptr;
	if (!s_pTextMsg && g_pNetworkMessages)
		s_pTextMsg = g_pNetworkMessages->FindNetworkMessagePartial("TextMsg");
	return s_pTextMsg;
}

// Send a HUD chat message to a recipient filter
static void SendChatToFilter(IRecipientFilter *pFilter, const char *text)
{
	INetworkMessageInternal *pNetMsg = GetTextMsgMessage();
	if (!pNetMsg || !g_pGameEventSystem)
		return;

	CNetMessage *pData = pNetMsg->AllocateMessage();
	if (!pData)
		return;

	auto *pTextMsg = pData->ToPB<CUserMessageTextMsg>();
	pTextMsg->set_dest(HUD_PRINTTALK);
	pTextMsg->add_param(text);

	g_pGameEventSystem->PostEventAbstract(-1, false, pFilter, pNetMsg, pData, 0);

	g_pNetworkMessages->DeallocateNetMessageAbstract(pNetMsg, pData);
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

	CSingleRecipientFilter filter(slot);
	SendChatToFilter(&filter, chatBuf);
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

	// Build a filter of all connected non-bot players
	CAdminRecipientFilter filter;
	CGlobalVars *globals = GetGameGlobals();
	if (globals)
	{
		for (int i = 0; i < globals->maxClients; i++)
		{
			PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
			if (p && p->connected && !p->fakePlayer)
				filter.AddRecipient(i);
		}
	}

	SendChatToFilter(&filter, chatBuf);

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

	// Build filter of admin clients
	CAdminRecipientFilter filter;
	int adminCount = 0;
	for (int i = 0; i < globals->maxClients; i++)
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
		if (p && p->connected && !p->fakePlayer)
		{
			if (g_CS2AAdminManager.GetPlayerAdmin(i) != nullptr)
			{
				filter.AddRecipient(i);
				adminCount++;
			}
		}
	}

	if (adminCount > 0)
		SendChatToFilter(&filter, chatBuf);
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

	CSingleRecipientFilter filter(slot);
	SendChatToFilter(&filter, chatBuf);
}
