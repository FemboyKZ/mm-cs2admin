#ifndef _INCLUDE_ADMIN_GAMEDATA_H_
#define _INCLUDE_ADMIN_GAMEDATA_H_

#include <string>
#include <map>

class CS2AGameData
{
public:
	// Load gamedata from a KV1 file. Returns true on success.
	bool Load(const char *path);

	// Get a named offset for the current platform. Returns -1 if not found.
	int GetOffset(const char *name) const;

	// Set an offset (used internally by the parser).
	void SetOffset(const std::string &name, int value);

private:
	// Offsets keyed by name, already resolved to current platform
	std::map<std::string, int> m_offsets;
};

extern CS2AGameData g_CS2AGameData;

#endif // _INCLUDE_ADMIN_GAMEDATA_H_
