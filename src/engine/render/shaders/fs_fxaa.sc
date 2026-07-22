$input v_texcoord0

#include <bgfx_shader.sh>

// Offscreen scene color (RGBA16F). Final write goes to the 8-bit swap chain.
SAMPLER2D(s_scene, 0);

// Bloom contribution (blurred bright-pass), added on top of the resolved
// scene just before dithering. Bound to a black-equivalent source with
// intensity 0 when bloom is disabled, so this pass stays a plain present.
SAMPLER2D(s_bloom, 1);

// x: flip V (handled in vs_fullscreen), y: 1/width, z: 1/height,
// w: quality (>=1 FXAA, 0 blit).
uniform vec4 u_presentParams;

// x: bloom intensity (0 disables the additive composite). yzw reserved.
uniform vec4 u_bloomComposite;

// xy: resolved-image displacement in output pixels.
uniform vec4 u_screenShake;

// xy: normalized center, z: short-axis radius, w: zoom.
uniform vec4 u_magnifier;

// x: radial distortion, y: edge feather, z: enabled.
uniform vec4 u_magnifierParams;

// xy: sun position in y-up normalized viewport coords, z: strength (0 disables).
uniform vec4 u_lensFlare;

// Camera lens flare: warm halo at the sun plus aperture "ghost" blobs spaced
// along the sun->screen-center axis. All soft Gaussians (no hard rings) so it
// stays artifact-free, additive over the resolved scene. Strength 0 is a no-op.
vec3 applyLensFlare(vec3 color, vec2 uv, vec2 texel)
{
	float strength = u_lensFlare.z;
	if (strength <= 0.0)
	{
		return color;
	}
	vec2 aspectScale = vec2(texel.y / max(texel.x, 1e-6), 1.0);
	vec2 sunPos = u_lensFlare.xy;
	vec2 center = vec2(0.5, 0.5);
	vec2 axis = center - sunPos;
	vec3 flare = vec3_splat(0.0);
	float sunDist = length((uv - sunPos) * aspectScale);
	flare += vec3(1.0, 0.85, 0.6) * exp(-sunDist * sunDist * 60.0) * 0.8;
	flare += vec3(1.0, 0.9, 0.75) * exp(-sunDist * 7.0) * 0.25;
	for (int i = 0; i < 6; ++i)
	{
		float t = 0.3 + float(i) * 0.28;
		vec2 ghost = sunPos + axis * t;
		float d = length((uv - ghost) * aspectScale);
		float size = 22.0 + float(i) * 8.0;
		vec3 tint = mix(vec3(0.5, 0.7, 1.0), vec3(1.0, 0.6, 0.4), fract(float(i) * 0.4));
		flare += tint * exp(-d * d * size) * 0.18;
	}
	float edge = smoothstep(1.5, 0.5, length((uv - center) * aspectScale));
	return color + flare * strength * edge;
}

vec2 clampSceneUv(vec2 uv, vec2 texel)
{
	return clamp(uv, texel * 0.5, vec2_splat(1.0) - texel * 0.5);
}

vec2 viewportToSceneUv(vec2 uv)
{
	uv.y = mix(uv.y, 1.0 - uv.y, u_presentParams.x);
	return uv;
}

vec2 applyViewEffects(vec2 inputUv, vec2 texel)
{
	vec2 shakeUv = u_screenShake.xy * texel;
	vec2 uv = inputUv + shakeUv;

	if (u_magnifierParams.z > 0.5 && u_magnifier.z > 0.0)
	{
		vec2 viewport = vec2(1.0 / max(texel.x, 1e-6), 1.0 / max(texel.y, 1e-6));
		float shortSide = min(viewport.x, viewport.y);
		vec2 axisScale = viewport / max(shortSide, 1.0);
		vec2 lensDelta = (inputUv - u_magnifier.xy) * axisScale;
		float distanceFromCenter = length(lensDelta);
		float radius = max(u_magnifier.z, 1e-5);
		float normalizedRadius = clamp(distanceFromCenter / radius, 0.0, 1.0);
		float radialWarp = 1.0 + u_magnifierParams.x
			* normalizedRadius * normalizedRadius;
		vec2 magnifiedDelta = lensDelta * radialWarp / max(u_magnifier.w, 1.0);
		vec2 magnifiedUv = u_magnifier.xy + magnifiedDelta / axisScale + shakeUv;
		float feather = min(max(u_magnifierParams.y, 0.0), radius);
		float blend = feather > 1e-5
			? 1.0 - smoothstep(radius - feather, radius, distanceFromCenter)
			: step(distanceFromCenter, radius);
		uv = mix(uv, magnifiedUv, blend);
	}

	return clampSceneUv(uv, texel);
}

