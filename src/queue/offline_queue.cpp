#include "offline_queue.h"
#include "utils/log.h"
#include "src/common.h"
#include "src/db/database.h"

#include "interfaces/sql_mm/sql_mm.h"
#include <algorithm>
#include <fstream>
#include <cstdio>

CS2AOfflineQueue g_CS2AOfflineQueue;

static const char *QUEUE_DELIMITER = "---END_QUERY---";
// Ahead of an entry's query, when it has a note.
static const std::string QUEUE_NOTE = "---NOTE---\t";

void CS2AOfflineQueue::Submit(const std::string &query, std::function<void()> onWritten, std::string note)
{
	if (m_entries.size() >= MAX_QUEUE_SIZE)
	{
		MMU_LOG_WARN("Offline queue full (%zu items), dropping query.\n", m_entries.size());
		return;
	}

	Entry entry;
	entry.id = m_nextId++;
	entry.query = query;
	entry.onWritten = std::move(onWritten);
	std::replace(note.begin(), note.end(), '\n', ' ');
	std::replace(note.begin(), note.end(), '\r', ' ');
	entry.note = std::move(note);
	m_entries.push_back(std::move(entry));

	// On disk before it goes out, so a crash or a silent query failure cannot lose it.
	SaveToFile();

	if (g_CS2ADatabase.IsConnected())
	{
		Send(m_entries.back());
	}
	else
	{
		MMU_LOG_INFO("Query queued offline (%zu in queue).\n", m_entries.size());
	}
}

void CS2AOfflineQueue::Send(Entry &entry)
{
	entry.attempts++;
	// Before the query: a disconnected one answers at once.
	entry.inFlight = true;

	uint64_t id = entry.id;
	g_CS2ADatabase.Query(entry.query.c_str(), [id](ISQLQuery *result) { g_CS2AOfflineQueue.OnAnswer(id, result != nullptr); });
}

void CS2AOfflineQueue::OnAnswer(uint64_t id, bool written)
{
	auto it = std::find_if(m_entries.begin(), m_entries.end(), [id](const Entry &e) { return e.id == id; });
	if (it == m_entries.end())
	{
		return;
	}
	if (!written)
	{
		// ProcessQueue sends it again.
		it->inFlight = false;
		return;
	}
	std::function<void()> onWritten = std::move(it->onWritten);
	m_entries.erase(it);
	SaveToFile();
	if (onWritten)
	{
		onWritten();
	}
}

void CS2AOfflineQueue::ProcessQueue()
{
	if (m_entries.empty() || !g_CS2ADatabase.IsConnected())
	{
		return;
	}

	size_t sent = 0;
	size_t dropped = 0;
	bool waiting = false;

	for (size_t i = 0; i < m_entries.size();)
	{
		Entry &entry = m_entries[i];

		// Still waiting on a callback, re-sending now would duplicate the row.
		if (entry.inFlight)
		{
			waiting = true;
			i++;
			continue;
		}

		if (entry.attempts >= MAX_ATTEMPTS)
		{
			MMU_LOG_WARN("Dropping queued query after %d failed attempts: %s\n", entry.attempts, entry.query.c_str());
			m_entries.erase(m_entries.begin() + i);
			dropped++;
			continue;
		}

		Send(entry);
		sent++;
		i++;
	}

	// A failed query is only reported once a later one answers, and on a quiet server there may be none.
	if (waiting && sent == 0)
	{
		g_CS2ADatabase.Query("SELECT 1", [](ISQLQuery *) {});
	}

	if (sent > 0 || dropped > 0)
	{
		MMU_LOG_INFO("Offline queue: sent %zu, dropped %zu, %zu still pending.\n", sent, dropped, m_entries.size());
	}
	if (dropped > 0)
	{
		SaveToFile();
	}
}

void CS2AOfflineQueue::SaveToFile()
{
	char path[512];
	snprintf(path, sizeof(path), "%s/addons/cs2admin/queue.txt", g_SMAPI->GetBaseDir());

	// An empty queue truncates the file and leaves it in place.
	// Removing it would unlink a symlinked queue.txt, and the target would keep its entries and replay them on every start.
	std::ofstream file(path, std::ios::trunc);
	if (!file.is_open())
	{
		return;
	}

	for (const Entry &entry : m_entries)
	{
		if (!entry.note.empty())
		{
			file << QUEUE_NOTE << entry.note << "\n";
		}
		file << entry.query << "\n" << QUEUE_DELIMITER << "\n";
	}
}

void CS2AOfflineQueue::LoadFromFile(const RestoreFn &restore)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/addons/cs2admin/queue.txt", g_SMAPI->GetBaseDir());

	std::ifstream file(path);
	if (!file.is_open())
	{
		return;
	}

	std::string current;
	std::string note;
	std::string line;
	while (std::getline(file, line))
	{
		if (line == QUEUE_DELIMITER)
		{
			if (!current.empty())
			{
				Entry entry;
				entry.id = m_nextId++;
				entry.query = current;
				entry.note = note;
				if (restore && !note.empty())
				{
					entry.onWritten = restore(note);
				}
				m_entries.push_back(std::move(entry));
				current.clear();
			}
			note.clear();
		}
		else if (current.empty() && line.compare(0, QUEUE_NOTE.size(), QUEUE_NOTE) == 0)
		{
			note = line.substr(QUEUE_NOTE.size());
		}
		else
		{
			if (!current.empty())
			{
				current += "\n";
			}
			current += line;
		}
	}

	if (!m_entries.empty())
	{
		MMU_LOG_INFO("Loaded %zu queued queries from file.\n", m_entries.size());
	}
}
