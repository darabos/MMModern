#include "platform/sdl/IndoorGpuRenderer.h"
#include "platform/sdl/SdlWindowPlacement.h"
#include "platform/sdl/IndoorCameraSpring.h"
#include "core/IndoorRenderScene.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
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

namespace mmodern {
namespace {
constexpr std::uint64_t pointSampling = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT |
	BGFX_SAMPLER_MIP_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr std::uint64_t colorWrite = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
constexpr std::uint32_t resetFlags = BGFX_RESET_VSYNC | BGFX_RESET_FLIP_AFTER_RENDER;
constexpr bgfx::ViewId worldView = 0, compositionView = 1, displayView = 2;
enum MeshFlags : unsigned { Distort = 1, Wall = 2, Billboard = 4 };
bool deviceActive = false;
struct MeshVertex {
	float x, y, z, u, v, anchorX, anchorZ, normalX, normalZ;
};
static_assert(sizeof(MeshVertex) == 9 * sizeof(float), "bgfx vertex layout must match MeshVertex");
static_assert(sizeof(IndoorVertex) == 5 * sizeof(float), "IndoorVertex must have no padding");

struct BatchKey {
	const IndoorTexture *texture;
	int x, z;
	unsigned flags;
	bool operator==(const BatchKey &other) const {
		return texture == other.texture && x == other.x && z == other.z && flags == other.flags;
	}
};
struct BatchKeyHash {
	std::size_t operator()(const BatchKey &key) const {
		auto hash = std::hash<const IndoorTexture *>{}(key.texture);
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

struct IndoorGpuRenderer::Impl {
	SDL_Window *window;
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
	bgfx::FrameBufferHandle worldTarget = BGFX_INVALID_HANDLE, compositionTarget = BGFX_INVALID_HANDLE;
	int cursorWidth = 0, cursorHeight = 0;
	IndexedFrame frame;
	IndoorCameraSpring camera;
	std::shared_ptr<const std::uint8_t> cameraContinuity;
	std::chrono::steady_clock::time_point cameraSample = std::chrono::steady_clock::now();
	bool modernEnabled = false;
	void advanceCamera() {
		const auto now = std::chrono::steady_clock::now();
		camera.advance(std::chrono::duration<double>(now - cameraSample).count());
		cameraSample = now;
	}
	struct Texture {
		std::weak_ptr<const IndoorTexture> source;
		bgfx::TextureHandle handle;
	};
	std::unordered_map<const IndoorTexture *, Texture> textures;
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
		std::shared_ptr<const IndoorTexture> texture;
		bgfx::TextureHandle image = BGFX_INVALID_HANDLE;
	};
	std::vector<Batch> batches;
	std::unordered_multimap<std::size_t, std::weak_ptr<Mesh>> meshCache;
	std::shared_ptr<const IndoorRenderScene> meshScene;

	Impl(SDL_Window *window, int width, int height, bool corrected) :
		window(window), frameWidth(width), frameHeight(height), corrected(corrected) {}

	~Impl() {
		if (!initialized) return;
		batches.clear();
		releaseHandle(rectangleVertices); releaseHandle(rectangleIndices);
		for (auto &entry : textures) releaseHandle(entry.second.handle);
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
					throw std::runtime_error("Invalid indoor GPU mesh dimensions");
				return std::max(1u, static_cast<unsigned>(std::ceil(length * 8)));
			};
			// A fixed world-space grid survives every camera pose. Only the
			// projection warp is view-dependent, and now runs entirely on GPU.
			const auto nx = segments(0, 1, 3, 2), ny = segments(0, 3, 1, 2);
			const auto base = vertices.size();
			const auto vertexCount = base + std::uint64_t(nx + 1) * (ny + 1);
			if (vertexCount > UINT32_MAX / sizeof(MeshVertex) ||
					std::uint64_t(nx) * ny > (UINT32_MAX / sizeof(std::uint32_t) - indices.size()) / 6)
				throw std::runtime_error("Indoor GPU mesh exceeds buffer limits");
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
			std::uint32_t(vertices.size() * sizeof(MeshVertex))), layout), "Cannot create indoor GPU mesh");
		mesh->indices = require(bgfx::createIndexBuffer(bgfx::copy(indices.data(),
			std::uint32_t(indices.size() * sizeof(std::uint32_t))), BGFX_BUFFER_INDEX32),
			"Cannot create indoor GPU mesh indices");
		meshCache.emplace(hash, mesh);
		return mesh;
	}

	void prepareScene(const std::shared_ptr<const IndoorRenderScene> &scene) {
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
			std::shared_ptr<const IndoorTexture> texture;
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

	void updateFrame(const IndexedFrame &supplied, bool modern, bool allowWorld) {
		if (!supplied.isValid() || supplied.width != frameWidth || supplied.height != frameHeight)
			throw std::invalid_argument("Invalid bgfx framebuffer or changed dimensions");
		advanceCamera();
		if (modern && supplied.indoorScene) {
			const auto &scene = *supplied.indoorScene;
			camera.target(scene.camera, !modernEnabled || cameraContinuity != scene.cameraContinuity);
			cameraContinuity = scene.cameraContinuity;
		}
		modernEnabled = modern;
		enhanced = modern && allowWorld && bool(supplied.indoorScene);
		if (enhanced && (frameWidth != 320 || frameHeight != 200 ||
				supplied.indoorScene->originalPixels.size() != supplied.pixels.size() ||
				supplied.indoorScene->interfaceCoverage.size() != supplied.pixels.size()))
			throw std::invalid_argument("Invalid indoor scene overlay coverage");
		if (enhanced) {
			prepareScene(supplied.indoorScene);
			for (auto it = textures.begin(); it != textures.end();) {
				if (it->second.source.expired() || palette != supplied.palette) {
					releaseHandle(it->second.handle); it = textures.erase(it);
				} else ++it;
			}
			palette = supplied.palette;
			for (auto &batch : batches) batch.image = uploadTexture(batch.texture);
		}
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
		advanceCamera();
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
			bgfx::setViewFrameBuffer(worldView, worldTarget);
			bgfx::setViewRect(worldView, 0, 0, 216 * 3, 132 * 3);
			bgfx::setViewClear(worldView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.f, 0);
			const auto &scene = *frame.indoorScene;
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
				const std::array<float, 4> options{{distort ? (billboard ? 2.f : 1.f) : -1.f, 0, 0, 0}};
				bgfx::setTransform(identity.data());
				bgfx::setUniform(distanceWarp, warp.data());
				bgfx::setUniform(geometry, options.data());
				bgfx::setUniform(cameraPosition, eye.data());
				bgfx::setVertexBuffer(0, mesh.vertices);
				bgfx::setIndexBuffer(mesh.indices);
				bgfx::setTexture(0, sampler, batch.image, std::uint32_t(pointSampling));
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
void IndoorGpuRenderer::upload(const IndexedFrame &frame, bool modern, bool allowWorld) {
	_impl->updateFrame(frame, modern, allowWorld);
}
bool IndoorGpuRenderer::animating() const { return _impl->modernEnabled && _impl->camera.moving(); }
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
