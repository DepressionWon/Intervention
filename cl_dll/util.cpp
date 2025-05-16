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
// util.cpp
//
// implementation of class-less helper functions
//

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>

#include "hud.h"
#include "cl_util.h"
#include <string.h>
#include "com_model.h"

#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

vec3_t vec3_origin( 0, 0, 0 );

SpriteHandle_t LoadSprite(const char *pszName)
{
	int i;
	char sz[256]; 

	if (ScreenWidth < 640)
		i = 320;
	else
		i = 640;

	sprintf(sz, pszName, i);

	return SPR_Load(sz);
}

//===========================================
// Con_Printf
//
//===========================================
void Con_Printf( char *fmt, ... )
{
	va_list	vArgPtr;
	char cMsg[MAX_PATH];
	
	va_start(vArgPtr,fmt);
	vsprintf(cMsg, fmt, vArgPtr);
	va_end(vArgPtr);

	gEngfuncs.Con_Printf(cMsg);
}

/*
====================
GetSpriteFrame

====================
*/
mspriteframe_t *GetSpriteFrame ( model_t *mod, int frame )
{
	msprite_t          *psprite;
	mspritegroup_t     *pspritegroup;
	mspriteframe_t     *pspriteframe;
	int                    i, numframes;
	float               *pintervals, fullinterval, targettime;

	psprite = (msprite_t*)mod->cache.data;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		gEngfuncs.Con_Printf ("R_GetSpriteFrame: no such frame %d\n", frame);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else
	{
		float realtime = gEngfuncs.GetClientTime();

		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by 0
		targettime = realtime - ((int)(realtime / fullinterval)) * fullinterval;

		for (i=0 ; i<(numframes-1) ; i++)
		{
			if (pintervals[i] > targettime)
				break;
		}

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}

//===========================================
// Con_Printf
//
//===========================================
char* COM_ReadLine( char* pstr, char* pstrOut )
{
	int length = 0;
	while(*pstr && *pstr != '\n' && *pstr != '\r')
	{
		pstrOut[length] = *pstr;
		length++; pstr++;
	}

	pstrOut[length] = '\0';

	while(*pstr && (*pstr == '\n' || *pstr == '\r'))
		pstr++;

	if(!(*pstr))
		return NULL;
	else
		return pstr;
}

//===========================================
// Con_Printf
//
//===========================================
void COM_ToLowerCase( char* pstr )
{
	char* _pstr = pstr;
	while(*_pstr)
	{
		*_pstr = tolower(*_pstr);
		_pstr++;
	}
}

// frac should always be multiplied by frametime
float lerp(float start, float end, float frac)
{
	// Exact, monotonic, bounded, determinate, and (for start=b=0) consistent:
	if (start <= 0 && end >= 0 || start >= 0 && end <= 0)
		return frac * end + (1.0f - frac) * start;

	if (frac == 1)
		return end; // exact
	// Exact at t=0, monotonic except near t=1,
	// bounded, determinate, and consistent:
	const float x = start + frac * (end - start);
	return frac > 1 == end > start ? max(end, x) : min(end, x); // monotonic near t=1
}

double dlerp(double start, double end, double frac)
{
	// Exact, monotonic, bounded, determinate, and (for start=b=0) consistent:
	if (start <= 0 && end >= 0 || start >= 0 && end <= 0)
		return frac * end + (1.0 - frac) * start;

	if (frac == 1)
		return end; // exact
	// Exact at t=0, monotonic except near t=1,
	// bounded, determinate, and consistent:
	const float x = start + frac * (end - start);
	return frac > 1 == end > start ? max(end, x) : min(end, x); // monotonic near t=1
}

float SmoothValues(float startValue, float endValue, float speed)
{
	float absd, d, finalValue;
	d = endValue - startValue;
	absd = fabs(d);

	if (absd > 0.01f)
	{
		if (d > 0)
			finalValue = startValue + (absd * speed);
		else
			finalValue = startValue - (absd * speed);
	}
	else
	{
		finalValue = endValue;
	}
	startValue = finalValue;
	return startValue;
}

float RemapVal(float val, float A, float B, float C, float D)
{
	if (A == B)
		return val >= B ? D : C;
	return C + (D - C) * (val - A) / (B - A);
}

Vector VectorInvertPitch(const Vector in)
{
	return Vector(-in[0], in[1], in[2]);
}

Vector VectorInvert(const Vector in, bool x, bool y, bool z)
{
	return Vector(x ? -in[0] : in[0], y ? -in[1] : in[1], z ? -in[2] : in[2]);
}