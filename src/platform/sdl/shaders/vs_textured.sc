$input a_position, a_texcoord0
$output v_texcoord0

#include <bgfx_shader.sh>

uniform vec4 u_distanceWarp;

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	if (u_distanceWarp.x > 0.0)
	{
		float depth = -a_position.z;
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
	v_texcoord0 = a_texcoord0;
}
