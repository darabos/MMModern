$input v_texcoord0, v_clip, v_fog

#include <bgfx_shader.sh>

SAMPLER2D(s_image, 0);
uniform vec4 u_geometry; // mode, UV flip, mip bias, unused

void main()
{
	if (v_clip.x < 0.0 || v_clip.y <= 0.0)
		discard;
	vec4 color = texture2DBias(s_image, v_texcoord0, u_geometry.z);
	if (color.a < 0.5)
		discard;
	float fog = smoothstep(3.0, 10.0, v_fog);
	gl_FragColor = vec4(mix(color.rgb, vec3(0.08, 0.09, 0.12), fog), color.a);
}
