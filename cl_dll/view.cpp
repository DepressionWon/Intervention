//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

// view/refresh setup functions

#include "hud.h"
#include "cl_util.h"
#include "cvardef.h"
#include "usercmd.h"
#include "const.h"
#include "r_efx.h" 

#include "entity_state.h"
#include "cl_entity.h"
#include "ref_params.h"
#include "in_defs.h" // PITCH YAW ROLL
#include "pm_movevars.h"
#include "pm_shared.h"
#include "pm_defs.h"
#include "event_api.h"
#include "pmtrace.h"
#include "screenfade.h"
#include "shake.h"
#include "hltv.h"
#include "Exports.h"
#include "fog.h"
#include "svd_render.h"
#include "elightlist.h"

#include "studio.h"
#include "com_model.h"
#include "StudioModelRenderer.h"
#include "GameStudioModelRenderer.h"

#include <cmath>
#include <cassert>
#include <iostream>

extern CGameStudioModelRenderer g_StudioRenderer;

ref_params_s g_refparams;
extern ref_params_s g_pparams;

int CL_IsThirdPerson( void );
void CL_CameraOffset( float *ofs );

void DLLEXPORT V_CalcRefdef( struct ref_params_s *pparams );

void	PM_ParticleLine( float *start, float *end, int pcolor, float life, float vert);
int		PM_GetVisEntInfo( int ent );
int		PM_GetPhysEntInfo( int ent );
void	InterpolateAngles(  float * start, float * end, float * output, float frac );
void	NormalizeAngles( float * angles );
float	Distance(const float * v1, const float * v2);
float	AngleBetweenVectors(  const float * v1,  const float * v2 );

extern float	vJumpOrigin[3];
extern float	vJumpAngles[3];

void ViewPunch(float frametime, float* ev_punchangle, float* punch);
void VectorAngles( const float *forward, float *angles );

void UTIL_SmoothInterpolateAngles(float* startAngle, float* endAngle, float* finalAngle, float degreesPerSec);

#include "r_studioint.h"
#include "kbutton.h"

extern engine_studio_api_t IEngineStudio;

extern kbutton_t in_mlook;

#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

#ifndef DEG2RAD
#define DEG2RAD( a ) ( a * M_PI ) / 180.0F
#endif

/*
The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.
*/

extern cvar_t	*cl_forwardspeed;
extern cvar_t	*chase_active;
extern cvar_t	*scr_ofsx, *scr_ofsy, *scr_ofsz;
extern cvar_t	*cl_vsmoothing;
extern cvar_t	*cl_rollangle;
extern cvar_t	*cl_rollspeed;

#define	CAM_MODE_RELAX		1
#define CAM_MODE_FOCUS		2

// view furstum planes
mplane_t	frustum[4];

vec3_t		v_origin, v_angles, v_cl_angles, v_sim_org, v_lastAngles;
float		v_frametime, v_lastDistance;	
float		v_cameraRelaxAngle	= 5.0f;
float		v_cameraFocusAngle	= 35.0f;
int			v_cameraMode = CAM_MODE_FOCUS;
bool		v_resetCamera = true;

Vector ev_punchangle, ev_punch;
Vector v_jumpangle, v_jumppunch;
Vector duck_punch, duck_angles;
Vector ev_recoilangle;

cvar_t	*scr_ofsx;
cvar_t	*scr_ofsy;
cvar_t	*scr_ofsz;

cvar_t	*v_centermove;
cvar_t	*v_centerspeed;

cvar_t	*cl_bobcycle;
cvar_t	*cl_bob;
cvar_t	*cl_bobup;
cvar_t	*cl_waterdist;
cvar_t	*cl_chasedist;

// These cvars are not registered (so users can't cheat), so set the ->value field directly
// Register these cvars in V_Init() if needed for easy tweaking
cvar_t	v_iyaw_cycle		= {"v_iyaw_cycle", "2", 0, 2};
cvar_t	v_iroll_cycle		= {"v_iroll_cycle", "0.5", 0, 0.5};
cvar_t	v_ipitch_cycle		= {"v_ipitch_cycle", "1", 0, 1};
cvar_t	v_iyaw_level		= {"v_iyaw_level", "0.3", 0, 0.3};
cvar_t	v_iroll_level		= {"v_iroll_level", "0.1", 0, 0.1};
cvar_t	v_ipitch_level		= {"v_ipitch_level", "0.3", 0, 0.3};

float	v_idlescale;  // used by TFC for concussion grenade effect
int HUD_LAG_VALUE; // The sensitivity of the HUD-sway effect is dependent on the screen resolution.
#define VEC_VIEW			Vector( 0, 0, 28 ) // bro be fr
#define VEC_DUCK_VIEW		Vector( 0, 0, 12 )

/*
===============
CalcBob

Bobs the viewmodel
===============
*/
enum class CalcBobMode
{
	VB_COS,
	VB_SIN,
	VB_COS2,
	VB_SIN2
};

// Quakeworld bob code, this fixes jitters in the mutliplayer since the clock (pparams->time) isn't quite linear
void V_CalcBob(struct ref_params_s* pparams, float frequencyMultiplier, const CalcBobMode& mode, double& bobtime, float& bob, float& lasttime)
{
	float cycle;
	Vector vel;

	if (pparams->onground == -1 ||
		pparams->time == lasttime)
	{
		// just use old value
		return;
	}

	lasttime = pparams->time;

	bobtime += pparams->frametime * frequencyMultiplier;

	cycle = bobtime - (int)(bobtime / cl_bobcycle->value) * cl_bobcycle->value;
	cycle /= cl_bobcycle->value;

	if (cycle < cl_bobup->value)
	{
		cycle = M_PI * cycle / cl_bobup->value;
	}
	else
	{
		cycle = M_PI + M_PI * (cycle - cl_bobup->value) / (1.0 - cl_bobup->value);
	}

	// bob is proportional to simulated velocity in the xy plane
	// (don't count Z, or jumping messes it up)
	VectorCopy(pparams->simvel, vel);
	vel[2] = 0;

	bob = sqrt(vel[0] * vel[0] + vel[1] * vel[1]) * cl_bob->value;

	if (mode == CalcBobMode::VB_SIN)
		bob = bob * 0.3 + bob * 0.7 * sin(cycle);
	else if (mode == CalcBobMode::VB_COS)
		bob = bob * 0.3 + bob * 0.7 * cos(cycle);
	else if (mode == CalcBobMode::VB_SIN2)
		bob = bob * 0.3 + bob * 0.7 * sin(cycle) * sin(cycle);
	else if (mode == CalcBobMode::VB_COS2)
		bob = bob * 0.3 + bob * 0.7 * cos(cycle) * cos(cycle);

	bob = min(bob, 4);
	bob = max(bob, -7);
}

/*
===============
V_CalcRoll
Used by view and sv_user
===============
*/
float V_CalcRoll (vec3_t angles, vec3_t velocity, float rollangle, float rollspeed, int angle)
{
    float   sign;
    float   side;
    float   value;
	vec3_t  forward, right, up;
    
	AngleVectors ( angles, forward, right, up );
    
	switch (angle)
	{
	case PITCH:
		side = DotProduct(velocity, forward);
		break;
	case YAW:
		side = DotProduct(velocity, right);
		break;
	default:
	case ROLL:
		side = DotProduct(velocity, up);
		break;
	}

    sign = side < 0 ? -1 : 1;
    side = fabs( side );
    
	value = rollangle;
    if (side < rollspeed)
	{
		side = side * value / rollspeed;
	}
    else
	{
		side = value;
	}
	return side * sign;
}

typedef struct pitchdrift_s
{
	float		pitchvel;
	int			nodrift;
	float		driftmove;
	double		laststop;
} pitchdrift_t;

static pitchdrift_t pd;

void V_StartPitchDrift( void )
{
	if ( pd.laststop == gEngfuncs.GetClientTime() )
	{
		return;		// something else is keeping it from drifting
	}

	if ( pd.nodrift || !pd.pitchvel )
	{
		pd.pitchvel = v_centerspeed->value;
		pd.nodrift = 0;
		pd.driftmove = 0;
	}
}

void V_StopPitchDrift ( void )
{
	pd.laststop = gEngfuncs.GetClientTime();
	pd.nodrift = 1;
	pd.pitchvel = 0;
}

/*
===============
V_DriftPitch

Moves the client pitch angle towards idealpitch sent by the server.

If the user is adjusting pitch manually, either with lookup/lookdown,
mlook and mouse, or klook and keyboard, pitch drifting is constantly stopped.
===============
*/
void V_DriftPitch ( struct ref_params_s *pparams )
{
	float		delta, move;

	if ( gEngfuncs.IsNoClipping() || !pparams->onground || pparams->demoplayback || pparams->spectator )
	{
		pd.driftmove = 0;
		pd.pitchvel = 0;
		return;
	}

	// don't count small mouse motion
	if (pd.nodrift)
	{
		if ( fabs( pparams->cmd->forwardmove ) < cl_forwardspeed->value )
			pd.driftmove = 0;
		else
			pd.driftmove += pparams->frametime;
	
		if ( pd.driftmove > v_centermove->value)
		{
			V_StartPitchDrift ();
		}
		return;
	}
	
	delta = pparams->idealpitch - pparams->cl_viewangles[PITCH];

	if (!delta)
	{
		pd.pitchvel = 0;
		return;
	}

	move = pparams->frametime * pd.pitchvel;
	pd.pitchvel += pparams->frametime * v_centerspeed->value;
	
	if (delta > 0)
	{
		if (move > delta)
		{
			pd.pitchvel = 0;
			move = delta;
		}
		pparams->cl_viewangles[PITCH] += move;
	}
	else if (delta < 0)
	{
		if (move > -delta)
		{
			pd.pitchvel = 0;
			move = -delta;
		}
		pparams->cl_viewangles[PITCH] -= move;
	}
}

/* 
============================================================================== 
						VIEW RENDERING 
============================================================================== 
*/ 

