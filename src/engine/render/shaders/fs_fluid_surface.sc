$input v_worldPos, v_normal

#include <bgfx_shader.sh>

SAMPLER2D(s_sceneColor, 0);
SAMPLER2D(s_sceneDepth, 1);
SAMPLER3D(s_field, 2);

uniform vec4 u_fluidEye;      // xyz world eye, w = near plane
uniform vec4 u_fluidScreen;   // x width, y height, z far, w = flipV
uniform vec4 u_fluidSunDir;   // xyz toward sun, w = glint strength
uniform vec4 u_fluidSunColor; // rgb
uniform vec4 u_fluidSkyZenith;  // rgb
uniform vec4 u_fluidSkyHorizon; // rgb
uniform vec4 u_fluidOptics;   // x ior, y absorption, z roughness, w iso
uniform vec4 u_fluidColor;    // rgb body tint (linear), w max scene distance
uniform vec4 u_fluidField0;   // xyz local field origin, w = field cell size
uniform vec4 u_fluidField1;   // xyz field dims (cells), w = march steps
uniform mat4 u_fluidInvModel;
uniform mat4 u_fluidModel;

/*
 * Dual-interface refraction through the reconstructed water volume.
 *
 * This is deliberately NOT a screen-space UV offset. At every surface pixel
 * the view ray is refracted by Snell's law at the entry interface (air ->
 * water), marched through the very density field the surface mesh was cut
 * from until it leaves the liquid, and refracted again at the exit interface
 * (water -> air, with total internal reflection producing up to two extra
 * bounces inside the volume). Only the final ray is resolved against the
 * scene — through a depth-guided ray march, not a distortion fudge — which
 * is what produces the convex-lens magnification/warping of a curved body
 * of water. Beer-Lambert absorption accumulates along the true in-volume
 * path length.
 */

vec3 FieldUvw(vec3 localPos)
{
	vec3 extent = u_fluidField1.xyz * u_fluidField0.w;
	return (localPos - u_fluidField0.xyz) / extent;
}

float FieldAt(vec3 localPos)
{
	vec3 uvw = FieldUvw(localPos);
	if (any(lessThan(uvw, vec3_splat(0.0))) || any(greaterThan(uvw, vec3_splat(1.0))))
	{
		return 0.0;
	}
	return texture3DLod(s_field, uvw, 0.0).r;
}

vec3 FieldGradientLocal(vec3 localPos)
{
	float e = u_fluidField0.w;
	return vec3(FieldAt(localPos + vec3(e, 0.0, 0.0)) - FieldAt(localPos - vec3(e, 0.0, 0.0)),
	            FieldAt(localPos + vec3(0.0, e, 0.0)) - FieldAt(localPos - vec3(0.0, e, 0.0)),
	            FieldAt(localPos + vec3(0.0, 0.0, e)) - FieldAt(localPos - vec3(0.0, 0.0, e)));
}

/* March `dir` from `from` until the field drops below iso; false on AABB escape. */
bool MarchToExit(vec3 from, vec3 dir, float iso, out vec3 exitPos)
{
	float stepLen = u_fluidField0.w * 0.9;
	int steps = int(u_fluidField1.w);
	vec3 prev = from;
	float prevValue = FieldAt(prev);
	for (int i = 0; i < 32; ++i)
	{
		if (i >= steps) { break; }
		vec3 cur = prev + dir * stepLen;
		vec3 uvw = FieldUvw(cur);
		if (any(lessThan(uvw, vec3_splat(0.0))) || any(greaterThan(uvw, vec3_splat(1.0))))
		{
			return false;
		}
		float value = FieldAt(cur);
		if (prevValue >= iso && value < iso)
		{
			// Bisect the crossing for a stable exit point.
			vec3 a = prev;
			vec3 b = cur;
			for (int k = 0; k < 5; ++k)
			{
				vec3 mid = (a + b) * 0.5;
				if (FieldAt(mid) >= iso) { a = mid; } else { b = mid; }
			}
			exitPos = (a + b) * 0.5;
			return true;
		}
		prev = cur;
		prevValue = value;
	}
	return false;
}

