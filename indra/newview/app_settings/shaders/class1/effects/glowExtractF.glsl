/**
 * @file glowExtractF.glsl
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

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

uniform sampler2D diffuseMap;

#if HAS_NOISE
uniform sampler2D glowNoiseMap;
uniform vec2 screen_res;
#endif

uniform float minLuminance;
uniform float maxExtractAlpha;
uniform vec3 lumWeights;
uniform vec3 warmthWeights;
uniform float warmthAmount;

in vec2 vary_texcoord0;


//===============================================================
// AAA Renderer - Cinematic Glow Extraction
//
// This pass determines which bright portions of the scene are
// allowed to contribute to the screen-space glow/bloom system.
//===============================================================

void main()
{
    //-----------------------------------------------------------
    // Original rendered scene color
    //-----------------------------------------------------------

    vec4 col =
        texture(
            diffuseMap,
            vary_texcoord0.xy
        );


    //-----------------------------------------------------------
    // Standard Second Life luminance extraction
    //-----------------------------------------------------------

    float luminance =
        dot(
            col.rgb,
            lumWeights
        );


    float lum =
        smoothstep(
            minLuminance,
            minLuminance + 1.0,
            luminance
        );


    //-----------------------------------------------------------
    // Warm-highlight extraction
    //
    // Preserves Second Life's existing ability to favor warm
    // bright sources such as lamps, fire and sunlight.
    //-----------------------------------------------------------

    float warmLuminance =
        max(
            col.r * warmthWeights.r,
            max(
                col.g * warmthWeights.g,
                col.b * warmthWeights.b
            )
        );


    float warmth =
        smoothstep(
            minLuminance,
            minLuminance + 1.0,
            warmLuminance
        );


    //-----------------------------------------------------------
    // Combine normal luminance and warmth extraction.
    //-----------------------------------------------------------

    float extractedGlow =
        mix(
            lum,
            warmth,
            warmthAmount
        );


    //===========================================================
    // AAA RENDERER
    // Soft cinematic bloom shoulder
    //
    // Standard extraction tends to transition rather abruptly
    // between non-glowing and glowing highlights.
    //
    // This curve retains the existing SL threshold while allowing
    // bright values immediately above that threshold to enter the
    // bloom buffer more smoothly.
    //
    // 1.00 = original response
    // 0.85 = slightly softer / richer bloom response
    //===========================================================

    extractedGlow =
        pow(
            clamp(
                extractedGlow,
                0.0,
                1.0
            ),
            0.85
        );


    //-----------------------------------------------------------
    // Slightly emphasize the hottest highlights.
    //
    // This is intentionally subtle so daylight scenes do not
    // develop a washed-out haze.
    //-----------------------------------------------------------

    float hotHighlight =
        extractedGlow *
        extractedGlow;


    extractedGlow =
        mix(
            extractedGlow,
            hotHighlight,
            0.08
        );


#if HAS_NOISE

    //-----------------------------------------------------------
    // Existing Second Life glow dithering
    //
    // Reduces visible banding in the reduced-precision glow
    // render target.
    //-----------------------------------------------------------

    float TRUE_NOISE_RES =
        128.0;


    vec3 glow_noise =
        texture(
            glowNoiseMap,
            vary_texcoord0.xy *
            (
                screen_res /
                TRUE_NOISE_RES
            )
        ).xyz;


    float NOISE_DEPTH =
        64.0;


    col.rgb +=
        glow_noise /
        NOISE_DEPTH;


    col.rgb =
        max(
            col.rgb,
            vec3(0.0)
        );

#endif


    //-----------------------------------------------------------
    // RGB remains the original scene color.
    //-----------------------------------------------------------

    frag_color.rgb =
        col.rgb;


    //-----------------------------------------------------------
    // Preserve explicit object glow stored in source alpha while
    // adding our improved luminance-based cinematic extraction.
    //-----------------------------------------------------------

    frag_color.a =
        max(
            col.a,
            extractedGlow *
            maxExtractAlpha
        );
}