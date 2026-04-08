#include "database.h"
#include "../common.h"
#include "../config/config.h"

#include <sql_mm.h>
#include <mysql_mm.h>
#include <sqlite_mm.h>

#include <cstdarg>

CS2ADatabase g_CS2ADatabase;

CS2ADatabase::~CS2ADatabase()
{
	Shutdown();
}

bool CS2ADatabase::Init()
{
	m_pSQLInterface = static_cast<ISQLInterface *>(
		g_SMAPI->MetaFactory(SQLMM_INTERFACE, nullptr, nullptr));

	if (!m_pSQLInterface)
	{
		META_CONPRINTF("[ADMIN] Failed to get ISQLInterface. Is sql_mm loaded?\n");
		return false;
	}

	// Determine database type from config
	if (g_CS2AConfig.dbType == "sqlite")
	{
		m_dbType = DatabaseType::SQLite;
		m_pSQLiteClient = m_pSQLInterface->GetSQLiteClient();
		if (!m_pSQLiteClient)
		{
			META_CONPRINTF("[ADMIN] Failed to get SQLite client from sql_mm.\n");
			return false;
		}
		META_CONPRINTF("[ADMIN] Database type: SQLite (standalone mode)\n");
	}
	else
	{
		m_dbType = DatabaseType::MySQL;
		m_pMySQLClient = m_pSQLInterface->GetMySQLClient();
		if (!m_pMySQLClient)
		{
			META_CONPRINTF("[ADMIN] Failed to get MySQL client from sql_mm.\n");
			return false;
		}
		META_CONPRINTF("[ADMIN] Database type: MySQL\n");
	}

	m_bInitialized = true;
	return true;
}

void CS2ADatabase::Connect(std::function<void(bool)> callback)
{
	if (m_dbType == DatabaseType::SQLite)
	{
		if (!m_pSQLiteClient)
		{
			META_CONPRINTF("[ADMIN] Cannot connect: SQLite client not initialized.\n");
			if (callback) callback(false);
			return;
		}

		// Path is relative to the game directory (e.g. game/csgo/)
		// sql_mm resolves it via g_pFullFileSystem->RelativePathToFullPath()
		SQLiteConnectionInfo info;
		info.database = g_CS2AConfig.dbPath.c_str();

		m_pConnection = m_pSQLiteClient->CreateSQLiteConnection(info);
		if (!m_pConnection)
		{
			META_CONPRINTF("[ADMIN] Failed to create SQLite connection object.\n");
			if (callback) callback(false);
			return;
		}

		m_pConnection->Connect([this, callback](bool success) {
			m_bConnected = success;
			if (success)
			{
				META_CONPRINTF("[ADMIN] SQLite database connected successfully.\n");
				// Enable WAL mode for better concurrent performance
				Query("PRAGMA journal_mode=WAL", [](ISQLQuery *) {});
				Query("PRAGMA foreign_keys=ON", [](ISQLQuery *) {});
				// Create schema if needed
				CreateSchema();
			}
			else
			{
				META_CONPRINTF("[ADMIN] SQLite database connection failed.\n");
			}
			if (callback) callback(success);
		});
	}
	else
	{
		if (!m_pMySQLClient)
		{
			META_CONPRINTF("[ADMIN] Cannot connect: MySQL client not initialized.\n");
			if (callback) callback(false);
			return;
		}

		if (g_CS2AConfig.dbHost.empty() || g_CS2AConfig.dbName.empty())
		{
			META_CONPRINTF("[ADMIN] Cannot connect: database host or name is empty. Check core.cfg.\n");
			if (callback) callback(false);
			return;
		}

		MySQLConnectionInfo info;
		info.host = g_CS2AConfig.dbHost.c_str();
		info.user = g_CS2AConfig.dbUser.c_str();
		info.pass = g_CS2AConfig.dbPass.c_str();
		info.database = g_CS2AConfig.dbName.c_str();
		info.port = g_CS2AConfig.dbPort;

		m_pConnection = m_pMySQLClient->CreateMySQLConnection(info);
		if (!m_pConnection)
		{
			META_CONPRINTF("[ADMIN] Failed to create MySQL connection object.\n");
			if (callback) callback(false);
			return;
		}

		m_pConnection->Connect([this, callback](bool success) {
			m_bConnected = success;
			if (success)
			{
				META_CONPRINTF("[ADMIN] MySQL database connected successfully.\n");
				Query("SET NAMES utf8mb4", [](ISQLQuery *) {});
				// Create schema if needed
				CreateSchema();
			}
			else
			{
				META_CONPRINTF("[ADMIN] MySQL database connection failed.\n");
			}
			if (callback) callback(success);
		});
	}
}

