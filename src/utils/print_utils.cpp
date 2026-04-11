#include "print_utils.h"
#include "../common.h"
#include "../config/config.h"
#include "../db/database.h"
#include "../admin/admin_manager.h"
#include "../player/player_manager.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

void ADMIN_PrintToClient(int slot, const char *fmt, ...)
{
	if (slot < 0 || slot > MAXPLAYERS || !g_pEngine)
		return;

	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

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

	ADMIN_PrintToClient(slot, "%s", buffer);
}

void ADMIN_ChatToAll(const char *fmt, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	ADMIN_PrintToAll("%s", buffer);
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

	CGlobalVars *globals = GetGameGlobals();
	if (!globals)
		return;

	for (int i = 0; i < globals->maxClients; i++)
	{
		PlayerInfo *p = g_CS2APlayerManager.GetPlayer(i);
		if (p && p->connected && !p->fakePlayer)
		{
			if (g_CS2AAdminManager.GetPlayerAdmin(i) != nullptr)
				g_pEngine->ClientPrintf(CPlayerSlot(i), buffer);
		}
	}
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

	if (!g_pEngine || slot > MAXPLAYERS)
		return;

	// Console reply
	char consoleBuffer[512];
	snprintf(consoleBuffer, sizeof(consoleBuffer), "[ADMIN] %s", buffer);
	g_pEngine->ClientPrintf(CPlayerSlot(slot), consoleBuffer);

	// Chat reply with config prefix
	char chatBuffer[512];
	snprintf(chatBuffer, sizeof(chatBuffer), " %s%s", g_CS2AConfig.chatPrefix.c_str(), buffer);
	ADMIN_PrintToChat(slot, "%s", chatBuffer);
}
