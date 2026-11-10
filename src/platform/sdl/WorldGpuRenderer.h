#ifndef MMODERN_PLATFORM_SDL_WORLD_GPU_RENDERER_H
#define MMODERN_PLATFORM_SDL_WORLD_GPU_RENDERER_H

#include "core/IndexedFrame.h"
#include <memory>
#include <utility>

struct SDL_Window;

namespace mmodern {
class WorldGpuRenderer {
public:
	// Window-facing methods are main-thread only; bgfx is owned by a private worker.
	WorldGpuRenderer(SDL_Window *, int frameWidth, int frameHeight, bool correctedAspect);
	~WorldGpuRenderer();
	WorldGpuRenderer(const WorldGpuRenderer &) = delete;
	WorldGpuRenderer &operator=(const WorldGpuRenderer &) = delete;
	void upload(const IndexedFrame &, bool modern, bool allowGameplayLayers = true);
	void cursor(const IndexedFrame &);
	// Waits for this frame's actual submission; autonomous redraws never acquire input.
	void present(bool cursorEnabled = false);
	void refreshWindow();
	void mouseMoved();
	std::uint64_t presentedFrames() const;
	std::pair<int, int> outputSize() const;
private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
}
#endif
