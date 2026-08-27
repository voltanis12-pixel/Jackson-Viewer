/**
 * @file glowF.glsl
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

out vec4 frag_color;

uniform sampler2D diffuseMap;
uniform float glowStrength;

in vec4 vary_texcoord0;
in vec4 vary_texcoord1;
in vec4 vary_texcoord2;
in vec4 vary_texcoord3;


//===============================================================
// AAA RENDERER
// Cinematic Glow Blur
//
// Second Life already performs an 8-tap separable blur here.
//
// The original kernel totaled 5.10:
//
// 0.25  0.50  0.80  1.00
// 1.00  0.80  0.50  0.25
//
// Our kernel ALSO totals 5.10, so overall glow energy remains
// approximately unchanged.
//
// We redistribute a small amount of energy toward the outer
// samples to produce a softer, wider and more cinematic halo.
//===============================================================

void main()
{
    vec4 col =
        vec4(
            0.0,
            0.0,
            0.0,
            0.0
        );


    //-----------------------------------------------------------
    // AAA Renderer - Soft Wide Glow Kernel
    //
    // Total weight: 5.10
    //
    // This keeps approximately the same total bloom energy as
    // stock Second Life while slightly widening its apparent
    // radius.
    //-----------------------------------------------------------

    float kern[8];

    // ATI compiler falls down on array initialization.

    kern[0] = 0.32;
    kern[1] = 0.55;
    kern[2] = 0.78;
    kern[3] = 0.90;

    kern[4] = 0.90;
    kern[5] = 0.78;
    kern[6] = 0.55;
    kern[7] = 0.32;


    //-----------------------------------------------------------
    // First half of blur samples
    //-----------------------------------------------------------

    col +=
        kern[0] *
        texture(
            diffuseMap,
            vary_texcoord0.xy
        );

    col +=
        kern[1] *
        texture(
            diffuseMap,
            vary_texcoord1.xy
        );

    col +=
        kern[2] *
        texture(
            diffuseMap,
            vary_texcoord2.xy
        );

    col +=
        kern[3] *
        texture(
            diffuseMap,
            vary_texcoord3.xy
        );


    //-----------------------------------------------------------
    // Mirrored half of blur samples
    //-----------------------------------------------------------

    col +=
        kern[4] *
        texture(
            diffuseMap,
            vary_texcoord0.zw
        );

    col +=
        kern[5] *
        texture(
            diffuseMap,
            vary_texcoord1.zw
        );

    col +=
        kern[6] *
        texture(
            diffuseMap,
            vary_texcoord2.zw
        );

    col +=
        kern[7] *
        texture(
            diffuseMap,
            vary_texcoord3.zw
        );


    //-----------------------------------------------------------
    // Existing viewer glow-strength control remains intact.
    //-----------------------------------------------------------

    vec3 glowColor =
        col.rgb *
        glowStrength;


    //-----------------------------------------------------------
    // Prevent invalid negative values.
    //-----------------------------------------------------------

    glowColor =
        max(
            glowColor,
            vec3(0.0)
        );


    //-----------------------------------------------------------
    // Preserve alpha behavior from the original glow pipeline.
    //-----------------------------------------------------------

    frag_color =
        vec4(
            glowColor,
            max(
                col.a,
                0.0
            )
        );
}