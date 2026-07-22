/*
 * SPDX-License-Identifier: MIT
 * Reference SMAA algorithm by Jorge Jimenez et al. (2013), ported to bgfx.
 */
$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_edges, 0);
SAMPLER2D(s_area, 1);
SAMPLER2D(s_search, 2);
uniform vec4 u_smaaTexel;
uniform vec4 u_smaaConfig;

#define SMAA_AREA_MAX_DISTANCE 16.0
#define SMAA_AREA_MAX_DISTANCE_DIAG 20.0
#define SMAA_AREA_PIXEL_SIZE vec2(0.00625, 0.0017857142857142857)
#define SMAA_SEARCH_SIZE vec2(66.0, 33.0)
#define SMAA_SEARCH_PACKED_SIZE vec2(64.0, 16.0)

vec2 SmaaDecodeDiag(vec2 edge)
{
	edge.r = edge.r * abs(5.0 * edge.r - 3.75);
	return floor(edge + 0.5);
}

vec4 SmaaDecodeDiag4(vec4 edge)
{
	edge.rb = edge.rb * abs(5.0 * edge.rb - vec2(3.75, 3.75));
	return floor(edge + 0.5);
}

vec2 SmaaSearchDiag1(vec2 uv, vec2 direction, out vec2 edge)
{
	vec4 coordinate = vec4(uv, -1.0, 1.0);
	while (coordinate.z < u_smaaConfig.z - 1.0 && coordinate.w > 0.9) {
		coordinate.xyz += vec3(u_smaaTexel.xy * direction, 1.0);
		edge = texture2DLod(s_edges, coordinate.xy, 0.0).rg;
		coordinate.w = dot(edge, vec2(0.5, 0.5));
	}
	return coordinate.zw;
}

vec2 SmaaSearchDiag2(vec2 uv, vec2 direction, out vec2 edge)
{
	vec4 coordinate = vec4(uv, -1.0, 1.0);
	coordinate.x += 0.25 * u_smaaTexel.x;
	while (coordinate.z < u_smaaConfig.z - 1.0 && coordinate.w > 0.9) {
		coordinate.xyz += vec3(u_smaaTexel.xy * direction, 1.0);
		edge = SmaaDecodeDiag(texture2DLod(s_edges, coordinate.xy, 0.0).rg);
		coordinate.w = dot(edge, vec2(0.5, 0.5));
	}
	return coordinate.zw;
}
vec2 SmaaAreaDiag(vec2 distance, vec2 edge)
{
	vec2 uv = SMAA_AREA_MAX_DISTANCE_DIAG * edge + distance;
	uv = SMAA_AREA_PIXEL_SIZE * uv + 0.5 * SMAA_AREA_PIXEL_SIZE;
	uv.x += 0.5;
	return texture2DLod(s_area, uv, 0.0).rg;
}

vec2 SmaaCalculateDiagWeights(vec2 uv, vec2 edge)
{
	vec2 weights = vec2(0.0, 0.0);
	vec4 distance;
	vec2 end;
	if (edge.r > 0.0) {
		distance.xz = SmaaSearchDiag1(uv, vec2(-1.0, 1.0), end);
		distance.x += float(end.y > 0.9);
	} else {
		distance.xz = vec2(0.0, 0.0);
	}
	distance.yw = SmaaSearchDiag1(uv, vec2(1.0, -1.0), end);
	if (distance.x + distance.y > 2.0) {
		vec4 coordinates = uv.xyxy + vec4(-distance.x + 0.25, distance.x,
			distance.y, -distance.y - 0.25) * u_smaaTexel.xyxy;
		vec4 crossing;
		crossing.xy = texture2DLodOffset(s_edges, coordinates.xy, 0.0, ivec2(-1, 0)).rg;
		crossing.zw = texture2DLodOffset(s_edges, coordinates.zw, 0.0, ivec2(1, 0)).rg;
		crossing.yxwz = SmaaDecodeDiag4(crossing);
		vec2 merged = 2.0 * crossing.xz + crossing.yw;
		if (distance.z >= 0.9) merged.x = 0.0;
		if (distance.w >= 0.9) merged.y = 0.0;
		weights += SmaaAreaDiag(distance.xy, merged);
	}

	distance.xz = SmaaSearchDiag2(uv, vec2(-1.0, -1.0), end);
	if (texture2DLodOffset(s_edges, uv, 0.0, ivec2(1, 0)).r > 0.0) {
		distance.yw = SmaaSearchDiag2(uv, vec2(1.0, 1.0), end);
		distance.y += float(end.y > 0.9);
	} else {
		distance.yw = vec2(0.0, 0.0);
	}
	if (distance.x + distance.y > 2.0) {
		vec4 coordinates = uv.xyxy + vec4(-distance.x, -distance.x,
			distance.y, distance.y) * u_smaaTexel.xyxy;
		vec4 crossing;
		crossing.x = texture2DLodOffset(s_edges, coordinates.xy, 0.0, ivec2(-1, 0)).g;
		crossing.y = texture2DLodOffset(s_edges, coordinates.xy, 0.0, ivec2(0, -1)).r;
		crossing.zw = texture2DLodOffset(s_edges, coordinates.zw, 0.0, ivec2(1, 0)).gr;
		vec2 merged = 2.0 * crossing.xz + crossing.yw;
		if (distance.z >= 0.9) merged.x = 0.0;
		if (distance.w >= 0.9) merged.y = 0.0;
		weights += SmaaAreaDiag(distance.xy, merged).gr;
	}
	return weights;
}
float SmaaSearchLength(vec2 edge, float offset)
{
	vec2 scale = SMAA_SEARCH_SIZE * vec2(0.5, -1.0);
	vec2 bias = SMAA_SEARCH_SIZE * vec2(offset, 1.0);
	scale += vec2(-1.0, 1.0);
	bias += vec2(0.5, -0.5);
	scale /= SMAA_SEARCH_PACKED_SIZE;
	bias /= SMAA_SEARCH_PACKED_SIZE;
	return texture2DLod(s_search, scale * edge + bias, 0.0).r;
}

