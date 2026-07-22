#include <bgfx_compute.sh>

// Analytic-sphere ray tracer with a current-frame real-scene cubemap.
//
// Pipeline order (bgfx view ids): shadow → scene → rtCompute → rtResolve → post.
// Sphere-to-sphere reflections are analytic; rays that leave the traced set
// sample the six-face scene capture. This avoids depth-only screen-space gaps,
// angle stretching and the old static ground/sky approximation.
//
// Output: rgba32f (display RGB + packed sample-count/device-depth). Resolve
// decodes coverage, blends the traced silhouette, and depth-tests into scene.

IMAGE2D_WO(s_rtTarget, rgba32f, 0);
SAMPLERCUBE(s_rtEnvironment, 1);

#define RT_MAX_SPHERES 8

uniform vec4 u_rtParams;
uniform vec4 u_rtCamera;
// x: homogeneousDepth, y: originBottomLeft
uniform vec4 u_rtOptions;
uniform vec4 u_rtLight;
uniform vec4 u_rtSun;
uniform vec4 u_rtSky;
uniform mat4 u_rtInvViewProj;
uniform mat4 u_rtViewProj;
uniform vec4 u_rtSpheres[RT_MAX_SPHERES * 3];

float TraceSpheres(vec3 origin, vec3 dir, int count, int ignore, out int hitIndex)
{
	float nearest = 1e30;
	hitIndex = -1;
	for (int i = 0; i < RT_MAX_SPHERES; ++i)
	{
		if (i >= count || i == ignore)
		{
			continue;
		}
		vec4 s0 = u_rtSpheres[i * 3 + 0];
		vec3 center = s0.xyz;
		float radius = s0.w;
		vec3 oc = origin - center;
		float b = dot(oc, dir);
		float c = dot(oc, oc) - radius * radius;
		float disc = b * b - c;
		if (disc < 0.0)
		{
			continue;
		}
		float t = -b - sqrt(disc);
		if (t > 0.001 && t < nearest)
		{
			nearest = t;
			hitIndex = i;
		}
	}
	return hitIndex >= 0 ? nearest : -1.0;
}

vec3 RtToLinear(vec3 c) { return pow(max(c, vec3_splat(0.0)), vec3_splat(2.2)); }

// Narkowicz ACES + gamma — identical film curve to fs_mesh so RT and raster match.
vec3 RtTonemap(vec3 x)
{
	x = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
	return pow(x, vec3_splat(1.0 / 2.2));
}

vec3 SkyColorLinear(vec3 dir)
{
	float up = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
	vec3 sky = u_rtSky.rgb;
	vec3 horizon = mix(sky, vec3_splat(dot(sky, vec3(0.33, 0.34, 0.33))), 0.35) * 1.6;
	vec3 zenith = sky * 0.9;
	return mix(horizon, zenith, up);
}

vec3 SkyAmbient()
{
	return u_rtSky.rgb * u_rtSky.w;
}

float SunVisibility(vec3 hit, vec3 normal, int count, int ignore)
{
	vec3 L = normalize(-u_rtLight.xyz);
	int blocker;
	return TraceSpheres(hit + normal * 0.012, L, count, ignore, blocker) > 0.0 ? 0.0 : 1.0;
}

