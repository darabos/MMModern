#include "platform/sdl/IndoorGpuRenderer.h"
#include "core/IndoorRenderScene.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <SDL_opengl.h>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace mmodern {
namespace {
constexpr int scale = 3, viewWidth = 216 * scale, viewHeight = 132 * scale;

// SDL may itself have a current GL context. Never leave our offscreen one acquired.
struct ContextRestore {
	SDL_Window *window = SDL_GL_GetCurrentWindow();
	SDL_GLContext context = SDL_GL_GetCurrentContext();
	~ContextRestore() {
		if (SDL_GL_MakeCurrent(window, context) != 0)
			std::cerr << "Cannot restore display GL context: " << SDL_GetError() << '\n';
	}
};

#define INDOOR_GL_FUNCTIONS(X) \
	X(CreateShader) X(ShaderSource) X(CompileShader) X(GetShaderiv) X(GetShaderInfoLog) X(DeleteShader) \
	X(CreateProgram) X(AttachShader) X(LinkProgram) X(GetProgramiv) X(GetProgramInfoLog) X(DeleteProgram) \
	X(UseProgram) X(GetUniformLocation) X(UniformMatrix4fv) X(Uniform1i) \
	X(GenVertexArrays) X(BindVertexArray) X(DeleteVertexArrays) \
	X(GenBuffers) X(BindBuffer) X(BufferData) X(DeleteBuffers) \
	X(EnableVertexAttribArray) X(VertexAttribPointer) \
	X(GenTextures) X(BindTexture) X(TexImage2D) X(TexParameteri) X(DeleteTextures) X(ActiveTexture) \
	X(GenFramebuffers) X(BindFramebuffer) X(FramebufferTexture2D) X(CheckFramebufferStatus) X(DeleteFramebuffers) \
	X(GenRenderbuffers) X(BindRenderbuffer) X(RenderbufferStorage) X(FramebufferRenderbuffer) X(DeleteRenderbuffers) \
	X(Viewport) X(ClearColor) X(Clear) X(Enable) X(Disable) X(DepthFunc) X(DrawArrays) X(ReadPixels) X(PixelStorei) X(GetError)

struct Gl {
#define DECLARE(name) decltype(&gl##name) name = nullptr;
	INDOOR_GL_FUNCTIONS(DECLARE)
#undef DECLARE
	void load() {
#define LOAD(name) name = reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress("gl" #name)); \
		if (!name) throw std::runtime_error("Missing GL function: gl" #name);
		INDOOR_GL_FUNCTIONS(LOAD)
#undef LOAD
	}
	void check(const char *operation) {
		const auto error = GetError();
		if (error != GL_NO_ERROR) throw std::runtime_error(std::string(operation) + ": GL error " + std::to_string(error));
	}
};
} // namespace

struct IndoorGpuRenderer::Impl {
	SDL_Window *window = nullptr;
	SDL_GLContext context = nullptr;
	Gl gl;
	GLuint program = 0, vao = 0, vbo = 0, framebuffer = 0, color = 0, depth = 0;
	struct Texture {
		std::weak_ptr<const IndoorTexture> source;
		GLuint name;
	};
	std::unordered_map<const IndoorTexture *, Texture> textures;
	std::array<std::uint8_t, 768> palette{};

	~Impl() {
		ContextRestore restore;
		if (restore.context == context) { restore.context = nullptr; restore.window = nullptr; }
		if (context && SDL_GL_MakeCurrent(window, context) == 0) {
			for (const auto &entry : textures) gl.DeleteTextures(1, &entry.second.name);
			if (color) gl.DeleteTextures(1, &color);
			if (depth) gl.DeleteRenderbuffers(1, &depth);
			if (framebuffer) gl.DeleteFramebuffers(1, &framebuffer);
			if (vbo) gl.DeleteBuffers(1, &vbo);
			if (vao) gl.DeleteVertexArrays(1, &vao);
			if (program) gl.DeleteProgram(program);
		}
		if (context) SDL_GL_DeleteContext(context);
		if (window) SDL_DestroyWindow(window);
	}

	GLuint shader(GLenum kind, const char *source) {
		const auto name = gl.CreateShader(kind);
		gl.ShaderSource(name, 1, &source, nullptr); gl.CompileShader(name);
		GLint good = 0; gl.GetShaderiv(name, GL_COMPILE_STATUS, &good);
		if (!good) {
			char message[2048]{}; gl.GetShaderInfoLog(name, sizeof(message), nullptr, message);
			gl.DeleteShader(name);
			throw std::runtime_error(std::string("Indoor shader compilation failed: ") + message);
		}
		return name;
	}

