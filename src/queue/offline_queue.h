#ifndef _INCLUDE_ADMIN_OFFLINE_QUEUE_H_
#define _INCLUDE_ADMIN_OFFLINE_QUEUE_H_

#include <string>
#include <vector>

class CS2AOfflineQueue
{
public:
	// Add a query to the offline queue (persisted to file)
	void Enqueue(const std::string &query);

	// Attempt to replay queued queries against the live DB.
	// Called periodically from GameFrame when DB is connected.
	void ProcessQueue();

	// Persist queue to disk
	void SaveToFile();

	// Load queue from disk (called on plugin load)
	void LoadFromFile();

	// Returns true if there are pending queries
	bool HasItems() const { return !m_queries.empty(); }

	size_t GetQueueSize() const { return m_queries.size(); }

private:
	std::vector<std::string> m_queries;
};

extern CS2AOfflineQueue g_CS2AOfflineQueue;

#endif // _INCLUDE_ADMIN_OFFLINE_QUEUE_H_
