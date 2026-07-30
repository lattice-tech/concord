$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_wpos, v_uv, v_surface, v_cascade, v_wave

#include <bgfx_shader.sh>

// Water vertex shader — adapted from three.js Water.js (MirrorShader), with a
// Gerstner displacement stage in front of it.
// Source: https://github.com/mrdoob/three.js/blob/dev/examples/jsm/objects/Water.js
// License: MIT (three.js).
//
// The vertex stage sums the same Gerstner octaves the CPU samples for buoyancy
// (see Water::SampleSurface), so the drawn surface and buoyancy queries agree.
// The summed analytic normal and a crest factor ride to the fragment stage in
// v_wave, where hash-noise ripples perturb them.

uniform vec4 u_waterSurface; // x = clock, y = wave count, z = animated, w = plane height
uniform vec4 u_waterWaveA[4]; // per wave: dirX, dirZ, amplitude, wavelength
uniform vec4 u_waterWaveB[4]; // per wave: speed, steepness (normalised Q), 0, 0

void main()
{
	// WaterGridMesh lays the plane on XZ at y=0: a_position = (u-0.5, 0, v-0.5).
	mat4 world = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	vec4 basePos4 = mul(world, vec4(a_position.xyz, 1.0));
	vec3 worldPos = basePos4.xyz;

	float time = u_waterSurface.x;
	int waveCount = int(min(u_waterSurface.y, 4.0));
	vec3 offset = vec3(0.0, 0.0, 0.0);
	// Analytic normal accumulators (gradient of the Gerstner sum).
	float nx = 0.0;
	float nz = 0.0;
	float crest = 0.0;
	float totalAmp = 0.0;
	if (u_waterSurface.z > 0.5) {
		for (int i = 0; i < 4; ++i) {
			if (i >= waveCount) { break; }
			vec4 wa = u_waterWaveA[i];
			vec4 wb = u_waterWaveB[i];
			float amp = wa.z;
			float wavelength = max(wa.w, 1e-3);
			vec2 dir = normalize(wa.xy + vec2(1e-5, 0.0));
			float k = 6.2831853 / wavelength;
			float q = wb.y;
			float phase = k * (dot(dir, worldPos.xz) - wb.x * time);
			float s = sin(phase);
			float c = cos(phase);
			offset.y += amp * s;
			offset.xz += dir * (q * amp * c);
			nx += dir.x * k * amp * c;
			nz += dir.y * k * amp * c;
			crest += amp * s;
			totalAmp += amp;
		}
	}
	worldPos += offset;

	vec4 clip = mul(u_viewProj, vec4(worldPos, 1.0));
	gl_Position = clip;
	v_wpos = worldPos;
	v_uv = a_texcoord0;
	v_surface = u_waterSurface;
	// Raw clip position: the fragment stage does the perspective divide.
	// Dividing here and interpolating the result is not perspective-correct
	// and smears every screen-space lookup along the triangle diagonals.
	v_cascade = clip;
	// Analytic wave normal plus a [0,1] crest factor for whitecap foam.
	vec3 waveNormal = normalize(vec3(-nx, 1.0, -nz));
	float crestNorm = totalAmp > 1e-4 ? clamp(crest / totalAmp * 0.5 + 0.5, 0.0, 1.0) : 0.0;
	v_wave = vec4(waveNormal, crestNorm);
}
