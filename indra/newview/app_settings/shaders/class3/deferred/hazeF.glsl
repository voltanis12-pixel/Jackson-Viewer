/**
 * @file class3/deferred/hazeF.glsl
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
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

out vec4 frag_color;


//===============================================================
// Inputs
//===============================================================

uniform vec3 sun_dir;
uniform vec3 moon_dir;
uniform int sun_up_factor;

in vec2 vary_fragcoord;


vec4 getNorm(vec2 pos_screen);

vec4 getPositionWithDepth(
    vec2 pos_screen,
    float depth
);

void calcAtmosphericVarsLinear(
    vec3 inPositionEye,
    vec3 norm,
    vec3 light_dir,
    out vec3 sunlit,
    out vec3 amblit,
    out vec3 atten,
    out vec3 additive
);


float getDepth(
    vec2 pos_screen
);


vec3 linear_to_srgb(
    vec3 c
);

vec3 srgb_to_linear(
    vec3 c
);


uniform vec4 waterPlane;

uniform int cube_snapshot;

uniform float sky_hdr_scale;


//===============================================================
// AAA RENDERER
// Distance Mask
//
// Nearby geometry remains completely stock.
//
// Atmospheric enhancement begins around 64 meters and gradually
// reaches full strength around 320 meters.
//===============================================================

float aaaAtmosphericDistanceMask(
    vec3 eyePosition
)
{
    float distanceFromCamera =
        length(
            eyePosition
        );


    return smoothstep(
        64.0,
        320.0,
        distanceFromCamera
    );
}


//===============================================================
// AAA RENDERER
// Atmospheric Scattering Boost
//
// 1.00 = stock
// 1.10 = maximum distant enhancement
//===============================================================

float aaaAtmosphericScatteringBoost(
    float distanceMask
)
{
    return mix(
        1.0,
        1.10,
        distanceMask
    );
}


//===============================================================
// AAA RENDERER
// Distance Attenuation Enhancement
//
// Atmospheric alpha here represents transmittance.
//
// Values closer to:
//     1.0 = clearer air
//     0.0 = stronger atmospheric attenuation
//
// We apply only a very mild nonlinear adjustment at distance.
//===============================================================

float aaaAtmosphericAttenuation(
    float attenuation,
    float distanceMask
)
{
    attenuation =
        clamp(
            attenuation,
            0.0,
            1.0
        );


    //-----------------------------------------------------------
    // Near camera:
    // exponent = 1.00
    //
    // Far distance:
    // exponent = 1.14
    //
    // Raising a 0..1 value to an exponent above 1 slightly
    // reduces transmittance, creating stronger atmospheric depth.
    //-----------------------------------------------------------

    float exponent =
        mix(
            1.0,
            1.14,
            distanceMask
        );


    return pow(
        attenuation,
        exponent
    );
}


//===============================================================
// Main
//===============================================================

void main()
{
    vec2 tc =
        vary_fragcoord.xy;


    float depth =
        getDepth(
            tc.xy
        );


    vec4 pos =
        getPositionWithDepth(
            tc,
            depth
        );


    vec4 norm =
        getNorm(
            tc
        );


    vec3 light_dir =
        (sun_up_factor == 1)
        ? sun_dir
        : moon_dir;


    vec3 color =
        vec3(
            0.0
        );


    float bloom =
        0.0;


    vec3 sunlit;
    vec3 amblit;
    vec3 additive;
    vec3 atten;


    //-----------------------------------------------------------
    // Standard Second Life atmospheric calculation
    //-----------------------------------------------------------

    calcAtmosphericVarsLinear(
        pos.xyz,
        norm.xyz,
        light_dir,
        sunlit,
        amblit,
        additive,
        atten
    );


    //-----------------------------------------------------------
    // Atmospheric mask relative to water plane
    //-----------------------------------------------------------

    bool do_atmospherics =
        false;


    if
    (
        dot(
            vec3(
                0.0
            ),
            waterPlane.xyz
        ) +
        waterPlane.w >
        0.0

        ||

        dot(
            pos.xyz,
            waterPlane.xyz
        ) +
        waterPlane.w >
        0.0
    )
    {
        do_atmospherics =
            true;
    }


    vec3 irradiance =
        vec3(
            0.0
        );


    vec3 radiance =
        vec3(
            0.0
        );


    //-----------------------------------------------------------
    // Sky, clouds, sun/moon and stars are handled elsewhere
    //-----------------------------------------------------------

    if (depth >= 1.0)
    {
        discard;
    }


    float alpha =
        0.0;


    if (do_atmospherics)
    {
        //-------------------------------------------------------
        // Determine distance enhancement.
        //-------------------------------------------------------

        float distanceMask =
            aaaAtmosphericDistanceMask(
                pos.xyz
            );


        //-------------------------------------------------------
        // Stock atmospheric attenuation.
        //-------------------------------------------------------

        alpha =
            atten.r;


        //-------------------------------------------------------
        // Stock atmospheric scattering.
        //-------------------------------------------------------

        color =
            srgb_to_linear(
                additive *
                2.0
            );


        color *=
            sky_hdr_scale;


        //=======================================================
        // AAA RENDERER
        // Stronger distant atmospheric separation
        //=======================================================

        color *=
            aaaAtmosphericScatteringBoost(
                distanceMask
            );


        //-------------------------------------------------------
        // Slightly increase long-distance attenuation.
        //-------------------------------------------------------

        alpha =
            aaaAtmosphericAttenuation(
                alpha,
                distanceMask
            );
    }
    else
    {
        color =
            vec3(
                0.0,
                0.0,
                0.0
            );


        alpha =
            1.0;
    }


    //-----------------------------------------------------------
    // Output remains linear.
    //-----------------------------------------------------------

    frag_color =
        max(
            vec4(
                color.rgb,
                alpha
            ),
            vec4(
                0.0
            )
        );
}