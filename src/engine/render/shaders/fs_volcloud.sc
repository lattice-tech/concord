$input v_ray, v_uv

#include <bgfx_shader.sh>

// Independent volumetric cloud pass (see BgfxVolumeCloudRenderer). Marches the
// authored cloud layer along the reconstructed world ray, truncated by the
// scene depth so opaque geometry correctly occludes the clouds, and outputs a
// premultiplied-alpha HDR contribution composited over the offscreen scene RT
// before tone mapping. This deliberately does NOT live in fs_sky: the sky is
// the far background, whereas clouds are a participating medium that must be
// depth-composited against scene geometry.

SAMPLER2D(s_vcSceneDepth, 0);

uniform mat4 u_vcInvViewProj;
// xyz: camera world position, w: near-plane NDC z.
uniform vec4 u_vcCamera;
// x: enabled, y: base height, z: thickness, w: density.
uniform vec4 u_vcLayer;
// x: coverage, y: world scale, z: erosion, w: detail.
uniform vec4 u_vcShape;
// xy: world offset, z: silver lining, w: fire emission.
uniform vec4 u_vcMotion;
uniform vec4 u_vcLit;
uniform vec4 u_vcShadow;
uniform vec4 u_vcFire;
// xyz: sun travel direction (light points this way), w: unused.
uniform vec4 u_vcSunDir;

vec3 CloudToLinear(vec3 color)
{
	return pow(max(color, vec3_splat(0.0)), vec3_splat(2.2));
}

float Hash31(vec3 samplePosition)
{
	samplePosition = fract(samplePosition * 0.1031);
	samplePosition += dot(samplePosition, samplePosition.yzx + 33.33);
	return fract((samplePosition.x + samplePosition.y) * samplePosition.z);
}

float ValueNoise(vec3 samplePosition)
{
	vec3 cell = floor(samplePosition);
	vec3 local = fract(samplePosition);
	local = local * local * (3.0 - 2.0 * local);
	float n000 = Hash31(cell + vec3(0.0, 0.0, 0.0));
	float n100 = Hash31(cell + vec3(1.0, 0.0, 0.0));
	float n010 = Hash31(cell + vec3(0.0, 1.0, 0.0));
	float n110 = Hash31(cell + vec3(1.0, 1.0, 0.0));
	float n001 = Hash31(cell + vec3(0.0, 0.0, 1.0));
	float n101 = Hash31(cell + vec3(1.0, 0.0, 1.0));
	float n011 = Hash31(cell + vec3(0.0, 1.0, 1.0));
	float n111 = Hash31(cell + vec3(1.0, 1.0, 1.0));
	float low = mix(mix(n000, n100, local.x), mix(n010, n110, local.x), local.y);
	float high = mix(mix(n001, n101, local.x), mix(n011, n111, local.x), local.y);
	return mix(low, high, local.z);
}

// Fractal Brownian motion: several octaves of value noise summed at halving
// amplitude and (slightly non-integer) doubling frequency, giving natural
// billowing cloud shape instead of the single-frequency layering that reads as
// horizontal stripes.
float Fbm(vec3 samplePosition)
{
	float sum = 0.0;
	float amplitude = 0.5;
	for (int octave = 0; octave < 3; ++octave)
	{
		sum += amplitude * ValueNoise(samplePosition);
		samplePosition *= 2.02;
		amplitude *= 0.5;
	}
	return sum;
}

float CloudDensity(vec3 samplePosition)
{
	float scale = max(u_vcShape.y, 1.0);
	vec3 samplePoint = vec3(
		(samplePosition.x + u_vcMotion.x) / scale,
		(samplePosition.y - u_vcLayer.y) / max(u_vcLayer.z, 1.0),
		(samplePosition.z + u_vcMotion.y) / scale);

	// Rounded vertical profile: thin at the slab's floor and ceiling, dense in
	// the middle, so the layer has soft tops/bottoms rather than a hard cut.
	float height = clamp(samplePoint.y, 0.0, 1.0);
	float vertical = smoothstep(0.0, 0.2, height) * (1.0 - smoothstep(0.5, 1.0, height));

	// Broad billowing shape carved to the requested coverage, then eroded by a
	// higher-frequency octave for wispy edges.
	float shape = Fbm(samplePoint * 3.0);
	float coverage = clamp(u_vcShape.x, 0.0, 1.0);
	float threshold = 1.0 - coverage;
	float formed = smoothstep(threshold, threshold + 0.2, shape) * vertical;
	float detail = Fbm(samplePoint * 9.0 + vec3(11.0, 5.0, 17.0));
	float eroded = formed - (1.0 - detail) * u_vcShape.z * 0.5;
	return clamp(eroded, 0.0, 1.0) * u_vcLayer.w;
}

// Cheap 2-octave density used only for the light-march self-shadow term. The
// lit visibility does not need the full 4-octave shape + detail the primary
// march uses, so this roughly halves the per-lit-step noise cost with no
// visible change to the cloud silhouette.
float CloudDensityLight(vec3 samplePosition)
{
	float scale = max(u_vcShape.y, 1.0);
	vec3 samplePoint = vec3(
		(samplePosition.x + u_vcMotion.x) / scale,
		(samplePosition.y - u_vcLayer.y) / max(u_vcLayer.z, 1.0),
		(samplePosition.z + u_vcMotion.y) / scale);
	float height = clamp(samplePoint.y, 0.0, 1.0);
	float vertical = smoothstep(0.0, 0.2, height) * (1.0 - smoothstep(0.5, 1.0, height));
	float shape = ValueNoise(samplePoint * 3.0) * 0.65 + ValueNoise(samplePoint * 6.06) * 0.35;
	float coverage = clamp(u_vcShape.x, 0.0, 1.0);
	float threshold = 1.0 - coverage;
	float formed = smoothstep(threshold, threshold + 0.2, shape) * vertical;
	return clamp(formed, 0.0, 1.0) * u_vcLayer.w;
}