// Direct sphere shade in LINEAR, then caller tonemaps — matches mesh PBR mood.
vec3 ShadeSphereLinear(vec3 hit, vec3 normal, vec3 albedoSrgb, vec3 viewDir,
	float roughness, float metallic, int count, int ignore)
{
	vec3 albedo = RtToLinear(albedoSrgb);
	vec3 L = normalize(-u_rtLight.xyz);
	float ndl = max(dot(normal, L), 0.0);
	float wrap = max(dot(normal, L) * 0.5 + 0.5, 0.0);
	wrap = wrap * wrap;
	float hemi = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
	hemi = hemi * hemi * (3.0 - 2.0 * hemi);
	vec3 ambient = SkyAmbient() * mix(0.75, 1.15, hemi);
	vec3 H = normalize(L + (-viewDir));
	float specPower = mix(256.0, 12.0, roughness);
	float spec = pow(max(dot(normal, H), 0.0), specPower) * mix(0.55, 0.06, roughness);
	float visibility = SunVisibility(hit, normal, count, ignore);
	vec3 diffuse = albedo * (ambient + u_rtSun.rgb * (ndl * 0.9 + wrap * 0.2) * visibility);
	// Metals keep little diffuse; dielectrics keep most.
	vec3 color = diffuse * (1.0 - metallic * 0.92) + u_rtSun.rgb * spec * visibility
		* mix(0.35, 1.0, metallic);
	return color * 1.05; // match fs_mesh pre-tonemap gain
}

bool TracePrimarySample(vec2 uv, int count, out vec3 sampleColor, out float sampleDepth)
{
	bool homogeneous = u_rtOptions.x > 0.5;
	bool originBottomLeft = u_rtOptions.y > 0.5;

	float ndcX = uv.x * 2.0 - 1.0;
	float ndcYtop = 1.0 - uv.y * 2.0;
	float ndcY = originBottomLeft ? -ndcYtop : ndcYtop;

	vec3 camPos = u_rtCamera.xyz;
	vec4 farClip = mul(u_rtInvViewProj, vec4(ndcX, ndcY, 1.0, 1.0));
	vec3 farWorld = farClip.xyz / farClip.w;
	vec3 rayDir = normalize(farWorld - camPos);

	int hitIndex;
	float t = TraceSpheres(camPos, rayDir, count, -1, hitIndex);
	if (hitIndex < 0)
	{
		return false;
	}

	// Primary hit supplies resolve depth; lighting follows up to three analytic
	// sphere bounces before resolving against the ground or sky.
	vec3 hit = camPos + rayDir * t;
	vec3 radianceLinear = vec3_splat(0.0);
	vec3 throughput = vec3_splat(1.0);
	vec3 bounceOrigin = camPos;
	vec3 bounceDir = rayDir;
	float bounceT = t;
	int bounceIndex = hitIndex;

	for (int bounce = 0; bounce < 3; ++bounce)
	{
		vec3 bounceHit = bounceOrigin + bounceDir * bounceT;
		vec4 bs0 = u_rtSpheres[bounceIndex * 3 + 0];
		vec4 bs1 = u_rtSpheres[bounceIndex * 3 + 1];
		vec4 bs2 = u_rtSpheres[bounceIndex * 3 + 2];
		vec3 bounceNormal = normalize(bounceHit - bs0.xyz);
		float roughness = clamp(bs2.x, 0.02, 1.0);
		float metallic = clamp(bs2.y, 0.0, 1.0);
		float f0 = clamp(bs1.w, 0.02, 1.0);

		vec3 directLin = ShadeSphereLinear(bounceHit, bounceNormal, bs1.rgb, bounceDir,
			roughness, metallic, count, bounceIndex);
		float ndv = max(dot(bounceNormal, -bounceDir), 0.0);
		float fresnel = f0 + (1.0 - f0) * pow(1.0 - ndv, 5.0);
		// Dielectrics: less mirror weight; metals: full tinted mirror.
		float mirrorW = fresnel * mix(0.55, 1.0, metallic) * (1.0 - roughness * 0.65);

		vec3 albedoLinear = RtToLinear(bs1.rgb);
		vec3 reflectionTint = mix(vec3_splat(1.0), albedoLinear, metallic);

		// Accumulate every bounce in linear space and tonemap exactly once.
		radianceLinear += throughput * directLin * (1.0 - mirrorW);

		// Roughness may attenuate or choose a prefiltered mip, but it must never
		// bend the reflection direction toward the normal: that visibly warps the scene.
		vec3 reflDir = normalize(reflect(bounceDir, bounceNormal));
		vec3 reflOrigin = bounceHit + bounceNormal * 0.015;

		// Analytic spheres bounce into one another. Rays leaving the traced set
		// read the real current-frame scene capture in linear HDR. Compute shaders
		// have no implicit derivatives, so the cubemap LOD is explicit.
		int nextIndex;
		float nextT = TraceSpheres(reflOrigin, reflDir, count, bounceIndex, nextIndex);
		if (nextIndex < 0)
		{
			vec4 environment = textureCubeLod(s_rtEnvironment, reflDir, 0.0);
			// Capture clear alpha is zero. Bilinear edge samples therefore behave
			// like premultiplied geometry and blend cleanly into the shared sky.
			vec3 environmentLinear = environment.rgb
				+ SkyColorLinear(reflDir) * (1.0 - clamp(environment.a, 0.0, 1.0));
			radianceLinear += throughput * environmentLinear * reflectionTint * mirrorW;
			break;
		}

		// Hit another RT sphere: continue bounce in linear.
		throughput *= reflectionTint * mirrorW;
		if (dot(throughput, vec3_splat(1.0)) < 0.02)
		{
			break;
		}
		bounceOrigin = reflOrigin;
		bounceDir = reflDir;
		bounceT = nextT;
		bounceIndex = nextIndex;
	}

	vec3 color = RtTonemap(radianceLinear);
	vec4 clip = mul(u_rtViewProj, vec4(hit, 1.0));
	float ndcZ = clip.z / clip.w;
	float depth01 = homogeneous ? (ndcZ * 0.5 + 0.5) : ndcZ;
	depth01 = clamp(depth01 - 0.00015, 0.0, 1.0);
	sampleColor = color;
	sampleDepth = depth01;
	return true;
}