	void initialize() {
		// Core GL is the small prototype backend. No fixed-function or platform GL linkage.
		constexpr SDL_GLattr attributes[]{SDL_GL_CONTEXT_MAJOR_VERSION, SDL_GL_CONTEXT_MINOR_VERSION,
			SDL_GL_CONTEXT_PROFILE_MASK};
		constexpr int values[]{3, 2, SDL_GL_CONTEXT_PROFILE_CORE};
		int previous[std::size(attributes)]{};
		for (std::size_t i = 0; i < std::size(attributes); ++i) {
			if (SDL_GL_GetAttribute(attributes[i], &previous[i]) != 0)
				throw std::runtime_error(std::string("Cannot query GL attributes: ") + SDL_GetError());
		}
		struct AttributeRestore {
			const SDL_GLattr *attributes; const int *values; std::size_t count;
			~AttributeRestore() {
				for (std::size_t i = 0; i < count; ++i)
					if (SDL_GL_SetAttribute(attributes[i], values[i]) != 0)
						std::cerr << "Cannot restore GL attributes: " << SDL_GetError() << '\n';
			}
		} restore{attributes, previous, std::size(attributes)};
		for (std::size_t i = 0; i < std::size(attributes); ++i)
			if (SDL_GL_SetAttribute(attributes[i], values[i]) != 0)
				throw std::runtime_error(std::string("Cannot configure indoor GL context: ") + SDL_GetError());
		window = SDL_CreateWindow("MMModern indoor GPU", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
		if (!window) throw std::runtime_error(std::string("Cannot create indoor GL window: ") + SDL_GetError());
		context = SDL_GL_CreateContext(window);
		if (!context) throw std::runtime_error(std::string("Cannot create indoor GL context: ") + SDL_GetError());
		gl.load();
		const char *vertexSource = R"(#version 150
in vec3 position;
in vec2 texcoord;
uniform mat4 projection;
out vec2 uv;
void main() { uv = texcoord; gl_Position = projection * vec4(position, 1.0); }
)";
		const char *fragmentSource = R"(#version 150
uniform sampler2D image;
in vec2 uv;
out vec4 color;
void main() {
    color = texture(image, uv);
    if (color.a < 0.5) discard;
}
)";
		const auto vertex = shader(GL_VERTEX_SHADER, vertexSource);
		GLuint fragment = 0;
		try { fragment = shader(GL_FRAGMENT_SHADER, fragmentSource); }
		catch (...) { gl.DeleteShader(vertex); throw; }
		program = gl.CreateProgram();
		gl.AttachShader(program, vertex); gl.AttachShader(program, fragment); gl.LinkProgram(program);
		gl.DeleteShader(vertex); gl.DeleteShader(fragment);
		GLint good = 0; gl.GetProgramiv(program, GL_LINK_STATUS, &good);
		if (!good) {
			char message[2048]{}; gl.GetProgramInfoLog(program, sizeof(message), nullptr, message);
			throw std::runtime_error(std::string("Indoor shader link failed: ") + message);
		}
		gl.GenVertexArrays(1, &vao); gl.BindVertexArray(vao);
		gl.GenBuffers(1, &vbo); gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
		// Explicit locations are bound below through queried shader attributes.
		const auto getAttribLocation = reinterpret_cast<decltype(&glGetAttribLocation)>(SDL_GL_GetProcAddress("glGetAttribLocation"));
		if (!getAttribLocation) throw std::runtime_error("Missing GL attribute lookup");
		for (const auto *name : {"position", "texcoord"}) {
			const GLint location = getAttribLocation(program, name);
			if (location < 0) throw std::runtime_error("Missing indoor shader attribute");
			gl.EnableVertexAttribArray(GLuint(location));
			const bool position = std::string(name) == "position";
			gl.VertexAttribPointer(GLuint(location), position ? 3 : 2, GL_FLOAT, GL_FALSE,
				sizeof(IndoorVertex), reinterpret_cast<const void *>(position ? 0 : offsetof(IndoorVertex, u)));
		}
		gl.GenFramebuffers(1, &framebuffer); gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		gl.GenTextures(1, &color); gl.BindTexture(GL_TEXTURE_2D, color);
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewWidth, viewHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
		gl.GenRenderbuffers(1, &depth); gl.BindRenderbuffer(GL_RENDERBUFFER, depth);
		gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, viewWidth, viewHeight);
		gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
		if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			throw std::runtime_error("Indoor GPU framebuffer is incomplete");
		gl.check("Indoor renderer initialization");
	}

	GLuint upload(const std::shared_ptr<const IndoorTexture> &source) {
		const auto found = textures.find(source.get());
		if (found != textures.end()) return found->second.name;
		GLuint name = 0; gl.GenTextures(1, &name); gl.BindTexture(GL_TEXTURE_2D, name);
		std::vector<std::uint8_t> rgba(source->indices.size() * 4);
		for (std::size_t i = 0; i < source->indices.size(); ++i) {
			const auto offset = std::size_t(source->indices[i]) * 3;
			for (unsigned c = 0; c < 3; ++c) rgba[i * 4 + c] = palette[offset + c];
			rgba[i * 4 + 3] = source->alpha[i];
		}
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, source->width, source->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		textures.emplace(source.get(), Texture{source, name});
		return name;
	}
};

