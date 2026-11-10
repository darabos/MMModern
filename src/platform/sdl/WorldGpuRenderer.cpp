#include "platform/sdl/WorldGpuRenderer.h"
#include "platform/sdl/SdlWindowPlacement.h"
#include "platform/sdl/WorldCameraSpring.h"
#include "core/WorldRenderScene.h"
#include "core/UiRenderScene.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#if defined(__APPLE__)
#include <SDL_metal.h>
#include <CoreGraphics/CoreGraphics.h>
#endif
#include <bgfx/bgfx.h>

#include "vs_textured.h"
#include "fs_textured.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <thread>

namespace mmodern {
namespace {
constexpr std::uint64_t pointSampling = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
	BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr std::uint64_t worldSampling = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_POINT |
	BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr std::uint64_t colorWrite = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
constexpr std::uint32_t resetFlags = BGFX_RESET_VSYNC | BGFX_RESET_FLIP_AFTER_RENDER |
	BGFX_RESET_MAXANISOTROPY;
constexpr bgfx::ViewId worldView = 0, compositionView = 1, displayView = 2;
enum MeshFlags : unsigned { Distort = 1, Wall = 2, Billboard = 4 };
bool deviceActive = false;
struct MeshVertex {
	float x, y, z, u, v, anchorX, anchorZ, normalX, normalZ;
};
static_assert(sizeof(MeshVertex) == 9 * sizeof(float), "bgfx vertex layout must match MeshVertex");
static_assert(sizeof(WorldVertex) == 5 * sizeof(float), "WorldVertex must have no padding");

struct BatchKey {
	const WorldTexture *texture;
	int x, z;
	unsigned flags;
	bool operator==(const BatchKey &other) const {
		return texture == other.texture && x == other.x && z == other.z && flags == other.flags;
	}
};
struct BatchKeyHash {
	std::size_t operator()(const BatchKey &key) const {
		auto hash = std::hash<const WorldTexture *>{}(key.texture);
		for (const auto value : {unsigned(key.x), unsigned(key.z), key.flags})
			hash = (hash ^ value) * std::size_t(1099511628211ull);
		return hash;
	}
};

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

std::size_t meshHash(const std::vector<MeshVertex> &vertices, unsigned flags) {
	std::size_t hash = flags;
	for (const auto &vertex : vertices) {
		std::array<std::uint32_t, 9> words;
		std::memcpy(words.data(), &vertex, sizeof(vertex));
		for (const auto word : words) hash = (hash ^ word) * std::size_t(1099511628211ull);
	}
	return hash;
}

MeshVertex interpolate(const MeshVertex &a, const MeshVertex &b, float t) {
	const auto mix = [t](float x, float y) { return x + (y - x) * t; };
	return {mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z), mix(a.u, b.u), mix(a.v, b.v),
		mix(a.anchorX, b.anchorX), mix(a.anchorZ, b.anchorZ),
		mix(a.normalX, b.normalX), mix(a.normalZ, b.normalZ)};
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

struct GpuDevice {
	bgfx::PlatformData platform;
	bgfx::SwapChain initialChain;
	int frameWidth, frameHeight, outputWidth = 0, outputHeight = 0, compositionScale = 0;
	bool corrected, initialized = false, enhanced = false;
	bgfx::SwapChain chain;
	bgfx::VertexLayout layout;
	bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle distanceWarp = BGFX_INVALID_HANDLE;
	bgfx::UniformHandle geometry = BGFX_INVALID_HANDLE, cameraPosition = BGFX_INVALID_HANDLE;
	bgfx::VertexBufferHandle rectangleVertices = BGFX_INVALID_HANDLE;
	bgfx::IndexBufferHandle rectangleIndices = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle frameTexture = BGFX_INVALID_HANDLE, cursorTexture = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle overlayTexture = BGFX_INVALID_HANDLE;
	bgfx::VertexBufferHandle uiVertices = BGFX_INVALID_HANDLE;
	struct UiDraw {
		std::shared_ptr<const WorldTexture> texture;
		bgfx::TextureHandle image = BGFX_INVALID_HANDLE;
		std::uint32_t first = 0, count = 0;
	};
	std::vector<UiDraw> uiDraws;
	std::shared_ptr<const UiRenderScene> uiScene;
	bool gpuHud = false;
	bgfx::FrameBufferHandle worldTarget = BGFX_INVALID_HANDLE, compositionTarget = BGFX_INVALID_HANDLE;
	int cursorWidth = 0, cursorHeight = 0;
	IndexedFrame frame;
	WorldCameraSpring camera;
	std::shared_ptr<const std::uint8_t> cameraContinuity;
	std::chrono::steady_clock::time_point cameraSample = std::chrono::steady_clock::now();
	bool modernEnabled = false;
	void advanceCamera() {
		const auto now = std::chrono::steady_clock::now();
		camera.advance(std::chrono::duration<double>(now - cameraSample).count());
		cameraSample = now;
	}
	struct Texture {
		std::weak_ptr<const WorldTexture> source;
		bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
		bgfx::TextureHandle mipHandle = BGFX_INVALID_HANDLE;
	};
	std::unordered_map<const WorldTexture *, Texture> textures;
	std::array<std::uint8_t, 768> palette{};
	struct Mesh {
		std::vector<MeshVertex> source;
		unsigned flags = 0;
		float x = 0, z = 0, radius = 0;
		bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
		bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
		~Mesh() { releaseHandle(vertices); releaseHandle(indices); }
	};
	struct Batch {
		std::shared_ptr<Mesh> mesh;
		std::shared_ptr<const WorldTexture> texture;
		bgfx::TextureHandle image = BGFX_INVALID_HANDLE;
	};
	std::vector<Batch> batches;
	std::unordered_multimap<std::size_t, std::weak_ptr<Mesh>> meshCache;
	std::shared_ptr<const WorldRenderScene> meshScene;

