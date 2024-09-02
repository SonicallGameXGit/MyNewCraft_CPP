#include <Brainstorm/brainstorm.h>
#include <glm/gtc/matrix_transform.hpp>
#include "FastNoise.h"

#include <thread>
#include <mutex>

#define INDEX_FROM_XYZ(X, Y, Z, WIDTH, LENGTH) ((X) + (Z) * (WIDTH) + (Y) * (WIDTH) * (LENGTH))

struct MatrixHelper {
	static glm::mat4 perspective(const BS::Window* window, float fov) {
		return glm::perspective(glm::radians(fov), static_cast<float>(window->getWidth()) / window->getHeight(), 0.01f, 500.0f);
	}
	static glm::mat4 view(const glm::vec3& position, const glm::vec3 rotation) {
		glm::mat4 matrix = glm::mat4(1.0f);
		matrix = glm::rotate(matrix, -glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		matrix = glm::rotate(matrix, -glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		matrix = glm::rotate(matrix, -glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
		matrix = glm::translate(matrix, -position);

		return matrix;
	}
	static glm::mat4 transform(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale) {
		glm::mat4 matrix = glm::mat4(1.0f);
		matrix = glm::translate(matrix, position);
		matrix = glm::rotate(matrix, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		matrix = glm::rotate(matrix, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		matrix = glm::rotate(matrix, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
		matrix = glm::scale(matrix, scale);

		return matrix;
	}
};

class BlockTextureAtlas {
private:
public:
	const GLuint id = 0;
	static const float SCALAR_X, SCALAR_Y;
	
	BlockTextureAtlas() {}
	BlockTextureAtlas(GLuint id) : id(id) {}

	static BlockTextureAtlas create() {
		return BlockTextureAtlas(BS::Texture::loadFromFile("assets/textures/terrain.png", BS::Texture::FILTER_NEAREST));
	}
};
struct BlockFace {
	glm::vec2 uv = glm::vec2(); // "NULL" texture

	BlockFace() {}
	BlockFace(glm::ivec2 uv) : uv(glm::vec2(uv.x * BlockTextureAtlas::SCALAR_X, uv.y * BlockTextureAtlas::SCALAR_Y)) {}
};

const float BlockTextureAtlas::SCALAR_X = 16.0f / 256.0f;
const float BlockTextureAtlas::SCALAR_Y = 16.0f / 256.0f;

struct Block {
	BlockFace front = {};
	BlockFace back = {};
	BlockFace top = {};
	BlockFace bottom = {};
	BlockFace right = {};
	BlockFace left = {};

	static inline Block create(const BlockFace all) {
		return {
			.front = all,
			.back = all,
			.top = all,
			.bottom = all,
			.right = all,
			.left = all
		};
	}
	static inline Block create(const BlockFace side, const BlockFace top, const BlockFace bottom) {
		return {
			.front = side,
			.back = side,
			.top = top,
			.bottom = bottom,
			.right = side,
			.left = side
		};
	}
	static inline Block create(const BlockFace horizontal, const BlockFace vertical) {
		return {
			.front = horizontal,
			.back = horizontal,
			.top = vertical,
			.bottom = vertical,
			.right = horizontal,
			.left = horizontal
		};
	}
};
class Blocks {
private:
	static std::vector<Block> blocks;
public:
	static void registerEntry(const Block block) {
		Blocks::blocks.push_back(block);
	}
	static const Block* getEntry(uint8_t id) {
		id--;

		if (id < 0 || id >= Blocks::blocks.size()) {
			return nullptr;
		}

		return &Blocks::blocks[id];
	}
};

std::vector<Block> Blocks::blocks = {};

class Chunk {
private:
	uint8_t* blocks = nullptr;
	glm::ivec3 position = glm::ivec3();

	const FastNoise* noise = nullptr;

	inline double getPerlin(double x, double z) const {
		return this->noise->GetPerlin(x, z) + 0.5;
	}
	inline int getHillsHeight(int64_t x, int64_t z) const {
		return static_cast<int>(
			this->getPerlin(x, z) * 48.0 +
			this->getPerlin(x * 5.0, z * 5.0) * 12.0 +
			this->getPerlin(x * 0.1, z * 0.1) * 32.0
		);
	}
	inline int getPlainsHeight(int64_t x, int64_t z) const {
		return static_cast<int>(
			20.0 +
			getPerlin(x * 0.4, z * 0.4) * 3.0 +
			getPerlin(x * 0.1, z * 0.1) * 12.0
		);
	}
	inline int getMountainsHeight(int64_t x, int64_t z) const {
		return static_cast<int>(
			30.0f +
			(pow(glm::max(getPerlin(x * 2.0, z * 2.0), 0.0), 3.0) * 128.0 - getPerlin(x * 8.0 + 3829.0, z * 8.0 - 9438.0) * 20.0) * getPerlin(x * 0.1, z * 0.1) +
			getPerlin(x * 12.0, z * 12.0) * 3.0
		);
	}
public:
	static const uint16_t WIDTH = 32, HEIGHT = 32, LENGTH = 32;

	~Chunk() {
		delete[] this->blocks;
	}

	void setGenerator(const FastNoise* noise) {
		this->noise = noise;
	}
	void create(const glm::ivec3& position, size_t chunksY) {
		if (this->blocks != nullptr || this->noise == nullptr) return;

		this->position = position;
		this->blocks = new uint8_t[Chunk::WIDTH * Chunk::HEIGHT * Chunk::LENGTH]();

		if (this->position.y == 0) {
			for (uint16_t x = 0; x < Chunk::WIDTH; x++) {
				for (uint16_t z = 0; z < Chunk::LENGTH; z++) {
					this->setBlock(x, 0, z, 3);
				}
			}
		}

		for (uint16_t x = 0; x < Chunk::WIDTH; x++) {
			for (uint16_t z = 0; z < Chunk::LENGTH; z++) {
				int64_t globalX = x + static_cast<int64_t>(this->position.x) * Chunk::WIDTH;
				int64_t globalZ = z + static_cast<int64_t>(this->position.z) * Chunk::LENGTH;

				int height = glm::mix(this->getHillsHeight(globalX, globalZ), this->getPlainsHeight(globalX, globalZ), this->getPerlin(globalX * 0.05, globalZ * 0.05));
				height = glm::mix(height, this->getMountainsHeight(globalX, globalZ), this->getPerlin(globalX * 0.05 + 3243.0, globalZ * 0.05 - 3923.0));
				height += 32;
				height -= this->position.y * Chunk::HEIGHT;
				
				int clampedHeight = glm::clamp<int>(height, 0, Chunk::HEIGHT);

				for (uint16_t y = this->position.y == 0 ? 1 : 0; y < clampedHeight; y++) {
					if (this->noise->GetSimplex(globalX * 4.0, (this->position.y * Chunk::HEIGHT + y) * 4.0, globalZ * 4.0) <= -0.49) continue;

					uint8_t block = 4;
					
					if (clampedHeight == height && y == clampedHeight - 1) block = 1;
					else if (y < height - 4 - this->noise->GetWhiteNoise(globalX, globalZ) * 3.0f) block = 2;

					this->setBlock(x, y, z, block);
				}
			}
		}
	}

	void setBlock(uint16_t x, uint16_t y, uint16_t z, uint8_t block) {
		if (this->blocks == nullptr || x >= Chunk::WIDTH || y >= Chunk::HEIGHT || z >= Chunk::LENGTH) return;
		this->blocks[INDEX_FROM_XYZ(x, y, z, Chunk::WIDTH, Chunk::LENGTH)] = block;
	}
	uint8_t getBlock(uint16_t x, uint16_t y, uint16_t z) const {
		if (this->blocks == nullptr || x >= Chunk::WIDTH || y >= Chunk::HEIGHT || z >= Chunk::LENGTH) return 0;
		return this->blocks[INDEX_FROM_XYZ(x, y, z, Chunk::WIDTH, Chunk::LENGTH)];
	}

	glm::ivec3 getPosition() const {
		return this->position;
	}
};
class ChunkMesh {
private:
	GLuint id = 0, vboId = 0, tboId = 0, nboId = 0, aboId = 0;
	GLsizei vertexCount = 0;

	const Chunk* chunk = nullptr;
	bool dirty = false, gpuDirty = false;

	std::vector<float> vertices, texcoords, normals, ambients;

	inline void addNormals(float x, float y, float z) {
		this->normals.push_back(x); this->normals.push_back(y); this->normals.push_back(z);
		this->normals.push_back(x); this->normals.push_back(y); this->normals.push_back(z);
		this->normals.push_back(x); this->normals.push_back(y); this->normals.push_back(z);

		this->normals.push_back(x); this->normals.push_back(y); this->normals.push_back(z);
		this->normals.push_back(x); this->normals.push_back(y); this->normals.push_back(z);
		this->normals.push_back(x); this->normals.push_back(y); this->normals.push_back(z);
	}
	inline void addTexcoords(BlockFace face) {
		this->texcoords.push_back(face.uv.x);								this->texcoords.push_back(face.uv.y);
		this->texcoords.push_back(face.uv.x + BlockTextureAtlas::SCALAR_X); this->texcoords.push_back(face.uv.y);
		this->texcoords.push_back(face.uv.x);								this->texcoords.push_back(face.uv.y + BlockTextureAtlas::SCALAR_Y);

		this->texcoords.push_back(face.uv.x + BlockTextureAtlas::SCALAR_X); this->texcoords.push_back(face.uv.y + BlockTextureAtlas::SCALAR_Y);
		this->texcoords.push_back(face.uv.x);								this->texcoords.push_back(face.uv.y + BlockTextureAtlas::SCALAR_Y);
		this->texcoords.push_back(face.uv.x + BlockTextureAtlas::SCALAR_X); this->texcoords.push_back(face.uv.y);
	}
	inline void addAmbients(uint8_t top, uint8_t right, uint8_t left, uint8_t bottom, uint8_t topRight, uint8_t topLeft, uint8_t bottomRight, uint8_t bottomLeft) {
		float a00 = ChunkMesh::buildAmbient(left, bottom, bottomLeft);
		float a10 = ChunkMesh::buildAmbient(right, bottom, bottomRight);
		float a11 = ChunkMesh::buildAmbient(right, top, topRight);
		float a01 = ChunkMesh::buildAmbient(left, top, topLeft);

		this->ambients.push_back(a10);
		this->ambients.push_back(a00);
		this->ambients.push_back(a11);
		this->ambients.push_back(a01);
		this->ambients.push_back(a11);
		this->ambients.push_back(a00);
	}

	static inline GLuint createVbo(const std::vector<float>& buffer, GLuint attributeIndex, GLint attributeDimensions) {
		GLuint id;

		glGenBuffers(1, &id);
		glBindBuffer(GL_ARRAY_BUFFER, id);

		glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(float), buffer.data(), GL_STATIC_DRAW);

		glEnableVertexAttribArray(attributeIndex);
		glVertexAttribPointer(attributeIndex, attributeDimensions, GL_FLOAT, false, 0, nullptr);

		return id;
	}
	static inline float buildAmbient(const uint8_t a, const uint8_t b, const uint8_t c) {
		return (a == 0 ? 0.0f : 1.0f) + (b == 0 ? 0.0f : 1.0f) + (c == 0 ? 0.0f : 1.0f);
	}

	inline void create(const Chunk* rightChunk, const Chunk* leftChunk, const Chunk* frontChunk, const Chunk* backChunk, const Chunk* topChunk, const Chunk* bottomChunk) {
		if (this->chunk == nullptr) return;

		for (uint16_t x0 = 0; x0 < Chunk::WIDTH; x0++) {
			for (uint16_t y0 = 0; y0 < Chunk::HEIGHT; y0++) {
				for (uint16_t z0 = 0; z0 < Chunk::LENGTH; z0++) {
					uint8_t id = this->chunk->getBlock(x0, y0, z0);
					if (id == 0) continue;

					uint16_t x1 = x0 + 1;
					uint16_t y1 = y0 + 1;
					uint16_t z1 = z0 + 1;

					const Block* block = Blocks::getEntry(id);

					if (y0 == Chunk::HEIGHT - 1 ? topChunk == nullptr || topChunk->getBlock(x0, 0, z0) == 0 : this->chunk->getBlock(x0, y0 + 1, z0) == 0) {
						this->vertices.push_back(x0);		this->vertices.push_back(y0 + 1); this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0);		this->vertices.push_back(y0 + 1); this->vertices.push_back(z0);

						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0);
						this->vertices.push_back(x0);		this->vertices.push_back(y0 + 1); this->vertices.push_back(z0);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0 + 1);

						this->addTexcoords(block->top);
						this->addNormals(0.0f, 1.0f, 0.0f);

						uint8_t top = this->chunk->getBlock(x0, y1, z0 - 1);
						uint8_t right = this->chunk->getBlock(x0 - 1, y1, z0);
						uint8_t left = this->chunk->getBlock(x1, y1, z0);
						uint8_t bottom = this->chunk->getBlock(x0, y1, z1);
						uint8_t topRight = this->chunk->getBlock(x0 - 1, y1, z0 - 1);
						uint8_t topLeft = this->chunk->getBlock(x1, y1, z0 - 1);
						uint8_t bottomRight = this->chunk->getBlock(x0 - 1, y1, z1);
						uint8_t bottomLeft = this->chunk->getBlock(x1, y1, z1);

						this->addAmbients(top, right, left, bottom, topRight, topLeft, bottomRight, bottomLeft);
					}
					if (y0 == 0 ? bottomChunk == nullptr || bottomChunk->getBlock(x0, Chunk::HEIGHT - 1, z0) == 0 : this->chunk->getBlock(x0, y0 - 1, z0) == 0) {
						this->vertices.push_back(x0);		 this->vertices.push_back(y0); this->vertices.push_back(z0);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0); this->vertices.push_back(z0);
						this->vertices.push_back(x0);		 this->vertices.push_back(y0); this->vertices.push_back(z0 + 1);

						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0); this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0);		 this->vertices.push_back(y0); this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0); this->vertices.push_back(z0);

						this->addTexcoords(block->bottom);
						this->addNormals(0.0f, -1.0f, 0.0f);

						uint8_t top = this->chunk->getBlock(x0, y0 - 1, z1);
						uint8_t right = this->chunk->getBlock(x0 - 1, y0 - 1, z0);
						uint8_t left = this->chunk->getBlock(x1, y0 - 1, z0);
						uint8_t bottom = this->chunk->getBlock(x0, y0 - 1, z0 - 1);
						uint8_t topRight = this->chunk->getBlock(x0 - 1, y0 - 1, z1);
						uint8_t topLeft = this->chunk->getBlock(x1, y0 - 1, z1);
						uint8_t bottomRight = this->chunk->getBlock(x0 - 1, y0 - 1, z0 - 1);
						uint8_t bottomLeft = this->chunk->getBlock(x1, y0 - 1, z0 - 1);

						this->addAmbients(top, right, left, bottom, topRight, topLeft, bottomRight, bottomLeft);
					}
					if (x0 == Chunk::WIDTH - 1 ? rightChunk == nullptr || rightChunk->getBlock(0, y0, z0) == 0 : this->chunk->getBlock(x0 + 1, y0, z0) == 0) {
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0);		  this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0);		  this->vertices.push_back(z0);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0 + 1);

						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0);		  this->vertices.push_back(z0);

						this->addTexcoords(block->right);
						this->addNormals(1.0f, 0.0f, 0.0f);

						uint8_t top = this->chunk->getBlock(x1, y1, z0);
						uint8_t right = this->chunk->getBlock(x1, y0, z1);
						uint8_t left = this->chunk->getBlock(x1, y0, z0 - 1);
						uint8_t bottom = this->chunk->getBlock(x1, y0 - 1, z0);
						uint8_t topRight = this->chunk->getBlock(x1, y1, z1);
						uint8_t topLeft = this->chunk->getBlock(x1, y1, z0 - 1);
						uint8_t bottomRight = this->chunk->getBlock(x1, y0 - 1, z1);
						uint8_t bottomLeft = this->chunk->getBlock(x1, y0 - 1, z0 - 1);

						this->addAmbients(top, right, left, bottom, topRight, topLeft, bottomRight, bottomLeft);
					}
					if (x0 == 0 ? leftChunk == nullptr || leftChunk->getBlock(Chunk::WIDTH - 1, y0, z0) == 0 : this->chunk->getBlock(x0 - 1, y0, z0) == 0) {
						this->vertices.push_back(x0); this->vertices.push_back(y0);		   this->vertices.push_back(z0);
						this->vertices.push_back(x0); this->vertices.push_back(y0);		   this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0);

						this->vertices.push_back(x0); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0);
						this->vertices.push_back(x0); this->vertices.push_back(y0);		   this->vertices.push_back(z0 + 1);

						this->addTexcoords(block->left);
						this->addNormals(-1.0f, 0.0f, 0.0f);

						uint8_t top = this->chunk->getBlock(x0 - 1, y1, z0);
						uint8_t right = this->chunk->getBlock(x0 - 1, y0, z0 - 1);
						uint8_t left = this->chunk->getBlock(x0 - 1, y0, z1);
						uint8_t bottom = this->chunk->getBlock(x0 - 1, y0 - 1, z0);
						uint8_t topRight = this->chunk->getBlock(x0 - 1, y1, z0 - 1);
						uint8_t topLeft = this->chunk->getBlock(x0 - 1, y1, z1);
						uint8_t bottomRight = this->chunk->getBlock(x0 - 1, y0 - 1, z0 - 1);
						uint8_t bottomLeft = this->chunk->getBlock(x0 - 1, y0 - 1, z1);

						this->addAmbients(top, right, left, bottom, topRight, topLeft, bottomRight, bottomLeft);
					}
					if (z0 == Chunk::LENGTH - 1 ? frontChunk == nullptr || frontChunk->getBlock(x0, y0, 0) == 0 : this->chunk->getBlock(x0, y0, z0 + 1) == 0) {
						this->vertices.push_back(x0);		 this->vertices.push_back(y0);		  this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0);		  this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0);		 this->vertices.push_back(y0 + 1); this->vertices.push_back(z0 + 1);

						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0);		 this->vertices.push_back(y0 + 1); this->vertices.push_back(z0 + 1);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0);		  this->vertices.push_back(z0 + 1);

						this->addTexcoords(block->front);
						this->addNormals(0.0f, 0.0f, 1.0f);

						uint8_t top = this->chunk->getBlock(x0, y1, z1);
						uint8_t right = this->chunk->getBlock(x0 - 1, y0, z1);
						uint8_t left = this->chunk->getBlock(x1, y0, z1);
						uint8_t bottom = this->chunk->getBlock(x0, y0 - 1, z1);
						uint8_t topRight = this->chunk->getBlock(x0 - 1, y1, z1);
						uint8_t topLeft = this->chunk->getBlock(x1, y1, z1);
						uint8_t bottomRight = this->chunk->getBlock(x0 - 1, y0 - 1, z1);
						uint8_t bottomLeft = this->chunk->getBlock(x1, y0 - 1, z1);

						this->addAmbients(top, right, left, bottom, topRight, topLeft, bottomRight, bottomLeft);
					}
					if (z0 == 0 ? backChunk == nullptr || backChunk->getBlock(x0, y0, Chunk::LENGTH - 1) == 0 : this->chunk->getBlock(x0, y0, z0 - 1) == 0) {
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0);		  this->vertices.push_back(z0);
						this->vertices.push_back(x0);		 this->vertices.push_back(y0);		  this->vertices.push_back(z0);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0);

						this->vertices.push_back(x0);		 this->vertices.push_back(y0 + 1); this->vertices.push_back(z0);
						this->vertices.push_back(x0 + 1); this->vertices.push_back(y0 + 1); this->vertices.push_back(z0);
						this->vertices.push_back(x0);		 this->vertices.push_back(y0);		  this->vertices.push_back(z0);

						this->addTexcoords(block->back);
						this->addNormals(0.0f, 0.0f, 1.0f);

						uint8_t top = this->chunk->getBlock(x0, y1, z0 - 1);
						uint8_t right = this->chunk->getBlock(x1, y0, z0 - 1);
						uint8_t left = this->chunk->getBlock(x0 - 1, y0, z0 - 1);
						uint8_t bottom = this->chunk->getBlock(x0, y0 - 1, z0 - 1);
						uint8_t topRight = this->chunk->getBlock(x1, y1, z0 - 1);
						uint8_t topLeft = this->chunk->getBlock(x0 - 1, y1, z0 - 1);
						uint8_t bottomRight = this->chunk->getBlock(x1, y0 - 1, z0 - 1);
						uint8_t bottomLeft = this->chunk->getBlock(x0 - 1, y0 - 1, z0 - 1);

						this->addAmbients(top, right, left, bottom, topRight, topLeft, bottomRight, bottomLeft);
					}
				}
			}
		}

		this->vertexCount = static_cast<GLsizei>(vertices.size() / 3);
		this->gpuDirty = true;
	}

	inline void clear() const {
		glDeleteVertexArrays(1, &this->id);

		glDeleteBuffers(1, &this->vboId);
		glDeleteBuffers(1, &this->tboId);
		glDeleteBuffers(1, &this->nboId);
		glDeleteBuffers(1, &this->aboId);
	}
