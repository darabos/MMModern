#ifndef MMODERN_PLATFORM_SDL_INDOOR_CAMERA_SPRING_H
#define MMODERN_PLATFORM_SDL_INDOOR_CAMERA_SPRING_H

#include "core/IndoorRenderScene.h"
#include <cmath>

namespace mmodern {

class IndoorCameraSpring {
public:
	void target(const IndoorCameraPose &pose, bool snap) {
		_target = pose;
		if (snap || !_initialized) {
			_current = pose;
			_velocity = {};
			_initialized = true;
		}
	}
	void advance(double seconds) {
		if (!_initialized || seconds <= 0) return;
		advanceAxis(_current.x, _velocity.x, _target.x, seconds, 0.0001);
		advanceAxis(_current.y, _velocity.y, _target.y, seconds, 0.0001);
		advanceAxis(_current.turns, _velocity.turns, _target.turns, seconds, 0.00001);
	}
	const IndoorCameraPose &pose() const { return _current; }
	bool moving() const {
		return _initialized && (_current.x != _target.x || _current.y != _target.y ||
			_current.turns != _target.turns || _velocity.x != 0 ||
			_velocity.y != 0 || _velocity.turns != 0);
	}
private:
	static void advanceAxis(double &position, double &velocity, double target,
			double seconds, double tolerance) {
		constexpr double frequency = 16.0;
		const double offset = position - target;
		const double impulse = velocity + frequency * offset;
		const double decay = std::exp(-frequency * seconds);
		position = target + (offset + impulse * seconds) * decay;
		velocity = (velocity - frequency * impulse * seconds) * decay;
		if (std::abs(position - target) < tolerance && std::abs(velocity) < tolerance * frequency) {
			position = target;
			velocity = 0;
		}
	}
	IndoorCameraPose _current, _target, _velocity;
	bool _initialized = false;
};

} // namespace mmodern
#endif
