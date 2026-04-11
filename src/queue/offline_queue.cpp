#include "offline_queue.h"
#include "../common.h"
#include "../db/database.h"

#include <sql_mm.h>
#include <fstream>
#include <cstdio>

CS2AOfflineQueue g_CS2AOfflineQueue;

static const char *QUEUE_DELIMITER = "---END_QUERY---";

void CS2AOfflineQueue::Enqueue(const std::string &query)
{
	if (m_queries.size() >= MAX_QUEUE_SIZE)
	{
		META_CONPRINTF("[ADMIN] Offline queue full (%zu items), dropping query.\n", m_queries.size());
		return;
	}
	m_queries.push_back(query);
	SaveToFile();
	META_CONPRINTF("[ADMIN] Query queued offline (%zu in queue).\n", m_queries.size());
}

void CS2AOfflineQueue::ProcessQueue()
{
	if (m_queries.empty() || !g_CS2ADatabase.IsConnected())
		return;

	META_CONPRINTF("[ADMIN] Processing offline queue (%zu items)...\n", m_queries.size());

	std::vector<std::string> pending = std::move(m_queries);
	m_queries.clear();

	for (const auto &query : pending)
	{
		g_CS2ADatabase.Query(query.c_str(), [this, query](ISQLQuery *result) {
			if (!result)
			{
				// Query failed entirely, re-queue and persist
				m_queries.push_back(query);
				SaveToFile();
				META_CONPRINTF("[ADMIN] Offline queue item failed, re-queued.\n");
			}
		});
	}

	// Persist the empty queue (clears the file).
	// If any callbacks requeue items, they will re-save.
	SaveToFile();
}

void CS2AOfflineQueue::SaveToFile()
{
	char path[512];
	snprintf(path, sizeof(path), "%s/addons/cs2admin/queue.txt", g_SMAPI->GetBaseDir());

	if (m_queries.empty())
	{
		// Delete file when queue is empty
		std::remove(path);
		return;
	}

	std::ofstream file(path, std::ios::trunc);
	if (!file.is_open())
		return;

	for (const auto &q : m_queries)
		file << q << "\n" << QUEUE_DELIMITER << "\n";
}

void CS2AOfflineQueue::LoadFromFile()
{
	char path[512];
	snprintf(path, sizeof(path), "%s/addons/cs2admin/queue.txt", g_SMAPI->GetBaseDir());

	std::ifstream file(path);
	if (!file.is_open())
		return;

	std::string current;
	std::string line;
	while (std::getline(file, line))
	{
		if (line == QUEUE_DELIMITER)
		{
			if (!current.empty())
			{
				m_queries.push_back(current);
				current.clear();
			}
		}
		else
		{
			if (!current.empty()) current += "\n";
			current += line;
		}
	}

	if (!m_queries.empty())
		META_CONPRINTF("[ADMIN] Loaded %zu queued queries from file.\n", m_queries.size());
}