float SmaaSearchXLeft(vec2 uv, float end)
{
	vec2 edge = vec2(0.0, 1.0);
	while (uv.x > end && edge.g > 0.8281 && edge.r == 0.0) {
		edge = texture2DLod(s_edges, uv, 0.0).rg;
		uv -= vec2(2.0 * u_smaaTexel.x, 0.0);
	}
	float offset = 3.25 - (255.0 / 127.0) * SmaaSearchLength(edge, 0.0);
	return uv.x + u_smaaTexel.x * offset;
}

float SmaaSearchXRight(vec2 uv, float end)
{
	vec2 edge = vec2(0.0, 1.0);
	while (uv.x < end && edge.g > 0.8281 && edge.r == 0.0) {
		edge = texture2DLod(s_edges, uv, 0.0).rg;
		uv += vec2(2.0 * u_smaaTexel.x, 0.0);
	}
	float offset = 3.25 - (255.0 / 127.0) * SmaaSearchLength(edge, 0.5);
	return uv.x - u_smaaTexel.x * offset;
}

float SmaaSearchYUp(vec2 uv, float end)
{
	vec2 edge = vec2(1.0, 0.0);
	while (uv.y > end && edge.r > 0.8281 && edge.g == 0.0) {
		edge = texture2DLod(s_edges, uv, 0.0).rg;
		uv -= vec2(0.0, 2.0 * u_smaaTexel.y);
	}
	float offset = 3.25 - (255.0 / 127.0) * SmaaSearchLength(edge.gr, 0.0);
	return uv.y + u_smaaTexel.y * offset;
}

float SmaaSearchYDown(vec2 uv, float end)
{
	vec2 edge = vec2(1.0, 0.0);
	while (uv.y < end && edge.r > 0.8281 && edge.g == 0.0) {
		edge = texture2DLod(s_edges, uv, 0.0).rg;
		uv += vec2(0.0, 2.0 * u_smaaTexel.y);
	}
	float offset = 3.25 - (255.0 / 127.0) * SmaaSearchLength(edge.gr, 0.5);
	return uv.y - u_smaaTexel.y * offset;
}

vec2 SmaaArea(vec2 distance, float firstEdge, float secondEdge)
{
	vec2 uv = SMAA_AREA_MAX_DISTANCE * floor(4.0 * vec2(firstEdge, secondEdge) + 0.5)
		+ distance;
	uv = SMAA_AREA_PIXEL_SIZE * uv + 0.5 * SMAA_AREA_PIXEL_SIZE;
	return texture2DLod(s_area, uv, 0.0).rg;
}
vec2 SmaaHorizontalCorners(vec2 weights, vec4 coordinates, vec2 distance)
{
	vec2 leftRight = step(distance.xy, distance.yx);
	vec2 rounding = (1.0 - u_smaaConfig.w) * leftRight;
	rounding /= leftRight.x + leftRight.y;
	vec2 factor = vec2(1.0, 1.0);
	factor.x -= rounding.x * texture2DLodOffset(s_edges, coordinates.xy, 0.0, ivec2(0, 1)).r;
	factor.x -= rounding.y * texture2DLodOffset(s_edges, coordinates.zw, 0.0, ivec2(1, 1)).r;
	factor.y -= rounding.x * texture2DLodOffset(s_edges, coordinates.xy, 0.0, ivec2(0, -2)).r;
	factor.y -= rounding.y * texture2DLodOffset(s_edges, coordinates.zw, 0.0, ivec2(1, -2)).r;
	return weights * saturate(factor);
}

