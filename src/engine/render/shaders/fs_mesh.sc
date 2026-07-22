$input v_wpos, v_wnormal, v_lpos, v_texcoord0

#include <bgfx_shader.sh>

// Must match Concord::kMaxRenderLights (see RenderLight.h).
#define MAX_LIGHTS 8
#define PI 3.14159265359

SAMPLER2D(s_albedo, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_metallicRoughness, 2);
SAMPLER2D(s_emissive, 3);
SAMPLER2D(s_shadowMap0, 4);
SAMPLER2D(s_shadowMap1, 5);
SAMPLER2D(s_shadowMap2, 6);
SAMPLER2D(s_planarReflection, 7);
SAMPLERCUBE(s_sceneReflection, 8);
uniform mat4 u_planarViewProj;
uniform vec4 u_texFlags; // x: normal map, y: flip shadow V, z: blend, w: planar
uniform vec4 u_outputFlags; // x: preserve linear HDR for reflection capture
uniform vec4 u_reflectionFlags; // x: real-time cubemap reflection, y: reflectivity
uniform vec4 u_reflectionProbe; // xyz: cubemap capture origin
uniform vec4 u_reflectionBoxMin; // xyz: local probe box minimum, w: enabled
uniform vec4 u_reflectionBoxMax; // xyz: local probe box maximum
uniform vec4 u_clipPlane; // xyz,d: world clip plane; zero normal disables

uniform mat4 u_lightViewProj[3];
uniform mat4 u_shadowCameraView;
uniform vec4 u_shadowCascadeSplits;
uniform vec4 u_shadowCascadeBlend;
uniform vec4 u_shadowPenumbra;
uniform vec4 u_shadowNormalBias; // xyz per-cascade world-space normal bias
uniform vec4 u_shadowParams;     // x depth bias, z texel size, w caster index + 1
uniform vec4 u_shadowLightDir;   // xyz travel direction of caster
uniform vec4 u_shadowFilter;     // x blocker search, y min radius, z max radius, w penumbra scale
uniform vec4 u_shadowProjection; // x: homogeneous depth

uniform vec4 u_albedo;
uniform vec4 u_gradientTo;
uniform vec4 u_emissive;
uniform vec4 u_matParams;  // x metallic, y roughness, z lit, w gradient

uniform vec4 u_lightPosType[MAX_LIGHTS];
uniform vec4 u_lightDirRange[MAX_LIGHTS];
uniform vec4 u_lightColor[MAX_LIGHTS];
uniform vec4 u_lightSpot[MAX_LIGHTS];
uniform vec4 u_ambient;
uniform vec4 u_camPos;

vec3 toLinear3(vec3 c) { return pow(max(c, vec3_splat(0.0)), vec3_splat(2.2)); }
vec3 toGamma3(vec3 c)  { return pow(max(c, vec3_splat(0.0)), vec3_splat(1.0 / 2.2)); }

vec3 BoxProjectReflection(vec3 direction, vec3 worldPosition)
{
	if (u_reflectionBoxMin.w < 0.5)
	{
		return direction;
	}
	vec3 directionSign = mix(
		vec3_splat(-1.0), vec3_splat(1.0), step(vec3_splat(0.0), direction));
	vec3 safeDirection = directionSign * max(abs(direction), vec3_splat(1e-4));
	vec3 distanceToMin = (u_reflectionBoxMin.xyz - worldPosition) / safeDirection;
	vec3 distanceToMax = (u_reflectionBoxMax.xyz - worldPosition) / safeDirection;
	vec3 exitDistance = max(distanceToMin, distanceToMax);
	float rayDistance = min(exitDistance.x, min(exitDistance.y, exitDistance.z));
	if (rayDistance <= 0.0)
	{
		return direction;
	}
	vec3 boxHit = worldPosition + direction * rayDistance;
	return normalize(boxHit - u_reflectionProbe.xyz);
}

