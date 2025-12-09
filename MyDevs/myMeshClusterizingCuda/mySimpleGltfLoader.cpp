/**
 * glTF loading code with tinygltf is largely based on Sascha's implementation. (https://github.com/SaschaWillems/Vulkan)
 * 
 */

// Define TINYGLTF Options Before Include tinygltf.h
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "mySimpleGltfLoader.h"

#include "VulkanTools.h"

namespace myglTF
{
	void MySimpleGltfLoader::LoadFromFile(vks::VulkanDevice* pVksDevice, Model* pModel, std::string filename)
	{
		m_pVksDevice = pVksDevice;
		m_pModel = pModel;
		tinygltf::Model tinygltfModel;
		tinygltf::TinyGLTF gltfContext;

		size_t pos = filename.find_last_of('/');

		std::string error, warning;

		bool fileLoaded = gltfContext.LoadASCIIFromFile(&tinygltfModel, &error, &warning, filename);
		if (!fileLoaded)
		{
			std::cerr << "gltf load failed!\n";
			return;
		}
		bool hasSkeletalMesh = tinygltfModel.skins.size() > 0;
		pModel->m_isSkeletalMesh = hasSkeletalMesh;
		const tinygltf::Scene& scene = tinygltfModel.scenes[tinygltfModel.defaultScene > -1 ? tinygltfModel.defaultScene : 0];
		for (size_t i = 0; i < scene.nodes.size(); ++i)
		{
			const tinygltf::Node tinygltfNode = tinygltfModel.nodes[scene.nodes[i]];
			LoadNode(nullptr, tinygltfNode, hasSkeletalMesh, scene.nodes[i], tinygltfModel, m_pModel->m_indices, m_pModel->m_vertices);
		}


		// This should be done outside
		{
			// Create Mesh Buffer
			VkDeviceSize bufferSize = pModel->m_isSkeletalMesh ? myglTF::MySimpleGltfLoader::SkeletalMesh::CONSTANT_DATA_SIZE : myglTF::MySimpleGltfLoader::Mesh::CONSTANT_DATA_SIZE;
			for (auto& pMesh : pModel->m_linearMeshes)
			{
				BufferSet& dstBuffer = pMesh->meshConstantBuffer;
				pVksDevice->CreateBuffer_HostVisible(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					bufferSize, &dstBuffer.vkBuffer, &dstBuffer.vkMemory, true, nullptr, &dstBuffer.mapped);
				dstBuffer.bufferSize = bufferSize;
				dstBuffer.descriptor = { dstBuffer.vkBuffer, 0, dstBuffer.bufferSize };
			}
		}

		if (hasSkeletalMesh)
		{
			LoadSkins(tinygltfModel);

			// Assign skins
			for (const auto& node : m_pModel->m_linearNodes)
			{
				if (node->skinIndex > -1)
				{
					node->skin = m_pModel->m_skins[node->skinIndex];
				}
			}

			m_pModel->UpdateJoints();
			for (auto& root : m_pModel->m_NodeRoots)
			{
				m_pModel->UpdateNodeTransforms(root);
			}

			if (tinygltfModel.animations.size() > 0)
			{
				LoadAnimations(tinygltfModel);
			}
			//m_pModel->InitAnimations(m_pModel->m_animations.size());
			m_pModel->InitAnimations(1);
		}


		uint32_t numVertices = m_pModel->m_vertices.size();
		m_pModel->m_vPositions.resize(numVertices);
		for (uint32_t i = 0; i < numVertices; ++i)
		{
			memcpy(&m_pModel->m_vPositions[i], &m_pModel->m_vertices[i].pos, sizeof(glm::vec3));
		}

		// Compute BBox
		BBox bbox{ glm::vec3{FLT_MAX}, glm::vec3{-FLT_MAX} };
		for (auto& v : m_pModel->m_vPositions)
		{
			bbox.min = glm::min(bbox.min, v);
			bbox.max = glm::max(bbox.max, v);
		}
		m_pModel->m_bbox = bbox;

		m_pModel = nullptr;
	}

