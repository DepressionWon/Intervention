//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#if !defined( STUDIO_UTIL_H )
#define STUDIO_UTIL_H
#if defined( WIN32 )
#pragma once
#endif

#ifndef M_PI
#define M_PI (float)3.14159265358979323846
#endif

#ifndef M_PI2
#define M_PI2 (float)6.28318530717958647692
#endif

#ifndef PITCH
// MOVEMENT INFO
// up / down
#define	PITCH	0
// left / right
#define	YAW		1
// fall over
#define	ROLL	2
#endif

typedef vec_t matrix3x4[3][4];

#define FDotProduct( a, b ) (fabs((a[0])*(b[0])) + fabs((a[1])*(b[1])) + fabs((a[2])*(b[2])))
#define RAD2DEG(x) ((float)(x) * (float)(180.f / M_PI))
#define DEG2RAD(x) ((float)(x) * (float)(M_PI / 180.f))

void	AngleMatrix (const float *angles, float (*matrix)[4] );
int		VectorCompare (const float *v1, const float *v2);
void	CrossProduct (const float *v1, const float *v2, float *cross);
void	VectorTransform (const float *in1, float in2[3][4], float *out);
void	ConcatTransforms (float in1[3][4], float in2[3][4], float out[3][4]);
void	MatrixCopy( float in[3][4], float out[3][4] );
void	QuaternionMatrix( vec4_t quaternion, float (*matrix)[4] );
void	QuaternionSlerp( vec4_t p, vec4_t q, float t, vec4_t qt );
void	AngleQuaternion( float *angles, vec4_t quaternion );
void	VectorRotate (const float *in1, float in2[3][4], float *out);
void	VectorIRotate (const float *in1, const float in2[3][4], float *out);
void	VectorNormalizeFast (float *v);
void Matrix3x4_OriginFromMatrix(const matrix3x4 in, float* out);
void Matrix3x4_AnglesFromMatrix(const matrix3x4 in, vec3_t out);
#endif // STUDIO_UTIL_H