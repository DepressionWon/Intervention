//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
 //
 // Purpose: 
 //
 // $NoKeywords: $
 //=============================================================================

 // studio_model.cpp
 // routines for setting up extra renderers and modifications

#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "com_model.h"
#include "studio.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "dlight.h"
#include "triangleapi.h"
#include "elightlist.h"

#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <math.h>

#include "studio_util.h"
#include "r_studioint.h"

#include "StudioModelRenderer.h"
#include "GameStudioModelRenderer.h"

#include "pmtrace.h"
#include "r_efx.h"
#include "event_api.h"
#include "event_args.h"
#include "in_defs.h"
#include "pm_defs.h"
#include "fog.h"

extern mspriteframe_t* GetSpriteFrame(model_t* mod, int frame);
extern void GetModelLighting(const Vector& lightposition, int effects, const Vector& skyVector, const Vector& skyColor, float directLight, alight_t& lighting);

// Global engine <-> studio model rendering code interface
extern engine_studio_api_t IEngineStudio;


/*
====================
StudioSetupRenderer

====================
*/
void CStudioModelRenderer::StudioSetupRenderer(int rendermode)
{
	glDisable(GL_BLEND);

	// Set the rendering mode
	gEngfuncs.pTriAPI->RenderMode(rendermode);

	// Push texture state
	glPushAttrib(GL_TEXTURE_BIT);

	glActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);

	glActiveTexture(GL_TEXTURE2);
	glDisable(GL_TEXTURE_2D);

	glActiveTexture(GL_TEXTURE3);
	glDisable(GL_TEXTURE_2D);

	// Set the active texture unit
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);

	// Set up texture state
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PRIMARY_COLOR_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 2);

	// set smoothing
	glShadeModel(GL_SMOOTH);

	glColor4f(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
	glDepthFunc(GL_LEQUAL);

	// Set this to 0 first
	m_uiActiveTextureId = 0;
}


// ================================= //
// =====	Stencil Shadows	   ===== //
// ================================= //

/*
====================
StudioGetMinsMaxs

====================
*/
void CStudioModelRenderer::StudioGetMinsMaxs(Vector& outMins, Vector& outMaxs)
{
    if (m_pCurrentEntity->curstate.sequence >= m_pStudioHeader->numseq)
        m_pCurrentEntity->curstate.sequence = 0;

    // Build full bounding box
    mstudioseqdesc_t* pseqdesc = (mstudioseqdesc_t*)((byte*)m_pStudioHeader + m_pStudioHeader->seqindex) + m_pCurrentEntity->curstate.sequence;

    vec3_t vTemp;
    static vec3_t vBounds[8];

    for (int i = 0; i < 8; i++)
    {
        if (i & 1) vTemp[0] = pseqdesc->bbmin[0];
        else vTemp[0] = pseqdesc->bbmax[0];
        if (i & 2) vTemp[1] = pseqdesc->bbmin[1];
        else vTemp[1] = pseqdesc->bbmax[1];
        if (i & 4) vTemp[2] = pseqdesc->bbmin[2];
        else vTemp[2] = pseqdesc->bbmax[2];
        VectorCopy(vTemp, vBounds[i]);
    }

    float rotationMatrix[3][4];
    m_pCurrentEntity->angles[PITCH] = -m_pCurrentEntity->angles[PITCH];
    AngleMatrix(m_pCurrentEntity->angles, rotationMatrix);
    m_pCurrentEntity->angles[PITCH] = -m_pCurrentEntity->angles[PITCH];

    for (int i = 0; i < 8; i++)
    {
        VectorCopy(vBounds[i], vTemp);
        VectorRotate(vTemp, rotationMatrix, vBounds[i]);
    }

    // Set the bounding box
    outMins = Vector(9999, 9999, 9999);
    outMaxs = Vector(-9999, -9999, -9999);
    for (int i = 0; i < 8; i++)
    {
        // Mins
        if (vBounds[i][0] < outMins[0]) outMins[0] = vBounds[i][0];
        if (vBounds[i][1] < outMins[1]) outMins[1] = vBounds[i][1];
        if (vBounds[i][2] < outMins[2]) outMins[2] = vBounds[i][2];

        // Maxs
        if (vBounds[i][0] > outMaxs[0]) outMaxs[0] = vBounds[i][0];
        if (vBounds[i][1] > outMaxs[1]) outMaxs[1] = vBounds[i][1];
        if (vBounds[i][2] > outMaxs[2]) outMaxs[2] = vBounds[i][2];
    }

    VectorAdd(outMins, m_pCurrentEntity->origin, outMins);
    VectorAdd(outMaxs, m_pCurrentEntity->origin, outMaxs);
}

