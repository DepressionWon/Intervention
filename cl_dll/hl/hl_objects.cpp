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
#include "../hud.h"
#include "../cl_util.h"
#include "../demo.h"

#include "demo_api.h"
#include "const.h"
#include "entity_state.h"
#include "cl_entity.h"

#include "pm_defs.h"
#include "event_api.h"
#include "entity_types.h"
#include "r_efx.h"
#include "ref_params.h"

extern BEAM *pBeam;
extern BEAM *pBeam2;
extern dlight_t* pLight;

void HUD_GetLastOrg( float *org );
ref_params_s g_pparams;

extern float g_flDmgTime;
extern byte g_fFireMode;

#define EGON_DISCHARGE_INTERVAL 0.1

void UpdateBeams ( void )
{
	float timedist = 0.0f;
	float r = 50.0f;
	float g = 50.0f;
	float b = 125.0f;

	static float randmultiplier = 0;

	vec3_t forward, vecSrc, vecEnd, origin, angles, right, up;
	vec3_t view_ofs;
	pmtrace_t tr;
	cl_entity_t *pthisplayer = gEngfuncs.GetLocalPlayer();
	int idx = pthisplayer->index;
	cl_entity_s* view = gEngfuncs.GetViewModel();
		
	// Get our exact viewangles from engine
	gEngfuncs.GetViewAngles( (float *)angles );

	// Determine our last predicted origin
	HUD_GetLastOrg( (float *)&origin );

	AngleVectors( angles, forward, right, up );

	VectorCopy( origin, vecSrc );
	
	VectorMA( vecSrc, 2048, forward, vecEnd );

	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, true );	
						
	// Store off the old count
	gEngfuncs.pEventAPI->EV_PushPMStates();
					
	// Now add in all of the players.
	gEngfuncs.pEventAPI->EV_SetSolidPlayers ( idx - 1 );	

	gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	gEngfuncs.pEventAPI->EV_PlayerTrace(vecSrc, vecEnd, PM_NORMAL, -1, &tr);

	gEngfuncs.pEventAPI->EV_PopPMStates();

	pBeam->source = view->attachment[0];
	pBeam2->source = view->attachment[0];

	if (g_flDmgTime < gEngfuncs.GetClientTime())
	{
		g_flDmgTime = gEngfuncs.GetClientTime() + EGON_DISCHARGE_INTERVAL;
	}

	timedist = (g_flDmgTime - gEngfuncs.GetClientTime()) / EGON_DISCHARGE_INTERVAL;

	if (timedist < 0)
		timedist = 0;
	else if (timedist > 1)
		timedist = 1;
	timedist = 1 - timedist;

	if ( pBeam )
	{
		pBeam->target = tr.endpos;
		pBeam->die	  = gEngfuncs.GetClientTime() + 0.1; // We keep it alive just a little bit forward in the future, just in case.

		// Fix speed of client beam
		pBeam->freq = pBeam->speed * gEngfuncs.GetClientTime();

		// WIDE
		if (g_fFireMode == 1)
		{
			pBeam->r = (30 + (25 * timedist)) / 255.0f;
			pBeam->g = (30 + (30 * timedist)) / 255.0f;
		}
		else
		{
			pBeam->r = (60 + (25 * timedist)) / 255.0f;
			pBeam->g = (120 + (30 * timedist)) / 255.0f;
		}
		pBeam->b = (64 + 80 * fabs(sin(gEngfuncs.GetClientTime() * 10))) / 255.0f;
	}
		
	if ( pBeam2 )
	{
		pBeam2->target = tr.endpos;
		pBeam2->die	   = gEngfuncs.GetClientTime() + 0.1; // We keep it alive just a little bit forward in the future, just in case.

		// Fix speed of client beam
		pBeam2->freq = pBeam2->speed * gEngfuncs.GetClientTime();
	}

	if (pLight)
	{
		pLight->origin = vecSrc;
		pLight->radius = lerp(pLight->radius, gEngfuncs.pfnRandomFloat(128, 256), gHUD.m_flTimeDelta * 17.0f);
		randmultiplier = lerp(randmultiplier, gEngfuncs.pfnRandomFloat(0.1f, 8.0f), gHUD.m_flTimeDelta * 15.0f);

		pLight->color.r = min(max(r * randmultiplier, 0), 255);
		pLight->color.g = min(max(g * randmultiplier, 0), 255);
		pLight->color.b = min(max(b * randmultiplier, 0), 255);

		pLight->die = gEngfuncs.GetClientTime() + 0.75f;
	}
}

/*
=====================
Game_AddObjects

Add game specific, client-side objects here
=====================
*/
void Game_AddObjects( void )
{
	if (pBeam || pBeam2)
		UpdateBeams();
}

