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
// mathlib.h

#pragma once

typedef float vec_t;
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];	// x,y,z,w
typedef vec_t vec5_t[5];

typedef short vec_s_t;
typedef vec_s_t vec3s_t[3];
typedef vec_s_t vec4s_t[4];	// x,y,z,w
typedef vec_s_t vec5s_t[5];

typedef	int	fixed4_t;
typedef	int	fixed8_t;
typedef	int	fixed16_t;

typedef vec_t matrix3x4[3][4];

#ifndef M_PI
#define M_PI (float)3.14159265358979323846
#endif

#ifndef M_PI2
#define M_PI2 (float)6.28318530717958647692
#endif

#define M_PI_F ((float)(M_PI))
#define M_PI2_F ((float)(M_PI2))

#define RAD2DEG(x) ((float)(x) * (float)(180.f / M_PI))
#define DEG2RAD(x) ((float)(x) * (float)(M_PI / 180.f))

struct mplane_s;

extern vec3_t vec3_origin;
extern	int nanmask;

#define	IS_NAN(x) (((*(int *)&x)&nanmask)==nanmask)

#ifndef VECTOR_H
	#define DotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#endif

#define VectorSubtract(a,b,c) {(c)[0]=(a)[0]-(b)[0];(c)[1]=(a)[1]-(b)[1];(c)[2]=(a)[2]-(b)[2];}
#define VectorAdd(a,b,c) {(c)[0]=(a)[0]+(b)[0];(c)[1]=(a)[1]+(b)[1];(c)[2]=(a)[2]+(b)[2];}
#define VectorCopy(a,b) {(b)[0]=(a)[0];(b)[1]=(a)[1];(b)[2]=(a)[2];}
inline void VectorClear(float* a) { a[0] = 0.0; a[1] = 0.0; a[2] = 0.0; }

void VectorMA(const float* veca, float scale, const float* vecb, float* vecc);

vec_t _DotProduct (vec3_t v1, vec3_t v2);
void _VectorSubtract (vec3_t veca, vec3_t vecb, vec3_t out);
void _VectorAdd (vec3_t veca, vec3_t vecb, vec3_t out);
void _VectorCopy (vec3_t in, vec3_t out);

int VectorCompare(const float* v1, const float* v2);
float Length(const float* v);
void CrossProduct(const float* v1, const float* v2, float* cross);
float VectorNormalize(float* v);		// returns vector length
void VectorInverse(float* v);
void VectorScale(const float* in, float scale, float* out);
int Q_log2(int val);

void SinCos(float radians, float* sine, float* cosine);
#define VectorAverage(a, b, o) ((o)[0] = ((a)[0] + (b)[0]) * 0.5, (o)[1] = ((a)[1] + (b)[1]) * 0.5, (o)[2] = ((a)[2] + (b)[2]) * 0.5)
#define DotProductAbs(x, y) (abs((x)[0] * (y)[0]) + abs((x)[1] * (y)[1]) + abs((x)[2] * (y)[2]))

void R_ConcatRotations (float in1[3][3], float in2[3][3], float out[3][3]);
void R_ConcatTransforms (float in1[3][4], float in2[3][4], float out[3][4]);

// Here are some "manual" INLINE routines for doing floating point to integer conversions
extern short new_cw, old_cw;

typedef union DLONG {
	int		i[2];
	double	d;
	float	f;
	} DLONG;

extern DLONG	dlong;

#ifdef _WIN32
void __inline set_fpu_cw(void)
{
_asm	
	{		wait
			fnstcw	old_cw
			wait
			mov		ax, word ptr old_cw
			or		ah, 0xc
			mov		word ptr new_cw,ax
			fldcw	new_cw
	}
}

int __inline quick_ftol(float f)
{
	_asm {
		// Assumes that we are already in chop mode, and only need a 32-bit int
		fld		DWORD PTR f
		fistp	DWORD PTR dlong
	}
	return dlong.i[0];
}

void __inline restore_fpu_cw(void)
{
	_asm	fldcw	old_cw
}
#else
#define set_fpu_cw() /* */
#define quick_ftol(f) ftol(f)
#define restore_fpu_cw() /* */
#endif

void FloorDivMod (double numer, double denom, int *quotient,
		int *rem);
fixed16_t Invert24To16(fixed16_t val);
int GreatestCommonDivisor (int i1, int i2);

void AngleVectors (const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
void AngleVectorsTranspose (const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
#define AngleIVectors	AngleVectorsTranspose

void AngleMatrix(const float* angles, float (*matrix)[4]);
void AngleIMatrix (const vec3_t angles, float (*matrix)[4] );
void VectorTransform(const float* in1, float in2[3][4], float* out);

void NormalizeAngles(float* angles);
void InterpolateAngles(float* start, float* end, float* output, float frac);
float AngleBetweenVectors(const float* v1, const float* v2);


void VectorMatrix( vec3_t forward, vec3_t right, vec3_t up);
void VectorAngles(const float* forward, float* angles);

int InvertMatrix( const float * m, float *out );

int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, struct mplane_s *plane);
float	anglemod(float a);



#define BOX_ON_PLANE_SIDE(emins, emaxs, p)	\
	(((p)->type < 3)?						\
	(										\
		((p)->dist <= (emins)[(p)->type])?	\
			1								\
		:									\
		(									\
			((p)->dist >= (emaxs)[(p)->type])?\
				2							\
			:								\
				3							\
		)									\
	)										\
	:										\
		BoxOnPlaneSide( (emins), (emaxs), (p)))