/*
====================
StudioEntityLight

====================
*/
void CStudioModelRenderer::StudioEntityLight(void)
{
    Vector mins, maxs;
    StudioGetMinsMaxs(mins, maxs);

    // Get elight list
    gELightList.GetLightList(m_pCurrentEntity->origin, mins, maxs, m_pEntityLights, &m_iNumEntityLights);

    // Reset this anyway
    m_iClosestLight = -1;

    if (!m_iNumEntityLights)
        return;

    Vector transOrigin;
    float flClosestDist = -1;

    // Find closest light origin
    for (unsigned int i = 0; i < m_iNumEntityLights; i++)
    {
        elight_t* plight = m_pEntityLights[i];

        if (!plight->temporary)
        {
            Vector& origin = m_pCurrentEntity->origin;
            if (plight->origin[0] > maxs[0] || plight->origin[1] > maxs[1] || plight->origin[2] > maxs[2]
                || plight->origin[0] < mins[0] || plight->origin[1] < mins[1] || plight->origin[2] < mins[2])
            {
                float flDist = (plight->origin - origin).Length();
                if (flClosestDist == -1 || flClosestDist > flDist)
                {
                    flClosestDist = flDist;
                    m_iClosestLight = i;
                }
            }
        }

        for (int j = 0; j < m_pStudioHeader->numbones; j++)
        {
            transOrigin[0] = m_pEntityLights[i]->origin[0] - (*m_pbonetransform)[j][0][3];
            transOrigin[1] = m_pEntityLights[i]->origin[1] - (*m_pbonetransform)[j][1][3];
            transOrigin[2] = m_pEntityLights[i]->origin[2] - (*m_pbonetransform)[j][2][3];
            VectorIRotate(transOrigin, (*m_pbonetransform)[j], m_lightLocalOrigins[i][j]);
        }
    }
}

/*
====================
StudioSetupShadows

====================
*/
void CStudioModelRenderer::StudioSetupShadows(void)
{
    if (IEngineStudio.IsHardware() != 1)
        return;

    // Determine the shading angle
    if (m_iClosestLight == -1 || (int)m_pCvarDrawShadows->value == 1)
    {
        Vector shadeVector;
        shadeVector[0] = 0.3;
        shadeVector[1] = 0.5;
        shadeVector[2] = 1;

        VectorInverse(shadeVector);
        shadeVector = shadeVector.Normalize();

        m_vShadowLightVector = shadeVector;
        m_shadowLightType = SL_TYPE_LIGHTVECTOR;
    }
    else
    {
        elight_t* plight = m_pEntityLights[m_iClosestLight];
        m_vShadowLightOrigin = plight->origin;
        m_shadowLightType = SL_TYPE_POINTLIGHT;
    }
}

/*
====================
StudioSetupModelSVD

====================
*/
void CStudioModelRenderer::StudioSetupModelSVD(int bodypart)
{
    if (bodypart > m_pSVDHeader->numbodyparts)
        bodypart = 0;

    svdbodypart_t* pbodypart = (svdbodypart_t*)((byte*)m_pSVDHeader + m_pSVDHeader->bodypartindex) + bodypart;

    int index = m_pCurrentEntity->curstate.body / pbodypart->base;
    index = index % pbodypart->numsubmodels;

    m_pSVDSubModel = (svdsubmodel_t*)((byte*)m_pSVDHeader + pbodypart->submodelindex) + index;
}

/*
====================
GL_StudioDrawShadow

====================
*/
void CStudioModelRenderer::GL_StudioDrawShadow(void)
{
    if (m_pCurrentEntity == gEngfuncs.GetViewModel())
        return;

    if (!m_pSubModel->numverts)
        return;

    if (m_pCvarDrawShadows->value != 0)
        return;

    vec3_t shadeVector;
    if (m_iClosestLight != -1)
    {
        vec3_t origin;
        for (int i = 0; i < 3; i++)
            origin[i] = (*m_protationmatrix)[i][3];

        VectorSubtract(m_pCurrentEntity->origin, m_pEntityLights[m_iClosestLight]->origin, shadeVector);
        VectorNormalizeFast(shadeVector);
        VectorInverse(shadeVector);
    }
    else
    {
        shadeVector[0] = 0.3;
        shadeVector[1] = 0.5;
        shadeVector[2] = 1;
    }
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glColor4f(GL_ZERO, GL_ZERO, GL_ZERO, 0.5);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (int j = 0; j < m_pSubModel->nummesh; j++)
    {
        mstudiomesh_t* pmesh = (mstudiomesh_t*)((byte*)m_pStudioHeader + m_pSubModel->meshindex) + j;
        short* ptricmds = (short*)((byte*)m_pStudioHeader + pmesh->triindex);

        int i = 0;
        while (i = *(ptricmds++))
        {
            if (i < 0)
            {
                glBegin(GL_TRIANGLE_FAN);
                i = -i;
            }
            else
            {
                glBegin(GL_TRIANGLE_STRIP);
            }

            for (; i > 0; i--, ptricmds += 4)
            {
                vec3_t* pvertex = m_vertexTransform + ptricmds[0];
                float f1 = pvertex->y;
                float f2 = pvertex->z - m_flShadowHeight;
                float f3 = pvertex->x - shadeVector[0] * f2;

                float vz = m_flShadowHeight + 0.1;
                float vx = f3;
                float vy = f1 - f2 * shadeVector[1];

                glVertex3f(vx, vy, vz);
            }
            glEnd();
        }
    }

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glColor4f(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
    glDepthFunc(GL_LEQUAL);
}

/*
====================
StudioShouldDrawShadow

====================
*/
bool CStudioModelRenderer::StudioShouldDrawShadow(void)
{
    if (m_pCvarDrawShadows->value < 1)
        return false;

    if (IEngineStudio.IsHardware() != 1)
        return false;

    if (!m_pRenderModel->visdata)
        return false;

    if (m_pCurrentEntity->curstate.renderfx == 101)
        return false;

    // Fucking butt-ugly hack to make the shadows less annoying
    pmtrace_t tr;
    gEngfuncs.pEventAPI->EV_SetTraceHull(2);
    gEngfuncs.pEventAPI->EV_PlayerTrace(m_vRenderOrigin, m_pCurrentEntity->origin + Vector(0, 0, 1), PM_WORLD_ONLY, -1, &tr);

    if (tr.fraction != 1.0)
        return false;

    return true;
}

/*
====================
StudioDrawShadow

====================
*/
void CStudioModelRenderer::StudioDrawShadow(void)
{
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);

    // Disabable these to avoid slowdown bug
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    glClientActiveTexture(GL_TEXTURE1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE2);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE3);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    glVertexPointer(3, GL_FLOAT, sizeof(Vector), m_vertexTransform);
    glEnableClientState(GL_VERTEX_ARRAY);

    // Set SVD header
    m_pSVDHeader = (svdheader_t*)m_pRenderModel->visdata;

    glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // disable writes to color buffer

    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 0, ~0);

    if (m_bTwoSideSupported)
    {
        glDisable(GL_CULL_FACE);
        glEnable(GL_STENCIL_TEST_TWO_SIDE_EXT);
    }

    for (int i = 0; i < m_pStudioHeader->numbodyparts; i++)
    {
        StudioSetupModelSVD(i);
        StudioDrawShadowVolume();
    }

    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST);

    if (m_bTwoSideSupported)
    {
        glEnable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST_TWO_SIDE_EXT);
    }

    glDisableClientState(GL_VERTEX_ARRAY);

    glPopClientAttrib();
}

