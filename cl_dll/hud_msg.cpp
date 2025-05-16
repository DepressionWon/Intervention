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
//
//  hud_msg.cpp
//

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "r_efx.h"
#include "com_weapons.h"

#define MAX_CLIENTS 32

extern BEAM *pBeam;
extern BEAM *pBeam2;
extern tempent_s* pLaserSpot;

extern void EV_HLDM_Particles(vec_t Pos_X, vec_t Pos_Y, vec_t Pos_Z, float PosNorm_X, float PosNorm_Y, float PosNorm_Z, int DoPuff, int Material);
extern void EV_HLDM_WaterSplash(float x, float y, float z);

/// USER-DEFINED SERVER MESSAGE HANDLERS

int CHud :: MsgFunc_ResetHUD(const char *pszName, int iSize, void *pbuf )
{
	ASSERT( iSize == 0 );

	// clear all hud data
	HUDLIST *pList = m_pHudList;

	while ( pList )
	{
		if ( pList->p )
			pList->p->Reset();
		pList = pList->pNext;
	}

	// reset sensitivity
	m_flMouseSensitivity = 0;

	//Reset weapon bits.
	m_iWeaponBits = 0ULL;

	// reset concussion effect
	m_iConcussionEffect = 0;

	return 1;
}

void CAM_ToFirstPerson(void);

void CHud :: MsgFunc_ViewMode( const char *pszName, int iSize, void *pbuf )
{
	CAM_ToFirstPerson();
}

void CHud :: MsgFunc_InitHUD( const char *pszName, int iSize, void *pbuf )
{
	// prepare all hud data
	HUDLIST *pList = m_pHudList;

	while (pList)
	{
		if ( pList->p )
			pList->p->InitHUDData();
		pList = pList->pNext;
	}

	//Probably not a good place to put this.
	pBeam = pBeam2 = NULL;

	pLaserSpot = NULL;
}


int CHud :: MsgFunc_GameMode(const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	m_Teamplay = READ_BYTE();

	return 1;
}


int CHud :: MsgFunc_Damage(const char *pszName, int iSize, void *pbuf )
{
	int		armor, blood;
	Vector	from;
	int		i;
	float	count;
	
	BEGIN_READ( pbuf, iSize );
	armor = READ_BYTE();
	blood = READ_BYTE();

	for (i=0 ; i<3 ; i++)
		from[i] = READ_COORD();

	count = (blood * 0.5) + (armor * 0.5);

	if (count < 10)
		count = 10;

	// TODO: kick viewangles,  show damage visually

	return 1;
}

int CHud :: MsgFunc_Concuss( const char *pszName, int iSize, void *pbuf )
{
	BEGIN_READ( pbuf, iSize );
	m_iConcussionEffect = READ_BYTE();
	if (m_iConcussionEffect)
	{
		int r, g, b;
		UnpackRGB(r, g, b, RGB_YELLOWISH);
		this->m_StatusIcons.EnableIcon("dmg_concuss", r, g, b);
	}
	else
		this->m_StatusIcons.DisableIcon("dmg_concuss");
	return 1;
}

bool CHud::MsgFunc_Weapons(const char* pszName, int iSize, void* pbuf)
{
	BEGIN_READ(pbuf, iSize);

	const std::uint64_t lowerBits = READ_LONG();
	const std::uint64_t upperBits = READ_LONG();

	m_iWeaponBits = lowerBits | (upperBits << 32ULL);

	return true;
}

bool CHud::MsgFunc_WeaponAnim(const char* pszName, int iSize, void* pbuf)
{
	BEGIN_READ(pbuf, iSize);
	int iAnim = READ_BYTE();
	int iBody = READ_SHORT();

	CL_SendWeaponAnim(iAnim, iBody);
	return true;
}

bool CHud::MsgFunc_WaterSplash(const char* pszName, int iSize, void* pbuf)
{
	BEGIN_READ(pbuf, iSize);
	float X, Y, Z;
	X = READ_COORD();
	Y = READ_COORD();
	Z = READ_COORD();

	EV_HLDM_WaterSplash(X, Y, Z);
	return 1;


}

bool CHud::MsgFunc_Impact(const char* pszName, int iSize, void* pbuf)
{
	BEGIN_READ(pbuf, iSize);
	int MatType = READ_SHORT();
	int DoPuffSpr = READ_BYTE();

	vec_t Pos_X, Pos_Y, Pos_Z;
	float PosNorm_X, PosNorm_Y, PosNorm_Z;

	Pos_X = READ_COORD();
	Pos_Y = READ_COORD();
	Pos_Z = READ_COORD();
	PosNorm_X = READ_COORD();
	PosNorm_Y = READ_COORD();
	PosNorm_Z = READ_COORD();

	EV_HLDM_Particles(Pos_X, Pos_Y, Pos_Z, PosNorm_X, PosNorm_Y, PosNorm_Z, DoPuffSpr, MatType);
	return true;
}

bool CHud::MsgFunc_PlayerSpawn(const char* pszName, int iSize, void* pbuf)
{
	BEGIN_READ(pbuf, iSize);

	gHUD.m_flAbsTime = 0;
	gHUD.m_flWeaponAnimTime = 0;
	gHUD.cachedviewmodel.curstate.sequence = -1;

	return true;
}

bool CHud::MsgFunc_SetBody(const char* pszName, int iSize, void* pbuf)
{
	BEGIN_READ(pbuf, iSize);

	int body = READ_SHORT();

	auto view = gEngfuncs.GetViewModel();

	view->curstate.body = body;

	return true;
}
