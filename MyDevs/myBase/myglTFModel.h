/*
* Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
*
* Copyright (C) 2018-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

/*
 * Note that this isn't a complete glTF loader and not all features of the glTF 2.0 spec are supported
 * For details on how glTF 2.0 works, see the official spec at https://github.com/KhronosGroup/glTF/tree/master/specification/2.0
 *
 * If you are looking for a complete glTF implementation, check out https://github.com/SaschaWillems/Vulkan-glTF-PBR/
 */
#pragma once
#include "myIncludes.h"
//#include "VulkanglTFModel.h"

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include "vulkan/vulkan.h"
#include "VulkanDevice.h"

#include <ktx.h>
#include <ktxvulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif
#include "tiny_gltf.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

struct meshopt_Meshlet;
namespace myglTF
{
	inline bool loadImageDataFunc(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height, const unsigned char* bytes, int size, void* userData)
	{
		// KTX files will be handled by our own code
		if (image->uri.find_last_of(".") != std::string::npos) {
			if (image->uri.substr(image->uri.find_last_of(".") + 1) == "ktx") {
				return true;
			}
		}

		return tinygltf::LoadImageData(image, imageIndex, error, warning, req_width, req_height, bytes, size, userData);
	}

	inline bool loadImageDataFuncEmpty(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height, const unsigned char* bytes, int size, void* userData)
	{
		// This function will be used for samples that don't require images to be loaded
		return true;
	}

	enum DescriptorBindingFlags {
		ImageBaseColor = 0x00000001,
		ImageNormalMap = 0x00000002
	};

	extern VkDescriptorSetLayout descriptorSetLayoutImage;
	extern VkDescriptorSetLayout descriptorSetLayoutUbo;
	extern VkMemoryPropertyFlags memoryPropertyFlags;
	extern uint32_t descriptorBindingFlags;

	struct Node;

	/*
		glTF texture loading class
	*/
	struct Texture {
		vks::VulkanDevice* device = nullptr;
		VkImage image;
		VkImageLayout imageLayout;
		VkDeviceMemory deviceMemory;
		VkImageView view;
		uint32_t width, height;
		uint32_t mipLevels;
		uint32_t layerCount;
		VkDescriptorImageInfo descriptor;
		VkSampler sampler;
		uint32_t index;
		void updateDescriptor();
		void destroy();
		void fromglTfImage(tinygltf::Image& gltfimage, std::string path, vks::VulkanDevice* device, VkQueue copyQueue);
	};

	/*
		glTF material class
	*/
	struct Material {
		vks::VulkanDevice* device = nullptr;
		enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
		AlphaMode alphaMode = ALPHAMODE_OPAQUE;
		float alphaCutoff = 1.0f;
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		myglTF::Texture* baseColorTexture = nullptr;
		myglTF::Texture* metallicRoughnessTexture = nullptr;
		myglTF::Texture* normalTexture = nullptr;
		myglTF::Texture* occlusionTexture = nullptr;
		myglTF::Texture* emissiveTexture = nullptr;

		myglTF::Texture* specularGlossinessTexture;
		myglTF::Texture* diffuseTexture;

		VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
		VkDescriptorSet meshShaderDescriptorSet{ VK_NULL_HANDLE };
		VkPipeline traditionalPipeline{ VK_NULL_HANDLE };
		VkPipeline meshShaderPipeline{ VK_NULL_HANDLE };

		Material(vks::VulkanDevice* device) : device(device) {};
		~Material();
		void createDescriptorSet(VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, uint32_t descriptorBindingFlags);
	};

	/*
		glTF primitive
	*/
	struct Primitive {
		uint32_t firstIndex;
		uint32_t indexCount;
		uint32_t firstVertex;
		uint32_t vertexCount;
		Material& material;

		struct Dimensions {
			glm::vec3 min = glm::vec3(FLT_MAX);
			glm::vec3 max = glm::vec3(-FLT_MAX);
			glm::vec3 size;
			glm::vec3 center;
			float radius;
		} dimensions;

