//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

// Triangle rendering, if any

#include "hud.h"
#include "cl_util.h"

// Triangle rendering apis are in gEngfuncs.pTriAPI

#include "const.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "triangleapi.h"
#include "fog.h"
#include "svd_render.h"
#include "elightlist.h"
#include "r_water.h"
#include "r_studioint.h"
extern engine_studio_api_t IEngineStudio;

void UpdateLaserSpot();

#define DLLEXPORT __declspec( dllexport )

extern "C"
{
	void DLLEXPORT HUD_DrawNormalTriangles( void );
	void DLLEXPORT HUD_DrawTransparentTriangles( void );
};


// TEXTURES
unsigned int g_uiScreenTex = 0;
unsigned int g_uiGlowTex = 0;

// FUNCTIONS
bool InitScreenGlow(void);
void RenderScreenGlow(void);
void DrawQuad(int width, int height, int ofsX = 0, int ofsY = 0);

cvar_t* glow_blur_steps, * glow_darken_steps, * glow_strength;

bool InitScreenGlow(void)
{
	// register the CVARs
	glow_blur_steps = gEngfuncs.pfnRegisterVariable("glow_blur_steps", "4", FCVAR_ARCHIVE);
	glow_darken_steps = gEngfuncs.pfnRegisterVariable("glow_darken_steps", "3", FCVAR_ARCHIVE);
	glow_strength = gEngfuncs.pfnRegisterVariable("glow_strength", "0", FCVAR_ARCHIVE); // future fix dw slimes

	return true;
}

bool VidInitScreenGlow()
{
	// create a load of blank pixels to create textures with
	unsigned char* pBlankTex = new unsigned char[ScreenWidth * ScreenHeight * 3];
	memset(pBlankTex, 0, ScreenWidth * ScreenHeight * 3);

	// Create the SCREEN-HOLDING TEXTURE
	glGenTextures(1, &g_uiScreenTex);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, g_uiScreenTex);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGB8, ScreenWidth, ScreenHeight, 0, GL_RGB8, GL_UNSIGNED_BYTE, pBlankTex);

	// Create the BLURRED TEXTURE
	glGenTextures(1, &g_uiGlowTex);
	glBindTexture(GL_TEXTURE_RECTANGLE_NV, g_uiGlowTex);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE_NV, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGB8, ScreenWidth / 2, ScreenHeight / 2, 0, GL_RGB8, GL_UNSIGNED_BYTE, pBlankTex);

	// free the memory
	delete[] pBlankTex;

	return true;
}


void DrawQuad(int width, int height, int ofsX, int ofsY)
{
	glTexCoord2f(ofsX, ofsY);
	glVertex3f(0, 1, -1);
	glTexCoord2f(ofsX, height + ofsY);
	glVertex3f(0, 0, -1);
	glTexCoord2f(width + ofsX, height + ofsY);
	glVertex3f(1, 0, -1);
	glTexCoord2f(width + ofsX, ofsY);
	glVertex3f(1, 1, -1);
}