/*
==================
V_CalcGunAngle
==================
*/
void V_CalcGunAngle ( struct ref_params_s *pparams )
{	
	cl_entity_t* viewent;

	viewent = gEngfuncs.GetViewModel();
	if (!viewent)
		return;

	viewent->angles[YAW] = pparams->viewangles[YAW] + pparams->crosshairangle[YAW];
	viewent->angles[PITCH] = -pparams->viewangles[PITCH] + pparams->crosshairangle[PITCH] * 0.25;
	viewent->angles[ROLL] -= (v_idlescale + 0.5f) * sin(pparams->time * v_iroll_cycle.value) * v_iroll_level.value;

	// don't apply all of the v_ipitch to prevent normally unseen parts of viewmodel from coming into view.
	viewent->angles[PITCH] -= (v_idlescale + 0.5f) * sin(pparams->time * v_ipitch_cycle.value) * (v_ipitch_level.value * 0.5);
	viewent->angles[YAW] -= (v_idlescale + 0.5f) * sin(pparams->time * v_iyaw_cycle.value) * v_iyaw_level.value;

	VectorCopy(viewent->angles, viewent->curstate.angles);
	VectorCopy(viewent->angles, viewent->latched.prevangles);
}

/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle ( struct ref_params_s *pparams )
{
	pparams->viewangles[ROLL] += (v_idlescale + 0.25f) * sin(pparams->time * v_iroll_cycle.value) * v_iroll_level.value;
	pparams->viewangles[PITCH] += (v_idlescale + 0.25f) * sin(pparams->time * v_ipitch_cycle.value) * v_ipitch_level.value;
	pparams->viewangles[YAW] += (v_idlescale + 0.25f) * sin(pparams->time * v_iyaw_cycle.value) * v_iyaw_level.value;
}


/*
==============
V_LocalScreenShake

Set screen shake
==============
*/
void V_LocalScreenShake(screen_shake_t* shake, float amplitude, float duration, float frequency)
{
	// don't overwrite larger existing shake
	if (amplitude > shake->amplitude)
		shake->amplitude = amplitude;

	shake->duration = duration;
	shake->time = gHUD.m_flTime + shake->duration;
	shake->frequency = frequency;
	shake->next_shake = 0.0f; // apply immediately
}

/*
=============
V_CalcShake

=============
*/
void V_CalcShake(screen_shake_t* shake)
{
	float frametime, fraction, freq;
	int i;
	float flTime = gHUD.m_flTime;

	if (flTime > shake->time || shake->amplitude <= 0 || shake->frequency <= 0 || shake->duration <= 0)
	{
		// reset shake
		if (shake->time != 0)
		{
			shake->time = 0;
			shake->applied_angle = 0;
			VectorClear(shake->applied_offset);
		}

		return;
	}

	frametime = gHUD.m_flTimeDelta;

	if (flTime > shake->next_shake)
	{
		// get next shake time based on frequency over duration
		shake->next_shake = (float)flTime + shake->frequency / shake->duration;

		// randomize each shake
		for (i = 0; i < 3; i++)
			shake->offset[i] = gEngfuncs.pfnRandomFloat(-shake->amplitude, shake->amplitude);
		shake->angle = gEngfuncs.pfnRandomFloat(-shake->amplitude * 0.25f, shake->amplitude * 0.25f);
	}

	// get initial fraction and frequency values over the duration
	fraction = ((float)flTime - shake->time) / shake->duration;
	freq = fraction != 0.0f ? (shake->frequency / fraction) * shake->frequency : 0.0f;

	// quickly approach zero but apply time over sine wave
	fraction *= fraction * sin(flTime * freq);

	// apply shake offset
	for (i = 0; i < 3; i++)
		shake->applied_offset[i] = shake->offset[i] * fraction;

	// apply roll angle
	shake->applied_angle = shake->angle * fraction;

	// decrease amplitude, but slower on longer shakes or higher frequency
	shake->amplitude -= shake->amplitude * (frametime / (shake->frequency * shake->duration));
}


/*
=============
V_ApplyShake

=============
*/
void V_ApplyShake(screen_shake_t* const shake, float* origin, float* angles, float factor)
{
	if (origin)
		VectorMA(origin, factor, shake->applied_offset, origin);

	if (angles)
		angles[ROLL] += shake->applied_angle * factor;
}


void V_CalcHudLag(ref_params_t* pparams)
{
	const float m_flHudLag = 1.5f;
	const float flScale = 2.0f;
	float flSpeed = 5.0f;

	static Vector m_vecLastFacing;
	static float vel;
	static float smoothedPitch = 0.0f;

	Vector vOriginalAngles = pparams->viewangles;
	Vector yawOnlyAngles = vOriginalAngles;
	yawOnlyAngles[PITCH] = 0;
	Vector forward, right, up;
	AngleVectors(yawOnlyAngles, forward, right, up);

	if (pparams->frametime != 0.0f)
	{
		Vector vDifference = forward - m_vecLastFacing;
		float flDiff = vDifference.Length();

		if ((flDiff > m_flHudLag) && (m_flHudLag > 0.0f))
		{
			float flScaleAdj = flDiff / m_flHudLag;
			flSpeed *= flScaleAdj;
		}

		m_vecLastFacing = m_vecLastFacing + vDifference * (flSpeed * pparams->frametime);
		m_vecLastFacing = m_vecLastFacing.Normalize();
		Vector yawLagOffset = (vDifference * -1.0f) * flScale;

		float pitch = vOriginalAngles[PITCH];
		if (pitch > 180.0f) pitch -= 360.0f;
		if (pitch < -180.0f) pitch += 360.0f;

		float pitchDelta = pitch - smoothedPitch;
		smoothedPitch = lerp(smoothedPitch, pitch, pparams->frametime * 8);

		if (ScreenWidth >= 2560 && ScreenHeight >= 1600)
			HUD_LAG_VALUE = 17;
		else if (ScreenWidth >= 1280 && ScreenHeight > 720)
			HUD_LAG_VALUE = 13;
		else if (ScreenWidth >= 640)
			HUD_LAG_VALUE = 8;
		else
			HUD_LAG_VALUE = 2;

        gHUD.m_flHudLagOfs[0] += V_CalcRoll(vOriginalAngles, yawLagOffset, HUD_LAG_VALUE, 500, YAW) * 280.0f;

		gHUD.m_flHudLagOfs[1] += clamp(pitchDelta * 0.3f, -10.0f, 10.0f);

		float simvelmid = pparams->simvel[2] * 0.007f;
		if (simvelmid <= -1.5f) simvelmid = -1.5f;
		if (simvelmid >= 1.5f) simvelmid = 1.5f;

		vel = SmoothValues(vel, simvelmid, pparams->frametime * 4);
		gHUD.m_flHudLagOfs[1] -= vel * 6.0f;

		gHUD.m_flHudLagOfs[0] = clamp(gHUD.m_flHudLagOfs[0], -25.0f, 25.0f);
		gHUD.m_flHudLagOfs[1] = clamp(gHUD.m_flHudLagOfs[1], -25.0f, 25.0f);
	}
}

void V_HandIntertia(ref_params_t* pparams, cl_entity_s* view, Vector original_angles)
{
	static float vel;
	Vector forward, right, up;
	static float l_mx = 0.0f, l_my = 0.0f;
	static float pitch = -original_angles[PITCH];
	extern int g_mx, g_my;

	const float weaponlagscale = 1.0f;
	const float weaponlagspeed = 7.5f;

	// simplified magic nips viewlag
	if (fabs(pparams->viewangles[0]) >= 86.0f)
	{
		g_my = 0;
	}

	// interpolate mouse movement
	l_mx = lerp(l_mx, (-g_mx * 0.01) * weaponlagscale * (1.0f / pparams->frametime * 0.01), pparams->frametime * weaponlagspeed);
	l_my = lerp(l_my, (g_my * 0.02) * weaponlagscale * (1.0f / pparams->frametime * 0.01), pparams->frametime * weaponlagspeed);

	l_mx = clamp(l_mx, -7.5f, 7.5f);
	l_my = clamp(l_my, -7.5f, 7.5f);

	// apply to viewmodel angles
	view->angles[0] += l_my - ((l_mx > 0.0f) ? (l_mx * 0.5f) : 0.0f);
	view->angles[1] -= l_mx;
	view->angles[2] -= l_mx * 2.5f;

	// apply some to viewroll
	pparams->viewangles[2] += l_mx * 0.15f;

	AngleVectors(Vector(-original_angles[0], original_angles[1], original_angles[2]), forward, right, up);
	pitch = lerp(pitch, -original_angles[PITCH], pparams->frametime * 8.5f);

	view->origin = view->origin - Vector(pparams->right) * (l_mx * 0.4f) - Vector(pparams->up) * (l_my * 0.4f);

	view->origin = view->origin + forward * (-pitch * 0.0175f);
	view->origin = view->origin + right * (-pitch * 0.015f);
	view->origin = view->origin + up * (-pitch * 0.01f);
}

void V_HandleWalls(struct ref_params_s* pparams)
{
	static float flVal = 0.0f;
	int idx = pparams->viewentity;

	pmtrace_t tr;

	Vector vecSrc = pparams->vieworg;
	Vector vecDir = pparams->forward;
	Vector vecEnd = vecSrc + (vecDir * 30);

	auto p = gEngfuncs.GetViewModel();

	if (gHUD.m_iFOV == gHUD.DefaultFov())
	{
		gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction(0, 1);

		// Store off the old count
		gEngfuncs.pEventAPI->EV_PushPMStates();

		// Now add in all of the players.
		gEngfuncs.pEventAPI->EV_SetSolidPlayers(idx - 1);

		gEngfuncs.pEventAPI->EV_SetTraceHull(2);
		gEngfuncs.pEventAPI->EV_PlayerTrace(vecSrc, vecEnd, PM_NORMAL, -1, &tr);

		flVal = lerp(flVal, (1.0f - tr.fraction) * 1.5f, pparams->frametime * 12.0f);

		gEngfuncs.pEventAPI->EV_PopPMStates();
	}
	else
	{
		flVal = lerp(flVal, 0.0f, pparams->frametime * 12.0f);
	}

	p->angles[0] += flVal * 13.0f;
	p->origin -= Vector(pparams->up) * flVal * 4.0f;
	p->origin -= Vector(pparams->forward) * flVal * 8.0f;
}