/*
====================
StudioDrawShadowVolume

====================
*/
void CStudioModelRenderer::StudioDrawShadowVolume(void)
{
    float plane[4];
    Vector lightdir;
    Vector* pv1, * pv2, * pv3;

    if (!m_pSVDSubModel->numfaces)
        return;

    Vector* psvdverts = (Vector*)((byte*)m_pSVDHeader + m_pSVDSubModel->vertexindex);
    byte* pvertbone = ((byte*)m_pSVDHeader + m_pSVDSubModel->vertinfoindex);

    // Extrusion distance
    float extrudeDistance = m_pCvarShadowVolumeExtrudeDistance->value;

    // Calculate vertex coords
    if (m_shadowLightType == SL_TYPE_POINTLIGHT)
    {
        // For point light sources
        for (int i = 0, j = 0; i < m_pSVDSubModel->numverts; i++, j += 2)
        {
            VectorTransform(psvdverts[i], (*m_pbonetransform)[pvertbone[i]], m_vertexTransform[j]);

            VectorSubtract(m_vertexTransform[j], m_vShadowLightOrigin, lightdir);
            VectorNormalizeFast(lightdir);

            VectorMA(m_vertexTransform[j], extrudeDistance, lightdir, m_vertexTransform[j + 1]);
        }
    }
    else
    {
        for (int i = 0, j = 0; i < m_pSVDSubModel->numverts; i++, j += 2)
        {
            VectorTransform(psvdverts[i], (*m_pbonetransform)[pvertbone[i]], m_vertexTransform[j]);
            VectorMA(m_vertexTransform[j], extrudeDistance, m_vShadowLightVector, m_vertexTransform[j + 1]);
        }
    }

    // Process the faces
    int numIndexes = 0;
    svdface_t* pfaces = (svdface_t*)((byte*)m_pSVDHeader + m_pSVDSubModel->faceindex);

    if (m_shadowLightType == SL_TYPE_POINTLIGHT)
    {
        // For point light sources
        for (int i = 0; i < m_pSVDSubModel->numfaces; i++)
        {
            pv1 = &m_vertexTransform[pfaces[i].vertex0];
            pv2 = &m_vertexTransform[pfaces[i].vertex1];
            pv3 = &m_vertexTransform[pfaces[i].vertex2];

            plane[0] = pv1->y * (pv2->z - pv3->z) + pv2->y * (pv3->z - pv1->z) + pv3->y * (pv1->z - pv2->z);
            plane[1] = pv1->z * (pv2->x - pv3->x) + pv2->z * (pv3->x - pv1->x) + pv3->z * (pv1->x - pv2->x);
            plane[2] = pv1->x * (pv2->y - pv3->y) + pv2->x * (pv3->y - pv1->y) + pv3->x * (pv1->y - pv2->y);
            plane[3] = -(pv1->x * (pv2->y * pv3->z - pv3->y * pv2->z) + pv2->x * (pv3->y * pv1->z - pv1->y * pv3->z) + pv3->x * (pv1->y * pv2->z - pv2->y * pv1->z));

            m_trianglesFacingLight[i] = (DotProduct(plane, m_vShadowLightOrigin) + plane[3]) > 0;
            if (m_trianglesFacingLight[i])
            {
                m_shadowVolumeIndexes[numIndexes] = pfaces[i].vertex0;
                m_shadowVolumeIndexes[numIndexes + 1] = pfaces[i].vertex2;
                m_shadowVolumeIndexes[numIndexes + 2] = pfaces[i].vertex1;

                m_shadowVolumeIndexes[numIndexes + 3] = pfaces[i].vertex0 + 1;
                m_shadowVolumeIndexes[numIndexes + 4] = pfaces[i].vertex1 + 1;
                m_shadowVolumeIndexes[numIndexes + 5] = pfaces[i].vertex2 + 1;

                numIndexes += 6;
            }
        }
    }
    else
    {
        // For a light vector
        for (int i = 0; i < m_pSVDSubModel->numfaces; i++)
        {
            pv1 = &m_vertexTransform[pfaces[i].vertex0];
            pv2 = &m_vertexTransform[pfaces[i].vertex1];
            pv3 = &m_vertexTransform[pfaces[i].vertex2];

            // Calculate normal of the face
            plane[0] = pv1->y * (pv2->z - pv3->z) + pv2->y * (pv3->z - pv1->z) + pv3->y * (pv1->z - pv2->z);
            plane[1] = pv1->z * (pv2->x - pv3->x) + pv2->z * (pv3->x - pv1->x) + pv3->z * (pv1->x - pv2->x);
            plane[2] = pv1->x * (pv2->y - pv3->y) + pv2->x * (pv3->y - pv1->y) + pv3->x * (pv1->y - pv2->y);

            m_trianglesFacingLight[i] = DotProduct(plane, m_vShadowLightVector) > 0;
            if (m_trianglesFacingLight[i])
            {
                m_shadowVolumeIndexes[numIndexes] = pfaces[i].vertex0;
                m_shadowVolumeIndexes[numIndexes + 1] = pfaces[i].vertex2;
                m_shadowVolumeIndexes[numIndexes + 2] = pfaces[i].vertex1;

                m_shadowVolumeIndexes[numIndexes + 3] = pfaces[i].vertex0 + 1;
                m_shadowVolumeIndexes[numIndexes + 4] = pfaces[i].vertex1 + 1;
                m_shadowVolumeIndexes[numIndexes + 5] = pfaces[i].vertex2 + 1;

                numIndexes += 6;
            }
        }
    }

    // Process the edges
    svdedge_t* pedges = (svdedge_t*)((byte*)m_pSVDHeader + m_pSVDSubModel->edgeindex);
    for (int i = 0; i < m_pSVDSubModel->numedges; i++)
    {
        if (m_trianglesFacingLight[pedges[i].face0])
        {
            if ((pedges[i].face1 != -1) && m_trianglesFacingLight[pedges[i].face1])
                continue;

            m_shadowVolumeIndexes[numIndexes] = pedges[i].vertex0;
            m_shadowVolumeIndexes[numIndexes + 1] = pedges[i].vertex1;
        }
        else
        {
            if ((pedges[i].face1 == -1) || !m_trianglesFacingLight[pedges[i].face1])
                continue;

            m_shadowVolumeIndexes[numIndexes] = pedges[i].vertex1;
            m_shadowVolumeIndexes[numIndexes + 1] = pedges[i].vertex0;
        }

        m_shadowVolumeIndexes[numIndexes + 2] = m_shadowVolumeIndexes[numIndexes] + 1;
        m_shadowVolumeIndexes[numIndexes + 3] = m_shadowVolumeIndexes[numIndexes + 2];
        m_shadowVolumeIndexes[numIndexes + 4] = m_shadowVolumeIndexes[numIndexes + 1];
        m_shadowVolumeIndexes[numIndexes + 5] = m_shadowVolumeIndexes[numIndexes + 1] + 1;
        numIndexes += 6;
    }

    if (m_bTwoSideSupported)
    {
        glActiveStencilFaceEXT(GL_BACK);
        glStencilOp(GL_KEEP, GL_INCR_WRAP_EXT, GL_KEEP);
        glStencilMask(~0);

        glActiveStencilFaceEXT(GL_FRONT);
        glStencilOp(GL_KEEP, GL_DECR_WRAP_EXT, GL_KEEP);
        glStencilMask(~0);

        glDrawElements(GL_TRIANGLES, numIndexes, GL_UNSIGNED_SHORT, m_shadowVolumeIndexes);
    }
    else
    {
        if (m_shadowLightType != SL_TYPE_POINTLIGHT)
        {
            // draw back faces incrementing stencil values when z fails
            glStencilOp(GL_KEEP, GL_INCR, GL_KEEP);
            glCullFace(GL_FRONT);
            glDrawElements(GL_TRIANGLES, numIndexes, GL_UNSIGNED_SHORT, m_shadowVolumeIndexes);

            // draw front faces decrementing stencil values when z fails
            glStencilOp(GL_KEEP, GL_DECR, GL_KEEP);
            glCullFace(GL_BACK);
            glDrawElements(GL_TRIANGLES, numIndexes, GL_UNSIGNED_SHORT, m_shadowVolumeIndexes);
        }
        else
        {
            // draw back faces incrementing stencil values when z fails
            glStencilOp(GL_KEEP, GL_INCR, GL_KEEP);
            glCullFace(GL_BACK);
            glDrawElements(GL_TRIANGLES, numIndexes, GL_UNSIGNED_SHORT, m_shadowVolumeIndexes);

            // draw front faces decrementing stencil values when z fails
            glStencilOp(GL_KEEP, GL_DECR, GL_KEEP);
            glCullFace(GL_FRONT);
            glDrawElements(GL_TRIANGLES, numIndexes, GL_UNSIGNED_SHORT, m_shadowVolumeIndexes);
        }
    }
}

