#include "games/xeen/CloudsMapComposer.h"

#include "formats/xeen/XeenAssetSource.h"
#include "games/xeen/CloudsUiComposer.h"
#include "games/xeen/XeenIndoorScene.h"
#include "games/xeen/XeenIndoorRenderScene.h"
#include "games/xeen/XeenOutdoorRenderScene.h"
#include "games/xeen/XeenWorld.h"
#include "games/xeen/XeenMapView.h"
#include "games/xeen/XeenSkills.h"
#include "games/xeen/XeenTextRenderer.h"
#include <algorithm>

namespace mmodern {

void CloudsMapComposer::drawOutdoorCommands(XeenAssetSource &assets,
		const std::vector<XeenOutdoorDrawCommand> &commands,XeenSceneArchive archive,
		std::vector<std::uint8_t> *effectCoverage) const {
	for (const auto &command : commands) {
		const bool track = effectCoverage && (command.projectile() || command.splat() ||
			(command.actor() && command.actor()->effectFlags));
		if (track) assets.beginInterfaceCoverage();
		auto options=command.drawOptions();
		if(options.archive==XeenSceneArchive::Current)options.archive=archive;
		if (const auto *object = command.object())
			assets.drawObjectVisual(object->visual, command.x, command.y, options);
		else if (const auto *actor = command.actor())
			assets.drawMonster(actor->image, {actor->kind, actor->frame}, command.x, command.y, options);
		else if(const auto *p=command.projectile())
			assets.drawProjectile(p->pow,p->enemy,p->row,command.x,command.y,options);
		else if(const auto *s=command.splat())assets.drawSceneSprite("charpow.icn",s->frame,command.x,command.y,options);
		else
			assets.drawSceneSprite(command.terrain().resourceName, command.terrain().frame,
				command.x, command.y, options);
		if (track) {
			const auto coverage = assets.endInterfaceCoverage();
			for (std::size_t i = 0; i < coverage.size(); ++i) (*effectCoverage)[i] |= coverage[i];
		}
	}
}

void CloudsMapComposer::drawIndoorCommands(XeenAssetSource &assets,
		const std::vector<XeenIndoorDrawCommand> &commands,XeenSceneArchive archive,
		std::vector<std::uint8_t> *effectCoverage) const {
	for (const auto &command : commands) {
		const auto *effectActor = command.actor();
		const bool track = effectCoverage && (command.projectile() || command.splat() ||
			(effectActor && effectActor->effectFlags));
		if (track) assets.beginInterfaceCoverage();
		auto options=command.drawOptions();
		if(options.archive==XeenSceneArchive::Current)options.archive=archive;
		if (const auto *object = command.object())
			assets.drawObjectVisual(object->visual, command.x, command.y,
				options);
		else if (const auto *actor=command.actor())
			assets.drawMonster(actor->image,{actor->kind,actor->frame},command.x,command.y,options);
		else if(const auto *p=command.projectile())assets.drawProjectile(p->pow,p->enemy,p->row,command.x,command.y,options);
		else if (const auto *wall=command.wallItem())
			assets.drawSceneSprite(wall->resourceName,wall->frame,command.x,command.y,options);
		else if(const auto *s=command.splat())assets.drawSceneSprite("charpow.icn",s->frame,command.x,command.y,options);
		else
			assets.drawSceneSprite(command.geometry().resourceName, command.geometry().frame,
				command.x, command.y, options);
		if (track) {
			const auto coverage = assets.endInterfaceCoverage();
			for (std::size_t i = 0; i < coverage.size(); ++i) (*effectCoverage)[i] |= coverage[i];
		}
	}
}

void CloudsMapComposer::drawInterfaceLayers(XeenAssetSource &assets,
		const XeenPartyState &partyState,
		const XeenCharacterRulesContext &context,XeenScenePresentation *presentation,XeenSceneArchive archive,
		UiRenderScene *ui) const {
    CloudsUiComposer::drawBorder(partyState,[&](const char *name,unsigned frame,int x,int y){
		if (ui) assets.appendUiSprite(*ui,name,frame,x,y,archive);
		else {XeenSpriteDrawOptions options;options.archive=archive;assets.drawSprite(name,frame,x,y,options);}
	},presentation);
	// The compass/main button at y=137 overlaps the scene border in the original UI.
	CloudsUiComposer().drawInterface(assets,partyState,context,archive,ui);
}

IndexedFrame CloudsMapComposer::drawDirectionSense(XeenAssetSource &assets,IndexedFrame frame,
		const XeenPartyState &partyState,const XeenCamera &camera,const XeenScenePresentation &presentation) const {
	// The border's last write (0x1bdb8..0x1bdfd, format 0x52a8a): skill 6 and
	// no open text or number prompt; the first letter of the facing's name.
	if(presentation.textPrompt || !xeenPartyHasSkill(partyState,6))return frame;
	const auto direction=static_cast<unsigned>(camera.direction);
	if(direction>3)throw std::invalid_argument("Invalid Direction Sense facing");
	if(!_font)_font=std::make_shared<const XeenFontFormat>(assets.readArchiveResource("fnt"),assets.language());
	XeenTextRenderOptions options;options.originalControls=true;options.bounds={0,0,320,200};
	const std::string text=std::string("\x02\f08\x03" "c\v139\t116")+"NESW"[direction]+"\fd\x01";
	auto rendered=XeenTextRenderer(*_font).render(frame,text,options);
	if(rendered.pages.size()!=1 || !rendered.diagnostics.empty())throw std::runtime_error("Direction Sense letter overflow");
	return std::move(rendered.pages.front());
}

IndexedFrame CloudsMapComposer::compose(XeenAssetSource &assets,
		XeenWorld &world, const XeenPartyState &partyState,
		const XeenCamera &camera,
		const XeenCharacterRulesContext &context,
		std::vector<XeenObjectVisual> *objectDiagnostics,
		std::optional<std::uint64_t> ordinaryPhase, bool *containsOrdinaryAnimation,
		std::optional<XeenMonsterAppearance> actorFrame) const {
	// Geometry, objects, monsters and pictures carry their own archive.
	// Projectiles and charpow remain active-side resources (pin Interface::setup / Combat::shoot).
	const auto archive=xeenMapResources(camera.mapId).ambient;
	if (containsOrdinaryAnimation) *containsOrdinaryAnimation = false;
	bool emittedAnimation = false;
	std::shared_ptr<WorldRenderScene> worldScene;
	if (objectDiagnostics) objectDiagnostics->clear();
	const auto actors=world.sceneActors(camera.mapId,[&] {
		const auto bytes=assets.readMonsterStatistics(xeenMapResources(camera.mapId).monsterTable);
		if (!bytes) throw std::runtime_error("Missing DARK.CC/xeen.mon for scene actors");
		return XeenMonsterFormat::parse(*bytes);
	});
	for (const auto &actor:actors) if (actor.statistics) {
		actor.statistics->validatePresentation();
		assets.validateNormalMonster(actor.statistics->image(),xeenMonsterArt(camera.mapId,actor.original.resourceId));
		assets.validateAttackMonster(actor.statistics->image(),xeenMonsterArt(camera.mapId,actor.original.resourceId));
	}
	CloudsUiComposer().loadBackground(assets,archive);

	const XeenMap &map = world.map(camera.mapId);
	const bool night = partyState.encounterContext &&
		xeenNight(partyState.encounterContext->minutes);
	if (map.geometry.isOutdoors()) {
		const auto resolver = XeenObjectVisualResolver::load(assets);
		const auto commands = XeenOutdoorScene().build(world, camera, &resolver, objectDiagnostics, ordinaryPhase, actorFrame,night);
		emittedAnimation = true; // The default outdoor water layer always animates.
		worldScene = buildOutdoorRenderScene(assets,world,camera,commands,ordinaryPhase);
		worldScene->interfaceCoverage.assign(320*200,0);
		drawOutdoorCommands(assets,commands,archive,&worldScene->interfaceCoverage);
	} else {
		const auto resolver = XeenObjectVisualResolver::load(assets);
		const auto commands = XeenIndoorScene().build(
			world, camera, &resolver, objectDiagnostics,ordinaryPhase,actorFrame,night,
			[&](const auto &name) {return assets.spriteFrameCount(name,xeenMapResources(camera.mapId).pictures);});
		emittedAnimation=std::any_of(commands.begin(),commands.end(),[](const auto &command) {
			if(command.actor()) return true;
			if(const auto *wall=command.wallItem()) return wall->animated;
			if(const auto *object=command.object()) return object->visual.status==XeenObjectVisualStatus::SupportedAnimated;
			return !command.projectile() && !command.splat() && command.geometry().animated;
		});
		worldScene = buildIndoorRenderScene(assets, world, camera, commands, ordinaryPhase);
		worldScene->interfaceCoverage.assign(320 * 200, 0);
		drawIndoorCommands(assets,commands,archive,&worldScene->interfaceCoverage);
	}

 // Obscure after scene/monsters, before overlapping border indicators.
 const bool black = world.sceneIsBlack(partyState,camera);
 if(black)assets.blackenScene();
	auto ui = std::make_shared<UiRenderScene>();
	auto base = assets.snapshot();
	ui->basePixels = base.pixels;
	if (worldScene) ui->effectCoverage = worldScene->interfaceCoverage;
	const auto background = assets.backgroundTexture(archive);
	ui->quads = {{background,0,0,0,0,320,8}, {background,0,0,0,140,320,200},
		{background,0,0,0,8,8,140}, {background,0,0,224,8,320,140}};
	drawInterfaceLayers(assets, partyState, context,&world.scenePresentation(),archive,ui.get());
	for (const auto &quad : ui->quads) XeenAssetSource::paintUiQuad(base, quad);
	if (worldScene) worldScene->black = black;
    if(partyState.encounterContext)for(auto i:{XeenPartyEffect::Levitate,XeenPartyEffect::Bless,XeenPartyEffect::PowerShield,XeenPartyEffect::HolyBonus,XeenPartyEffect::Heroism})
        emittedAnimation=emittedAnimation || partyState.encounterContext->effects[i]!=0;
	const auto &indicators=world.scenePresentation();
	emittedAnimation=emittedAnimation || (indicators.thinWall && xeenPartyHasSkill(partyState,16)) ||
		(indicators.dangerSense && xeenPartyHasSkill(partyState,17));
	auto result = drawDirectionSense(assets,base,partyState,camera,indicators);
	// Direction Sense remains software text, represented by a small transparent
	// texture until the text renderer itself migrates.
	int left = 320, top = 200, right = 0, bottom = 0;
	for (int y = 0; y < 200; ++y) for (int x = 0; x < 320; ++x)
		if (result.pixels[y*320+x] != base.pixels[y*320+x]) {
			left = std::min(left,x); top = std::min(top,y);
			right = std::max(right,x+1); bottom = std::max(bottom,y+1);
		}
	if (left < right && top < bottom) {
		auto letter = std::make_shared<WorldTexture>();
		letter->width = right-left; letter->height = bottom-top;
		for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
			letter->indices.push_back(result.pixels[y*320+x]);
			letter->alpha.push_back(result.pixels[y*320+x] != base.pixels[y*320+x] ? 255 : 0);
		}
		ui->quads.push_back({std::move(letter),left,top});
	}
	const auto minimap = xeenMinimapCommands(world,camera,
		partyState.encounterContext && partyState.encounterContext->effects[XeenPartyEffect::AutomapOn]!=0,
		ordinaryPhase.value_or(0),
		partyState.encounterContext && partyState.encounterContext->effects[XeenPartyEffect::WizardEye]!=0);
	for (const auto &command : minimap) {
		const auto previous = ui->quads.size();
		assets.appendUiSprite(*ui,command.resource,command.frame,command.x,command.y,
			xeenDialogSpriteArchive(camera.mapId,command.resource.c_str()),
			command.clip.left,command.clip.top,command.clip.right,command.clip.bottom);
		if (ui->quads.size() != previous) XeenAssetSource::paintUiQuad(result,ui->quads.back());
	}
	if (containsOrdinaryAnimation) *containsOrdinaryAnimation = emittedAnimation;
	if (worldScene) {
		for (const auto &quad : ui->quads) {
			const auto &t = *quad.texture;
			for (int y = std::max({0,quad.y,quad.top}); y < std::min({200,quad.bottom,quad.y+t.height}); ++y)
				for (int x = std::max({0,quad.x,quad.left}); x < std::min({320,quad.right,quad.x+t.width}); ++x)
					if (t.alpha[std::size_t(y-quad.y)*t.width+x-quad.x])
						worldScene->interfaceCoverage[y*320+x] = 1;
		}
		worldScene->originalPixels = result.pixels;
		// Direction Sense occupies the last row of the scene.
		std::fill_n(worldScene->interfaceCoverage.begin() + 139 * 320, 320, 1);
		result.worldScene = std::move(worldScene);
	}
	ui->referencePixels = result.pixels;
	result.uiScene = std::move(ui);
	return result;
}

} // namespace mmodern