public:
	ChunkMesh() {}
	~ChunkMesh() {
		this->clear();
	}

	void connect(const Chunk* chunk) {
		this->chunk = chunk;
	}

	void use() const {
		glBindVertexArray(this->id);
	}
	void render(const BS::ShaderProgram shader, const BlockTextureAtlas blockTextureAtlas, const glm::mat4& projectViewMatrix) const {
		BS::Texture::use(blockTextureAtlas.id);
		glm::mat4 modelMatrix = MatrixHelper::transform(this->chunk->getPosition() * glm::ivec3(Chunk::WIDTH, Chunk::HEIGHT, Chunk::LENGTH), glm::vec3(), glm::vec3(1.0f));

		shader.setMatrix4("mvpMatrix", projectViewMatrix * modelMatrix);
		shader.setMatrix4("modelMatrix", modelMatrix);
		
		glDrawArrays(GL_TRIANGLES, 0, this->vertexCount);
	}
	void update(const Chunk* rightChunk = nullptr, const Chunk* leftChunk = nullptr, const Chunk* frontChunk = nullptr, const Chunk* backChunk = nullptr, const Chunk* topChunk = nullptr, const Chunk* bottomChunk = nullptr) {
		if (!this->dirty) return;

		this->create(rightChunk, leftChunk, frontChunk, backChunk, topChunk, bottomChunk);
		this->dirty = false;
	}
	void saveToGPU() {
		if (!this->gpuDirty) return;
		this->clear();

		glGenVertexArrays(1, &this->id);
		this->use();

		this->vboId = ChunkMesh::createVbo(this->vertices, 0, 3);
		this->tboId = ChunkMesh::createVbo(this->texcoords, 1, 2);
		this->nboId = ChunkMesh::createVbo(this->normals, 2, 3);
		this->aboId = ChunkMesh::createVbo(this->ambients, 3, 1);

		BS::Mesh::drop();

		this->vertices.clear();
		this->vertices.shrink_to_fit();

		this->texcoords.clear();
		this->texcoords.shrink_to_fit();

		this->normals.clear();
		this->normals.shrink_to_fit();

		this->ambients.clear();
		this->ambients.shrink_to_fit();

		this->gpuDirty = false;
	}
	void markDirty() {
		this->dirty = true;
	}
};