// ================================= //
// =====	Vertex Lighting	   ===== //
// ================================= //

/*
====================
StudioSetupModel

====================
*/
void CStudioModelRenderer::StudioSetupModel(int bodypart)
{
	if (bodypart > m_pStudioHeader->numbodyparts)
		bodypart = 0;

	m_pBodyPart = (mstudiobodyparts_t*)((byte*)m_pStudioHeader + m_pStudioHeader->bodypartindex) + bodypart;

	int index = m_pCurrentEntity->curstate.body / m_pBodyPart->base;
	index = index % m_pBodyPart->nummodels;

	m_pSubModel = (mstudiomodel_t*)((byte*)m_pStudioHeader + m_pBodyPart->modelindex) + index;
}

/*
====================
StudioSetLightVectors

====================
*/
void CStudioModelRenderer::StudioSetLightVectors(void)
{
	for (int j = 0; j < m_pStudioHeader->numbones; j++)
		VectorIRotate(m_vLightDirection, (*m_pbonetransform)[j], m_lightVectors[j]);
}

/*
====================
StudioSetChromeVectors

====================
*/
void CStudioModelRenderer::StudioSetChromeVectors(void)
{
	vec3_t tmp;
	vec3_t chromeupvec;
	vec3_t chromerightvec;

	for (int i = 0; i < m_pStudioHeader->numbones; i++)
	{
		VectorScale(m_chromeOrigin, -1, tmp);
		tmp[0] += (*m_pbonetransform)[i][0][3];
		tmp[1] += (*m_pbonetransform)[i][1][3];
		tmp[2] += (*m_pbonetransform)[i][2][3];

		VectorNormalizeFast(tmp);
		CrossProduct(tmp, m_vRight, chromeupvec);
		VectorNormalizeFast(chromeupvec);
		CrossProduct(tmp, chromeupvec, chromerightvec);
		VectorNormalizeFast(chromerightvec);

		VectorIRotate(chromeupvec, (*m_pbonetransform)[i], m_chromeUp[i]);
		VectorIRotate(chromerightvec, (*m_pbonetransform)[i], m_chromeRight[i]);
	}
}

