/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
/*

===== lights.cpp ========================================================

  spawn and think functions for editor-placed lights

*/

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "UserMessages.h"

class CLight : public CPointEntity
{
public:
	virtual void	KeyValue( KeyValueData* pkvd ); 
	virtual void	Spawn( void );
	void	Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void	SendInitMessages( CBaseEntity* pPlayer = NULL );

	virtual int		Save( CSave &save );
	virtual int		Restore( CRestore &restore );
	
	static	TYPEDESCRIPTION m_SaveData[];

private:
	int		m_iStyle;
	int		m_iszPattern;
	int		m_colorR;
	int		m_colorG;
	int		m_colorB;
	int		m_brightness;
	BOOL	m_isActive;
};
LINK_ENTITY_TO_CLASS( light, CLight );

TYPEDESCRIPTION	CLight::m_SaveData[] = 
{
	DEFINE_FIELD( CLight, m_iStyle, FIELD_INTEGER ),
	DEFINE_FIELD( CLight, m_iszPattern, FIELD_STRING ),
	DEFINE_FIELD( CLight, m_colorR, FIELD_INTEGER ),
	DEFINE_FIELD( CLight, m_colorG, FIELD_INTEGER ),
	DEFINE_FIELD( CLight, m_colorB, FIELD_INTEGER ),
	DEFINE_FIELD( CLight, m_brightness, FIELD_INTEGER ),
	DEFINE_FIELD( CLight, m_isActive, FIELD_BOOLEAN ),

};

IMPLEMENT_SAVERESTORE( CLight, CPointEntity );


//
// Cache user-entity-field values until spawn is called.
//
void CLight :: KeyValue( KeyValueData* pkvd)
{
	if (FStrEq(pkvd->szKeyName, "_light"))
	{
		int r, g, b, v, j;
		v = 0;

		j = sscanf( pkvd->szValue, "%d %d %d %d\n", &r, &g, &b, &v );
		if (j == 1)
			g = b = r;

		if(!v)
			v = 64;
		
		m_colorR = r;
		m_colorG = g;
		m_colorB = b;
		m_brightness = v;
	}
	else if (FStrEq(pkvd->szKeyName, "style"))
	{
		m_iStyle = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "pitch"))
	{
		pev->angles.x = atof(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "pattern"))
	{
		m_iszPattern = ALLOC_STRING( pkvd->szValue );
		pkvd->fHandled = TRUE;
	}
	else
	{
		CPointEntity::KeyValue( pkvd );
	}
}

/*QUAKED light (0 1 0) (-8 -8 -8) (8 8 8) LIGHT_START_OFF
Non-displayed light.
Default light value is 300
Default style is 0
If targeted, it will toggle between on or off.
*/

void CLight :: Spawn( void )
{
	if (m_iStyle >= 32)
	{
//		CHANGE_METHOD(ENT(pev), em_use, light_use);
		if (FBitSet(pev->spawnflags, SF_LIGHT_START_OFF))
		{
			LIGHT_STYLE(m_iStyle, "a");
			m_isActive = FALSE;
		}
		else
		{
			if (m_iszPattern)
				LIGHT_STYLE(m_iStyle, (char *)STRING( m_iszPattern ));
			else
				LIGHT_STYLE(m_iStyle, "m");

			m_isActive = TRUE;
		}
	}
}


void CLight :: Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if (m_iStyle >= 32)
	{
		if ( !ShouldToggle( useType, m_isActive ) )
			return;

		if (!m_isActive)
		{
			if (m_iszPattern)
				LIGHT_STYLE(m_iStyle, (char *)STRING( m_iszPattern ));
			else
				LIGHT_STYLE(m_iStyle, "m");

			m_isActive = TRUE;
		}
		else
		{
			LIGHT_STYLE(m_iStyle, "a");
			m_isActive = FALSE;
		}

		SendInitMessages(NULL);
	}
}

void CLight::SendInitMessages ( CBaseEntity* pPlayer )
{
	if(pPlayer && !m_isActive)
		return;

	if(pPlayer)
		MESSAGE_BEGIN(MSG_ONE, gmsgELight, NULL, pPlayer->pev);
	else
		MESSAGE_BEGIN(MSG_ALL, gmsgELight, NULL);

	WRITE_SHORT(entindex());
	WRITE_BYTE(m_isActive ? 1 : 0);

	if(m_isActive)
	{
		WRITE_COORD(pev->origin.x);
		WRITE_COORD(pev->origin.y);
		WRITE_COORD(pev->origin.z);
		WRITE_BYTE(m_colorR);
		WRITE_BYTE(m_colorG);
		WRITE_BYTE(m_colorB);
		WRITE_COORD((float)m_brightness / 9);
	}
	MESSAGE_END();
}

//
// shut up spawn functions for new spotlights
//
LINK_ENTITY_TO_CLASS( light_spot, CLight );


class CEnvLight : public CLight
{
public:
	void	KeyValue( KeyValueData* pkvd ); 
	void	Spawn( void );
};

LINK_ENTITY_TO_CLASS( light_environment, CEnvLight );

void CEnvLight::KeyValue( KeyValueData* pkvd )
{
	if (FStrEq(pkvd->szKeyName, "_light"))
	{
		int r, g, b, v, j;
		char szColor[64];
		j = sscanf( pkvd->szValue, "%d %d %d %d\n", &r, &g, &b, &v );
		if (j == 1)
		{
			g = b = r;
		}
		else if (j == 4)
		{
			r = r * (v / 255.0);
			g = g * (v / 255.0);
			b = b * (v / 255.0);
		}

		// simulate qrad direct, ambient,and gamma adjustments, as well as engine scaling
		r = pow( r / 114.0, 0.6 ) * 264;
		g = pow( g / 114.0, 0.6 ) * 264;
		b = pow( b / 114.0, 0.6 ) * 264;

		pkvd->fHandled = TRUE;
		sprintf( szColor, "%d", r );
		CVAR_SET_STRING( "sv_skycolor_r", szColor );
		sprintf( szColor, "%d", g );
		CVAR_SET_STRING( "sv_skycolor_g", szColor );
		sprintf( szColor, "%d", b );
		CVAR_SET_STRING( "sv_skycolor_b", szColor );
	}
	else
	{
		CLight::KeyValue( pkvd );
	}
}


void CEnvLight :: Spawn( void )
{
	char szVector[64];
	UTIL_MakeAimVectors( pev->angles );

	sprintf( szVector, "%f", gpGlobals->v_forward.x );
	CVAR_SET_STRING( "sv_skyvec_x", szVector );
	sprintf( szVector, "%f", gpGlobals->v_forward.y );
	CVAR_SET_STRING( "sv_skyvec_y", szVector );
	sprintf( szVector, "%f", gpGlobals->v_forward.z );
	CVAR_SET_STRING( "sv_skyvec_z", szVector );

	CLight::Spawn( );
}
