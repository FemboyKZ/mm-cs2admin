#ifndef _INCLUDE_ADMIN_COMMAND_SYSTEM_H_
#define _INCLUDE_ADMIN_COMMAND_SYSTEM_H_

#include "src/common.h"
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
	// group and defaultFlag feed CanPlayerUseCommand, which runs before the callback does.
	void RegisterCommand(const char *name, const char *group, uint32_t defaultFlag, ChatCommandCallback callback);

	// Register mm_ server console commands mirroring all chat commands.
	// Call once after RegisterBuiltinCommands().
	void RegisterConsoleCommands();

	// Unregister and delete all dynamically created ConCommand objects.
	// Must be called from CS2APlugin::Unload().
	void Shutdown();

	// Process a chat message. Called from the say/say_team hook.
	// Returns true if the message was a command (and was handled),
	// false if it's a normal chat message.
	// silent is set when the command used the silent prefix, so the chat line should be hidden.
	bool ProcessChatMessage(int slot, const char *message, bool teamOnly, bool &silent);

	// Dispatch a console command to the matching chat command handler.
	// cmdName = command name without "mm_" prefix (e.g. "who").
	// args = parsed arguments (excluding command name).
	// slot = player slot (-1 for server console).
	void DispatchConsoleCommand(const char *cmdName, const std::vector<std::string> &args, int slot);

	// Check if a gagged player should have their message blocked.
	bool ShouldBlockChat(int slot);

private:
	struct Command
	{
		const char *group;
		uint32_t defaultFlag;
		ChatCommandCallback callback;
	};

	bool CanUse(int slot, const std::string &name, const Command &command) const;
	// Replies with the permission error instead of calling back when the caller may not use it.
	void Run(const std::string &name, const Command &command, int slot, const std::vector<std::string> &args, bool silent);

	std::unordered_map<std::string, Command> m_commands;

	// Dynamically allocated ConCommand objects for mm_ console commands.
	// Stored as raw pointers with persistent name/desc strings.
	struct ConsoleCmd
	{
		std::string name; // "mm_ban", "mm_who", etc.
		std::string desc; // description string
		ConCommand *cmd;
	};

	std::vector<ConsoleCmd> m_consoleCommands;
};

extern CS2ACommandSystem g_CS2ACommandSystem;

#endif // _INCLUDE_ADMIN_COMMAND_SYSTEM_H_