	GpuDevice(bgfx::PlatformData platform, bgfx::SwapChain chain, int width, int height, bool corrected) :
		platform(platform), initialChain(chain), frameWidth(width), frameHeight(height), corrected(corrected) {}

	~GpuDevice() {
		if (!initialized) return;
		batches.clear();
		releaseHandle(rectangleVertices); releaseHandle(rectangleIndices);
		releaseHandle(uiVertices); releaseHandle(overlayTexture);
		for (auto &entry : textures) {
			releaseHandle(entry.second.handle);
			releaseHandle(entry.second.mipHandle);
		}
		releaseHandle(worldTarget); releaseHandle(compositionTarget);
		releaseHandle(frameTexture); releaseHandle(cursorTexture);
		releaseHandle(program); releaseHandle(sampler);
		releaseHandle(distanceWarp);
		releaseHandle(geometry); releaseHandle(cameraPosition);
		bgfx::shutdown();
		deviceActive = false;
	}

	void initialize() {
		if (deviceActive) throw std::runtime_error("Cannot initialize a second bgfx presentation loop");
		const int width = int(initialChain.width), height = int(initialChain.height);
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
		chain = initialChain;
		init.platformData = platform;
		init.swapChain = chain;
		init.reset = resetFlags;
		if (!bgfx::init(init)) throw std::runtime_error("Cannot initialize the bgfx graphics device");
		initialized = true;
		deviceActive = true;
		outputWidth = width; outputHeight = height;
		layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float).end();
		const auto vertex = require(bgfx::createShader(bgfx::copy(vs_textured, sizeof(vs_textured))),
			"Cannot create bgfx vertex shader");
		auto fragment = bgfx::createShader(bgfx::copy(fs_textured, sizeof(fs_textured)));
		if (!bgfx::isValid(fragment)) {
			bgfx::destroy(vertex);
			throw std::runtime_error("Cannot create bgfx fragment shader");
		}
		program = require(bgfx::createProgram(vertex, fragment, true), "Cannot link bgfx texture program");
		sampler = require(bgfx::createUniform("s_image", bgfx::UniformType::Sampler), "Cannot create bgfx sampler");
		distanceWarp = require(bgfx::createUniform("u_distanceWarp", bgfx::UniformType::Vec4),
			"Cannot create bgfx distance warp uniform");
		geometry = require(bgfx::createUniform("u_geometry", bgfx::UniformType::Vec4),
			"Cannot create bgfx geometry uniform");
		cameraPosition = require(bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4),
			"Cannot create bgfx camera uniform");
		const MeshVertex unitQuad[]{{0, 1, 0, 0, 1, 0, 0, 0, 0}, {1, 1, 0, 1, 1, 0, 0, 0, 0},
			{1, 0, 0, 1, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 0}};
		const std::uint16_t unitIndices[]{0, 1, 2, 0, 2, 3};
		rectangleVertices = require(bgfx::createVertexBuffer(bgfx::copy(unitQuad, sizeof(unitQuad)), layout),
			"Cannot create bgfx presentation quad");
		rectangleIndices = require(bgfx::createIndexBuffer(bgfx::copy(unitIndices, sizeof(unitIndices))),
			"Cannot create bgfx presentation indices");
		frameTexture = createTexture(frameWidth, frameHeight);
		for (auto view : {worldView, compositionView, displayView}) bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
		bgfx::setViewName(worldView, "World");
		bgfx::setViewName(compositionView, "Original UI and cursor");
		bgfx::setViewName(displayView, "Window presentation");
		std::cerr << "bgfx presenter: " << bgfx::getRendererName(bgfx::getRendererType()) << '\n';
	}