/*
====================
StudioLightsforVertex

====================
*/
__forceinline void CStudioModelRenderer::StudioLightsforVertex(int index, byte boneindex, const vec3_t& origin)
{
	static unsigned int i;
	static vec3_t dir;

	static float radius;
	static float dist;
	static float attn;

	static elight_t* plight;

	for (i = 0; i < m_iNumEntityLights; i++)
	{
		plight = m_pEntityLights[i];

		// Inverse square radius
		radius = plight->radius * plight->radius;
		VectorSubtract(m_lightLocalOrigins[i][boneindex], origin, dir);

		dist = DotProduct(dir, dir);
		attn = max((dist / radius - 1) * -1, 0);

		m_lightStrengths[i][index] = attn;

		VectorNormalizeFast(dir);
		VectorCopy(dir, m_lightShadeVectors[i][index]);
	}
}

/*
====================
StudioLightsforVertex

====================
*/
__forceinline void CStudioModelRenderer::StudioLighting(float* lv, byte bone, int flags, const vec3_t& normal)
{
	static float illum;
	static float lightcos;

	illum = m_lightingInfo.ambientlight;

	if (flags & STUDIO_NF_FLATSHADE)
	{
		illum += m_lightingInfo.shadelight * 0.8;
	}
	else
	{
		lightcos = DotProduct(normal, m_lightVectors[bone]); // -1 colinear, 1 opposite

		if (lightcos > 1.0)
			lightcos = 1;

		illum += m_lightingInfo.shadelight;

		lightcos = (lightcos + (m_pCvarLambert->value - 1.0)) / m_pCvarLambert->value; 		// do modified hemispherical lighting
		if (lightcos > 0.0)
			illum -= m_lightingInfo.shadelight * lightcos;

		if (illum <= 0)
			illum = 0;
	}

	if (illum > 255)
		illum = 255;

	*lv = illum / 255.0;	// Light from 0 to 1.0
}

/*
====================
LightValueforVertex

====================
*/
__forceinline void CStudioModelRenderer::LightValueforVertex(vec3_t& outColor, int vertindex, int normindex, const vec3_t& normal)
{
	static float fldot;
	static unsigned int i;

	static elight_t* plight;
	outColor = m_lightValues[normindex];

	if (m_iNumEntityLights)
	{
		for (i = 0; i < m_iNumEntityLights; i++)
		{
			plight = m_pEntityLights[i];

			fldot = max(DotProduct(normal, m_lightShadeVectors[i][vertindex]), 0);
			VectorMA(outColor, m_lightStrengths[i][vertindex] * fldot, plight->color, outColor);
		}
	}
}

