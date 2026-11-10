#ifndef MMODERN_GAMES_XEEN_OUTDOOR_RENDER_SCENE_H
#define MMODERN_GAMES_XEEN_OUTDOOR_RENDER_SCENE_H
#include "core/WorldRenderScene.h"
#include "games/xeen/XeenOutdoorScene.h"

namespace mmodern {
class XeenAssetSource;
std::shared_ptr<WorldRenderScene> buildOutdoorRenderScene(XeenAssetSource &, XeenWorld &,
	const XeenCamera &, const std::vector<XeenOutdoorDrawCommand> &,
	std::optional<std::uint64_t> ordinaryPhase = std::nullopt);
}
#endif
