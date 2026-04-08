#ifndef _INCLUDE_ADMIN_DATABASE_H_
#define _INCLUDE_ADMIN_DATABASE_H_

#include <functional>
#include <string>

// Forward declarations from sql_mm
class ISQLInterface;
class ISQLConnection;
class ISQLQuery;
class IMySQLClient;
class ISQLiteClient;

enum class DatabaseType
{
	MySQL,
	SQLite
};

class CS2ADatabase
{
public:
	CS2ADatabase() = default;
	~CS2ADatabase();

	// Initialize: acquire ISQLInterface from Metamod's MetaFactory.
	// Must be called in AllPluginsLoaded or later.
	bool Init();

	// Connect to the database using the loaded config.
	// Callback fires on the main thread with success/failure.
	void Connect(std::function<void(bool)> callback);

	// Disconnect and clean up.
	void Shutdown();

	// Is the database connection established?
	bool IsConnected() const { return m_bConnected; }

	// Was Init() successful (sql_mm available)?
	bool IsInitialized() const { return m_bInitialized; }

	// Get the active database type.
	DatabaseType GetType() const { return m_dbType; }
	bool IsSQLite() const { return m_dbType == DatabaseType::SQLite; }
	bool IsMySQL() const { return m_dbType == DatabaseType::MySQL; }

	// Attempt reconnection (called periodically when connection is lost)
	void Reconnect(std::function<void(bool)> callback);

	// Get the raw connection for queries.
	ISQLConnection *GetConnection() const { return m_pConnection; }

	// Convenience: run a query with a callback.
	void Query(const char *query, std::function<void(ISQLQuery *)> callback);

	// Convenience: run a formatted query with a callback.
	void QueryFmt(std::function<void(ISQLQuery *)> callback, const char *fmt, ...);

	// Escape a string for safe SQL insertion.
	std::string Escape(const char *str);

	// Generate SQL fragment matching a Steam authid by suffix (works in both MySQL and SQLite).
	// Returns e.g.: "(authid LIKE 'STEAM_0:0:12345' OR authid LIKE 'STEAM_1:0:12345')"
	static std::string AuthMatch(const char *column, const std::string &escapedSuffix);

	// Create database schema tables if they don't exist.
	void CreateSchema();

private:
	ISQLInterface *m_pSQLInterface = nullptr;
	IMySQLClient *m_pMySQLClient = nullptr;
	ISQLiteClient *m_pSQLiteClient = nullptr;
	ISQLConnection *m_pConnection = nullptr;
	DatabaseType m_dbType = DatabaseType::MySQL;
	bool m_bConnected = false;
	bool m_bInitialized = false;
};

extern CS2ADatabase g_CS2ADatabase;

#endif // _INCLUDE_ADMIN_DATABASE_H_