float HenyeyGreenstein(float cosAngle, float g)
{
	float gg = g * g;
	return (1.0 - gg) / pow(max(1.0 + gg - 2.0 * g * cosAngle, 1e-4), 1.5);
}

vec4 MarchClouds(vec3 direction, vec3 toSun, float sceneDistance, float jitter)
{
	// Skip rays at or below the horizon-fade threshold: their contribution is
	// faded to ~zero anyway, so marching them is wasted work.
	if (u_vcLayer.x < 0.5 || direction.y <= 0.04)
	{
		return vec4(0.0);
	}
	float startDistance = max((u_vcLayer.y - u_vcCamera.y) / direction.y, 0.0);
	float endDistance = max((u_vcLayer.y + u_vcLayer.z - u_vcCamera.y) / direction.y, startDistance);
	// Depth truncation: never march past the nearest opaque surface, so scene
	// geometry occludes the clouds correctly.
	endDistance = min(endDistance, sceneDistance);
	if (endDistance <= startDistance)
	{
		return vec4(0.0);
	}

	const int kSteps = 28;
	float stepDistance = (endDistance - startDistance) / float(kSteps);
	float transmittance = 1.0;
	vec3 radiance = vec3(0.0);
	float cosAngle = dot(direction, toSun);
	float phase = mix(HenyeyGreenstein(cosAngle, 0.35),
		HenyeyGreenstein(cosAngle, -0.15), 0.4);
	for (int step = 0; step < kSteps; ++step)
	{
		// Per-pixel jittered sample position breaks the fixed-step layering
		// that otherwise reads as horizontal banding, trading it for fine noise.
		float distance = startDistance + (float(step) + jitter) * stepDistance;
		if (distance > sceneDistance)
		{
			break;
		}
		vec3 samplePosition = u_vcCamera.xyz + direction * distance;
		float density = CloudDensity(samplePosition);
		if (density > 0.001)
		{
			// Thickness-normalized extinction: opacity depends on density and
			// the fraction of the layer each step covers, not the absolute world
			// size, so the same density reads consistently for a thin demo slab
			// and a kilometre-scale sky layer alike.
			float opticalDepth = density * (stepDistance / max(u_vcLayer.z, 1.0)) * 6.0;
			float alpha = 1.0 - exp(-opticalDepth);
			float lightDensity = CloudDensityLight(samplePosition + toSun * max(u_vcLayer.z * 0.08, 30.0));
			float lightVisibility = exp(-lightDensity * 1.8);
			float forward = clamp(phase, 0.0, 4.0) * 0.15 * u_vcMotion.z;
			// Sky ambient lifts the self-shadowed cloud interior toward the lit
			// (white) tone rather than the darker shadow color. Clouds viewed
			// near the grazing horizon otherwise let the gray horizon sky read
			// through and look flat gray; this makes them read white, matching
			// the brighter clouds seen reflected from above in the mirror sphere.
			float ambientFill = 0.35;
			vec3 lit = mix(CloudToLinear(u_vcShadow.rgb), CloudToLinear(u_vcLit.rgb),
				clamp(lightVisibility + forward + ambientFill, 0.0, 1.0));
			vec3 fire = CloudToLinear(u_vcFire.rgb) * u_vcMotion.w
				* density * (0.35 + 0.65 * ValueNoise(samplePosition / 480.0));
			radiance += (lit + fire) * alpha * transmittance;
			transmittance *= 1.0 - alpha;
			if (transmittance < 0.02)
			{
				break;
			}
		}
	}
	// Fade the layer toward the horizon. At grazing angles the ray crosses the
	// slab nearly edge-on, so the half-resolution march badly undersamples the
	// fast-changing density and reads as blocky "mosaic"; fading there both
	// hides that and matches how distant clouds dissolve into atmospheric haze.
	float horizonFade = smoothstep(0.04, 0.28, direction.y);
	return vec4(radiance, 1.0 - transmittance) * horizonFade;
}

void main()
{
	vec3 direction = normalize(v_ray);

	float sceneDistance = 1e9;
	// Offscreen render targets use a top-left origin on Vulkan (this engine is
	// Vulkan-only), so sampling the scene depth from a fullscreen pass needs the
	// same V flip the present pass applies. The reconstruction ndc stays in this
	// fragment's own clip space (unflipped) so the fetched depth and the ndc
	// describe the same screen pixel.
	vec2 depthUv = vec2(v_uv.x, 1.0 - v_uv.y);
	float depth = texture2D(s_vcSceneDepth, depthUv).x;
	if (depth < 1.0)
	{
		vec2 ndc = v_uv * 2.0 - 1.0;
		vec4 worldPosition = mul(u_vcInvViewProj, vec4(ndc, depth, 1.0));
		worldPosition.xyz /= worldPosition.w;
		sceneDistance = length(worldPosition.xyz - u_vcCamera.xyz);
	}

	vec3 toSun = normalize(-u_vcSunDir.xyz);
	float jitter = fract(sin(dot(v_uv, vec2(12.9898, 78.233))) * 43758.5453);
	vec4 clouds = MarchClouds(direction, toSun, sceneDistance, jitter);

	// Premultiplied-alpha HDR contribution; the pass blends with
	// (ONE, INV_SRC_ALPHA) over the linear scene color.
	gl_FragColor = vec4(clouds.rgb, clouds.a);
}
