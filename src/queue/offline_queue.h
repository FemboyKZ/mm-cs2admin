#ifndef _INCLUDE_ADMIN_OFFLINE_QUEUE_H_
#define _INCLUDE_ADMIN_OFFLINE_QUEUE_H_

#include <cstdint>
#include <string>
#include <vector>

// Write-ahead journal for every DB write that must not be lost.
//
// sql_mm never calls the query callback when a query errors,
// so a write issued against a connection that is up but broken would simply vanish.
// Entries are therefore persisted before they are sent and only dropped once their success callback lands.
class CS2AOfflineQueue
{
public:
	// Persist a write, then send it if the DB is connected.
	void Submit(const std::string &query);

	// Re-send entries that are not already in flight.
	// Called periodically from GameFrame and after a (re)connect.
	void ProcessQueue();

	// Persist the journal to disk
	void SaveToFile();

	// Load the journal from disk (called on plugin load)
	void LoadFromFile();

	// Returns true if there are entries waiting for a success callback
	bool HasItems() const
	{
		return !m_entries.empty();
	}

	size_t GetQueueSize() const
	{
		return m_entries.size();
	}

	static constexpr size_t MAX_QUEUE_SIZE = 500;

private:
	struct Entry
	{
		uint64_t id = 0;
		std::string query;
		int attempts = 0;
		// Plat_FloatTime of the send still waiting for a callback, 0 when idle.
		double sentAt = 0.0;
	};

	void Send(Entry &entry);
	void OnSuccess(uint64_t id);

	// A send that produced no callback within this many seconds is assumed dead and may be retried.
	static constexpr double IN_FLIGHT_TIMEOUT = 60.0;
	static constexpr int MAX_ATTEMPTS = 5;

	std::vector<Entry> m_entries;
	uint64_t m_nextId = 1;
};

extern CS2AOfflineQueue g_CS2AOfflineQueue;

#endif // _INCLUDE_ADMIN_OFFLINE_QUEUE_H_
