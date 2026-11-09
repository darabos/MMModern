#include "platform/sdl/IndoorGpuRenderer.h"
#include "platform/sdl/SdlWindowPlacement.h"
#include "core/IndoorRenderScene.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#include <bgfx/bgfx.h>

#include "vs_textured.h"
#include "fs_textured.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace mmodern {
namespace {
constexpr std::uint64_t pointSampling = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
	BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr std::uint64_t colorWrite = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
constexpr std::uint32_t resetFlags = BGFX_RESET_VSYNC | BGFX_RESET_FLIP_AFTER_RENDER;
constexpr bgfx::ViewId worldView = 0, compositionView = 1, displayView = 2;
bool deviceActive = false;
static_assert(sizeof(IndoorVertex) == 5 * sizeof(float), "bgfx vertex layout must match IndoorVertex");

template<class Handle> Handle require(Handle handle, const char *what) {
	if (!bgfx::isValid(handle)) throw std::runtime_error(what);
	return handle;
}

template<class Handle> void releaseHandle(Handle &handle) {
	if (bgfx::isValid(handle)) bgfx::destroy(handle);
	handle = BGFX_INVALID_HANDLE;
}

std::array<float, 16> ortho(float width, float height) {
	std::array<float, 16> result{};
	result[0] = 2 / width; result[5] = -2 / height;
	result[10] = 1; result[12] = -1; result[13] = 1; result[15] = 1;
	return result;
}

bgfx::SwapChain swapChain(SDL_Window *window, int width, int height, bgfx::PlatformData &platform) {
	SDL_SysWMinfo info{};
	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window, &info))
		throw std::runtime_error(std::string("Cannot obtain bgfx native window: ") + SDL_GetError());
	bgfx::SwapChain chain;
	chain.width = std::uint32_t(width); chain.height = std::uint32_t(height);
	// This bgfx revision initializes Metal before resolving Count/default formats.
	chain.formatColor = bgfx::TextureFormat::BGRA8;
	chain.formatDepthStencil = bgfx::TextureFormat::D24S8;
	switch (info.subsystem) {
#if defined(SDL_VIDEO_DRIVER_COCOA)
	case SDL_SYSWM_COCOA: chain.nwh = info.info.cocoa.window; break;
#endif
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
	case SDL_SYSWM_WINDOWS: chain.nwh = info.info.win.window; break;
#endif
#if defined(SDL_VIDEO_DRIVER_X11)
	case SDL_SYSWM_X11:
		chain.nwh = reinterpret_cast<void *>(std::uintptr_t(info.info.x11.window));
		chain.ndt = info.info.x11.display;
		break;
#endif
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
	case SDL_SYSWM_WAYLAND:
		chain.nwh = info.info.wl.surface; chain.ndt = info.info.wl.display;
		platform.type = bgfx::NativeWindowHandleType::Wayland;
		break;
#endif
	default: throw std::runtime_error("This SDL window system is not supported by the bgfx presenter");
	}
	return chain;
}
} // namespace

struct IndoorGpuRenderer::Impl {
	SDL_Window *window;
	int frameWidth, frameHeight, outputWidth = 0, outputHeight = 0, compositionScale = 0;
	bool corrected, initialized = false, enhanced = false;
	bgfx::SwapChain chain;
	bgfx::VertexLayout layout;
	bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle frameTexture = BGFX_INVALID_HANDLE, cursorTexture = BGFX_INVALID_HANDLE;
	bgfx::FrameBufferHandle worldTarget = BGFX_INVALID_HANDLE, compositionTarget = BGFX_INVALID_HANDLE;
	int cursorWidth = 0, cursorHeight = 0;
	IndexedFrame frame;
	struct Texture {
		std::weak_ptr<const IndoorTexture> source;
		bgfx::TextureHandle handle;
	};
	std::unordered_map<const IndoorTexture *, Texture> textures;
	std::array<std::uint8_t, 768> palette{};

	Impl(SDL_Window *window, int width, int height, bool corrected) :
		window(window), frameWidth(width), frameHeight(height), corrected(corrected) {}

	~Impl() {
		if (!initialized) return;
		for (auto &entry : textures) releaseHandle(entry.second.handle);
		releaseHandle(worldTarget); releaseHandle(compositionTarget);
		releaseHandle(frameTexture); releaseHandle(cursorTexture);
		releaseHandle(program); releaseHandle(sampler);
		bgfx::shutdown();
		deviceActive = false;
	}