	bgfx::TextureHandle createTexture(int width, int height, std::uint64_t flags = pointSampling,
			bool mipmaps = false) {
		if (width <= 0 || height <= 0 || width > bgfx::getCaps()->limits.maxTextureSize ||
				height > bgfx::getCaps()->limits.maxTextureSize)
			throw std::runtime_error("Texture dimensions exceed bgfx device limits");
		return require(bgfx::createTexture2D(std::uint16_t(width), std::uint16_t(height), mipmaps, 1,
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
		return require(result, "Cannot create bgfx world target");
	}

	void rectangle(bgfx::ViewId view, bgfx::TextureHandle image, float x, float y, float width, float height,
			bool targetImage = false, bool blend = false, bool linear = false) {
		std::array<float, 16> model{};
		model[0] = width; model[5] = height; model[10] = model[15] = 1;
		model[12] = x; model[13] = y;
		const std::array<float, 4> warp{};
		const std::array<float, 4> options{{0, targetImage && bgfx::getCaps()->originBottomLeft ? 1.f : 0.f, 0, 0}};
		bgfx::setTransform(model.data());
		bgfx::setUniform(distanceWarp, warp.data());
		bgfx::setUniform(geometry, options.data());
		bgfx::setVertexBuffer(0, rectangleVertices);
		bgfx::setIndexBuffer(rectangleIndices);
		bgfx::setTexture(0, sampler, image,
			linear ? BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP : std::uint32_t(pointSampling));
		bgfx::setState(colorWrite | (blend ? BGFX_STATE_BLEND_ALPHA : 0));
		bgfx::submit(view, program);
	}

	std::shared_ptr<Mesh> prepareMesh(std::vector<MeshVertex> source, unsigned flags) {
		const auto hash = meshHash(source, flags);
		const auto range = meshCache.equal_range(hash);
		for (auto it = range.first; it != range.second; ++it) {
			const auto mesh = it->second.lock();
			if (mesh && mesh->flags == flags && mesh->source.size() == source.size() &&
					std::memcmp(mesh->source.data(), source.data(), source.size() * sizeof(MeshVertex)) == 0)
				return mesh;
		}
		auto mesh = std::make_shared<Mesh>();
		mesh->source = std::move(source); mesh->flags = flags;
		float minX = std::numeric_limits<float>::max(), minZ = minX;
		float maxX = -minX, maxZ = -minX;
		for (const auto &v : mesh->source) {
			const float radius = flags & Billboard ? std::hypot(v.x, v.z) : 0;
			const float x = flags & Billboard ? v.anchorX : v.x, z = flags & Billboard ? v.anchorZ : v.z;
			minX = std::min(minX, x - radius); maxX = std::max(maxX, x + radius);
			minZ = std::min(minZ, z - radius); maxZ = std::max(maxZ, z + radius);
		}
		mesh->x = (minX + maxX) / 2; mesh->z = (minZ + maxZ) / 2;
		mesh->radius = std::hypot(maxX - minX, maxZ - minZ) / 2;
		std::vector<MeshVertex> vertices;
		std::vector<std::uint32_t> indices;
		for (std::size_t q = 0; q < mesh->source.size(); q += 4) {
			const auto *quad = mesh->source.data() + q;
			const auto segments = [&](int a, int b, int c, int d) {
				if (!(flags & Distort) || (flags & Billboard)) return 1u;
				const float length = std::max(std::hypot(quad[a].x - quad[b].x, quad[a].z - quad[b].z),
					std::hypot(quad[c].x - quad[d].x, quad[c].z - quad[d].z));
				if (!std::isfinite(length) || double(length) * 8 > UINT32_MAX - 1)
					throw std::runtime_error("Invalid world GPU mesh dimensions");
				return std::max(1u, static_cast<unsigned>(std::ceil(length * 8)));
			};
			// A fixed world-space grid survives every camera pose. Only the
			// projection warp is view-dependent, and now runs entirely on GPU.
			const auto nx = segments(0, 1, 3, 2), ny = segments(0, 3, 1, 2);
			const auto base = vertices.size();
			const auto vertexCount = base + std::uint64_t(nx + 1) * (ny + 1);
			if (vertexCount > UINT32_MAX / sizeof(MeshVertex) ||
					std::uint64_t(nx) * ny > (UINT32_MAX / sizeof(std::uint32_t) - indices.size()) / 6)
				throw std::runtime_error("World GPU mesh exceeds buffer limits");
			for (unsigned y = 0; y <= ny; ++y) {
				const auto left = interpolate(quad[0], quad[3], float(y) / ny);
				const auto right = interpolate(quad[1], quad[2], float(y) / ny);
				for (unsigned x = 0; x <= nx; ++x)
					vertices.push_back(interpolate(left, right, float(x) / nx));
			}
			for (unsigned y = 0; y < ny; ++y) for (unsigned x = 0; x < nx; ++x) {
				const auto a = std::uint32_t(base + y * (nx + 1) + x), b = a + 1, c = b + nx + 1, d = c - 1;
				indices.insert(indices.end(), {a, b, c, a, c, d});
			}
		}
		mesh->vertices = require(bgfx::createVertexBuffer(bgfx::copy(vertices.data(),
			std::uint32_t(vertices.size() * sizeof(MeshVertex))), layout), "Cannot create world GPU mesh");
		mesh->indices = require(bgfx::createIndexBuffer(bgfx::copy(indices.data(),
			std::uint32_t(indices.size() * sizeof(std::uint32_t))), BGFX_BUFFER_INDEX32),
			"Cannot create world GPU mesh indices");
		meshCache.emplace(hash, mesh);
		return mesh;
	}

	void prepareScene(const std::shared_ptr<const WorldRenderScene> &scene) {
		if (meshScene == scene) return;
		if (meshScene && scene && meshScene->quads.size() == scene->quads.size()) {
			const bool unchanged = std::equal(scene->quads.begin(), scene->quads.end(), meshScene->quads.begin(),
				[](const auto &a, const auto &b) {
					return a.texture == b.texture && a.distort == b.distort && a.wall == b.wall &&
						a.billboard == b.billboard && a.anchorX == b.anchorX && a.anchorZ == b.anchorZ &&
						std::memcmp(a.vertices.data(), b.vertices.data(), sizeof(a.vertices)) == 0;
				});
			if (unchanged) { meshScene = scene; return; }
		}
		struct SourceBatch {
			std::shared_ptr<const WorldTexture> texture;
			std::vector<MeshVertex> vertices;
			unsigned flags;
		};
		std::vector<SourceBatch> sources;
		std::unordered_map<BatchKey, std::size_t, BatchKeyHash> groups;
		if (scene) for (const auto &quad : scene->quads) {
			const unsigned flags = (quad.distort ? Distort : 0u) | (quad.wall ? Wall : 0u) | (quad.billboard ? Billboard : 0u);
			const float x = quad.billboard ? quad.anchorX : quad.vertices[0].x;
			const float z = quad.billboard ? quad.anchorZ : quad.vertices[0].z;
			const BatchKey key{quad.texture.get(), int(std::floor(x / 4)), int(std::floor(z / 4)), flags};
			const auto entry = groups.emplace(key, sources.size());
			if (entry.second) sources.push_back({quad.texture, {}, flags});
			auto &source = sources[entry.first->second].vertices;
			const auto &a = quad.vertices[0], &b = quad.vertices[1];
			const float normalX = quad.wall ? -(b.z - a.z) : 0, normalZ = quad.wall ? b.x - a.x : 0;
			for (const auto &v : quad.vertices)
				source.push_back({v.x, v.y, v.z, v.u, v.v, quad.anchorX, quad.anchorZ, normalX, normalZ});
		}
		std::vector<Batch> prepared;
		prepared.reserve(sources.size());
		for (auto &source : sources)
			prepared.push_back({prepareMesh(std::move(source.vertices), source.flags), std::move(source.texture)});
		batches = std::move(prepared);
		meshScene = scene;
		for (auto it = meshCache.begin(); it != meshCache.end();) {
			if (it->second.expired()) it = meshCache.erase(it);
			else ++it;
		}
	}

	void prepareUi(const std::shared_ptr<const UiRenderScene> &scene) {
		if (uiScene == scene) return;
		if (uiScene && uiScene->quads.size() == scene->quads.size() &&
				std::equal(uiScene->quads.begin(),uiScene->quads.end(),scene->quads.begin(),[](const auto &a,const auto &b) {
					return a.texture==b.texture && a.x==b.x && a.y==b.y && a.left==b.left &&
						a.top==b.top && a.right==b.right && a.bottom==b.bottom;
				})) { uiScene=scene; return; }
		std::vector<MeshVertex> vertices;
		std::vector<UiDraw> draws;
		for (const auto &quad : scene->quads) {
			if (!quad.texture || quad.texture->width <= 0 || quad.texture->height <= 0)
				throw std::invalid_argument("Invalid GPU HUD texture");
			const auto &t = *quad.texture;
			const int left = std::max({0,quad.left,quad.x}), top = std::max({0,quad.top,quad.y});
			const int right = std::min({frameWidth,quad.right,quad.x+t.width});
			const int bottom = std::min({frameHeight,quad.bottom,quad.y+t.height});
			if (left >= right || top >= bottom) continue;
			const auto vertex = [&](int x,int y) {
				return MeshVertex{float(x),float(y),0,float(x-quad.x)/t.width,float(y-quad.y)/t.height,0,0,0,0};
			};
			if (draws.empty() || draws.back().texture != quad.texture)
				draws.push_back({quad.texture,BGFX_INVALID_HANDLE,std::uint32_t(vertices.size()),0});
			vertices.insert(vertices.end(),{vertex(left,bottom),vertex(right,bottom),vertex(right,top),
				vertex(left,bottom),vertex(right,top),vertex(left,top)});
			draws.back().count += 6;
		}
		bgfx::VertexBufferHandle buffer = BGFX_INVALID_HANDLE;
		if (!vertices.empty())
			buffer = require(bgfx::createVertexBuffer(bgfx::copy(vertices.data(),
				std::uint32_t(vertices.size()*sizeof(MeshVertex))),layout),"Cannot create GPU HUD geometry");
		releaseHandle(uiVertices);
		uiVertices = buffer; uiDraws = std::move(draws); uiScene = scene;
	}

	void updateFrame(const IndexedFrame &supplied, bool modern, bool allowGameplayLayers) {
		if (!supplied.isValid() || supplied.width != frameWidth || supplied.height != frameHeight)
			throw std::invalid_argument("Invalid bgfx framebuffer or changed dimensions");
		advanceCamera();
		if (modern && supplied.worldScene) {
			const auto &scene = *supplied.worldScene;
			camera.target(scene.camera, !modernEnabled || cameraContinuity != scene.cameraContinuity);
			cameraContinuity = scene.cameraContinuity;
		}
		modernEnabled = modern;
		enhanced = modern && allowGameplayLayers && bool(supplied.worldScene);
		gpuHud = allowGameplayLayers && bool(supplied.uiScene);
		if (gpuHud && (supplied.uiScene->basePixels.size()!=supplied.pixels.size() ||
				supplied.uiScene->referencePixels.size()!=supplied.pixels.size() ||
				(enhanced && supplied.uiScene->effectCoverage.size()!=supplied.pixels.size())))
			throw std::invalid_argument("Invalid GPU HUD framebuffer layers");
		if (enhanced && (frameWidth != 320 || frameHeight != 200 ||
				supplied.worldScene->originalPixels.size() != supplied.pixels.size() ||
				supplied.worldScene->interfaceCoverage.size() != supplied.pixels.size()))
			throw std::invalid_argument("Invalid world scene overlay coverage");
		if (enhanced) prepareScene(supplied.worldScene);
		if (gpuHud) prepareUi(supplied.uiScene);
		if (enhanced || gpuHud) {
			for (auto it = textures.begin(); it != textures.end();) {
				if (it->second.source.expired() || palette != supplied.palette) {
					releaseHandle(it->second.handle);
					releaseHandle(it->second.mipHandle);
					it = textures.erase(it);
				} else ++it;
			}
			palette = supplied.palette;
			if (enhanced) for (auto &batch : batches) batch.image = uploadTexture(batch.texture, true);
			if (gpuHud) for (auto &draw : uiDraws) draw.image = uploadTexture(draw.texture);
		}
		std::vector<std::uint8_t> rgba(supplied.pixels.size() * 4);
		std::vector<std::uint8_t> overlay;
		if (gpuHud) overlay.resize(rgba.size());
		for (std::size_t i = 0; i < supplied.pixels.size(); ++i) {
			const auto p = std::size_t(gpuHud ? supplied.uiScene->basePixels[i] : supplied.pixels[i]) * 3;
			for (unsigned c = 0; c < 3; ++c) rgba[i * 4 + c] = supplied.palette[p + c];
			bool transparent = false;
			if (enhanced) {
				const auto &scene = *supplied.worldScene;
				const auto x = i % frameWidth, y = i / frameWidth;
				transparent = x >= 8 && x < 224 && y >= 8 && y < 140 &&
					(gpuHud ? !supplied.uiScene->effectCoverage[i] :
						!scene.interfaceCoverage[i] && supplied.pixels[i] == scene.originalPixels[i]);
			}
			rgba[i * 4 + 3] = transparent ? 0 : 255;
			if (gpuHud) {
				const auto color = std::size_t(supplied.pixels[i])*3;
				for (unsigned c=0;c<3;++c) overlay[i*4+c]=supplied.palette[color+c];
				overlay[i*4+3]=supplied.pixels[i]!=supplied.uiScene->referencePixels[i] ? 255 : 0;
			}
		}
		bgfx::updateTexture2D(frameTexture, 0, 0, 0, 0, std::uint16_t(frameWidth), std::uint16_t(frameHeight),
			bgfx::copy(rgba.data(), std::uint32_t(rgba.size())));
		if (gpuHud) {
			if (!bgfx::isValid(overlayTexture)) overlayTexture=createTexture(frameWidth,frameHeight);
			bgfx::updateTexture2D(overlayTexture,0,0,0,0,std::uint16_t(frameWidth),std::uint16_t(frameHeight),
				bgfx::copy(overlay.data(),std::uint32_t(overlay.size())));
		}
		frame = supplied;
	}

	bgfx::TextureHandle uploadTexture(const std::shared_ptr<const WorldTexture> &source, bool mipmaps = false) {
		const auto found = textures.find(source.get());
		if (found != textures.end()) {
			const auto image = mipmaps ? found->second.mipHandle : found->second.handle;
			if (bgfx::isValid(image)) return image;
		}
		if (source->width <= 0 || source->height <= 0)
			throw std::invalid_argument("Invalid world texture dimensions");
		const auto count = std::size_t(source->width) * source->height;
		if (source->indices.size() != count || source->alpha.size() != count)
			throw std::invalid_argument("Invalid world texture payload");
		std::vector<std::uint8_t> rgba(count * 4);
		for (std::size_t i = 0; i < count; ++i) {
			const auto p = std::size_t(source->indices[i]) * 3;
			for (unsigned c = 0; c < 3; ++c) rgba[i * 4 + c] = palette[p + c];
			rgba[i * 4 + 3] = source->alpha[i];
		}
		const auto sampling = mipmaps ? worldSampling : pointSampling;
		auto image = createTexture(source->width, source->height, sampling, mipmaps);
		int width = source->width, height = source->height;
		for (std::uint8_t level = 0;; ++level) {
			bgfx::updateTexture2D(image, 0, level, 0, 0, std::uint16_t(width), std::uint16_t(height),
				bgfx::copy(rgba.data(), std::uint32_t(rgba.size())));
			if (!mipmaps || (width == 1 && height == 1)) break;
			const int nextWidth = std::max(1,width/2), nextHeight = std::max(1,height/2);
			std::vector<std::uint8_t> next(std::size_t(nextWidth)*nextHeight*4);
			for (int y = 0; y < nextHeight; ++y) for (int x = 0; x < nextWidth; ++x) {
				unsigned alpha = 0, samples = 0;
				std::array<unsigned,3> color{};
				// Include odd edge texels and average in premultiplied-alpha space
				// so transparent sprite borders do not introduce dark fringes.
				for (int sy = y*height/nextHeight; sy < (y+1)*height/nextHeight; ++sy)
					for (int sx = x*width/nextWidth; sx < (x+1)*width/nextWidth; ++sx) {
						const auto offset = (std::size_t(sy)*width+sx)*4;
						const unsigned a = rgba[offset+3];
						for (unsigned c = 0; c < 3; ++c) color[c] += unsigned(rgba[offset+c])*a;
						alpha += a; ++samples;
					}
				const auto offset = (std::size_t(y)*nextWidth+x)*4;
				for (unsigned c = 0; c < 3; ++c)
					next[offset+c] = alpha ? std::uint8_t((color[c]+alpha/2)/alpha) : 0;
				next[offset+3] = std::uint8_t((alpha+samples/2)/samples);
			}
			rgba = std::move(next); width = nextWidth; height = nextHeight;
		}
		auto &cached = textures[source.get()];
		cached.source = source;
		(mipmaps ? cached.mipHandle : cached.handle) = image;
		return image;
	}

	bool present(std::optional<std::pair<int, int>> cursorPosition, int width, int height) {
		advanceCamera();
		if (width <= 0 || height <= 0 || !frame.isValid()) return false;
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
			bgfx::setViewFrameBuffer(worldView, worldTarget);
			bgfx::setViewRect(worldView, 0, 0, 216 * 3, 132 * 3);
			bgfx::setViewClear(worldView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.f, 0);
			const auto &scene = *frame.worldScene;
			constexpr float nearPlane = 0.04f;
			const float farPlane = scene.cameraSetback + 12.f;
			const auto pose = camera.pose();
			const double angle = pose.turns * 1.57079632679489661923;
			const float sine = float(std::sin(angle)), cosine = float(std::cos(angle));
			std::array<float, 16> view{};
			view[0] = view[10] = cosine; view[2] = -sine; view[8] = sine;
			view[5] = view[15] = 1;
			view[12] = -cosine * float(pose.x) + sine * float(pose.y);
			view[14] = sine * float(pose.x) + cosine * float(pose.y) - scene.cameraSetback;
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
			const std::array<float, 4> eye{{float(pose.x), -float(pose.y), 0, 0}};
			std::array<float, 16> identity{};
			identity[0] = identity[5] = identity[10] = identity[15] = 1;
			if (!scene.black) for (const auto &batch : batches) {
				const auto &mesh = *batch.mesh;
				if (mesh.flags & Distort) {
					const float dx = mesh.x - float(pose.x), dy = -mesh.z - float(pose.y);
					const float right = cosine * dx - sine * dy, forward = sine * dx + cosine * dy;
					if (forward - mesh.radius > 12 || forward + mesh.radius < nearPlane - scene.cameraSetback ||
							std::abs(right) > 3 * (scene.cameraSetback + forward) + std::sqrt(10.f) * mesh.radius)
						continue;
				}
				const bool distort = mesh.flags & Distort, wall = mesh.flags & Wall, billboard = mesh.flags & Billboard;
				const std::array<float, 4> warp{{distort ? 1.f : 0.f,
					1.f - 2.f * scene.horizon / 132.f, scene.cameraSetback, wall ? 1.f : 0.f}};
				const std::array<float, 4> options{{distort ? (billboard ? 2.f : 1.f) : -1.f,
					0, distort ? -0.75f : 0.f, 0}};
				bgfx::setTransform(identity.data());
				bgfx::setUniform(distanceWarp, warp.data());
				bgfx::setUniform(geometry, options.data());
				bgfx::setUniform(cameraPosition, eye.data());
				bgfx::setVertexBuffer(0, mesh.vertices);
				bgfx::setIndexBuffer(mesh.indices);
				bgfx::setTexture(0, sampler, batch.image, std::uint32_t(worldSampling));
				bgfx::setState(colorWrite | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL);
				bgfx::submit(worldView, program);
			}
		}
		bgfx::setViewFrameBuffer(compositionView, compositionTarget);
		bgfx::setViewRect(compositionView, 0, 0, std::uint16_t(frameWidth * wanted), std::uint16_t(frameHeight * wanted));
		bgfx::setViewClear(compositionView, BGFX_CLEAR_COLOR, 0x000000ff);
		const auto compositionProjection = ortho(float(frameWidth), float(frameHeight));
		bgfx::setViewTransform(compositionView, nullptr, compositionProjection.data());
		if (enhanced) rectangle(compositionView, bgfx::getTexture(worldTarget), 8, 8, 216, 132, true);
		rectangle(compositionView, frameTexture, 0, 0, float(frameWidth), float(frameHeight), false, true);
		if (gpuHud) {
			std::array<float,16> identity{};
			identity[0]=identity[5]=identity[10]=identity[15]=1;
			const std::array<float,4> options{};
			for (const auto &draw : uiDraws) {
				bgfx::setTransform(identity.data());
				bgfx::setUniform(distanceWarp,options.data());
				bgfx::setUniform(geometry,options.data());
				bgfx::setVertexBuffer(0,uiVertices,draw.first,draw.count);
				bgfx::setTexture(0,sampler,draw.image,std::uint32_t(pointSampling));
				bgfx::setState(colorWrite | BGFX_STATE_BLEND_ALPHA);
				bgfx::submit(compositionView,program);
			}
			rectangle(compositionView,overlayTexture,0,0,float(frameWidth),float(frameHeight),false,true);
		}
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
		// All bgfx calls, including submission and destruction, belong to this thread.
		bgfx::frame();
		return true;
	}
	void cursor(const IndexedFrame &frame) {
		if (!frame.isValid()) throw std::invalid_argument("Invalid original cursor");
		releaseHandle(cursorTexture);
		cursorTexture = createTexture(frame.width, frame.height);
		cursorWidth = frame.width; cursorHeight = frame.height;
		std::vector<std::uint8_t> rgba(frame.pixels.size() * 4);
		for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
			const auto p = std::size_t(frame.pixels[i]) * 3;
			for (unsigned c = 0; c < 3; ++c) rgba[i * 4 + c] = frame.palette[p + c];
			rgba[i * 4 + 3] = frame.pixels[i] ? 255 : 0;
		}
		bgfx::updateTexture2D(cursorTexture, 0, 0, 0, 0, std::uint16_t(frame.width), std::uint16_t(frame.height),
			bgfx::copy(rgba.data(), std::uint32_t(rgba.size())));
	}
};

