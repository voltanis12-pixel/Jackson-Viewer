/**
 * @file class3\deferred\multiPointLightF.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

uniform sampler2D lightFunc;

uniform vec3 env_mat[3];
uniform float sun_wash;

uniform int light_count;


//===============================================================
// Point light arrays
//
// light[].xyz = light position
// light[].w   = light radius / size
//
// light_col[].rgb = linear light color
// light_col[].a   = falloff
//===============================================================

uniform vec4 light[LIGHT_COUNT];
uniform vec4 light_col[LIGHT_COUNT];


uniform vec2 screen_res;
uniform float far_z;
uniform mat4 inv_proj;
uniform int classic_mode;


in vec4 vary_fragcoord;


//===============================================================
// Existing viewer functions
//===============================================================

void calcHalfVectors(
    vec3 lv,
    vec3 n,
    vec3 v,
    out vec3 h,
    out vec3 l,
    out float nh,
    out float nl,
    out float nv,
    out float vh,
    out float lightDist
);


float calcLegacyDistanceAttenuation(
    float distance,
    float falloff
);


vec4 getPosition(
    vec2 pos_screen
);


vec4 getNorm(
    vec2 screenpos
);


vec2 getScreenXY(
    vec4 clip
);


vec2 getScreenCoord(
    vec4 clip
);


vec3 srgb_to_linear(
    vec3 c
);


// Util
vec3 hue_to_rgb(
    float hue
);


void pbrPunctual(
    vec3 diffuseColor,
    vec3 specularColor,
    float perceptualRoughness,
    float metallic,
    vec3 n,
    vec3 v,
    vec3 l,
    out float nl,
    out vec3 diff,
    out vec3 spec
);


GBufferInfo getGBuffer(
    vec2 screenpos
);


//===============================================================
// AAA RENDERER
// Multi-Point Light Radius Feather
//
// Matches the already-tested single point-light behavior.
//
// 0.00 - 0.82 radius:
//     completely stock attenuation
//
// 0.82 - 1.00 radius:
//     smoothly fades toward zero
//
// > 1.00 radius:
//     excluded exactly as before
//
// No light-color, BRDF, PBR intensity, radius or specular values
// are changed.
//===============================================================

float aaaPointLightEdgeFade(
    float normalizedDistance
)
{
    const float fadeStart =
        0.82;


    float edgeFade =
        1.0 -
        smoothstep(
            fadeStart,
            1.0,
            normalizedDistance
        );


    return clamp(
        edgeFade,
        0.0,
        1.0
    );
}


//===============================================================
// Main
//===============================================================

void main()
{
    vec3 final_color =
        vec3(
            0.0,
            0.0,
            0.0
        );


    vec2 tc =
        getScreenCoord(
            vary_fragcoord
        );


    vec3 pos =
        getPosition(
            tc
        ).xyz;


    //-----------------------------------------------------------
    // Preserve existing far-plane behavior.
    //-----------------------------------------------------------

    if (pos.z < far_z)
    {
        discard;
    }


    GBufferInfo gb =
        getGBuffer(
            tc
        );


    vec3 n =
        gb.normal;


    vec4 spec =
        gb.specular;


    vec3 diffuse =
        gb.albedo.rgb;


    vec3 h;

    vec3 l;

    vec3 v =
        -normalize(
            pos
        );


    float nh;
    float nv;
    float vh;
    float lightDist;


    //===========================================================
    // PBR MATERIALS
    //===========================================================

    if
    (
        GET_GBUFFER_FLAG(
            gb.gbufferFlag,
            GBUFFER_FLAG_HAS_PBR
        )
    )
    {
        vec3 colorEmissive =
            gb.emissive.rgb;


        vec3 orm =
            spec.rgb;


        float perceptualRoughness =
            orm.g;


        float metallic =
            orm.b;


        vec3 f0 =
            vec3(
                0.04
            );


        vec3 baseColor =
            diffuse.rgb;


        vec3 diffuseColor =
            baseColor.rgb *
            (
                vec3(
                    1.0
                ) -
                f0
            );


        diffuseColor *=
            1.0 -
            metallic;


        vec3 specularColor =
            mix(
                f0,
                baseColor.rgb,
                metallic
            );


        //-------------------------------------------------------
        // Process each active point light.
        //-------------------------------------------------------

        for
        (
            int light_idx = 0;
            light_idx < LIGHT_COUNT;
            ++light_idx
        )
        {
            vec3 lightColor =
                light_col[
                    light_idx
                ].rgb;


            float falloff =
                light_col[
                    light_idx
                ].a;


            float lightSize =
                light[
                    light_idx
                ].w;


            vec3 lv =
                light[
                    light_idx
                ].xyz -
                pos;


            lightDist =
                length(
                    lv
                );


            float dist =
                lightDist /
                lightSize;


            if (dist <= 1.0)
            {
                lv /=
                    lightDist;


                //---------------------------------------------------
                // Stock Second Life attenuation.
                //---------------------------------------------------

                float dist_atten =
                    calcLegacyDistanceAttenuation(
                        dist,
                        falloff
                    );


                //===============================================
                // AAA RENDERER
                // Same outer-radius feather as pointLightF.glsl
                //===============================================

                dist_atten *=
                    aaaPointLightEdgeFade(
                        dist
                    );


                //---------------------------------------------------
                // Preserve the existing PBR intensity multiplier.
                //---------------------------------------------------

                vec3 intensity =
                    dist_atten *
                    lightColor *
                    3.25;


                float nl =
                    0.0;


                vec3 diff =
                    vec3(
                        0.0
                    );


                vec3 specPunc =
                    vec3(
                        0.0
                    );


                pbrPunctual(
                    diffuseColor,
                    specularColor,
                    perceptualRoughness,
                    metallic,
                    n.xyz,
                    v,
                    lv,
                    nl,
                    diff,
                    specPunc
                );


                final_color +=
                    intensity *
                    clamp(
                        nl *
                        (
                            diff +
                            specPunc
                        ),
                        vec3(
                            0.0
                        ),
                        vec3(
                            10.0
                        )
                    );
            }
        }
    }


    //===========================================================
    // LEGACY MATERIALS
    //===========================================================

    else
    {
        diffuse =
            srgb_to_linear(
                diffuse
            );


        spec.rgb =
            srgb_to_linear(
                spec.rgb
            );


        //-------------------------------------------------------
        // As of OSX 10.6.7 ATI Apple's crash when using a
        // variable-size loop.
        //-------------------------------------------------------

        for
        (
            int i = 0;
            i < LIGHT_COUNT;
            ++i
        )
        {
            vec3 lv =
                light[
                    i
                ].xyz -
                pos;


            float dist =
                length(
                    lv
                );


            dist /=
                light[
                    i
                ].w;


            if (dist <= 1.0)
            {
                float nl =
                    dot(
                        n,
                        lv
                    );


                if (nl > 0.0)
                {
                    float lightDist;


                    calcHalfVectors(
                        lv,
                        n,
                        v,
                        h,
                        l,
                        nh,
                        nl,
                        nv,
                        vh,
                        lightDist
                    );


                    float fa =
                        light_col[
                            i
                        ].a;


                    //---------------------------------------------------
                    // Stock legacy attenuation.
                    //---------------------------------------------------

                    float dist_atten =
                        calcLegacyDistanceAttenuation(
                            dist,
                            fa
                        );


                    //===============================================
                    // AAA RENDERER
                    // Matching outer-radius feather.
                    //===============================================

                    dist_atten *=
                        aaaPointLightEdgeFade(
                            dist
                        );


                    float lit =
                        nl *
                        dist_atten;


                    vec3 col =
                        light_col[
                            i
                        ].rgb *
                        lit *
                        diffuse;


                    //---------------------------------------------------
                    // Existing legacy specular calculation.
                    //---------------------------------------------------

                    if (spec.a > 0.0)
                    {
                        lit =
                            min(
                                nl *
                                6.0,
                                1.0
                            ) *
                            dist_atten;


                        float fres =
                            pow(
                                1.0 -
                                vh,
                                5.0
                            ) *
                            0.4 +
                            0.5;


                        float gtdenom =
                            2.0 *
                            nh;


                        float gt =
                            max(
                                0.0,
                                min(
                                    gtdenom *
                                    nv /
                                    vh,
                                    gtdenom *
                                    nl /
                                    vh
                                )
                            );


                        if (nh > 0.0)
                        {
                            float scol =
                                fres *
                                texture(
                                    lightFunc,
                                    vec2(
                                        nh,
                                        spec.a
                                    )
                                ).r *
                                gt /
                                (
                                    nh *
                                    nl
                                );


                            col +=
                                lit *
                                scol *
                                light_col[
                                    i
                                ].rgb *
                                spec.rgb;
                        }
                    }


                    final_color +=
                        col;
                }
            }
        }
    }


    //===========================================================
    // Existing final scaling
    //===========================================================

    float final_scale =
        1.0;


    if (classic_mode > 0)
    {
        final_scale =
            0.9;
    }


    frag_color.rgb =
        max(
            final_color *
            final_scale,
            vec3(
                0.0
            )
        );


    frag_color.a =
        0.0;


#ifdef IS_AMD_CARD

    //-----------------------------------------------------------
    // Preserve the original AMD workaround.
    //
    // Ensure the GLSL compiler sees static references to the
    // arrays so it does not optimize their storage away.
    //-----------------------------------------------------------

    vec4 dummy1 =
        light[
            0
        ];


    vec4 dummy2 =
        light_col[
            0
        ];


    vec4 dummy3 =
        light[
            LIGHT_COUNT -
            1
        ];


    vec4 dummy4 =
        light_col[
            LIGHT_COUNT -
            1
        ];

#endif
}