void CS2ADatabase::Shutdown()
{
	if (m_pConnection)
	{
		m_pConnection->Destroy();
		m_pConnection = nullptr;
	}
	m_bConnected = false;
	m_bInitialized = false;
	m_pMySQLClient = nullptr;
	m_pSQLiteClient = nullptr;
	m_pSQLInterface = nullptr;
}

void CS2ADatabase::Reconnect(std::function<void(bool)> callback)
{
	if (!m_bInitialized)
	{
		if (callback) callback(false);
		return;
	}

	if (m_dbType == DatabaseType::MySQL && !m_pMySQLClient)
	{
		if (callback) callback(false);
		return;
	}

	if (m_dbType == DatabaseType::SQLite && !m_pSQLiteClient)
	{
		if (callback) callback(false);
		return;
	}

	if (m_bConnected)
	{
		if (callback) callback(true);
		return;
	}

	// Destroy old connection object if any
	if (m_pConnection)
	{
		m_pConnection->Destroy();
		m_pConnection = nullptr;
	}

	Connect(callback);
}

void CS2ADatabase::Query(const char *query, std::function<void(ISQLQuery *)> callback)
{
	if (!m_pConnection || !m_bConnected)
	{
		META_CONPRINTF("[ADMIN] Cannot query: not connected.\n");
		if (callback)
			callback(nullptr);
		return;
	}

	// sql_mm requires a valid callback, never pass a null std::function
	m_pConnection->Query(query, callback ? callback : [](ISQLQuery *){});
}

void CS2ADatabase::QueryFmt(std::function<void(ISQLQuery *)> callback, const char *fmt, ...)
{
	char buffer[4096];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	Query(buffer, callback);
}

std::string CS2ADatabase::Escape(const char *str)
{
	if (!m_pConnection)
		return str ? str : "";
	return m_pConnection->Escape(str);
}

std::string CS2ADatabase::AuthMatch(const char *column, const std::string &escapedSuffix)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "(%s LIKE 'STEAM_0:%s' OR %s LIKE 'STEAM_1:%s')",
		column, escapedSuffix.c_str(), column, escapedSuffix.c_str());
	return std::string(buf);
}