void V_Jump(struct ref_params_s* pparams, cl_entity_s* view)
{
	auto pl = gEngfuncs.GetLocalPlayer();

	extern kbutton_t in_jump;
	static int iJumpStage = 0;

	static float flFallDist = 0.0f;

	Vector vecSrc = pparams->simorg;
	Vector vecEnd = vecSrc - Vector(pparams->viewheight) - Vector(0, 0, 12);

	pmtrace_t tr;
	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction(0, 1);

	gEngfuncs.pEventAPI->EV_PushPMStates();
	gEngfuncs.pEventAPI->EV_SetTraceHull(2);
	gEngfuncs.pEventAPI->EV_SetSolidPlayers(pparams->viewentity - 1);
	gEngfuncs.pEventAPI->EV_PlayerTrace(vecSrc, vecEnd, PM_NORMAL, -1, &tr);
	gEngfuncs.pEventAPI->EV_PopPMStates();

	if (iJumpStage == 0)
	{
		flFallDist = lerp(flFallDist, 0, pparams->frametime * 17.0f);
	}
	if (pl->curstate.movetype != MOVETYPE_WALK || pparams->waterlevel != 0.0f || (iJumpStage == 0 && (tr.fraction < 1.0)))
	{
		iJumpStage = 0;
	}
	else if (iJumpStage == 0 && (in_jump.state & 1) != 0)
	{
		iJumpStage = 1;
	}
	else if (iJumpStage == 0 && (pparams->onground == 0))
	{
		iJumpStage = 2;
	}
	else if (iJumpStage > 0 && (pparams->onground != 0 || tr.fraction < 1.0))
	{
		v_jumppunch = Vector(-3, 3, 0) * 20.0f;
		iJumpStage = 0;
	}
	else if (iJumpStage == 1)
	{
		v_jumppunch = Vector(-4, 4, 0) * 20.0f;
		iJumpStage = 2;
	}
	else if (iJumpStage == 2)
	{
		flFallDist += pparams->frametime * 5.0f;
		flFallDist = min(flFallDist, 10.0f);
	}

	view->angles = view->angles + v_jumpangle + Vector(flFallDist, -flFallDist, 0);
	pparams->viewangles[0] -= v_jumpangle[0] * 0.5f;
}

void V_ApplyCrouchAngles(struct ref_params_s* pparams, cl_entity_s* view)
{
	extern kbutton_s in_duck;

	VectorAdd(pparams->viewangles, duck_angles / 3.0f, pparams->viewangles);
	VectorAdd(view->angles, duck_angles, view->angles);

	view->origin = view->origin + Vector(pparams->up) * -duck_angles[0] * 0.2f;

	if (g_viewmodelinfo.crouchstate == 1)
	{
		duck_punch[0] = 10 * 2.5;
		g_viewmodelinfo.crouchstate = 2;
	}
	else if (g_viewmodelinfo.crouchstate == 2 && (pparams->viewheight[2] != VEC_DUCK_VIEW[2] && (in_duck.state & 1) == 0))
	{
		duck_punch[0] = -20 * 2.5;
		g_viewmodelinfo.crouchstate = 0;
	}
}

void V_ApplyBob(struct ref_params_s* pparams, cl_entity_s* view)
{
	static double bobTimes[3]{ 0.0f };
	static float lastTimes[3]{ 0.0f };
	static float flBobScale = 1.0f;
	float bobForward, bobRight, bobUp;

	V_CalcBob(pparams, 0.75f, CalcBobMode::VB_SIN, bobTimes[0], bobRight, lastTimes[0]);
	V_CalcBob(pparams, 1.50f, CalcBobMode::VB_SIN, bobTimes[1], bobUp, lastTimes[1]);
	V_CalcBob(pparams, 1.00f, CalcBobMode::VB_SIN, bobTimes[2], bobForward, lastTimes[2]);

	if ((Vector(pparams->simvel).Length2D() - 50.0f) > pparams->movevars->maxspeed)
	{
		flBobScale = lerp(flBobScale, 0.5f, pparams->frametime * 15.0f);
	}
	else
	{
		flBobScale = lerp(flBobScale, 0.2f, pparams->frametime * 15.0f);
	}


	for (int i = 0; i < 3; i++)
	{
		view->origin[i] += (bobRight * 0.72f) * pparams->right[i] * flBobScale;
		view->origin[i] += (bobUp * 0.47f) * pparams->up[i] * flBobScale;
	}

	pparams->viewangles[0] += (bobUp * 0.42f) * flBobScale;
	pparams->viewangles[1] += (bobRight * 0.652f) * flBobScale;

	gHUD.m_flHudLagOfs[0] += (bobRight  - (ev_punchangle[1])) * 3.0f * flBobScale;
	gHUD.m_flHudLagOfs[1] += (bobUp  - (ev_punchangle[0])) * 3.0f * flBobScale;
}

void V_CalcViewRoll ( struct ref_params_s *pparams, cl_entity_s* view, cl_entity_s* viewent)
{
	static float interp_roll = 0;

	float roll = V_CalcRoll(viewent->angles, pparams->simvel, cl_rollangle->value, cl_rollspeed->value, YAW);

	interp_roll = lerp(interp_roll, roll, pparams->frametime * 17.0f);

	view->angles[ROLL] += interp_roll * 1.5f;

	gHUD.m_flHudLagOfs[0] += interp_roll * 1.5f;
}

void V_ApplyPunchAngles(struct ref_params_s* pparams, cl_entity_s* view)
{
	gHUD.m_flHudLagOfs[0] = (ev_punchangle[1]) * 2.5f;
	gHUD.m_flHudLagOfs[1] = (ev_punchangle[0]) * 2.5f;

	// Add in the punchangle, if any
	VectorAdd(pparams->viewangles, pparams->punchangle, pparams->viewangles);

	// calculate punchangles
	ViewPunch(pparams->frametime, (float*)&ev_punchangle, (float*)&ev_punch);
	ViewPunch(pparams->frametime, (float*)&duck_angles, (float*)&duck_punch);
	ViewPunch(pparams->frametime, (float*)&v_jumpangle, (float*)&v_jumppunch);

	v_frametime = pparams->frametime;

	if (gHUD.m_flRecoilTime < gEngfuncs.GetClientTime())
		UTIL_SmoothInterpolateAngles((float*)&ev_recoilangle, Vector(0, 0, 0), (float*)&ev_recoilangle, 25.0f);

	// Include client side punch, too
	VectorAdd(pparams->viewangles, (float*)&ev_punchangle, pparams->viewangles);
	VectorAdd(pparams->viewangles, (float*)&ev_recoilangle, pparams->viewangles);
}

/*
==============
V_CalcViewAngles

==============
*/
void V_CalcViewAngles(struct ref_params_s* pparams, cl_entity_s* view)
{
	cl_entity_t* viewentity;

	Vector c_angle;

	viewentity = gEngfuncs.GetEntityByIndex(pparams->viewentity);
	if (!viewentity)
		return;

	const int idx = 0;
	vec3_t boneangle = g_StudioRenderer.viewboneangles[idx] - g_StudioRenderer.viewfirstboneangles[idx];
	for (int i = 0; i < 3; i++)
	{
		if (fabs(boneangle[i]) > 1)
			continue;

		g_StudioRenderer.lerpedboneangles[i] = lerp(g_StudioRenderer.lerpedboneangles[i], boneangle[i], pparams->frametime * 20.0f);
	}

	V_ApplyPunchAngles(pparams, view);
	V_CalcViewRoll(pparams, view, viewentity);
	V_CalcHudLag(pparams);
	V_ApplyBob(pparams, view);
	V_HandIntertia(pparams, view, view->prevstate.angles);
	V_HandleWalls(pparams);
	V_Jump(pparams, view);
	V_ApplyCrouchAngles(pparams, view);


	// apply angles
	VectorCopy(view->angles, view->curstate.angles);
	VectorCopy(view->angles, view->prevstate.angles);
	VectorAdd(pparams->viewangles, g_StudioRenderer.lerpedboneangles * 1.35f, pparams->viewangles);
}

/*
==================
V_CalcIntermissionRefdef

==================
*/
void V_CalcIntermissionRefdef ( struct ref_params_s *pparams )
{
	cl_entity_t	*ent, *view;
	float		old;

	// ent is the player model ( visible when out of body )
	ent = gEngfuncs.GetLocalPlayer();
	
	// view is the weapon model (only visible from inside body )
	view = gEngfuncs.GetViewModel();

	VectorCopy ( pparams->simorg, pparams->vieworg );
	VectorCopy ( pparams->cl_viewangles, pparams->viewangles );

	view->model = NULL;

	// allways idle in intermission
	old = v_idlescale;
	v_idlescale = 1;

	V_AddIdle ( pparams );

	if ( gEngfuncs.IsSpectateOnly() )
	{
		// in HLTV we must go to 'intermission' position by ourself
		VectorCopy( gHUD.m_Spectator.m_cameraOrigin, pparams->vieworg );
		VectorCopy( gHUD.m_Spectator.m_cameraAngles, pparams->viewangles );
	}

	v_idlescale = old;

	v_cl_angles = pparams->cl_viewangles;
	v_origin = pparams->vieworg;
	v_angles = pparams->viewangles;
}

#define ORIGIN_BACKUP 64
#define ORIGIN_MASK ( ORIGIN_BACKUP - 1 )

