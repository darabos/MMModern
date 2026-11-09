$input a_position, a_texcoord0, a_texcoord1
$output v_texcoord0, v_clip

#include <bgfx_shader.sh>

uniform vec4 u_distanceWarp; // enabled, horizon, eye setback, wall clipping
uniform vec4 u_geometry; // mode: sky=-1, UI=0, world=1, billboard=2; UV flip
uniform vec4 u_cameraPosition;

void main()
{
	vec4 position = mul(u_modelView, vec4(a_position, 1.0));
	if (u_geometry.x > 1.5)
		position = mul(u_view, vec4(a_texcoord1.x, 0.0, a_texcoord1.y, 1.0))
			+ vec4(a_position, 0.0);
	else if (u_geometry.x < -0.5)
		position = vec4(a_position.xy, a_position.z - u_distanceWarp.z, 1.0);
	gl_Position = mul(u_proj, position);
	float depth = -position.z - u_distanceWarp.z;
	v_clip = vec2(1.0, 1.0);
	if (u_distanceWarp.w > 0.5)
		v_clip = vec2(depth - 0.04, dot(a_texcoord1.zw, u_cameraPosition.xy - a_position.xz));
	if (u_distanceWarp.x > 0.0)
	{
		float scale = 1.0;
		if (depth > 3.5)
			scale = (42.0 / 273.0) * (depth + 1.125) / (depth - 1.75);
		else if (depth > 2.5)
			scale = mix(29.0 / 39.0, 37.0 / 91.0, depth - 2.5);
		else if (depth > 1.5)
			scale = mix(1.0, 29.0 / 39.0, depth - 1.5);
		float horizon = u_distanceWarp.y * gl_Position.w;
		gl_Position.x *= scale;
		gl_Position.y = horizon + (gl_Position.y - horizon) * scale;
	}
	v_texcoord0 = vec2(a_texcoord0.x, mix(a_texcoord0.y, 1.0 - a_texcoord0.y, u_geometry.y));
}
