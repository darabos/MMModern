#include "games/xeen/XeenIndoorRenderScene.h"
#include "games/xeen/XeenIndoorSceneTables.h"
#include "formats/xeen/XeenAssetSource.h"
#include "games/xeen/XeenWorld.h"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace mmodern {

std::shared_ptr<IndoorRenderScene> buildIndoorRenderScene(XeenAssetSource &assets,
		XeenWorld &world, const XeenCamera &camera,
		const std::vector<XeenIndoorDrawCommand> &commands,
		std::optional<std::uint64_t> ordinaryPhase) {
	auto scene = std::make_shared<IndoorRenderScene>();
	world.scenePresentation().publishGpuCamera(camera);
	scene->camera = world.scenePresentation().gpuCameraState.pose;
	scene->cameraContinuity = world.scenePresentation().gpuCameraState.continuity;
	// IndoorDrawList: nearest front wall (32,24)..(200,109), next
	// (64,40)..(168,93). F/(0.5+B)=168, F/(1.5+B)=104.
	scene->focalLength = 273.f;
	scene->cameraSetback = 1.125f;
	scene->horizon = 66.5f - 8.f;
	const auto &map = world.map(camera.mapId);
	const auto resources = xeenMapResources(camera.mapId);
	static constexpr const char *prefixes[]{"town", "cave", "towr", "cstl", "dung", "scfi"};
	const unsigned kind = map.geometry.wallKind;
	if (kind >= std::size(prefixes)) throw std::runtime_error("Invalid indoor GPU wall kind");
	const std::string prefix = prefixes[kind];
	const auto ambient = resources.ambient;
	const auto texture = [&](const std::string &name, std::size_t frame, XeenSceneArchive archive) {
		return assets.spriteTexture(name, frame, archive == XeenSceneArchive::Current ? ambient : archive);
	};
	const auto referenceWall = texture("f" + prefix + "2.fwl", 1, resources.walls);
	if (!referenceWall->width || !referenceWall->height)
		throw std::runtime_error("Indoor wall material has no dimensions");
	const float wallHeight = float(referenceWall->height) / referenceWall->width;
	const float eyeHeight = wallHeight / 2;
	const auto vertex = [&](float x, float y, float height, float u, float v) {
		x -= int(camera.x); y -= int(camera.y);
		float right = 0, forward = 0;
		switch (static_cast<XeenDirection>(camera.direction)) {
		case XeenDirection::North: right = x; forward = y; break;
		case XeenDirection::East: right = -y; forward = x; break;
		case XeenDirection::South: right = -x; forward = -y; break;
		case XeenDirection::West: right = y; forward = -x; break;
		default: throw std::runtime_error("Invalid indoor GPU camera direction");
		}
		return IndoorVertex{right, height - eyeHeight, -forward, u, v};
	};
	const auto add = [&](std::shared_ptr<const IndoorTexture> image, std::array<IndoorVertex, 4> vertices,
			bool allowBehindParty = false, bool distort = true) {
		if (!image->width || !image->height) return;
		scene->quads.push_back({std::move(image), vertices, distort, !allowBehindParty && distort});
	};
	const auto ground = texture(prefix + ".gnd", 0, resources.ground);
	const auto &drawing = world.scenePresentation().drawingState();
	for (const auto &command : commands) {
		const auto *geometry = std::get_if<XeenIndoorGeometryDraw>(&command.content);
		if (!geometry || command.originalOrder != 1) continue;
		const auto sky = texture(geometry->resourceName, geometry->frame, geometry->options.archive);
		add(sky, {{{-12, -eyeHeight, -10, 0, 1}, {12, -eyeHeight, -10, 1, 1},
			{12, 10, -10, 1, 0}, {-12, 10, -10, 0, 0}}}, false, false);
		break;
	}
	const auto wallTexture = [&](unsigned value, bool open) {
		// Temporary material mapping: the closest head-on art, never side-perspective frames.
		static constexpr unsigned frames[]{0, 1, 9, 3, 0, 0, 7, 8, 0, 2, 6, 6, 1, 0, 4, 5};
		if (value >= std::size(frames)) throw std::runtime_error("Invalid indoor GPU wall type");
		unsigned frame = frames[value];
		const bool first = value == 5 || value == 8 || value == 11 || value == 12;
		if (value == 12) frame += drawing.overallFrame;
		if (open && (value == 4 || value == 13)) frame = kind ? 1 : 10;
		return texture("f" + prefix + (first ? "1.fwl" : "2.fwl"), frame, resources.walls);
	};
	for (int y = -16; y < 32; ++y) for (int x = -16; x < 32; ++x) {
		const auto cell = world.sceneCell(camera.mapId, x, y);
		if (!cell) continue;
		const float l = x - 0.5f, r = x + 0.5f, s = y - 0.5f, n = y + 0.5f;
		add(ground, {vertex(l, s, 0, 0, 1), vertex(r, s, 0, 1, 1),
			vertex(r, n, 0, 1, 0), vertex(l, n, 0, 0, 0)}, true);
		if (cell->cell->flags & 8) {
			add(texture(prefix + ".sky", 0, resources.sky),
				{vertex(l, n, wallHeight, 0, 0), vertex(r, n, wallHeight, 1, 0),
				 vertex(r, s, wallHeight, 1, 1), vertex(l, s, wallHeight, 0, 1)}, true);
		}
		for (unsigned face = 0; face < 4; ++face) {
			const auto direction = static_cast<XeenDirection>(face);
			const unsigned value = wallAt(*cell->cell, direction);
			if (!value) continue;
			float ax = l, ay = n, bx = r, by = n;
			switch (direction) {
			case XeenDirection::North: break;
			case XeenDirection::East: ax = r; ay = n; bx = r; by = s; break;
			case XeenDirection::South: ax = r; ay = s; bx = l; by = s; break;
			case XeenDirection::West: ax = l; ay = s; bx = l; by = n; break;
			}
			const bool open = x == camera.x && y == camera.y && direction == camera.direction && drawing.openDoor;
			auto image = wallTexture(value, open);
			if (!image->width || !image->height) continue;
			// A unit-wide quad needs H/W world height for isotropic source texels.
			const float height = float(image->height) / image->width;
			add(image, {vertex(ax, ay, 0, 0, 1), vertex(bx, by, 0, 1, 1),
				vertex(bx, by, height, 1, 0), vertex(ax, ay, height, 0, 0)});
		}
	}
	// Broader GPU visibility does not activate actors or advance original animation.
	std::vector<XeenIndoorDrawCommand> renderCommands;
	const auto resolver = XeenObjectVisualResolver::load(assets);
	const auto &file = world.objectFile(camera.mapId);
	for (std::size_t i = 0; i < file.entities.objects.size(); ++i) {
		const XeenObjectIdentity id{camera.mapId, i};
		const auto object = world.effectiveObject(id);
		if (!object.isActive() || object.resourceId >= 255 || world.isObjectDisabled(id)) continue;
		auto found = std::find_if(commands.begin(), commands.end(), [&](const auto &c) {
			return c.object() && c.object()->visual.identity == id;
		});
		if (found != commands.end()) { renderCommands.push_back(*found); continue; }
		auto visual = resolver.resolve(file, i, camera.direction, ordinaryPhase, object.tableIndex);
		if (visual.status != XeenObjectVisualStatus::SupportedStatic &&
				visual.status != XeenObjectVisualStatus::SupportedAnimated &&
				visual.status != XeenObjectVisualStatus::UnsupportedAnimation)
			throw std::runtime_error("Cannot build indoor GPU object: " + visual.diagnostic);
		if (const auto frame = world.scenePresentation().drawnObjectFrame(id)) visual.frame = *frame;
		XeenIndoorDrawCommand command;
		command.sourceX = object.x; command.sourceY = object.y;
		command.content = XeenIndoorObjectDraw{std::move(visual)};
		renderCommands.push_back(std::move(command));
	}
	std::map<std::pair<int, int>, int> actorLanes;
	for (const auto &actor : world.sceneActors(camera.mapId)) {
		if (actor.lifecycle != XeenActorLifecycle::Present || actor.hp <= 0 ||
				!actor.statistics || !actor.statistics->supportsRendering()) continue;
		const int lane = actorLanes[{actor.x, actor.y}]++ % 3;
		const auto found = std::find_if(commands.begin(), commands.end(), [&](const auto &c) {
			return c.actor() && c.actor()->identity == actor.id;
		});
		if (found != commands.end()) {
			renderCommands.push_back(*found);
			std::get<XeenIndoorActorDraw>(renderCommands.back().content).selectedSlot = lane;
			continue;
		}
		XeenIndoorDrawCommand command;
		command.sourceX = actor.x; command.sourceY = actor.y;
		XeenIndoorActorDraw draw{actor.id, actor.statistics->image()};
		draw.archive = xeenMonsterArt(camera.mapId, actor.original.resourceId);
		draw.selectedSlot = lane;
		if (const auto *animation = drawing.animation(actor.id)) {
			draw.kind = animation->frame >= 8 ? XeenMonsterSpriteKind::Attack : XeenMonsterSpriteKind::Normal;
			draw.frame = animation->frame % 8; draw.effectFlags = animation->flags();
		}
		command.content = draw;
		renderCommands.push_back(std::move(command));
	}
	for (const auto &command : renderCommands) {
		std::shared_ptr<const IndoorTexture> image;
		bool flip = false;
		float lane = 0, height = wallHeight * 0.8f;
		if (const auto *object = command.object()) {
			image = texture(object->visual.spriteName, object->visual.frame,
				xeenMapResources(object->visual.identity.mapId).objectArt);
			flip = object->visual.horizontalFlip;
			if (!image->width || !image->height) continue;
			// setIndoorsObjects draws the interaction position unscaled at
			// (-5,2), or (-35,-65) for the exceptional resource. Preserve
			// sprite header offsets rather than centering the cropped image.
			const auto &record = world.effectiveObject(object->visual.identity);
			const bool special = record.resourceId ==
				(camera.mapId.side == XeenSide::Clouds ? 113 : 47);
			const auto &placement = xeen_indoor_scene_tables::kObjectPlacements.front();
			const auto anchor = special ? 1u : 0u;
			const float screenBottom = placement.y[anchor] + image->originY + image->height;
			const float belowHorizon = screenBottom - (scene->horizon + 8.f);
			// Solve floor contact along the original bottom-corner rays.
			// Floating artwork stays floating; only submerged bases move.
			const float distance = belowHorizon > 0 ?
				std::min(scene->cameraSetback + 0.4f, scene->focalLength * eyeHeight / belowHorizon) :
				scene->cameraSetback + 0.4f;
			const float unitsPerPixel = distance / scene->focalLength;
			auto center = vertex(float(command.sourceX), float(command.sourceY), 0, 0, 0);
			center.z += scene->cameraSetback - distance;
			const float left = center.x +
				(placement.x[anchor] + image->originX - 116.f) * unitsPerPixel;
			const float top = (scene->horizon + 8.f -
				(placement.y[anchor] + image->originY)) * unitsPerPixel;
			const float right = left + image->width * unitsPerPixel;
			const float bottom = top - image->height * unitsPerPixel;
			const float u0 = flip ? 1.f : 0.f, u1 = 1.f - u0;
			add(image, {{{left, bottom, center.z, u0, 1}, {right, bottom, center.z, u1, 1},
				{right, top, center.z, u1, 0}, {left, top, center.z, u0, 0}}}, true);
			auto &quad = scene->quads.back();
			quad.billboard = true;
			quad.anchorX = float(command.sourceX); quad.anchorZ = -float(command.sourceY);
			const auto anchorVertex = vertex(float(command.sourceX), float(command.sourceY), 0, 0, 0);
			for (auto &v : quad.vertices) { v.x -= anchorVertex.x; v.z -= anchorVertex.z; }
			continue;
		} else if (const auto *actor = command.actor()) {
			if (actor->effectFlags) continue; // Original effect pixels are retained as an overlay.
			const auto name = actor->kind == XeenMonsterSpriteKind::Attack ?
				XeenAssetSource::attackMonsterResource(actor->image) : XeenAssetSource::normalMonsterResource(actor->image);
			image = texture(name, actor->frame, actor->archive);
			lane = float(actor->selectedSlot % 3 - 1) * 0.23f;
		} else continue;
		if (!image->width || !image->height) continue;
		auto center = vertex(float(command.sourceX), float(command.sourceY), 0, 0, 0);
		center.x += lane;
		center.z -= 0.4f;
		float width = height * image->width / image->height;
		if (width > 0.95f) { height *= 0.95f / width; width = 0.95f; }
		const float bottom = -eyeHeight;
		const float u0 = flip ? 1.f : 0.f, u1 = 1.f - u0;
		add(image, {{{center.x - width / 2, bottom, center.z, u0, 1},
			{center.x + width / 2, bottom, center.z, u1, 1},
			{center.x + width / 2, bottom + height, center.z, u1, 0},
			{center.x - width / 2, bottom + height, center.z, u0, 0}}}, true);
		auto &quad = scene->quads.back();
		quad.billboard = true;
		quad.anchorX = float(command.sourceX); quad.anchorZ = -float(command.sourceY);
		const auto anchorVertex = vertex(float(command.sourceX), float(command.sourceY), 0, 0, 0);
		for (auto &v : quad.vertices) { v.x -= anchorVertex.x; v.z -= anchorVertex.z; }
	}
	for (std::size_t i = 0; i < file.entities.wallItems.size(); ++i) {
		const auto item = world.effectiveWallItem({camera.mapId, i});
		if (!item.isActive() || item.resourceId >= 255) continue;
		if (item.direction >= 4) throw std::runtime_error("Invalid indoor GPU wall-item direction");
		const auto digits = std::to_string(int(item.resourceId));
		const auto name = std::string(3 - digits.size(), '0') + digits + ".pic";
		const auto count = assets.spriteFrameCount(name, resources.pictures);
		auto image = texture(name, drawing.wallFrame(count), resources.pictures);
		if (!image->width || !image->height) continue;
		float height = wallHeight * 0.35f;
		float width = height * image->width / image->height;
		if (width > 0.95f) { height *= 0.95f / width; width = 0.95f; }
		constexpr float rx[]{1, 0, -1, 0}, ry[]{0, -1, 0, 1};
		constexpr float fx[]{0, 1, 0, -1}, fy[]{1, 0, -1, 0};
		const unsigned d = item.direction;
		const float x = float(int(item.x)) + fx[d] * 0.46f, y = float(int(item.y)) + fy[d] * 0.46f;
		const float bottom = eyeHeight - wallHeight * 0.05f;
		add(image, {vertex(x - rx[d] * width / 2, y - ry[d] * width / 2, bottom, 0, 1),
			vertex(x + rx[d] * width / 2, y + ry[d] * width / 2, bottom, 1, 1),
			vertex(x + rx[d] * width / 2, y + ry[d] * width / 2, bottom + height, 1, 0),
			vertex(x - rx[d] * width / 2, y - ry[d] * width / 2, bottom + height, 0, 0)}, true);
	}
	// Return fixed world coordinates. Billboard vertices instead remain offsets
	// in the render camera's basis, around an immutable world anchor.
	for (auto &quad : scene->quads) {
		if (!quad.distort || quad.billboard) continue;
		for (auto &v : quad.vertices) {
			const float right = v.x, forward = -v.z;
			switch (XeenDirection(camera.direction)) {
			case XeenDirection::North: v.x = right; v.z = -forward; break;
			case XeenDirection::East: v.x = forward; v.z = right; break;
			case XeenDirection::South: v.x = -right; v.z = forward; break;
			case XeenDirection::West: v.x = -forward; v.z = -right; break;
			}
			v.x += int(camera.x); v.z -= int(camera.y);
		}
	}
	return scene;
}
} // namespace mmodern