typedef struct 
{
	float Origins[ ORIGIN_BACKUP ][3];
	float OriginTime[ ORIGIN_BACKUP ];

	float Angles[ ORIGIN_BACKUP ][3];
	float AngleTime[ ORIGIN_BACKUP ];

	int CurrentOrigin;
	int CurrentAngle;
} viewinterp_t;

/*
 =============
 PLut Client Punch From HL2
 =============
 */
void ViewPunch(float frametime, float* ev_punchangle, float* punch)
{
	float damping;
	float springForceMagnitude;

	constexpr auto PUNCH_DAMPING = 10.0f;		  // bigger number makes the response more damped, smaller is less damped
	// currently the system will overshoot, with larger damping values it won't
	constexpr auto PUNCH_SPRING_CONSTANT = 75.0f; // bigger number increases the speed at which the view corrects


	if (Length(ev_punchangle) > 0.001 || Length(punch) > 0.001)
	{
		VectorMA(ev_punchangle, frametime, punch, ev_punchangle);
		damping = 1 - (PUNCH_DAMPING * frametime);

		if (damping < 0)
		{
			damping = 0;
		}
		VectorScale(punch, damping, punch);

		// torsional spring
		// UNDONE: Per-axis spring constant?
		springForceMagnitude = PUNCH_SPRING_CONSTANT * frametime;
		springForceMagnitude = clamp(springForceMagnitude, 0.0f, 2.0f);
		VectorMA(punch, -springForceMagnitude, ev_punchangle, punch);

		// don't wrap around
		ev_punchangle[0] = clamp(ev_punchangle[0], -15.0f, 15.0f);
		ev_punchangle[1] = clamp(ev_punchangle[1], -179.0f, 179.0f);
		ev_punchangle[2] = clamp(ev_punchangle[2], -15.0f, 15.0f);
	}
}

/*
==================
V_CalcRefdef

==================
*/
void V_CalcNormalRefdef ( struct ref_params_s *pparams )
{
	cl_entity_t		*ent, *view;
	int				i;
	vec3_t			angles;
	float			waterOffset;
	static viewinterp_t		ViewInterp;

	static float oldz = 0;
	static float oldViewz = 0;
	static float lasttime;
	static Vector lastAngles;

	vec3_t camAngles, camForward, camRight, camUp;
	cl_entity_t *pwater;

	static Vector viewheight = VEC_VIEW;

	auto pShake = gHUD.GetScreenShake();

	V_DriftPitch(pparams);

	static float l_deadangle = 0;

	if ( gEngfuncs.IsSpectateOnly() )
	{
		ent = gEngfuncs.GetEntityByIndex( g_iUser2 );
	}
	else
	{
		// ent is the player model ( visible when out of body )
		ent = gEngfuncs.GetLocalPlayer();
	}
	
	// interpolate
	if (pparams->viewheight[2] == 28.0f && viewheight[2] < 28.0f)
	{
		viewheight[2] = lerp(viewheight[2], 28.0f, pparams->frametime * 17.0f);
	}
	else
	{
		viewheight[2] = pparams->viewheight[2];
	}


	// view is the weapon model (only visible from inside body )
	view = gEngfuncs.GetViewModel();

	// refresh position
	VectorCopy ( pparams->simorg, pparams->vieworg );
	VectorAdd(pparams->vieworg, viewheight, pparams->vieworg);

	if (pparams->health <= 0 && (pparams->viewheight[2] != 0))
	{
		// only roll the view if the player is dead and the viewheight[2] is nonzero
		// this is so deadcam in multiplayer will work.
		l_deadangle = lerp(l_deadangle, 80, pparams->frametime * 15.0f); // dead view angle
		pparams->cl_viewangles[0] = lastAngles[0];
		pparams->cl_viewangles[1] = lastAngles[1];
		pparams->cl_viewangles[ROLL] = l_deadangle;
	}
	else
	{
		l_deadangle = 0;
	}

	VectorCopy ( pparams->cl_viewangles, pparams->viewangles );

	gEngfuncs.V_CalcShake();
	gEngfuncs.V_ApplyShake( pparams->vieworg, pparams->viewangles, 1.0 );

	V_CalcShake(pShake);
	V_ApplyShake(pShake, pparams->vieworg, pparams->viewangles, 1.0);

	// Check for problems around water, move the viewer artificially if necessary 
	// -- this prevents drawing errors in GL due to waves
	waterOffset = 0;
	if ( pparams->waterlevel >= 2 )
	{
		int		i, contents, waterDist, waterEntity;
		vec3_t	point;
		waterDist = cl_waterdist->value;

		if ( pparams->hardware )
		{
			waterEntity = gEngfuncs.PM_WaterEntity( pparams->simorg );
			if ( waterEntity >= 0 && waterEntity < pparams->max_entities )
			{
				pwater = gEngfuncs.GetEntityByIndex( waterEntity );
				if ( pwater && ( pwater->model != NULL ) )
				{
					waterDist += ( pwater->curstate.scale * 16 );	// Add in wave height
				}
			}
		}
		else
		{
			waterEntity = 0;	// Don't need this in software
		}
		
		VectorCopy( pparams->vieworg, point );

		// Eyes are above water, make sure we're above the waves
		if ( pparams->waterlevel == 2 )	
		{
			point[2] -= waterDist;
			for ( i = 0; i < waterDist; i++ )
			{
				contents = gEngfuncs.PM_PointContents( point, NULL );
				if ( contents > CONTENTS_WATER )
					break;
				point[2] += 1;
			}
			waterOffset = (point[2] + waterDist) - pparams->vieworg[2];
		}
		else
		{
			// eyes are under water.  Make sure we're far enough under
			point[2] += waterDist;

			for ( i = 0; i < waterDist; i++ )
			{
				contents = gEngfuncs.PM_PointContents( point, NULL );
				if ( contents <= CONTENTS_WATER )
					break;
				point[2] -= 1;
			}
			waterOffset = (point[2] - waterDist) - pparams->vieworg[2];
		}
	}

	pparams->vieworg[2] += waterOffset;
		
	V_AddIdle ( pparams );

	// offsets
	VectorCopy( pparams->cl_viewangles, angles );

	AngleVectors ( angles, pparams->forward, pparams->right, pparams->up );

	// don't allow cheats in multiplayer
	if ( pparams->maxclients <= 1 )
	{
		for ( i=0 ; i<3 ; i++ )
		{
			pparams->vieworg[i] += scr_ofsx->value*pparams->forward[i] + scr_ofsy->value*pparams->right[i] + scr_ofsz->value*pparams->up[i];
		}
	}
	
	{
		static Vector l_ofs;

		vec3_t ofs;

		ofs[0] = ofs[1] = ofs[2] = 0.0;

		CL_CameraOffset( (float *)&ofs );

		VectorCopy( ofs, camAngles );
		camAngles[ ROLL ]	= 0;

		AngleVectors( camAngles, camForward, camRight, camUp );
		// Treating cam_ofs[2] as the distance
		if (0 != CL_IsThirdPerson())
		{
			for (i = 0; i < 3; i++)
			{
				//	l_ofs[i] = lerp(l_ofs[i], ofs[i], pparams->frametime * 17.0f);
				l_ofs[i] = ofs[i];
				pparams->vieworg[i] += -l_ofs[2] * camForward[i];
			}
		}
		else
		{
			for (i = 0; i < 3; i++)
			{
				//		l_ofs[i] = lerp(l_ofs[i], 0, pparams->frametime * 17.0f);
				//		pparams->vieworg[i] += -l_ofs[2] * camForward[i];
			}
		}
	}
	// Give gun our viewangles
	VectorCopy ( pparams->cl_viewangles, view->angles );
	
	// set up gun position
	V_CalcGunAngle ( pparams );

	// Use predicted origin as view origin.
	VectorCopy ( pparams->simorg, view->origin );      
	view->origin[2] += ( waterOffset );
	VectorAdd(view->origin, viewheight, view->origin);

	// Let the viewmodel shake at about 10% of the amplitude
	gEngfuncs.V_ApplyShake( view->origin, view->angles, 0.9 );
	V_ApplyShake(pShake, view->origin, view->angles, 0.9);

	// pushing the view origin down off of the same X/Z plane as the ent's origin will give the
	// gun a very nice 'shifting' effect when the player looks up/down. If there is a problem
	// with view model distortion, this may be a cause. (SJB). 
	view->origin -= Vector(pparams->up) * 1;

	V_CalcViewAngles(pparams, view);

	// smooth out stair step ups
	if (!pparams->smoothing && pparams->onground && (pparams->simorg[2] != oldz) && oldViewz == pparams->viewheight[2])
	{
		int dir = (pparams->simorg[2] > oldz) ? 1 : -1;

		float steptime = pparams->time - lasttime;
		if (steptime < 0)
			steptime = 0; //FIXME	I_Error ("steptime < 0");

		oldz += steptime * 150 * dir;

		const float stepSize = pparams->movevars->stepsize;

		if (dir > 0)
		{
			if (oldz > pparams->simorg[2])
				oldz = pparams->simorg[2];

			if (pparams->simorg[2] - oldz > stepSize)
				oldz = pparams->simorg[2] - stepSize;
		}
		else
		{
			if (oldz < pparams->simorg[2])
				oldz = pparams->simorg[2];

			if (pparams->simorg[2] - oldz < -stepSize)
				oldz = pparams->simorg[2] + stepSize;
		}

		pparams->vieworg[2] += oldz - pparams->simorg[2];
		view->origin[2] += oldz - pparams->simorg[2];
	}
	else
	{
		oldz = pparams->simorg[2];
		oldViewz = pparams->viewheight[2];
	}

	{
		static float lastorg[3];
		vec3_t delta;

		VectorSubtract( pparams->simorg, lastorg, delta );

		if ( Length( delta ) != 0.0 )
		{
			VectorCopy( pparams->simorg, ViewInterp.Origins[ ViewInterp.CurrentOrigin & ORIGIN_MASK ] );
			ViewInterp.OriginTime[ ViewInterp.CurrentOrigin & ORIGIN_MASK ] = pparams->time;
			ViewInterp.CurrentOrigin++;

			VectorCopy( pparams->simorg, lastorg );
		}
	}

	// Smooth out whole view in multiplayer when on trains, lifts
	if ( cl_vsmoothing && cl_vsmoothing->value &&
		( pparams->smoothing && ( pparams->maxclients > 1 ) ) )
	{
		int foundidx;
		int i;
		float t;

		if ( cl_vsmoothing->value < 0.0 )
		{
			gEngfuncs.Cvar_SetValue( "cl_vsmoothing", 0.0 );
		}

		t = pparams->time - cl_vsmoothing->value;

		for ( i = 1; i < ORIGIN_MASK; i++ )
		{
			foundidx = ViewInterp.CurrentOrigin - 1 - i;
			if ( ViewInterp.OriginTime[ foundidx & ORIGIN_MASK ] <= t )
				break;
		}

		if ( i < ORIGIN_MASK &&  ViewInterp.OriginTime[ foundidx & ORIGIN_MASK ] != 0.0 )
		{
			// Interpolate
			vec3_t delta;
			double frac;
			double dt;
			vec3_t neworg;

			dt = ViewInterp.OriginTime[ (foundidx + 1) & ORIGIN_MASK ] - ViewInterp.OriginTime[ foundidx & ORIGIN_MASK ];
			if ( dt > 0.0 )
			{
				frac = ( t - ViewInterp.OriginTime[ foundidx & ORIGIN_MASK] ) / dt;
				frac = min( 1.0, frac );
				VectorSubtract( ViewInterp.Origins[ ( foundidx + 1 ) & ORIGIN_MASK ], ViewInterp.Origins[ foundidx & ORIGIN_MASK ], delta );
				VectorMA( ViewInterp.Origins[ foundidx & ORIGIN_MASK ], frac, delta, neworg );

				// Dont interpolate large changes
				if ( Length( delta ) < 64 )
				{
					VectorSubtract( neworg, pparams->simorg, delta );

					VectorAdd( pparams->simorg, delta, pparams->simorg );
					VectorAdd( pparams->vieworg, delta, pparams->vieworg );
					VectorAdd( view->origin, delta, view->origin );

				}
			}
		}
	}

	// Store off v_angles before munging for third person
	v_angles = pparams->viewangles;
	v_lastAngles = pparams->viewangles;
//	v_cl_angles = pparams->cl_viewangles;	// keep old user mouse angles !
	if ( CL_IsThirdPerson() )
	{
		VectorCopy( camAngles, pparams->viewangles);
		float pitch = camAngles[ 0 ];

		// Normalize angles
		if ( pitch > 180 ) 
			pitch -= 360.0;
		else if ( pitch < -180 )
			pitch += 360;

		// Player pitch is inverted
		pitch /= -3.0;

		// Slam local player's pitch value
		ent->angles[ 0 ] = pitch;
		ent->curstate.angles[ 0 ] = pitch;
		ent->prevstate.angles[ 0 ] = pitch;
		ent->latched.prevangles[ 0 ] = pitch;
	}

	// override all previous settings if the viewent isn't the client
	if ( pparams->viewentity > pparams->maxclients )
	{
		cl_entity_t *viewentity;
		viewentity = gEngfuncs.GetEntityByIndex( pparams->viewentity );
		if ( viewentity )
		{
			VectorCopy( viewentity->origin, pparams->vieworg );
			VectorCopy( viewentity->angles, pparams->viewangles );

			// Store off overridden viewangles
			v_angles = pparams->viewangles;
		}
	}

	lasttime = pparams->time;
	lastAngles = pparams->viewangles;
	v_origin = pparams->vieworg;
}

