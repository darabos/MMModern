#include "games/xeen/CloudsUiComposer.h"

#include "formats/xeen/XeenAssetSource.h"
#include "games/xeen/XeenSkills.h"
#include "games/xeen/XeenScenePresentation.h"

#include <cstddef>
#include <algorithm>
#include <stdexcept>

namespace mmodern {
namespace {

struct SpritePlacement {
	const char *resourceName;
	std::size_t frame;
	int x;
	int y;
};

// Static Clouds of Xeen buttons taken from Interface::setMainButtons() and
// ButtonContainer's normal-frame rule. Party portraits are data-driven.
constexpr SpritePlacement kInterfaceButtons[] = {
	{ "main.icn",     0, 235,  75 },
	{ "main.icn",     2, 260,  75 },
	{ "main.icn",     4, 286,  75 },
	{ "main.icn",     6, 235,  96 },
	{ "main.icn",     8, 260,  96 },
	{ "main.icn",    10, 286,  96 },
	{ "main.icn",    12, 235, 117 },
	{ "main.icn",    14, 260, 117 },
	{ "main.icn",    16, 286, 117 },
	{ "main.icn",    18, 109, 137 },
	{ "main.icn",    20, 235, 148 },
	{ "main.icn",    22, 260, 148 },
	{ "main.icn",    24, 286, 148 },
	{ "main.icn",    26, 235, 169 },
	{ "main.icn",    28, 260, 169 },
	{ "main.icn",    30, 286, 169 }
};

} // namespace

void CloudsUiComposer::drawCombatButtons(XeenAssetSource &assets,IndexedFrame &frame,XeenSceneArchive archive) const {
	XeenSpriteDrawOptions options;options.archive=archive;
    // Interface::setMainButtons(ICONS_COMBAT), pinned ScummVM
    // 6814ee9ba54582f5b5adcffab49efbbd8f589edd. GPL-3.0-or-later,
    // attributed to the ScummVM developers listed in upstream COPYRIGHT.
    if (!frame.isValid() || frame.width!=320 || frame.height!=200)
        throw std::runtime_error("Invalid combat interface framebuffer");
	if (frame.uiScene) {
		auto ui = std::make_shared<UiRenderScene>(*frame.uiScene);
		auto reference = frame;
		reference.pixels = ui->basePixels;
		for (const auto &placement : kInterfaceButtons) {
			ui->quads.erase(std::remove_if(ui->quads.begin(),ui->quads.end(),[&](const auto &q) {
				return q.resource=="combat.icn" && q.anchorX==placement.x && q.anchorY==placement.y;
			}),ui->quads.end());
			const auto previous = ui->quads.size();
			assets.appendUiSprite(*ui, "combat.icn", placement.frame, placement.x, placement.y, archive);
			if (previous == ui->quads.size()) continue;
			XeenAssetSource::paintUiQuad(frame,ui->quads.back());
		}
		for (const auto &quad:ui->quads) XeenAssetSource::paintUiQuad(reference,quad);
		ui->referencePixels = std::move(reference.pixels);
		frame.uiScene = std::move(ui);
		return;
	}
    for (const auto &placement : kInterfaceButtons)
        assets.drawSprite("combat.icn",placement.frame,placement.x,placement.y,options);
    const auto buttons=assets.snapshot();
    // The asset surface retains the current composition. Copy just controls,
    // preserving Flow's text layers and the rest of the supplied framebuffer.
    for (const auto &placement : kInterfaceButtons) {
        const int width=placement.frame==18 ? 13 : 24;
        const int height=placement.frame==18 ? 10 : 20;
        for(int y=placement.y;y<placement.y+height;++y) {
            const auto offset=static_cast<std::size_t>(y)*320+placement.x;
            std::copy_n(buttons.pixels.begin()+offset,width,frame.pixels.begin()+offset);
        }
    }
}

void CloudsUiComposer::loadBackground(XeenAssetSource &assets,XeenSceneArchive archive) const {
	assets.loadPalette("mm4.pal",archive);
	assets.loadRawFramebuffer("back.raw",archive);
}

void CloudsUiComposer::drawInterface(XeenAssetSource &assets,
		const XeenPartyState &partyState,
		const XeenCharacterRulesContext &context,XeenSceneArchive archive, UiRenderScene *ui) const {
	XeenSpriteDrawOptions options;options.archive=archive;
	const auto draw = [&](const std::string &name, std::size_t frame, int x, int y) {
		if (ui) assets.appendUiSprite(*ui, name, frame, x, y, archive);
		else assets.drawSprite(name, frame, x, y, options);
	};
	// PartyDrawer::drawParty() restores all six slots before drawing active faces.
	draw("restorex.icn",0,8,149);
	for (const PortraitPlacement &placement : buildPortraitPlacements(partyState)) {
		draw(placement.resourceName, placement.frame, placement.x,placement.y);
	}
	// The original draws every face first, then the classic HP indicator pass.
	for (const HpPlacement &placement : buildHpPlacements(partyState, context)) {
		draw("hpbars.icn", placement.frame, placement.x,placement.y);
	}
	for (const SpritePlacement &placement : kInterfaceButtons) {
		draw(placement.resourceName, placement.frame, placement.x,placement.y);
	}
}

IndexedFrame CloudsUiComposer::compose(XeenAssetSource &assets,
		const XeenPartyState &partyState,
		const XeenCharacterRulesContext &context) const {
	loadBackground(assets);
	drawInterface(assets, partyState, context);
	return assets.snapshot();
}


void CloudsUiComposer::drawBorder(const XeenPartyState &partyState,
        const std::function<void(const char *,unsigned,int,int)> &draw,XeenScenePresentation *presentation) {
	// Interface::assembleBorder() redraws this frame after drawScene(). Parts of
	// the corner gems intentionally overlap the generic scene clipping rectangle.
	draw("global.icn", 0, 8, 8);
	// Resting-state border overlays from Interface::assembleBorder(). These fill
	// the intentionally transparent openings left by global.icn around the scene.
    const auto *buffs=partyState.encounterContext?&*partyState.encounterContext:nullptr;
    unsigned phase=0;
    const auto levitate=XeenScenePresentation::levitateBorderFrame(buffs && buffs->effects[XeenPartyEffect::Levitate],presentation?presentation->levitateFrame:phase);
    draw("border.icn",levitate,0,82);
	// Spot Secret Doors (skill 16) and Danger Sense (skill 17), each from its
	// own counter (0x1ba1e..0x1baab); not from one shared counter as in the pin.
	unsigned spotPhase=0,dangerPhase=0;
	const auto spot=XeenScenePresentation::animatedBorderFrame(presentation && presentation->thinWall &&
		xeenPartyHasSkill(partyState,16),28,presentation?presentation->spotDoorsFrame:spotPhase);
	draw("border.icn",spot,194,91);
	const auto danger=XeenScenePresentation::animatedBorderFrame(presentation && presentation->dangerSense &&
		xeenPartyHasSkill(partyState,17),40,presentation?presentation->dangerSenseFrame:dangerPhase);
	draw("border.icn",danger,107,9);
	draw("border.icn",  0,   0, 32); // Left clairvoyance statue, inactive.
	draw("border.icn",  8, 215, 32); // Right clairvoyance statue, inactive.
	// Default (no active resistance) corner indicators from assembleBorder().
    draw("fecp.brd", buffs && buffs->lightAndResistances[2]?1:0,   2,   2); // Fire.
	draw("fecp.brd", buffs && buffs->lightAndResistances[3]?3:2, 219,   2); // Electricity.
	draw("fecp.brd", buffs && buffs->lightAndResistances[4]?5:4,   2, 134); // Cold.
	draw("fecp.brd", buffs && buffs->lightAndResistances[5]?7:6, 219, 134); // Poison.
	draw("bless.icn", 16, 33, 137); // Blessed indicator, inactive.

	constexpr XeenPartyEffect indices[]{XeenPartyEffect::Bless,XeenPartyEffect::PowerShield,XeenPartyEffect::HolyBonus,XeenPartyEffect::Heroism};constexpr int x[]{33,55,160,182};
    for(unsigned i=0;i<4;++i)if(buffs && buffs->effects[indices[i]]) {
        const auto phase=presentation?(presentation->buffFrames[i]=(presentation->buffFrames[i]+1)%4):1;
        draw("bless.icn",4*i+phase,x[i],137);
    }
}

} // namespace mmodern
