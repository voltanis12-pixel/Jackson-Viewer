/**
 * @file postDeferredTonemap.glsl
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
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

uniform sampler2D exposureMap;
uniform vec2 screen_res;
in vec2 vary_fragcoord;


//===============================================================
// Tone mapping taken from Khronos sample implementation
//===============================================================

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACESInputMat = mat3
(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
);


// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat = mat3
(
    1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602
);


//===============================================================
// ACES Narkowicz
//===============================================================

vec3 toneMapACES_Narkowicz(vec3 color)
{
    const float A = 2.51;
    const float B = 0.03;
    const float C = 2.43;
    const float D = 0.59;
    const float E = 0.14;

    return clamp(
        (color * (A * color + B)) /
        (color * (C * color + D) + E),
        0.0,
        1.0
    );
}


//===============================================================
// ACES Hill
//===============================================================

vec3 RRTAndODTFit(vec3 color)
{
    vec3 a =
        color *
        (color + 0.0245786) -
        0.000090537;

    vec3 b =
        color *
        (0.983729 * color + 0.4329510) +
        0.238081;

    return a / b;
}


vec3 toneMapACES_Hill(vec3 color)
{
    color = ACESInputMat * color;

    color = RRTAndODTFit(color);

    color = ACESOutputMat * color;

    color = clamp(
        color,
        0.0,
        1.0
    );

    return color;
}


//===============================================================
// Khronos Neutral Tone Mapping
//===============================================================

vec3 PBRNeutralToneMapping(vec3 color)
{
    const float startCompression =
        0.8 - 0.04;

    const float desaturation =
        0.15;


    float x =
        min(
            color.r,
            min(
                color.g,
                color.b
            )
        );


    float offset =
        x < 0.08
        ? x - 6.25 * x * x
        : 0.04;


    color -= offset;


    float peak =
        max(
            color.r,
            max(
                color.g,
                color.b
            )
        );


    if (peak < startCompression)
    {
        return color;
    }


    const float d =
        1.0 - startCompression;


    float newPeak =
        1.0 -
        d * d /
        (
            peak +
            d -
            startCompression
        );


    color *=
        newPeak / peak;


    float g =
        1.0 -
        1.0 /
        (
            desaturation *
            (
                peak -
                newPeak
            ) +
            1.0
        );


    return mix(
        color,
        newPeak *
        vec3(
            1.0,
            1.0,
            1.0
        ),
        g
    );
}


uniform float exposure;
uniform float tonemap_mix;
uniform int tonemap_type;


//===============================================================
// AAA Renderer - Highlight Rolloff
//===============================================================

vec3 aaaHighlightRolloff(vec3 color)
{
    float peak =
        max(
            color.r,
            max(
                color.g,
                color.b
            )
        );


    const float shoulderStart =
        0.75;


    if (peak > shoulderStart)
    {
        float highlightAmount =
            peak -
            shoulderStart;


        float compressedHighlight =
            highlightAmount /
            (
                1.0 +
                1.35 *
                highlightAmount
            );


        float newPeak =
            shoulderStart +
            compressedHighlight;


        if (peak > 0.0001)
        {
            color *=
                newPeak /
                peak;
        }
    }


    return color;
}


//===============================================================
// AAA Renderer - Filmic Shadow Toe
//
// Preserves subtle detail in very dark areas while leaving
// midtones and highlights essentially untouched.
//===============================================================

vec3 aaaShadowToe(vec3 color)
{
    //-----------------------------------------------------------
    // Determine perceptual brightness of the current pixel.
    //-----------------------------------------------------------

    float luma =
        dot(
            color,
            vec3(
                0.2126,
                0.7152,
                0.0722
            )
        );


    //-----------------------------------------------------------
    // Affect primarily the darkest ~20% of the image.
    //
    // At black:       strongest effect
    // Around 0.22:    no effect
    //-----------------------------------------------------------

    float shadowMask =
        1.0 -
        smoothstep(
            0.02,
            0.22,
            luma
        );


    //-----------------------------------------------------------
    // Very small lift.
    //
    // 0.012 = enough to retain texture/detail without turning
    // dark scenes gray.
    //-----------------------------------------------------------

    float shadowLift =
        shadowMask *
        0.012;


    color +=
        vec3(
            shadowLift
        );


    return color;
}


//===============================================================
// Main Tone Mapping
//===============================================================

vec3 toneMap(vec3 color)
{
#ifndef NO_POST

    vec3 linear_input_color =
        color;


    float exp_scale =
        texture(
            exposureMap,
            vec2(
                0.5,
                0.5
            )
        ).r;


    float final_exposure =
        exposure *
        exp_scale;


    vec3 exposed_color =
        color *
        final_exposure;


    //-----------------------------------------------------------
    // Standard Second Life tone mapping
    //-----------------------------------------------------------

    vec3 tonemapped_color =
        exposed_color;


    switch (tonemap_type)
    {
        case 0:

            tonemapped_color =
                PBRNeutralToneMapping(
                    exposed_color
                );

            break;


        case 1:

            tonemapped_color =
                toneMapACES_Hill(
                    exposed_color
                );

            break;
    }


    vec3 exposed_linear_input =
        linear_input_color *
        final_exposure;


    color =
        mix(
            exposed_linear_input,
            tonemapped_color,
            tonemap_mix
        );


    //===========================================================
    // AAA RENDERER
    // Cinematic image enhancement
    //===========================================================


    //-----------------------------------------------------------
    // Cinematic Contrast
    //
    // 1.00 = original
    // 1.10 = stronger cinematic contrast
    //-----------------------------------------------------------

    color =
        (
            color -
            vec3(0.5)
        ) *
        1.10 +
        vec3(0.5);


    //-----------------------------------------------------------
    // Perceptual luminance
    //-----------------------------------------------------------

    float aaa_luma =
        dot(
            color,
            vec3(
                0.2126,
                0.7152,
                0.0722
            )
        );


    //-----------------------------------------------------------
    // Saturation
    //
    // 1.00 = original
    // 1.08 = richer colors
    //-----------------------------------------------------------

    color =
        mix(
            vec3(
                aaa_luma
            ),
            color,
            1.08
        );


    //-----------------------------------------------------------
    // Filmic Highlight Rolloff
    //-----------------------------------------------------------

    color =
        aaaHighlightRolloff(
            color
        );


    //-----------------------------------------------------------
    // Filmic Shadow Toe
    //
    // Restores a small amount of detail to the deepest shadows.
    //-----------------------------------------------------------

    color =
        aaaShadowToe(
            color
        );


    //-----------------------------------------------------------
    // Final legal display range
    //-----------------------------------------------------------

    color =
        clamp(
            color,
            0.0,
            1.0
        );


#else

    color *=
        exposure *
        texture(
            exposureMap,
            vec2(
                0.5,
                0.5
            )
        ).r;


    color =
        clamp(
            color,
            0.0,
            1.0
        );

#endif

    return color;
}


//===============================================================
// Tone Mapping Without Exposure
//===============================================================

vec3 toneMapNoExposure(vec3 color)
{
#ifndef NO_POST

    vec3 linear_input_color =
        color;


    vec3 tonemapped_color =
        color;


    switch (tonemap_type)
    {
        case 0:

            tonemapped_color =
                PBRNeutralToneMapping(
                    color
                );

            break;


        case 1:

            tonemapped_color =
                toneMapACES_Hill(
                    color
                );

            break;
    }


    color =
        mix(
            linear_input_color,
            tonemapped_color,
            tonemap_mix
        );


    color =
        clamp(
            color,
            0.0,
            1.0
        );


#else

    color =
        clamp(
            color,
            0.0,
            1.0
        );

#endif

    return color;
}


//===============================================================
// Exposure Debug Visualization
//===============================================================

void debugExposure(inout vec3 color)
{
    float exp_scale =
        texture(
            exposureMap,
            vec2(
                0.5,
                0.5
            )
        ).r;


    exp_scale *=
        0.5;


    if
    (
        abs(
            vary_fragcoord.y -
            exp_scale
        ) <
        0.01
        &&
        vary_fragcoord.x <
        0.1
    )
    {
        color =
            vec3(
                1.0,
                0.0,
                0.0
            );
    }
}