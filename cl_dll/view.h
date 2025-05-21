//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#if !defined ( VIEWH )
#define VIEWH 
#pragma once

#include "com_model.h"

void V_StartPitchDrift( void );
void V_StopPitchDrift( void );

// ??
int SignbitsForPlane(const mplane_t* out);
int BoxOnPlaneSide(const vec3_t& emins, const vec3_t& emaxs, const mplane_t* p);
void R_ConcatRotations(float in1[3][3], float in2[3][3], float out[3][3]);
void ProjectPointOnPlane(vec3_t& dst, const vec3_t& p, const vec3_t& normal);
void PerpendicularVector(vec3_t& dst, const vec3_t& src);
void RotatePointAroundVector(vec3_t& dst, const vec3_t& dir, const vec3_t& point, float degrees);
void R_SetFrustum(const vec3_t& vOrigin, const vec3_t& vAngles, float flFOV, float flFarDist);
qboolean R_CullBox(const vec3_t& mins, const vec3_t& maxs);


#endif // !VIEWH