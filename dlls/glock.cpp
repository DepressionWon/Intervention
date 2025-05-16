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

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "player.h"
#include "UserMessages.h"

LINK_ENTITY_TO_CLASS( weapon_glock, CGlock );
LINK_ENTITY_TO_CLASS( weapon_9mmhandgun, CGlock );
//LINK_ENTITY_TO_CLASS_SPECIAL(weapon_silencer, CGlock, CGlock_SpawnSilenced);

void CGlock_SpawnSilenced(entvars_s* pev)
{
	pev->body = 1;
}

void CGlock::Spawn( )
{
	pev->classname = MAKE_STRING("weapon_9mmhandgun"); // hack to allow for old names
	Precache( );
	m_iId = WEAPON_GLOCK;

	if (pev->body != 0)
		SET_MODEL(ENT(pev), "models/w_silencer.mdl");
	else
		SET_MODEL(ENT(pev), "models/w_9mmhandgun.mdl");

	m_iDefaultAmmo = GLOCK_DEFAULT_GIVE;

	FallInit();// get ready to fall down.

	if (pev->body != 0)
		SetTouch(&CGlock::DefaultTouch);
}

void CGlock::DefaultTouch(CBaseEntity* pOther)
{
	if (!pOther->IsPlayer())
		return;

	auto pPlayer = reinterpret_cast<CBasePlayer*>(pOther);

	if (!pPlayer->HasWeaponBit(WEAPON_GLOCK_SILENCER) && pev->body != 0)
	{
		SET_MODEL(ENT(pev), "models/w_9mmhandgun.mdl");
		pPlayer->SetWeaponBit(WEAPON_GLOCK_SILENCER);
		m_bSilencer = true;

#ifndef CLIENT_DLL 
		if (pPlayer->HasWeaponBit(WEAPON_GLOCK))
		{
			MESSAGE_BEGIN(MSG_ONE, gmsgWeapPickup, NULL, pOther->pev);
			WRITE_BYTE(WEAPON_GLOCK);
			MESSAGE_END();
		}
#endif
		EMIT_SOUND(ENT(pPlayer->pev), CHAN_ITEM, "items/gunpickup2.wav", 1, ATTN_NORM);
	}

	CBasePlayerWeapon::DefaultTouch(pOther);
}

void CGlock::Precache( void )
{
	PRECACHE_MODEL("models/v_9mmhandgun.mdl");
	PRECACHE_MODEL("models/w_9mmhandgun.mdl");
	PRECACHE_MODEL("models/p_9mmhandgun.mdl");

	PRECACHE_MODEL("models/w_silencer.mdl");

	m_iShell = PRECACHE_MODEL ("models/shell.mdl");// brass shell

	PRECACHE_SOUND("items/9mmclip1.wav");
	PRECACHE_SOUND("items/9mmclip2.wav");

	PRECACHE_SOUND ("weapons/pl_gun1.wav");//silenced handgun
	PRECACHE_SOUND ("weapons/pl_gun2.wav");//silenced handgun
	PRECACHE_SOUND ("weapons/pl_gun3.wav");//handgun

	m_usFireGlock1 = PRECACHE_EVENT( 1, "events/glock1.sc" );
	m_usFireGlock2 = PRECACHE_EVENT( 1, "events/glock2.sc" );
}

int CGlock::GetItemInfo(ItemInfo *p)
{
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "9mm";
	p->iMaxAmmo1 = _9MM_MAX_CARRY;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = GLOCK_MAX_CLIP;
	p->iSlot = 1;
	p->iPosition = 0;
	p->iFlags = 0;
	p->iId = m_iId = WEAPON_GLOCK;
	p->iWeight = GLOCK_WEIGHT;

	return 1;
}

int CGlock::AddToPlayer(CBasePlayer* pPlayer)
{
	return CBasePlayerWeapon::AddToPlayer(pPlayer);
}

BOOL CGlock::Deploy( )
{
	pev->body = m_bSilencer ? 1 : 0;
	return DefaultDeploy("models/v_9mmhandgun.mdl", "models/p_9mmhandgun.mdl", GLOCK_DRAW, "onehanded", pev->body);
}

void CGlock::SecondaryAttack( void )
{
	if (!m_pPlayer->HasWeaponBit(WEAPON_GLOCK_SILENCER))
		return;

	if (m_bSilencer)
	{
		SendWeaponAnim(GLOCK_HOLSTER);
		m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 1.0f;
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 2.0f;
	}
	else
	{
		SendWeaponAnim(GLOCK_ADD_SILENCER);
		m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.8f;
		m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 3.3f;
	}

	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 4.0f;

	m_iSilencerState = 1;
}

void CGlock::PrimaryAttack( void )
{
	//GlockFire( 0.01, 0.3, TRUE );
	if ((m_pPlayer->m_iBtnAttackBits & IN_ATTACK) != 0)
		return;

	GlockFire(0.01, 0.1, TRUE);
}

