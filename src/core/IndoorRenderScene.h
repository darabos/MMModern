#ifndef MMODERN_CORE_INDOOR_RENDER_SCENE_H
#define MMODERN_CORE_INDOOR_RENDER_SCENE_H

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mmodern {

struct IndoorTexture {
	int width = 0, height = 0;
	std::vector<std::uint8_t> indices, alpha;
};

struct IndoorVertex {
	float x, y, z, u, v;
};

struct IndoorQuad {
	std::shared_ptr<const IndoorTexture> texture;
	std::array<IndoorVertex, 4> vertices;
};

// Camera-relative geometry; immutable once acquired by the presentation loop.
struct IndoorRenderScene {
	std::vector<IndoorQuad> quads;
	std::vector<std::uint8_t> originalPixels, interfaceCoverage;
	// Projection calibration in original viewport pixels and grid-cell units.
	float focalLength = 0, horizon = 0, cameraSetback = 0;
	bool black = false;
};

} // namespace mmodern
#endif
