#ifndef _INCLUDE_ADMIN_DATABASE_H_
#define _INCLUDE_ADMIN_DATABASE_H_

#include "mmu/sql.h"

#include <functional>
#include <string>

class CS2ADatabase
{
public:
	// Acquire sql_mm and select the client from the loaded config.
	// Must be called in AllPluginsLoaded or later.
	bool Init();

	// Connect to the database using the loaded config.
	// Callback fires on the main thread with success/failure.
	void Connect(std::function<void(bool)> callback);

	// Disconnect and clean up.
	void Shutdown();

	bool IsConnected() const
	{
		return m_conn.IsConnected();
	}

	// True while an async connect is in flight.
	bool IsConnecting() const
	{
		return m_conn.IsConnecting();
	}

	// Was Init() successful (sql_mm available)?
	bool IsInitialized() const
	{
		return m_conn.IsInitialized();
	}

	// True from the moment Shutdown() is called.
	bool IsShuttingDown() const
	{
		return m_conn.IsShuttingDown();
	}

	bool IsSQLite() const
	{
		return m_conn.IsSQLite();
	}

	bool IsMySQL() const
	{
		return m_conn.IsMySQL();
	}

	// Attempt reconnection (called periodically when the connection is lost).
	void Reconnect(std::function<void(bool)> callback);

	// Convenience: run a query with a callback.
	void Query(const char *query, std::function<void(ISQLQuery *)> callback);

	// Convenience: run a formatted query with a callback.
	void QueryFmt(std::function<void(ISQLQuery *)> callback, const char *fmt, ...);

	// Escape a string for safe SQL insertion.
	std::string Escape(const char *str);

	// Generate SQL fragment matching a Steam authid by suffix (works in both MySQL and SQLite).
	// Returns e.g.: "(authid LIKE 'STEAM_0:0:12345' OR authid LIKE 'STEAM_1:0:12345')"
	static std::string AuthMatch(const char *column, const std::string &escapedSuffix)
	{
		return mmu::sql::AuthMatch(column, escapedSuffix);
	}

	// Create database schema tables if they don't exist.
	void CreateSchema();

private:
	// Build connection params from the loaded config.
	mmu::sql::ConnectParams BuildParams() const;

	mmu::sql::Connection m_conn;
};

extern CS2ADatabase g_CS2ADatabase;

#endif // _INCLUDE_ADMIN_DATABASE_H_
