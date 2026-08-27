/**
 * @file class2/deferred/sunLightSSAOF.glsl
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

// class 2 -- shadows and SSAO

// Inputs
in vec2 vary_fragcoord;

vec4 getPosition(vec2 pos_screen);
vec4 getNorm(vec2 pos_screen);

float sampleDirectionalShadow(vec3 shadow_pos, vec3 norm, vec2 pos_screen);
float sampleSpotShadow(vec3 shadow_pos, vec3 norm, int index, vec2 pos_screen);
float calcAmbientOcclusion(vec4 pos, vec3 norm, vec2 pos_screen);


//===============================================================
// AAA RENDERER
// Cinematic SSAO shaping
//
// This pass packs:
//   R = directional shadow
//   G = ambient occlusion
//   B = spotlight shadow 0
//   A = spotlight shadow 1
//
// We are only reshaping the AO channel here.
//===============================================================

void main()
{
    vec2 pos_screen = vary_fragcoord.xy;
    vec4 pos  = getPosition(pos_screen);
    vec4 norm = getNorm(pos_screen);

    vec4 col;

    //-----------------------------------------------------------
    // Preserve existing shadow behavior
    //-----------------------------------------------------------

    col.r = sampleDirectionalShadow(pos.xyz, norm.xyz, pos_screen);
    col.b = sampleSpotShadow(pos.xyz, norm.xyz, 0, pos_screen);
    col.a = sampleSpotShadow(pos.xyz, norm.xyz, 1, pos_screen);


    //-----------------------------------------------------------
    // Original ambient occlusion result
    //-----------------------------------------------------------

    float ao = calcAmbientOcclusion(pos, norm.xyz, pos_screen);


    //-----------------------------------------------------------
    // AAA Renderer - Contact Shadow Shaping
    //
    // Assumes AO is in the standard 0..1 range, where:
    //   1.0 = unoccluded
    //   0.0 = fully occluded
    //
    // pow(ao, 1.18) slightly deepens mid occlusion values,
    // strengthening contact shadows where surfaces meet.
    //
    // The mix keeps the effect subtle and avoids over-darkening.
    //-----------------------------------------------------------

    float shapedAO =
        pow(
            clamp(ao, 0.0, 1.0),
            1.18
        );

    ao =
        mix(
            ao,
            shapedAO,
            0.55
        );


    //-----------------------------------------------------------
    // Small floor to avoid excessively crushed occlusion.
    //-----------------------------------------------------------

    ao =
        max(
            ao,
            0.03
        );


    //-----------------------------------------------------------
    // Store reshaped AO in the green channel
    //-----------------------------------------------------------

    col.g = ao;


    //-----------------------------------------------------------
    // Final packed output
    //-----------------------------------------------------------

    frag_color =
        clamp(
            col,
            vec4(0.0),
            vec4(1.0)
        );
}