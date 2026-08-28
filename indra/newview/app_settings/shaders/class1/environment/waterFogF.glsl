/**
 * @file class1\environment\waterFogF.glsl
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



uniform vec4 waterPlane;
uniform vec4 waterFogColor;
uniform float waterFogDensity;
uniform float waterFogKS;

vec3 srgb_to_linear(vec3 col);
vec3 linear_to_srgb(vec3 col);

vec3 atmosFragLighting(vec3 light, vec3 additive, vec3 atten);


//===============================================================
// AAA RENDERER
// Underwater Visibility Preservation
//
// Keep the region's authored waterFogColor, waterFogDensity, and
// waterFogKS exactly as they are.
//
// The stock fog calculation is preserved. We only recover a small
// amount of transmittance at medium and long viewing distances so
// underwater scenes do not collapse into near-black too quickly.
//
// 0 - 8 m:
//     completely stock
//
// 8 - 80 m:
//     gradual transition
//
// 80 m+:
//     maximum transmittance exponent = 0.90
//
// Because values in the 0..1 range become slightly larger when
// raised to an exponent below 1.0, distant scene detail remains
// more readable without changing the authored fog color.
//===============================================================

float aaaPreserveUnderwaterTransmittance(
    float stockTransmittance,
    float physicalDistance
)
{
    stockTransmittance =
        clamp(
            stockTransmittance,
            0.0,
            1.0
        );

    float distanceMask =
        smoothstep(
            8.0,
            80.0,
            physicalDistance
        );

    float exponent =
        mix(
            1.0,
            0.90,
            distanceMask
        );

    return pow(
        stockTransmittance,
        exponent
    );
}


// get a water fog color that will apply the appropriate haze to a color given
// a blend function of (ONE, SOURCE_ALPHA)
vec4 getWaterFogViewNoClip(vec3 pos)
{
    vec3 view = normalize(pos);
    //normalize view vector
    float es = -(dot(view, waterPlane.xyz));

    //find intersection point with water plane and eye vector

    //get eye depth
    float e0 = max(-waterPlane.w, 0.0);

    vec3 int_v = waterPlane.w > 0.0 ? view * waterPlane.w/es : vec3(0.0, 0.0, 0.0);

    //get object depth
    float depth = length(pos - int_v);

    //get "thickness" of water
    float l = max(depth, 0.1);

    float kd = waterFogDensity;
    float ks = waterFogKS;
    vec4 kc = waterFogColor;

    float F = 0.98;

    float t1 = -kd * pow(F, ks * e0);
    float t2 = kd + ks * es;
    float t3 = pow(F, t2*l) - 1.0;

    float L = pow(min(t1/t2*t3, 1.0), 1.0/1.7);

    //-----------------------------------------------------------
    // Stock Second Life transmittance.
    //-----------------------------------------------------------

    float D = pow(0.98, l*kd);

    //===========================================================
    // AAA RENDERER
    // Recover only a small amount of medium/long-distance light.
    //
    // Fog color contribution L stays stock.
    //===========================================================

    D =
        aaaPreserveUnderwaterTransmittance(
            D,
            l
        );

    return vec4(srgb_to_linear(kc.rgb)*L, D);
}

vec4 getWaterFogView(vec3 pos)
{
    if (dot(pos, waterPlane.xyz) + waterPlane.w > 0.0)
    {
        return vec4(0,0,0,1);
    }

    return getWaterFogViewNoClip(pos);
}

vec4 applyWaterFogView(vec3 pos, vec4 color)
{
    vec4 fogged = getWaterFogView(pos);

    color.rgb = color.rgb * fogged.a + fogged.rgb;

    return color;
}

vec4 applyWaterFogViewLinearNoClip(vec3 pos, vec4 color)
{
    vec4 fogged = getWaterFogViewNoClip(pos);
    color.rgb *= fogged.a;
    color.rgb += fogged.rgb;
    return color;
}

vec4 applyWaterFogViewLinear(vec3 pos, vec4 color)
{
    if (dot(pos, waterPlane.xyz) + waterPlane.w > 0.0)
    {
        return color;
    }

    return applyWaterFogViewLinearNoClip(pos, color);
}

// for post deferred shaders, apply sky and water fog in a way that is consistent with
// the deferred rendering haze post effects
vec4 applySkyAndWaterFog(vec3 pos, vec3 additive, vec3 atten, vec4 color)
{
    bool eye_above_water = dot(vec3(0), waterPlane.xyz) + waterPlane.w > 0.0;
    bool obj_above_water = dot(pos.xyz, waterPlane.xyz) + waterPlane.w > 0.0;

    if (eye_above_water)
    {
        if (!obj_above_water)
        {
            color.rgb = applyWaterFogViewLinearNoClip(pos, color).rgb;
        }
        else
        {
            color.rgb = atmosFragLighting(color.rgb, additive, atten);
        }
    }
    else
    {
        if (obj_above_water)
        {
            color.rgb = atmosFragLighting(color.rgb, additive, atten);
        }
        else
        {
            color.rgb = applyWaterFogViewLinearNoClip(pos, color).rgb;
        }
    }

    return color;
}