struct WorldGpuRenderer::Impl {
	struct HostState {
		int x = 0, y = 0, width = 0, height = 0, outputWidth = 0, outputHeight = 0;
		bool visible = false, cursorEnabled = false;
		std::optional<std::pair<int,int>> eventCursor;
	};
	struct Upload {
		IndexedFrame frame;
		bool modern, gameplayLayers;
	};
	struct Request {
		std::optional<Upload> upload;
		std::optional<IndexedFrame> cursor;
		HostState host;
		bool present = false;
		std::promise<void> complete;
	};
	SDL_Window *window;
	int frameWidth, frameHeight;
	bool corrected;
#if !defined(__APPLE__) && !defined(_WIN32)
	bool globalCursor = false;
#endif
#if defined(_WIN32)
	HWND nativeWindow = nullptr;
#endif
#if defined(__APPLE__)
	SDL_MetalView metalView = nullptr;
#endif
	std::thread thread;
	mutable std::mutex mutex;
	std::condition_variable wake;
	std::shared_ptr<Request> request;
	std::optional<HostState> pendingHost;
	std::exception_ptr failure;
	bool stopping = false;
	std::uint64_t frames = 0;
	std::optional<Upload> pendingUpload;
	bool cursorEnabled = false;

	HostState hostState(bool cursorEnabled) const {
		HostState host;
		SDL_GetWindowPosition(window,&host.x,&host.y);
		SDL_GetWindowSize(window,&host.width,&host.height);
		SDL_GetWindowSizeInPixels(window,&host.outputWidth,&host.outputHeight);
		const auto flags = SDL_GetWindowFlags(window);
		host.visible = (flags & SDL_WINDOW_SHOWN) && !(flags & SDL_WINDOW_MINIMIZED);
		host.cursorEnabled = cursorEnabled && SDL_GetMouseFocus()==window;
#if !defined(__APPLE__) && !defined(_WIN32)
		if (!globalCursor && host.cursorEnabled) {
			int x = 0, y = 0;
			SDL_GetMouseState(&x,&y);
			host.eventCursor = windowToFrame(x,y,host.width,host.height,
				host.outputWidth,host.outputHeight,frameWidth,frameHeight,
				corrected ? PictureAspect::FourThree : PictureAspect::Square);
		}
#endif
		return host;
	}

