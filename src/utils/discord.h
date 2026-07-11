#ifndef _INCLUDE_ADMIN_DISCORD_H_
#define _INCLUDE_ADMIN_DISCORD_H_

#include <cstdint>
#include <string>

// Discord webhook notifications, sent through the shared mmu::http worker.
class CS2ADiscord
{
public:
	// Set up the shared HTTP client (user agent + shutdown latch).
	// Call Init() from plugin Load, Shutdown() on plugin unload.
	void Init();
	void Shutdown();

	// Send a plain text message to the configured webhook.
	void SendTextMessage(const char *content);

	// Send a rich embed to the configured webhook.
	void SendEmbedMessage(const char *title, const char *description, int color = 0x3498DB, const char *footer = nullptr);

	// Notify of an admin action
	void NotifyAdminAction(const char *adminName, const char *action, const char *targetName, const char *reason = nullptr, int durationMinutes = -1,
						   uint64_t adminSteamid64 = 0, uint64_t targetSteamid64 = 0, const char *output = nullptr);

	// Notify of a player report
	void NotifyReport(const char *reporterName, const char *targetName, const char *reason, uint64_t reporterSteamid64 = 0,
					  uint64_t targetSteamid64 = 0);

	bool IsEnabled() const;

private:
	void SendPayload(const std::string &json);
};

extern CS2ADiscord g_CS2ADiscord;

#endif // _INCLUDE_ADMIN_DISCORD_H_
