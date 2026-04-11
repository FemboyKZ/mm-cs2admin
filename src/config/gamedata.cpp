#include "gamedata.h"
#include "kv_parser.h"
#include "../common.h"

#include <fstream>
#include <algorithm>
#include <cstdlib>

CS2AGameData g_CS2AGameData;

static const char *GetPlatformKey()
{
#ifdef _WIN32
	return "windows";
#else
	return "linux";
#endif
}

// Gamedata KV handler. Offset subsections contain platform keys:
//   "OffsetName" { "windows" "88" "linux" "80" }
// We resolve to current platform during parse.
struct GameDataParseCtx
{
	CS2AGameData *gd;
	std::string currentOffset; // name of the offset subsection being parsed
};

static void GameDataHandler(const std::string &section, const std::string &key,
	const std::string &value, void *userdata)
{
	auto *ctx = static_cast<GameDataParseCtx *>(userdata);

	// Inside an offset subsection: key is platform, value is the offset
	std::string sectionLower = section;
	std::transform(sectionLower.begin(), sectionLower.end(), sectionLower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	std::string keyLower = key;
	std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (keyLower == GetPlatformKey())
	{
		// The section name is the offset name
		ctx->gd->SetOffset(section, std::atoi(value.c_str()));
	}
}

bool CS2AGameData::Load(const char *path)
{
	std::ifstream file(path);
	if (!file.is_open())
		return false;

	// Expect: "GameData" { "Offsets" { ... } }
	kv::Token root = kv::NextToken(file);
	if (root.kind != kv::TokenType::String)
		return false;

	kv::Token brace = kv::NextToken(file);
	if (brace.kind != kv::TokenType::OpenBrace)
		return false;

	GameDataParseCtx ctx;
	ctx.gd = this;
	kv::ParseSection(file, root.value, GameDataHandler, &ctx);
	return true;
}

int CS2AGameData::GetOffset(const char *name) const
{
	auto it = m_offsets.find(name);
	if (it != m_offsets.end())
		return it->second;
	return -1;
}

void CS2AGameData::SetOffset(const std::string &name, int value)
{
	m_offsets[name] = value;
}
