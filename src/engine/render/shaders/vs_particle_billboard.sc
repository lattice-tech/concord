$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0, v_wpos

#include <bgfx_shader.sh>

void main()
{
	vec4 viewCenter = mul(u_view, i_data3);
	vec2 scale = vec2(length(i_data0.xyz), length(i_data1.xyz));
	viewCenter.xy += a_position.xy * scale;
	gl_Position = mul(u_proj, viewCenter);
	v_texcoord0 = a_texcoord0;
	v_wpos = i_data3.xyz;
}
