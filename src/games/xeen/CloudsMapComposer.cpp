#include "games/xeen/CloudsMapComposer.h"

#include "formats/xeen/XeenAssetSource.h"
#include "games/xeen/CloudsUiComposer.h"
#include "games/xeen/XeenIndoorScene.h"
#include "games/xeen/XeenIndoorRenderScene.h"
#include "games/xeen/XeenWorld.h"
#include "games/xeen/XeenMapView.h"
#include "games/xeen/XeenSkills.h"
#include "games/xeen/XeenTextRenderer.h"
#include <algorithm>

namespace mmodern {

void CloudsMapComposer::drawOutdoorCommands(XeenAssetSource &assets,
		const std::vector<XeenOutdoorDrawCommand> &commands,XeenSceneArchive archive) const {
	for (const auto &command : commands) {
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
		const XeenCharacterRulesContext &context,XeenScenePresentation *presentation,XeenSceneArchive archive) const {
    CloudsUiComposer::drawBorder(partyState,[&](const char *name,unsigned frame,int x,int y){XeenSpriteDrawOptions options;options.archive=archive;assets.drawSprite(name,frame,x,y,options);},presentation);
	// The compass/main button at y=137 overlaps the scene border in the original UI.
	CloudsUiComposer().drawInterface(assets,partyState,context,archive);
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
	std::shared_ptr<IndoorRenderScene> indoorScene;
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
		drawOutdoorCommands(assets,commands,archive);
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
		indoorScene = buildIndoorRenderScene(assets, world, camera, commands, ordinaryPhase);
		indoorScene->interfaceCoverage.assign(320 * 200, 0);
		drawIndoorCommands(assets,commands,archive,&indoorScene->interfaceCoverage);
	}

 // Obscure after scene/monsters, before overlapping border indicators.
 const bool black = world.sceneIsBlack(partyState,camera);
 if(black)assets.blackenScene();
	if (indoorScene) assets.beginInterfaceCoverage();
	drawInterfaceLayers(assets, partyState, context,&world.scenePresentation(),archive);
	if (indoorScene) {
		const auto coverage = assets.endInterfaceCoverage();
		for (std::size_t i = 0; i < coverage.size(); ++i)
			indoorScene->interfaceCoverage[i] |= coverage[i];
		indoorScene->black = black;
	}
    if(partyState.encounterContext)for(auto i:{XeenPartyEffect::Levitate,XeenPartyEffect::Bless,XeenPartyEffect::PowerShield,XeenPartyEffect::HolyBonus,XeenPartyEffect::Heroism})
        emittedAnimation=emittedAnimation || partyState.encounterContext->effects[i]!=0;
	const auto &indicators=world.scenePresentation();
	emittedAnimation=emittedAnimation || (indicators.thinWall && xeenPartyHasSkill(partyState,16)) ||
		(indicators.dangerSense && xeenPartyHasSkill(partyState,17));
	auto result = drawXeenMinimap(world,camera,drawDirectionSense(assets,assets.snapshot(),partyState,camera,indicators),
        partyState.encounterContext && partyState.encounterContext->effects[XeenPartyEffect::AutomapOn]!=0,
        [&](IndexedFrame &frame,const char *name,unsigned index,int x,int y) {
            assets.drawDialogSprite(frame,name,index,x,y,xeenDialogSpriteArchive(camera.mapId,name));
        },ordinaryPhase.value_or(0),
        partyState.encounterContext && partyState.encounterContext->effects[XeenPartyEffect::WizardEye]!=0);
	if (containsOrdinaryAnimation) *containsOrdinaryAnimation = emittedAnimation;
	if (indoorScene) {
		indoorScene->originalPixels = result.pixels;
		// Direction Sense occupies the last row of the scene.
		std::fill_n(indoorScene->interfaceCoverage.begin() + 139 * 320, 320, 1);
		result.indoorScene = std::move(indoorScene);
	}
	return result;
}

} // namespace mmodern
