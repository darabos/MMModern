#include "games/xeen/XeenOutdoorRenderScene.h"
#include "games/xeen/XeenOutdoorSceneTables.h"
#include "games/xeen/XeenWorldRenderScene.h"
#include "games/xeen/XeenWorld.h"
#include "formats/xeen/XeenAssetSource.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace mmodern {
std::shared_ptr<WorldRenderScene> buildOutdoorRenderScene(XeenAssetSource &assets,
		XeenWorld &world, const XeenCamera &camera,
		const std::vector<XeenOutdoorDrawCommand> &commands, std::optional<std::uint64_t> ordinaryPhase) {
	using namespace xeen_scene_tables;
	auto scene = makeWorldRenderScene(world,camera);
	constexpr float eyeHeight = 85.f/168.f/2;
	const auto resources = xeenMapResources(camera.mapId);
	const auto &map = world.map(camera.mapId);
	const auto &drawing = world.scenePresentation().drawingState();
	const auto texture = [&](const std::string &name, std::size_t frame) {
		return assets.spriteTexture(name,frame,resources.ambient);
	};
	// Sky bands keep their original native placement. They share the undistorted
	// backdrop mode used indoors and never take part in depth fog.
	for (const auto &command : commands) {
		if (!std::holds_alternative<XeenOutdoorTerrainDraw>(command.content) || command.originalOrder > 1) continue;
		const auto &terrain = command.terrain();
		auto image = texture(terrain.resourceName,terrain.frame);
		const float units = 10.f/scene->focalLength;
		const float left = (command.x+image->originX-116.f)*units;
		const float top = (scene->horizon+8-command.y-image->originY)*units;
		const float right = left+image->width*units, bottom = top-image->height*units;
		const float u0 = terrain.options.horizontalFlip ? 1.f : 0.f, u1 = 1-u0;
		scene->quads.push_back({image,{{{left,bottom,-10+scene->cameraSetback,u0,1},
			{right,bottom,-10+scene->cameraSetback,u1,1},{right,top,-10+scene->cameraSetback,u1,0},
			{left,top,-10+scene->cameraSetback,u0,0}}},false});
	}
	const auto water = texture("water.out",0);
	for (int y = -16; y < 32; ++y) for (int x = -16; x < 32; ++x) {
		const auto cell = world.sceneCell(camera.mapId,x,y);
		if (cell && !cell->geometry->isOutdoors()) continue;
		// Original getCell exposes missing neighbors as slot 15 (SPACE), not
		// absent ground. Its palette/material still belongs to the logical map.
		const auto *layers = cell ? &xeenGet<XeenOutdoorLayers>(cell->cell->geometry) : nullptr;
		const unsigned slot = layers ? unsigned(layers->surface) : 15;
		const unsigned surface = map.geometry.surfaceTypes[slot];
		if (surface >= kSurfaceNames.size()) throw std::runtime_error("Invalid outdoor GPU surface");
		auto ground = slot && surface ? texture(kSurfaceNames[surface],1) : water;
		const bool flip = (surface==0 || surface==5 || surface==8) ? drawing.water : drawing.ground;
		const float u0 = flip ? 1.f : 0.f, u1 = 1-u0;
		const float l = x-0.5f, r = x+0.5f, s = y-0.5f, n = y+0.5f;
		const std::array<WorldVertex,4> floor{{{l,-eyeHeight,-s,u0,1},{r,-eyeHeight,-s,u1,1},
			{r,-eyeHeight,-n,u1,0},{l,-eyeHeight,-n,u0,0}}};
		// The original water backdrop fills transparent holes in surface art.
		scene->quads.push_back({water,floor});
		if (ground != water) {
			auto surfaceFloor = floor;
			for (auto &v : surfaceFloor) v.y += 0.0001f;
			scene->quads.push_back({ground,surfaceFloor});
		}
		const unsigned terrain = layers ? unsigned(map.geometry.wallTypes[layers->middle]) : 0;
		if (terrain >= kOutdoorWallNames.size()) throw std::runtime_error("Invalid outdoor GPU terrain");
		const auto *name = kOutdoorWallNames[terrain];
		if (*name) appendGroundedSprite(*scene,texture(std::string(name)+".wal",1),
			float(x),float(y),eyeHeight,32,24,false,1,1.625f);
	}
	const auto resolver = XeenObjectVisualResolver::load(assets);
	const auto &file = world.objectFile(camera.mapId);
	std::set<std::pair<int,int>> objectCells;
	for (std::size_t i = 0; i < file.entities.objects.size(); ++i) {
		const XeenObjectIdentity id{camera.mapId,i};
		const auto object = world.effectiveObject(id);
		if (!object.isActive() || object.resourceId >= 255 || world.isObjectDisabled(id)) continue;
		if (!objectCells.emplace(int(object.x),int(object.y)).second) continue;
		auto found = std::find_if(commands.begin(),commands.end(),[&](const auto &command) {
			return command.object() && command.object()->visual.identity==id;
		});
		auto visual = found!=commands.end() ? found->object()->visual :
			resolver.resolve(file,i,camera.direction,ordinaryPhase,object.tableIndex);
		if (visual.status != XeenObjectVisualStatus::SupportedStatic &&
				visual.status != XeenObjectVisualStatus::SupportedAnimated &&
				visual.status != XeenObjectVisualStatus::UnsupportedAnimation)
			throw std::runtime_error("Cannot build outdoor GPU object: "+visual.diagnostic);
		if (const auto frame = world.scenePresentation().drawnObjectFrame(id)) visual.frame = *frame;
		const bool special = object.resourceId==(camera.mapId.side==XeenSide::Clouds ? 113 : 47);
		const auto &placement = kObjectPlacements.front();
		const auto row = special ? 1u : 0u;
		appendGroundedSprite(*scene,assets.spriteTexture(visual.spriteName,visual.frame,resources.objectArt),
			float(object.x),float(object.y),eyeHeight,float(placement.x[row]),float(placement.y[row]),visual.horizontalFlip);
	}
	std::map<std::pair<int,int>,int> lanes;
	for (const auto &actor : world.sceneActors(camera.mapId)) {
		if (actor.lifecycle!=XeenActorLifecycle::Present || actor.hp<=0 ||
				!actor.statistics || !actor.statistics->supportsRendering()) continue;
		const int lane = lanes[{actor.x,actor.y}]++%3;
		const auto found = std::find_if(commands.begin(),commands.end(),[&](const auto &command) {
			return command.actor() && command.actor()->identity==actor.id;
		});
		XeenMonsterSpriteKind kind = XeenMonsterSpriteKind::Normal;
		unsigned frame = 0, effects = 0;
		if (found!=commands.end()) {
			kind = found->actor()->kind; frame = found->actor()->frame; effects = found->actor()->effectFlags;
		} else if (const auto *animation = drawing.animation(actor.id)) {
			kind = animation->frame>=8 ? XeenMonsterSpriteKind::Attack : XeenMonsterSpriteKind::Normal;
			frame = animation->frame%8; effects = animation->flags();
		}
		if (effects) continue;
		const auto name = kind==XeenMonsterSpriteKind::Attack ?
			XeenAssetSource::attackMonsterResource(actor.statistics->image()) :
			XeenAssetSource::normalMonsterResource(actor.statistics->image());
		auto image = assets.spriteTexture(name,frame,xeenMonsterArt(camera.mapId,actor.original.resourceId));
		if (!image->width || !image->height) continue;
		float height = eyeHeight*1.6f, width = height*image->width/image->height;
		if (width>0.95f) {height*=0.95f/width; width=0.95f;}
		const float center = (lane-1)*0.23f;
		WorldQuad quad;
		quad.texture = image; quad.billboard = true;
		quad.anchorX = float(actor.x); quad.anchorZ = -float(actor.y);
		quad.vertices = {{{center-width/2,-eyeHeight,-0.4f,0,1},{center+width/2,-eyeHeight,-0.4f,1,1},
			{center+width/2,-eyeHeight+height,-0.4f,1,0},{center-width/2,-eyeHeight+height,-0.4f,0,0}}};
		scene->quads.push_back(std::move(quad));
	}
	return scene;
}
}
