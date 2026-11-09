$input v_texcoord0, v_clip

#include <bgfx_shader.sh>

SAMPLER2D(s_image, 0);

void main()
{
	if (v_clip.x < 0.0 || v_clip.y <= 0.0)
		discard;
	vec4 color = texture2D(s_image, v_texcoord0);
	if (color.a < 0.5)
		discard;
	gl_FragColor = color;
}