IndoorGpuRenderer::IndoorGpuRenderer() = default;
IndoorGpuRenderer::~IndoorGpuRenderer() = default;

std::vector<std::uint32_t> IndoorGpuRenderer::render(const IndexedFrame &frame) {
	if (!frame.indoorScene || !frame.isValid() || frame.width != 320 || frame.height != 200)
		throw std::invalid_argument("Indoor GPU rendering requires a 320x200 scene snapshot");
	const auto &scene = *frame.indoorScene;
	if (scene.originalPixels.size() != frame.pixels.size() || scene.interfaceCoverage.size() != frame.pixels.size())
		throw std::invalid_argument("Invalid indoor scene overlay coverage");
	ContextRestore restore;
	if (!_impl) {
		auto impl = std::make_unique<Impl>();
		impl->initialize();
		_impl = std::move(impl);
	}
	auto &r = *_impl; auto &gl = r.gl;
	if (SDL_GL_MakeCurrent(r.window, r.context) != 0)
		throw std::runtime_error(std::string("Cannot acquire indoor GL context: ") + SDL_GetError());
	for (auto it = r.textures.begin(); it != r.textures.end();) {
		if (it->second.source.expired() || r.palette != frame.palette) {
			gl.DeleteTextures(1, &it->second.name); it = r.textures.erase(it);
		} else ++it;
	}
	r.palette = frame.palette;
	gl.BindFramebuffer(GL_FRAMEBUFFER, r.framebuffer);
	gl.Viewport(0, 0, viewWidth, viewHeight);
	gl.ClearColor(0, 0, 0, 1); gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gl.Enable(GL_DEPTH_TEST); gl.DepthFunc(GL_LEQUAL); gl.Disable(GL_CULL_FACE); gl.Disable(GL_BLEND);
	gl.UseProgram(r.program); gl.BindVertexArray(r.vao); gl.BindBuffer(GL_ARRAY_BUFFER, r.vbo);
	gl.ActiveTexture(GL_TEXTURE0); gl.Uniform1i(gl.GetUniformLocation(r.program, "image"), 0);
	// 90-degree vertical FOV; account for the original monitor's tall pixels.
	constexpr float nearPlane = 0.04f, farPlane = 12.f, aspect = 216.f / (132.f * 1.2f);
	const GLfloat projection[]{1 / aspect, 0, 0, 0, 0, 1, 0, 0,
		0, 0, -(farPlane + nearPlane) / (farPlane - nearPlane), -1,
		0, 0, -2 * farPlane * nearPlane / (farPlane - nearPlane), 0};
	gl.UniformMatrix4fv(gl.GetUniformLocation(r.program, "projection"), 1, GL_FALSE, projection);
	if (!scene.black) for (const auto &quad : scene.quads) {
		gl.BindTexture(GL_TEXTURE_2D, r.upload(quad.texture));
		const IndoorVertex vertices[]{quad.vertices[0], quad.vertices[1], quad.vertices[2],
			quad.vertices[0], quad.vertices[2], quad.vertices[3]};
		gl.BufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
		gl.DrawArrays(GL_TRIANGLES, 0, 6);
	}
	std::vector<std::uint8_t> view(std::size_t(viewWidth) * viewHeight * 4);
	gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
	gl.ReadPixels(0, 0, viewWidth, viewHeight, GL_RGBA, GL_UNSIGNED_BYTE, view.data());
	gl.check("Indoor GPU rendering");
	std::vector<std::uint32_t> pixels(std::size_t(320 * scale) * 200 * scale);
	for (int y = 0; y < 200 * scale; ++y) for (int x = 0; x < 320 * scale; ++x) {
		const auto native = std::size_t(y / scale) * 320 + x / scale;
		const auto offset = std::size_t(frame.pixels[native]) * 3;
		std::uint32_t color = 0xff000000u | (std::uint32_t(frame.palette[offset]) << 16) |
			(std::uint32_t(frame.palette[offset + 1]) << 8) | frame.palette[offset + 2];
		if (x >= 8 * scale && x < 224 * scale && y >= 8 * scale && y < 140 * scale &&
			!scene.interfaceCoverage[native] && frame.pixels[native] == scene.originalPixels[native]) {
			const auto p = (std::size_t(viewHeight - 1 - (y - 8 * scale)) * viewWidth + x - 8 * scale) * 4;
			color = 0xff000000u | (std::uint32_t(view[p]) << 16) | (std::uint32_t(view[p + 1]) << 8) | view[p + 2];
		}
		pixels[std::size_t(y) * 320 * scale + x] = color;
	}
	return pixels;
}
} // namespace mmodern
