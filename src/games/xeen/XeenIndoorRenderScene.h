#ifndef MMODERN_GAMES_XEEN_INDOOR_RENDER_SCENE_H
#define MMODERN_GAMES_XEEN_INDOOR_RENDER_SCENE_H

#include "core/IndoorRenderScene.h"
#include "games/xeen/XeenIndoorScene.h"

namespace mmodern {
class XeenAssetSource;
std::shared_ptr<IndoorRenderScene> buildIndoorRenderScene(XeenAssetSource &, XeenWorld &,
	const XeenCamera &, const std::vector<XeenIndoorDrawCommand> &,
	std::optional<std::uint64_t> ordinaryPhase = std::nullopt);
}
#endif