void RenderScreenGlow(void)
{
	// check to see if (a) we can render it, and (b) we're meant to render it

	if (IEngineStudio.IsHardware() != 1)
		return;

	if ((int)glow_blur_steps->value == 0 || (int)glow_strength->value == 0)
		return;

	// enable some OpenGL stuff
	glEnable(GL_TEXTURE_RECTANGLE_NV);
	glColor3f(1, 1, 1);
	glDisable(GL_DEPTH_TEST);

	// STEP 1: Grab the screen and put it into a texture

	glBindTexture(GL_TEXTURE_RECTANGLE_NV, g_uiScreenTex);
	glCopyTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGB, 0, 0, ScreenWidth, ScreenHeight, 0);

	// STEP 2: Set up an orthogonal projection

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, 1, 1, 0, 0.1, 100);

	// STEP 3: Render the current scene to a new, lower-res texture, darkening non-bright areas of the scene
   // by multiplying it with itself a few times.

	glViewport(0, 0, ScreenWidth / 2, ScreenHeight / 2);

	glBindTexture(GL_TEXTURE_RECTANGLE_NV, g_uiScreenTex);

	glBlendFunc(GL_DST_COLOR, GL_ZERO);

	glDisable(GL_BLEND);

	glBegin(GL_QUADS);
	DrawQuad(ScreenWidth, ScreenHeight);
	glEnd();

	glEnable(GL_BLEND);

	glBegin(GL_QUADS);
	for (int i = 0; i < (int)glow_darken_steps->value; i++)
		DrawQuad(ScreenWidth, ScreenHeight);
	glEnd();

	glBindTexture(GL_TEXTURE_RECTANGLE_NV, g_uiGlowTex);
	glCopyTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGB, 0, 0, ScreenWidth / 2, ScreenHeight / 2, 0);


	// STEP 4: Blur the now darkened scene in the horizontal direction.

	float blurAlpha = 1 / (glow_blur_steps->value * 2 + 1);

	glColor4f(1, 1, 1, blurAlpha);

	glBlendFunc(GL_SRC_ALPHA, GL_ZERO);

	glBegin(GL_QUADS);
	DrawQuad(ScreenWidth / 2, ScreenHeight / 2);
	glEnd();

	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	glBegin(GL_QUADS);
	for (int i = 1; i <= (int)glow_blur_steps->value; i++)
	{
		DrawQuad(ScreenWidth / 2, ScreenHeight / 2, -i, 0);
		DrawQuad(ScreenWidth / 2, ScreenHeight / 2, i, 0);
	}
	glEnd();

	glCopyTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGB, 0, 0, ScreenWidth / 2, ScreenHeight / 2, 0);

	// STEP 5: Blur the horizontally blurred image in the vertical direction.

	glBlendFunc(GL_SRC_ALPHA, GL_ZERO);

	glBegin(GL_QUADS);
	DrawQuad(ScreenWidth / 2, ScreenHeight / 2);
	glEnd();

	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	glBegin(GL_QUADS);
	for (int i = 1; i <= (int)glow_blur_steps->value; i++)
	{
		DrawQuad(ScreenWidth / 2, ScreenHeight / 2, 0, -i);
		DrawQuad(ScreenWidth / 2, ScreenHeight / 2, 0, i);
	}
	glEnd();

	glCopyTexImage2D(GL_TEXTURE_RECTANGLE_NV, 0, GL_RGB, 0, 0, ScreenWidth / 2, ScreenHeight / 2, 0);

	// STEP 6: Combine the blur with the original image.

	glViewport(0, 0, ScreenWidth, ScreenHeight);

	glDisable(GL_BLEND);

	glBegin(GL_QUADS);
	DrawQuad(ScreenWidth / 2, ScreenHeight / 2);
	glEnd();

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	glBegin(GL_QUADS);
	for (int i = 1; i < (int)glow_strength->value; i++)
	{
		DrawQuad(ScreenWidth / 2, ScreenHeight / 2);
	}
	glEnd();

	glBindTexture(GL_TEXTURE_RECTANGLE_NV, g_uiScreenTex);

	glBegin(GL_QUADS);
	DrawQuad(ScreenWidth, ScreenHeight);
	glEnd();

	// STEP 7: Restore the original projection and modelview matrices and disable rectangular textures.

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glDisable(GL_TEXTURE_RECTANGLE_NV);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
}

/*
=================
HUD_DrawNormalTriangles

Non-transparent triangles-- add them here
=================
*/
void DLLEXPORT HUD_DrawNormalTriangles( void )
{
    g_WaterRenderer.Draw();

	gHUD.m_Spectator.DrawOverview();
	gFog.RenderFog();

	UpdateLaserSpot();

	gELightList.DrawNormal();
}

/*
=================
HUD_DrawTransparentTriangles

Render any triangles with transparent rendermode needs here
=================
*/
void DLLEXPORT HUD_DrawTransparentTriangles( void )
{
	SVD_DrawTransparentTriangles();

	UpdateLaserSpot();

    g_WaterRenderer.DrawTransparent();

	gFog.BlackFog();
}

// Draw Blood
void DrawBloodOverlay()
{
    if (gHUD.m_Health.m_iHealth < 30) {
        gEngfuncs.pTriAPI->RenderMode(kRenderTransAdd); //additive
        gEngfuncs.pTriAPI->Color4f(1, 1, 1, 1); //set 

        //calculate opacity
        float scale = (30 - gHUD.m_Health.m_iHealth) / 30.0f;
        if (gHUD.m_Health.m_iHealth != 0)
            gEngfuncs.pTriAPI->Brightness(scale);
        else
            gEngfuncs.pTriAPI->Brightness(1);

        //gEngfuncs.Con_Printf("scale :  %f health : %i", scale, gHUD.m_Health.m_iHealth);

        gEngfuncs.pTriAPI->SpriteTexture((struct model_s*)
        gEngfuncs.GetSpritePointer(SPR_Load("sprites/damagehud.spr")), 4);
        gEngfuncs.pTriAPI->CullFace(TRI_NONE); //no culling
        gEngfuncs.pTriAPI->Begin(TRI_QUADS); //start our quad

        //top left
        gEngfuncs.pTriAPI->TexCoord2f(0.0f, 1.0f);
        gEngfuncs.pTriAPI->Vertex3f(0, 0, 0);

        //bottom left
        gEngfuncs.pTriAPI->TexCoord2f(0.0f, 0.0f);
        gEngfuncs.pTriAPI->Vertex3f(0, ScreenHeight, 0);

        //bottom right
        gEngfuncs.pTriAPI->TexCoord2f(1.0f, 0.0f);
        gEngfuncs.pTriAPI->Vertex3f(ScreenWidth, ScreenHeight, 0);

        //top right
        gEngfuncs.pTriAPI->TexCoord2f(1.0f, 1.0f);
        gEngfuncs.pTriAPI->Vertex3f(ScreenWidth, 0, 0);

        gEngfuncs.pTriAPI->End(); //end our list of vertexes
        gEngfuncs.pTriAPI->RenderMode(kRenderNormal); //return to normal
    }

}

void HUD_DrawBloodOverlay(void)
{
    DrawBloodOverlay();
}