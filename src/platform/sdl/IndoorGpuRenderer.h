#ifndef MMODERN_PLATFORM_SDL_INDOOR_GPU_RENDERER_H
#define MMODERN_PLATFORM_SDL_INDOOR_GPU_RENDERER_H

#include "core/IndexedFrame.h"
#include <memory>
#include <optional>
#include <utility>

struct SDL_Window;

namespace mmodern {
class IndoorGpuRenderer {
public:
	IndoorGpuRenderer(SDL_Window *, int frameWidth, int frameHeight, bool correctedAspect);
	~IndoorGpuRenderer();
	IndoorGpuRenderer(const IndoorGpuRenderer &) = delete;
	IndoorGpuRenderer &operator=(const IndoorGpuRenderer &) = delete;
	void upload(const IndexedFrame &, bool modern, bool allowGameplayLayers = true);
	void cursor(const IndexedFrame &);
	void present(std::optional<std::pair<int, int>> cursorPosition);
	bool animating() const;
	std::pair<int, int> outputSize() const;
private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
}
#endif
