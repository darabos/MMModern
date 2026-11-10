#include "games/xeen/XeenWorldRenderScene.h"
#include "games/xeen/XeenWorld.h"
#include <algorithm>

namespace mmodern {
std::shared_ptr<WorldRenderScene> makeWorldRenderScene(XeenWorld &world, const XeenCamera &camera) {
	auto scene = std::make_shared<WorldRenderScene>();
	world.scenePresentation().publishGpuCamera(camera);
	scene->camera = world.scenePresentation().gpuCameraState.pose;
	scene->cameraContinuity = world.scenePresentation().gpuCameraState.continuity;
	scene->focalLength = 273.f;
	scene->cameraSetback = 1.125f;
	scene->horizon = 58.5f;
	return scene;
}

void appendGroundedSprite(WorldRenderScene &scene, std::shared_ptr<const WorldTexture> image,
		float cellX, float cellY, float eyeHeight, float screenX, float screenY,
		bool flip, float referenceForward, float referenceEyeDepth) {
	if (!image->width || !image->height) return;
	const float horizon = scene.horizon + 8.f;
	const float belowHorizon = screenY + image->originY + image->height - horizon;
	const float distance = belowHorizon > 0 ?
		std::min(referenceEyeDepth, scene.focalLength * eyeHeight / belowHorizon) : referenceEyeDepth;
	const float units = distance / scene.focalLength;
	const float left = (screenX + image->originX - 116.f) * units;
	const float top = (horizon - screenY - image->originY) * units;
	const float right = left + image->width * units, bottom = top - image->height * units;
	const float z = scene.cameraSetback + referenceForward - distance;
	const float u0 = flip ? 1.f : 0.f, u1 = 1.f-u0;
	WorldQuad quad;
	quad.texture = std::move(image);
	quad.billboard = true; quad.anchorX = cellX; quad.anchorZ = -cellY;
	quad.vertices = {{{left,bottom,z,u0,1},{right,bottom,z,u1,1},
		{right,top,z,u1,0},{left,top,z,u0,0}}};
	scene.quads.push_back(std::move(quad));
}
}