	std::optional<std::pair<int,int>> sampleCursor(const HostState &host) const {
		if (!host.cursorEnabled) return {};
		int x = 0, y = 0;
#if defined(__APPLE__)
		// CoreGraphics samples the OS, not SDL's last main-thread event pump.
		const auto event = CGEventCreate(nullptr);
		if (!event) throw std::runtime_error("Cannot sample native cursor position");
		const auto point = CGEventGetLocation(event);
		CFRelease(event);
		x = int(point.x); y = int(point.y);
#elif defined(_WIN32)
		POINT point;
		if (!GetCursorPos(&point)) throw std::runtime_error("Cannot sample native cursor position");
		if (!ScreenToClient(nativeWindow,&point)) throw std::runtime_error("Cannot map native cursor position");
		x = point.x+host.x; y = point.y+host.y;
#else
		if (!globalCursor) return host.eventCursor;
		SDL_GetGlobalMouseState(&x,&y);
#endif
		return windowToFrame(x-host.x,y-host.y,host.width,host.height,
			host.outputWidth,host.outputHeight,frameWidth,frameHeight,
			corrected ? PictureAspect::FourThree : PictureAspect::Square);
	}

	void run(bgfx::PlatformData platform, bgfx::SwapChain chain, std::promise<void> ready) noexcept {
		std::shared_ptr<Request> active;
		bool initialized = false;
		try {
			GpuDevice device(platform,chain,frameWidth,frameHeight,corrected);
			device.initialize();
			ready.set_value(); initialized = true;
			HostState host;
			bool hasFrame = false;
			std::optional<Upload> retainedUpload;
			auto nextFrame = std::chrono::steady_clock::now();
			for (;;) {
				std::optional<HostState> hostUpdate;
				{
					std::unique_lock<std::mutex> lock(mutex);
					if (hasFrame && host.visible)
						wake.wait_until(lock,nextFrame,[&] { return stopping || request || pendingHost; });
					else wake.wait(lock,[&] { return stopping || request || pendingHost; });
					if (stopping) break;
					active = std::move(request);
					hostUpdate = std::move(pendingHost);
					pendingHost.reset();
				}
				if (hostUpdate) host = *hostUpdate;
				if (active) {
					if (active->cursor) device.cursor(*active->cursor);
					if (active->upload) {
						const auto &upload = *active->upload;
						try {
							device.updateFrame(upload.frame,upload.modern,upload.gameplayLayers);
						} catch (const std::exception &error) {
							if (!retainedUpload) throw;
							const auto rejected = std::current_exception();
							// Restore a coherent last frame before resuming autonomous redraws.
							// If rollback also fails, the outer handler terminates the device.
							device.updateFrame(retainedUpload->frame,retainedUpload->modern,retainedUpload->gameplayLayers);
							std::cerr << "Render upload rejected: " << error.what() << '\n';
							active->complete.set_exception(rejected);
							active.reset();
							continue;
						}
						retainedUpload = std::move(active->upload);
						hasFrame = true;
					}
					if (active->present) host = active->host;
				}
				if (hasFrame && ((active && active->present) ||
						(host.visible && std::chrono::steady_clock::now() >= nextFrame))) {
					const auto started = std::chrono::steady_clock::now();
					if (device.present(sampleCursor(host),host.outputWidth,host.outputHeight)) {
						std::lock_guard<std::mutex> lock(mutex);
						++frames;
					}
					const auto deadline = active && active->present ? started : nextFrame;
					nextFrame = std::max(deadline + std::chrono::nanoseconds(16666667),
						std::chrono::steady_clock::now());
				}
				if (active) {
					active->complete.set_value();
					active.reset();
				}
			}
		} catch (...) {
			const auto error = std::current_exception();
			{
				std::lock_guard<std::mutex> lock(mutex);
				failure = error;
				if (request) { request->complete.set_exception(error); request.reset(); }
			}
			if (active) active->complete.set_exception(error);
			if (!initialized) ready.set_exception(error);
			try { std::rethrow_exception(error); }
			catch (const std::exception &message) { std::cerr << "Render thread failed: " << message.what() << '\n'; }
			catch (...) { std::cerr << "Render thread failed: unknown graphics error\n"; }
		}
	}

