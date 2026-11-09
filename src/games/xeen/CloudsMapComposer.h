#ifndef MMODERN_GAMES_XEEN_CLOUDS_MAP_COMPOSER_H
#define MMODERN_GAMES_XEEN_CLOUDS_MAP_COMPOSER_H

#include "core/IndexedFrame.h"
#include "formats/xeen/XeenFontFormat.h"
#include "games/xeen/XeenIndoorScene.h"
#include "games/xeen/XeenOutdoorScene.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace mmodern {

class XeenAssetSource;
class XeenWorld;
struct XeenCharacterRulesContext;
struct XeenPartyState;
class XeenScenePresentation;
struct UiRenderScene;

class CloudsMapComposer {
public:
	// Optional output for skipped visuals. Present corrupt metadata/sprite data
	// still raises the 16A diagnostic exception; missing metadata permits terrain.
	IndexedFrame compose(XeenAssetSource &assets, XeenWorld &world,
		const XeenPartyState &partyState,
		const XeenCamera &camera,
		const XeenCharacterRulesContext &context,
		std::vector<XeenObjectVisual> *objectDiagnostics = nullptr,
		std::optional<std::uint64_t> ordinaryPhase = std::nullopt,
		bool *containsOrdinaryAnimation = nullptr,
		std::optional<XeenMonsterAppearance> actorFrame = std::nullopt) const;
	// Execute the single, already ordered outdoor command stream.
	void drawOutdoorCommands(XeenAssetSource &assets,
		const std::vector<XeenOutdoorDrawCommand> &commands,XeenSceneArchive archive=XeenSceneArchive::Current) const;
	// Execute the single, already ordered indoor geometry/object command stream.
	void drawIndoorCommands(XeenAssetSource &assets,
		const std::vector<XeenIndoorDrawCommand> &commands,XeenSceneArchive archive=XeenSceneArchive::Current,
		std::vector<std::uint8_t> *effectCoverage=nullptr) const;
	// Complete a scene replay through the same production border/UI layers.
	void drawInterfaceLayers(XeenAssetSource &assets,
		const XeenPartyState &partyState,
		const XeenCharacterRulesContext &context,XeenScenePresentation *presentation=nullptr,
		XeenSceneArchive archive=XeenSceneArchive::Current, UiRenderScene *ui=nullptr) const;
	// Direction Sense's facing letter, written after the border layers.
	IndexedFrame drawDirectionSense(XeenAssetSource &assets,IndexedFrame frame,
		const XeenPartyState &partyState,const XeenCamera &camera,const XeenScenePresentation &presentation) const;
private:
	mutable std::shared_ptr<const XeenFontFormat> _font; // Loaded on first use.
};

} // namespace mmodern

#endif
