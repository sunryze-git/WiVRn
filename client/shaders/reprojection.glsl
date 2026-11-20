/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#version 450
#extension GL_GOOGLE_include_directive : require

layout(push_constant) uniform pc
{
	ivec4 rgb_rect;
	ivec4 a_rect;
	vec4 scale;
	vec4 bias;
	int debug_foveation;
	float sharpening_strength;
	ivec2 sharpening_extent;
	vec4 sharpening_inner;
	vec4 sharpening_outer;
};

#ifdef VERT_SHADER

layout (location = 0) in vec2 vPosition;
layout (location = 1) in uvec2 vUV;

layout(location = 0) out vec4 outUV;

void main()
{
	gl_Position = vec4(vPosition, 0.0, 1.0);
	vec2 uv = vUV;
	outUV.xy = (uv + rgb_rect.xy) / rgb_rect.zw;
	outUV.zw = (uv + a_rect.xy) / a_rect.zw;
}
#endif

#ifdef FRAG_SHADER

#include "rcas.inc"

layout(constant_id = 0) const int alpha = 1;
layout(constant_id = 1) const bool do_srgb = false;
// The output view stores the sRGB encoded colours as they are
layout(constant_id = 2) const bool srgb_passthrough = false;
layout(constant_id = 3) const bool sharpening_enabled = true;

layout(set = 0, binding = 0) uniform sampler2D rgb[alpha + 1];

layout(location = 0) in vec4 inUV;

layout(location = 0) out vec4 outColor;

float sRGB_to_linear(float x)
{
	if (x <= 0.04045)
		return x / 12.92;
	return pow((x + 0.055) / 1.055, 2.4);
}

vec4 sRGB_to_linear_rgba(vec4 x)
{
	return vec4(
	        sRGB_to_linear(x.r),
	        sRGB_to_linear(x.g),
	        sRGB_to_linear(x.b),
	        x.a);
}

float linear_to_sRGB(float x)
{
	if (x <= 0.0031308)
		return x * 12.92;
	return 1.055 * pow(x, 1 / 2.4) - 0.055;
}

void main()
{
	if (alpha == 1)
	{
		// Avoid sampling between the eyes
		vec2 a = inUV.zw;
		float d = a.x - 0.5;
		if (abs(d) *a_rect.z < 1)
			a.x += (d > 0 ? 1 : -1)  / float(a_rect.z);
		outColor = vec4(texture(rgb[0], inUV.xy).rgb, texture(rgb[1], a).r);
	}
	else
		outColor = texture(rgb[0], inUV.xy).rgba;

	if (sharpening_enabled && alpha == 0 && sharpening_strength > 0.0 && (do_srgb || srgb_passthrough) &&
	    all(greaterThan(sharpening_extent, ivec2(0))))
	{
		vec2 pixel = inUV.xy * vec2(rgb_rect.zw) - vec2(rgb_rect.xy);
		vec2 low = clamp((pixel - sharpening_outer.xy) / max(sharpening_inner.xy - sharpening_outer.xy, vec2(1)), 0.0, 1.0);
		vec2 high = clamp((sharpening_outer.zw - pixel) / max(sharpening_outer.zw - sharpening_inner.zw, vec2(1)), 0.0, 1.0);
		low = mix(step(sharpening_inner.xy, pixel), low * low * (3.0 - 2.0 * low), greaterThan(sharpening_inner.xy, sharpening_outer.xy));
		high = mix(step(pixel, sharpening_inner.zw), high * high * (3.0 - 2.0 * high), greaterThan(sharpening_outer.zw, sharpening_inner.zw));
		float strength = sharpening_strength * low.x * low.y * high.x * high.y;
		if (strength > 0.0)
		{
			vec2 texel = 1.0 / vec2(rgb_rect.zw);
			vec2 crop_min = (vec2(rgb_rect.xy) + 0.5) * texel;
			vec2 crop_max = (vec2(rgb_rect.xy + sharpening_extent) - 0.5) * texel;
			vec3 n = textureLod(rgb[0], clamp(inUV.xy - vec2(0, texel.y), crop_min, crop_max), 0.0).rgb;
			vec3 w = textureLod(rgb[0], clamp(inUV.xy - vec2(texel.x, 0), crop_min, crop_max), 0.0).rgb;
			vec3 e = textureLod(rgb[0], clamp(inUV.xy + vec2(texel.x, 0), crop_min, crop_max), 0.0).rgb;
			vec3 s = textureLod(rgb[0], clamp(inUV.xy + vec2(0, texel.y), crop_min, crop_max), 0.0).rgb;
			outColor.rgb = rcas(n, w, outColor.rgb, e, s, strength);
		}
	}

	// Scale and bias are linear: only convert while they change the colours (dimming)
	bool convert = srgb_passthrough && (scale != vec4(1) || bias != vec4(0));
	if (do_srgb || convert)
	{
		outColor = sRGB_to_linear_rgba(outColor);
	}

	if (alpha == 0)
	{
		outColor = outColor * scale + bias;
	}
	else if (outColor.a < 0.02)
	{
		outColor = vec4(0);
	}
	else
	{
		outColor.rgb /= outColor.a;
		outColor = outColor * scale + bias;
		outColor.rgb *= outColor.a;
	}

	if (convert)
	{
		outColor.rgb = vec3(linear_to_sRGB(outColor.r), linear_to_sRGB(outColor.g), linear_to_sRGB(outColor.b));
	}

	if (debug_foveation != 0)
	{
		// Streamed pixels per output pixel: 1 where the stream keeps full resolution, less where foveation squeezes
		vec2 streamed = fwidth(inUV.xy * vec2(rgb_rect.zw));
		float d = min(streamed.x, streamed.y);
		vec3 tint = d > 0.9 ? vec3(0, 1, 0) : d > 0.45 ? vec3(1, 1, 0) : vec3(1, 0, 0);
		// A faint colour cast keeps the image's own detail; the borders between resolutions are drawn solid
		outColor.rgb *= mix(vec3(1), 0.5 + 0.5 * tint, 0.6);
		float level = d > 0.9 ? 2.0 : d > 0.45 ? 1.0 : 0.0;
		if (fwidth(level) > 0.0)
			outColor.rgb = tint * max(outColor.a, 0.5);
	}
}

#endif