class ChunkGenerator {
private:
	std::mutex blockChangeMutex, runningMutex;
	FastNoise noise;
public:
	static const size_t CHUNKS_X = 12, CHUNKS_Y = 8, CHUNKS_Z = 12;

	Chunk* chunks = new Chunk[CHUNKS_X * CHUNKS_Y * CHUNKS_Z]();
	ChunkMesh* chunkMeshes = new ChunkMesh[CHUNKS_X * CHUNKS_Y * CHUNKS_Z]();

	ChunkGenerator() {
		srand(0);
		this->noise = FastNoise(std::chrono::high_resolution_clock::now().time_since_epoch().count());
	}
	~ChunkGenerator() {
		delete[] this->chunks;
		delete[] this->chunkMeshes;
	}

	void create() {
		for (size_t x = 0; x < ChunkGenerator::CHUNKS_X; x++) {
			for (size_t y = 0; y < ChunkGenerator::CHUNKS_Y; y++) {
				for (size_t z = 0; z < ChunkGenerator::CHUNKS_Z; z++) {
					size_t id = INDEX_FROM_XYZ(x, y, z, ChunkGenerator::CHUNKS_X, ChunkGenerator::CHUNKS_Z);

					this->chunks[id].setGenerator(&this->noise);
					this->chunks[id].create(glm::ivec3(x, y, z), ChunkGenerator::CHUNKS_Y);

					this->chunkMeshes[id].connect(&this->chunks[id]);
					this->chunkMeshes[id].markDirty();
				}
			}
		}
	}
	void run(BS::Window* window) {
		while (window->isRunning()) {
			for (size_t x = 0; x < ChunkGenerator::CHUNKS_X; x++) {
				for (size_t y = 0; y < ChunkGenerator::CHUNKS_Y; y++) {
					for (size_t z = 0; z < ChunkGenerator::CHUNKS_Z; z++) {
						this->chunkMeshes[INDEX_FROM_XYZ(x, y, z, ChunkGenerator::CHUNKS_X, ChunkGenerator::CHUNKS_Z)].update(
							static_cast<uint16_t>(x) == ChunkGenerator::CHUNKS_X - 1 ? nullptr : &this->chunks[INDEX_FROM_XYZ(x + 1, y, z, ChunkGenerator::CHUNKS_X, ChunkGenerator::CHUNKS_Z)],
																				x == 0 ? nullptr : &this->chunks[INDEX_FROM_XYZ(x - 1, y, z, ChunkGenerator::CHUNKS_X, ChunkGenerator::CHUNKS_Z)],
							static_cast<uint16_t>(z) == ChunkGenerator::CHUNKS_Z - 1 ? nullptr : &this->chunks[INDEX_FROM_XYZ(x, y, z + 1, ChunkGenerator::CHUNKS_X, ChunkGenerator::CHUNKS_Z)],
																				z == 0 ? nullptr : &this->chunks[INDEX_FROM_XYZ(x, y, z - 1, ChunkGenerator::CHUNKS_X, ChunkGenerator::CHUNKS_Z)],
							static_cast<uint16_t>(y) == ChunkGenerator::CHUNKS_Y - 1 ? nullptr : &this->chunks[INDEX_FROM_XYZ(x, y + 1, z, ChunkGenerator::CHUNKS_X, ChunkGenerator::CHUNKS_Z)],
																				y == 0 ? nullptr : &this->chunks[INDEX_FROM_XYZ(x, y - 1, z, ChunkGenerator::CHUNKS_X, ChunkGenerator::CHUNKS_Z)]
						);
					}
				}
			}
		}
	}