void V_SmoothInterpolateAngles( float * startAngle, float * endAngle, float * finalAngle, float degreesPerSec )
{
	float absd,frac,d,threshhold;
	
	NormalizeAngles( startAngle );
	NormalizeAngles( endAngle );

	for ( int i = 0 ; i < 3 ; i++ )
	{
		d = endAngle[i] - startAngle[i];

		if ( d > 180.0f )
		{
			d -= 360.0f;
		}
		else if ( d < -180.0f )
		{	
			d += 360.0f;
		}

		absd = fabs(d);

		if ( absd > 0.01f )
		{
			frac = degreesPerSec * v_frametime;

			threshhold= degreesPerSec / 4;

			if ( absd < threshhold )
			{
				float h = absd / threshhold;
				h *= h;
				frac*= h;  // slow down last degrees
			}

			if ( frac >  absd )
			{
				finalAngle[i] = endAngle[i];
			}
			else
			{
				if ( d>0)
					finalAngle[i] = startAngle[i] + frac;
				else
					finalAngle[i] = startAngle[i] - frac;
			}
		}
		else
		{
			finalAngle[i] = endAngle[i];
		}

	}

	NormalizeAngles( finalAngle );
}

// Get the origin of the Observer based around the target's position and angles
void V_GetChaseOrigin( float * angles, float * origin, float distance, float * returnvec )
{
	vec3_t	vecEnd;
	vec3_t	forward;
	vec3_t	vecStart;
	pmtrace_t* trace = nullptr;
	int maxLoops = 8;

	int ignoreent = -1;	// first, ignore no entity
	
	cl_entity_t	 *	ent = NULL;
	
	// Trace back from the target using the player's view angles
	AngleVectors(angles, forward, NULL, NULL);
	
	VectorScale(forward,-1,forward);

	VectorCopy( origin, vecStart );

	VectorMA(vecStart, distance , forward, vecEnd);

	while ( maxLoops > 0)
	{
		trace = gEngfuncs.PM_TraceLine( vecStart, vecEnd, PM_TRACELINE_PHYSENTSONLY, 2, ignoreent );

		// WARNING! trace->ent is is the number in physent list not the normal entity number

		if ( trace->ent <= 0)
			break;	// we hit the world or nothing, stop trace

		ent = gEngfuncs.GetEntityByIndex( PM_GetPhysEntInfo( trace->ent ) );

		if ( ent == NULL )
			break;

		// hit non-player solid BSP , stop here
		if ( ent->curstate.solid == SOLID_BSP && !ent->player ) 
			break;

		// if close enought to end pos, stop, otherwise continue trace
		if( Distance(trace->endpos, vecEnd ) < 1.0f )
		{
			break;
		}
		else
		{
			ignoreent = trace->ent;	// ignore last hit entity
			VectorCopy( trace->endpos, vecStart);
		}

		maxLoops--;
	}  

/*	if ( ent )
	{
		gEngfuncs.Con_Printf("Trace loops %i , entity %i, model %s, solid %i\n",(8-maxLoops),ent->curstate.number, ent->model->name , ent->curstate.solid ); 
	} */

	VectorMA( trace->endpos, 4, trace->plane.normal, returnvec );

	v_lastDistance = Distance(trace->endpos, origin);	// real distance without offset
}

/*void V_GetDeathCam(cl_entity_t * ent1, cl_entity_t * ent2, float * angle, float * origin)
{
	float newAngle[3]; float newOrigin[3]; 

	float distance = 168.0f;

	v_lastDistance+= v_frametime * 96.0f;	// move unit per seconds back

	if ( v_resetCamera )
		v_lastDistance = 64.0f;

	if ( distance > v_lastDistance )
		distance = v_lastDistance;

	VectorCopy(ent1->origin, newOrigin);

	if ( ent1->player )
		newOrigin[2]+= 17; // head level of living player

	// get new angle towards second target
	if ( ent2 )
	{
		VectorSubtract( ent2->origin, ent1->origin, newAngle );
		VectorAngles( newAngle, newAngle );
		newAngle[0] = -newAngle[0];
	}
	else
	{
		// if no second target is given, look down to dead player
		newAngle[0] = 90.0f;
		newAngle[1] = 0.0f;
		newAngle[2] = 0;
	}

	// and smooth view
	V_SmoothInterpolateAngles( v_lastAngles, newAngle, angle, 120.0f );
			
	V_GetChaseOrigin( angle, newOrigin, distance, origin );

	VectorCopy(angle, v_lastAngles);
}*/

void V_GetSingleTargetCam(cl_entity_t * ent1, float * angle, float * origin)
{
	float newAngle[3]; float newOrigin[3]; 
	
	int flags 	   = gHUD.m_Spectator.m_iObserverFlags;

	// see is target is a dead player
	qboolean deadPlayer = ent1->player && (ent1->curstate.solid == SOLID_NOT);
	
	float dfactor   = ( flags & DRC_FLAG_DRAMATIC )? -1.0f : 1.0f;

	float distance = 112.0f + ( 16.0f * dfactor ); // get close if dramatic;
	
	// go away in final scenes or if player just died
	if ( flags & DRC_FLAG_FINAL )
		distance*=2.0f;	
	else if ( deadPlayer )
		distance*=1.5f;	

	// let v_lastDistance float smoothly away
	v_lastDistance+= v_frametime * 32.0f;	// move unit per seconds back

	if ( distance > v_lastDistance )
		distance = v_lastDistance;
	
	VectorCopy(ent1->origin, newOrigin);

	if ( ent1->player )
	{
		if ( deadPlayer )  
			newOrigin[2]+= 2;	//laying on ground
		else
			newOrigin[2]+= 17; // head level of living player
			
	}
	else
		newOrigin[2]+= 8;	// object, tricky, must be above bomb in CS

	// we have no second target, choose view direction based on
	// show front of primary target
	VectorCopy(ent1->angles, newAngle);

	// show dead players from front, normal players back
	if ( flags & DRC_FLAG_FACEPLAYER )
		newAngle[1]+= 180.0f;


	newAngle[0]+= 12.5f * dfactor; // lower angle if dramatic

	// if final scene (bomb), show from real high pos
	if ( flags & DRC_FLAG_FINAL )
		newAngle[0] = 22.5f; 

	// choose side of object/player			
	if ( flags & DRC_FLAG_SIDE )
		newAngle[1]+=22.5f;
	else
		newAngle[1]-=22.5f;

	V_SmoothInterpolateAngles( v_lastAngles, newAngle, angle, 120.0f );

	// HACK, if player is dead don't clip against his dead body, can't check this
	V_GetChaseOrigin( angle, newOrigin, distance, origin );
}

