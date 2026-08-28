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

out vec4 frag_color;

uniform sampler2D diffuseRect;

in vec2 vary_fragcoord;

#ifdef GAMMA_CORRECT
uniform float gamma;
#endif

vec3 linear_to_srgb(vec3 cl);
vec3 toneMap(vec3 color);

vec3 clampHDRRange(vec3 color);

float aaaInterleavedGradientNoise(vec2 pixelPosition)
{
    return fract(
        52.9829189 *
        fract(
            dot(
                pixelPosition,
                vec2(
                    0.06711056,
                    0.00583715
                )
            )
        )
    );
}

#ifdef GAMMA_CORRECT
vec3 legacyGamma(vec3 color)
{
    vec3 c = 1. - clamp(color, vec3(0.), vec3(1.));
    c = 1. - pow(c, vec3(gamma)); // s/b inverted already CPU-side

    return c;
}
#endif

void main()
{
    //this is the one of the rare spots where diffuseRect contains linear color values (not sRGB)
    vec4 diff = texture(diffuseRect, vary_fragcoord);

#ifndef NO_POST
    diff.rgb = toneMap(diff.rgb);
#else
    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0));
#endif

#ifdef GAMMA_CORRECT
    diff.rgb = linear_to_srgb(diff.rgb);

#ifdef LEGACY_GAMMA
    diff.rgb = legacyGamma(diff.rgb);
#endif

#endif

    // AAA: apply sub-pixel output dithering to reduce visible
    // 8-bit banding in skies, fog, shadows and smooth gradients.
    float aaaDither =
        aaaInterleavedGradientNoise(gl_FragCoord.xy) -
        0.5;

    diff.rgb +=
        vec3(
            aaaDither *
            (1.0 / 255.0)
        );

    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0)); // We should always be 0-1 past this point

    //debugExposure(diff.rgb);
    frag_color = diff;
}/**
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

out vec4 frag_color;

uniform sampler2D diffuseRect;

in vec2 vary_fragcoord;

#ifdef GAMMA_CORRECT
uniform float gamma;
#endif

vec3 linear_to_srgb(vec3 cl);
vec3 toneMap(vec3 color);

vec3 clampHDRRange(vec3 color);

float aaaInterleavedGradientNoise(vec2 pixelPosition)
{
    return fract(
        52.9829189 *
        fract(
            dot(
                pixelPosition,
                vec2(
                    0.06711056,
                    0.00583715
                )
            )
        )
    );
}

#ifdef GAMMA_CORRECT
vec3 legacyGamma(vec3 color)
{
    vec3 c = 1. - clamp(color, vec3(0.), vec3(1.));
    c = 1. - pow(c, vec3(gamma)); // s/b inverted already CPU-side

    return c;
}
#endif

void main()
{
    //this is the one of the rare spots where diffuseRect contains linear color values (not sRGB)
    vec4 diff = texture(diffuseRect, vary_fragcoord);

#ifndef NO_POST
    diff.rgb = toneMap(diff.rgb);
#else
    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0));
#endif

#ifdef GAMMA_CORRECT
    diff.rgb = linear_to_srgb(diff.rgb);

#ifdef LEGACY_GAMMA
    diff.rgb = legacyGamma(diff.rgb);
#endif

#endif

    // AAA: apply sub-pixel output dithering to reduce visible
    // 8-bit banding in skies, fog, shadows and smooth gradients.
    float aaaDither =
        aaaInterleavedGradientNoise(gl_FragCoord.xy) -
        0.5;

    diff.rgb +=
        vec3(
            aaaDither *
            (1.0 / 255.0)
        );

    diff.rgb = clamp(diff.rgb, vec3(0.0), vec3(1.0)); // We should always be 0-1 past this point

    //debugExposure(diff.rgb);
    frag_color = diff;
}