	void setBlock(uint32_t x, uint32_t y, uint32_t z, uint8_t block) {
		int chunkX = static_cast<int>(floor(x / Chunk::WIDTH));
		int chunkY = static_cast<int>(floor(y / Chunk::HEIGHT));
		int chunkZ = static_cast<int>(floor(z / Chunk::LENGTH));

		if (chunkX < 0 || chunkY < 0 || chunkZ < 0 || chunkX >= CHUNKS_X || chunkY >= CHUNKS_Y || chunkZ >= CHUNKS_Z) return;
		size_t index = INDEX_FROM_XYZ(chunkX, chunkY, chunkZ, CHUNKS_X, CHUNKS_Z);

		uint16_t localX = x - chunkX * Chunk::WIDTH;
		uint16_t localY = y - chunkY * Chunk::HEIGHT;
		uint16_t localZ = z - chunkZ * Chunk::LENGTH;

		this->chunks[index].setBlock(localX, localY, localZ, block);
		this->chunkMeshes[index].markDirty();

		if (localX == 0 && chunkX > 0) this->chunkMeshes[INDEX_FROM_XYZ(chunkX - 1, chunkY, chunkZ, CHUNKS_X, CHUNKS_Z)].markDirty();
		if (localX == Chunk::WIDTH - 1 && chunkX < CHUNKS_X - 1) this->chunkMeshes[INDEX_FROM_XYZ(chunkX + 1, chunkY, chunkZ, CHUNKS_X, CHUNKS_Z)].markDirty();
		if (localY == 0 && chunkY > 0) this->chunkMeshes[INDEX_FROM_XYZ(chunkX, chunkY - 1, chunkZ, CHUNKS_X, CHUNKS_Z)].markDirty();
		if (localY == Chunk::HEIGHT - 1 && chunkY < CHUNKS_Y - 1) this->chunkMeshes[INDEX_FROM_XYZ(chunkX, chunkY + 1, chunkZ, CHUNKS_X, CHUNKS_Z)].markDirty();
		if (localZ == 0 && chunkZ > 0) this->chunkMeshes[INDEX_FROM_XYZ(chunkX, chunkY, chunkZ - 1, CHUNKS_X, CHUNKS_Z)].markDirty();
		if (localZ == Chunk::LENGTH - 1 && chunkZ < CHUNKS_Z - 1) this->chunkMeshes[INDEX_FROM_XYZ(chunkX, chunkY, chunkZ + 1, CHUNKS_X, CHUNKS_Z)].markDirty();
	}
	uint8_t getBlock(uint32_t x, uint32_t y, uint32_t z) const {
		int chunkX = static_cast<int>(floor(x / Chunk::WIDTH));
		int chunkY = static_cast<int>(floor(y / Chunk::HEIGHT));
		int chunkZ = static_cast<int>(floor(z / Chunk::LENGTH));

		if (chunkX < 0 || chunkY < 0 || chunkZ < 0 || chunkX >= CHUNKS_X || chunkY >= CHUNKS_Y || chunkZ >= CHUNKS_Z) return 0;
		
		return this->chunks[INDEX_FROM_XYZ(chunkX, chunkY, chunkZ, CHUNKS_X, CHUNKS_Z)].getBlock(
			x - chunkX * Chunk::WIDTH,
			y - chunkY * Chunk::HEIGHT,
			z - chunkZ * Chunk::LENGTH
		);
	}
};