/*
====================
StudioChrome

====================
*/
__forceinline void CStudioModelRenderer::StudioChrome(int normIndex, int bone, const vec3_t& normal)
{
	// calc s coord
	float n = DotProduct(normal, m_chromeRight[bone]);
	m_chromeCoords[normIndex][0] = (n + 1.0) * 32; // FIX: make this a float

	// calc t coord
	n = DotProduct(normal, m_chromeUp[bone]);
	m_chromeCoords[normIndex][1] = (n + 1.0) * 32; // FIX: make this a float
}

/*
====================
StudioDrawMesh

====================
*/
void CStudioModelRenderer::StudioDrawMesh(mstudiomesh_t* pmesh, mstudiotexture_t* ptexture, float alpha)
{
	int i;
	vec3_t color;

	vec3_t* pstudioverts = (vec3_t*)((byte*)m_pStudioHeader + m_pSubModel->vertindex);
	vec3_t* pstudionorms = (vec3_t*)((byte*)m_pStudioHeader + m_pSubModel->normindex);

	short* ptricmds = (short*)((byte*)m_pStudioHeader + pmesh->triindex);

	if (m_uiActiveTextureId != ptexture->index)
	{
		glBindTexture(GL_TEXTURE_2D, ptexture->index);
		m_uiActiveTextureId = ptexture->index;
	}

	// Set to base and scale the texture matrix
	glLoadIdentity();
	glScalef(1.0 / (float)ptexture->width, 1.0 / (float)ptexture->height, 1.0);

	if (ptexture->flags & STUDIO_NF_CHROME)
	{
		while (i = *(ptricmds++))
		{
			if (i < 0)
			{
				glBegin(GL_TRIANGLE_FAN);
				i = -i;
			}
			else
			{
				glBegin(GL_TRIANGLE_STRIP);
			}


			for (; i > 0; i--, ptricmds += 4)
			{
				LightValueforVertex(color, ptricmds[0], ptricmds[1], pstudionorms[ptricmds[1]]);

				glTexCoord2f(m_chromeCoords[ptricmds[1]][0], m_chromeCoords[ptricmds[1]][1]);
				glColor4f(color[0], color[1], color[2], alpha);
				glVertex3fv(m_vertexTransform[ptricmds[0]]);
			}
			glEnd();
		}
	}
	else
	{
		while (i = *(ptricmds++))
		{
			if (i < 0)
			{
				glBegin(GL_TRIANGLE_FAN);
				i = -i;
			}
			else
			{
				glBegin(GL_TRIANGLE_STRIP);
			}

			for (; i > 0; i--, ptricmds += 4)
			{
				LightValueforVertex(color, ptricmds[0], ptricmds[1], pstudionorms[ptricmds[1]]);

				glTexCoord2i(ptricmds[2], ptricmds[3]);
				glColor4f(color[0], color[1], color[2], alpha);
				glVertex3fv(m_vertexTransform[ptricmds[0]]);
			}
			glEnd();
		}
	}
}