	void initialize() {
		if (deviceActive) throw std::runtime_error("Cannot initialize a second bgfx presentation loop");
		int width = 0, height = 0;
		SDL_GetWindowSizeInPixels(window, &width, &height);
		if (width <= 0 || height <= 0) throw std::runtime_error("Invalid bgfx window dimensions");
		bgfx::Init init;
#if defined(__APPLE__)
		init.type = bgfx::RendererType::Metal;
#elif defined(_WIN32)
		init.type = bgfx::RendererType::Direct3D11;
#else
		init.type = bgfx::RendererType::Vulkan;
#endif
		// Shader binaries target this backend; never silently select an incompatible API.
		init.fallback = false;
		init.videoDecode = false;
		chain = swapChain(window, width, height, init.platformData);
		init.swapChain = chain;
		init.reset = resetFlags;
		if (!bgfx::init(init)) throw std::runtime_error("Cannot initialize the bgfx graphics device");
		initialized = true;
		deviceActive = true;
		outputWidth = width; outputHeight = height;
		layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float).end();
		const auto vertex = require(bgfx::createShader(bgfx::copy(vs_textured, sizeof(vs_textured))),
			"Cannot create bgfx vertex shader");
		auto fragment = bgfx::createShader(bgfx::copy(fs_textured, sizeof(fs_textured)));
		if (!bgfx::isValid(fragment)) {
			bgfx::destroy(vertex);
			throw std::runtime_error("Cannot create bgfx fragment shader");
		}
		program = require(bgfx::createProgram(vertex, fragment, true), "Cannot link bgfx texture program");
		sampler = require(bgfx::createUniform("s_image", bgfx::UniformType::Sampler), "Cannot create bgfx sampler");
		frameTexture = createTexture(frameWidth, frameHeight);
		for (auto view : {worldView, compositionView, displayView}) bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
		bgfx::setViewName(worldView, "Indoor world");
		bgfx::setViewName(compositionView, "Original UI and cursor");
		bgfx::setViewName(displayView, "Window presentation");
		std::cerr << "bgfx presenter: " << bgfx::getRendererName(bgfx::getRendererType()) << '\n';
	}

	bgfx::TextureHandle createTexture(int width, int height, std::uint64_t flags = pointSampling) {
		if (width <= 0 || height <= 0 || width > bgfx::getCaps()->limits.maxTextureSize ||
				height > bgfx::getCaps()->limits.maxTextureSize)
			throw std::runtime_error("Texture dimensions exceed bgfx device limits");
		return require(bgfx::createTexture2D(std::uint16_t(width), std::uint16_t(height), false, 1,
			bgfx::TextureFormat::RGBA8, flags), "Cannot allocate bgfx texture");
	}

	bgfx::FrameBufferHandle target(int width, int height, bool depth) {
		auto color = createTexture(width, height, pointSampling | BGFX_TEXTURE_RT);
		if (!depth) {
			const auto result = bgfx::createFrameBuffer(1, &color, true);
			if (!bgfx::isValid(result)) bgfx::destroy(color);
			return require(result, "Cannot create bgfx composition target");
		}
		bgfx::TextureHandle attachments[]{color, bgfx::createTexture2D(std::uint16_t(width),
			std::uint16_t(height), false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY)};
		if (!bgfx::isValid(attachments[1])) {
			bgfx::destroy(color);
			throw std::runtime_error("Cannot create bgfx depth attachment");
		}
		const auto result = bgfx::createFrameBuffer(2, attachments, true);
		if (!bgfx::isValid(result)) for (const auto attachment : attachments) bgfx::destroy(attachment);
		return require(result, "Cannot create bgfx indoor target");
	}

	void draw(bgfx::ViewId view, bgfx::TextureHandle image, const std::array<IndoorVertex, 4> &quad,
			std::uint64_t state, std::uint32_t sampling = std::uint32_t(pointSampling)) {
		if (bgfx::getAvailTransientVertexBuffer(6, layout) != 6)
			throw std::runtime_error("bgfx transient geometry buffer exhausted");
		bgfx::TransientVertexBuffer buffer;
		bgfx::allocTransientVertexBuffer(&buffer, 6, layout);
		const IndoorVertex vertices[]{quad[0], quad[1], quad[2], quad[0], quad[2], quad[3]};
		std::memcpy(buffer.data, vertices, sizeof(vertices));
		bgfx::setVertexBuffer(0, &buffer);
		bgfx::setTexture(0, sampler, image, sampling);
		bgfx::setState(state);
		bgfx::submit(view, program);
	}

	void rectangle(bgfx::ViewId view, bgfx::TextureHandle image, float x, float y, float width, float height,
			bool targetImage = false, bool blend = false, bool linear = false) {
		const float top = targetImage && bgfx::getCaps()->originBottomLeft ? 1.f : 0.f;
		const float bottom = 1 - top;
		draw(view, image, {{{x, y + height, 0, 0, bottom}, {x + width, y + height, 0, 1, bottom},
			{x + width, y, 0, 1, top}, {x, y, 0, 0, top}}},
			colorWrite | (blend ? BGFX_STATE_BLEND_ALPHA : 0),
			linear ? BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP : std::uint32_t(pointSampling));
	}

	void updateFrame(const IndexedFrame &supplied, bool modern) {
		if (!supplied.isValid() || supplied.width != frameWidth || supplied.height != frameHeight)
			throw std::invalid_argument("Invalid bgfx framebuffer or changed dimensions");
		enhanced = modern && bool(supplied.indoorScene);
		if (enhanced && (frameWidth != 320 || frameHeight != 200 ||
				supplied.indoorScene->originalPixels.size() != supplied.pixels.size() ||
				supplied.indoorScene->interfaceCoverage.size() != supplied.pixels.size()))
			throw std::invalid_argument("Invalid indoor scene overlay coverage");
		std::vector<std::uint8_t> rgba(supplied.pixels.size() * 4);
		for (std::size_t i = 0; i < supplied.pixels.size(); ++i) {
			const auto p = std::size_t(supplied.pixels[i]) * 3;
			for (unsigned c = 0; c < 3; ++c) rgba[i * 4 + c] = supplied.palette[p + c];
			bool transparent = false;
			if (enhanced) {
				const auto &scene = *supplied.indoorScene;
				const auto x = i % frameWidth, y = i / frameWidth;
				transparent = x >= 8 && x < 224 && y >= 8 && y < 140 &&
					!scene.interfaceCoverage[i] && supplied.pixels[i] == scene.originalPixels[i];
			}
			rgba[i * 4 + 3] = transparent ? 0 : 255;
		}
		bgfx::updateTexture2D(frameTexture, 0, 0, 0, 0, std::uint16_t(frameWidth), std::uint16_t(frameHeight),
			bgfx::copy(rgba.data(), std::uint32_t(rgba.size())));
		frame = supplied;
	}

	bgfx::TextureHandle uploadTexture(const std::shared_ptr<const IndoorTexture> &source) {
		const auto found = textures.find(source.get());
		if (found != textures.end()) return found->second.handle;
		const auto count = std::size_t(source->width) * source->height;
		if (source->indices.size() != count || source->alpha.size() != count)
			throw std::invalid_argument("Invalid indoor texture payload");
		std::vector<std::uint8_t> rgba(count * 4);
		for (std::size_t i = 0; i < count; ++i) {
			const auto p = std::size_t(source->indices[i]) * 3;
			for (unsigned c = 0; c < 3; ++c) rgba[i * 4 + c] = palette[p + c];
			rgba[i * 4 + 3] = source->alpha[i];
		}
		auto image = createTexture(source->width, source->height);
		bgfx::updateTexture2D(image, 0, 0, 0, 0, std::uint16_t(source->width), std::uint16_t(source->height),
			bgfx::copy(rgba.data(), std::uint32_t(rgba.size())));
		textures.emplace(source.get(), Texture{source, image});
		return image;
	}

	void present(std::optional<std::pair<int, int>> cursorPosition) {
		int width = 0, height = 0;
		SDL_GetWindowSizeInPixels(window, &width, &height);
		if (width <= 0 || height <= 0 || !frame.isValid()) return;
		if (width > UINT16_MAX || height > UINT16_MAX) throw std::runtime_error("bgfx window is too large");
		if (width != outputWidth || height != outputHeight) {
			chain.width = std::uint32_t(width); chain.height = std::uint32_t(height);
			bgfx::reset(resetFlags, &chain);
			outputWidth = width; outputHeight = height;
		}
		const auto aspect = corrected ? PictureAspect::FourThree : PictureAspect::Square;
		const auto picture = pictureRect(width, height, frameWidth, frameHeight, aspect);
		const int limit = bgfx::getCaps()->limits.maxTextureSize;
		const int wanted = picturePrescale(picture, frameWidth, frameHeight, limit, limit);
		if (wanted < 1) throw std::runtime_error("bgfx texture limit is smaller than the picture");
		if (compositionScale != wanted) {
			releaseHandle(compositionTarget);
			compositionTarget = target(frameWidth * wanted, frameHeight * wanted, false);
			compositionScale = wanted;
		}
		if (enhanced) {
			if (!bgfx::isValid(worldTarget)) worldTarget = target(216 * 3, 132 * 3, true);
			for (auto it = textures.begin(); it != textures.end();) {
				if (it->second.source.expired() || palette != frame.palette) {
					releaseHandle(it->second.handle); it = textures.erase(it);
				} else ++it;
			}
			palette = frame.palette;
			bgfx::setViewFrameBuffer(worldView, worldTarget);
			bgfx::setViewRect(worldView, 0, 0, 216 * 3, 132 * 3);
			bgfx::setViewClear(worldView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.f, 0);
			const auto &scene = *frame.indoorScene;
			// Set back the virtual eye, but keep clipping at the party rather
			// than exposing walls behind it. The original 4:3 stretch happens
			// during presentation, not in this native-pixel calibration.
			const float nearPlane = scene.cameraSetback + 0.04f;
			const float farPlane = scene.cameraSetback + 12.f;
			std::array<float, 16> view{};
			view[0] = view[5] = view[10] = view[15] = 1;
			view[14] = -scene.cameraSetback;
			std::array<float, 16> projection{};
			projection[0] = 2 * scene.focalLength / 216.f;
			projection[5] = 2 * scene.focalLength / 132.f;
			projection[9] = 2 * scene.horizon / 132.f - 1;
			projection[11] = -1;
			if (bgfx::getCaps()->homogeneousDepth) {
				projection[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
				projection[14] = -2 * farPlane * nearPlane / (farPlane - nearPlane);
			} else {
				projection[10] = -farPlane / (farPlane - nearPlane);
				projection[14] = -farPlane * nearPlane / (farPlane - nearPlane);
			}
			bgfx::setViewTransform(worldView, view.data(), projection.data());
			bgfx::touch(worldView);
			if (!frame.indoorScene->black) for (const auto &quad : frame.indoorScene->quads)
				draw(worldView, uploadTexture(quad.texture), quad.vertices,
					colorWrite | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL);
		}
		bgfx::setViewFrameBuffer(compositionView, compositionTarget);
		bgfx::setViewRect(compositionView, 0, 0, std::uint16_t(frameWidth * wanted), std::uint16_t(frameHeight * wanted));
		bgfx::setViewClear(compositionView, BGFX_CLEAR_COLOR, 0x000000ff);
		const auto compositionProjection = ortho(float(frameWidth), float(frameHeight));
		bgfx::setViewTransform(compositionView, nullptr, compositionProjection.data());
		if (enhanced) rectangle(compositionView, bgfx::getTexture(worldTarget), 8, 8, 216, 132, true);
		rectangle(compositionView, frameTexture, 0, 0, float(frameWidth), float(frameHeight), false, true);
		if (cursorPosition && bgfx::isValid(cursorTexture))
			rectangle(compositionView, cursorTexture, float(cursorPosition->first), float(cursorPosition->second),
				float(cursorWidth), float(cursorHeight), false, true);
		bgfx::setViewFrameBuffer(displayView, BGFX_INVALID_HANDLE);
		bgfx::setViewRect(displayView, 0, 0, std::uint16_t(width), std::uint16_t(height));
		bgfx::setViewClear(displayView, BGFX_CLEAR_COLOR, 0x000000ff);
		const auto displayProjection = ortho(float(width), float(height));
		bgfx::setViewTransform(displayView, nullptr, displayProjection.data());
		rectangle(displayView, bgfx::getTexture(compositionTarget), float(picture.x), float(picture.y),
			float(picture.width), float(picture.height), true, false, corrected);
		// Single-threaded bgfx presents before returning: acquisition still follows actual submission.
		bgfx::frame();
	}
};

