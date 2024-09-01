#include <Brainstorm/brainstorm.h>
#include <glm/gtc/matrix_transform.hpp>
#include <thread>

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

float tMaths_Lerp(float a, float b, float delta)
{
	float max = delta < 0.0f ? 0.0f : delta;
	return a + (b - a) * (max > 1.0f ? 1.0f : max);
}
void Noise_CombineSeed(int64_t seed, int64_t x, int64_t z)
{
	srand(seed + ((x * 73856093L) ^ (z * 19349663L)));
}
float Noise_GetRandom(int64_t seed, int64_t x, int64_t z)
{
	Noise_CombineSeed(seed, x, z);
	return (float)rand() / RAND_MAX;
}
float Noise_GetSmooth(int64_t seed, double x, double z)
{
	int64_t intX = (int64_t)floor(x);
	int64_t intZ = (int64_t)floor(z);

	double fracX = x - (double)intX;
	double fracZ = z - (double)intZ;

	float a = Noise_GetRandom(seed, intX, intZ);
	float b = Noise_GetRandom(seed, intX + 1L, intZ);
	float d = Noise_GetRandom(seed, intX, intZ + 1L);
	float c = Noise_GetRandom(seed, intX + 1L, intZ + 1L);

	return tMaths_Lerp(tMaths_Lerp(a, b, fracX), tMaths_Lerp(d, c, fracX), fracZ);
}

class Chunk {
private:
	uint8_t* blocks = nullptr;
	glm::ivec3 position = glm::ivec3();
public:
	static const uint16_t WIDTH = 16, HEIGHT = 16, LENGTH = 16;

	Chunk() {}
	~Chunk() {
		delete[] this->blocks;
	}

