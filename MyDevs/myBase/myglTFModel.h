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
#include "myVulkan.h"

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
#include "myStructsRT.h"
#include "tiny_gltf.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

#include "nvcluster/nvcluster.h"
#include "nvcluster/nvcluster_storage.hpp"

#define WATCH_AABB 0
#define WATCH_GEOMETRYNODE 1

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

	//extern VkDescriptorSetLayout descriptorSetLayoutImage;
	//extern VkDescriptorSetLayout descriptorSetLayoutUbo;
	//extern VkMemoryPropertyFlags memoryPropertyFlags;
	//extern uint32_t descriptorBindingFlags;

	struct Node;


	typedef struct UniformBufferSet
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkDescriptorBufferInfo descriptor;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		uint64_t deviceAddress = 0;
		void* mapped;
	}UniformBufferSet, MeshUniformBuffer, ModelCombinedBuffer;

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
		uint32_t numVertices = 0;
		MeshUniformBuffer uniformBuffer;

		struct UniformBlock {
			glm::mat4 matrix;
			glm::mat4 jointMatrix[MAX_JOINTS]{};
			//float jointcount{ 0 };
		} uniformBlock;

		/**
		 * "createUniformBuffer" combined
		 */
		Mesh(vks::VulkanDevice* device, glm::mat4 matrix);
		~Mesh();
		void createUniformBuffer(bool hasSkin);
	};

	/*
		glTF skin
	*/
	struct Skin {
		std::string name;
		Node* skeletonRoot = nullptr;
		Node* jointRoot = nullptr; // it can be skeletonRoot or joints[0]
		std::vector<glm::mat4> inverseBindMatrices;
		std::vector<Node*> joints;
		// bool bUpdated = false;
	};

	/*
		glTF node
	*/
	struct Node {
		Node* parent = nullptr;
		uint32_t index;
		std::vector<Node*> children;
		glm::mat4 matrix;
		std::string name;
		Mesh* mesh = nullptr;
		Skin* skin = nullptr;
		int32_t skinIndex = -1;
		int32_t jointNodeIndex = -1; // if -1, not joint node
		int32_t jointIndexInSkin = -1; // if -1, not joint node
		glm::vec3 translation{};
		glm::vec3 scale{ 1.0f };
		glm::quat rotation{};
		glm::mat4 localMatrix();
		glm::mat4 getMatrix();
		void update();

		/**
		 * @param parentMatrix In Initial Call, If this is Identity, jointMatrices represent "To Mesh Local". If this is "ToWorld", jointMatrices represent "To World"
		 */
		void updateJoints(glm::mat4 parentMatrix, std::array<glm::mat4, MAX_JOINTS>& jointMatrices);
		~Node();
	};

	struct JointNode
	{
		JointNode() = delete;
		JointNode(Node* node) : gltfNode(node), nodeIndex(node->index){}
		Node* gltfNode = nullptr; // original node
		std::vector<JointNode*> children;
		glm::mat4 inverseBindMatrix = glm::mat4(1.f);
		uint32_t nodeIndex = -1;
		glm::mat4 finalMatrix = glm::mat4(1.f);
		void update(glm::mat4 parentMatrix)
		{
			glm::mat4 curNodeMatrix = parentMatrix * gltfNode->localMatrix();
			finalMatrix = glm::inverse(gltfNode->localMatrix()) * curNodeMatrix * inverseBindMatrix;
			for (auto& child : children)
				child->update(curNodeMatrix);
		}
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
	struct ActiveAnimation : Animation
	{
		ActiveAnimation(const Animation& rhs)
		{
			name = rhs.name;
			samplers = rhs.samplers;
			channels = rhs.channels;
			start = rhs.start;
			end = rhs.end;
		}
		float accPlayTime = 0.f;
	};

	/*
		glTF default vertex layout with easy Vulkan mapping functions
	*/
	enum class VertexComponent { Position, Normal, UV, Color, Tangent, Joint0, Weight0 };

	typedef struct VERTEX_TYPE
	{
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec4 color;
		glm::vec4 tangent;
	}VertexType, VertexSimple;
	struct VertexSkinning : public VertexType
	{
		glm::vec4 joint0;
		glm::vec4 weight0;
#if CUSTOM_VERTEX
		glm::uvec4 customData4; // [meshID, primitiveIdInMesh, 0, 0]
#endif
	};

	enum FileLoadingFlags {
		None = 0x00000000,
		PreTransformVertices = 0x00000001,
		PreMultiplyVertexColors = 0x00000002,
		FlipY = 0x00000004,
		DontLoadImages = 0x00000008,
		ForceNodesTransformIdentity = 0x000000010, // apply node's transform to vertices while loading
		PrepareTraditionalPipeline = 0x000000020,
		PrepareMeshShaderPipeline = 0x000000040,
		GeometryNodePerPrimitive = 0x000000080, // Original Sascha Style
		GeometryNodePerMesh = 0x000000100, // New Style
		MakeClusters = 0x000000200, // Just make clusters. decide usage for clusterdTriangleBLAS or ClusteredBLAS
		ClusteredTriangleBLAS = (MakeClusters | 0x000000400), // for Clustered Triangle Base Mesh, No CLAS
		ClusteredBLAS = (MakeClusters | 0x000000800), // for CLAS nv Extensions
		CombinedMeshBuffer = 0x000001000, // for make mesh's buffer(joint matrices) combined
	};

	// descriptorset bind num into pipeline
	enum RenderFlags {
		BindImages = 0x00000001,
		RenderOpaqueNodes = 0x00000002,
		RenderAlphaMaskedNodes = 0x00000004,
		RenderAlphaBlendedNodes = 0x00000008
	};

	/**
	 * Model Class for Ray tracing
	 */
	class ModelRT
	{
	public:
		VkDescriptorSetLayout descriptorSetLayoutImage{ VK_NULL_HANDLE };
		uint32_t modelBufferBinding = 0;
		VkDescriptorSetLayout descriptorSetLayoutModel{ VK_NULL_HANDLE };
		static VkMemoryPropertyFlags memoryPropertyFlags;
		static uint32_t descriptorBindingFlags;
	private:
		myglTF::Texture* getTexture(uint32_t index);
		myglTF::Texture emptyTexture;
		void createEmptyTexture(VkQueue transferQueue);
	public:
		vks::VulkanDevice* device;
		VkDescriptorPool descriptorPool;
		typedef struct BUFFER_TAG
		{
			uint32_t count = 0;
			VkBuffer buffer = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			uint64_t deviceAddress = 0;
			VkDeviceSize size = 0;
			VkDescriptorBufferInfo descriptor{};
		}Vertices, Indices, ClusterVertices, ClusterIndices, ClusterBBoxes, Clusters, GeometryNodes, Primitives;
		void CleanBufferMemory(BUFFER_TAG& bufferAndMemory);

#pragma region Buffers
		Vertices vertices{};
		Vertices deformingVertices{}; // for deforming vertex buffer. If skeletal mesh, "vertices" is t-pose
		Indices indices{};
		GeometryNodes geometryNodes{}; // GeometryNode type and the buffer size are already determined at creation.
		Primitives primitives; // for multi blas
		// Used only if model needs only single representing uniform data
		ModelCombinedBuffer representingBuffer{}; // this can be root node buffer or something else(bakedAnimiData,,).
		ModelCombinedBuffer combinedMeshBuffer{}; // combined all mesh's uniform buffer
#pragma endregion Buffers

		struct UniformData
		{
			glm::mat4 matrix; // root model matrix
		}uniformBlock{};

		std::vector<Node*> nodes;
		std::vector<Node*> linearNodes;
		std::unordered_map<Node*, std::array<glm::mat4, MAX_JOINTS>> rootToMatricesMap; // rootJoint -> matrices
		std::vector<Skin*> skins;
		std::vector<JointNode*> jointRoots;
		std::vector<Mesh*> linearMeshes;
		std::vector<Texture> textures;
		std::vector<Material> materials;
		std::vector<Animation> animations;
		std::vector<ActiveAnimation> activeAnimations;
		std::vector<GeometryNodePerPrimitiveRT> geometryNodesPerPrimitive;
		std::vector<GeometryNodePerMeshRT> geometryNodesPerMesh;

		struct Dimensions {
			glm::vec3 min = glm::vec3(FLT_MAX);
			glm::vec3 max = glm::vec3(-FLT_MAX);
			glm::vec3 size;
			glm::vec3 center;
			float radius;
		} dimensions;
#pragma region Cluster
		// cluster build config
		//inline static constexpr uint32_t clusterTrianglesMax	= 64 / 4;
		//inline static constexpr uint32_t clusterVerticesMax		= 64 * 2;
		inline static constexpr uint32_t clusterTrianglesMax = 64 * 2;
		inline static constexpr uint32_t clusterVerticesMax = 64 * 2;
		// struct for geometry node used for both cpu and shader
		std::vector<ClusteredGeometryNodeRT> clusteredGeometryNodes; // per mesh
		// per mesh
		struct PerMeshClustersBuildData // for genetrate clusters
		{
			bool hasSkin = false;
			uint32_t numMeshIndices; // indices count of original mesh
			uint32_t vertexStartOffset = 0;
			uint32_t indexStartOffset = 0;
			std::vector<uint32_t> clusterVerticesCPU; // indices of original vertex array
			std::vector<uint8_t> clusterIndicesCPU; // indexing clusterVerticesCPU
			std::vector<BBox> clusterBBoxesCPU;
			std::vector<ClusterRT> clustersCPU;

			// Buffer & Memory
			ClusterVertices clusterVerticesGPU{};
			ClusterIndices clusterIndicesGPU{};
			ClusterBBoxes clusterBBoxesGPU{};
			Clusters clustersGPU{};

#if WATCH_AABB
			// for test deformation
			std::vector<float> aabbChangeRatios;
#endif
		};
#if WATCH_AABB
		Vertices verticesHostVisible{};
		VertexSkinning* vertexPositionViewer;
#endif

#if WATCH_GEOMETRYNODE
		GeometryNodePerMeshRT* geometryNodeViewer;
#endif

		std::vector<PerMeshClustersBuildData> perMeshClustersBuildDatas;
		Clusters clusters{}; // all scene's clusters
		//uint32_t clusterTrianglesMax = 64;
		/* count array per triangle-counts */
		std::vector<uint32_t> clusterTriangleHistogram;
		/* count array per vertex-counts */
		std::vector<uint32_t> clusterVertexHistogram;
		uint32_t mostFrequentNumOfClusterTriangles = 0u;
		uint32_t mostFrequentNumOfClusterVertices = 0u;
		uint32_t m_clusterTriangleMax = 0u;
		uint32_t m_clusterVertexMax = 0u;
		uint32_t m_perMeshClusterMax = 0u; // max num of clusters per mesh
		uint32_t m_numTotalClusters = 0u;
		std::vector<ClusterRT> tempClusters; // for Total Clusters
		ClusterRT* clusterViewer;

		void initClusters(std::vector<uint32_t>& originalIndices, const std::vector<glm::vec3>& vertexPositions, PerMeshClustersBuildData& perMeshClustersBuildData, const uint32_t firstIndexGlobalOffset);
		void updateClustersAABB(VkQueue transferQueue);
#pragma endregion Cluster
		void updateGeometryNode(float* blasBuildTimes, uint32_t numBLASes);

		// bake animation (test)
		void bakeAnimations();
		bool isBakedAnimation = false;
		float animMaxFrame = 0;
		float animMaxTime = 0;
		float samplingRate = FLT_MAX; // min second interval between samples
		uint32_t animMaxFPS = 0;
		struct BakedAnimation // num : skinnedMesh * maxTime
		{
			glm::mat4 jointMats[64 * 4]{}; // num: (animFps * animDuration) * allAnims
		};
		std::vector<BakedAnimation> bakedAnimations;
		std::vector<UniformBufferSet> bakedUniformBuffers;

		// scene
		BBox sceneBBox;

		bool metallicRoughnessWorkflow = true;
		bool buffersBound = false;
		bool preTransform = false;
		bool forceOneInstance = true;
		std::string path;

		ModelRT() {};
		~ModelRT();
		void loadNode(myglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, std::vector<uint32_t>& indexBuffer, std::vector<VertexType*>& vertices, float globalscale);
		void loadSkins(tinygltf::Model& gltfModel);
		void loadImages(tinygltf::Model& gltfModel, vks::VulkanDevice* device, VkQueue transferQueue);
		void loadMaterials(tinygltf::Model& gltfModel);
		void loadAnimations(tinygltf::Model& gltfModel);
		void loadFromFile(std::string filename, vks::VulkanDevice* device, VkQueue transferQueue, uint32_t fileLoadingFlags = myglTF::FileLoadingFlags::None, float scale = 1.0f);
		void bindBuffers(VkCommandBuffer commandBuffer);
		void getNodeDimensions(Node* node, glm::vec3& min, glm::vec3& max);
		void getSceneDimensions();
		/**
		 * @note after all animations updated, updateNodeTransfors() must be called
		 */
		void updateAnimation(uint32_t index, float time);
		void updateNodeTransforms(); // Legacy - Sascha's Style (BAD)
		void updateJoints();
		void updateNodeTransforms(Node* pNode);
		void updateCombinedMeshBuffer();
		Node* findNode(Node* parent, uint32_t index);
		Node* nodeFromIndex(uint32_t index);
		void prepareNodeDescriptor(myglTF::Node* node, VkDescriptorSetLayout descriptorSetLayout);
	};

	inline void ModelRT::CleanBufferMemory(BUFFER_TAG& bufferAndMemory)
	{
		if (bufferAndMemory.buffer || bufferAndMemory.memory)
		{
			vkDestroyBuffer(device->logicalDevice, bufferAndMemory.buffer, nullptr);
			vkFreeMemory(device->logicalDevice, bufferAndMemory.memory, nullptr);
		}
	}
}