IndoorGpuRenderer::IndoorGpuRenderer(SDL_Window *window, int width, int height, bool corrected) :
	_impl(std::make_unique<Impl>(window, width, height, corrected)) {
	_impl->initialize();
}
IndoorGpuRenderer::~IndoorGpuRenderer() = default;
void IndoorGpuRenderer::upload(const IndexedFrame &frame, bool modern) { _impl->updateFrame(frame, modern); }
void IndoorGpuRenderer::present(std::optional<std::pair<int, int>> cursorPosition) { _impl->present(cursorPosition); }
std::pair<int, int> IndoorGpuRenderer::outputSize() const {
	int width = 0, height = 0;
	SDL_GetWindowSizeInPixels(_impl->window, &width, &height);
	return {width, height};
}
void IndoorGpuRenderer::cursor(const IndexedFrame &frame) {
	if (!frame.isValid()) throw std::invalid_argument("Invalid original cursor");
	auto &r = *_impl;
	releaseHandle(r.cursorTexture);
	r.cursorTexture = r.createTexture(frame.width, frame.height);
	r.cursorWidth = frame.width; r.cursorHeight = frame.height;
	std::vector<std::uint8_t> rgba(frame.pixels.size() * 4);
	for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
		const auto p = std::size_t(frame.pixels[i]) * 3;
		for (unsigned c = 0; c < 3; ++c) rgba[i * 4 + c] = frame.palette[p + c];
		rgba[i * 4 + 3] = frame.pixels[i] ? 255 : 0;
	}
	bgfx::updateTexture2D(r.cursorTexture, 0, 0, 0, 0, std::uint16_t(frame.width), std::uint16_t(frame.height),
		bgfx::copy(rgba.data(), std::uint32_t(rgba.size())));
}
} // namespace mmodern