struct World {
	float gravity = 32.0f;
};
class Camera {
private:
	static inline float clipVelocity(const glm::vec3& minA, const glm::vec3& maxA, const glm::vec3& minB, const glm::vec3& maxB, float velocity, size_t axis) {
		int x = axis;
		int y = x + 1 >= 3 ? 0 : x + 1;
		int z = y + 1 >= 3 ? 0 : y + 1;

		if (minA[y] >= maxB[y] || maxA[y] <= minB[y]) {
			return velocity;
		}
		if (minA[z] >= maxB[z] || maxA[z] <= minB[z]) {
			return velocity;
		}

		if (velocity > 0.0f && maxA[x] <= minB[x]) {
			float difference = minB[x] - maxA[x] - 0.01f;
			if (difference < velocity) {
				return difference;
			}
		}
		if (velocity < 0.0f && minA[x] >= maxB[x]) {
			float difference = maxB[x] - minA[x] + 0.01f;
			if (difference > velocity) {
				return difference;
			}
		}

		return velocity;
	}

	void collide(const ChunkGenerator& chunkGenerator, const float delta) {
		glm::vec3 boxMin = this->position;
		glm::vec3 boxMax = this->position + this->scale;

		float scaledVx = this->velocity.x * delta;
		float scaledVy = this->velocity.y * delta;
		float scaledVz = this->velocity.z * delta;

		float originVx = scaledVx;
		float originVy = scaledVy;
		float originVz = scaledVz;

		float velocity = abs(scaledVy);
		for (int x = (int)boxMin.x; x <= (int)boxMax.x; ++x) {
			for (int y = (int)(boxMin.y - velocity) - 1; y <= (int)(boxMax.y + velocity) + 1; ++y) {
				for (int z = (int)boxMin.z; z <= (int)boxMax.z; ++z) {
					uint8_t block = chunkGenerator.getBlock(x, y, z);
					if (block == 0) {
						continue;
					}

					glm::vec3 blockMin = glm::vec3(x, y, z);
					glm::vec3 blockMax = glm::vec3(blockMin) + 1.0f;

					scaledVy = Camera::clipVelocity(boxMin, boxMax, blockMin, blockMax, scaledVy, 1);
				}
			}
		}
		this->position.y += scaledVy;

		// X collision
		boxMin = this->position;
		boxMax = this->position + this->scale;

		velocity = abs(scaledVx);
		for (int x = (int)(boxMin.x - velocity) - 1; x <= (int)(boxMax.x + velocity) + 1; ++x) {
			for (int y = (int)boxMin.y; y <= (int)boxMax.y; ++y) {
				for (int z = (int)boxMin.z; z <= (int)boxMax.z; ++z) {
					uint8_t block = chunkGenerator.getBlock(x, y, z);
					if (block == 0) {
						continue;
					}

					glm::vec3 blockMin = glm::vec3(x, y, z);
					glm::vec3 blockMax = glm::vec3(blockMin) + 1.0f;

					scaledVx = Camera::clipVelocity(boxMin, boxMax, blockMin, blockMax, scaledVx, 0);
				}
			}
		}
		this->position.x += scaledVx;

		// Z collision
		boxMin = this->position;
		boxMax = this->position + this->scale;

		velocity = abs(scaledVz);
		for (int x = (int)boxMin.x; x <= (int)boxMax.x; ++x) {
			for (int y = (int)boxMin.y; y <= (int)boxMax.y; ++y) {
				for (int z = (int)(boxMin.z - velocity) - 1; z <= (int)(boxMax.z + velocity) + 1; ++z) {
					uint8_t block = chunkGenerator.getBlock(x, y, z);
					if (block == 0) {
						continue;
					}

					glm::vec3 blockMin = glm::vec3(x, y, z);
					glm::vec3 blockMax = glm::vec3(blockMin) + 1.0f;

					scaledVz = Camera::clipVelocity(boxMin, boxMax, blockMin, blockMax, scaledVz, 2);
				}
			}
		}
		this->position.z += scaledVz;

		if (originVy != 0.0f) {
			this->onGround = false;
		}
		if (originVy != scaledVy) {
			if (originVy < 0.0) {
				this->onGround = true;
			}

			this->velocity.y = 0.0f;
		}
		if (originVx != scaledVx) {
			if (abs(originVx - scaledVx) / glm::max(originVx, scaledVx) > 0.2f) {
				this->running = false;
			}

			this->velocity.x = 0.0f;
		}
		if (originVz != scaledVz) {
			if (abs(originVz - scaledVz) / glm::max(originVz, scaledVz) > 0.2f) {
				this->running = false;
			}

			this->velocity.z = 0.0f;
		}
	}

