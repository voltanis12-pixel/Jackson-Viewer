/**
 * @file class1/deferred/shadowUtil.glsl
 *
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

uniform sampler2D normalMap;

#if defined(SUN_SHADOW)
uniform sampler2DShadow shadowMap0;
uniform sampler2DShadow shadowMap1;
uniform sampler2DShadow shadowMap2;
uniform sampler2DShadow shadowMap3;
#endif

#if defined(SPOT_SHADOW)
uniform sampler2DShadow shadowMap4;
uniform sampler2DShadow shadowMap5;
#endif

uniform vec3 sun_dir;
uniform vec3 moon_dir;
uniform vec2 shadow_res;
uniform vec2 proj_shadow_res;
uniform mat4 shadow_matrix[6];
uniform vec4 shadow_clip;
uniform float shadow_bias;
uniform float shadow_offset;
uniform float spot_shadow_bias;
uniform float spot_shadow_offset;
uniform mat4 inv_proj;
uniform vec2 screen_res;
uniform int sun_up_factor;


//===============================================================
// AAA RENDERER
// Directional Shadow PCF
//
// Stock Second Life uses a compact 5-position PCF pattern.
//
// We keep:
//   - the same number of texture samples
//   - the same weighting
//   - the same shadow bias
//   - the same overall darkness
//
// We only widen the outer sample positions slightly to soften
// jagged sun/moon shadow edges.
//===============================================================

float pcfShadow(
    sampler2DShadow shadowMap,
    vec3 norm,
    vec4 stc,
    float bias_mul,
    vec2 pos_screen,
    vec3 light_dir
)
{
#if defined(SUN_SHADOW)

    float offset =
        shadow_bias *
        bias_mul;


    stc.xyz /=
        stc.w;


    stc.z +=
        offset *
        2.0;


    //-----------------------------------------------------------
    // Existing chaotic jitter used by Second Life to disguise
    // shadow-map snapping.
    //-----------------------------------------------------------

    stc.x =
        floor(
            stc.x *
            shadow_res.x +
            fract(
                pos_screen.y *
                shadow_res.y
            )
        ) /
        shadow_res.x;


    //-----------------------------------------------------------
    // Center sample.
    //
    // Original weighting remains unchanged.
    //-----------------------------------------------------------

    float cs =
        texture(
            shadowMap,
            stc.xyz
        );


    float shadow =
        cs *
        4.0;


    //===========================================================
    // AAA RENDERER
    // Slightly Wider PCF Kernel
    //
    // STOCK:
    //   major offset = 1.50 texels
    //   minor offset = 0.50 texels
    //
    // AAA:
    //   major offset = 1.70 texels
    //   minor offset = 0.60 texels
    //
    // This is intentionally conservative.
    //===========================================================

    const float aaaMajor =
        1.70;

    const float aaaMinor =
        0.60;


    shadow +=
        texture(
            shadowMap,
            stc.xyz +
            vec3(
                 aaaMajor / shadow_res.x,
                 aaaMinor / shadow_res.y,
                 0.0
            )
        );


    shadow +=
        texture(
            shadowMap,
            stc.xyz +
            vec3(
                 aaaMinor / shadow_res.x,
                -aaaMajor / shadow_res.y,
                 0.0
            )
        );


    shadow +=
        texture(
            shadowMap,
            stc.xyz +
            vec3(
                -aaaMajor / shadow_res.x,
                -aaaMinor / shadow_res.y,
                 0.0
            )
        );


    shadow +=
        texture(
            shadowMap,
            stc.xyz +
            vec3(
                -aaaMinor / shadow_res.x,
                 aaaMajor / shadow_res.y,
                 0.0
            )
        );


    //-----------------------------------------------------------
    // Same total normalization as stock Second Life.
    //-----------------------------------------------------------

    return clamp(
        shadow *
        0.125,
        0.0,
        1.0
    );

#else

    return 1.0;

#endif
}


//===============================================================
// Spotlight Shadow PCF
//
// Left completely stock for this experiment.
//===============================================================

float pcfSpotShadow(
    sampler2DShadow shadowMap,
    vec4 stc,
    float bias_scale,
    vec2 pos_screen
)
{
#if defined(SPOT_SHADOW)

    stc.xyz /=
        stc.w;


    stc.z +=
        spot_shadow_bias *
        bias_scale;


    stc.x =
        floor(
            proj_shadow_res.x *
            stc.x +
            fract(
                pos_screen.y *
                0.666666666
            )
        ) /
        proj_shadow_res.x;


    float cs =
        texture(
            shadowMap,
            stc.xyz
        );


    float shadow =
        cs;


    vec2 off =
        1.0 /
        proj_shadow_res;


    off.y *=
        1.5;


    shadow +=
        texture(
            shadowMap,
            stc.xyz +
            vec3(
                off.x * 2.0,
                off.y,
                0.0
            )
        );


    shadow +=
        texture(
            shadowMap,
            stc.xyz +
            vec3(
                off.x,
                -off.y,
                0.0
            )
        );


    shadow +=
        texture(
            shadowMap,
            stc.xyz +
            vec3(
                -off.x,
                off.y,
                0.0
            )
        );


    shadow +=
        texture(
            shadowMap,
            stc.xyz +
            vec3(
                -off.x * 2.0,
                -off.y,
                0.0
            )
        );


    return
        shadow *
        0.2;

#else

    return 1.0;

#endif
}


//===============================================================
// Directional Shadow Sampling
//===============================================================

float sampleDirectionalShadow(
    vec3 pos,
    vec3 norm,
    vec2 pos_screen
)
{
#if defined(SUN_SHADOW)

    float shadow =
        0.0f;


    vec3 light_dir =
        normalize(
            (sun_up_factor == 1)
            ? sun_dir
            : moon_dir
        );


    float dp_directional_light =
        max(
            0.0,
            dot(
                norm.xyz,
                light_dir
            )
        );


    dp_directional_light =
        clamp(
            dp_directional_light,
            0.0,
            1.0
        );


    vec3 shadow_pos =
        pos.xyz;


    vec3 offset =
        light_dir.xyz *
        (
            1.0 -
            dp_directional_light
        );


    shadow_pos +=
        offset *
        shadow_offset *
        2.0;


    vec4 spos =
        vec4(
            shadow_pos.xyz,
            1.0
        );


    if (spos.z > -shadow_clip.w)
    {
        vec4 lpos;


        vec4 near_split =
            shadow_clip *
            -0.75;


        vec4 far_split =
            shadow_clip *
            -1.25;


        vec4 transition_domain =
            near_split -
            far_split;


        float weight =
            0.0;


        //-------------------------------------------------------
        // Cascade 3
        //-------------------------------------------------------

        if (spos.z < near_split.z)
        {
            lpos =
                shadow_matrix[3] *
                spos;


            float w =
                1.0;


            w -=
                max(
                    spos.z -
                    far_split.z,
                    0.0
                ) /
                transition_domain.z;


            float contrib =
                pcfShadow(
                    shadowMap3,
                    norm,
                    lpos,
                    1.0,
                    pos_screen,
                    light_dir
                ) *
                w;


            {
                shadow +=
                    contrib;

                weight +=
                    w;
            }


            shadow +=
                max(
                    (
                        pos.z +
                        shadow_clip.z
                    ) /
                    (
                        shadow_clip.z -
                        shadow_clip.w
                    ) *
                    2.0 -
                    1.0,
                    0.0
                );
        }


        //-------------------------------------------------------
        // Cascade 2
        //-------------------------------------------------------

        if (
            spos.z < near_split.y &&
            spos.z > far_split.z
        )
        {
            lpos =
                shadow_matrix[2] *
                spos;


            float w =
                1.0;


            w -=
                max(
                    spos.z -
                    far_split.y,
                    0.0
                ) /
                transition_domain.y;


            w -=
                max(
                    near_split.z -
                    spos.z,
                    0.0
                ) /
                transition_domain.z;


            float contrib =
                pcfShadow(
                    shadowMap2,
                    norm,
                    lpos,
                    1.0,
                    pos_screen,
                    light_dir
                ) *
                w;


            {
                shadow +=
                    contrib;

                weight +=
                    w;
            }
        }


        //-------------------------------------------------------
        // Cascade 1
        //-------------------------------------------------------

        if (
            spos.z < near_split.x &&
            spos.z > far_split.y
        )
        {
            lpos =
                shadow_matrix[1] *
                spos;


            float w =
                1.0;


            w -=
                max(
                    spos.z -
                    far_split.x,
                    0.0
                ) /
                transition_domain.x;


            w -=
                max(
                    near_split.y -
                    spos.z,
                    0.0
                ) /
                transition_domain.y;


            float contrib =
                pcfShadow(
                    shadowMap1,
                    norm,
                    lpos,
                    1.0,
                    pos_screen,
                    light_dir
                ) *
                w;


            {
                shadow +=
                    contrib;

                weight +=
                    w;
            }
        }


        //-------------------------------------------------------
        // Cascade 0
        //-------------------------------------------------------

        if (spos.z > far_split.x)
        {
            lpos =
                shadow_matrix[0] *
                spos;


            float w =
                1.0;


            w -=
                max(
                    near_split.x -
                    spos.z,
                    0.0
                ) /
                transition_domain.x;


            float contrib =
                pcfShadow(
                    shadowMap0,
                    norm,
                    lpos,
                    1.0,
                    pos_screen,
                    light_dir
                ) *
                w;


            {
                shadow +=
                    contrib;

                weight +=
                    w;
            }
        }


        shadow /=
            weight;
    }
    else
    {
        //-------------------------------------------------------
        // Lit beyond the far split.
        //-------------------------------------------------------

        return 1.0f;
    }


    return shadow;

#else

    return 1.0;

#endif
}


//===============================================================
// Spotlight Shadow Sampling
//===============================================================

float sampleSpotShadow(
    vec3 pos,
    vec3 norm,
    int index,
    vec2 pos_screen
)
{
#if defined(SPOT_SHADOW)

    float shadow =
        0.0f;


    pos +=
        norm *
        spot_shadow_offset;


    vec4 spos =
        vec4(
            pos,
            1.0
        );


    if (spos.z > -shadow_clip.w)
    {
        vec4 lpos;


        vec4 near_split =
            shadow_clip *
            -0.75;


        vec4 far_split =
            shadow_clip *
            -1.25;


        vec4 transition_domain =
            near_split -
            far_split;


        float weight =
            0.0;


        {
            float w =
                1.0;


            w -=
                max(
                    spos.z -
                    far_split.z,
                    0.0
                ) /
                transition_domain.z;


            if (index == 0)
            {
                lpos =
                    shadow_matrix[4] *
                    spos;


                shadow +=
                    pcfSpotShadow(
                        shadowMap4,
                        lpos,
                        0.8,
                        spos.xy
                    ) *
                    w;
            }
            else
            {
                lpos =
                    shadow_matrix[5] *
                    spos;


                shadow +=
                    pcfSpotShadow(
                        shadowMap5,
                        lpos,
                        0.8,
                        spos.xy
                    ) *
                    w;
            }


            weight +=
                w;


            shadow +=
                max(
                    (
                        pos.z +
                        shadow_clip.z
                    ) /
                    (
                        shadow_clip.z -
                        shadow_clip.w
                    ) *
                    2.0 -
                    1.0,
                    0.0
                );
        }


        shadow /=
            weight;
    }
    else
    {
        shadow =
            1.0f;
    }


    return shadow;

#else

    return 1.0;

#endif
}