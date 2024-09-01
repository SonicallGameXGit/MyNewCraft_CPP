#pragma once
#include <GL/glew.h>
#include <array>

#include "../io/logger.h"

namespace Brainstorm {
	enum class AttachmentFormat : GLenum {
		DEPTH, R, RG, RGB, RGBA
	};

	struct Attachment {
		AttachmentFormat format;
		GLsizei width, height;
	};

	template<size_t Attachments>
	class FrameBuffer {
	private:
		GLuint id;
		std::array<GLuint, Attachments> attachments;
	public:
		FrameBuffer(const std::array<Attachment, Attachments>& attachments);
	};
}