	void MySimpleGltfLoader::LoadNode(Node* pParent, const tinygltf::Node& tinygltfNode, bool hasSkeletalMesh,
	                                  uint32_t nodeIndex, const tinygltf::Model& tinygltfModel, std::vector<uint32_t>& indices, std::vector<VertexSimple>& vertices)
	{
		Node* newNode = new Node();
		newNode->pParent = pParent;
		newNode->pMesh = nullptr;
		newNode->index = nodeIndex;
		newNode->skinIndex = tinygltfNode.skin;
		newNode->matrix = glm::mat4(1.f);

		uint32_t numMeshVertices = 0;
		// Generate local node matrix
		glm::vec3 translation = glm::vec3(0.0f);
		glm::mat4 rotation = glm::mat4(1.0f);
		glm::vec3 scale = glm::vec3(1.0f);
		if (tinygltfNode.translation.size() == 3)
		{
			translation = glm::make_vec3(tinygltfNode.translation.data());
		}
		if (tinygltfNode.rotation.size() == 4)
		{
			glm::quat q = glm::make_quat(tinygltfNode.rotation.data());
		}
		if (tinygltfNode.scale.size() == 3)
		{
			scale = glm::make_vec3(tinygltfNode.scale.data());
		}
		if (tinygltfNode.matrix.size() == 16)
		{
			newNode->matrix = glm::make_mat4x4(tinygltfNode.matrix.data());
		}

		// Node with children
		if (tinygltfNode.children.size() > 0)
		{
			for (size_t i = 0; i < tinygltfNode.children.size(); i++)
			{
				LoadNode(newNode, tinygltfModel.nodes[tinygltfNode.children[i]], hasSkeletalMesh, tinygltfNode.children[i], tinygltfModel, indices, vertices);
			}
		}

		// Node contains mesh data
		if (tinygltfNode.mesh > -1) {
			static uint32_t meshID = 0;
			const tinygltf::Mesh mesh = tinygltfModel.meshes[tinygltfNode.mesh];
			bool hasSkin = hasSkeletalMesh;
			Mesh* newMesh = hasSkin ? new SkeletalMesh() : new Mesh();
			for (size_t j = 0; j < mesh.primitives.size(); j++) {
				const tinygltf::Primitive& primitive = mesh.primitives[j];
				if (primitive.indices < 0) {
					continue;
				}
				uint32_t indexStart = static_cast<uint32_t>(indices.size());
				uint32_t vertexStart = static_cast<uint32_t>(vertices.size());
				uint32_t indexCount = 0;
				uint32_t vertexCount = 0;
				glm::vec3 posMin{};
				glm::vec3 posMax{};
				// Vertices
				{
					const float* bufferPos = nullptr;
					const float* bufferNormals = nullptr;
					const float* bufferTexCoords = nullptr;
					const float* bufferColors = nullptr;
					const float* bufferTangents = nullptr;
					uint32_t numColorComponents;
					const uint16_t* bufferJoints = nullptr;
					const float* bufferWeights = nullptr;

					// Position attribute is required
					assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

					const tinygltf::Accessor& posAccessor = tinygltfModel.accessors[primitive.attributes.find("POSITION")->second];
					const tinygltf::BufferView& posView = tinygltfModel.bufferViews[posAccessor.bufferView];
					bufferPos = reinterpret_cast<const float*>(&(tinygltfModel.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
					posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
					posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

					if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
						const tinygltf::Accessor& normAccessor = tinygltfModel.accessors[primitive.attributes.find("NORMAL")->second];
						const tinygltf::BufferView& normView = tinygltfModel.bufferViews[normAccessor.bufferView];
						bufferNormals = reinterpret_cast<const float*>(&(tinygltfModel.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
					}

					if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
						const tinygltf::Accessor& uvAccessor = tinygltfModel.accessors[primitive.attributes.find("TEXCOORD_0")->second];
						const tinygltf::BufferView& uvView = tinygltfModel.bufferViews[uvAccessor.bufferView];
						bufferTexCoords = reinterpret_cast<const float*>(&(tinygltfModel.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
					}

					if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
					{
						const tinygltf::Accessor& colorAccessor = tinygltfModel.accessors[primitive.attributes.find("COLOR_0")->second];
						const tinygltf::BufferView& colorView = tinygltfModel.bufferViews[colorAccessor.bufferView];
						// Color buffer are either of type vec3 or vec4
						numColorComponents = colorAccessor.type == TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 ? 3 : 4;
						bufferColors = reinterpret_cast<const float*>(&(tinygltfModel.buffers[colorView.buffer].data[colorAccessor.byteOffset + colorView.byteOffset]));
					}

					if (primitive.attributes.find("TANGENT") != primitive.attributes.end())
					{
						const tinygltf::Accessor& tangentAccessor = tinygltfModel.accessors[primitive.attributes.find("TANGENT")->second];
						const tinygltf::BufferView& tangentView = tinygltfModel.bufferViews[tangentAccessor.bufferView];
						bufferTangents = reinterpret_cast<const float*>(&(tinygltfModel.buffers[tangentView.buffer].data[tangentAccessor.byteOffset + tangentView.byteOffset]));
					}

					// Skinning
					// Joints
					if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
						const tinygltf::Accessor& jointAccessor = tinygltfModel.accessors[primitive.attributes.find("JOINTS_0")->second];
						const tinygltf::BufferView& jointView = tinygltfModel.bufferViews[jointAccessor.bufferView];
						bufferJoints = reinterpret_cast<const uint16_t*>(&(tinygltfModel.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]));
					}

					if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
						const tinygltf::Accessor& uvAccessor = tinygltfModel.accessors[primitive.attributes.find("WEIGHTS_0")->second];
						const tinygltf::BufferView& uvView = tinygltfModel.bufferViews[uvAccessor.bufferView];
						bufferWeights = reinterpret_cast<const float*>(&(tinygltfModel.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
					}

					hasSkin |= (bufferJoints && bufferWeights);

					vertexCount = static_cast<uint32_t>(posAccessor.count);

					for (size_t v = 0; v < posAccessor.count; v++) {
						/*
						 * if skin:VertexSkiniing, else: VertexSimple
						 * allocated in here, released in "loadfromfile()"
						 * pushed into param::vertexBuffer
						 */
						VertexSimple vert{};

						vert.pos = glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
						//if (bool preTransform) // apply node's transform to vertices while loading
						//{
						//	vert->pos = newNode->getMatrix() * glm::vec4(vert->pos, 1.f);
						//}

						vert.normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
						vert.uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
						if (bufferColors) {
							switch (numColorComponents) {
							case 3:
								vert.color = glm::vec4(glm::make_vec3(&bufferColors[v * 3]), 1.0f);
								break;
							case 4:
								vert.color = glm::make_vec4(&bufferColors[v * 4]);
								break;
							}
						}
						else {
							vert.color = glm::vec4(1.0f);
						}
						//vert->tangent = bufferTangents ? glm::vec4(glm::make_vec4(&bufferTangents[v * 4])) : glm::vec4(0.0f);
						if (hasSkin)
						{
							//static_cast<VertexSkinning*>(vert)->joint0 = glm::vec4(glm::make_vec4(&bufferJoints[v * 4]));
							uint8_t* ptr = (uint8_t*)bufferJoints;
							(vert).joint0 = glm::vec4(glm::make_vec4(&ptr[v * 4]));
							(vert).weight0 = glm::vec4(glm::make_vec4(&bufferWeights[v * 4]));
#if CUSTOM_VERTEX
							(vert).customData4.x = meshID;
#endif
						}
						vertices.push_back(vert);
					}
				}
				// Indices
				{
					const tinygltf::Accessor& accessor = tinygltfModel.accessors[primitive.indices];
					const tinygltf::BufferView& bufferView = tinygltfModel.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = tinygltfModel.buffers[bufferView.buffer];

					indexCount = static_cast<uint32_t>(accessor.count);

					switch (accessor.componentType) {
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
						uint32_t* buf = new uint32_t[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
						for (size_t index = 0; index < accessor.count; index++) {
							indices.push_back(buf[index] + vertexStart);
						}
						delete[] buf;
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
						uint16_t* buf = new uint16_t[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
						for (size_t index = 0; index < accessor.count; index++) {
							indices.push_back(buf[index] + vertexStart);
						}
						delete[] buf;
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
						uint8_t* buf = new uint8_t[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
						for (size_t index = 0; index < accessor.count; index++) {
							indices.push_back(buf[index] + vertexStart);
						}
						delete[] buf;
						break;
					}
					default:
						std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
						return;
					}
				}
				GltfPrimitive* newPrimitive = new GltfPrimitive{};
				newPrimitive->firstVertex = vertexStart;
				newPrimitive->vertexCount = vertexCount;
				newMesh->primitives.push_back(newPrimitive);
				numMeshVertices += vertexCount;
			}
			++meshID;
			newMesh->numVertices = numMeshVertices;
			newNode->pMesh = newMesh;

			{
				VkDeviceSize blockSize = hasSkin ? SkeletalMesh::CONSTANT_DATA_SIZE : Mesh::CONSTANT_DATA_SIZE;
				m_pVksDevice->CreateBuffer_HostVisible(
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					blockSize,
					&newMesh->meshConstantBuffer.vkBuffer,
					&newMesh->meshConstantBuffer.vkMemory,
					true,
					nullptr,
					&newMesh->meshConstantBuffer.mapped);

				newMesh->meshConstantBuffer.descriptor = { newMesh->meshConstantBuffer.vkBuffer, 0, blockSize };
			}

			m_pModel->m_linearMeshes.push_back(newMesh);
		}
		if (pParent) {
			pParent->children.push_back(newNode);
		}
		else {
			m_pModel->m_NodeRoots.push_back(newNode);
		}
		m_pModel->m_linearNodes.push_back(newNode);
	}

	void MySimpleGltfLoader::LoadSkins(const tinygltf::Model& tinygltfModel)
	{
		Node** pRoots = m_pModel->m_NodeRoots.data();
		uint32_t numRoots = m_pModel->m_NodeRoots.size();
		for (const tinygltf::Skin& source : tinygltfModel.skins) {
			Skin* newSkin = new Skin{};
			newSkin->name = source.name;

			// Find skeleton root node
			if (source.skeleton > -1) {
				newSkin->skeletonRoot = NodeFromIndex(source.skeleton, pRoots, numRoots);
				newSkin->jointRoot = newSkin->skeletonRoot;
			}
			else // assume skin's first joint is root joint
				newSkin->jointRoot = NodeFromIndex(source.joints[0], pRoots, numRoots);

			m_pModel->m_rootToMatricesMap.emplace(newSkin->jointRoot, std::array<glm::mat4, MAX_JOINTS>{});

			// Find joint nodes
			int32_t jointIndexInSkin = 0;
			for (int jointIndex : source.joints) {
				Node* node = NodeFromIndex(jointIndex, pRoots, numRoots);
				if (node) {
					node->jointNodeIndex = jointIndex;
					node->jointIndexInSkin = jointIndexInSkin++;
					newSkin->joints.push_back(NodeFromIndex(jointIndex, pRoots, numRoots));
				}
			}

			// Get inverse bind matrices from buffer
			if (source.inverseBindMatrices > -1) {
				const tinygltf::Accessor& accessor = tinygltfModel.accessors[source.inverseBindMatrices];
				const tinygltf::BufferView& bufferView = tinygltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = tinygltfModel.buffers[bufferView.buffer];
				newSkin->inverseBindMatrices.resize(accessor.count);
				memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
			}

			m_pModel->m_skins.push_back(newSkin);
		}
	}

	void MySimpleGltfLoader::LoadAnimations(const tinygltf::Model& tinygltfModel)
	{
		for (const tinygltf::Animation& anim : tinygltfModel.animations) {
			Animation animation{};
			animation.name = anim.name;
			if (anim.name.empty()) {
				animation.name = std::to_string(m_pModel->m_animations.size());
			}

			// Samplers
			for (auto& samp : anim.samplers) {
				AnimationSampler sampler{};

				if (samp.interpolation == "LINEAR") {
					sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
				}
				if (samp.interpolation == "STEP") {
					sampler.interpolation = AnimationSampler::InterpolationType::STEP;
				}
				if (samp.interpolation == "CUBICSPLINE") {
					sampler.interpolation = AnimationSampler::InterpolationType::CUBICSPLINE;
				}

				// Read sampler input time values
				{
					const tinygltf::Accessor& accessor = tinygltfModel.accessors[samp.input];
					const tinygltf::BufferView& bufferView = tinygltfModel.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = tinygltfModel.buffers[bufferView.buffer];

					assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

					float* buf = new float[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(float));
					for (size_t index = 0; index < accessor.count; index++) {
						sampler.inputs.push_back(buf[index]);
					}
					delete[] buf;
					for (auto input : sampler.inputs) {
						if (input < animation.start) {
							animation.start = input;
						};
						if (input > animation.end) {
							animation.end = input;
						}
					}
				}

				// Read sampler output T/R/S values 
				{
					const tinygltf::Accessor& accessor = tinygltfModel.accessors[samp.output];
					const tinygltf::BufferView& bufferView = tinygltfModel.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = tinygltfModel.buffers[bufferView.buffer];

					assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

					switch (accessor.type) {
					case TINYGLTF_TYPE_VEC3: {
						glm::vec3* buf = new glm::vec3[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::vec3));
						for (size_t index = 0; index < accessor.count; index++) {
							sampler.outputsVec4.push_back(glm::vec4(buf[index], 0.0f));
						}
						delete[] buf;
						break;
					}
					case TINYGLTF_TYPE_VEC4: {
						glm::vec4* buf = new glm::vec4[accessor.count];
						memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::vec4));
						for (size_t index = 0; index < accessor.count; index++) {
							sampler.outputsVec4.push_back(buf[index]);
						}
						delete[] buf;
						break;
					}
					default: {
						std::cout << "unknown type" << std::endl;
						break;
					}
					}
				}

				animation.samplers.push_back(sampler);
			}

			// Channels
			for (auto& source : anim.channels) {
				AnimationChannel channel{};

				if (source.target_path == "rotation") {
					channel.path = AnimationChannel::PathType::ROTATION;
				}
				if (source.target_path == "translation") {
					channel.path = AnimationChannel::PathType::TRANSLATION;
				}
				if (source.target_path == "scale") {
					channel.path = AnimationChannel::PathType::SCALE;
				}
				if (source.target_path == "weights") {
					std::cout << "weights not yet supported, skipping channel" << std::endl;
					continue;
				}
				channel.samplerIndex = source.sampler;
				channel.node = NodeFromIndex(source.target_node, m_pModel->m_NodeRoots.data(), m_pModel->m_NodeRoots.size());
				if (!channel.node) {
					continue;
				}

				animation.channels.push_back(channel);
			}

			m_pModel->m_animations.push_back(animation);
		}
	}

	void MySimpleGltfLoader::Node::UpdateJoints(glm::mat4 parentMatrix, std::array<glm::mat4, MAX_JOINTS>& jointMatrices)
	{
		// if not joint node, skip.
		if (jointNodeIndex < 0)
			return;

		glm::mat4 curNodeMatrix = localMatrix();
		glm::mat4 toRoot = parentMatrix * curNodeMatrix;

		// curjointSpace -> jointRoot
		jointMatrices[jointIndexInSkin] = toRoot;

		for (auto& child : children)
			child->UpdateJoints(toRoot, jointMatrices);
	}

	void MySimpleGltfLoader::Animator::UpdateAnimation(float deltaTime)
	{
		if (m_pTargetAnim == nullptr)
			return;

		m_accPlayTime += deltaTime * m_speed;
		if (m_accPlayTime > m_pTargetAnim->end)
		{
			m_accPlayTime = 0.f;
		}

		float time = m_accPlayTime;
		//time = deltaTime; // TEMP TODO
		bool updated = false;
		for (auto& channel : m_pTargetAnim->channels) {
			AnimationSampler& sampler = m_pTargetAnim->samplers[channel.samplerIndex];
			if (sampler.inputs.size() > sampler.outputsVec4.size()) {
				continue;
			}

			for (auto i = 0; i < sampler.inputs.size() - 1; i++) {
				if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
					float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
					if (u <= 1.0f) {
						switch (channel.path) {
						case AnimationChannel::PathType::TRANSLATION: {
							glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
							channel.node->translation = glm::vec3(trans);
							break;
						}
						case AnimationChannel::PathType::SCALE: {
							glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
							channel.node->scale = glm::vec3(trans);
							break;
						}
						case AnimationChannel::PathType::ROTATION: {
							glm::quat q1;
							q1.x = sampler.outputsVec4[i].x;
							q1.y = sampler.outputsVec4[i].y;
							q1.z = sampler.outputsVec4[i].z;
							q1.w = sampler.outputsVec4[i].w;
							glm::quat q2;
							q2.x = sampler.outputsVec4[i + 1].x;
							q2.y = sampler.outputsVec4[i + 1].y;
							q2.z = sampler.outputsVec4[i + 1].z;
							q2.w = sampler.outputsVec4[i + 1].w;
							channel.node->rotation = glm::normalize(glm::slerp(q1, q2, u));
							break;
						}
						}
						updated = true;
					}
				}
			}
		}
	}

	MySimpleGltfLoader::Node* MySimpleGltfLoader::NodeFromIndex(uint32_t index, Node** pRoots,
	                                                            uint32_t numRoots)
	{
		Node* nodeFound = nullptr;
		for (uint32_t i = 0; i < numRoots; ++i)
		{
			nodeFound = FindNode_Recur(pRoots[i], index);
			if (nodeFound) { break; }
		}
		return nodeFound;
	}

	MySimpleGltfLoader::Node* MySimpleGltfLoader::FindNode_Recur(Node* pParent, uint32_t index)
	{
		Node* nodeFound = nullptr;
		if (pParent->index == index) {
			return pParent;
		}
		for (auto& child : pParent->children) {
			nodeFound = FindNode_Recur(child, index);
			if (nodeFound) {
				break;
			}
		}
		return nodeFound;
	}

	void MySimpleGltfLoader::Model::UpdateJoints()
	{
		for (auto& rootToMatrices : m_rootToMatricesMap)
		{
			Node* jointRoot = rootToMatrices.first;
			std::array<glm::mat4, 256>& jointMatrices = rootToMatrices.second;
			jointRoot->UpdateJoints(glm::mat4(1.f), jointMatrices);
		}
	}

	void MySimpleGltfLoader::Model::UpdateNodeTransforms(Node* pNode)
	{
		if (pNode->pMesh) 
		{
			glm::mat4 m = pNode->getMatrix();
			auto& pMesh = pNode->pMesh;

			pMesh->matrix = m;
			// Copy nodeMatrix First
			memcpy(pMesh->meshConstantBuffer.mapped, &pMesh->matrix, Mesh::CONSTANT_DATA_SIZE);
			if (pNode->skin) {
				SkeletalMesh* pCastedMesh = static_cast<SkeletalMesh*>(pMesh);

				const std::array<glm::mat4, MAX_JOINTS>& jointMatrices = m_rootToMatricesMap[pNode->skin->jointRoot];

				//glm::mat4 inverseTransform = glm::inverse(m); // inverse(node to world)
				for (size_t i = 0; i < pNode->skin->joints.size(); i++) {
					Node* jointNode = pNode->skin->joints[i];
					// No need to Multiply inverse of m(nodeWorld or MeshWorld), because jointMatrices is already MESH LOCAL
					glm::mat4 jointMat = jointMatrices[i] * pNode->skin->inverseBindMatrices[i];
					pCastedMesh->jointMatrix[i] = jointMat;
				}
				memcpy((uint8_t*)pMesh->meshConstantBuffer.mapped + Mesh::CONSTANT_DATA_SIZE, &pCastedMesh->jointMatrix, SkeletalMesh::CONSTANT_DATA_SIZE);
			}
		}

		for (auto& child : pNode->children) {
			UpdateNodeTransforms(child);
		}
	}

	void Create_ModelDeviceResources(VkDevice device, MySimpleGltfLoader::Model& model)
	{
		// Create Vertex/Index Buffer
		{
			//VkDeviceSize vertexBufferSize = sizeof(myglTF::MySimpleGltfLoader::VertexSimple) * model.m_vertices.size();
			//m_pVksDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			//	vertexBufferSize, &model.m_vertexBuffer.vkBuffer, &model.m_vertexBuffer.vkMemory, graphicsQueue, model.m_vertices.data());
			//model.m_vertexBuffer.device = device;
			//model.m_vertexBuffer.deviceAddress = getBufferDeviceAddress(model.m_vertexBuffer.vkBuffer);
			//model.m_vertexBuffer.bufferSize = vertexBufferSize;
		}

		model.m_bindLayoutSet.device = device;
		uint32_t bufferCount = model.m_linearMeshes.size();
		VkDescriptorType modelDescriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

		// DescriptorPool
		{
			std::vector<VkDescriptorPoolSize> poolSizes;
			poolSizes.push_back(VkDescriptorPoolSize{ modelDescriptorType, bufferCount });

			VkDescriptorPoolCreateInfo descriptorPoolCI
			{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
				.maxSets = bufferCount + 1,
				.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
				.pPoolSizes = poolSizes.data(),
			};

			VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &model.m_bindLayoutSet.descriptorPool));
		}
		// DescriptorsetLayout
		{
			uint32_t additionalFlag = model.m_isSkeletalMesh ? VK_SHADER_STAGE_COMPUTE_BIT : 0;
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				// [model matrix] or [modelMat + Skinning info]
				vks::initializers::descriptorSetLayoutBinding(modelDescriptorType, VK_SHADER_STAGE_VERTEX_BIT | additionalFlag, BINDING_MODEL_DEFAULT),
			};
			VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
			descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			descriptorLayoutCI.pBindings = setLayoutBindings.data();
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &model.m_bindLayoutSet.descriptorSetLayout));
			
		}
		// Alloc Descriptor Sets and Update
		for (auto& pMesh : model.m_linearMeshes)
		{
			MySimpleGltfLoader::SkeletalMesh* pCastedMesh = static_cast<MySimpleGltfLoader::SkeletalMesh*>(pMesh);

			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = model.m_bindLayoutSet.descriptorPool;
			descriptorSetAllocInfo.pSetLayouts = &model.m_bindLayoutSet.descriptorSetLayout;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &pCastedMesh->meshConstantBuffer.descriptorSet));

			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSet.descriptorCount = 1;
			writeDescriptorSet.dstSet = pCastedMesh->meshConstantBuffer.descriptorSet;
			writeDescriptorSet.dstBinding = BINDING_MODEL_DEFAULT;
			writeDescriptorSet.pBufferInfo = &pCastedMesh->meshConstantBuffer.descriptor;

			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}
	}
}