/*
====================
StudioDrawPoints

====================
*/
void CStudioModelRenderer::StudioDrawPoints(void)
{
	float lightStrength;

	float alpha;
	if (m_pCurrentEntity->curstate.rendermode != kRenderNormal)
		alpha = (float)m_pCurrentEntity->curstate.renderamt / 255.0f;
	else
		alpha = 1.0;

	byte* pvertbone = ((byte*)m_pStudioHeader + m_pSubModel->vertinfoindex);
	byte* pnormbone = ((byte*)m_pStudioHeader + m_pSubModel->norminfoindex);
	mstudiotexture_t* ptextures = (mstudiotexture_t*)((byte*)m_pTextureHeader + m_pTextureHeader->textureindex);

	mstudiomesh_t* pmeshes = (mstudiomesh_t*)((byte*)m_pStudioHeader + m_pSubModel->meshindex);

	vec3_t* pstudioverts = (vec3_t*)((byte*)m_pStudioHeader + m_pSubModel->vertindex);
	vec3_t* pstudionorms = (vec3_t*)((byte*)m_pStudioHeader + m_pSubModel->normindex);

	int skinNum = m_pCurrentEntity->curstate.skin;
	short* pskinref = (short*)((byte*)m_pTextureHeader + m_pTextureHeader->skinindex);
	if (skinNum != 0 && skinNum < m_pTextureHeader->numskinfamilies)
		pskinref += (skinNum * m_pTextureHeader->numskinref);

	//
	// Transform the vertices
	//
	for (int i = 0; i < m_pSubModel->numverts; i++)
		VectorTransform(pstudioverts[i], (*m_pbonetransform)[pvertbone[i]], m_vertexTransform[i]);

	//
	// Calculate light values
	//
	for (int j = 0, normIndex = 0; j < m_pSubModel->nummesh; j++)
	{
		int flags = ptextures[pskinref[pmeshes[j].skinref]].flags;
		for (int i = 0; i < pmeshes[j].numnorms; i++, normIndex++)
		{
			StudioLighting(&lightStrength, pnormbone[normIndex], flags, (float*)pstudionorms[normIndex]);
			VectorScale(m_lightingInfo.color, lightStrength, m_lightValues[normIndex]);
		}
	}

	//
	// Calculate chrome for each vertex
	//
	int normIndex = 0;
	for (int j = 0; j < m_pSubModel->nummesh; j++)
	{
		int flags = ptextures[pskinref[pmeshes[j].skinref]].flags;

		// Skip non-chrome parts
		if (!(flags & STUDIO_NF_CHROME))
		{
			normIndex += pmeshes[j].numnorms;
			continue;
		}

		for (int i = 0; i < pmeshes[j].numnorms; i++, normIndex++)
			StudioChrome(normIndex, pnormbone[normIndex], (float*)pstudionorms[normIndex]);
	}

	//
	// Calculate light data for elights
	//
	if (m_iNumEntityLights > 0)
	{
		for (int i = 0; i < m_pSubModel->numverts; i++)
			StudioLightsforVertex(i, pvertbone[i], pstudioverts[i]);
	}

	// Set matrix mode to texture here
	glMatrixMode(GL_TEXTURE);

	int flags;
	for (int j = 0; j < m_pSubModel->nummesh; j++)
	{
		mstudiomesh_t* pmesh = &pmeshes[j];
		mstudiotexture_t* ptexture = &ptextures[pskinref[pmesh->skinref]];

		if (ptexture->flags & (STUDIO_NF_ADDITIVE | STUDIO_NF_ALPHABLEND))
		{
			flags |= ptexture->flags;
			continue;
		}

		if (ptexture->flags & STUDIO_NF_ALPHATEST)
		{
			glEnable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GREATER, 0.5);
		}

		StudioDrawMesh(pmesh, ptexture, alpha);

		if (ptexture->flags & STUDIO_NF_ALPHATEST)
		{
			glDisable(GL_ALPHA_TEST);
			glAlphaFunc(GL_GREATER, 0);
		}
	}

	if (flags & (STUDIO_NF_ADDITIVE | STUDIO_NF_ALPHABLEND))
	{
		glEnable(GL_BLEND);
		glDepthMask(GL_FALSE);

		// Draw additive last
		for (int j = 0; j < m_pSubModel->nummesh; j++)
		{
			mstudiomesh_t* pmesh = &pmeshes[j];
			mstudiotexture_t* ptexture = &ptextures[pskinref[pmesh->skinref]];

			if (ptexture->flags & STUDIO_NF_ADDITIVE)
			{
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);
				gFog.BlackFog();
			}
			else if (ptexture->flags & STUDIO_NF_ALPHABLEND)
			{
				if (m_pCurrentEntity->curstate.rendermode != kRenderNormal)
					alpha = (m_pCurrentEntity->curstate.renderamt / 255.0f) * 0.25;
				else
					alpha = 0.25;

				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			else
				continue;

			StudioDrawMesh(pmesh, ptexture, alpha);

			// Reset this
			if (m_pCurrentEntity->curstate.rendermode != kRenderNormal)
				alpha = (float)m_pCurrentEntity->curstate.renderamt / 255.0f;
			else
				alpha = 1.0;

			if (ptexture->flags & STUDIO_NF_ADDITIVE)
				gFog.RenderFog();
		}

		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();

	glMatrixMode(GL_MODELVIEW);
}

/*
====================
SetupTextureHeader

====================
*/
void CStudioModelRenderer::StudioSetupTextureHeader(void)
{
	if (m_pStudioHeader->numtextures && m_pStudioHeader->textureindex)
	{
		m_pTextureHeader = m_pStudioHeader;
		return;
	}

	if (m_pRenderModel->lightdata)
	{
		m_pTextureHeader = (studiohdr_t*)((model_t*)m_pRenderModel->lightdata)->cache.data;
		return;
	}

	char szName[64];
	strcpy(szName, m_pRenderModel->name);
	strcpy(&szName[(strlen(szName) - 4)], "T.mdl");

	// Potential crash with Mod_ForName
	model_t* pModel = IEngineStudio.Mod_ForName(szName, TRUE);
	if (!pModel)
		return;

	m_pTextureHeader = (studiohdr_t*)pModel->cache.data;
	m_pRenderModel->lightdata = (color24*)pModel;
	pModel->clipnodes = (dclipnode_t*)m_pRenderModel;
}

/*
====================
StudioSetupGlowShellNormals

====================
*/
void CStudioModelRenderer::StudioSetupGlowShellNormals(void)
{
	vec3_t* pstudionorms = (vec3_t*)((byte*)m_pStudioHeader + m_pSubModel->normindex);
	mstudiomesh_t* pmeshes = (mstudiomesh_t*)((byte*)m_pStudioHeader + m_pSubModel->meshindex);

	// Reset the array
	for (int i = 0; i < m_pSubModel->numverts; i++)
	{
		m_vertexNormals[i] = Vector(0, 0, 0);
		m_vertexNumNormals[i] = 0;
	}

	// Use tricmds to construct the combined normals
	for (int j = 0; j < m_pSubModel->nummesh; j++)
	{
		mstudiomesh_t* pmesh = &pmeshes[j];

		short* ptricmds = (short*)((byte*)m_pStudioHeader + pmesh->triindex);

		int i = 0;
		while (i = *(ptricmds++))
		{
			if (i < 0)
				i = -i;

			for (; i > 0; i--, ptricmds += 4)
			{
				VectorAdd(m_vertexNormals[ptricmds[0]], pstudionorms[ptricmds[1]], m_vertexNormals[ptricmds[0]]);
				m_vertexNumNormals[ptricmds[0]]++;
			}
		}
	}

	// Calculate final result
	for (int i = 0; i < m_pSubModel->numverts; i++)
		VectorScale(m_vertexNormals[i], 1.0f / (float)(m_vertexNumNormals[i]), m_vertexNormals[i]);
}

