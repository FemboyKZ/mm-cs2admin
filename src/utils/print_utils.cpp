#include "print_utils.h"
#include "utils/log.h"
#include "src/common.h"
#include "src/config/config.h"
#include "src/db/database.h"
#include "src/admin/admin_manager.h"
#include "src/lang/translations.h"
#include "src/player/player_manager.h"

#include "game/print.h"

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
	// As for !pm copies, a reserved-slot holder is not staff.
	return g_CS2AAdminManager.PlayerHasFlag(slot, ADMFLAG_GENERIC);
}

static mmu::ChatPrinter &Printer()
{
	static mmu::ChatPrinter printer = []
	{
		mmu::ChatPrinter p;
		mmu::ChatPrinter::Setup s;
		s.translations = &g_CS2ATranslations;
		s.chatPrefix = &g_CS2AConfig.general.chatPrefix;
		s.resetColorAfterPrefix = true;
		s.conTag = "ADMIN";
		s.slotIsHuman = &SlotIsHuman;
		p.Configure(s);
		return p;
	}();
	return printer;
}

MMU_PRINT_SLOT_FN(CS2A_PrintToClient, Printer().ClientConsoleV(slot, fmt, args))
MMU_PRINT_GLOBAL_FN(CS2A_PrintToAll, Printer().ConsoleToAllV(fmt, args))
MMU_PRINT_SLOT_FN(CS2A_PrintToChat, Printer().ChatToSlotV(slot, fmt, args))
// Callers embed the prefix themselves.
MMU_PRINT_GLOBAL_FN(CS2A_ChatToAll, Printer().ChatToAllV(fmt, args, false))
MMU_PRINT_GLOBAL_FN(CS2A_ChatToAdmins, Printer().ChatToPredV(&SlotIsAdmin, fmt, args))
MMU_PRINT_SLOT_FN(CS2A_ReplyToCommand, Printer().ReplyV(slot, fmt, args))
MMU_PRINT_SLOT_FN(CS2A_PrintToClientT, Printer().ClientConsoleTV(slot, fmt, args))
MMU_PRINT_SLOT_FN(CS2A_PrintToChatT, Printer().ChatToSlotTV(slot, fmt, args))
MMU_PRINT_GLOBAL_FN(CS2A_ChatToAllT, Printer().ChatToAllTV(fmt, args))
MMU_PRINT_GLOBAL_FN(CS2A_ChatToAdminsT, Printer().ChatToPredTV(&SlotIsAdmin, fmt, args))
MMU_PRINT_SLOT_FN(CS2A_ReplyToCommandT, Printer().ReplyTV(slot, fmt, args))

void CS2A_LogAction(int adminSlot, const char *message)
{
	if (!g_CS2ADatabase.IsConnected())
	{
		MMU_LOG_WARN("Log (no DB): %s\n", message ? message : "");
		return;
	}

	std::string prefix = g_CS2AConfig.database.prefix;
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
