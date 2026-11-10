#ifndef MMODERN_CORE_WORLD_RENDER_SCENE_H
#define MMODERN_CORE_WORLD_RENDER_SCENE_H

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mmodern {

struct WorldTexture {
	int width = 0, height = 0;
	// Cropped bounds relative to the original sprite's draw anchor.
	int originX = 0, originY = 0;
	std::vector<std::uint8_t> indices, alpha;
};

struct WorldVertex {
	float x, y, z, u, v;
};

struct WorldQuad {
	std::shared_ptr<const WorldTexture> texture;
	std::array<WorldVertex, 4> vertices;
	bool distort = true;
	bool wall = false, billboard = false;
	float anchorX = 0, anchorZ = 0;
};

struct WorldCameraPose {
	std::uint32_t map = 0;
	double x = 0, y = 0, turns = 0;
};

// World-space geometry; immutable once acquired by the presentation loop.
struct WorldRenderScene {
	std::vector<WorldQuad> quads;
	std::vector<std::uint8_t> originalPixels, interfaceCoverage;
	// Projection calibration in original viewport pixels and grid-cell units.
	float focalLength = 0, horizon = 0, cameraSetback = 0;
	WorldCameraPose camera;
	std::shared_ptr<const std::uint8_t> cameraContinuity;
	bool black = false;
};

} // namespace mmodern
#endif
