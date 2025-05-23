/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   This source code contains proprietary and confidential information of
*   Valve LLC and its suppliers.  Access to this code is restricted to
*   persons who have executed a written SDK license with Valve.  Any access,
*   use or distribution of this code by or to any unlicensed person is illegal.
*
****/
/*

===== h_cine.cpp ========================================================

  The Halflife hard coded "scripted sequence".

  I'm pretty sure all this code is obsolete

*/

#include	"extdll.h"
#include	"util.h"
#include	"cbase.h"
#include	"monsters.h"
#include	"decals.h"


class CLegacyCineMonster : public CBaseMonster
{
public:
	void CineSpawn( char *szModel );
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void EXPORT CineThink( void );
	void Pain( void );
	void Die( void );
};

class CCineScientist : public CLegacyCineMonster
{
public:
	void Spawn( void ) { CineSpawn("models/cine-scientist.mdl"); }
};
class CCine2Scientist : public CLegacyCineMonster
{
public:
	void Spawn( void ) { CineSpawn("models/cine2-scientist.mdl"); }
};
class CCinePanther : public CLegacyCineMonster
{
public:
	void Spawn( void ) { CineSpawn("models/cine-panther.mdl"); }
};

class CCineBarney : public CLegacyCineMonster
{
public:
	void Spawn( void ) { CineSpawn("models/cine-barney.mdl"); }
};

class CCine2HeavyWeapons : public CLegacyCineMonster
{
public:
	void Spawn( void ) { CineSpawn("models/cine2_hvyweapons.mdl"); }
};

class CCine2Slave : public CLegacyCineMonster
{
public:
	void Spawn( void ) { CineSpawn("models/cine2_slave.mdl"); }
};

class CCine3Scientist : public CLegacyCineMonster
{
public:
	void Spawn( void ) { CineSpawn("models/cine3-scientist.mdl"); }
};

class CCine3Barney : public CLegacyCineMonster
{
public:
	void Spawn( void ) { CineSpawn("models/cine3-barney.mdl"); }
};

//
// ********** Scientist SPAWN **********
//

LINK_ENTITY_TO_CLASS( monster_cine_scientist, CCineScientist );
LINK_ENTITY_TO_CLASS( monster_cine_panther, CCinePanther );
LINK_ENTITY_TO_CLASS( monster_cine_barney, CCineBarney );
LINK_ENTITY_TO_CLASS( monster_cine2_scientist, CCine2Scientist );
LINK_ENTITY_TO_CLASS( monster_cine2_hvyweapons, CCine2HeavyWeapons );
LINK_ENTITY_TO_CLASS( monster_cine2_slave, CCine2Slave );
LINK_ENTITY_TO_CLASS( monster_cine3_scientist, CCine3Scientist );
LINK_ENTITY_TO_CLASS( monster_cine3_barney, CCine3Barney );

//
// ********** Scientist SPAWN **********
//

void CLegacyCineMonster :: CineSpawn( char *szModel )
{
	PRECACHE_MODEL(szModel);
	SET_MODEL(ENT(pev), szModel);
	UTIL_SetSize(pev, Vector(-16, -16, 0), Vector(16, 16, 64));

	pev->solid			= SOLID_SLIDEBOX;
	pev->movetype		= MOVETYPE_STEP;
	pev->effects		= 0;
	pev->health			= 1;
	pev->yaw_speed		= 10;
	
	// ugly alpha hack, can't set ints from the bsp.	
	pev->sequence		= (int)pev->impulse;
	ResetSequenceInfo( );
	pev->framerate = 0.0;

	m_bloodColor = BLOOD_COLOR_RED;

	// if no targetname, start now
	if ( FStringNull(pev->targetname) )	
	{
		SetThink( &CLegacyCineMonster::CineThink );
		pev->nextthink += 1.0;
	}
}


//
// CineStart
//
void CLegacyCineMonster :: Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	pev->animtime = 0;	// reset the sequence
	SetThink( &CLegacyCineMonster::CineThink );
	pev->nextthink = gpGlobals->time;
}

//
// ********** Scientist DIE **********
//
void CLegacyCineMonster :: Die( void )
{
	SetThink( &CBaseEntity::SUB_Remove );
}

//
// ********** Scientist PAIN **********
//
void CLegacyCineMonster :: Pain( void )
{
	EMIT_SOUND(ENT(pev), CHAN_VOICE, "player/pain3.wav", 1, ATTN_NORM);
}

void CLegacyCineMonster :: CineThink( void )
{
	// DBG_CheckMonsterData(pev);
	
	// Emit particles from origin (double check animator's placement of model)
	// THIS is a test feature
	//UTIL_ParticleEffect(pev->origin, g_vecZero, 255, 20);

	if (!pev->animtime)
		ResetSequenceInfo( );

	pev->nextthink = gpGlobals->time + 1.0;

	if (pev->spawnflags != 0 && m_fSequenceFinished)
	{
		Die();
		return;
	}

	StudioFrameAdvance ( );
}