// Adds the blurred bloom energy on top of the resolved scene color. The bloom
// chain is built by the same kind of no-flip fullscreen passes SMAA uses, so
// the bloom target shares the scene target's orientation and is sampled with
// the very same `uv` (v_texcoord0) — the final present V-flip in vs_fullscreen
// applies to both together. (An extra 1-uv.y here flipped bloom upside down,
// smearing the scene's bright top half onto the bottom.)
vec3 addBloom(vec3 color, vec2 uv)
{
	return color + texture2D(s_bloom, uv).rgb * u_bloomComposite.x;
}

vec3 sampleScene(vec2 uv, vec2 texel)
{
	return texture2D(s_scene, clampSceneUv(uv, texel)).rgb;
}

// Hash white noise in [0,1) (Dave Hoskins' hash12). Unlike interleaved
// gradient noise it has no directional structure, so it does not leave a
// diagonal cross-hatch when it dithers a smooth lighting gradient on a flat
// wall or floor — that ordered-dither weave was the visible "一道一道" pattern.
float hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
	p3 += vec3_splat(dot(p3, vec3(p3.y, p3.z, p3.x) + vec3_splat(33.33)));
	return fract((p3.x + p3.y) * p3.z);
}

// Triangular-PDF dither (difference of two uniform noises) applied only at the
// final 8-bit write. TPDF fully decorrelates quantization error from the signal
// — the standard fix for flat-surface banding — and per-channel noise keeps the
// residual grain neutral instead of tinted.
vec3 applyOutputDither(vec3 color, vec2 fragCoord)
{
	vec3 r0 = vec3(hash12(fragCoord + vec2(0.5, 0.5)),
	              hash12(fragCoord + vec2(19.19, 7.31)),
	              hash12(fragCoord + vec2(43.37, 61.7)));
	vec3 r1 = vec3(hash12(fragCoord + vec2(113.5, 271.9)),
	              hash12(fragCoord + vec2(61.7, 19.19)),
	              hash12(fragCoord + vec2(7.31, 157.3)));
	return color + (r0 - r1) * (1.0 / 255.0);
}

void main()
{
	vec2 texel = vec2(u_presentParams.y, u_presentParams.z);
	// Undo the render-target V flip while authoring screen effects. This keeps
	// normalized viewport coordinates backend-independent.
	vec2 outputUv = v_texcoord0;
	outputUv.y = mix(outputUv.y, 1.0 - outputUv.y, u_presentParams.x);
	vec2 uv = viewportToSceneUv(applyViewEffects(outputUv, texel));
	// Keep output dither fixed to display pixels so shake/lens motion cannot
	// make the quantization noise swim across the image.
	vec2 fragCoord = outputUv / max(texel, vec2_splat(1e-6));

	// Quality 0: plain blit (Off / MSAA / SMAA result) + final dither.
	// No spatial deband here — neighbour blending on already-banded LDR can
	// create "ridge / wave" looking artifacts that read as fake depth.
	if (u_presentParams.w < 0.5)
	{
		vec3 color = sampleScene(uv, texel);
		color = addBloom(color, uv);
		color = applyLensFlare(color, outputUv, texel);
		gl_FragColor = vec4(applyOutputDither(color, fragCoord), 1.0);
		return;
	}

	// FXAA (Lottes console), then dither the AA'd result.
	float quality = max(u_presentParams.w, 1.0);
	float spanMax = 6.0 * quality + 6.0;
	float reduceMul = 1.0 / 6.0;
	float reduceMin = 1.0 / 96.0;

	vec3 rgbNW = sampleScene(uv + vec2(-1.0, -1.0) * texel, texel);
	vec3 rgbNE = sampleScene(uv + vec2( 1.0, -1.0) * texel, texel);
	vec3 rgbSW = sampleScene(uv + vec2(-1.0,  1.0) * texel, texel);
	vec3 rgbSE = sampleScene(uv + vec2( 1.0,  1.0) * texel, texel);
	vec3 rgbM  = sampleScene(uv, texel);

	vec3 luma = vec3(0.299, 0.587, 0.114);
	float lumaNW = dot(rgbNW, luma);
	float lumaNE = dot(rgbNE, luma);
	float lumaSW = dot(rgbSW, luma);
	float lumaSE = dot(rgbSE, luma);
	float lumaM  = dot(rgbM,  luma);

	float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

	vec2 dir;
	dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
	dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

	float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * reduceMul, reduceMin);
	float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = clamp(dir * rcpDirMin, vec2(-spanMax, -spanMax), vec2(spanMax, spanMax)) * texel;

	vec3 rgbA = 0.5 * (
		sampleScene(uv + dir * (1.0 / 3.0 - 0.5), texel) +
		sampleScene(uv + dir * (2.0 / 3.0 - 0.5), texel));
	vec3 rgbB = rgbA * 0.5 + 0.25 * (
		sampleScene(uv + dir * -0.5, texel) +
		sampleScene(uv + dir *  0.5, texel));

	float lumaB = dot(rgbB, luma);
	vec3 color = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
	color = addBloom(color, uv);
	color = applyLensFlare(color, outputUv, texel);
	gl_FragColor = vec4(applyOutputDither(color, fragCoord), 1.0);
}
