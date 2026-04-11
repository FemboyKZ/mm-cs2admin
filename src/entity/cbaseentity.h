#ifndef _INCLUDE_ADMIN_ENTITY_CBASEENTITY_H_
#define _INCLUDE_ADMIN_ENTITY_CBASEENTITY_H_

#include "schema.h"
#include <entity2/entityinstance.h>

// CBaseEntity : CEntityInstance
// Only fields we actually need for targeting.
class CBaseEntity : public CEntityInstance
{
public:
	DECLARE_SCHEMA_CLASS(CBaseEntity)

	SCHEMA_FIELD(int, m_iTeamNum)
	SCHEMA_FIELD(int, m_iHealth)
	SCHEMA_FIELD(uint8_t, m_lifeState)
};

#endif // _INCLUDE_ADMIN_ENTITY_CBASEENTITY_H_