	Impl(SDL_Window *window, int width, int height, bool corrected) :
		window(window),frameWidth(width),frameHeight(height),corrected(corrected) {
		try {
#if !defined(__APPLE__) && !defined(_WIN32)
			const auto driver = SDL_GetCurrentVideoDriver();
			globalCursor = driver && std::strcmp(driver,"x11")==0;
			if (!globalCursor)
				std::cerr << "SDL backend has no global cursor query; cursor follows main-thread event samples\n";
#endif
			auto host = hostState(false);
			bgfx::PlatformData platform;
			auto chain = swapChain(window,host.outputWidth,host.outputHeight,platform);
#if defined(_WIN32)
			nativeWindow = static_cast<HWND>(chain.nwh);
#endif
#if defined(__APPLE__)
			// Cocoa layer creation must happen on the window thread, before we wait.
			metalView = SDL_Metal_CreateView(window);
			if (!metalView) throw std::runtime_error(std::string("Cannot create Metal view: ")+SDL_GetError());
			chain.nwh = SDL_Metal_GetLayer(metalView);
			if (!chain.nwh) throw std::runtime_error("Cannot obtain Metal layer");
#endif
			std::promise<void> ready;
			auto completion = ready.get_future();
			thread = std::thread([this,platform,chain,ready=std::move(ready)]() mutable {
				run(platform,chain,std::move(ready));
			});
			completion.get();
		} catch (...) {
			if (thread.joinable()) thread.join();
#if defined(__APPLE__)
			if (metalView) SDL_Metal_DestroyView(metalView);
#endif
			throw;
		}
	}
	~Impl() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
		}
		wake.notify_one();
		if (thread.joinable()) thread.join();
