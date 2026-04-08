#ifndef _INCLUDE_ADMIN_COMMAND_SYSTEM_H_
#define _INCLUDE_ADMIN_COMMAND_SYSTEM_H_

#include "../common.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

// Callback type for chat commands.
// slot = player slot who typed the command.
// args = the arguments after the command name.
// silent = true if command was prefixed with / (don't echo to chat).
typedef std::function<void(int slot, const std::vector<std::string> &args, bool silent)> ChatCommandCallback;

class CS2ACommandSystem
{
public:
	// Register all built-in chat commands (ban, unban, mute, etc.)
	void RegisterBuiltinCommands();

	// Register a chat command (e.g., "ban", "mute").
	// Triggered by !ban, /ban, !mute, /mute etc. in chat.
	void RegisterCommand(const char *name, ChatCommandCallback callback);

	// Process a chat message. Called from the say/say_team hook.
	// Returns true if the message was a command (and was handled),
	// false if it's a normal chat message.
	bool ProcessChatMessage(int slot, const char *message, bool teamOnly);

	// Check if a gagged player should have their message blocked.
	bool ShouldBlockChat(int slot);

private:
	std::unordered_map<std::string, ChatCommandCallback> m_commands;
};

extern CS2ACommandSystem g_CS2ACommandSystem;

#endif // _INCLUDE_ADMIN_COMMAND_SYSTEM_H_