		void setDimensions(glm::vec3 min, glm::vec3 max);
		Primitive(uint32_t firstIndex, uint32_t indexCount, Material& material) : firstIndex(firstIndex), indexCount(indexCount), material(material) {};
	};

	/*
		glTF mesh
	*/
	struct Mesh {
		vks::VulkanDevice* device;

		std::vector<Primitive*> primitives;
		std::string name;

		struct UniformBuffer {
			VkBuffer buffer;
			VkDeviceMemory memory;
			VkDescriptorBufferInfo descriptor;
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
			void* mapped;
		} uniformBuffer;

		struct UniformBlock {
			glm::mat4 matrix;
			glm::mat4 jointMatrix[64]{};
			float jointcount{ 0 };
		} uniformBlock;

		Mesh(vks::VulkanDevice* device, glm::mat4 matrix, bool hasSkin = false);
		~Mesh();
	};

	/*
		glTF skin
	*/
	struct Skin {
		std::string name;
		Node* skeletonRoot = nullptr;
		std::vector<glm::mat4> inverseBindMatrices;
		std::vector<Node*> joints;
	};

	/*
		glTF node
	*/
	struct Node {
		Node* parent;
		uint32_t index;
		std::vector<Node*> children;
		glm::mat4 matrix;
		std::string name;
		Mesh* mesh;
		Skin* skin;
		int32_t skinIndex = -1;
		glm::vec3 translation{};
		glm::vec3 scale{ 1.0f };
		glm::quat rotation{};
		glm::mat4 localMatrix();
		glm::mat4 getMatrix();
		void update();
		~Node();
	};

	/*
		glTF animation channel
	*/
	struct AnimationChannel {
		enum PathType { TRANSLATION, ROTATION, SCALE };
		PathType path;
		Node* node;
		uint32_t samplerIndex;
	};

	/*
		glTF animation sampler
	*/
	struct AnimationSampler {
		enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
		InterpolationType interpolation;
		std::vector<float> inputs;
		std::vector<glm::vec4> outputsVec4;
	};

	/*
		glTF animation
	*/
	struct Animation {
		std::string name;
		std::vector<AnimationSampler> samplers;
		std::vector<AnimationChannel> channels;
		float start = std::numeric_limits<float>::max();
		float end = std::numeric_limits<float>::min();
	};

	/*
		glTF default vertex layout with easy Vulkan mapping functions
	*/
	enum class VertexComponent { Position, Normal, UV, Color, Tangent, Joint0, Weight0 };

	typedef struct VERTEX_TYPE
	{
		alignas(16)glm::vec3 pos;
		alignas(16)glm::vec3 normal;
		alignas(16)glm::vec2 uv;
		alignas(16)glm::vec4 color;
		alignas(16)glm::vec4 tangent;
	}VertexType, VertexSimple;
	struct VertexSkinning : public VertexType
	{
		alignas(16)glm::vec4 joint0;
		alignas(16)glm::vec4 weight0;
	};

	enum FileLoadingFlags {
		None = 0x00000000,
		PreTransformVertices = 0x00000001,
		PreMultiplyVertexColors = 0x00000002,
		FlipY = 0x00000004,
		DontLoadImages = 0x00000008,
		PrepareTraditionalPipeline = 0x000000010,
		PrepareMeshShaderPipeline = 0x000000020,
	};

	// descriptorset bind num into pipeline
	enum RenderFlags {
		BindImages = 0x00000001,
		RenderOpaqueNodes = 0x00000002,
		RenderAlphaMaskedNodes = 0x00000004,
		RenderAlphaBlendedNodes = 0x00000008
	};