float MaxAngleBetweenAngles(  float * a1, float * a2 )
{
	float d, maxd = 0.0f;

	NormalizeAngles( a1 );
	NormalizeAngles( a2 );

	for ( int i = 0 ; i < 3 ; i++ )
	{
		d = a2[i] - a1[i];
		if ( d > 180 )
		{
			d -= 360;
		}
		else if ( d < -180 )
		{	
			d += 360;
		}

		d = fabs(d);

		if ( d > maxd )
			maxd=d;
	}

	return maxd;
}

void V_GetDoubleTargetsCam(cl_entity_t	 * ent1, cl_entity_t * ent2,float * angle, float * origin)
{
	float newAngle[3]; float newOrigin[3]; float tempVec[3];

	int flags 	   = gHUD.m_Spectator.m_iObserverFlags;

	float dfactor   = ( flags & DRC_FLAG_DRAMATIC )? -1.0f : 1.0f;

	float distance = 112.0f + ( 16.0f * dfactor ); // get close if dramatic;
	
	// go away in final scenes or if player just died
	if ( flags & DRC_FLAG_FINAL )
		distance*=2.0f;	
	
	// let v_lastDistance float smoothly away
	v_lastDistance+= v_frametime * 32.0f;	// move unit per seconds back

	if ( distance > v_lastDistance )
		distance = v_lastDistance;

	VectorCopy(ent1->origin, newOrigin);

	if ( ent1->player )
		newOrigin[2]+= 17; // head level of living player
	else
		newOrigin[2]+= 8;	// object, tricky, must be above bomb in CS

	// get new angle towards second target
	VectorSubtract( ent2->origin, ent1->origin, newAngle );

	VectorAngles( newAngle, newAngle );
	newAngle[0] = -newAngle[0];

	// set angle diffrent in Dramtaic scenes
	newAngle[0]+= 12.5f * dfactor; // lower angle if dramatic
			
	if ( flags & DRC_FLAG_SIDE )
		newAngle[1]+=22.5f;
	else
		newAngle[1]-=22.5f;

	float d = MaxAngleBetweenAngles( v_lastAngles, newAngle );

	if ( ( d < v_cameraFocusAngle) && ( v_cameraMode == CAM_MODE_RELAX ) )
	{
		// difference is to small and we are in relax camera mode, keep viewangles
		VectorCopy(v_lastAngles, newAngle );
	}
	else if ( (d < v_cameraRelaxAngle) && (v_cameraMode == CAM_MODE_FOCUS) )
	{
		// we catched up with our target, relax again
		v_cameraMode = CAM_MODE_RELAX;
	}
	else
	{
		// target move too far away, focus camera again
		v_cameraMode = CAM_MODE_FOCUS;
	}

	// and smooth view, if not a scene cut
	if ( v_resetCamera || (v_cameraMode == CAM_MODE_RELAX) )
	{
		VectorCopy( newAngle, angle );
	}
	else
	{
		V_SmoothInterpolateAngles( v_lastAngles, newAngle, angle, 180.0f );
	}

	V_GetChaseOrigin( newAngle, newOrigin, distance, origin );

	// move position up, if very close at target
	if ( v_lastDistance < 64.0f )
		origin[2]+= 16.0f*( 1.0f - (v_lastDistance / 64.0f ) );

	// calculate angle to second target
	VectorSubtract( ent2->origin, origin, tempVec );
	VectorAngles( tempVec, tempVec );
	tempVec[0] = -tempVec[0];

	/* take middle between two viewangles
	InterpolateAngles( newAngle, tempVec, newAngle, 0.5f); */
	
	

}

void V_GetDirectedChasePosition(cl_entity_t	 * ent1, cl_entity_t * ent2,float * angle, float * origin)
{

	if ( v_resetCamera )
	{
		v_lastDistance = 4096.0f;
		// v_cameraMode = CAM_MODE_FOCUS;
	}

	if ( ( ent2 == (cl_entity_t*)0xFFFFFFFF ) || ( ent1->player && (ent1->curstate.solid == SOLID_NOT) ) )
	{
		// we have no second target or player just died
		V_GetSingleTargetCam(ent1, angle, origin);
	}
	else if ( ent2 )
	{
		// keep both target in view
		V_GetDoubleTargetsCam( ent1, ent2, angle, origin );
	}
	else
	{
		// second target disappeard somehow (dead)

		// keep last good viewangle
		float newOrigin[3];

		int flags 	   = gHUD.m_Spectator.m_iObserverFlags;

		float dfactor   = ( flags & DRC_FLAG_DRAMATIC )? -1.0f : 1.0f;

		float distance = 112.0f + ( 16.0f * dfactor ); // get close if dramatic;
	
		// go away in final scenes or if player just died
		if ( flags & DRC_FLAG_FINAL )
			distance*=2.0f;	
	
		// let v_lastDistance float smoothly away
		v_lastDistance+= v_frametime * 32.0f;	// move unit per seconds back

		if ( distance > v_lastDistance )
			distance = v_lastDistance;
		
		VectorCopy(ent1->origin, newOrigin);

		if ( ent1->player )
			newOrigin[2]+= 17; // head level of living player
		else
			newOrigin[2]+= 8;	// object, tricky, must be above bomb in CS

		V_GetChaseOrigin( angle, newOrigin, distance, origin );
	}

	VectorCopy(angle, v_lastAngles);
}

void V_GetChasePos(int target, float * cl_angles, float * origin, float * angles)
{
	cl_entity_t	 *	ent = NULL;
	
	if ( target ) 
	{
		ent = gEngfuncs.GetEntityByIndex( target );
	};
	
	if (!ent)
	{
		// just copy a save in-map position
		VectorCopy ( vJumpAngles, angles );
		VectorCopy ( vJumpOrigin, origin );
		return;
	}
	
	
	
	if ( gHUD.m_Spectator.m_autoDirector->value )
	{
		if ( g_iUser3 )
			V_GetDirectedChasePosition( ent, gEngfuncs.GetEntityByIndex( g_iUser3 ),
				angles, origin );
		else
			V_GetDirectedChasePosition( ent, ( cl_entity_t*)0xFFFFFFFF,
				angles, origin );
	}
	else
	{
		if ( cl_angles == NULL )	// no mouse angles given, use entity angles ( locked mode )
		{
			VectorCopy ( ent->angles, angles);
			angles[0]*=-1;
		}
		else
			VectorCopy ( cl_angles, angles);


		VectorCopy ( ent->origin, origin);
		
		origin[2]+= 28; // DEFAULT_VIEWHEIGHT - some offset

		V_GetChaseOrigin( angles, origin, cl_chasedist->value, origin );
	}

	v_resetCamera = false;	
}

void V_ResetChaseCam()
{
	v_resetCamera = true;
}


void V_GetInEyePos(int target, float * origin, float * angles )
{
	if ( !target)
	{
		// just copy a save in-map position
		VectorCopy ( vJumpAngles, angles );
		VectorCopy ( vJumpOrigin, origin );
		return;
	};


	cl_entity_t	 * ent = gEngfuncs.GetEntityByIndex( target );

	if ( !ent )
		return;

	VectorCopy ( ent->origin, origin );
	VectorCopy ( ent->angles, angles );

	angles[PITCH]*=-3.0f;	// see CL_ProcessEntityUpdate()

	if ( ent->curstate.solid == SOLID_NOT )
	{
		angles[ROLL] = 80;	// dead view angle
		origin[2]+= -8 ; // PM_DEAD_VIEWHEIGHT
	}
	else if (ent->curstate.usehull == 1 )
		origin[2]+= 12; // VEC_DUCK_VIEW;
	else
		// exacty eye position can't be caluculated since it depends on
		// client values like cl_bobcycle, this offset matches the default values
		origin[2]+= 28; // DEFAULT_VIEWHEIGHT
}

void V_GetMapFreePosition( float * cl_angles, float * origin, float * angles )
{
	vec3_t forward;
	vec3_t zScaledTarget;

	VectorCopy(cl_angles, angles);

	// modify angles since we don't wanna see map's bottom
	angles[0] = 51.25f + 38.75f*(angles[0]/90.0f);

	zScaledTarget[0] = gHUD.m_Spectator.m_mapOrigin[0];
	zScaledTarget[1] = gHUD.m_Spectator.m_mapOrigin[1];
	zScaledTarget[2] = gHUD.m_Spectator.m_mapOrigin[2] * (( 90.0f - angles[0] ) / 90.0f );
	

	AngleVectors(angles, forward, NULL, NULL);

	VectorNormalize(forward);

	VectorMA(zScaledTarget, -( 4096.0f / gHUD.m_Spectator.m_mapZoom ), forward , origin);
}

void V_GetMapChasePosition(int target, float * cl_angles, float * origin, float * angles)
{
	vec3_t forward;

	if ( target )
	{
		cl_entity_t	 *	ent = gEngfuncs.GetEntityByIndex( target );

		if ( gHUD.m_Spectator.m_autoDirector->value )
		{
			// this is done to get the angles made by director mode
			V_GetChasePos(target, cl_angles, origin, angles);
			VectorCopy(ent->origin, origin);
			
			// keep fix chase angle horizontal
			angles[0] = 45.0f;
		}
		else
		{
			VectorCopy(cl_angles, angles);
			VectorCopy(ent->origin, origin);

			// modify angles since we don't wanna see map's bottom
			angles[0] = 51.25f + 38.75f*(angles[0]/90.0f);
		}
	}
	else
	{
		// keep out roaming position, but modify angles
		VectorCopy(cl_angles, angles);
		angles[0] = 51.25f + 38.75f*(angles[0]/90.0f);
	}

	origin[2] *= (( 90.0f - angles[0] ) / 90.0f );
	angles[2] = 0.0f;	// don't roll angle (if chased player is dead)

	AngleVectors(angles, forward, NULL, NULL);

	VectorNormalize(forward);

	VectorMA(origin, -1536, forward, origin); 
}