#if defined(__APPLE__)
		if (metalView) SDL_Metal_DestroyView(metalView);
#endif
	}
	void submit(const std::shared_ptr<Request> &next) {
		auto complete = next->complete.get_future();
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (failure) std::rethrow_exception(failure);
			if (stopping || request) throw std::logic_error("Invalid render-thread request");
			request = next;
		}
		wake.notify_one();
		complete.get();
	}
};

WorldGpuRenderer::WorldGpuRenderer(SDL_Window *window, int width, int height, bool corrected) :
	_impl(std::make_unique<Impl>(window,width,height,corrected)) {}
WorldGpuRenderer::~WorldGpuRenderer() = default;
void WorldGpuRenderer::upload(const IndexedFrame &frame, bool modern, bool allowGameplayLayers) {
	if (!frame.isValid() || frame.width!=_impl->frameWidth || frame.height!=_impl->frameHeight)
		throw std::invalid_argument("Invalid render-thread framebuffer");
	// Stage on the producer: an unpresented handoff must not leak into redraws.
	_impl->pendingUpload = Impl::Upload{frame,modern,allowGameplayLayers};
}
void WorldGpuRenderer::present(bool cursorEnabled) {
	auto request = std::make_shared<Impl::Request>();
	_impl->cursorEnabled = cursorEnabled;
	request->host = _impl->hostState(cursorEnabled);
	request->upload = std::move(_impl->pendingUpload);
	_impl->pendingUpload.reset();
	request->present = true;
	_impl->submit(request);
}
void WorldGpuRenderer::refreshWindow() {
	const auto host = _impl->hostState(_impl->cursorEnabled);
	{
		std::lock_guard<std::mutex> lock(_impl->mutex);
		if (_impl->failure) std::rethrow_exception(_impl->failure);
		_impl->pendingHost = host;
	}
	_impl->wake.notify_one();
}
void WorldGpuRenderer::mouseMoved() {
#if !defined(__APPLE__) && !defined(_WIN32)
	if (!_impl->globalCursor) refreshWindow();
#endif
}
std::uint64_t WorldGpuRenderer::presentedFrames() const {
	std::lock_guard<std::mutex> lock(_impl->mutex);
	if (_impl->failure) std::rethrow_exception(_impl->failure);
	return _impl->frames;
}
std::pair<int,int> WorldGpuRenderer::outputSize() const {
	int width = 0, height = 0;
	SDL_GetWindowSizeInPixels(_impl->window,&width,&height);
	return {width,height};
}
void WorldGpuRenderer::cursor(const IndexedFrame &frame) {
	auto request = std::make_shared<Impl::Request>();
	request->cursor = frame;
	_impl->submit(request);
}
} // namespace mmodern
