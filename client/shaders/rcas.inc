// Adapted from AMD FidelityFX FSR1 RCAS, with denoising and guarded divisions.
// https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

vec3 rcas(vec3 n, vec3 w, vec3 c, vec3 e, vec3 s, float strength)
{
	// YCbCr conversion is allowed to return RGB outside the SDR display range.
	n = clamp(n, 0.0, 1.0);
	w = clamp(w, 0.0, 1.0);
	c = clamp(c, 0.0, 1.0);
	e = clamp(e, 0.0, 1.0);
	s = clamp(s, 0.0, 1.0);
	vec3 lo = min(min(n, w), min(e, s));
	vec3 hi = max(max(n, w), max(e, s));
	vec3 hit_min = min(lo, c) / max(4.0 * hi, vec3(1e-6));
	vec3 hit_max = (1.0 - max(hi, c)) / min(4.0 * lo - 4.0, vec3(-1e-6));
	vec3 limits = max(-hit_min, hit_max);
	float lobe = max(-0.1875, min(max(max(limits.r, limits.g), limits.b), 0.0));
	vec3 luma = vec3(0.5, 1.0, 0.5);
	float nl = dot(n, luma), wl = dot(w, luma), cl = dot(c, luma);
	float el = dot(e, luma), sl = dot(s, luma);
	float range = max(max(max(nl, wl), max(el, sl)), cl) - min(min(min(nl, wl), min(el, sl)), cl);
	float noise = clamp(abs(0.25 * (nl + wl + el + sl) - cl) / max(range, 1e-6), 0.0, 1.0);
	lobe *= strength * (1.0 - 0.5 * noise);
	return clamp((lobe * (n + w + e + s) + c) / (4.0 * lobe + 1.0), 0.0, 1.0);
}
