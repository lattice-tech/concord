$input a_position, a_indices, a_weight, i_data0, i_data1, i_data2, i_data3
$output v_wpos

#include <bgfx_shader.sh>

uniform mat4 u_bones[64];

void main()
{
	mat4 skin = a_weight.x * u_bones[int(a_indices.x)]
	          + a_weight.y * u_bones[int(a_indices.y)]
	          + a_weight.z * u_bones[int(a_indices.z)]
	          + a_weight.w * u_bones[int(a_indices.w)];
	vec4 skinned = mul(skin, vec4(a_position, 1.0));
	vec4 worldPos = i_data0 * skinned.x
	              + i_data1 * skinned.y
	              + i_data2 * skinned.z
	              + i_data3 * skinned.w;
	gl_Position = mul(u_viewProj, worldPos);
	v_wpos = worldPos.xyz;
}
