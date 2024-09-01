#pragma once
#include <vector>
#include <GL/glew.h>

namespace Brainstorm {
	struct VertexBuffer {
		std::vector<float> data;
		int dimensions;

		VertexBuffer(const std::vector<float>& data, int dimensions);
	};

	class Mesh {
	private:
		GLuint id;
		static GLuint boundId;

		std::vector<GLuint> buffers;

		GLsizei vertexCount;
		GLint renderMode;
	public:
		const static GLint TRIANGLES;
		const static GLint TRIANGLE_FAN;
		const static GLint TRIANGLE_STRIP;

		const static GLint LINES;
		const static GLint LINE_STRIP;
		const static GLint LINE_LOOP;

		const static GLint POINTS;

		Mesh(const VertexBuffer& vertices, const std::vector<VertexBuffer>& additional, GLint renderMode);
		~Mesh();

		void render() const;
		void destroy();

		static void drop();
	};
}