vec3 acesFilm(vec3 x)
{
	float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float distributionGGX(float ndh, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float denom = ndh * ndh * (a2 - 1.0) + 1.0;
	return a2 / max(PI * denom * denom, 1e-7);
}

float geometrySchlickGGX(float ndv, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	return ndv / (ndv * (1.0 - k) + k);
}

float geometrySmith(float ndv, float ndl, float roughness)
{
	return geometrySchlickGGX(ndv, roughness) * geometrySchlickGGX(ndl, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	vec3 fr = max(vec3_splat(1.0 - roughness), F0);
	return F0 + (fr - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 perturbNormal(vec3 N, vec3 posW, vec2 uv, vec3 mapN)
{
	vec3 dp1 = dFdx(posW);
	vec3 dp2 = dFdy(posW);
	vec2 duv1 = dFdx(uv);
	vec2 duv2 = dFdy(uv);
	vec3 dp2perp = cross(dp2, N);
	vec3 dp1perp = cross(N, dp1);
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
	float invMax = inversesqrt(max(dot(T, T), dot(B, B)));
	T *= invMax;
	B *= invMax;
	return normalize(T * mapN.x + B * mapN.y + N * mapN.z);
}

float sampleShadowDepth(vec2 uv, int cascade)
{
	if (cascade == 0) { return texture2D(s_shadowMap0, uv).r; }
	if (cascade == 1) { return texture2D(s_shadowMap1, uv).r; }
	return texture2D(s_shadowMap2, uv).r;
}

float sampleShadowBilinear(vec2 baseUv, float baseRefDepth, vec2 depthGradient,
	float texel, int cascade)
{
	vec2 uvT = baseUv / texel - 0.5;
	vec2 fl = floor(uvT);
	vec2 f = uvT - fl;
	vec2 c = (fl + 0.5) * texel;
	vec2 uv00 = c;
	vec2 uv10 = c + vec2(texel, 0.0);
	vec2 uv01 = c + vec2(0.0, texel);
	vec2 uv11 = c + vec2(texel, texel);
	float r00 = baseRefDepth + dot(depthGradient, uv00 - baseUv);
	float r10 = baseRefDepth + dot(depthGradient, uv10 - baseUv);
	float r01 = baseRefDepth + dot(depthGradient, uv01 - baseUv);
	float r11 = baseRefDepth + dot(depthGradient, uv11 - baseUv);
	float s00 = (sampleShadowDepth(uv00, cascade) >= r00) ? 1.0 : 0.0;
	float s10 = (sampleShadowDepth(uv10, cascade) >= r10) ? 1.0 : 0.0;
	float s01 = (sampleShadowDepth(uv01, cascade) >= r01) ? 1.0 : 0.0;
	float s11 = (sampleShadowDepth(uv11, cascade) >= r11) ? 1.0 : 0.0;
	return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}

// Finite-size local light. Avoid pure 1/r² on untextured flats — that is the
// main source of isophote "波纹/条纹" (looks like ridges when posterised).
// Use a smooth window * soft inverse-square with a large floor so mid-range
// gradients stay gentle under ACES + 8-bit output.
float localLightAttenuation(float dist, float range, float sourceRadius)
{
	float radius = max(sourceRadius, 1e-3);
	float d = max(dist, radius);
	// Soft falloff: closer to 1/(1 + (d/r)^2) than pure 1/d².
	float t = d / radius;
	float invSoft = 1.0 / (1.0 + t * t);
	float nd = clamp(dist / max(range, 1e-4), 0.0, 1.0);
	// Smoothstep window to zero at range (no hard circle).
	float w = 1.0 - nd * nd * (3.0 - 2.0 * nd);
	w = w * w;
	return invSoft * w;
}

// Stable two-ring blocker search. The previous irregular 8-point set summed
// to (0.2195,-0.5427), biasing blocker depth and therefore PCSS softness along
// one shadow-map axis. These exact opposite/D4 pairs keep the same RMS radius
// (~0.463) and texture-read cost while removing that directional drift.
void shadowPoisson8(out vec2 o0, out vec2 o1, out vec2 o2, out vec2 o3,
                    out vec2 o4, out vec2 o5, out vec2 o6, out vec2 o7)
{
	o0 = vec2( 0.250000,  0.000000);
	o1 = vec2( 0.000000,  0.250000);
	o2 = vec2(-0.250000,  0.000000);
	o3 = vec2( 0.000000, -0.250000);
	o4 = vec2( 0.427664,  0.427664);
	o5 = vec2(-0.427664,  0.427664);
	o6 = vec2(-0.427664, -0.427664);
	o7 = vec2( 0.427664, -0.427664);
}

// Stable two-ring disk quadrature for final visibility. Exact opposite pairs
// and 45-degree rotational symmetry remove the directional bias a small fixed
// Vogel sequence exposes as horizontal steps on diagonal shadow silhouettes.
// Its mean squared radius remains 0.5, so this changes angular balance rather
// than making every shadow blurrier or adding screen-space temporal noise.
void shadowDisk16(out vec2 o0, out vec2 o1, out vec2 o2, out vec2 o3,
                  out vec2 o4, out vec2 o5, out vec2 o6, out vec2 o7,
                  out vec2 o8, out vec2 o9, out vec2 o10, out vec2 o11,
                  out vec2 o12, out vec2 o13, out vec2 o14, out vec2 o15)
{
	o0 = vec2( 0.500000,  0.000000);
	o1 = vec2( 0.353553,  0.353553);
	o2 = vec2( 0.000000,  0.500000);
	o3 = vec2(-0.353553,  0.353553);
	o4 = vec2(-0.500000,  0.000000);
	o5 = vec2(-0.353553, -0.353553);
	o6 = vec2( 0.000000, -0.500000);
	o7 = vec2( 0.353553, -0.353553);
	o8  = vec2( 0.800103,  0.331414);
	o9  = vec2( 0.331414,  0.800103);
	o10 = vec2(-0.331414,  0.800103);
	o11 = vec2(-0.800103,  0.331414);
	o12 = vec2(-0.800103, -0.331414);
	o13 = vec2(-0.331414, -0.800103);
	o14 = vec2( 0.331414, -0.800103);
	o15 = vec2( 0.800103, -0.331414);
}

float computeCascadeShadow(vec3 wpos, vec3 wposDx, vec3 wposDy,
	vec3 normal, float slope, int cascade)
{
	vec3 lightTravel = normalize(u_shadowLightDir.xyz);
	float normalBias = (cascade == 0) ? u_shadowNormalBias.x
		: (cascade == 1) ? u_shadowNormalBias.y : u_shadowNormalBias.z;
	vec3 normalOffset = normal * normalBias * (1.0 - abs(dot(normal, lightTravel)));
	vec3 biased = wpos + normalOffset;

	vec4 clipH = mul(u_lightViewProj[cascade], vec4(biased, 1.0));
	vec4 clipDxH = mul(u_lightViewProj[cascade], vec4(wposDx, 0.0));
	vec4 clipDyH = mul(u_lightViewProj[cascade], vec4(wposDy, 0.0));
	float invW2 = 1.0 / max(clipH.w * clipH.w, 1e-8);
	vec3 clipDx = (clipDxH.xyz * clipH.w - clipH.xyz * clipDxH.w) * invW2;
	vec3 clipDy = (clipDyH.xyz * clipH.w - clipH.xyz * clipDyH.w) * invW2;
	vec4 clip = vec4(clipH.xyz / clipH.w, 1.0);
	if (u_shadowProjection.x > 0.5)
	{
		clip.z = clip.z * 0.5 + 0.5;
		clipDx.z *= 0.5;
		clipDy.z *= 0.5;
	}
	if (clip.x < -1.0 || clip.x > 1.0 || clip.y < -1.0 || clip.y > 1.0
	||  clip.z < 0.0 || clip.z > 1.0) { return 1.0; }

	vec2 uv = clip.xy * 0.5 + 0.5;
	uv.y = mix(uv.y, 1.0 - uv.y, u_texFlags.y);
	vec2 uvDx = clipDx.xy * 0.5;
	vec2 uvDy = clipDy.xy * 0.5;
	uvDx.y = mix(uvDx.y, -uvDx.y, u_texFlags.y);
	uvDy.y = mix(uvDy.y, -uvDy.y, u_texFlags.y);
	float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
	vec2 depthGradient = vec2_splat(0.0);
	if (abs(determinant) > 1e-10)
	{
		depthGradient.x = (clipDx.z * uvDy.y - clipDy.z * uvDx.y) / determinant;
		depthGradient.y = (uvDx.x * clipDy.z - uvDy.x * clipDx.z) / determinant;
		// Degenerate grazing projections can amplify numerical noise. This bound
		// still permits 0.04 depth change across the widest current PCSS kernel.
		depthGradient = clamp(depthGradient, vec2_splat(-4.0), vec2_splat(4.0));
	}
	float texel = u_shadowParams.z;
	float refDepth = clip.z - u_shadowParams.x * (1.0 + 3.0 * slope);

	vec2 p0, p1, p2, p3, p4, p5, p6, p7;
	shadowPoisson8(p0, p1, p2, p3, p4, p5, p6, p7);

	float searchR = u_shadowFilter.x * texel;
	float blockerSeparation = 0.0;
	float blockerCount = 0.0;
	{
		vec2 o;
		o = p0 * searchR;
		{ float r = refDepth + dot(depthGradient, o); float s = sampleShadowDepth(uv + o, cascade); if (s < r) { blockerSeparation += r - s; blockerCount += 1.0; } }
		o = p1 * searchR;
		{ float r = refDepth + dot(depthGradient, o); float s = sampleShadowDepth(uv + o, cascade); if (s < r) { blockerSeparation += r - s; blockerCount += 1.0; } }
		o = p2 * searchR;
		{ float r = refDepth + dot(depthGradient, o); float s = sampleShadowDepth(uv + o, cascade); if (s < r) { blockerSeparation += r - s; blockerCount += 1.0; } }
		o = p3 * searchR;
		{ float r = refDepth + dot(depthGradient, o); float s = sampleShadowDepth(uv + o, cascade); if (s < r) { blockerSeparation += r - s; blockerCount += 1.0; } }
		o = p4 * searchR;
		{ float r = refDepth + dot(depthGradient, o); float s = sampleShadowDepth(uv + o, cascade); if (s < r) { blockerSeparation += r - s; blockerCount += 1.0; } }
		o = p5 * searchR;
		{ float r = refDepth + dot(depthGradient, o); float s = sampleShadowDepth(uv + o, cascade); if (s < r) { blockerSeparation += r - s; blockerCount += 1.0; } }
		o = p6 * searchR;
		{ float r = refDepth + dot(depthGradient, o); float s = sampleShadowDepth(uv + o, cascade); if (s < r) { blockerSeparation += r - s; blockerCount += 1.0; } }
		o = p7 * searchR;
		{ float r = refDepth + dot(depthGradient, o); float s = sampleShadowDepth(uv + o, cascade); if (s < r) { blockerSeparation += r - s; blockerCount += 1.0; } }
	}
	if (blockerCount < 0.5) { return 1.0; }

	float separation = blockerSeparation / blockerCount;
	float penumbraScale = (cascade == 0) ? u_shadowPenumbra.x
		: (cascade == 1) ? u_shadowPenumbra.y : u_shadowPenumbra.z;
	float radius = clamp(u_shadowFilter.y + separation * penumbraScale,
		u_shadowFilter.y, u_shadowFilter.z);
	float filterR = radius * texel;

	vec2 f0, f1, f2, f3, f4, f5, f6, f7;
	vec2 f8, f9, f10, f11, f12, f13, f14, f15;
	shadowDisk16(f0, f1, f2, f3, f4, f5, f6, f7,
		f8, f9, f10, f11, f12, f13, f14, f15);

	float sum = 0.0;
	{
		vec2 o;
		o = f0 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f1 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f2 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f3 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f4 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f5 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f6 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f7 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f8 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f9 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f10 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f11 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f12 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f13 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f14 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
		o = f15 * filterR;
		sum += sampleShadowBilinear(uv + o, refDepth + dot(depthGradient, o), depthGradient, texel, cascade);
	}
	return sum * (1.0 / 16.0);
}

// Stable CSM selection with a cross-fade band before each split. The far map
// overlaps the near interval, hiding resolution changes without temporal noise.
float computeShadow(vec3 wpos, vec3 normal)
{
	vec3 wposDx = dFdx(wpos);
	vec3 wposDy = dFdy(wpos);
	if (u_shadowParams.w < 0.5) { return 1.0; }
	vec3 L = normalize(-u_shadowLightDir.xyz);
	float ndl = dot(normal, L);
	if (ndl <= 0.0) { return 1.0; }
	float slope = clamp(1.0 - ndl, 0.0, 1.0);
	float viewDepth = mul(u_shadowCameraView, vec4(wpos, 1.0)).z;
	if (viewDepth <= 0.0 || viewDepth >= u_shadowCascadeSplits.z) { return 1.0; }
	int cascade = (viewDepth <= u_shadowCascadeSplits.x) ? 0
		: (viewDepth <= u_shadowCascadeSplits.y) ? 1 : 2;
	float visibility = computeCascadeShadow(wpos, wposDx, wposDy, normal, slope, cascade);
	if (cascade < 2)
	{
		float split = (cascade == 0) ? u_shadowCascadeSplits.x : u_shadowCascadeSplits.y;
		float blendWidth = (cascade == 0) ? u_shadowCascadeBlend.x : u_shadowCascadeBlend.y;
		float blend = clamp((viewDepth - (split - blendWidth)) / max(blendWidth, 1e-4), 0.0, 1.0);
		if (blend > 0.0)
		{
			float nextVisibility = computeCascadeShadow(wpos, wposDx, wposDy, normal, slope, cascade + 1);
			blend = blend * blend * (3.0 - 2.0 * blend);
			visibility = mix(visibility, nextVisibility, blend);
		}
	}
	else
	{
		float fadeWidth = max(u_shadowCascadeBlend.z, 1e-4);
		float fade = clamp((viewDepth - (u_shadowCascadeSplits.z - fadeWidth)) / fadeWidth, 0.0, 1.0);
		fade = fade * fade * (3.0 - 2.0 * fade);
		visibility = mix(visibility, 1.0, fade);
	}
	return visibility;
}

// ---- Forward+ clustered lighting (constants mirror Concord::ClusterGrid) ----
SAMPLER2D(s_lightData, 9);      // 4 texels/light: posType, dirRange, colorLinear+intensity, spot
SAMPLER2D(s_clusterRanges, 10); // per-cluster (offset, count) in R,G
SAMPLER2D(s_lightIndices, 11);  // flat local-light index list (R)
uniform vec4 u_clusterParams;   // x mode(0 classic,1 clustered), y dirCount, z near, w far

#define CLUSTER_DIM_X 16
#define CLUSTER_DIM_Y 9
#define CLUSTER_DIM_Z 24
#define CLUSTER_INDEX_TEX_W 1024
#define CLUSTER_MAX_ITER 64

int ClusterSlice(float viewZ, float nearP, float farP)
{
	float z = max(viewZ, nearP);
	float lg = log(farP / nearP);
	if (lg <= 0.0) { return 0; }
	int s = int(log(z / nearP) / lg * float(CLUSTER_DIM_Z));
	return clamp(s, 0, CLUSTER_DIM_Z - 1);
}

// One light's PBR contribution — identical math to the classic loop, factored
// so the clustered path reuses it. `lcolor` is already linear; `vis` is the
// shadow/1.0 visibility multiplier.
vec3 ShadeLight(float type, vec3 lpos, vec3 ldir, float lrange, vec3 lcolor, float lintensity,
	float cosInner, float cosOuter, float srcRadius,
	vec3 N, vec3 V, vec3 wpos, vec3 albedo, float metallic, float roughness, vec3 F0, float ndv, float vis)
{
	vec3 radiance = lcolor * lintensity;
	vec3 L;
	float isLocal = 0.0;
	if (type < 0.5)
	{
		L = normalize(-ldir);
	}
	else
	{
		vec3 toLight = lpos - wpos;
		float dist = length(toLight);
		L = toLight / max(dist, 1e-4);
		float srcR = srcRadius > 1e-4 ? srcRadius : 0.4;
		radiance *= localLightAttenuation(dist, lrange, srcR);
		isLocal = 1.0;
		if (type > 1.5)
		{
			float cosA = dot(normalize(ldir), -L);
			float cone = clamp((cosA - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
			cone = cone * cone * (3.0 - 2.0 * cone);
			radiance *= cone;
		}
	}
	float ndl = max(dot(N, L), 0.0);
	if (ndl <= 1e-5) { return vec3_splat(0.0); }
	vec3 H = normalize(V + L);
	float ndh = max(dot(N, H), 0.0);
	float hdv = max(dot(H, V), 0.0);
	float ndlSpec = max(dot(N, L), 1e-4);
	float D = distributionGGX(ndh, roughness);
	float G = geometrySmith(ndv, ndlSpec, roughness);
	vec3  Fr = fresnelSchlick(hdv, F0);
	vec3 specular = (D * G * Fr) / max(4.0 * ndv * ndlSpec, 1e-4);
	if (isLocal > 0.5) { specular = clamp(specular, vec3_splat(0.0), vec3_splat(2.5)); }
	vec3 kd = (vec3_splat(1.0) - Fr) * (1.0 - metallic);
	float diffWeight = (isLocal > 0.5) ? 0.35 : 1.0;
	float specWeight = (isLocal > 0.5) ? (0.05 + 0.95 * metallic) : 1.0;
	return (kd * albedo / PI * diffWeight + specular * specWeight) * radiance * ndl * vis;
}

void main()
{
	if (dot(u_clipPlane.xyz, u_clipPlane.xyz) > 0.5
		&& dot(u_clipPlane.xyz, v_wpos) + u_clipPlane.w < 0.0)
	{
		discard;
	}
	float metallic  = clamp(u_matParams.x, 0.0, 1.0);
	float roughness = clamp(u_matParams.y, 0.04, 1.0);
	float lit       = u_matParams.z;
	float gradCode  = u_matParams.w;
	float reflectivity = clamp(u_reflectionFlags.y, 0.0, 1.0);

	vec4 base = u_albedo;
	if (gradCode > 0.5)
	{
		float coord = (gradCode < 1.5) ? v_lpos.x
		            : (gradCode < 2.5) ? v_lpos.y
		            :                    v_lpos.z;
		float t = clamp(coord * 0.5 + 0.5, 0.0, 1.0);
		base = mix(u_albedo, u_gradientTo, t);
	}

	vec2 uv = v_texcoord0;
	vec3 albedoTex = toLinear3(texture2D(s_albedo, uv).rgb);
	vec4 mrTex = texture2D(s_metallicRoughness, uv);
	vec3 emissiveTex = toLinear3(texture2D(s_emissive, uv).rgb);

	metallic  = clamp(metallic * mrTex.b, 0.0, 1.0);
	roughness = clamp(roughness * mrTex.g, 0.04, 1.0);

	vec3 outColor;
	if (lit > 0.5)
	{
		vec3 planarDisplay = vec3_splat(0.0);
		float planarWeight = 0.0;
		vec3 albedo = toLinear3(base.rgb) * albedoTex;
		vec3 authoredNormal = normalize(v_wnormal);
		vec3 geometricNormal = authoredNormal;
		vec3 V = normalize(u_camPos.xyz - v_wpos);
		if (dot(geometricNormal, V) < 0.0) { geometricNormal = -geometricNormal; }
		vec3 N = geometricNormal;
		if (u_texFlags.x > 0.5)
		{
			vec3 mapN = texture2D(s_normal, uv).xyz * 2.0 - 1.0;
			N = perturbNormal(N, v_wpos, uv, mapN);
		}

		// Geometric specular AA: lifts roughness where normals vary across the
		// pixel. Cap is kept modest so large flat walls don't pick up noisy
		// micro-roughness that reads as hatching under specular.
		vec3 dNdx = dFdx(N);
		vec3 dNdy = dFdy(N);
		float normalVariance = dot(dNdx, dNdx) + dot(dNdy, dNdy);
		roughness = sqrt(clamp(roughness * roughness + min(0.35 * normalVariance, 0.18), 0.0, 1.0));

		float ndv = max(dot(N, V), 1e-4);
		vec3 F0 = mix(vec3_splat(0.04), albedo, metallic);

		// Shadow receiver bias must not flip when the camera crosses a two-sided
		// surface. Orient the authored normal toward the shadow light instead;
		// shading can remain view-facing without making the shadow camera-dependent.
		vec3 shadowL = normalize(-u_shadowLightDir.xyz);
		vec3 shadowNormal = dot(authoredNormal, shadowL) < 0.0 ? -authoredNormal : authoredNormal;
		float shadowVisibility = computeShadow(v_wpos, shadowNormal);
		float shadowCasterIndex = u_shadowParams.w - 1.0;

		vec3 Lo = vec3_splat(0.0);
		if (u_clusterParams.x > 0.5)
		{
			// ===== Forward+ clustered path =====
			int dirCount = int(u_clusterParams.y);
			for (int d = 0; d < dirCount; ++d)
			{
				vec4 pt = texelFetch(s_lightData, ivec2(0, d), 0);
				vec4 dr = texelFetch(s_lightData, ivec2(1, d), 0);
				vec4 cl = texelFetch(s_lightData, ivec2(2, d), 0);
				vec4 sp = texelFetch(s_lightData, ivec2(3, d), 0);
				Lo += ShadeLight(pt.w, pt.xyz, dr.xyz, dr.w, cl.rgb, cl.w, sp.x, sp.y, sp.z,
					N, V, v_wpos, albedo, metallic, roughness, F0, ndv, shadowVisibility);
			}
			vec4 clip = mul(u_viewProj, vec4(v_wpos, 1.0));
			vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
			vec4 vpos = mul(u_view, vec4(v_wpos, 1.0));
			int tx = clamp(int(uv.x * float(CLUSTER_DIM_X)), 0, CLUSTER_DIM_X - 1);
			int ty = clamp(int(uv.y * float(CLUSTER_DIM_Y)), 0, CLUSTER_DIM_Y - 1);
			int slice = ClusterSlice(vpos.z, u_clusterParams.z, u_clusterParams.w);
			vec4 rng = texelFetch(s_clusterRanges, ivec2(ty * CLUSTER_DIM_X + tx, slice), 0);
			int off = int(rng.x + 0.5);
			int cnt = int(rng.y + 0.5);
			for (int k = 0; k < CLUSTER_MAX_ITER; ++k)
			{
				if (k >= cnt) { break; }
				int flatIdx = off + k;
				int li = int(texelFetch(s_lightIndices,
					ivec2(flatIdx % CLUSTER_INDEX_TEX_W, flatIdx / CLUSTER_INDEX_TEX_W), 0).x + 0.5);
				vec4 pt = texelFetch(s_lightData, ivec2(0, li), 0);
				vec4 dr = texelFetch(s_lightData, ivec2(1, li), 0);
				vec4 cl = texelFetch(s_lightData, ivec2(2, li), 0);
				vec4 sp = texelFetch(s_lightData, ivec2(3, li), 0);
				Lo += ShadeLight(pt.w, pt.xyz, dr.xyz, dr.w, cl.rgb, cl.w, sp.x, sp.y, sp.z,
					N, V, v_wpos, albedo, metallic, roughness, F0, ndv, 1.0);
			}
		}
		else
		{
		int count = int(u_ambient.w);
		for (int i = 0; i < MAX_LIGHTS; ++i)
		{
			if (i >= count) { break; }

			float type = u_lightPosType[i].w;
			vec3 radiance = toLinear3(u_lightColor[i].rgb) * u_lightColor[i].w;
			vec3 L;
			float isLocal = 0.0;

			if (type < 0.5)
			{
				L = normalize(-u_lightDirRange[i].xyz);
			}
			else
			{
				vec3 toLight = u_lightPosType[i].xyz - v_wpos;
				float dist = length(toLight);
				L = toLight / max(dist, 1e-4);
				// spot.z packs optional source radius (0 => default lamp size).
				float srcR = u_lightSpot[i].z > 1e-4 ? u_lightSpot[i].z : 0.4;
				radiance *= localLightAttenuation(dist, u_lightDirRange[i].w, srcR);
				isLocal = 1.0;

				if (type > 1.5)
				{
					float cosA = dot(normalize(u_lightDirRange[i].xyz), -L);
					float cosInner = u_lightSpot[i].x;
					float cosOuter = u_lightSpot[i].y;
					float cone = clamp((cosA - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
					cone = cone * cone * (3.0 - 2.0 * cone);
					radiance *= cone;
				}
			}

			float ndl = max(dot(N, L), 0.0);
			// No wrap for local lamps: wrap paints extra circular bands on walls.
			if (ndl <= 1e-5) { continue; }

			vec3 H = normalize(V + L);
			float ndh = max(dot(N, H), 0.0);
			float hdv = max(dot(H, V), 0.0);
			float ndlSpec = max(dot(N, L), 1e-4);

			float D = distributionGGX(ndh, roughness);
			float G = geometrySmith(ndv, ndlSpec, roughness);
			vec3  Fr = fresnelSchlick(hdv, F0);
			vec3 specular = (D * G * Fr) / max(4.0 * ndv * ndlSpec, 1e-4);
			if (isLocal > 0.5)
			{
				// Kill specular fireflies + ring highlights on smooth flats.
				specular = clamp(specular, vec3_splat(0.0), vec3_splat(2.5));
			}
			vec3 kd = (vec3_splat(1.0) - Fr) * (1.0 - metallic);

			// Local: weak diffuse only (directional carries the look). Specular
			// from point/spot on dielectrics paints ring "ridges" on flats.
			float diffWeight = (isLocal > 0.5) ? 0.35 : 1.0;
			float specWeight = (isLocal > 0.5) ? (0.05 + 0.95 * metallic) : 1.0;

			float visibility = 1.0;
			if (type < 0.5 && abs(float(i) - shadowCasterIndex) < 0.5)
			{
				visibility = shadowVisibility;
			}
			Lo += (kd * albedo / PI * diffWeight + specular * specWeight)
			    * radiance * ndl * visibility;
		}
		}

		// Hemisphere ambient fill (no circular isophotes).
		vec3 Famb = fresnelSchlickRoughness(ndv, F0, roughness);
		vec3 kdAmb = (vec3_splat(1.0) - Famb) * (1.0 - metallic);
		vec3 skyAmbient = toLinear3(u_ambient.rgb);
		vec3 groundAmbient = skyAmbient * vec3(1.18, 0.90, 0.68) + vec3(0.028, 0.020, 0.012);
		float hemi = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
		hemi = hemi * hemi * (3.0 - 2.0 * hemi);
		vec3 ambientLin = mix(groundAmbient, skyAmbient, hemi);
		float envSpec = 0.55 + 0.45 * metallic;
		float envDiff = 1.0 + 0.15 * (1.0 - roughness);
		vec3 ambient = ambientLin * (kdAmb * albedo * envDiff + Famb * envSpec);

		vec3 emissiveLin = toLinear3(u_emissive.rgb) * u_emissive.w * emissiveTex;
		outColor = ambient + Lo + emissiveLin;
		outColor *= 1.05;
		if (u_reflectionFlags.x > 0.5)
		{
			vec3 reflectionDir = normalize(reflect(-V, N));
			vec3 sampleDir = BoxProjectReflection(reflectionDir, v_wpos);
			vec3 environmentLinear = textureCubeLod(
				s_sceneReflection, sampleDir, 0.0).rgb;
			vec3 dielectricWeight = fresnelSchlickRoughness(ndv, F0, roughness)
				* 0.45 * (1.0 - roughness * 0.65);
			float polishedMetalWeight = 1.0 - roughness * 0.35;
			vec3 reflectionWeight = mix(
				dielectricWeight, vec3_splat(polishedMetalWeight), metallic);
			reflectionWeight = clamp(
				reflectionWeight * reflectivity, vec3_splat(0.0), vec3_splat(1.0));
			outColor = outColor * (vec3_splat(1.0) - reflectionWeight)
				+ environmentLinear * reflectionWeight;
		}

		// Planar reflection (industrial mirrors): the mirrored-camera target is
		// already display-referred, so defer its Fresnel blend until this surface
		// has passed through the same film curve.
		if (u_texFlags.w > 0.5)
		{
			vec4 reflClip = mul(u_planarViewProj, vec4(v_wpos, 1.0));
			if (reflClip.w > 1e-4)
			{
				vec2 reflUv = reflClip.xy / reflClip.w * 0.5 + 0.5;
				if (u_texFlags.y > 0.5)
				{
					// Match the render-target origin used by the mirrored-camera pass.
					reflUv.y = 1.0 - reflUv.y;
				}
				float edge = smoothstep(0.0, 0.04, reflUv.x) * smoothstep(0.0, 0.04, reflUv.y)
					* smoothstep(0.0, 0.04, 1.0 - reflUv.x) * smoothstep(0.0, 0.04, 1.0 - reflUv.y);
				if (edge > 0.01 && reflUv.x > 0.0 && reflUv.x < 1.0 && reflUv.y > 0.0 && reflUv.y < 1.0)
				{
					planarDisplay = texture2D(s_planarReflection, reflUv).rgb;
					float fresnel = F0.x + (1.0 - F0.x) * pow(1.0 - ndv, 5.0);
					planarWeight = clamp(fresnel * mix(0.35, 1.0, metallic)
						* (1.0 - roughness * 0.55) * edge * reflectivity, 0.0, 1.0);
				}
			}
		}

		if (u_outputFlags.x < 0.5)
		{
			outColor = acesFilm(outColor);
			outColor = toGamma3(outColor);
			outColor = mix(outColor, planarDisplay, planarWeight);
		}
	}
	else
	{
		vec3 albedoSrgb = texture2D(s_albedo, uv).rgb;
		vec3 emissiveSrgb = texture2D(s_emissive, uv).rgb;
		outColor = base.rgb * albedoSrgb + u_emissive.rgb * u_emissive.w * emissiveSrgb;
		if (u_outputFlags.x > 0.5)
		{
			outColor = toLinear3(outColor);
		}
		else if (u_texFlags.w > 0.5)
		{
			vec4 reflClip = mul(u_planarViewProj, vec4(v_wpos, 1.0));
			if (reflClip.w > 1e-4)
			{
				vec2 reflUv = reflClip.xy / reflClip.w * 0.5 + 0.5;
				if (u_texFlags.y > 0.5)
				{
					reflUv.y = 1.0 - reflUv.y;
				}
				if (reflUv.x > 0.0 && reflUv.x < 1.0 && reflUv.y > 0.0 && reflUv.y < 1.0)
				{
					vec3 reflDisplay = texture2D(s_planarReflection, reflUv).rgb;
					outColor = mix(outColor, reflDisplay, 0.85 * reflectivity);
				}
			}
		}
	}

	gl_FragColor = vec4(outColor, base.a);
}