void UpdateFlashlight(ref_params_t* pparams)
{
	dlight_t* dl;
	Vector forward, vecSrc, vecEnd, origin, angles, right, up;
	Vector view_ofs;
	pmtrace_t tr;
	cl_entity_t* pthisplayer = gEngfuncs.GetLocalPlayer();
	int idx = pthisplayer->index;

	if (!gHUD.m_bFlashlight)
	{
		return;
	}

	origin = pparams->vieworg;
	angles = pparams->viewangles;

	AngleVectors(angles, forward, right, up);

	VectorCopy(origin, vecSrc);

	VectorMA(vecSrc, 8192, forward, vecEnd);

	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction(0, 1);

	// Store off the old count
	gEngfuncs.pEventAPI->EV_PushPMStates();

	// Now add in all of the players.
	gEngfuncs.pEventAPI->EV_SetSolidPlayers(idx - 1);

	gEngfuncs.pEventAPI->EV_SetTraceHull(2);
	gEngfuncs.pEventAPI->EV_PlayerTrace(vecSrc, vecEnd, PM_STUDIO_BOX, -1, &tr);

	gEngfuncs.pEventAPI->EV_PopPMStates();

	dl = gEngfuncs.pEfxAPI->CL_AllocDlight(-999);
	if (dl)
	{
		dl->origin = tr.endpos;
		dl->radius = 100;
		dl->decay = 512;
		dl->die = gEngfuncs.GetClientTime() + 0.1f;
		dl->color = { 255, 255, 255 };
	}
}

#define MAX_LIGHTS 7

tempent_s* pLaserSpot = NULL;
extern cvar_s* cl_lw;

dlight_s* pSpotDl[MAX_LIGHTS] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL };
dlight_s* pSpotEl = { NULL };
void CheckSuspend()
{
	if (gHUD.m_iLaserState > 2 && gHUD.m_iLaserSuspendTime < gEngfuncs.GetClientTime())
	{
		if (gHUD.m_iLaserState == 1 || gHUD.m_iLaserState == 3)
		{
			gHUD.m_iLaserState = 1;
			if (!pLaserSpot)
				pLaserSpot = gEngfuncs.pEfxAPI->R_TempSprite(Vector(0, 0, 0), Vector(0, 0, 0), 1, gEngfuncs.pEventAPI->EV_FindModelIndex("sprites/laserdot.spr"), kRenderGlow, kRenderFxNoDissipation, 1, 99999, 0);
		}
		else if (gHUD.m_iLaserState == 2 || gHUD.m_iLaserState == 4 || gHUD.m_iLaserState == 5)
		{
			gHUD.m_iLaserState = 2;
			if (!pLaserSpot)
				pLaserSpot = gEngfuncs.pEfxAPI->R_TempSprite(Vector(0, 0, 0), Vector(0, 0, 0), 0.5, gEngfuncs.pEventAPI->EV_FindModelIndex("sprites/laserdot.spr"), kRenderGlow, kRenderFxNoDissipation, 1, 99999, 0);
		}
	}
}

void UpdateLaserSpot()
{
	if (!cl_lw->value || !gHUD.m_iLaserState)
	{
		if (pLaserSpot)
		{
			pLaserSpot->die = 0;
			pLaserSpot = NULL;
		}
		return;
	}
	Vector forward, vecSrc, vecEnd, origin, angles, right, up;
	Vector view_ofs;
	pmtrace_t tr;
	cl_entity_t* pthisplayer = gEngfuncs.GetLocalPlayer();
	int idx = pthisplayer->index;
	VectorCopy(g_pparams.vieworg, origin);
	VectorCopy(g_pparams.cl_viewangles, angles);
	AngleVectors(angles, forward, right, up);
	VectorCopy(origin, vecSrc);
	VectorMA(vecSrc, 8192, forward, vecEnd);
	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction(false, true);
	// Store off the old count
	gEngfuncs.pEventAPI->EV_PushPMStates();
	// Now add in all of the players.
	gEngfuncs.pEventAPI->EV_SetSolidPlayers(idx - 1);
	gEngfuncs.pEventAPI->EV_SetTraceHull(2);
	gEngfuncs.pEventAPI->EV_PlayerTrace(vecSrc, vecEnd, PM_STUDIO_BOX, -1, &tr);
	gEngfuncs.pEventAPI->EV_PopPMStates();
	if (pLaserSpot)
	{
		pLaserSpot->entity.origin = pLaserSpot->entity.curstate.origin = tr.endpos;
		pLaserSpot->die = gEngfuncs.GetClientTime() + 0.1;

		if (!pSpotEl)
			pSpotEl = gEngfuncs.pEfxAPI->CL_AllocElight(idx + 1);

		for (int i = 0; i < MAX_LIGHTS; i++)
		{
			if (!pSpotDl[i])
				pSpotDl[i] = gEngfuncs.pEfxAPI->CL_AllocDlight((idx)+i);

			if (pSpotDl[i])
			{
				pSpotDl[i]->color.r = 255;
				pSpotDl[i]->color.g = 0;
				pSpotDl[i]->color.b = 0;
				pSpotDl[i]->radius = 23;
				pSpotDl[i]->origin = pLaserSpot->entity.origin;
				pSpotDl[i]->die = gEngfuncs.GetClientTime() + 0.035f;
			}
		}
		if (pSpotDl)
		{
			pSpotEl->color.r = 255;
			pSpotEl->color.g = 0;
			pSpotEl->color.b = 0;
			pSpotEl->radius = 35;
			pSpotEl->origin = pLaserSpot->entity.origin;
			pSpotEl->die = gEngfuncs.GetClientTime() + 0.035f;
		}
	}
}