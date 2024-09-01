#pragma once
#include <stdint.h>

namespace Brainstorm {
	class Timer {
	private:
		int64_t lastTime;
		float delta, time, realTime;
	public:
		float scale;

		Timer();

		void update();
		
		float getDelta() const;
		float getRealDelta() const;

		float getTime() const;
		float getRealTime() const;
	};
}