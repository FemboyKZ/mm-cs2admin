#include "print_utils.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/db/database.h"
#include "src/admin/admin_manager.h"
#include "src/lang/translations.h"
#include "src/player/player_manager.h"

#include "mmu/print.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

static bool SlotIsHuman(int slot)
{
	PlayerInfo *p = g_CS2APlayerManager.GetPlayer(slot);
	return p && p->connected && !p->fakePlayer;
}

static bool SlotIsAdmin(int slot)
{
	return g_CS2AAdminManager.GetPlayerAdmin(slot) != nullptr;
}

static mmu::ChatPrinter &Printer()
{
	static mmu::ChatPrinter printer = [] {
		mmu::ChatPrinter p;
		mmu::ChatPrinter::Setup s;
		s.translations = &g_CS2ATranslations;
		s.slotLanguage = &ADMIN_SlotLanguage;
		s.chatPrefix = &g_CS2AConfig.chatPrefix;
		s.resetColorAfterPrefix = true;
		s.conTag = "ADMIN";
		s.slotIsHuman = &SlotIsHuman;
		p.Configure(s);
		return p;
	}();
	return printer;
}

void ADMIN_PrintToClient(int slot, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	Printer().ClientConsoleV(slot, fmt, args);
	va_end(args);
}

void ADMIN_PrintToAll(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	Printer().ConsoleToAllV(fmt, args);
	va_end(args);
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
		{
			aid = admin->adminId;
		}
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
	va_list args;
	va_start(args, fmt);
	Printer().ChatToSlotV(slot, fmt, args);
	va_end(args);
}

void ADMIN_ChatToAll(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	// Callers already include chatPrefix in the message.
	Printer().ChatToAllV(fmt, args, false);
	va_end(args);
}

void ADMIN_ChatToAdmins(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	Printer().ChatToPredV(&SlotIsAdmin, fmt, args);
	va_end(args);
}

void ADMIN_ReplyToCommand(int slot, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	Printer().ReplyV(slot, fmt, args);
	va_end(args);
}

void ADMIN_PrintToClientT(int slot, const char *phrase, ...)
{
	va_list args;
	va_start(args, phrase);
	Printer().ClientConsoleTV(slot, phrase, args);
	va_end(args);
}

void ADMIN_PrintToChatT(int slot, const char *phrase, ...)
{
	va_list args;
	va_start(args, phrase);
	Printer().ChatToSlotTV(slot, phrase, args);
	va_end(args);
}

void ADMIN_ChatToAllT(const char *phrase, ...)
{
	va_list args;
	va_start(args, phrase);
	Printer().ChatToAllTV(phrase, args);
	va_end(args);
}

void ADMIN_ChatToAdminsT(const char *phrase, ...)
{
	va_list args;
	va_start(args, phrase);
	Printer().ChatToPredTV(&SlotIsAdmin, phrase, args);
	va_end(args);
}

void ADMIN_ReplyToCommandT(int slot, const char *phrase, ...)
{
	va_list args;
	va_start(args, phrase);
	Printer().ReplyTV(slot, phrase, args);
	va_end(args);
}