/*
====================
StudioDrawGlowShell

====================
*/
void CStudioModelRenderer::StudioDrawGlowShell(void)
{
	byte* pvertbone = ((byte*)m_pStudioHeader + m_pSubModel->vertinfoindex);
	byte* pnormbone = ((byte*)m_pStudioHeader + m_pSubModel->norminfoindex);

	mstudiomesh_t* pmeshes = (mstudiomesh_t*)((byte*)m_pStudioHeader + m_pSubModel->meshindex);

	vec3_t* pstudioverts = (vec3_t*)((byte*)m_pStudioHeader + m_pSubModel->vertindex);
	vec3_t* pstudionorms = (vec3_t*)((byte*)m_pStudioHeader + m_pSubModel->normindex);

	mstudiotexture_t* ptextures = (mstudiotexture_t*)((byte*)m_pTextureHeader + m_pTextureHeader->textureindex);

	int skinNum = m_pCurrentEntity->curstate.skin;
	short* pskinref = (short*)((byte*)m_pTextureHeader + m_pTextureHeader->skinindex);
	if (skinNum != 0 && skinNum < m_pTextureHeader->numskinfamilies)
		pskinref += (skinNum * m_pTextureHeader->numskinref);

	// Set the vectors for this crap
	StudioSetupGlowShellNormals();

	//
	// Calculate chrome for each vertex
	//
	for (int i = 0; i < m_pSubModel->numverts; i++)
		StudioChrome(i, pvertbone[i], m_vertexNormals[i]);

	//
	// Transform the vertices
	//
	Vector vertexPosition;
	float scale = m_pCurrentEntity->curstate.renderamt * 0.05;

	for (int i = 0; i < m_pSubModel->numverts; i++)
	{
		VectorMA(pstudioverts[i], scale, m_vertexNormals[i], vertexPosition);
		VectorTransform(vertexPosition, (*m_pbonetransform)[pvertbone[i]], m_vertexTransform[i]);
	}

	float alpha = (float)m_pCurrentEntity->curstate.renderamt / 255.0f;
	Vector color = Vector((float)m_pCurrentEntity->curstate.rendercolor.r / 255.0f,
		(float)m_pCurrentEntity->curstate.rendercolor.g / 255.0f,
		(float)m_pCurrentEntity->curstate.rendercolor.b / 255.0f);

	glColor4f(color[0], color[1], color[2], alpha);

	// Bind the texture
	mspriteframe_t* pframe = GetSpriteFrame(m_pChromeSprite, 0);
	if (!pframe)
		return;

	glBindTexture(GL_TEXTURE_2D, pframe->gl_texturenum);
	m_uiActiveTextureId = pframe->gl_texturenum;

	glMatrixMode(GL_TEXTURE);

	for (int j = 0; j < m_pSubModel->nummesh; j++)
	{
		mstudiomesh_t* pmesh = &pmeshes[j];
		mstudiotexture_t* ptexture = &ptextures[pskinref[pmesh->skinref]];

		short* ptricmds = (short*)((byte*)m_pStudioHeader + pmesh->triindex);

		// Set to base and scale the texture matrix

		glLoadIdentity();
		glScalef(1.0 / (float)ptexture->width, 1.0 / (float)ptexture->height, 1.0);

		int i = 0;
		while (i = *(ptricmds++))
		{
			if (i < 0)
			{
				glBegin(GL_TRIANGLE_FAN);
				i = -i;
			}
			else
			{
				glBegin(GL_TRIANGLE_STRIP);
			}


			for (; i > 0; i--, ptricmds += 4)
			{
				glTexCoord2f(m_chromeCoords[ptricmds[0]][0], m_chromeCoords[ptricmds[0]][1]);
				glVertex3fv(m_vertexTransform[ptricmds[0]]);
			}
			glEnd();
		}

		glPopMatrix();
	}

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();

	glMatrixMode(GL_MODELVIEW);
}

/*
====================
StudioDynamicLight

====================
*/
void CStudioModelRenderer::StudioDynamicLight(void)
{
	Vector skyVector;
	skyVector.x = m_pSkylightDirX->value;
	skyVector.y = m_pSkylightDirY->value;
	skyVector.z = m_pSkylightDirZ->value;
	VectorNormalizeFast(skyVector);

	Vector skyColor;
	skyColor.x = m_pSkylightColorR->value;
	skyColor.y = m_pSkylightColorG->value;
	skyColor.z = m_pSkylightColorB->value;
	VectorScale(skyColor, 1.0f / 255.0f, skyColor);

	GetModelLighting(m_pCurrentEntity->origin, m_pCurrentEntity->curstate.effects, skyVector, skyColor, m_pCvarDirect->value, m_lightingInfo);
}