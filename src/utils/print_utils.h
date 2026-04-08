#ifndef _INCLUDE_ADMIN_PRINT_UTILS_H_
#define _INCLUDE_ADMIN_PRINT_UTILS_H_

// Helper: send a console message to a specific player.
void ADMIN_PrintToClient(int slot, const char *fmt, ...);

// Helper: send a message to all connected players' consoles.
void ADMIN_PrintToAll(const char *fmt, ...);

// Helper: log an admin action to the database log table.
void ADMIN_LogAction(int adminSlot, const char *message);

// Send a formatted chat message to a player.
void ADMIN_PrintToChat(int slot, const char *fmt, ...);

// Broadcast a formatted chat message to all connected players.
void ADMIN_ChatToAll(const char *fmt, ...);

// Broadcast a formatted chat message to all connected admins.
void ADMIN_ChatToAdmins(const char *fmt, ...);

#endif //_INCLUDE_ADMIN_PRINT_UTILS_H_