	// Quake Physics
	/*void friction(const glm::vec2& innerForce, float delta) {
		if (this->onGround) {
			this->velocity = glm::mix(this->velocity, glm::vec3(), glm::clamp(24.0f * delta, 0.0f, 1.0f));
		}
	}
	void accelerate(const glm::vec3& innerForce, float delta) {
		this->friction(innerForce, delta);

		float currentSpeed = glm::dot(glm::vec3(this->velocity.x, 0.0f, this->velocity.z), innerForce);
		float addSpeed = glm::clamp(this->speed - currentSpeed, 0.0f, (this->onGround ? this->speed * 10.0f : this->speed * 2.0f) * delta);

		this->velocity += addSpeed * innerForce;
	}*/

	bool onGround = false, running = false, debugMode = false;

	glm::vec2 bobbingOffset = glm::vec2();
	float bobbingTime = 0.0f;

	float currentFov = 90.0f;
public:
	glm::vec3 position, rotation, scale, velocity = glm::vec3();
	
	float speed = 4.0f, runSpeed = 5.0f;
	float fov = 90.0f, runFov = 100.0f;

	Camera(glm::vec3 position = glm::vec3(), glm::vec3 rotation = glm::vec3(), glm::vec3 scale = glm::vec3(0.5f, 1.82f, 0.5f)) :
		position(position),
		rotation(rotation),
		scale(scale)
	{}

