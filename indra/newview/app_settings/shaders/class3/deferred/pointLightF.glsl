/**
 * @file class3\deferred\pointLightF.glsl
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


//===============================================================
// Light Parameters
//===============================================================

uniform vec3 color;
uniform float falloff;
uniform float size;


in vec4 vary_fragcoord;
in vec3 trans_center;


uniform vec2 screen_res;

uniform mat4 inv_proj;
uniform vec4 viewport;
uniform int classic_mode;


//===============================================================
// Existing Viewer Functions
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


vec4 getNorm(
    vec2 screenpos
);


vec4 getPosition(
    vec2 pos_screen
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


float getDepth(
    vec2 tc
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
// Point-Light Radius Feather
//
// This does NOT change:
//
//   light color
//   light radius
//   center brightness
//   PBR BRDF
//   specular calculation
//   legacy attenuation curve
//
// It only softens the final portion of the radius.
//
// 0.00 - 0.82 radius:
//     completely unchanged
//
// 0.82 - 1.00 radius:
//     progressively fades toward zero
//
// This should make lamps, torches and magical point lights end
// more naturally instead of producing an obvious outer boundary.
//===============================================================

float aaaPointLightEdgeFade(
    float normalizedDistance
)
{
    //-----------------------------------------------------------
    // Full intensity through most of the light radius.
    //-----------------------------------------------------------

    const float fadeStart =
        0.82;


    //-----------------------------------------------------------
    // smoothstep returns:
    //
    // 0 at fadeStart
    // 1 at radius edge
    //
    // Invert it so the light fades OUT.
    //-----------------------------------------------------------

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


    GBufferInfo gb =
        getGBuffer(
            tc
        );


    vec3 n =
        gb.normal;


    vec3 diffuse =
        gb.albedo.rgb;


    vec4 spec =
        gb.specular;


    //===========================================================
    // Common half-vector calculations
    //===========================================================

    vec3 lv =
        trans_center.xyz -
        pos;


    vec3 h;

    vec3 l;

    vec3 v =
        -normalize(
            pos
        );


    float nh;
    float nl;
    float nv;
    float vh;
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


    //-----------------------------------------------------------
    // Preserve the existing light radius exactly.
    //-----------------------------------------------------------

    if (lightDist >= size)
    {
        discard;
    }


    //-----------------------------------------------------------
    // Normalized light distance.
    //-----------------------------------------------------------

    float dist =
        lightDist /
        size;


    //-----------------------------------------------------------
    // Original Second Life distance attenuation.
    //-----------------------------------------------------------

    float dist_atten =
        calcLegacyDistanceAttenuation(
            dist,
            falloff
        );


    //===========================================================
    // AAA RENDERER
    // Gentle outer-radius feather.
    //
    // The original attenuation remains intact.
    // We only soften the very outside of the light volume.
    //===========================================================

    float aaaEdgeFade =
        aaaPointLightEdgeFade(
            dist
        );


    dist_atten *=
        aaaEdgeFade;


    //===========================================================
    // PBR Materials
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
        // Preserve existing viewer light intensity exactly.
        //-------------------------------------------------------

        vec3 intensity =
            dist_atten *
            color *
            3.25;


        float nl =
            0.0;


        vec3 diffPunc =
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
            normalize(
                lv
            ),
            nl,
            diffPunc,
            specPunc
        );


        final_color +=
            intensity *
            clamp(
                nl *
                (
                    diffPunc +
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


    //===========================================================
    // Legacy Materials
    //===========================================================

    else
    {
        if (nl < 0.0)
        {
            discard;
        }


        diffuse =
            srgb_to_linear(
                diffuse
            );


        spec.rgb =
            srgb_to_linear(
                spec.rgb
            );


        float lit =
            nl *
            dist_atten;


        final_color =
            color.rgb *
            lit *
            diffuse;


        //-------------------------------------------------------
        // Legacy specular lighting.
        //-------------------------------------------------------

        if (spec.a > 0.0)
        {
            lit =
                min(
                    nl *
                    6.0,
                    1.0
                ) *
                dist_atten;


            float sa =
                nh;


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


                final_color +=
                    lit *
                    scol *
                    color.rgb *
                    spec.rgb;
            }
        }


        //-------------------------------------------------------
        // Preserve existing discard behavior.
        //-------------------------------------------------------

        if
        (
            dot(
                final_color,
                final_color
            ) <=
            0.0
        )
        {
            discard;
        }
    }


    //===========================================================
    // Existing Viewer Final Scale
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
}