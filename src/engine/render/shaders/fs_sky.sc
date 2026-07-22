$input v_skyDirection

#include <bgfx_shader.sh>

uniform vec4 u_skySolid;
uniform vec4 u_skyZenith;
uniform vec4 u_skyHorizon;
uniform vec4 u_skyGround;
// x: procedural, y: intensity, z: horizon falloff, w: linear HDR output.
uniform vec4 u_skyParams;
// x: sun disk enabled, y: disk intensity, z: directional sun present.
uniform vec4 u_skyOptions;
// xyz: light travel direction, w: apparent angular radius in degrees.
uniform vec4 u_skySunDirection;
// rgb: sun color in sRGB, w: light intensity.
uniform vec4 u_skySunColor;
uniform vec4 u_skyCamera;
// x: enabled, y: base height, z: thickness, w: density.
uniform vec4 u_cloudLayer;
// x: coverage, y: world scale, z: erosion, w: detail.
uniform vec4 u_cloudShape;
// xy: world offset, z: silver lining, w: fire emission.
uniform vec4 u_cloudMotion;
uniform vec4 u_cloudLit;
uniform vec4 u_cloudShadow;
uniform vec4 u_cloudFire;
// x: enabled, y: density, z: base height, w: height falloff.
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

vec3 SkyToLinear(vec3 color)
{
	return pow(max(color, vec3_splat(0.0)), vec3_splat(2.2));
}