	/*
		glTF model loading and rendering class
	*/
	class Model {
	public:
		VkDescriptorSetLayout descriptorSetLayoutImage {VK_NULL_HANDLE};
		VkDescriptorSetLayout descriptorSetLayoutUbo{ VK_NULL_HANDLE };
		VkDescriptorSetLayout descriptorSetLayoutMeshShader{ VK_NULL_HANDLE };
		static VkMemoryPropertyFlags memoryPropertyFlags;
		static uint32_t descriptorBindingFlags;
	private:
		myglTF::Texture* getTexture(uint32_t index);
		myglTF::Texture emptyTexture;
		void createEmptyTexture(VkQueue transferQueue);
		/**
		 * @param outMeshletVertices: Meshlet::vertex == Index from OriginalVertexBuffer
		 * @param outMeshletPackedTriangles: single uint32 contains 3 indices(triangle)
		 * @param pOutMeshlets
		 * @param outNumMeshlets
		 */
		void generateMeshlets(const std::vector<VertexType*>& originalVertices, const std::vector<uint32_t>& originalIndices, std::vector<uint32_t>&
		                      outMeshletVertices, std::vector<uint32_t>& outMeshletPackedTriangles, meshopt_Meshlet** pOutMeshlets, uint32_t&
		                      outNumMeshlets);
	public:
		vks::VulkanDevice* device;
		VkDescriptorPool descriptorPool;
		typedef struct PRIMITIVE_TAG
		{
			int count;
			VkBuffer buffer;
			VkDeviceMemory memory;
		}Vertices, Indices, MeshletVertices, MeshletIndices, Meshlets;
		Vertices vertices;
		Indices indices;
#pragma region MeshShader
		Meshlets meshlets;
		MeshletVertices meshletVertices;
		MeshletIndices meshletIndices;
		VkDescriptorBufferInfo vertexBufferDescriptor; // for Original vertex, used only for mesh shader
		VkDescriptorBufferInfo meshletsDescriptor;
		VkDescriptorBufferInfo meshletVerticesDescriptor;
		VkDescriptorBufferInfo meshletIndicesDescriptor;
		VkDescriptorSet meshShaderDescriptorSet{ VK_NULL_HANDLE };
#pragma endregion MeshShader

		std::vector<Node*> nodes;
		std::vector<Node*> linearNodes;

		std::vector<Skin*> skins;

		std::vector<Texture> textures;
		std::vector<Material> materials;
		std::vector<Animation> animations;

		struct Dimensions {
			glm::vec3 min = glm::vec3(FLT_MAX);
			glm::vec3 max = glm::vec3(-FLT_MAX);
			glm::vec3 size;
			glm::vec3 center;
			float radius;
		} dimensions;

		bool metallicRoughnessWorkflow = true;
		bool buffersBound = false;
		std::string path;

		Model() {};
		~Model();
		void loadNode(myglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, std::vector<uint32_t>& indexBuffer, std::vector<VertexType*>& vertices, float globalscale);
		void loadSkins(tinygltf::Model& gltfModel);
		void loadImages(tinygltf::Model& gltfModel, vks::VulkanDevice* device, VkQueue transferQueue);
		void loadMaterials(tinygltf::Model& gltfModel);
		void loadAnimations(tinygltf::Model& gltfModel);
		void loadFromFile(std::string filename, vks::VulkanDevice* device, VkQueue transferQueue, uint32_t fileLoadingFlags = myglTF::FileLoadingFlags::None, float scale = 1.0f);
		void bindBuffers(VkCommandBuffer commandBuffer);
		void drawNode(Node* node, VkCommandBuffer commandBuffer, uint32_t renderFlags = 0, VkPipelineLayout pipelineLayout = VK_NULL_HANDLE, uint32_t bindImageSet = 1, PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT = nullptr);
		void draw(VkCommandBuffer commandBuffer, uint32_t renderFlags = 0, VkPipelineLayout pipelineLayout = VK_NULL_HANDLE, uint32_t bindImageSet = 1, PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT = nullptr);
		void getNodeDimensions(Node* node, glm::vec3& min, glm::vec3& max);
		void getSceneDimensions();
		void updateAnimation(uint32_t index, float time);
		Node* findNode(Node* parent, uint32_t index);
		Node* nodeFromIndex(uint32_t index);
		void prepareNodeDescriptor(myglTF::Node* node, VkDescriptorSetLayout descriptorSetLayout);
	};
}
