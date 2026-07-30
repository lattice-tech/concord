$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_wpos, v_uv, v_surface, v_cascade

#include <bgfx_shader.sh>

// Water vertex shader — adapted from three.js Water.js (MirrorShader).
// Source: https://github.com/mrdoob/three.js/blob/dev/examples/jsm/objects/Water.js
// License: MIT (three.js).
//
// The original projects the surface vertex into a mirrored camera's clip
// space via a textureMatrix; Concord already runs a planar reflection pass
// and hands the result's view-projection, so the fragment stage reprojects
// from world position instead.

uniform vec4 u_waterSurface; // x = clock, y = levels(unused), z = animated, w = plane height

void main()
{
	// WaterGridMesh lays the plane on XZ at y=0: a_position = (u-0.5, 0, v-0.5).
	mat4 world = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	vec4 worldPos4 = mul(world, vec4(a_position.xyz, 1.0));
	vec3 worldPos = worldPos4.xyz;

	vec4 clip = mul(u_viewProj, vec4(worldPos, 1.0));
	gl_Position = clip;
	v_wpos = vec4(worldPos, 1.0);
	v_uv = a_texcoord0;
	v_surface = u_waterSurface;
	v_cascade = vec4(clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5, 0.0, 0.0);
}
