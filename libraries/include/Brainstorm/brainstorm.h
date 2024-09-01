#pragma once
#include "io/window.h"
#include "io/logger.h"

#include "graphics/mesh.h"
#include "graphics/shader.h"
#include "graphics/texture.h"
#include "graphics/framebuffer.h"

#include "util/time.h"

namespace BS = Brainstorm;

namespace Brainstorm {
	void initialize();
	void registerWindow(Window* window);
	
	int run();
}