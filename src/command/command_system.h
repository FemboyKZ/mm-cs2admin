#ifndef _INCLUDE_ADMIN_COMMAND_SYSTEM_H_
#define _INCLUDE_ADMIN_COMMAND_SYSTEM_H_

#include "../common.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

// Callback type for chat commands.
// slot = player slot who typed the command.
// args = the arguments after the command name.
// silent = true if command was prefixed with / (don't echo to chat).
typedef std::function<void(int slot, const std::vector<std::string> &args, bool silent)> ChatCommandCallback;

class ConCommand;

class CS2ACommandSystem
{
public:
	// Register all built-in chat commands (ban, unban, mute, etc.)
	void RegisterBuiltinCommands();

	// Register a chat command (e.g., "ban", "mute").
	// Triggered by !ban, /ban, !mute, /mute etc. in chat.
	void RegisterCommand(const char *name, ChatCommandCallback callback);

	// Register mm_ server console commands mirroring all chat commands.
	// Call once after RegisterBuiltinCommands().
	void RegisterConsoleCommands();

	// Process a chat message. Called from the say/say_team hook.
	// Returns true if the message was a command (and was handled),
	// false if it's a normal chat message.
	bool ProcessChatMessage(int slot, const char *message, bool teamOnly);

	// Dispatch a console command to the matching chat command handler.
	// cmdName = command name without "mm_" prefix (e.g. "who").
	// args = parsed arguments (excluding command name).
	// slot = player slot (-1 for server console).
	void DispatchConsoleCommand(const char *cmdName, const std::vector<std::string> &args, int slot);

	// Check if a gagged player should have their message blocked.
	bool ShouldBlockChat(int slot);

private:
	std::unordered_map<std::string, ChatCommandCallback> m_commands;

	// Dynamically allocated ConCommand objects for mm_ console commands.
	// Stored as raw pointers with persistent name/desc strings.
	struct ConsoleCmd
	{
		std::string name;   // "mm_ban", "mm_who", etc.
		std::string desc;   // description string
		ConCommand *cmd;
	};
	std::vector<ConsoleCmd> m_consoleCommands;
};

extern CS2ACommandSystem g_CS2ACommandSystem;

#endif // _INCLUDE_ADMIN_COMMAND_SYSTEM_H_
