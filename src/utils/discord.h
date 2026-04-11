#ifndef _INCLUDE_ADMIN_DISCORD_H_
#define _INCLUDE_ADMIN_DISCORD_H_

#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

class CS2ADiscord
{
public:
	CS2ADiscord();
	~CS2ADiscord();

	// Start/stop the background worker thread.
	// Call Init() after config is loaded, Shutdown() on plugin unload.
	void Init();
	void Shutdown();

	// Send a plain text message to the configured webhook.
	void SendTextMessage(const char *content);

	// Send a rich embed to the configured webhook.
	void SendEmbedMessage(const char *title, const char *description, int color = 0x3498DB, const char *footer = nullptr);

	// Notify of an admin action
	void NotifyAdminAction(const char *adminName, const char *action, const char *targetName,
		const char *reason = nullptr, int durationMinutes = -1);

	// Notify of a player report
	void NotifyReport(const char *reporterName, const char *targetName, const char *reason);

	bool IsEnabled() const;

private:
	void SendPayload(const std::string &json);
	void WorkerThread();
	bool HttpsPost(const std::string &host, const std::string &path, const std::string &json);

	std::thread m_worker;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::queue<std::string> m_queue;
	std::atomic<bool> m_running{false};

	static constexpr size_t MAX_QUEUE_SIZE = 100;
};

extern CS2ADiscord g_CS2ADiscord;

#endif // _INCLUDE_ADMIN_DISCORD_H_
