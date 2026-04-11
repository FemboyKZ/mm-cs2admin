#ifndef _INCLUDE_ADMIN_ENTITY_CCSPLAYERCONTROLLER_H_
#define _INCLUDE_ADMIN_ENTITY_CCSPLAYERCONTROLLER_H_

#include "schema.h"
#include "cbaseentity.h"
#include <entity2/entitysystem.h>
#include <entity2/entityidentity.h>
#include <entityhandle.h>

// Resolve a CEntityHandle to a CEntityInstance via the entity system.
inline CEntityInstance *ResolveEntityHandle(const CEntityHandle &handle)
{
	extern CGameEntitySystem *g_pEntitySystem;
	if (!handle.IsValid() || !g_pEntitySystem)
		return nullptr;

	int entIndex = handle.GetEntryIndex();
	int chunk = entIndex / MAX_ENTITIES_IN_LIST;
	int offset = entIndex % MAX_ENTITIES_IN_LIST;

	if (chunk < 0 || chunk >= MAX_ENTITY_LISTS)
		return nullptr;

	CEntityIdentity *pChunk = g_pEntitySystem->m_EntityList.m_pIdentityChunks[chunk];
	if (!pChunk)
		return nullptr;

	CEntityIdentity *pIdent = &pChunk[offset];
	if (!pIdent || !pIdent->m_pInstance)
		return nullptr;

	return pIdent->m_pInstance;
}

// CBasePlayerController : CBaseEntity
// m_hPawn lives on this class in the game schema.
class CBasePlayerController : public CBaseEntity
{
public:
	DECLARE_SCHEMA_CLASS(CBasePlayerController)

	SCHEMA_FIELD(CEntityHandle, m_hPawn)
};

// CCSPlayerController : CBasePlayerController
class CCSPlayerController : public CBasePlayerController
{
public:
	DECLARE_SCHEMA_CLASS(CCSPlayerController)

	SCHEMA_FIELD(bool, m_bPawnIsAlive)
	SCHEMA_FIELD(uint32_t, m_iPawnHealth)

	// Get controller from player slot (slot 0 -> entity index 1)
	static CCSPlayerController *FromSlot(int slot)
	{
		extern CGameEntitySystem *g_pEntitySystem;
		if (!g_pEntitySystem)
			return nullptr;

		// Player controllers are at entity index (slot + 1)
		int entIndex = slot + 1;

		// Access the entity list directly: each chunk holds 512 identities
		int chunk = entIndex / MAX_ENTITIES_IN_LIST;
		int offset = entIndex % MAX_ENTITIES_IN_LIST;

		if (chunk < 0 || chunk >= MAX_ENTITY_LISTS)
			return nullptr;

		CEntityIdentity *pChunk = g_pEntitySystem->m_EntityList.m_pIdentityChunks[chunk];
		if (!pChunk)
			return nullptr;

		CEntityIdentity *pIdent = &pChunk[offset];
		if (!pIdent || !pIdent->m_pInstance)
			return nullptr;

		return reinterpret_cast<CCSPlayerController *>(pIdent->m_pInstance);
	}
};

// CS2 team constants
constexpr int CS_TEAM_NONE = 0;
constexpr int CS_TEAM_SPECTATOR = 1;
constexpr int CS_TEAM_T = 2;
constexpr int CS_TEAM_CT = 3;

#endif // _INCLUDE_ADMIN_ENTITY_CCSPLAYERCONTROLLER_H_