int V_FindViewModelByWeaponModel(int weaponindex)
{

	static char * modelmap[][2] =	{
		{ "models/p_crossbow.mdl",		"models/v_crossbow.mdl"		},
		{ "models/p_crowbar.mdl",		"models/v_crowbar.mdl"		},
		{ "models/p_egon.mdl",			"models/v_egon.mdl"			},
		{ "models/p_gauss.mdl",			"models/v_gauss.mdl"		},
		{ "models/p_9mmhandgun.mdl",	"models/v_9mmhandgun.mdl"	},
		{ "models/p_grenade.mdl",		"models/v_grenade.mdl"		},
		{ "models/p_hgun.mdl",			"models/v_hgun.mdl"			},
		{ "models/p_9mmAR.mdl",			"models/v_9mmAR.mdl"		},
		{ "models/p_357.mdl",			"models/v_357.mdl"			},
		{ "models/p_rpg.mdl",			"models/v_rpg.mdl"			},
		{ "models/p_shotgun.mdl",		"models/v_shotgun.mdl"		},
		{ "models/p_squeak.mdl",		"models/v_squeak.mdl"		},
		{ "models/p_tripmine.mdl",		"models/v_tripmine.mdl"		},
		{ "models/p_satchel_radio.mdl",	"models/v_satchel_radio.mdl"},
		{ "models/p_satchel.mdl",		"models/v_satchel.mdl"		},
		{ NULL, NULL } };

	struct model_s * weaponModel = IEngineStudio.GetModelByIndex( weaponindex );

	if ( weaponModel )
	{
		int len = strlen( weaponModel->name );
		int i = 0;

		while ( modelmap[i] != NULL )
		{
			if ( !strnicmp( weaponModel->name, modelmap[i][0], len ) )
			{
				return gEngfuncs.pEventAPI->EV_FindModelIndex( modelmap[i][1] );
			}
			i++;
		}

		return 0;
	}
	else
		return 0;

}


/*
==================
V_CalcSpectatorRefdef

==================
*/
void V_CalcSpectatorRefdef ( struct ref_params_s * pparams )
{
	static vec3_t			velocity ( 0.0f, 0.0f, 0.0f);

	static int lastWeaponModelIndex = 0;
	static int lastViewModelIndex = 0;
		
	cl_entity_t	 * ent = gEngfuncs.GetEntityByIndex( g_iUser2 );
	
	pparams->onlyClientDraw = false;

	// refresh position
	VectorCopy ( pparams->simorg, v_sim_org );

	// get old values
	VectorCopy ( pparams->cl_viewangles, v_cl_angles );
	VectorCopy ( pparams->viewangles, v_angles );
	VectorCopy ( pparams->vieworg, v_origin );

	if (  ( g_iUser1 == OBS_IN_EYE || gHUD.m_Spectator.m_pip->value == INSET_IN_EYE ) && ent )
	{
		// calculate player velocity
		float timeDiff = ent->curstate.msg_time - ent->prevstate.msg_time;

		if ( timeDiff > 0 )
		{
			vec3_t distance;
			VectorSubtract(ent->prevstate.origin, ent->curstate.origin, distance);
			VectorScale(distance, 1/timeDiff, distance );

			velocity[0] = velocity[0]*0.9f + distance[0]*0.1f;
			velocity[1] = velocity[1]*0.9f + distance[1]*0.1f;
			velocity[2] = velocity[2]*0.9f + distance[2]*0.1f;
			
			VectorCopy(velocity, pparams->simvel);
		}

		// predict missing client data and set weapon model ( in HLTV mode or inset in eye mode )
		if ( gEngfuncs.IsSpectateOnly() )
		{
			V_GetInEyePos( g_iUser2, pparams->simorg, pparams->cl_viewangles );

			pparams->health = 1;

			cl_entity_t	 * gunModel = gEngfuncs.GetViewModel();

			if ( lastWeaponModelIndex != ent->curstate.weaponmodel )
			{
				// weapon model changed

				lastWeaponModelIndex = ent->curstate.weaponmodel;
				lastViewModelIndex = V_FindViewModelByWeaponModel( lastWeaponModelIndex );
				if ( lastViewModelIndex )
				{
					gEngfuncs.pfnWeaponAnim(0,0);	// reset weapon animation
				}
				else
				{
					// model not found
					gunModel->model = NULL;	// disable weapon model
					lastWeaponModelIndex = lastViewModelIndex = 0;
				}
			}

			if ( lastViewModelIndex )
			{
				gunModel->model = IEngineStudio.GetModelByIndex( lastViewModelIndex );
				gunModel->curstate.modelindex = lastViewModelIndex;
				gunModel->curstate.frame = 0;
				gunModel->curstate.colormap = 0; 
				gunModel->index = g_iUser2;
			}
			else
			{
				gunModel->model = NULL;	// disable weaopn model
			}
		}
		else
		{
			// only get viewangles from entity
			VectorCopy ( ent->angles, pparams->cl_viewangles );
			pparams->cl_viewangles[PITCH]*=-3.0f;	// see CL_ProcessEntityUpdate()
		}
	}

	v_frametime = pparams->frametime;

	if ( pparams->nextView == 0 )
	{
		// first renderer cycle, full screen

		switch ( g_iUser1 )
		{
			case OBS_CHASE_LOCKED:	V_GetChasePos( g_iUser2, NULL, v_origin, v_angles );
									break;

			case OBS_CHASE_FREE:	V_GetChasePos( g_iUser2, v_cl_angles, v_origin, v_angles );
									break;

			case OBS_ROAMING	:	VectorCopy (v_cl_angles, v_angles);
									VectorCopy (v_sim_org, v_origin);
									break;

			case OBS_IN_EYE		:   V_CalcNormalRefdef ( pparams );
									break;
				
			case OBS_MAP_FREE  :	pparams->onlyClientDraw = true;
									V_GetMapFreePosition( v_cl_angles, v_origin, v_angles );
									break;

			case OBS_MAP_CHASE  :	pparams->onlyClientDraw = true;
									V_GetMapChasePosition( g_iUser2, v_cl_angles, v_origin, v_angles );
									break;
		}

		if ( gHUD.m_Spectator.m_pip->value )
			pparams->nextView = 1;	// force a second renderer view

		gHUD.m_Spectator.m_iDrawCycle = 0;

	}
	else
	{
		// second renderer cycle, inset window

		// set inset parameters
		pparams->viewport[0] = XRES(gHUD.m_Spectator.m_OverviewData.insetWindowX);	// change viewport to inset window
		pparams->viewport[1] = YRES(gHUD.m_Spectator.m_OverviewData.insetWindowY);
		pparams->viewport[2] = XRES(gHUD.m_Spectator.m_OverviewData.insetWindowWidth);
		pparams->viewport[3] = YRES(gHUD.m_Spectator.m_OverviewData.insetWindowHeight);
		pparams->nextView	 = 0;	// on further view

		// override some settings in certain modes
		switch ( (int)gHUD.m_Spectator.m_pip->value )
		{
			case INSET_CHASE_FREE : V_GetChasePos( g_iUser2, v_cl_angles, v_origin, v_angles );
									break;	

			case INSET_IN_EYE	 :	V_CalcNormalRefdef ( pparams );
									break;

			case INSET_MAP_FREE  :	pparams->onlyClientDraw = true;
									V_GetMapFreePosition( v_cl_angles, v_origin, v_angles );
									break;

			case INSET_MAP_CHASE  :	pparams->onlyClientDraw = true;

									if ( g_iUser1 == OBS_ROAMING )
										V_GetMapChasePosition( 0, v_cl_angles, v_origin, v_angles );
									else
										V_GetMapChasePosition( g_iUser2, v_cl_angles, v_origin, v_angles );

									break;
		}

		gHUD.m_Spectator.m_iDrawCycle = 1;
	}

	// write back new values into pparams
	VectorCopy ( v_cl_angles, pparams->cl_viewangles );
	VectorCopy ( v_angles, pparams->viewangles );
	VectorCopy ( v_origin, pparams->vieworg );

}

ref_params_s g_params;
extern void UpdateFlashlight(ref_params_t* pparams);
void DLLEXPORT V_CalcRefdef( struct ref_params_s *pparams )
{
	// intermission / finale rendering
	if ( pparams->intermission )
	{	
		V_CalcIntermissionRefdef ( pparams );	
	}
	else if ( pparams->spectator || g_iUser1 )
	{
		V_CalcSpectatorRefdef ( pparams );	
	}
	else if ( !pparams->paused )
	{
		V_CalcNormalRefdef ( pparams );
	}
	if (pparams->paused == 0)
	{
		gHUD.m_flAbsTime += pparams->frametime;
	}

	g_refparams = *pparams;

	memcpy(&gHUD.r_params, pparams, sizeof(ref_params_s));
	memcpy(&g_pparams, pparams, sizeof(ref_params_s));
	memcpy(&g_params, pparams, sizeof(ref_params_s));

	gELightList.CalcRefDef();
	SVD_CalcRefDef(pparams);
	gFog.CalcRefDef(pparams);
	UpdateFlashlight(pparams);
}

/*
=============
V_PunchAxis

Client side punch effect
=============
*/
void V_PunchAxis( int axis, float punch )
{
	ev_punch[axis] = punch * 20;
}

