#ifndef _INCLUDE_CS2A_PRINT_UTILS_H_
#define _INCLUDE_CS2A_PRINT_UTILS_H_

// Helper: send a console message to a specific player.
void CS2A_PrintToClient(int slot, const char *fmt, ...);

// Helper: send a message to all connected players' consoles.
void CS2A_PrintToAll(const char *fmt, ...);

// Helper: log an admin action to the database log table.
void CS2A_LogAction(int adminSlot, const char *message);

// Send a formatted chat message to a player.
void CS2A_PrintToChat(int slot, const char *fmt, ...);

// Broadcast a formatted chat message to all connected players.
void CS2A_ChatToAll(const char *fmt, ...);

// Broadcast a formatted chat message to all connected admins.
void CS2A_ChatToAdmins(const char *fmt, ...);

// Reply to a command: sends to player's console AND chat (with config prefix).
// For console callers (slot < 0), prints to server console only.
void CS2A_ReplyToCommand(int slot, const char *fmt, ...);

// Translated variants.
void CS2A_PrintToClientT(int slot, const char *phrase, ...);
void CS2A_PrintToChatT(int slot, const char *phrase, ...);
void CS2A_ChatToAllT(const char *phrase, ...);
void CS2A_ChatToAdminsT(const char *phrase, ...);
void CS2A_ReplyToCommandT(int slot, const char *phrase, ...);

#endif //_INCLUDE_CS2A_PRINT_UTILS_H_
