$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_wpos

#include <bgfx_shader.sh>

void main()
{
	// Same instance-matrix contract as vs_mesh: four column vectors of a
	// bx column-major world matrix, applied with an explicit column blend
	// so HLSL and SPIR-V stay in lock-step.
	vec4 worldPos = i_data0 * a_position.x
	              + i_data1 * a_position.y
	              + i_data2 * a_position.z
	              + i_data3;
	gl_Position = mul(u_viewProj, worldPos);

	v_wpos = worldPos.xyz;
}
