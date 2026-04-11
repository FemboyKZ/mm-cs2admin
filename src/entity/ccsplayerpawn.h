#ifndef _INCLUDE_ADMIN_ENTITY_CCSPLAYERPAWN_H_
#define _INCLUDE_ADMIN_ENTITY_CCSPLAYERPAWN_H_

#include "schema.h"
#include "cbaseentity.h"

// Matches CPlayerPawnComponent vtable layout from the game.
// We define all virtual methods to ensure correct vtable indices for derived classes.
class CPlayerPawnComponent
{
	virtual ~CPlayerPawnComponent() = 0;
	virtual void unk_01() = 0;
	virtual void unk_02() = 0;
	virtual void unk_03() = 0;
	virtual void unk_04() = 0;
	virtual void unk_05() = 0;
	virtual void unk_06() = 0;
	virtual void unk_07() = 0;
	virtual void unk_08() = 0;
	virtual void unk_09() = 0;
	virtual void unk_10() = 0;
	virtual void unk_11() = 0;
	virtual void unk_12() = 0;
	virtual void unk_13() = 0;
	virtual void unk_14() = 0;
	virtual void unk_15() = 0;
	virtual void unk_16() = 0;
	virtual void unk_17() = 0;
	virtual void unk_18() = 0;
};

class CPlayer_ItemServices : public CPlayerPawnComponent
{
	virtual ~CPlayer_ItemServices() = 0;
};

// CCSPlayer_ItemServices virtual method layout (matches CS2Fixes).
class CCSPlayer_ItemServices : public CPlayer_ItemServices
{
	virtual ~CCSPlayer_ItemServices() = 0;

private:
	virtual void *_GiveNamedItem(const char *name) = 0;

public:
	virtual bool GiveNamedItemBool(const char *name) = 0;
	virtual void *GiveNamedItem(const char *name) = 0;
	virtual void DropActiveWeapon(void *weapon) = 0;
	virtual void StripPlayerWeapons(bool removeSuit) = 0;
};

// CBasePlayerPawn : CBaseEntity
// Schema class name must match game's class for field resolution.
class CBasePlayerPawn : public CBaseEntity
{
public:
	DECLARE_SCHEMA_CLASS(CBasePlayerPawn)

	SCHEMA_FIELD(CCSPlayer_ItemServices *, m_pItemServices)
};

// CCSPlayerPawn : CBasePlayerPawn (via CCSPlayerPawnBase in game hierarchy)
class CCSPlayerPawn : public CBasePlayerPawn
{
public:
	DECLARE_SCHEMA_CLASS(CCSPlayerPawn)
};

#endif // _INCLUDE_ADMIN_ENTITY_CCSPLAYERPAWN_H_