vec3 SkyToDisplay(vec3 color)
{
	color = clamp((color * (2.51 * color + 0.03))
		/ (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
	return pow(color, vec3_splat(1.0 / 2.2));
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

// The inline sky clouds share the exact density model and march the independent
// volumetric cloud pass (fs_volcloud) uses, so clouds reflected in the mirror
// sphere / planar reflections match the real clouds the main scene draws. The
// only difference is the sky path is a pure background (no scene-depth
// truncation), whereas the volumetric pass depth-composites against geometry.
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
	float scale = max(u_cloudShape.y, 1.0);
	vec3 samplePoint = vec3(
		(samplePosition.x + u_cloudMotion.x) / scale,
		(samplePosition.y - u_cloudLayer.y) / max(u_cloudLayer.z, 1.0),
		(samplePosition.z + u_cloudMotion.y) / scale);
	float height = clamp(samplePoint.y, 0.0, 1.0);
	float vertical = smoothstep(0.0, 0.2, height) * (1.0 - smoothstep(0.5, 1.0, height));
	float shape = Fbm(samplePoint * 3.0);
	float coverage = clamp(u_cloudShape.x, 0.0, 1.0);
	float threshold = 1.0 - coverage;
	float formed = smoothstep(threshold, threshold + 0.2, shape) * vertical;
	float detail = Fbm(samplePoint * 9.0 + vec3(11.0, 5.0, 17.0));
	float eroded = formed - (1.0 - detail) * u_cloudShape.z * 0.5;
	return clamp(eroded, 0.0, 1.0) * u_cloudLayer.w;
}

float CloudDensityLight(vec3 samplePosition)
{
	float scale = max(u_cloudShape.y, 1.0);
	vec3 samplePoint = vec3(
		(samplePosition.x + u_cloudMotion.x) / scale,
		(samplePosition.y - u_cloudLayer.y) / max(u_cloudLayer.z, 1.0),
		(samplePosition.z + u_cloudMotion.y) / scale);
	float height = clamp(samplePoint.y, 0.0, 1.0);
	float vertical = smoothstep(0.0, 0.2, height) * (1.0 - smoothstep(0.5, 1.0, height));
	float shape = ValueNoise(samplePoint * 3.0) * 0.65 + ValueNoise(samplePoint * 6.06) * 0.35;
	float coverage = clamp(u_cloudShape.x, 0.0, 1.0);
	float threshold = 1.0 - coverage;
	float formed = smoothstep(threshold, threshold + 0.2, shape) * vertical;
	return clamp(formed, 0.0, 1.0) * u_cloudLayer.w;
}

float HenyeyGreenstein(float cosAngle, float g)
{
	float gg = g * g;
	return (1.0 - gg) / pow(max(1.0 + gg - 2.0 * g * cosAngle, 1e-4), 1.5);
}

vec4 MarchClouds(vec3 direction, vec3 toSun)
{
	if (u_cloudLayer.x < 0.5 || direction.y <= 0.04)
	{
		return vec4(0.0);
	}
	float startDistance = max((u_cloudLayer.y - u_skyCamera.y) / direction.y, 0.0);
	float endDistance = max((u_cloudLayer.y + u_cloudLayer.z - u_skyCamera.y) / direction.y, startDistance);
	if (endDistance <= startDistance)
	{
		return vec4(0.0);
	}
	const int kSteps = 28;
	float stepDistance = (endDistance - startDistance) / float(kSteps);
	// A ray-direction hash dithers the fixed step positions to break the banding
	// a fixed-step march otherwise leaves; the sky pass has no screen UV here.
	float jitter = fract(sin(dot(direction.xz, vec2(12.9898, 78.233))) * 43758.5453);
	float transmittance = 1.0;
	vec3 radiance = vec3(0.0);
	float cosAngle = dot(direction, toSun);
	float phase = mix(HenyeyGreenstein(cosAngle, 0.35),
		HenyeyGreenstein(cosAngle, -0.15), 0.4);
	for (int step = 0; step < kSteps; ++step)
	{
		float distance = startDistance + (float(step) + jitter) * stepDistance;
		vec3 samplePosition = u_skyCamera.xyz + direction * distance;
		float density = CloudDensity(samplePosition);
		if (density > 0.001)
		{
			float opticalDepth = density * (stepDistance / max(u_cloudLayer.z, 1.0)) * 6.0;
			float alpha = 1.0 - exp(-opticalDepth);
			float lightDensity = CloudDensityLight(samplePosition + toSun * max(u_cloudLayer.z * 0.08, 30.0));
			float lightVisibility = exp(-lightDensity * 1.8);
			float forward = clamp(phase, 0.0, 4.0) * 0.15 * u_cloudMotion.z;
			vec3 lit = mix(SkyToLinear(u_cloudShadow.rgb), SkyToLinear(u_cloudLit.rgb),
				clamp(lightVisibility + forward, 0.0, 1.0));
			vec3 fire = SkyToLinear(u_cloudFire.rgb) * u_cloudMotion.w
				* density * (0.35 + 0.65 * ValueNoise(samplePosition / 480.0));
			radiance += (lit + fire) * alpha * transmittance;
			transmittance *= 1.0 - alpha;
			if (transmittance < 0.02)
			{
				break;
			}
		}
	}
	float horizonFade = smoothstep(0.04, 0.28, direction.y);
	return vec4(radiance, 1.0 - transmittance) * horizonFade;
}

void main()
{
	vec3 direction = normalize(v_skyDirection);
	vec3 color = SkyToLinear(u_skySolid.rgb) * u_skyParams.y;
	vec3 toSun = normalize(-u_skySunDirection.xyz);
	vec4 clouds = vec4(0.0);

	if (u_skyParams.x > 0.5)
	{
		float vertical = abs(direction.y);
		float blendWeight = pow(clamp(vertical, 0.0, 1.0), u_skyParams.z);
		vec3 pole = direction.y >= 0.0 ? u_skyZenith.rgb : u_skyGround.rgb;
		color = SkyToLinear(mix(u_skyHorizon.rgb, pole, blendWeight)) * u_skyParams.y;

		if (u_skyOptions.z > 0.5)
		{
			float sunHeight = toSun.y;
			float daylight = smoothstep(-0.10, 0.04, sunHeight);
			vec3 night = vec3(0.006, 0.009, 0.020);
			color = mix(night, color, daylight);

			float horizonBand = 1.0 - smoothstep(0.02, 0.35, abs(sunHeight));
			vec2 viewHorizontal = vec2(direction.x, direction.z);
			vec2 sunHorizontal = vec2(toSun.x, toSun.z);
			float horizontalLength = max(
				length(viewHorizontal) * length(sunHorizontal), 1e-5);
			float towardSun = max(dot(viewHorizontal, sunHorizontal) / horizontalLength, 0.0);
			vec3 sunset = SkyToLinear(vec3(1.0, 0.34, 0.10));
			color += sunset * horizonBand * pow(towardSun, 8.0)
				* pow(1.0 - vertical, 3.0) * 0.22;

			if (u_skyOptions.x > 0.5 && sunHeight > -0.08)
			{
				const float kDegToRad = 0.017453292519943295;
				float physicalRadius = max(u_skySunDirection.w, 0.01) * kDegToRad;
				// The visible disk is intentionally larger than the physical sun
				// (which is ~0.5 deg and reads as a dot) so it looks like a sun;
				// this is decoupled from the small angular size used for shadow
				// penumbra. Medium disk + a tight bloom core + a wide warm haze.
				float diskRadius = max(physicalRadius * 3.0, 1.6 * kDegToRad);
				float edgeRadius = diskRadius + 0.7 * kDegToRad;
				float alignment = dot(direction, toSun);
				float disk = smoothstep(cos(edgeRadius), cos(diskRadius), alignment);
				float glow = pow(max(alignment, 0.0), 900.0) * 0.5;
				float haze = pow(max(alignment, 0.0), 300.0) * 0.15;
				vec3 radiance = SkyToLinear(u_skySunColor.rgb)
					* u_skySunColor.w * u_skyOptions.y;
				color += radiance * (disk * 1.2 + glow + haze) * daylight;
			}
		}

		// Clouds are composited after fog (below), so the fogged sky shows
		// through their gaps and they are not themselves tinted by fog - this
		// matches the main scene, where the volumetric cloud pass layers over
		// the already-fogged sky. Compositing before fog would fog-tint the
		// reflected clouds and make them differ from the real ones.
		clouds = MarchClouds(direction, toSun);
	}

	if (u_fogParams.x > 0.5)
	{
		float heightDensity = exp(-max(u_skyCamera.y - u_fogParams.z, 0.0)
			* u_fogParams.w);
		float horizonDistance = 1.0 / max(abs(direction.y), 0.025);
		float fogAmount = 1.0 - exp(-u_fogParams.y * heightDensity * horizonDistance * 4.0);
		color = mix(color, SkyToLinear(u_fogColor.rgb), clamp(fogAmount, 0.0, 0.96));
	}

	color = color * (1.0 - clouds.a) + clouds.rgb;

	vec3 outputColor = u_skyParams.w > 0.5 ? color : SkyToDisplay(color);
	gl_FragColor = vec4(outputColor, 1.0);
}
