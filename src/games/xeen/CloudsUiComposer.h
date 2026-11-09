#ifndef MMODERN_GAMES_XEEN_CLOUDS_UI_COMPOSER_H
#define MMODERN_GAMES_XEEN_CLOUDS_UI_COMPOSER_H

#include "core/IndexedFrame.h"
#include "formats/xeen/XeenSpriteDrawOptions.h"
#include "games/xeen/XeenParty.h"

#include <cstddef>
#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mmodern {
struct UiRenderScene;

class XeenAssetSource;
struct XeenCharacterRulesContext;
class XeenScenePresentation;

class CloudsUiComposer {
public:
	struct PortraitPlacement {
		std::string resourceName;
		std::size_t frame = 0;
		int x = 0;
		int y = 0;
	};

	struct HpPlacement {
		std::size_t partySlot = 0;
		std::uint8_t rosterId = 0;
		std::size_t frame = 0;
		int x = 0;
		int y = 0;
	};

	static constexpr int kWidth = 320;
	static constexpr int kHeight = 200;

	static std::vector<PortraitPlacement> buildPortraitPlacements(
		const XeenPartyState &partyState, unsigned memberMask);
    static std::vector<PortraitPlacement> buildPortraitPlacements(const XeenPartyState &p) {return buildPortraitPlacements(p,p.party.activeOrder().mask());}
	// Every active member's own face, frame 4, whatever its condition: the fall's scared faces.
	static std::vector<PortraitPlacement> buildFallPortraitPlacements(const XeenPartyState &partyState);
	static std::vector<HpPlacement> buildHpPlacements(
		const XeenPartyState &partyState,
		const XeenCharacterRulesContext &context, unsigned memberMask);
    static std::vector<HpPlacement> buildHpPlacements(const XeenPartyState &p,const XeenCharacterRulesContext &c) {return buildHpPlacements(p,c,p.party.activeOrder().mask());}
	static std::optional<std::size_t> partyMemberAtSlot(const XeenPartyState &, unsigned memberMask, std::size_t slot);
	static std::vector<PortraitPlacement> buildPartyFeedbackPlacements(const XeenPartyState &,
		const XeenScenePresentation &, unsigned memberMask, int actingMember);
    static void drawBorder(const XeenPartyState &,
        const std::function<void(const char *,unsigned,int,int)> &,XeenScenePresentation * = nullptr);
	void loadBackground(XeenAssetSource &assets,XeenSceneArchive archive=XeenSceneArchive::Current) const;
	// Replace the already composed main-screen controls with ICONS_COMBAT.
	void drawCombatButtons(XeenAssetSource &assets,IndexedFrame &frame,XeenSceneArchive archive=XeenSceneArchive::Current) const;
	void drawInterface(XeenAssetSource &assets,const XeenPartyState &partyState,
		const XeenCharacterRulesContext &context,XeenSceneArchive archive=XeenSceneArchive::Current,
		UiRenderScene *ui = nullptr) const;
	IndexedFrame compose(XeenAssetSource &assets, const XeenPartyState &partyState,
		const XeenCharacterRulesContext &context) const;
};

} // namespace mmodern

#endif
