$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_image, 0);

void main()
{
	vec4 color = texture2D(s_image, v_texcoord0);
	if (color.a < 0.5)
		discard;
	gl_FragColor = color;
}
