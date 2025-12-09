#pragma once
#include <myStructs.h>
#include <vector>
#include <string>
#include <iostream>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "myIncludesCPUGPU.h"
#include "tiny_gltf.h"
#include "VulkanDevice.h"


/**
 * Simple gltf Loader, only loading vertices and indices
 */
namespace myglTF
{
	class MySimpleGltfLoader
	{
	public: // TypeDefs
		class Model;
		struct Node;

		typedef struct VERTEX_TYPE
		{
			glm::vec3 pos;
			glm::vec3 normal;
			glm::vec2 uv;
			glm::vec4 color;
			glm::vec4 joint0;
			glm::vec4 weight0;
#if CUSTOM_VERTEX
			glm::vec4 customData4; // [meshID, primitiveIdInMesh, 0, 0]
#endif
		}VertexType, VertexSimple;
		struct GltfPrimitive
		{
			uint32_t firstIndex;
			uint32_t indexCount;
			uint32_t firstVertex;
			uint32_t vertexCount;
		};
		struct Mesh
		{
			Mesh() = default;
			virtual ~Mesh() = default;
			std::vector<GltfPrimitive*> primitives;
			uint32_t numVertices = 0;
			glm::mat4 matrix;

			BufferSet meshConstantBuffer{};
			static constexpr size_t CONSTANT_DATA_SIZE = sizeof(glm::mat4);
		};
		class SkeletalMesh : public Mesh
		{
		public:
			SkeletalMesh() = default;
			~SkeletalMesh() override = default;

			glm::mat4 jointMatrix[MAX_JOINTS]{};

			static constexpr size_t CONSTANT_DATA_SIZE = sizeof(glm::mat4) + sizeof(glm::mat4) * MAX_JOINTS;
		};
		struct Skin {
			std::string name;
			Node* skeletonRoot = nullptr;
			Node* jointRoot = nullptr; // it can be skeletonRoot or joints[0]
			std::vector<glm::mat4> inverseBindMatrices;
			std::vector<Node*> joints;
		};
		struct Node
		{
			Node* pParent;
			Mesh* pMesh;
			Skin* skin;
			uint32_t index;
			std::vector<Node*> children;
			glm::mat4 matrix;
			std::string name;
			int32_t skinIndex = -1;
			int32_t jointNodeIndex = -1; // if -1, not joint node
			int32_t jointIndexInSkin = -1; // if -1, not joint node
			glm::vec3 translation{};
			glm::vec3 scale{ 1.0f };
			glm::quat rotation{};
			glm::mat4 localMatrix()
			{
				return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix;
			}
			glm::mat4 getMatrix()
			{
				glm::mat4 m = localMatrix();
				Node* p = pParent;
				while (p) {
					m = p->localMatrix() * m;
					p = p->pParent;
				}
				return m;
			}
			void update();

			/**
			 * @param parentMatrix In Initial Call, If this is Identity, jointMatrices represent "To Mesh Local". If this is "ToWorld", jointMatrices represent "To World"
			 */
			void UpdateJoints(glm::mat4 parentMatrix, std::array<glm::mat4, MAX_JOINTS>& jointMatrices);
			~Node();
		};
		struct JointNode
		{
			JointNode() = delete;
			JointNode(Node* node) : gltfNode(node), nodeIndex(node->index) {}
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
		struct AnimationChannel {
			enum PathType { TRANSLATION, ROTATION, SCALE };
			PathType path;
			Node* node;
			uint32_t samplerIndex;
		};
		struct AnimationSampler {
			enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
			InterpolationType interpolation;
			std::vector<float> inputs;
			std::vector<glm::vec4> outputsVec4;
		};
		struct Animation {
			std::string name;
			std::vector<AnimationSampler> samplers;
			std::vector<AnimationChannel> channels;
			float start = std::numeric_limits<float>::max();
			float end = std::numeric_limits<float>::min();
		};

		class Animator
		{
		public:
			void UpdateAnimation(float deltaTime);
		public:
			Animation* m_pTargetAnim = nullptr;
			float m_accPlayTime = 0.f;
			float m_speed = 0.3f;
		};

	public:
		MySimpleGltfLoader() = default;
		~MySimpleGltfLoader() {}

	public: // Funcs
		static MySimpleGltfLoader::Node* NodeFromIndex(uint32_t index, Node** pRoots, uint32_t numRoots);
		static MySimpleGltfLoader::Node* FindNode_Recur(Node* pParent, uint32_t index);
	public:
		void LoadFromFile(vks::VulkanDevice* pVksDevice, Model* pModel, std::string filename);
	public: // Members
		Model* m_pModel = nullptr; // Temp pointer while loading. Not Instantiate in this class

	private:
		void LoadNode(Node* pParent, const tinygltf::Node& tinygltfNode, bool hasSkeletalMesh, uint32_t nodeIndex, const tinygltf::Model& tinygltfModel, std::vector<uint32_t>& indices, std::vector<VertexSimple>& vertices);
		void LoadSkins(const tinygltf::Model& tinygltfModel);
		void LoadAnimations(const tinygltf::Model& tinygltfModel);
		vks::VulkanDevice* m_pVksDevice = nullptr;
	};


	class MySimpleGltfLoader::Model
	{
	public:
		MySimpleGltfLoader::Model() = default;
		~Model()
		{
			for (auto& pAnimator : m_pAnimators)
			{
				if (pAnimator)
				{
					delete pAnimator; pAnimator = nullptr;
				}
			}
		}
	public: // Funcs
		void UpdateJoints();
		void UpdateNodeTransforms(Node* pNode);
		void InitAnimations(uint32_t numAnims)
		{
			m_pAnimators.resize(numAnims, new Animator());
			for (uint32_t i = 0; i < numAnims; ++i)
			{
				m_pAnimators[i]->m_pTargetAnim = &m_animations[i];
			}
		}
		void UpdateAnimation(float deltaTime)
		{
			for (auto& animator : m_pAnimators)
			{
				animator->UpdateAnimation(deltaTime);
			}
			UpdateJoints();
			for (auto& root : m_NodeRoots)
			{
				UpdateNodeTransforms(root);
			}
		}
	public: // CPU Resourecs
		std::vector<VertexSimple> m_vertices;
		std::vector<glm::vec3> m_vPositions;
		std::vector<uint32_t> m_indices;
		std::vector<Mesh*> m_linearMeshes;
		std::vector<Skin*> m_skins;
		std::vector<Animation> m_animations;
		std::vector<Node*> m_NodeRoots;
		std::vector<Node*> m_linearNodes;
		std::unordered_map<Node*, std::array<glm::mat4, MAX_JOINTS>> m_rootToMatricesMap; // rootJoint -> matrices
		BBox m_bbox{ glm::vec3{FLT_MAX}, glm::vec3{-FLT_MAX} };
	public: //VkResourecs
		BindLayoutSet m_bindLayoutSet{};
		BufferSet m_vertexBuffer{};
		BufferSet m_deformingVertexBuffer{};
		BufferSet m_indexBuffer{};
	public:
		std::vector<Animator*> m_pAnimators;
		bool m_isSkeletalMesh = true;
	private:
	};

	void Create_ModelDeviceResources(VkDevice device, MySimpleGltfLoader::Model& model);

}