NUM_THREADS(16, 16, 1)
void main()
{
	ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
	float imgW = u_rtParams.x;
	float imgH = u_rtParams.y;
	if (float(coord.x) >= imgW || float(coord.y) >= imgH)
	{
		return;
	}

	int count = int(u_rtParams.z);
	if (count <= 0)
	{
		imageStore(s_rtTarget, coord, vec4_splat(0.0));
		return;
	}

	// Standard rotated-grid 4x sample positions. Each sample traces its own
	// reflection path, so both the analytic silhouette and reflected detail are
	// antialiased before the final full-scene FXAA/SMAA pass.
	vec2 imageSize = vec2(imgW, imgH);
	vec2 pixel = vec2(coord);
	vec3 colorSum = vec3_splat(0.0);
	float nearestDepth = 1.0;
	int hitCount = 0;
	vec3 color;
	float depth;

	if (TracePrimarySample((pixel + vec2(0.375, 0.125)) / imageSize, count, color, depth))
	{
		colorSum += color;
		nearestDepth = min(nearestDepth, depth);
		hitCount += 1;
	}
	if (TracePrimarySample((pixel + vec2(0.875, 0.375)) / imageSize, count, color, depth))
	{
		colorSum += color;
		nearestDepth = min(nearestDepth, depth);
		hitCount += 1;
	}
	if (TracePrimarySample((pixel + vec2(0.125, 0.625)) / imageSize, count, color, depth))
	{
		colorSum += color;
		nearestDepth = min(nearestDepth, depth);
		hitCount += 1;
	}
	if (TracePrimarySample((pixel + vec2(0.625, 0.875)) / imageSize, count, color, depth))
	{
		colorSum += color;
		nearestDepth = min(nearestDepth, depth);
		hitCount += 1;
	}

	if (hitCount == 0)
	{
		imageStore(s_rtTarget, coord, vec4_splat(0.0));
		return;
	}

	// RGBA32F leaves ample exact integer range. A two-unit stride keeps the
	// [0,1] device depth disjoint from sample count, including far-plane values.
	float packedCoverageDepth = float(hitCount) * 2.0 + nearestDepth;
	imageStore(s_rtTarget, coord,
		vec4(colorSum / float(hitCount), packedCoverageDepth));
}
