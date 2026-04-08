#ifndef _INCLUDE_ADMIN_STEAM_UTILS_H_
#define _INCLUDE_ADMIN_STEAM_UTILS_H_

#include <cstdint>
#include <string>

// Utility: convert SteamID64 to "X:Y" suffix (the part after STEAM_)
// SBPP stores authids as STEAM_0:X:Y, but queries with REGEXP '^STEAM_[0-9]:X:Y$'
// Steam universe digit can vary, so we store as STEAM_0: but query with regex.
inline std::string SteamID64ToSuffix(uint64_t steamid64)
{
	// SteamID64 format: upper 32 bits = metadata, lower 32 bits = account info
	// Account ID = steamid64 & 0xFFFFFFFF
	// Y = AccountID & 1
	// Z = AccountID >> 1
	uint32_t accountId = static_cast<uint32_t>(steamid64 & 0xFFFFFFFF);
	uint32_t y = accountId & 1;
	uint32_t z = accountId >> 1;
	return std::to_string(y) + ":" + std::to_string(z);
}

inline std::string SteamID64ToAuthId(uint64_t steamid64)
{
	std::string suffix = SteamID64ToSuffix(steamid64);
	return "STEAM_0:" + suffix;
}

// Helper: extract the "X:Y" suffix from a "STEAM_0:X:Y" auth ID safely.
// Returns the input as-is if the format is unexpected.
inline std::string ExtractAuthSuffix(const std::string &authid)
{
	// Find second colon in "STEAM_0:X:Y" => skip past "STEAM_0:" (8 chars)
	if (authid.size() > 10 && authid[7] == ':' && authid[9] == ':')
		return authid.substr(8);

	// Fallback: try to find first colon manually, return everything after it
	size_t first = authid.find(':');
	if (first != std::string::npos && first + 1 < authid.size())
		return authid.substr(first + 1);

	return authid;
}

#endif //_INCLUDE_ADMIN_STEAM_UTILS_H_
