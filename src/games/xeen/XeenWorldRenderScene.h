#ifndef MMODERN_GAMES_XEEN_WORLD_RENDER_SCENE_H
#define MMODERN_GAMES_XEEN_WORLD_RENDER_SCENE_H

#include "core/WorldRenderScene.h"
#include "games/xeen/XeenNavigation.h"

namespace mmodern {
class XeenWorld;
std::shared_ptr<WorldRenderScene> makeWorldRenderScene(XeenWorld &, const XeenCamera &);
// Preserve an original near sprite rectangle while keeping its base above ground.
void appendGroundedSprite(WorldRenderScene &, std::shared_ptr<const WorldTexture>,
	float cellX, float cellY, float eyeHeight, float screenX, float screenY,
	bool flip, float referenceForward = 0, float referenceEyeDepth = 1.525f);
}
#endif