float LinearDepth(float deviceZ, float nearPlane, float farPlane)
{
	return nearPlane * farPlane / (farPlane - deviceZ * (farPlane - nearPlane));
}

vec3 SkyApprox(vec3 dir)
{
	vec3 sky = mix(u_fluidSkyHorizon.rgb, u_fluidSkyZenith.rgb,
	               clamp(dir.y * 1.4 + 0.25, 0.0, 1.0));
	float sunAmount = pow(clamp(dot(dir, u_fluidSunDir.xyz), 0.0, 1.0), 350.0);
	return sky + u_fluidSunColor.rgb * sunAmount * 2.0;
}

/* Depth-guided march of the final (twice-refracted) ray against the scene. */
vec3 ResolveScene(vec3 from, vec3 dir)
{
	float nearPlane = u_fluidEye.w;
	float farPlane = u_fluidScreen.z;
	float maxDist = u_fluidColor.w;
	float stepLen = maxDist / 12.0;
	bool flipV = u_fluidScreen.w > 0.5;
	float prevT = 0.02;
	for (int i = 1; i <= 12; ++i)
	{
		float t = stepLen * float(i);
		vec3 p = from + dir * t;
		vec4 clip = mul(u_viewProj, vec4(p, 1.0));
		if (clip.w <= 0.0) { prevT = t; continue; }
		vec3 ndc = clip.xyz / clip.w;
		vec2 uv = ndc.xy * 0.5 + 0.5;
		if (flipV) { uv.y = 1.0 - uv.y; }
		if (any(lessThan(uv, vec2_splat(0.0))) || any(greaterThan(uv, vec2_splat(1.0))))
		{
			prevT = t;
			continue;
		}
		float sceneZ = LinearDepth(texture2DLod(s_sceneDepth, uv, 0.0).r, nearPlane, farPlane);
		float rayZ = mul(u_view, vec4(p, 1.0)).z;
		if (rayZ <= sceneZ + 0.02 * t && rayZ > 0.0)
		{
			float hitT = t;
			for (int k = 0; k < 2; ++k)
			{
				float mid = (prevT + hitT) * 0.5;
				vec3 mp = from + dir * mid;
				vec4 mc = mul(u_viewProj, vec4(mp, 1.0));
				if (mc.w <= 0.0) { break; }
				vec2 muv = mc.xy / mc.w * 0.5 + 0.5;
				if (flipV) { muv.y = 1.0 - muv.y; }
				float mz = LinearDepth(texture2DLod(s_sceneDepth, muv, 0.0).r,
				                       nearPlane, farPlane);
				if (mul(u_view, vec4(mp, 1.0)).z <= mz + 0.02 * mid) { hitT = mid; }
				else { prevT = mid; }
			}
			vec3 hp = from + dir * hitT;
			vec4 hc = mul(u_viewProj, vec4(hp, 1.0));
			vec2 huv = hc.xy / hc.w * 0.5 + 0.5;
			if (flipV) { huv.y = 1.0 - huv.y; }
			return texture2DLod(s_sceneColor, huv, 0.0).rgb;
		}
		prevT = t;
	}
	return SkyApprox(dir);
}

