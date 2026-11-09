#ifndef MMODERN_CORE_UI_RENDER_SCENE_H
#define MMODERN_CORE_UI_RENDER_SCENE_H

#include "core/IndoorRenderScene.h"
#include <string>

namespace mmodern {

struct UiQuad {
	std::shared_ptr<const IndoorTexture> texture;
	int x = 0, y = 0;
	int left = 0, top = 0, right = 320, bottom = 200;
	std::string resource;
	int anchorX = 0, anchorY = 0;
};

// Original-art HUD commands, in painter order. Text/dialogs can still modify
// the reference framebuffer; the presenter composites those changes last.
// referencePixels is exactly basePixels with the current quads painted over it.
struct UiRenderScene {
	std::vector<UiQuad> quads;
	std::vector<std::uint8_t> basePixels, referencePixels, effectCoverage;
};

} // namespace mmodern
#endif
