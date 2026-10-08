#ifndef _INCLUDE_ADMIN_OFFLINE_QUEUE_H_
#define _INCLUDE_ADMIN_OFFLINE_QUEUE_H_

#include <cstdint>
#include <functional>
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
	// `onWritten` runs once the database has it. It does not survive a restart.
	void Submit(const std::string &query, std::function<void()> onWritten = nullptr);

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
		// Sent and not answered yet.
		bool inFlight = false;
		std::function<void()> onWritten;
	};

	void Send(Entry &entry);
	void OnAnswer(uint64_t id, bool written);

	static constexpr int MAX_ATTEMPTS = 5;

	std::vector<Entry> m_entries;
	uint64_t m_nextId = 1;
};

extern CS2AOfflineQueue g_CS2AOfflineQueue;

#endif // _INCLUDE_ADMIN_OFFLINE_QUEUE_H_
