$input a_position, a_normal, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_wpos, v_wnormal, v_lpos, v_texcoord0

#include <bgfx_shader.sh>

void main()
{
	// Instance data is a bx **column-major** 4x4 world matrix packed as four
	// column vectors (memcpy on the CPU — never transpose). Transform the
	// vertex with an explicit column blend instead of mat4/mtxFromCols:
	//
	//   world = col0*x + col1*y + col2*z + col3
	//
	// SPIR-V / Vulkan column-vector math; avoids mat4 constructor traps that
	// historically broke placement when a second API was present.
	vec4 worldPos = i_data0 * a_position.x
	              + i_data1 * a_position.y
	              + i_data2 * a_position.z
	              + i_data3;
	gl_Position = mul(u_viewProj, worldPos);

	v_wpos = worldPos.xyz;
	// Direction: same 3x3, no translation column.
	v_wnormal = (i_data0 * a_normal.x
	           + i_data1 * a_normal.y
	           + i_data2 * a_normal.z).xyz;
	v_lpos = a_position;
	v_texcoord0 = a_texcoord0;
}
