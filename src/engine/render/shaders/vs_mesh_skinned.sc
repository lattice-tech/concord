$input a_position, a_normal, a_texcoord0, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3
$output v_wpos, v_wnormal, v_lpos, v_texcoord0

#include <bgfx_shader.sh>

// Bone matrix palette: one column-major model->posed-model matrix per bone,
// uploaded by the skinned draw path. Must match Concord::kMaxRenderBones.
uniform mat4 u_bones[64];

void main()
{
	// Linear blend skinning: weighted sum of the four influencing bone
	// matrices. Indices arrive as unnormalised floats (Uint8 vertex attribute),
	// so int() recovers the palette slot.
	mat4 skin = a_weight.x * u_bones[int(a_indices.x)]
	          + a_weight.y * u_bones[int(a_indices.y)]
	          + a_weight.z * u_bones[int(a_indices.z)]
	          + a_weight.w * u_bones[int(a_indices.w)];

	vec4 skinned = mul(skin, vec4(a_position, 1.0));

	// Instance data is the bx column-major world matrix packed as four columns;
	// blend them explicitly (same contract as vs_mesh), applied after skinning.
	vec4 worldPos = i_data0 * skinned.x
	              + i_data1 * skinned.y
	              + i_data2 * skinned.z
	              + i_data3 * skinned.w;
	gl_Position = mul(u_viewProj, worldPos);

	v_wpos = worldPos.xyz;

	vec3 skinnedNormal = mul(skin, vec4(a_normal, 0.0)).xyz;
	v_wnormal = (i_data0 * skinnedNormal.x
	           + i_data1 * skinnedNormal.y
	           + i_data2 * skinnedNormal.z).xyz;

	v_lpos = a_position;
	v_texcoord0 = a_texcoord0;
}
