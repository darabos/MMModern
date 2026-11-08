#ifndef MMODERN_PLATFORM_SDL_INDOOR_GPU_RENDERER_H
#define MMODERN_PLATFORM_SDL_INDOOR_GPU_RENDERER_H

#include "core/IndexedFrame.h"
#include <memory>

namespace mmodern {
class IndoorGpuRenderer {
public:
	explicit IndoorGpuRenderer(float displayPixelAspect = 1.2f);
	~IndoorGpuRenderer();
	IndoorGpuRenderer(const IndoorGpuRenderer &) = delete;
	IndoorGpuRenderer &operator=(const IndoorGpuRenderer &) = delete;
	// Three-times-native RGB viewport, composited with the indexed original UI.
	std::vector<std::uint32_t> render(const IndexedFrame &);
private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
	float _displayPixelAspect;
};
}
#endif