vec2 SmaaVerticalCorners(vec2 weights, vec4 coordinates, vec2 distance)
{
	vec2 leftRight = step(distance.xy, distance.yx);
	vec2 rounding = (1.0 - u_smaaConfig.w) * leftRight;
	rounding /= leftRight.x + leftRight.y;
	vec2 factor = vec2(1.0, 1.0);
	factor.x -= rounding.x * texture2DLodOffset(s_edges, coordinates.xy, 0.0, ivec2(1, 0)).g;
	factor.x -= rounding.y * texture2DLodOffset(s_edges, coordinates.zw, 0.0, ivec2(1, 1)).g;
	factor.y -= rounding.x * texture2DLodOffset(s_edges, coordinates.xy, 0.0, ivec2(-2, 0)).g;
	factor.y -= rounding.y * texture2DLodOffset(s_edges, coordinates.zw, 0.0, ivec2(-2, 1)).g;
	return weights * saturate(factor);
}

void main()
{
	vec2 uv = v_texcoord0;
	vec2 texel = u_smaaTexel.xy;
	vec2 pixel = uv * u_smaaTexel.zw;
	vec4 offset0 = uv.xyxy + texel.xyxy * vec4(-0.25, -0.125, 1.25, -0.125);
	vec4 offset1 = uv.xyxy + texel.xyxy * vec4(-0.125, -0.25, -0.125, 1.25);
	vec4 ends = vec4(offset0.x - 2.0 * u_smaaConfig.y * texel.x,
		offset0.z + 2.0 * u_smaaConfig.y * texel.x,
		offset1.y - 2.0 * u_smaaConfig.y * texel.y,
		offset1.w + 2.0 * u_smaaConfig.y * texel.y);
	vec4 weights = vec4(0.0, 0.0, 0.0, 0.0);
	vec2 edge = texture2D(s_edges, uv).rg;

	if (edge.g > 0.0) {
		weights.rg = SmaaCalculateDiagWeights(uv, edge);
		if (weights.r == -weights.g) {
			vec2 distance;
			vec3 coordinates;
			coordinates.x = SmaaSearchXLeft(offset0.xy, ends.x);
			coordinates.y = offset1.y;
			distance.x = coordinates.x;
			float firstEdge = texture2DLod(s_edges, coordinates.xy, 0.0).r;
			coordinates.z = SmaaSearchXRight(offset0.zw, ends.y);
			distance.y = coordinates.z;
			distance = abs(floor(u_smaaTexel.zz * distance - pixel.xx + vec2(0.5, 0.5)));
			float secondEdge = texture2DLodOffset(s_edges, coordinates.zy, 0.0, ivec2(1, 0)).r;
			weights.rg = SmaaArea(sqrt(distance), firstEdge, secondEdge);
			coordinates.y = uv.y;
			weights.rg = SmaaHorizontalCorners(weights.rg, coordinates.xyzy, distance);
		} else {
			edge.r = 0.0;
		}
	}

	if (edge.r > 0.0) {
		vec2 distance;
		vec3 coordinates;
		coordinates.y = SmaaSearchYUp(offset1.xy, ends.z);
		coordinates.x = offset0.x;
		distance.x = coordinates.y;
		float firstEdge = texture2DLod(s_edges, coordinates.xy, 0.0).g;
		coordinates.z = SmaaSearchYDown(offset1.zw, ends.w);
		distance.y = coordinates.z;
		distance = abs(floor(u_smaaTexel.ww * distance - pixel.yy + vec2(0.5, 0.5)));
		float secondEdge = texture2DLodOffset(s_edges, coordinates.xz, 0.0, ivec2(0, 1)).g;
		weights.ba = SmaaArea(sqrt(distance), firstEdge, secondEdge);
		coordinates.x = uv.x;
		weights.ba = SmaaVerticalCorners(weights.ba, coordinates.xyxz, distance);
	}
	gl_FragColor = weights;
}
