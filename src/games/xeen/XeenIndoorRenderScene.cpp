#include "games/xeen/XeenIndoorRenderScene.h"
#include "games/xeen/XeenIndoorSceneTables.h"
#include "formats/xeen/XeenAssetSource.h"
#include "games/xeen/XeenWorld.h"

#include <algorithm>
#include <stdexcept>

namespace mmodern {

std::shared_ptr<IndoorRenderScene> buildIndoorRenderScene(XeenAssetSource &assets,
		XeenWorld &world, const XeenCamera &camera,
		const std::vector<XeenIndoorDrawCommand> &commands) {
	auto scene = std::make_shared<IndoorRenderScene>();
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
			bool allowBehindParty = false) {
		if (!image->width || !image->height) return;
		// Keep walls clipped at the party, but let horizontal surfaces and
		// grounded objects extend toward the virtual eye behind it.
		constexpr float clipZ = -0.04f;
		if (allowBehindParty || std::all_of(vertices.begin(), vertices.end(),
				[&](const auto &v) { return v.z <= clipZ; })) {
			scene->quads.push_back({std::move(image), vertices});
			return;
		}
		std::vector<IndoorVertex> clipped;
		auto previous = vertices.back();
		for (const auto &current : vertices) {
			if ((previous.z <= clipZ) != (current.z <= clipZ)) {
				const float t = (clipZ - previous.z) / (current.z - previous.z);
				const auto lerp = [&](float a, float b) { return a + t * (b - a); };
				clipped.push_back({lerp(previous.x, current.x), lerp(previous.y, current.y), clipZ,
					lerp(previous.u, current.u), lerp(previous.v, current.v)});
			}
			if (current.z <= clipZ) clipped.push_back(current);
			previous = current;
		}
		for (std::size_t i = 1; i + 1 < clipped.size(); ++i)
			scene->quads.push_back({image, {clipped[0], clipped[i], clipped[i + 1], clipped[i + 1]}});
	};
	const auto ground = texture(prefix + ".gnd", 0, resources.ground);
	const auto &drawing = world.scenePresentation().drawingState();
	for (const auto &command : commands) {
		const auto *geometry = std::get_if<XeenIndoorGeometryDraw>(&command.content);
		if (!geometry || command.originalOrder != 1) continue;
		const auto sky = texture(geometry->resourceName, geometry->frame, geometry->options.archive);
		add(sky, {{{-12, -eyeHeight, -10, 0, 1}, {12, -eyeHeight, -10, 1, 1},
			{12, 10, -10, 1, 0}, {-12, 10, -10, 0, 0}}});
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
	for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
		const int x = int(camera.x) + dx, y = int(camera.y) + dy;
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
			bool visibleSide = int(camera.y) < n;
			switch (direction) {
			case XeenDirection::North: break;
			case XeenDirection::East: ax = r; ay = n; bx = r; by = s; visibleSide = int(camera.x) < r; break;
			case XeenDirection::South: ax = r; ay = s; bx = l; by = s; visibleSide = int(camera.y) > s; break;
			case XeenDirection::West: ax = l; ay = s; bx = l; by = n; visibleSide = int(camera.x) > l; break;
			}
			if (!visibleSide) continue;
			const bool open = dx == 0 && dy == 0 && direction == camera.direction && drawing.openDoor;
			auto image = wallTexture(value, open);
			if (!image->width || !image->height) continue;
			// A unit-wide quad needs H/W world height for isotropic source texels.
			const float height = float(image->height) / image->width;
			add(image, {vertex(ax, ay, 0, 0, 1), vertex(bx, by, 0, 1, 1),
				vertex(bx, by, height, 1, 0), vertex(ax, ay, height, 0, 0)});
		}
	}
	// Use the original visible records and animation frames; GPU visibility never drives combat.
	for (const auto &command : commands) {
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
			continue;
		} else if (const auto *actor = command.actor()) {
			if (actor->effectFlags) continue; // Original effect pixels are retained as an overlay.
			const auto name = actor->kind == XeenMonsterSpriteKind::Attack ?
				XeenAssetSource::attackMonsterResource(actor->image) : XeenAssetSource::normalMonsterResource(actor->image);
			image = texture(name, actor->frame, actor->archive);
			lane = float(actor->selectedSlot % 3 - 1) * 0.23f;
		} else if (const auto *item = command.wallItem()) {
			image = texture(item->resourceName, item->frame, item->archive);
			height = wallHeight * 0.35f;
		} else continue;
		if (!image->width || !image->height) continue;
		auto center = vertex(float(command.sourceX), float(command.sourceY), 0, 0, 0);
		center.x += lane;
		if (center.z > -0.3f) center.z = -0.4f;
		if (command.wallItem()) center.z -= 0.46f;
		float width = height * image->width / image->height;
		if (width > 0.95f) { height *= 0.95f / width; width = 0.95f; }
		const float bottom = command.wallItem() ? -wallHeight * 0.05f : -eyeHeight;
		const float u0 = flip ? 1.f : 0.f, u1 = 1.f - u0;
		add(image, {{{center.x - width / 2, bottom, center.z, u0, 1},
			{center.x + width / 2, bottom, center.z, u1, 1},
			{center.x + width / 2, bottom + height, center.z, u1, 0},
			{center.x - width / 2, bottom + height, center.z, u0, 0}}});
	}
	return scene;
}
} // namespace mmodern
