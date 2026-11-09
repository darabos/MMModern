#include "app/XeenNavigationFlow.h"

#include "games/xeen/XeenWorld.h"

namespace mmodern {

XeenNavigationFlow::XeenNavigationFlow(XeenEventSystem &eventSystem) :
	_eventSystem(eventSystem) {
}

XeenAutomaticEventResult XeenNavigationFlow::processInitialEvent(
		XeenWorld &world, XeenPartyState &partyState, XeenCamera &camera,
		XeenGameFlags &gameFlags) {
	return _eventSystem.runAutomaticEvent(world, partyState, camera, gameFlags);
}

XeenNavigationFlowResult XeenNavigationFlow::processNavigationAction(
		XeenWorld &world, XeenPartyState &partyState, XeenCamera &camera,
		XeenGameFlags &gameFlags, NavigationAction action) {
	const auto before = camera;
	const XeenMovementResult movementResult = _movement.apply(world, camera, action,XeenMovement::capabilities(partyState));
	const auto cameraAfterMovement = camera;
 if(movementResult!=XeenMovementResult::Moved && movementResult!=XeenMovementResult::Turned)
     return {movementResult,{},cameraAfterMovement};
 world.scenePresentation().navigateGpuCamera(before, camera, action);
 world.scenePresentation().markLightingStep();
	return {movementResult,
		XeenAutomaticEventNoTrigger{}, cameraAfterMovement};
}

XeenManualEventResult XeenNavigationFlow::processInteraction(
		XeenWorld &world, XeenPartyState &partyState, XeenCamera &camera,
		XeenGameFlags &gameFlags) {
	return _eventSystem.runManualEvent(world, partyState, camera, gameFlags);
}

} // namespace mmodern