void main()
{
	float ior = u_fluidOptics.x;
	float iso = u_fluidOptics.w;
	vec3 eye = u_fluidEye.xyz;
	vec3 V = normalize(v_worldPos - eye);
	vec3 N1 = normalize(v_normal);

	// Work in the fluid's local frame, where the density field lives.
	vec3 entryL = mul(u_fluidInvModel, vec4(v_worldPos, 1.0)).xyz;
	vec3 dirL = normalize(mul(u_fluidInvModel, vec4(V, 0.0)).xyz);

	// Camera inside the medium: the ray is already in the liquid at entry.
	float behind = FieldAt(entryL - dirL * u_fluidField0.w);
	bool inside = behind > iso;

	vec3 rayL = dirL;
	float fresnel = 0.0;
	if (!inside)
	{
		vec3 n = dot(N1, V) < 0.0 ? N1 : -N1;
		float cosI = clamp(-dot(V, n), 0.0, 1.0);
		fresnel = 0.02 + 0.98 * pow(1.0 - cosI, 5.0);
		vec3 r = refract(V, n, 1.0 / ior);
		if (dot(r, r) > 1.0e-8)
		{
			rayL = normalize(mul(u_fluidInvModel, vec4(normalize(r), 0.0)).xyz);
		}
	}

	// Travel the volume: exit interface + Snell again, with TIR bounces.
	vec3 curL = entryL + rayL * (u_fluidField0.w * 0.5);
	vec3 exitW = v_worldPos;
	vec3 outDirW = normalize(refract(V, dot(N1, V) < 0.0 ? N1 : -N1, 1.0 / max(ior, 1.0)));
	if (dot(outDirW, outDirW) <= 1.0e-8)
	{
		outDirW = normalize(reflect(V, dot(N1, V) < 0.0 ? N1 : -N1));
	}
	float thickness = 0.0;
	bool resolved = false;
	for (int bounce = 0; bounce < 2 && !resolved; ++bounce)
	{
		vec3 exitL;
		if (!MarchToExit(curL, rayL, iso, exitL))
		{
			// Never crossed the surface again (thin feature): leave as-is.
			exitW = mul(u_fluidModel, vec4(curL, 1.0)).xyz;
			outDirW = normalize(mul(u_fluidModel, vec4(rayL, 0.0)).xyz);
			resolved = true;
			break;
		}
		thickness += length(exitL - curL);
		vec3 gradL = FieldGradientLocal(exitL);
		vec3 nL = length(gradL) > 1.0e-5 ? normalize(gradL) : vec3(0.0, 1.0, 0.0);
		// nL points into the liquid (gradient ascent); flip to face the ray.
		vec3 nW = normalize(mul(u_fluidModel, vec4(nL, 0.0)).xyz);
		vec3 dirW = normalize(mul(u_fluidModel, vec4(rayL, 0.0)).xyz);
		vec3 facingW = dot(nW, dirW) > 0.0 ? -nW : nW;
		vec3 refractedW = refract(dirW, facingW, ior);
		if (dot(refractedW, refractedW) > 1.0e-8)
		{
			exitW = mul(u_fluidModel, vec4(exitL, 1.0)).xyz;
			outDirW = normalize(refractedW);
			resolved = true;
			break;
		}
		// Total internal reflection: keep marching inside the volume.
		vec3 facingL = dot(nL, rayL) > 0.0 ? -nL : nL;
		rayL = reflect(rayL, facingL);
		curL = exitL + rayL * (u_fluidField0.w * 1.5);
	}

	vec3 sceneColor = ResolveScene(exitW, outDirW);

	// Beer-Lambert along the true in-volume path, tinted by the body colour.
	float transmit = exp(-u_fluidOptics.y * thickness);
	vec3 tint = u_fluidColor.rgb;
	vec3 absorbed = sceneColor * (tint * transmit)
	    + tint * (1.0 - transmit) * 0.35
	       * (u_fluidSkyZenith.rgb + u_fluidSkyHorizon.rgb);

	// Entry-interface reflection (Schlick) + sun glint.
	vec3 reflDir = reflect(V, dot(N1, V) < 0.0 ? N1 : -N1);
	vec3 reflection = SkyApprox(reflDir);
	float shininess = exp2(10.0 * (1.0 - u_fluidOptics.z)) + 2.0;
	float glint = pow(clamp(dot(reflDir, u_fluidSunDir.xyz), 0.0, 1.0), shininess)
	    * u_fluidSunDir.w;
	vec3 color = mix(absorbed, reflection, fresnel) + u_fluidSunColor.rgb * glint;

	gl_FragColor = vec4(color, 1.0);
}