/*
=============
V_Init
=============
*/
void V_Init (void)
{
	gEngfuncs.pfnAddCommand ("centerview", V_StartPitchDrift );

	scr_ofsx			= gEngfuncs.pfnRegisterVariable( "scr_ofsx","0", 0 );
	scr_ofsy			= gEngfuncs.pfnRegisterVariable( "scr_ofsy","0", 0 );
	scr_ofsz			= gEngfuncs.pfnRegisterVariable( "scr_ofsz","0", 0 );

	v_centermove		= gEngfuncs.pfnRegisterVariable( "v_centermove", "0.15", 0 );
	v_centerspeed		= gEngfuncs.pfnRegisterVariable( "v_centerspeed","500", 0 );

	cl_bobcycle			= gEngfuncs.pfnRegisterVariable( "cl_bobcycle","0.8", 0 );// best default for my experimental gun wag (sjb)
	cl_bob				= gEngfuncs.pfnRegisterVariable( "cl_bob","0.01", 0 );// best default for my experimental gun wag (sjb)
	cl_bobup			= gEngfuncs.pfnRegisterVariable( "cl_bobup","0.5", 0 );
	cl_waterdist		= gEngfuncs.pfnRegisterVariable( "cl_waterdist","4", 0 );
	cl_chasedist		= gEngfuncs.pfnRegisterVariable( "cl_chasedist","112", 0 );
}


//#define TRACE_TEST
#if defined( TRACE_TEST )

extern float in_fov;
/*
====================
CalcFov
====================
*/
float CalcFov (float fov_x, float width, float height)
{
	float	a;
	float	x;

	if (fov_x < 1 || fov_x > 179)
		fov_x = 90;	// error, set to 90

	x = width/tan(fov_x/360*M_PI);

	a = atan (height/x);

	a = a*360/M_PI;

	return a;
}

int hitent = -1;

void V_Move( int mx, int my )
{
	float fov;
	float fx, fy;
	float dx, dy;
	float c_x, c_y;
	float dX, dY;
	vec3_t forward, up, right;
	vec3_t newangles;

	vec3_t farpoint;
	pmtrace_t tr;

	fov = CalcFov( in_fov, (float)ScreenWidth, (float)ScreenHeight );

	c_x = (float)ScreenWidth / 2.0;
	c_y = (float)ScreenHeight / 2.0;

	dx = (float)mx - c_x;
	dy = (float)my - c_y;

	// Proportion we moved in each direction
	fx = dx / c_x;
	fy = dy / c_y;

	dX = fx * in_fov / 2.0 ;
	dY = fy * fov / 2.0;

	newangles = v_angles;

	newangles[ YAW ] -= dX;
	newangles[ PITCH ] += dY;

	// Now rotate v_forward around that point
	AngleVectors ( newangles, forward, right, up );

	farpoint = v_origin + 8192 * forward;

	// Trace
	tr = *(gEngfuncs.PM_TraceLine( (float *)&v_origin, (float *)&farpoint, PM_TRACELINE_PHYSENTSONLY, 2 /*point sized hull*/, -1 ));

	if ( tr.fraction != 1.0 && tr.ent != 0 )
	{
		hitent = PM_GetPhysEntInfo( tr.ent );
		PM_ParticleLine( (float *)&v_origin, (float *)&tr.endpos, 5, 1.0, 0.0 );
	}
	else
	{
		hitent = -1;
	}
}

#endif

void UTIL_SmoothInterpolateAngles(float* startAngle, float* endAngle, float* finalAngle, float degreesPerSec)
{
	float absd, frac, d;

	NormalizeAngles(startAngle);
	NormalizeAngles(endAngle);

	for (int i = 0; i < 3; i++)
	{
		d = endAngle[i] - startAngle[i];

		if (d > 180.0f)
		{
			d -= 360.0f;
		}
		else if (d < -180.0f)
		{
			d += 360.0f;
		}

		absd = fabs(d);

		if (absd > 0.01f)
		{
			frac = degreesPerSec * v_frametime;

			if (frac > absd)
			{
				finalAngle[i] = endAngle[i];
			}
			else
			{
				if (d > 0)
					finalAngle[i] = startAngle[i] + frac;
				else
					finalAngle[i] = startAngle[i] - frac;
			}
		}
		else
		{
			finalAngle[i] = endAngle[i];
		}
	}

	NormalizeAngles(finalAngle);
}

/*
=================
SignbitsForPlane

=================
*/
int SignbitsForPlane ( const mplane_t *out )
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}

/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/
int BoxOnPlaneSide ( const vec3_t& emins, const vec3_t& emaxs, const mplane_t *p )
{
	float	dist1, dist2;
	int		sides;
	
// general case
	switch (p->signbits)
	{
	case 0:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		break;
	case 1:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		break;
	case 2:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		break;
	case 3:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		break;
	case 4:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		break;
	case 5:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		break;
	case 6:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		break;
	case 7:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		break;
	default:
		dist1 = dist2 = 0;		// shut up compiler
		break;
	}

	sides = 0;
	if (dist1 >= p->dist)
		sides = 1;
	if (dist2 < p->dist)
		sides |= 2;

	return sides;
}

/*
================
R_ConcatRotations

================
*/
void R_ConcatRotations (float in1[3][3], float in2[3][3], float out[3][3])
{
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
				in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
				in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
				in1[0][2] * in2[2][2];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
				in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
				in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
				in1[1][2] * in2[2][2];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
				in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
				in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
				in1[2][2] * in2[2][2];
}

/*
=================
ProjectPointOnPlane

=================
*/
void ProjectPointOnPlane( vec3_t& dst, const vec3_t& p, const vec3_t& normal )
{
	float d;
	vec3_t n;
	float inv_denom;

	inv_denom = 1.0F / DotProduct( normal, normal );

	d = DotProduct( normal, p ) * inv_denom;

	n[0] = normal[0] * inv_denom;
	n[1] = normal[1] * inv_denom;
	n[2] = normal[2] * inv_denom;

	dst[0] = p[0] - d * n[0];
	dst[1] = p[1] - d * n[1];
	dst[2] = p[2] - d * n[2];
}

/*
=================
PerpendicularVector

=================
*/
void PerpendicularVector( vec3_t& dst, const vec3_t& src )
{
	int	pos;
	int i;
	float minelem = 1.0F;
	vec3_t tempvec;

	/*
	** find the smallest magnitude axially aligned vector
	*/
	for ( pos = 0, i = 0; i < 3; i++ )
	{
		if ( fabs( src[i] ) < minelem )
		{
			pos = i;
			minelem = fabs( src[i] );
		}
	}
	tempvec[0] = tempvec[1] = tempvec[2] = 0.0F;
	tempvec[pos] = 1.0F;

	/*
	** project the point onto the plane defined by src
	*/
	ProjectPointOnPlane( dst, tempvec, src );

	/*
	** normalize the result
	*/
	VectorNormalize( dst );
}

/*
=================
RotatePointAroundVector

=================
*/
void RotatePointAroundVector( vec3_t& dst, const vec3_t& dir, const vec3_t& point, float degrees )
{
	float	m[3][3];
	float	im[3][3];
	float	zrot[3][3];
	float	tmpmat[3][3];
	float	rot[3][3];
	int	i;
	vec3_t vr, vup, vf;

	vf[0] = dir[0];
	vf[1] = dir[1];
	vf[2] = dir[2];

	PerpendicularVector( vr, dir );
	vup = CrossProduct( vr, vf );

	m[0][0] = vr[0];
	m[1][0] = vr[1];
	m[2][0] = vr[2];

	m[0][1] = vup[0];
	m[1][1] = vup[1];
	m[2][1] = vup[2];

	m[0][2] = vf[0];
	m[1][2] = vf[1];
	m[2][2] = vf[2];

	memcpy( im, m, sizeof( im ) );

	im[0][1] = m[1][0];
	im[0][2] = m[2][0];
	im[1][0] = m[0][1];
	im[1][2] = m[2][1];
	im[2][0] = m[0][2];
	im[2][1] = m[1][2];

	memset( zrot, 0, sizeof( zrot ) );
	zrot[0][0] = zrot[1][1] = zrot[2][2] = 1.0F;

	zrot[0][0] = cos( DEG2RAD( degrees ) );
	zrot[0][1] = sin( DEG2RAD( degrees ) );
	zrot[1][0] = -sin( DEG2RAD( degrees ) );
	zrot[1][1] = cos( DEG2RAD( degrees ) );

	R_ConcatRotations( m, zrot, tmpmat );
	R_ConcatRotations( tmpmat, im, rot );

	for ( i = 0; i < 3; i++ )
	{
		dst[i] = rot[i][0] * point[0] + rot[i][1] * point[1] + rot[i][2] * point[2];
	}
}

/*
=================
R_SetFrustum

=================
*/
void R_SetFrustum (const vec3_t& vOrigin, const vec3_t& vAngles, float flFOV, float flFarDist)
{
	int		i;
	vec3_t vpn, vright, vup;
	AngleVectors(vAngles, vpn, vright, vup);

	if (flFOV == 90)
	{
		// front side is visible
		VectorAdd (vpn, vright, frustum[0].normal);
		VectorSubtract (vpn, vright, frustum[1].normal);

		VectorAdd (vpn, vup, frustum[2].normal);
		VectorSubtract (vpn, vup, frustum[3].normal);
	}
	else
	{
		// rotate VPN right by FOV_X/2 degrees
		RotatePointAroundVector( frustum[0].normal, vup, vpn, -(90- flFOV / 2 ) );
		// rotate VPN left by FOV_X/2 degrees
		RotatePointAroundVector( frustum[1].normal, vup, vpn, 90- flFOV / 2 );
		// rotate VPN up by FOV_X/2 degrees
		RotatePointAroundVector( frustum[2].normal, vright, vpn, 90- flFOV / 2 );
		// rotate VPN down by FOV_X/2 degrees
		RotatePointAroundVector( frustum[3].normal, vright, vpn, -( 90 - flFOV / 2 ) );
	}

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (vOrigin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox ( const vec3_t& mins, const vec3_t& maxs )
{
	int		i;

	for (i=0 ; i<4 ; i++)
	{
		if (BoxOnPlaneSide (mins, maxs, &frustum[i]) == 2)
			return true;
	}

	return false;
}