	void update(const BS::Window& window, const World& world, const ChunkGenerator& chunkGenerator, const BS::Timer& timer) {
		glm::vec3 innerForce = glm::vec3();

		if (window.isKeyPressed(BS::KeyCode::W)) {
			innerForce.x -= sin(glm::radians(this->rotation.y));
			innerForce.z -= cos(glm::radians(this->rotation.y));
		}
		if (window.isKeyPressed(BS::KeyCode::S)) {
			innerForce.x += sin(glm::radians(this->rotation.y));
			innerForce.z += cos(glm::radians(this->rotation.y));
		}
		if (window.isKeyPressed(BS::KeyCode::D)) {
			innerForce.x += cos(glm::radians(this->rotation.y));
			innerForce.z -= sin(glm::radians(this->rotation.y));
		}
		if (window.isKeyPressed(BS::KeyCode::A)) {
			innerForce.x -= cos(glm::radians(this->rotation.y));
			innerForce.z += sin(glm::radians(this->rotation.y));
		}
		
		if (glm::length(innerForce) > glm::epsilon<float>()) innerForce = glm::normalize(innerForce);

		this->running = window.isKeyPressed(BS::KeyCode::LEFT_CONTROL) && glm::length(innerForce) > glm::epsilon<float>() && glm::dot(innerForce, glm::vec3(-sin(glm::radians(this->rotation.y)), 0.0f, -cos(glm::radians(this->rotation.y)))) > 0.0f;

		if (!this->debugMode) this->velocity.y -= world.gravity * timer.getDelta();

		if (window.isKeyJustPressed(BS::KeyCode::F4)) {
			this->debugMode = !this->debugMode;
		}

		if (window.isKeyPressed(BS::KeyCode::SPACE) && (this->onGround || this->debugMode)) {
			if (!this->debugMode) {
				velocity.y = 10.0f;
				velocity.x *= 1.2f;
				velocity.z *= 1.2f;
			}
			else {
				innerForce.y += 1.0f;
			}

			this->onGround = false;
		}
		if (this->debugMode && window.isKeyPressed(BS::KeyCode::LEFT_SHIFT)) {
			innerForce.y -= 1.0f;
		}

		innerForce *= this->running ? this->runSpeed : this->speed;
		this->velocity = glm::mix(this->velocity, glm::vec3(innerForce.x, this->debugMode ? innerForce.y : this->velocity.y, innerForce.z), glm::clamp((this->onGround ? 24.0f : 4.0f) * timer.getDelta(), 0.0f, 1.0f));

		// Quake Physics
		// this->accelerate(innerForce, timer.getDelta());

		this->rotation.x -= window.getMouseDy() * 0.1f;
		this->rotation.y -= window.getMouseDx() * 0.1f;

		this->rotation.x = glm::clamp(this->rotation.x, -90.0f, 90.0f);
		this->rotation.y -= floor(this->rotation.y / 360.0f) * 360.0f;

		if (this->debugMode) this->position += this->velocity * timer.getDelta();
		else this->collide(chunkGenerator, timer.getDelta());

		glm::vec2 rawBobbingOffset = glm::vec2();
		if (glm::min(glm::length(glm::vec2(this->velocity.x, this->velocity.z)), glm::length(innerForce)) > glm::epsilon<float>() && this->onGround) {
			this->bobbingTime += glm::length(glm::vec2(this->velocity.x, this->velocity.z)) * 2.0f * timer.getDelta();
			if (this->bobbingTime >= glm::pi<float>() * 4.0f) {
				this->bobbingTime = 0.0f;
			}

			rawBobbingOffset = glm::vec2(cos(this->bobbingTime) * 0.1f, abs(sin(this->bobbingTime)) * 0.2f);
		}
		else {
			this->bobbingTime = glm::mix(this->bobbingTime, 0.0f, 12.0f * timer.getDelta());
		}

		this->bobbingOffset = glm::mix(this->bobbingOffset, rawBobbingOffset, 12.0f * timer.getDelta());
		this->currentFov = glm::mix(this->currentFov, this->running ? this->runFov : this->fov, 6.0f * timer.getDelta());
	}

