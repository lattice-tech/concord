$input a_position
$output v_uv

#include <bgfx_shader.sh>

// z = index of the level being baked, w = number of levels in the atlas.
uniform vec4 u_waterCascadeSpectrum;

/**
 * Draws one column of the cascade atlas.
 *
 * The quad spans the whole target in Y and one level's slice in X, so a single
 * atlas texture and a single sampler serve every level (the vertex stage of the
 * water shader has one sampler to spare, not five).
 */
void main()
{
	float levels = max(u_waterCascadeSpectrum.w, 1.0);
	float level = u_waterCascadeSpectrum.z;
	// a_position is the unit quad in [0, 1]; map it into this level's column.
	float x = (level + a_position.x) / levels;
	gl_Position = vec4(x * 2.0 - 1.0, a_position.y * 2.0 - 1.0, 0.0, 1.0);
	v_uv = a_position;
}