	void create(const glm::ivec3& position, size_t chunksY) {
		if (this->blocks != nullptr) return;

		this->position = position;
		this->blocks = new uint8_t[Chunk::WIDTH * Chunk::HEIGHT * Chunk::LENGTH]();

		if (this->position.y == 0) {
			for (uint16_t x = 0; x < Chunk::WIDTH; x++) {
				for (uint16_t z = 0; z < Chunk::LENGTH; z++) {
					this->setBlock(x, 0, z, 3);
				}
			}
		}

		const uint64_t seed = 0;
		for (uint16_t x = 0; x < Chunk::WIDTH; x++) {
			for (uint16_t z = 0; z < Chunk::LENGTH; z++) {
				int64_t globalX = x + static_cast<int64_t>(this->position.x) * Chunk::WIDTH;
				int64_t globalZ = z + static_cast<int64_t>(this->position.z) * Chunk::LENGTH;

				int height = static_cast<int>(
					Noise_GetSmooth(seed, globalX * 0.05f, globalZ * 0.05f) * 48.0f +
					Noise_GetSmooth(seed, globalX * 0.1f, globalZ * 0.1f) * 12.0f +
					Noise_GetSmooth(seed, globalX * 0.005f, globalZ * 0.005f) * 32.0f
				);
				height -= this->position.y * Chunk::HEIGHT;
				
				int clampedHeight = glm::clamp<int>(height, 0, Chunk::HEIGHT);

				for (uint16_t y = 0; y < clampedHeight; y++) {
					uint8_t block = 4;
					
					if (clampedHeight == height && y == clampedHeight - 1) block = 1;
					else if (y < height - 4 - Noise_GetRandom(seed, globalX, globalZ) * 3.0f) block = 2;

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
	GLuint id = 0, vboId = 0, tboId = 0, nboId = 0;
	GLsizei vertexCount = 0;

	const Chunk* chunk = nullptr;
	bool dirty = false;

	static inline void addNormals(std::vector<float>& normals, float x, float y, float z) {
		normals.push_back(x); normals.push_back(y); normals.push_back(z);
		normals.push_back(x); normals.push_back(y); normals.push_back(z);
		normals.push_back(x); normals.push_back(y); normals.push_back(z);

		normals.push_back(x); normals.push_back(y); normals.push_back(z);
		normals.push_back(x); normals.push_back(y); normals.push_back(z);
		normals.push_back(x); normals.push_back(y); normals.push_back(z);
	}
	static inline void addTexcoords(std::vector<float>& texcoords, BlockFace face) {
		texcoords.push_back(face.uv.x); texcoords.push_back(face.uv.y);
		texcoords.push_back(face.uv.x + BlockTextureAtlas::SCALAR_X); texcoords.push_back(face.uv.y);
		texcoords.push_back(face.uv.x); texcoords.push_back(face.uv.y + BlockTextureAtlas::SCALAR_Y);

		texcoords.push_back(face.uv.x + BlockTextureAtlas::SCALAR_X); texcoords.push_back(face.uv.y + BlockTextureAtlas::SCALAR_Y);
		texcoords.push_back(face.uv.x); texcoords.push_back(face.uv.y + BlockTextureAtlas::SCALAR_Y);
		texcoords.push_back(face.uv.x + BlockTextureAtlas::SCALAR_X); texcoords.push_back(face.uv.y);
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
	inline void create(const Chunk* rightChunk, const Chunk* leftChunk, const Chunk* frontChunk, const Chunk* backChunk, const Chunk* topChunk, const Chunk* bottomChunk) {
		if (this->chunk == nullptr) return;
		std::vector<float> vertices, texcoords, normals;

		for (uint16_t x = 0; x < Chunk::WIDTH; x++) {
			for (uint16_t y = 0; y < Chunk::HEIGHT; y++) {
				for (uint16_t z = 0; z < Chunk::LENGTH; z++) {
					uint8_t id = this->chunk->getBlock(x, y, z);
					if (id == 0) continue;

					const Block* block = Blocks::getEntry(id);

					if (y == Chunk::HEIGHT - 1 ? topChunk == nullptr || topChunk->getBlock(x, 0, z) == 0 : this->chunk->getBlock(x, y + 1, z) == 0) {
						vertices.push_back(x); vertices.push_back(y + 1.0f); vertices.push_back(z + 1.0f);
						vertices.push_back(x + 1.0f); vertices.push_back(y + 1.0f); vertices.push_back(z + 1.0f);
						vertices.push_back(x); vertices.push_back(y + 1.0f); vertices.push_back(z);

						vertices.push_back(x + 1.0f); vertices.push_back(y + 1.0f); vertices.push_back(z);
						vertices.push_back(x); vertices.push_back(y + 1.0f); vertices.push_back(z);
						vertices.push_back(x + 1.0f); vertices.push_back(y + 1.0f); vertices.push_back(z + 1.0f);

						ChunkMesh::addTexcoords(texcoords, block->top);
						ChunkMesh::addNormals(normals, 0.0f, 1.0f, 0.0f);
					}
					if (y == 0 ? bottomChunk == nullptr || bottomChunk->getBlock(x, Chunk::HEIGHT - 1, z) == 0 : this->chunk->getBlock(x, y - 1, z) == 0) {
						vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
						vertices.push_back(x + 1.0f); vertices.push_back(y); vertices.push_back(z);
						vertices.push_back(x); vertices.push_back(y); vertices.push_back(z + 1.0f);

						vertices.push_back(x + 1.0f); vertices.push_back(y); vertices.push_back(z + 1.0f);
						vertices.push_back(x); vertices.push_back(y); vertices.push_back(z + 1.0f);
						vertices.push_back(x + 1.0f); vertices.push_back(y); vertices.push_back(z);

						ChunkMesh::addTexcoords(texcoords, block->bottom);
						ChunkMesh::addNormals(normals, 0.0f, -1.0f, 0.0f);
					}
					if (x == Chunk::WIDTH - 1 ? rightChunk == nullptr || rightChunk->getBlock(0, y, z) == 0 : this->chunk->getBlock(x + 1, y, z) == 0) {
						vertices.push_back(x + 1.0f); vertices.push_back(y); vertices.push_back(z + 1.0f);
						vertices.push_back(x + 1.0f); vertices.push_back(y); vertices.push_back(z);
						vertices.push_back(x + 1.0f); vertices.push_back(y + 1.0f); vertices.push_back(z + 1.0f);

						vertices.push_back(x + 1.0f); vertices.push_back(y + 1.0f); vertices.push_back(z);
						vertices.push_back(x + 1.0f); vertices.push_back(y + 1.0f); vertices.push_back(z + 1.0f);
						vertices.push_back(x + 1.0f); vertices.push_back(y); vertices.push_back(z);

						ChunkMesh::addTexcoords(texcoords, block->right);
						ChunkMesh::addNormals(normals, 1.0f, 0.0f, 0.0f);
					}
					if (x == 0 ? leftChunk == nullptr || leftChunk->getBlock(Chunk::WIDTH - 1, y, z) == 0 : this->chunk->getBlock(x - 1, y, z) == 0) {
						vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
						vertices.push_back(x); vertices.push_back(y); vertices.push_back(z + 1.0f);
						vertices.push_back(x); vertices.push_back(y + 1.0f); vertices.push_back(z);

						vertices.push_back(x); vertices.push_back(y + 1.0f); vertices.push_back(z + 1.0f);
						vertices.push_back(x); vertices.push_back(y + 1.0f); vertices.push_back(z);
						vertices.push_back(x); vertices.push_back(y); vertices.push_back(z + 1.0f);

						ChunkMesh::addTexcoords(texcoords, block->left);
						ChunkMesh::addNormals(normals, -1.0f, 0.0f, 0.0f);
					}
					if (z == Chunk::LENGTH - 1 ? frontChunk == nullptr || frontChunk->getBlock(x, y, 0) == 0 : this->chunk->getBlock(x, y, z + 1) == 0) {
						vertices.push_back(x); vertices.push_back(y); vertices.push_back(z + 1.0f);
						vertices.push_back(x + 1.0f); vertices.push_back(y); vertices.push_back(z + 1.0f);
						vertices.push_back(x); vertices.push_back(y + 1.0f); vertices.push_back(z + 1.0f);

						vertices.push_back(x + 1.0f); vertices.push_back(y + 1.0f); vertices.push_back(z + 1.0f);
						vertices.push_back(x); vertices.push_back(y + 1.0f); vertices.push_back(z + 1.0f);
						vertices.push_back(x + 1.0f); vertices.push_back(y); vertices.push_back(z + 1.0f);

						ChunkMesh::addTexcoords(texcoords, block->front);
						ChunkMesh::addNormals(normals, 0.0f, 0.0f, 1.0f);
					}
					if (z == 0 ? backChunk == nullptr || backChunk->getBlock(x, y, Chunk::LENGTH - 1) == 0 : this->chunk->getBlock(x, y, z - 1) == 0) {
						vertices.push_back(x + 1.0f); vertices.push_back(y); vertices.push_back(z);
						vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
						vertices.push_back(x + 1.0f); vertices.push_back(y + 1.0f); vertices.push_back(z);

						vertices.push_back(x); vertices.push_back(y + 1.0f); vertices.push_back(z);
						vertices.push_back(x + 1.0f); vertices.push_back(y + 1.0f); vertices.push_back(z);
						vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);

						ChunkMesh::addTexcoords(texcoords, block->back);
						ChunkMesh::addNormals(normals, 0.0f, 0.0f, 1.0f);
					}
				}
			}
		}

		this->vertexCount = static_cast<GLsizei>(vertices.size() / 3);

		glGenVertexArrays(1, &this->id);
		this->use();

		this->vboId = ChunkMesh::createVbo(vertices, 0, 3);
		this->tboId = ChunkMesh::createVbo(texcoords, 1, 2);
		this->nboId = ChunkMesh::createVbo(normals, 2, 3);

		BS::Mesh::drop();
	}
	inline void clear() {
		if (this->vertexCount == 0) return;
		glDeleteVertexArrays(1, &this->id);

		glDeleteBuffers(1, &this->vboId);
		glDeleteBuffers(1, &this->tboId);
		glDeleteBuffers(1, &this->nboId);

		this->vertexCount = 0;
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

		shader.setMatrix4("modelMatrix", modelMatrix);
		shader.setMatrix4("mvpMatrix", projectViewMatrix * modelMatrix);
		
		glDrawArrays(GL_TRIANGLES, 0, this->vertexCount);
	}
	void update(const Chunk* rightChunk = nullptr, const Chunk* leftChunk = nullptr, const Chunk* frontChunk = nullptr, const Chunk* backChunk = nullptr, const Chunk* topChunk = nullptr, const Chunk* bottomChunk = nullptr) {
		if (!this->dirty) return;

		this->clear();
		this->create(rightChunk, leftChunk, frontChunk, backChunk, topChunk, bottomChunk);
		
		this->dirty = false;
	}
	void markDirty() {
		this->dirty = true;
	}
};

class ChunkGenerator {
private:
	BS::Timer timer;

	int fps = 0;
	float fpsTimer = 0.0f;
public:
	void run(BS::Window* window) {
		while (window->isRunning()) {
			this->timer.update();

			this->fpsTimer += this->timer.getRealDelta();
			this->fps++;

			BS::Logger::debug("Test Message");
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}
};

struct Camera {
	glm::vec3 position = glm::vec3(), rotation = glm::vec3();

	glm::mat4 getProjectViewMatrix(BS::Window* window) const {
		return MatrixHelper::perspective(window, 90.0f) * MatrixHelper::view(this->position, this->rotation);
	}
};

class MainWindow : public BS::Window {
private:
	BS::Timer timer;
	Camera camera = { camera.position = glm::vec3(84.0f, 72.0f, 222.0f), camera.rotation = glm::vec3(-45.0f, 0.0f, 0.0f) };

	BS::ShaderProgram terrainShader = BS::ShaderProgram("assets/shaders/terrain.vsh", "assets/shaders/terrain.fsh", nullptr);

	static const size_t CHUNKS_X = 12, CHUNKS_Y = 4, CHUNKS_Z = 12;

	Chunk* chunks = new Chunk[CHUNKS_X * CHUNKS_Y * CHUNKS_Z]();
	ChunkMesh* chunkMeshes = new ChunkMesh[CHUNKS_X * CHUNKS_Y * CHUNKS_Z]();

	float fpsTimer = 0.0f;
	int fps = 0;
private:
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

	void createBlob(uint16_t x, uint16_t y, uint16_t z, uint8_t block, int blobSize = 12, bool noisy = false) {		
		for (int ox = -blobSize; ox < blobSize; ox++) {
			for (int oy = -blobSize; oy < blobSize; oy++) {
				for (int oz = -blobSize; oz < blobSize; oz++) {
					if (glm::length(glm::vec3(ox, oy, oz)) <= blobSize - (noisy ? rand() % 10000 / 10000.0f : 0)) {
						this->setBlock(
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
	
	ChunkGenerator chunkGenerator;
	std::thread chunkGeneratorThread;
public:
	MainWindow() : Window(1920, 1080, "MineStorm"), blockTextureAtlas(BlockTextureAtlas::create()) {
		this->disableVSync();

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);

		//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

		for (size_t x = 0; x < CHUNKS_X; x++) {
			for (size_t y = 0; y < CHUNKS_Y; y++) {
				for (size_t z = 0; z < CHUNKS_Z; z++) {
					size_t id = INDEX_FROM_XYZ(x, y, z, CHUNKS_X, CHUNKS_Z);
					
					this->chunks[id].create(glm::ivec3(x, y, z), CHUNKS_Y);
					this->chunkMeshes[id].connect(&this->chunks[id]);
					
					this->chunkMeshes[id].markDirty();
				}
			}
		}

		Blocks::registerEntry(Block::create(BlockFace(glm::ivec2(3, 15)), BlockFace(glm::ivec2(0, 15)), BlockFace(glm::ivec2(2, 15))));
		Blocks::registerEntry(Block::create(BlockFace(glm::ivec2(1, 15))));
		Blocks::registerEntry(Block::create(BlockFace(glm::ivec2(1, 14))));
		Blocks::registerEntry(Block::create(BlockFace(glm::ivec2(2, 15))));
		
		this->chunkGeneratorThread = std::thread(&ChunkGenerator::run, &this->chunkGenerator, this);
		if (this->chunkGeneratorThread.joinable()) this->chunkGeneratorThread.detach();
	}
	~MainWindow() {
		delete[] this->chunks;
	}

	std::vector<float> blobTimers = {};
	std::vector<glm::vec3> blobPositions = {};
	std::vector<glm::vec3> blobVelocities = {};

	float blobSpawnTimer = 0.0f;
	void onUpdate() override {
		this->timer.update();
		this->timer.scale = 0.3f;

		for (int i = 0; i < blobTimers.size(); i++) {
			blobTimers[i] += this->timer.getDelta();
			if (blobTimers[i] >= 0.01f) {
				this->createBlob(blobPositions[i].x, blobPositions[i].y, blobPositions[i].z, 0);
				blobPositions[i] += blobVelocities[i];

				blobVelocities[i].y -= 1;

				this->createBlob(blobPositions[i].x, blobPositions[i].y, blobPositions[i].z, 3);
				blobTimers[i] = 0.0f;

				if (blobPositions[i].y < 0.0f) {
					this->createBlob(blobPositions[i].x, 12, blobPositions[i].z, 0, 32, true);

					blobPositions.erase(blobPositions.begin() + i);
					blobVelocities.erase(blobVelocities.begin() + i);
					blobTimers.erase(blobTimers.begin() + i);
				}
			}
		}

		blobSpawnTimer += this->timer.getDelta();
		if (blobSpawnTimer >= 0.4f) {
			blobSpawnTimer = 0.0f;

			blobTimers.push_back(0.0f);
			blobPositions.push_back(glm::vec3(rand() % (CHUNKS_X * Chunk::WIDTH), 0.0f, rand() % (CHUNKS_Z * Chunk::LENGTH)));

			blobVelocities.push_back(glm::vec3(-(blobPositions[blobPositions.size() - 1].x - CHUNKS_X * Chunk::WIDTH * 0.5f) * 0.07f, 12.0f, -(blobPositions[blobPositions.size() - 1].z - CHUNKS_Z * Chunk::LENGTH * 0.5f) * 0.07f));
		}

		const float distance = 127.0f;
		this->camera.position.x = sin(this->timer.getTime() + glm::pi<float>()) * distance + CHUNKS_X * Chunk::WIDTH * 0.5f;
		this->camera.position.z = -cos(this->timer.getTime() + glm::pi<float>()) * distance + CHUNKS_Z * Chunk::LENGTH * 0.5f;

		this->camera.rotation.y = -glm::degrees(this->timer.getTime());

		this->fpsTimer += this->timer.getRealDelta();
		this->fps++;
		
		if (this->fpsTimer >= 1.0f) {
			this->setTitle(std::string("MineStorm | FPS: " + std::to_string(this->fps)).c_str());
			
			this->fpsTimer = 0.0f;
			this->fps = 0;
		}

		if (this->isMouseButtonPressed(BS::MouseButton::LEFT)) {
			uint16_t x = rand() % 342;
			uint16_t y = rand() % 342;
			uint16_t z = rand() % 342;

			this->createBlob(x, y, z, 1);
		}
		if (this->isMouseButtonPressed(BS::MouseButton::RIGHT)) {
			uint16_t x = rand() % 342;
			uint16_t y = rand() % 342;
			uint16_t z = rand() % 342;

			this->createBlob(x, y, z, 0);
		}

		glm::mat4 projectViewMatrix = camera.getProjectViewMatrix(this);
		
		this->terrainShader.use();
		
		for (size_t x = 0; x < CHUNKS_X; x++) {
			for (size_t y = 0; y < CHUNKS_Y; y++) {
				for (size_t z = 0; z < CHUNKS_Z; z++) {
					size_t id = x + z * CHUNKS_X + y * CHUNKS_X * CHUNKS_Z;

					this->chunkMeshes[id].update(
						static_cast<uint16_t>(x) == CHUNKS_X - 1 ? nullptr : &this->chunks[x + 1 + z * CHUNKS_X + y * CHUNKS_X * CHUNKS_Z],
						x == 0 ? nullptr : &this->chunks[x - 1 + z * CHUNKS_X + y * CHUNKS_X * CHUNKS_Z],
						static_cast<uint16_t>(z) == CHUNKS_Z - 1 ? nullptr : &this->chunks[x + (z + 1) * CHUNKS_X + y * CHUNKS_X * CHUNKS_Z],
						z == 0 ? nullptr : &this->chunks[x + (z - 1) * CHUNKS_X + y * CHUNKS_X * CHUNKS_Z],
						static_cast<uint16_t>(y) == CHUNKS_Y - 1 ? nullptr : &this->chunks[x + z * CHUNKS_X + (y + 1) * CHUNKS_X * CHUNKS_Z],
						y == 0 ? nullptr : &this->chunks[x + z * CHUNKS_X + (y - 1) * CHUNKS_X * CHUNKS_Z]
					);

					this->chunkMeshes[id].use();
					this->chunkMeshes[id].render(this->terrainShader, this->blockTextureAtlas, projectViewMatrix);
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