	glm::mat4 getProjectViewMatrix(BS::Window* window) const {
		return MatrixHelper::perspective(window, this->currentFov) * MatrixHelper::view(
			this->getEyePosition(),
			this->rotation
		);
	}

	glm::vec3 getEyePosition() const {
		return
			this->position +
			glm::vec3(this->scale.x * 0.5f, this->scale.y - 0.1f, this->scale.z * 0.5f) +
			glm::vec3(
				this->bobbingOffset.x * cos(glm::radians(this->rotation.y)),
				this->bobbingOffset.y,
				this->bobbingOffset.x * -sin(glm::radians(this->rotation.y))
			);
	}
};

class MainWindow : public BS::Window {
private:
	BS::Timer timer;
	Camera camera = Camera(glm::vec3(84.0f, 72.0f, 222.0f), glm::vec3(-45.0f, 0.0f, 0.0f));

	BS::ShaderProgram terrainShader = BS::ShaderProgram("assets/shaders/terrain.vsh", "assets/shaders/terrain.fsh", nullptr);

	float fpsTimer = 0.0f;
	int fps = 0;
private:
	void createBlob(uint16_t x, uint16_t y, uint16_t z, uint8_t block, int blobSize = 12, bool noisy = false) {		
		for (int ox = -blobSize; ox < blobSize; ox++) {
			for (int oy = -blobSize; oy < blobSize; oy++) {
				for (int oz = -blobSize; oz < blobSize; oz++) {
					if (glm::length(glm::vec3(ox, oy, oz)) <= blobSize - (noisy ? rand() % 10000 / 10000.0f : 0)) {
						this->chunkGenerator.setBlock(
							x + ox,
							y + oy,
							z + oz,
							block
						);
					}
				}
			}
		}
	}

	const BlockTextureAtlas blockTextureAtlas;
	
	World world;
	ChunkGenerator chunkGenerator;

	std::thread chunkGeneratorThread;
public:
	MainWindow() : Window(1920, 1080, "MineStorm"), blockTextureAtlas(BlockTextureAtlas::create()) {
		//this->disableVSync();
		this->grabMouse();

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);

		glClearColor(130.0f / 255.0f, 172.0f / 255.0f, 254.0f / 255.0f, 1.0f);

		//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		Blocks::registerEntry(Block::create(BlockFace(glm::ivec2(3, 15)), BlockFace(glm::ivec2(0, 15)), BlockFace(glm::ivec2(2, 15))));
		Blocks::registerEntry(Block::create(BlockFace(glm::ivec2(1, 15))));
		Blocks::registerEntry(Block::create(BlockFace(glm::ivec2(1, 14))));
		Blocks::registerEntry(Block::create(BlockFace(glm::ivec2(2, 15))));

		this->chunkGenerator.create();
		
		this->chunkGeneratorThread = std::thread(&ChunkGenerator::run, &this->chunkGenerator, this);
		if (this->chunkGeneratorThread.joinable()) this->chunkGeneratorThread.detach();
	}

	void onUpdate() override {
		this->timer.update();
		this->camera.update(*this, this->world, this->chunkGenerator, this->timer);

		this->fpsTimer += this->timer.getRealDelta();
		this->fps++;
		
		if (this->fpsTimer >= 1.0f) {
			this->setTitle(std::string("MineStorm | FPS: " + std::to_string(this->fps)).c_str());
			
			this->fpsTimer = 0.0f;
			this->fps = 0;
		}

		if (this->isKeyJustPressed(BS::KeyCode::ESCAPE)) {
			this->toggleMouse();
		}

		if (this->isMouseButtonPressed(BS::MouseButton::LEFT)) {
			uint16_t x = rand() % (ChunkGenerator::CHUNKS_X * Chunk::WIDTH);
			uint16_t y = rand() % (ChunkGenerator::CHUNKS_Y * Chunk::HEIGHT);
			uint16_t z = rand() % (ChunkGenerator::CHUNKS_Z * Chunk::LENGTH);

			this->createBlob(x, y, z, 1);
		}
		if (this->isMouseButtonJustPressed(BS::MouseButton::RIGHT)) {
			uint16_t x = rand() % (ChunkGenerator::CHUNKS_X * Chunk::WIDTH);
			uint16_t y = rand() % (ChunkGenerator::CHUNKS_Y * Chunk::HEIGHT);
			uint16_t z = rand() % (ChunkGenerator::CHUNKS_Z * Chunk::LENGTH);

			this->createBlob(x, y, z, 0, 16, true);
		}

		glm::mat4 projectViewMatrix = this->camera.getProjectViewMatrix(this);

		this->terrainShader.use();
		this->terrainShader.setVector3("eyePosition", this->camera.getEyePosition());
		this->terrainShader.setVector3("fogColor", glm::vec3(186 / 255.0f, 210 / 255.0f, 255 / 255.0f));

		for (size_t x = 0; x < ChunkGenerator::CHUNKS_X; x++) {
			for (size_t y = 0; y < ChunkGenerator::CHUNKS_Y; y++) {
				for (size_t z = 0; z < ChunkGenerator::CHUNKS_Z; z++) {
					size_t id = INDEX_FROM_XYZ(x, y, z, ChunkGenerator::CHUNKS_X, ChunkGenerator::CHUNKS_Z);

					this->chunkGenerator.chunkMeshes[id].saveToGPU();

					this->chunkGenerator.chunkMeshes[id].use();
					this->chunkGenerator.chunkMeshes[id].render(this->terrainShader, this->blockTextureAtlas, projectViewMatrix);
				}
			}
		}
	}
};

int main() {
	BS::initialize();
	BS::registerWindow(new MainWindow());

	return BS::run();
}