void CS2ADatabase::CreateSchema()
{
	const char *prefix = g_CS2AConfig.databasePrefix.c_str();
	char query[4096];

	if (IsSQLite())
	{
		// schema (standalone mode)
		// Mirrors the SBPP schema with SQLite-compatible types

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_bans ("
			"bid INTEGER PRIMARY KEY AUTOINCREMENT, "
			"ip TEXT DEFAULT NULL, "
			"authid TEXT NOT NULL DEFAULT '', "
			"name TEXT NOT NULL DEFAULT 'unnamed', "
			"created INTEGER NOT NULL DEFAULT 0, "
			"ends INTEGER NOT NULL DEFAULT 0, "
			"length INTEGER NOT NULL DEFAULT 0, "
			"reason TEXT NOT NULL, "
			"aid INTEGER NOT NULL DEFAULT 0, "
			"adminIp TEXT NOT NULL DEFAULT '', "
			"sid INTEGER NOT NULL DEFAULT 0, "
			"country TEXT DEFAULT NULL, "
			"RemovedBy INTEGER DEFAULT NULL, "
			"RemoveType TEXT DEFAULT NULL, "
			"RemovedOn INTEGER DEFAULT NULL, "
			"type INTEGER NOT NULL DEFAULT 0, "
			"ureason TEXT DEFAULT NULL"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE INDEX IF NOT EXISTS idx_%s_bans_sid ON %s_bans (sid)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});
		snprintf(query, sizeof(query),
			"CREATE INDEX IF NOT EXISTS idx_%s_bans_type_authid ON %s_bans (type, authid)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});
		snprintf(query, sizeof(query),
			"CREATE INDEX IF NOT EXISTS idx_%s_bans_type_ip ON %s_bans (type, ip)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_comms ("
			"bid INTEGER PRIMARY KEY AUTOINCREMENT, "
			"authid TEXT NOT NULL, "
			"name TEXT NOT NULL DEFAULT 'unnamed', "
			"created INTEGER NOT NULL DEFAULT 0, "
			"ends INTEGER NOT NULL DEFAULT 0, "
			"length INTEGER NOT NULL DEFAULT 0, "
			"reason TEXT NOT NULL, "
			"aid INTEGER NOT NULL DEFAULT 0, "
			"adminIp TEXT NOT NULL DEFAULT '', "
			"sid INTEGER NOT NULL DEFAULT 0, "
			"RemovedBy INTEGER DEFAULT NULL, "
			"RemoveType TEXT DEFAULT NULL, "
			"RemovedOn INTEGER DEFAULT NULL, "
			"type INTEGER NOT NULL DEFAULT 0, "
			"ureason TEXT DEFAULT NULL"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE INDEX IF NOT EXISTS idx_%s_comms_sid ON %s_comms (sid)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});
		snprintf(query, sizeof(query),
			"CREATE INDEX IF NOT EXISTS idx_%s_comms_type ON %s_comms (type)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});
		snprintf(query, sizeof(query),
			"CREATE INDEX IF NOT EXISTS idx_%s_comms_authid ON %s_comms (authid)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});
		snprintf(query, sizeof(query),
			"CREATE INDEX IF NOT EXISTS idx_%s_comms_created ON %s_comms (created)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});
		snprintf(query, sizeof(query),
			"CREATE INDEX IF NOT EXISTS idx_%s_comms_RemoveType ON %s_comms (RemoveType)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});
		snprintf(query, sizeof(query),
			"CREATE INDEX IF NOT EXISTS idx_%s_comms_aid ON %s_comms (aid)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_admins ("
			"aid INTEGER PRIMARY KEY AUTOINCREMENT, "
			"user TEXT NOT NULL, "
			"authid TEXT NOT NULL DEFAULT '', "
			"password TEXT NOT NULL, "
			"gid INTEGER NOT NULL, "
			"email TEXT NOT NULL, "
			"validate TEXT DEFAULT NULL, "
			"extraflags INTEGER NOT NULL DEFAULT 0, "
			"immunity INTEGER NOT NULL DEFAULT 0, "
			"srv_group TEXT DEFAULT NULL, "
			"srv_flags TEXT DEFAULT NULL, "
			"srv_password TEXT DEFAULT NULL, "
			"lastvisit INTEGER DEFAULT NULL"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE UNIQUE INDEX IF NOT EXISTS idx_%s_admins_user ON %s_admins (user)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_srvgroups ("
			"id INTEGER PRIMARY KEY AUTOINCREMENT, "
			"flags TEXT NOT NULL DEFAULT '', "
			"immunity INTEGER NOT NULL DEFAULT 0, "
			"name TEXT NOT NULL DEFAULT '', "
			"groups_immune TEXT NOT NULL DEFAULT ''"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_srvgroups_overrides ("
			"id INTEGER PRIMARY KEY AUTOINCREMENT, "
			"group_id INTEGER NOT NULL, "
			"type TEXT NOT NULL DEFAULT 'command', "
			"name TEXT NOT NULL DEFAULT '', "
			"access TEXT NOT NULL DEFAULT 'allow'"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE UNIQUE INDEX IF NOT EXISTS idx_%s_sgo_gtn ON %s_srvgroups_overrides (group_id, type, name)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_overrides ("
			"id INTEGER PRIMARY KEY AUTOINCREMENT, "
			"type TEXT NOT NULL DEFAULT 'command', "
			"name TEXT NOT NULL DEFAULT '', "
			"flags TEXT NOT NULL DEFAULT ''"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE UNIQUE INDEX IF NOT EXISTS idx_%s_overrides_tn ON %s_overrides (type, name)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_admins_servers_groups ("
			"admin_id INTEGER NOT NULL DEFAULT 0, "
			"group_id INTEGER NOT NULL DEFAULT 0, "
			"srv_group_id INTEGER NOT NULL DEFAULT 0, "
			"server_id INTEGER NOT NULL DEFAULT 0"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_servers ("
			"sid INTEGER PRIMARY KEY AUTOINCREMENT, "
			"ip TEXT NOT NULL DEFAULT '', "
			"port INTEGER NOT NULL DEFAULT 0, "
			"rcon TEXT NOT NULL DEFAULT '', "
			"modid INTEGER NOT NULL DEFAULT 0, "
			"enabled INTEGER NOT NULL DEFAULT 1"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE UNIQUE INDEX IF NOT EXISTS idx_%s_servers_ip_port ON %s_servers (ip, port)",
			prefix, prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_servers_groups ("
			"server_id INTEGER NOT NULL DEFAULT 0, "
			"group_id INTEGER NOT NULL DEFAULT 0, "
			"PRIMARY KEY (server_id, group_id)"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_log ("
			"lid INTEGER PRIMARY KEY AUTOINCREMENT, "
			"type TEXT NOT NULL DEFAULT 'm', "
			"title TEXT NOT NULL DEFAULT '', "
			"message TEXT NOT NULL DEFAULT '', "
			"function TEXT NOT NULL DEFAULT '', "
			"query TEXT NOT NULL DEFAULT '', "
			"aid INTEGER NOT NULL DEFAULT 0, "
			"host TEXT NOT NULL DEFAULT '', "
			"created INTEGER NOT NULL DEFAULT 0"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS %s_submissions ("
			"subid INTEGER PRIMARY KEY AUTOINCREMENT, "
			"submitted INTEGER NOT NULL DEFAULT 0, "
			"ModID INTEGER NOT NULL DEFAULT 0, "
			"SteamId TEXT NOT NULL DEFAULT '', "
			"name TEXT NOT NULL DEFAULT '', "
			"email TEXT NOT NULL DEFAULT '', "
			"reason TEXT NOT NULL, "
			"ip TEXT NOT NULL DEFAULT '', "
			"subname TEXT DEFAULT NULL, "
			"sip TEXT DEFAULT NULL, "
			"archiv INTEGER DEFAULT 0, "
			"archivedby INTEGER DEFAULT NULL, "
			"server INTEGER DEFAULT NULL"
			")", prefix);
		Query(query, [](ISQLQuery *) {});

		META_CONPRINTF("[ADMIN] SQLite schema created/verified.\n");
	}
	else
	{
		// MySQL schema (exact SBPP schema)

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_bans` ("
			"`bid` int(6) NOT NULL auto_increment, "
			"`ip` varchar(32) default NULL, "
			"`authid` varchar(64) NOT NULL default '', "
			"`name` varchar(128) NOT NULL default 'unnamed', "
			"`created` int(11) NOT NULL default '0', "
			"`ends` int(11) NOT NULL default '0', "
			"`length` int(10) NOT NULL default '0', "
			"`reason` text NOT NULL, "
			"`aid` int(6) NOT NULL default '0', "
			"`adminIp` varchar(128) NOT NULL default '', "
			"`sid` int(6) NOT NULL default '0', "
			"`country` varchar(4) default NULL, "
			"`RemovedBy` int(8) NULL, "
			"`RemoveType` VARCHAR(3) NULL, "
			"`RemovedOn` int(10) NULL, "
			"`type` TINYINT NOT NULL DEFAULT '0', "
			"`ureason` text, "
			"PRIMARY KEY (`bid`), "
			"KEY `sid` (`sid`), "
			"KEY `type_authid` (`type`,`authid`), "
			"KEY `type_ip` (`type`,`ip`)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_comms` ("
			"`bid` int(6) NOT NULL AUTO_INCREMENT, "
			"`authid` varchar(64) NOT NULL, "
			"`name` varchar(128) NOT NULL DEFAULT 'unnamed', "
			"`created` int(11) NOT NULL DEFAULT '0', "
			"`ends` int(11) NOT NULL DEFAULT '0', "
			"`length` int(10) NOT NULL DEFAULT '0', "
			"`reason` text NOT NULL, "
			"`aid` int(6) NOT NULL DEFAULT '0', "
			"`adminIp` varchar(128) NOT NULL DEFAULT '', "
			"`sid` int(6) NOT NULL DEFAULT '0', "
			"`RemovedBy` int(8) DEFAULT NULL, "
			"`RemoveType` varchar(3) DEFAULT NULL, "
			"`RemovedOn` int(11) DEFAULT NULL, "
			"`type` tinyint(4) NOT NULL DEFAULT '0', "
			"`ureason` text, "
			"PRIMARY KEY (`bid`), "
			"KEY `sid` (`sid`), "
			"KEY `type` (`type`), "
			"KEY `RemoveType` (`RemoveType`), "
			"KEY `authid` (`authid`), "
			"KEY `created` (`created`), "
			"KEY `aid` (`aid`)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_admins` ("
			"`aid` int(6) NOT NULL auto_increment, "
			"`user` varchar(64) NOT NULL, "
			"`authid` varchar(64) NOT NULL default '', "
			"`password` varchar(128) NOT NULL, "
			"`gid` int(6) NOT NULL, "
			"`email` varchar(128) NOT NULL, "
			"`validate` varchar(128) NULL default NULL, "
			"`extraflags` int(10) NOT NULL, "
			"`immunity` int(10) NOT NULL default '0', "
			"`srv_group` varchar(128) default NULL, "
			"`srv_flags` varchar(64) default NULL, "
			"`srv_password` varchar(128) default NULL, "
			"`lastvisit` int(11) NULL, "
			"PRIMARY KEY (`aid`), "
			"UNIQUE KEY `user` (`user`)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_srvgroups` ("
			"`id` int(10) unsigned NOT NULL auto_increment, "
			"`flags` varchar(30) NOT NULL, "
			"`immunity` int(10) unsigned NOT NULL, "
			"`name` varchar(120) NOT NULL, "
			"`groups_immune` varchar(255) NOT NULL, "
			"PRIMARY KEY (`id`)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_srvgroups_overrides` ("
			"`id` int(11) NOT NULL AUTO_INCREMENT, "
			"`group_id` smallint(5) unsigned NOT NULL, "
			"`type` enum('command','group') NOT NULL, "
			"`name` varchar(32) NOT NULL, "
			"`access` enum('allow','deny') NOT NULL, "
			"PRIMARY KEY (`id`), "
			"UNIQUE KEY `group_id` (`group_id`,`type`,`name`)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_overrides` ("
			"`id` int(11) NOT NULL AUTO_INCREMENT, "
			"`type` enum('command','group') NOT NULL, "
			"`name` varchar(32) NOT NULL, "
			"`flags` varchar(30) NOT NULL, "
			"PRIMARY KEY (`id`), "
			"UNIQUE KEY `type` (`type`,`name`)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_admins_servers_groups` ("
			"`admin_id` int(10) NOT NULL, "
			"`group_id` int(10) NOT NULL, "
			"`srv_group_id` int(10) NOT NULL, "
			"`server_id` int(10) NOT NULL"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_servers` ("
			"`sid` int(6) NOT NULL auto_increment, "
			"`ip` varchar(64) NOT NULL, "
			"`port` int(5) NOT NULL, "
			"`rcon` varchar(64) NOT NULL, "
			"`modid` int(10) NOT NULL, "
			"`enabled` TINYINT NOT NULL DEFAULT '1', "
			"PRIMARY KEY (`sid`), "
			"UNIQUE KEY `ip` (`ip`,`port`)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_servers_groups` ("
			"`server_id` int(10) NOT NULL, "
			"`group_id` int(10) NOT NULL, "
			"PRIMARY KEY (`server_id`,`group_id`)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_log` ("
			"`lid` int(11) NOT NULL auto_increment, "
			"`type` enum('m','w','e') NOT NULL, "
			"`title` varchar(512) NOT NULL, "
			"`message` text NOT NULL, "
			"`function` text NOT NULL, "
			"`query` text NOT NULL, "
			"`aid` int(11) NOT NULL, "
			"`host` text NOT NULL, "
			"`created` int(11) NOT NULL, "
			"PRIMARY KEY (`lid`)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		snprintf(query, sizeof(query),
			"CREATE TABLE IF NOT EXISTS `%s_submissions` ("
			"`subid` int(6) NOT NULL auto_increment, "
			"`submitted` int(11) NOT NULL, "
			"`ModID` int(6) NOT NULL, "
			"`SteamId` varchar(64) NOT NULL default 'unnamed', "
			"`name` varchar(128) NOT NULL, "
			"`email` varchar(128) NOT NULL, "
			"`reason` text NOT NULL, "
			"`ip` varchar(64) NOT NULL, "
			"`subname` varchar(128) default NULL, "
			"`sip` varchar(64) default NULL, "
			"`archiv` tinyint(1) default '0', "
			"`archivedby` INT(11) NULL, "
			"`server` tinyint(3) default NULL, "
			"PRIMARY KEY (`subid`)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4", prefix);
		Query(query, [](ISQLQuery *) {});

		META_CONPRINTF("[ADMIN] MySQL schema created/verified.\n");
	}
}