void CGlock::GlockFire( float flSpread , float flCycleTime, BOOL fUseAutoAim )
{
	if (m_iClip <= 0)
	{
		if (m_fFireOnEmpty)
		{
			PlayEmptySound();
			m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + 0.2;
		}

		return;
	}

	m_iClip--;

	m_pPlayer->pev->effects = (int)(m_pPlayer->pev->effects) | EF_MUZZLEFLASH;

	int flags;

#if defined( CLIENT_WEAPONS )
	flags = FEV_NOTHOST;
#else
	flags = 0;
#endif

	// player "shoot" animation
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

	// silenced
	if (pev->body == 1)
	{
		m_pPlayer->m_iWeaponVolume = QUIET_GUN_VOLUME;
		m_pPlayer->m_iWeaponFlash = DIM_GUN_FLASH;
	}
	else
	{
		// non-silenced
		m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
		m_pPlayer->m_iWeaponFlash = NORMAL_GUN_FLASH;
	}

	Vector vecSrc	 = m_pPlayer->GetGunPosition( );
	Vector vecAiming;
	
	if ( fUseAutoAim )
	{
		vecAiming = m_pPlayer->GetAutoaimVector( AUTOAIM_10DEGREES );
	}
	else
	{
		UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->m_vecRecoil);
		vecAiming = gpGlobals->v_forward;
	}

	Vector vecDir;
	vecDir = m_pPlayer->FireBulletsPlayer( 1, vecSrc, vecAiming, Vector( flSpread, flSpread, flSpread ), 8192, BULLET_PLAYER_9MM, 0, 0, m_pPlayer->pev, m_pPlayer->random_seed );

	PLAYBACK_EVENT_FULL(flags, m_pPlayer->edict(), fUseAutoAim ? m_usFireGlock1 : m_usFireGlock2, 0.0, (float*)&g_vecZero, (float*)&g_vecZero, vecDir.x, vecDir.y, m_pPlayer->m_vecRecoil[0] * 1000, 0, (m_iClip == 0) ? 1 : 0, 0);

	m_pPlayer->m_iBtnAttackBits |= IN_ATTACK;

	m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + flCycleTime;

	if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
		// HEV suit - indicate out of ammo condition
		m_pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);

	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
}


void CGlock::Reload( void )
{
	if ( m_pPlayer->ammo_9mm <= 0 )
		 return;

	int iResult;

	if (m_iClip == 0)
		iResult = DefaultReload( 17, GLOCK_RELOAD, 1.65 );
	else
		iResult = DefaultReload( 17, GLOCK_RELOAD_NOT_EMPTY, 1.65 );

	if (iResult)
	{
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 10, 15 );
	}
}

void CGlock::ItemPostFrame()
{
	if (m_iSilencerState != 0)
	{
		if (!m_bSilencer)
		{
			if (m_iSilencerState == 1)
			{
				m_iSilencerState = 2;
				SetBody(1);
				m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 2.1f;
			}
			else if (m_iSilencerState == 2)
			{
				m_iSilencerState = 0;
				m_bSilencer = true;
			}
		}
		else
		{
			if (m_iSilencerState != 0)
			{
				m_iSilencerState = 0;
				m_bSilencer = false;
				SendWeaponAnim(GLOCK_DRAW);
				SetBody(0);
				m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.3f;
			}
		}
	}

	CBasePlayerWeapon::ItemPostFrame();
}

void CGlock::Holster(int skiplocal)
{
	m_fInReload = false; // cancel any reload in progress.

	m_iSilencerState = 0;

	SendWeaponAnim(GLOCK_HOLSTER);
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.98f;
}

void CGlock::WeaponIdle( void )
{
	ResetEmptySound( );

	m_pPlayer->GetAutoaimVector( AUTOAIM_10DEGREES );

	if ( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;

	//	Prevent client side jankiness
#ifndef CLIENT_DLL 
	SetBody(m_bSilencer ? 1 : 0);
#endif
	// only idle if the slid isn't back
	if (m_iClip != 0)
	{
		int iAnim;
		float flRand = UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.0, 1.0 );

		if (flRand <= 0.3 + 0 * 0.75)
		{
			iAnim = GLOCK_IDLE3;
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 49.0 / 16;
		}
		else if (flRand <= 0.6 + 0 * 0.875)
		{
			iAnim = GLOCK_IDLE1;
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 60.0 / 16.0;
		}
		else
		{
			iAnim = GLOCK_IDLE2;
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 40.0 / 16.0;
		}
		SendWeaponAnim( iAnim, 1 );
	}
}





class CGlockAmmo : public CBasePlayerAmmo
{
	void Spawn( void )
	{ 
		Precache( );
		SET_MODEL(ENT(pev), "models/w_9mmclip.mdl");
		CBasePlayerAmmo::Spawn( );
	}
	void Precache( void )
	{
		PRECACHE_MODEL ("models/w_9mmclip.mdl");
		PRECACHE_SOUND("items/9mmclip1.wav");
	}
	BOOL AddAmmo( CBaseEntity *pOther ) 
	{ 
		if (pOther->GiveAmmo( AMMO_GLOCKCLIP_GIVE, "9mm", _9MM_MAX_CARRY ) != -1)
		{
			EMIT_SOUND(ENT(pev), CHAN_ITEM, "items/9mmclip1.wav", 1, ATTN_NORM);
			return TRUE;
		}
		return FALSE;
	}
};
LINK_ENTITY_TO_CLASS( ammo_glockclip, CGlockAmmo );
LINK_ENTITY_TO_CLASS( ammo_9mmclip, CGlockAmmo );















