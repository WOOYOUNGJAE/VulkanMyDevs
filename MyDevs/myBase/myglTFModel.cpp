#include "myglTFModel.h"

#include "myDeviceFuncTable.h"
#include "myIncludesCPUGPU.h"
#include "MyVulkanRTBase.h"

VkMemoryPropertyFlags myglTF::Model::memoryPropertyFlags = 0;
uint32_t myglTF::Model::descriptorBindingFlags = myglTF::DescriptorBindingFlags::ImageBaseColor | myglTF::DescriptorBindingFlags::ImageNormalMap;
VkMemoryPropertyFlags myglTF::ModelRT::memoryPropertyFlags = 0;
uint32_t myglTF::ModelRT::descriptorBindingFlags = myglTF::DescriptorBindingFlags::ImageBaseColor | myglTF::DescriptorBindingFlags::ImageNormalMap;

#include "meshoptimizer.h"

myglTF::Model::~Model()
{
	vkDestroyBuffer(device->logicalDevice, rootUniformBuffer.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, rootUniformBuffer.memory, nullptr);

	vkDestroyBuffer(device->logicalDevice, vertices.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, vertices.memory, nullptr);
	vkDestroyBuffer(device->logicalDevice, indices.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, indices.memory, nullptr);
	vkDestroyBuffer(device->logicalDevice, meshletVertices.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, meshletVertices.memory, nullptr);
	vkDestroyBuffer(device->logicalDevice, meshletIndices.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, meshletIndices.memory, nullptr);
	vkDestroyBuffer(device->logicalDevice, meshlets.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, meshlets.memory, nullptr);
	for (auto& texture : textures) {
		texture.destroy();
	}
	for (auto& node : nodes) {
		delete node;
	}
	for (auto& skin : skins) {
		delete skin;
	}
	if (descriptorSetLayoutUbo != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayoutUbo, nullptr);
		descriptorSetLayoutUbo = VK_NULL_HANDLE;
	}
	if (descriptorSetLayoutImage != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayoutImage, nullptr);
		descriptorSetLayoutImage = VK_NULL_HANDLE;
	}
	if (descriptorSetLayoutMeshShader != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayoutMeshShader, nullptr);
		descriptorSetLayoutMeshShader = VK_NULL_HANDLE;
	}
	vkDestroyDescriptorPool(device->logicalDevice, descriptorPool, nullptr);
	emptyTexture.destroy();


	if (meshShaderPipeline)
		vkDestroyPipeline(device->logicalDevice, meshShaderPipeline, nullptr);
}

void myglTF::Model::loadNode(myglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex,
	const tinygltf::Model& model, std::vector<uint32_t>& indexBuffer, std::vector<VertexType*>& vertices,
	float globalscale)
{
	myglTF::Node* newNode = new Node{};
	newNode->index = nodeIndex;
	newNode->parent = parent;
	newNode->name = node.name;
	newNode->skinIndex = node.skin;
	newNode->matrix = glm::mat4(1.0f);

	// Generate local node matrix
	glm::vec3 translation = glm::vec3(0.0f);
	if (node.translation.size() == 3) {
		translation = glm::make_vec3(node.translation.data());
		newNode->translation = translation;
	}
	glm::mat4 rotation = glm::mat4(1.0f);
	if (node.rotation.size() == 4) {
		glm::quat q = glm::make_quat(node.rotation.data());
		newNode->rotation = glm::mat4(q);
	}
	glm::vec3 scale = glm::vec3(1.0f);
	if (node.scale.size() == 3) {
		scale = glm::make_vec3(node.scale.data());
		newNode->scale = scale;
	}
	if (node.matrix.size() == 16) {
		newNode->matrix = glm::make_mat4x4(node.matrix.data());
		if (globalscale != 1.0f) {
			//newNode->matrix = glm::scale(newNode->matrix, glm::vec3(globalscale));
		}
	};

	// Node with children
	if (node.children.size() > 0) {
		for (auto i = 0; i < node.children.size(); i++) {
			loadNode(newNode, model.nodes[node.children[i]], node.children[i], model, indexBuffer, vertices, globalscale);
		}
	}

	// Node contains mesh data
	if (node.mesh > -1) {
		const tinygltf::Mesh mesh = model.meshes[node.mesh];
		Mesh* newMesh = new Mesh(device, newNode->matrix, !preTransform, newNode->skin);
		newMesh->name = mesh.name;
		for (size_t j = 0; j < mesh.primitives.size(); j++) {
			const tinygltf::Primitive& primitive = mesh.primitives[j];
			if (primitive.indices < 0) {
				continue;
			}
			uint32_t indexStart = static_cast<uint32_t>(indexBuffer.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertices.size());
			uint32_t indexCount = 0;
			uint32_t vertexCount = 0;
			glm::vec3 posMin{};
			glm::vec3 posMax{};
			bool hasSkin = false;
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

				const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
				const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
				bufferPos = reinterpret_cast<const float*>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
				posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
				posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

				if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
					const tinygltf::Accessor& normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView& normView = model.bufferViews[normAccessor.bufferView];
					bufferNormals = reinterpret_cast<const float*>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
				}

				if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
					bufferTexCoords = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
				{
					const tinygltf::Accessor& colorAccessor = model.accessors[primitive.attributes.find("COLOR_0")->second];
					const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor.bufferView];
					// Color buffer are either of type vec3 or vec4
					numColorComponents = colorAccessor.type == TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 ? 3 : 4;
					bufferColors = reinterpret_cast<const float*>(&(model.buffers[colorView.buffer].data[colorAccessor.byteOffset + colorView.byteOffset]));
				}

				if (primitive.attributes.find("TANGENT") != primitive.attributes.end())
				{
					const tinygltf::Accessor& tangentAccessor = model.accessors[primitive.attributes.find("TANGENT")->second];
					const tinygltf::BufferView& tangentView = model.bufferViews[tangentAccessor.bufferView];
					bufferTangents = reinterpret_cast<const float*>(&(model.buffers[tangentView.buffer].data[tangentAccessor.byteOffset + tangentView.byteOffset]));
				}

				// Skinning
				// Joints
				if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& jointAccessor = model.accessors[primitive.attributes.find("JOINTS_0")->second];
					const tinygltf::BufferView& jointView = model.bufferViews[jointAccessor.bufferView];
					bufferJoints = reinterpret_cast<const uint16_t*>(&(model.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]));
				}

				if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
					const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
					bufferWeights = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				hasSkin = (bufferJoints && bufferWeights);

				vertexCount = static_cast<uint32_t>(posAccessor.count);

				for (size_t v = 0; v < posAccessor.count; v++) {
					/*
					 * if skin:VertexSkiniing, else: VertexSimple
					 * allocated in here, released in "loadfromfile()"
					 * pushed into param::vertexBuffer
					 */
					VertexType* vert = hasSkin ? new VertexSkinning{} : new VertexSimple{};

					vert->pos = glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
					//if (bool preTransform) // apply node's transform to vertices while loading
					//{
					//	vert->pos = newNode->getMatrix() * glm::vec4(vert->pos, 1.f);
					//}

					vert->normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
					vert->uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
					if (bufferColors) {
						switch (numColorComponents) {
						case 3:
							vert->color = glm::vec4(glm::make_vec3(&bufferColors[v * 3]), 1.0f);
							break;
						case 4:
							vert->color = glm::make_vec4(&bufferColors[v * 4]);
							break;
						}
					}
					else {
						vert->color = glm::vec4(1.0f);
					}
					vert->tangent = bufferTangents ? glm::vec4(glm::make_vec4(&bufferTangents[v * 4])) : glm::vec4(0.0f);
					if (hasSkin)
					{
						static_cast<VertexSkinning*>(vert)->joint0 = glm::vec4(glm::make_vec4(&bufferJoints[v * 4]));
						static_cast<VertexSkinning*>(vert)->weight0 = glm::vec4(glm::make_vec4(&bufferWeights[v * 4]));
					}
					vertices.push_back(vert);
				}
			}
			// Indices
			{
				const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
				const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

				indexCount = static_cast<uint32_t>(accessor.count);

				switch (accessor.componentType) {
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
					uint32_t* buf = new uint32_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
					uint16_t* buf = new uint16_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
					uint8_t* buf = new uint8_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				default:
					std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
					return;
				}
			}
			Primitive* newPrimitive = new Primitive(indexStart, indexCount, primitive.material > -1 ? materials[primitive.material] : materials.back());
			newPrimitive->firstVertex = vertexStart;
			newPrimitive->vertexCount = vertexCount;
			newPrimitive->setDimensions(posMin, posMax);
			newMesh->primitives.push_back(newPrimitive);
		}
		newNode->mesh = newMesh;
	}
	if (parent) {
		parent->children.push_back(newNode);
	}
	else {
		nodes.push_back(newNode);
	}
	linearNodes.push_back(newNode);
}

void myglTF::Model::loadSkins(tinygltf::Model& gltfModel)
{
	for (tinygltf::Skin& source : gltfModel.skins) {
		Skin* newSkin = new Skin{};
		newSkin->name = source.name;

		// Find skeleton root node
		if (source.skeleton > -1) {
			newSkin->skeletonRoot = nodeFromIndex(source.skeleton);
		}

		// Find joint nodes
		for (int jointIndex : source.joints) {
			Node* node = nodeFromIndex(jointIndex);
			if (node) {
				newSkin->joints.push_back(nodeFromIndex(jointIndex));
			}
		}

		// Get inverse bind matrices from buffer
		if (source.inverseBindMatrices > -1) {
			const tinygltf::Accessor& accessor = gltfModel.accessors[source.inverseBindMatrices];
			const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];
			newSkin->inverseBindMatrices.resize(accessor.count);
			memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
		}

		skins.push_back(newSkin);
	}
}

void myglTF::Model::loadImages(tinygltf::Model& gltfModel, vks::VulkanDevice* device, VkQueue transferQueue)
{
	for (tinygltf::Image& image : gltfModel.images) {
		myglTF::Texture texture;
		texture.fromglTfImage(image, path, device, transferQueue);
		texture.index = static_cast<uint32_t>(textures.size());
		textures.push_back(texture);
	}
	// Create an empty texture to be used for empty material images
	createEmptyTexture(transferQueue);
}

void myglTF::Model::loadMaterials(tinygltf::Model& gltfModel)
{
	for (tinygltf::Material& mat : gltfModel.materials) {
		myglTF::Material material(device);
		if (mat.values.find("baseColorTexture") != mat.values.end()) {
			material.baseColorTexture = getTexture(gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source);
		}
		// Metallic roughness workflow
		if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
			material.metallicRoughnessTexture = getTexture(gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source);
		}
		if (mat.values.find("roughnessFactor") != mat.values.end()) {
			material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
		}
		if (mat.values.find("metallicFactor") != mat.values.end()) {
			material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
		}
		if (mat.values.find("baseColorFactor") != mat.values.end()) {
			material.baseColorFactor = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
		}
		if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
			material.normalTexture = getTexture(gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source);
		}
		else {
			material.normalTexture = &emptyTexture;
		}
		if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
			material.emissiveTexture = getTexture(gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
			material.occlusionTexture = getTexture(gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
			tinygltf::Parameter param = mat.additionalValues["alphaMode"];
			if (param.string_value == "BLEND") {
				material.alphaMode = myglTF::Material::ALPHAMODE_BLEND;
			}
			if (param.string_value == "MASK") {
				material.alphaMode = myglTF::Material::ALPHAMODE_MASK;
			}
		}
		if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
			material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
		}

		materials.push_back(material);
	}
	// Push a default material at the end of the list for meshes with no material assigned
	materials.push_back(myglTF::Material(device));
}

void myglTF::Model::loadAnimations(tinygltf::Model& gltfModel)
{
	for (tinygltf::Animation& anim : gltfModel.animations) {
		myglTF::Animation animation{};
		animation.name = anim.name;
		if (anim.name.empty()) {
			animation.name = std::to_string(animations.size());
		}

		// Samplers
		for (auto& samp : anim.samplers) {
			myglTF::AnimationSampler sampler{};

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
				const tinygltf::Accessor& accessor = gltfModel.accessors[samp.input];
				const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

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
				const tinygltf::Accessor& accessor = gltfModel.accessors[samp.output];
				const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

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
			myglTF::AnimationChannel channel{};

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
			channel.node = nodeFromIndex(source.target_node);
			if (!channel.node) {
				continue;
			}

			animation.channels.push_back(channel);
		}

		animations.push_back(animation);
	}
}

void myglTF::Model::loadFromFile(std::string filename, vks::VulkanDevice* device, VkQueue transferQueue,
	uint32_t fileLoadingFlags, float scale)
{
	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF gltfContext;
	if (fileLoadingFlags & FileLoadingFlags::DontLoadImages) {
		gltfContext.SetImageLoader(loadImageDataFuncEmpty, nullptr);
	}
	else {
		gltfContext.SetImageLoader(loadImageDataFunc, nullptr);
	}
#if defined(__ANDROID__)
	// On Android all assets are packed with the apk in a compressed form, so we need to open them using the asset manager
	// We let tinygltf handle this, by passing the asset manager of our app
	tinygltf::asset_manager = androidApp->activity->assetManager;
#endif
	size_t pos = filename.find_last_of('/');
	path = filename.substr(0, pos);

	std::string error, warning;

	this->device = device;

#if defined(__ANDROID__)
	// On Android all assets are packed with the apk in a compressed form, so we need to open them using the asset manager
	// We let tinygltf handle this, by passing the asset manager of our app
	tinygltf::asset_manager = androidApp->activity->assetManager;
#endif
	bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename);
	std::vector<VertexType*> tempVerticesCPU; 
	std::vector<uint32_t> tempIndicesCPU;
	bool isSkinningModel = gltfModel.skins.size() > 0;
	preTransform = fileLoadingFlags & FileLoadingFlags::PreTransformVertices;
	uint32_t vertexSize = isSkinningModel ? sizeof(VertexSkinning) : sizeof(VertexSimple);
	if (fileLoaded) {
		if (!(fileLoadingFlags & FileLoadingFlags::DontLoadImages)) {
			loadImages(gltfModel, device, transferQueue);
		}
		loadMaterials(gltfModel);
		const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
		for (size_t i = 0; i < scene.nodes.size(); i++) {
			const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
			loadNode(nullptr, node, scene.nodes[i], gltfModel, tempIndicesCPU, tempVerticesCPU, scale);
		}
		if (gltfModel.animations.size() > 0) {
			loadAnimations(gltfModel);
		}

		loadSkins(gltfModel);
		for (auto node : linearNodes) {
			// Assign skins
			if (node->skinIndex > -1) {
				node->skin = skins[node->skinIndex];
			}
			// Initial pose
			if (preTransform == false && node->mesh) {
				node->update();
			}
		}
	}
	else {
		vks::tools::exitFatal("Could not load glTF file \"" + filename + "\": " + error, -1);
		return;
	}

	// Pre-Calculations for requested features
	if ((fileLoadingFlags & FileLoadingFlags::PreTransformVertices) || (fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors) || (fileLoadingFlags & FileLoadingFlags::FlipY)) {
		
		const bool preMultiplyColor = fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors;
		const bool flipY = fileLoadingFlags & FileLoadingFlags::FlipY;
		for (Node* node : linearNodes) {
			if (node->mesh) {
				const glm::mat4 localMatrix = node->getMatrix();
				for (Primitive* primitive : node->mesh->primitives) {
					for (uint32_t i = 0; i < primitive->vertexCount; i++) {
						VertexType* vertex = tempVerticesCPU[primitive->firstVertex + i];
						// Pre-transform vertex positions by node-hierarchy
						if (preTransform) {
							vertex->pos = glm::vec3(localMatrix * glm::vec4(vertex->pos, 1.0f));
							vertex->normal = glm::normalize(glm::mat3(localMatrix) * vertex->normal);
						}
						// Flip Y-Axis of vertex positions
						if (flipY) {
							vertex->pos.y *= -1.0f;
							vertex->normal.y *= -1.0f;
						}
						// Pre-Multiply vertex colors with material base color
						if (preMultiplyColor) {
							vertex->color = primitive->material.baseColorFactor * vertex->color;
						}
					}
				}
			}
		}
	}

	for (auto& extension : gltfModel.extensionsUsed) {
		if (extension == "KHR_materials_pbrSpecularGlossiness") {
			std::cout << "Required extension: " << extension;
			metallicRoughnessWorkflow = false;
		}
	}
	// A vector used to store vertex data in byte form, becuase of two different type vertex(VertexSimple/VertexSkinning)
	std::vector<byte> vertexBufferByte(vertexSize * tempVerticesCPU.size());
	uint64_t byteOffset = 0;
	for (VertexType*& vertex : tempVerticesCPU)
	{
		memcpy(&vertexBufferByte[byteOffset], vertex, vertexSize);
		byteOffset += vertexSize;
	}

	size_t vertexBufferSize = tempVerticesCPU.size() * vertexSize;
	size_t indexBufferSize = tempIndicesCPU.size() * sizeof(uint32_t);
	indices.count = static_cast<uint32_t>(tempIndicesCPU.size());
	vertices.count = static_cast<uint32_t>(tempVerticesCPU.size());

	assert((vertexBufferSize > 0) && (indexBufferSize > 0));

	struct StagingBuffer {
		VkBuffer buffer;
		VkDeviceMemory memory;
	} vertexStaging{}, indexStaging{}, meshletVertexStaging{}, meshletIndexStaging{}, meshletStaging{};

	// Create staging buffers
	uint32_t additionalBufferUsageFlag = 0x00000000; // uint32 becuase VkBufferUsageFlagBits does not have 0
	if (fileLoadingFlags & FileLoadingFlags::PrepareMeshShaderPipeline)
	{
		additionalBufferUsageFlag |= (uint32_t)VkBufferUsageFlagBits::VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
	// Vertex data
	VK_CHECK_RESULT(device->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		vertexBufferSize,
		&vertexStaging.buffer,
		&vertexStaging.memory,
		vertexBufferByte.data()));
	// Index data
	VK_CHECK_RESULT(device->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		indexBufferSize,
		&indexStaging.buffer,
		&indexStaging.memory,
		tempIndicesCPU.data()));

	// Create device local buffers
	// Vertex buffer
	VK_CHECK_RESULT(device->createBuffer(
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | memoryPropertyFlags | (VkBufferUsageFlagBits)additionalBufferUsageFlag,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		vertexBufferSize,
		&vertices.buffer,
		&vertices.memory));
	// Index buffer
	VK_CHECK_RESULT(device->createBuffer(
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | memoryPropertyFlags,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		indexBufferSize,
		&indices.buffer,
		&indices.memory));

	// Copy from staging buffers
	VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	VkBufferCopy copyRegion = {};

	copyRegion.size = vertexBufferSize;
	vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, vertices.buffer, 1, &copyRegion);

	copyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(copyCmd, indexStaging.buffer, indices.buffer, 1, &copyRegion);

	device->flushCommandBuffer(copyCmd, transferQueue, false);
	vkDestroyBuffer(device->logicalDevice, vertexStaging.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, vertexStaging.memory, nullptr);
	vkDestroyBuffer(device->logicalDevice, indexStaging.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, indexStaging.memory, nullptr);

	// Prepare Meshlets
	if (fileLoadingFlags & FileLoadingFlags::PrepareMeshShaderPipeline)
	{
		meshopt_Meshlet* tempAllocatedMeshlets = nullptr;
		uint32_t numMeshlets = 0;
		std::vector<uint32_t> tempMeshletVertices; // Meshlet::vertex == Index from OriginalVertexBuffer
		std::vector<uint32_t> tempMeshletPackedTriangles; // single uint32 contains 3 indices(triangle)
		generateMeshlets(tempVerticesCPU, tempIndicesCPU, tempMeshletVertices, tempMeshletPackedTriangles, &tempAllocatedMeshlets, numMeshlets);



		size_t meshletVertexBufferSize = tempMeshletVertices.size() * sizeof(uint32_t);
		size_t meshletIndexBufferSize = tempMeshletPackedTriangles.size() * sizeof(uint32_t);
		size_t meshletBufferSize = numMeshlets * sizeof(meshopt_Meshlet);

		// Create staging buffers
		// Staging Buffer - Meshlet Vertex
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			meshletVertexBufferSize,
			&meshletVertexStaging.buffer,
			&meshletVertexStaging.memory,
			tempMeshletVertices.data()));

		// Staging Buffer - Meshlet Index
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			meshletIndexBufferSize,
			&meshletIndexStaging.buffer,
			&meshletIndexStaging.memory,
			tempMeshletPackedTriangles.data()));

		// Staging Buffer - Meshlet
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			meshletBufferSize,
			&meshletStaging.buffer,
			&meshletStaging.memory,
			tempAllocatedMeshlets));

		// Create device local buffers
		// Meshlet Vertex buffer
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | memoryPropertyFlags,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			meshletVertexBufferSize,
			&meshletVertices.buffer,
			&meshletVertices.memory));
		// Meshlet Index buffer
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | memoryPropertyFlags,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			meshletIndexBufferSize,
			&meshletIndices.buffer,
			&meshletIndices.memory));
		// Meshlet buffer
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | memoryPropertyFlags,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			meshletBufferSize,
			&meshlets.buffer,
			&meshlets.memory));

		// Fill Meshlet buffers count data
		meshlets.count = numMeshlets;
		meshletVertices.count = static_cast<int>(tempMeshletVertices.size());
		meshletIndices.count = static_cast<int>(tempMeshletPackedTriangles.size());

		// Copy from staging buffers
		//VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

		// copy meshlet vertices
		copyRegion.size = meshletVertexBufferSize;
		vkCmdCopyBuffer(copyCmd, meshletVertexStaging.buffer, meshletVertices.buffer, 1, &copyRegion);

		// copy meshlet indices
		copyRegion.size = meshletIndexBufferSize;
		vkCmdCopyBuffer(copyCmd, meshletIndexStaging.buffer, meshletIndices.buffer, 1, &copyRegion);

		// copy meshlets
		copyRegion.size = meshletBufferSize;
		vkCmdCopyBuffer(copyCmd, meshletStaging.buffer, meshlets.buffer, 1, &copyRegion);


		device->flushCommandBuffer(copyCmd, transferQueue, true);
		delete[] tempAllocatedMeshlets; tempAllocatedMeshlets = nullptr;


		// Create Descriptor
		vertexBufferDescriptor = { vertices.buffer, 0, vertexBufferSize };
		meshletsDescriptor = { meshlets.buffer, 0, meshletBufferSize };
		meshletVerticesDescriptor = { meshletVertices.buffer, 0, meshletVertexBufferSize };
		meshletIndicesDescriptor = { meshletIndices.buffer, 0, meshletIndexBufferSize };


		vkDestroyBuffer(device->logicalDevice, meshletVertexStaging.buffer, nullptr);
		vkFreeMemory(device->logicalDevice, meshletVertexStaging.memory, nullptr);
		vkDestroyBuffer(device->logicalDevice, meshletIndexStaging.buffer, nullptr);
		vkFreeMemory(device->logicalDevice, meshletIndexStaging.memory, nullptr);
		vkDestroyBuffer(device->logicalDevice, meshletStaging.buffer, nullptr);
		vkFreeMemory(device->logicalDevice, meshletStaging.memory, nullptr);
	}
	getSceneDimensions();

	// Setup descriptors
	uint32_t uboCount{ 0 };
	uint32_t imageCount{ 0 };
	if (preTransform == false)
	{
		for (auto& node : linearNodes) {
			if (node->mesh) {
				uboCount++;
			}
		}
	}
	else uboCount = 1;
	
	for (auto& material : materials) {
		if (material.baseColorTexture != nullptr) {
			imageCount++;
		}
	}
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uboCount },
	};
	if (imageCount > 0) {
		if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
			poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount });
		}
		if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
			poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount });
		}
	}
	if (fileLoadingFlags & FileLoadingFlags::PrepareMeshShaderPipeline) // for Mesh Shader
	{
		poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }); // vertex buffer
		poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }); // meshlet buffer
		poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }); // meshlet vertex buffer
		poolSizes.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }); // meshlet index buffer
	}

	VkDescriptorPoolCreateInfo descriptorPoolCI{};
	descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	descriptorPoolCI.pPoolSizes = poolSizes.data();
	descriptorPoolCI.maxSets = uboCount + imageCount * 2 + 4;
	VK_CHECK_RESULT(vkCreateDescriptorPool(device->logicalDevice, &descriptorPoolCI, nullptr, &descriptorPool));


	// Descriptors for per-node uniform buffers
	{
		// Layout is global, so only create if it hasn't already been created before
		if (descriptorSetLayoutUbo == VK_NULL_HANDLE) {
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				// [model matrix] or [modelMat + Skinning info]
				vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT, 0),
			};
			VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
			descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			descriptorLayoutCI.pBindings = setLayoutBindings.data();
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI, nullptr, &descriptorSetLayoutUbo));
		}
		if (preTransform)
		{
			// Create bufffer
			VK_CHECK_RESULT(device->createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				sizeof(UniformData),
				&rootUniformBuffer.buffer,
				&rootUniformBuffer.memory,
				&uniformBlock));
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, rootUniformBuffer.memory, 0, sizeof(UniformData), 0, &rootUniformBuffer.mapped));
			rootUniformBuffer.descriptor = { rootUniformBuffer.buffer, 0, sizeof(UniformData) };

			// allocate descriptor
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorPool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayoutUbo;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &rootUniformBuffer.descriptorSet));

			// update
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSet.descriptorCount = 1;
			writeDescriptorSet.dstSet = rootUniformBuffer.descriptorSet;
			writeDescriptorSet.dstBinding = 0;
			writeDescriptorSet.pBufferInfo = &rootUniformBuffer.descriptor;

			vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
		}
		else // prepare all meshes' ubo
		{
			for (auto node : nodes) {
				prepareNodeDescriptor(node, descriptorSetLayoutUbo);
			}			
		}
	}


	// Descriptors for per-material images
	{
		// Layout is global, so only create if it hasn't already been created before
		if (descriptorSetLayoutImage == VK_NULL_HANDLE) {
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
			setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(setLayoutBindings.size())));
			setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(setLayoutBindings.size())));
			/*if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
			}
			if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
			}*/
			VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
			descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			descriptorLayoutCI.pBindings = setLayoutBindings.data();
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI, nullptr, &descriptorSetLayoutImage));
		}
		for (auto& material : materials) {
			if (material.baseColorTexture != nullptr) {
				material.createDescriptorSet(descriptorPool, descriptorSetLayoutImage, descriptorBindingFlags);
			}
		}
	}

	// Descriptors for mesh shader pipeline
	if (fileLoadingFlags & FileLoadingFlags::PrepareMeshShaderPipeline && descriptorSetLayoutMeshShader == VK_NULL_HANDLE)
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
		// 0: vertxBuffer 1: meshlet buffer, 2: meshlet vertex buffer, 3: meshlet index buffer
		setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, /*VK_SHADER_STAGE_TASK_BIT_EXT |*/ VK_SHADER_STAGE_MESH_BIT_EXT, static_cast<uint32_t>(setLayoutBindings.size())));
		setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, /*VK_SHADER_STAGE_TASK_BIT_EXT |*/ VK_SHADER_STAGE_MESH_BIT_EXT, static_cast<uint32_t>(setLayoutBindings.size())));
		setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, /*VK_SHADER_STAGE_TASK_BIT_EXT |*/ VK_SHADER_STAGE_MESH_BIT_EXT, static_cast<uint32_t>(setLayoutBindings.size())));
		setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, /*VK_SHADER_STAGE_TASK_BIT_EXT |*/ VK_SHADER_STAGE_MESH_BIT_EXT, static_cast<uint32_t>(setLayoutBindings.size())));

		VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
		descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		descriptorLayoutCI.pBindings = setLayoutBindings.data();
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI, nullptr, &descriptorSetLayoutMeshShader));

		// Allocate & Write DescriptorSets
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
		descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocInfo.descriptorPool = descriptorPool;
		descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayoutMeshShader;
		descriptorSetAllocInfo.descriptorSetCount = 1;

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &meshShaderDescriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		// 0: vertxBuffer 1: meshlet buffer, 2: meshlet vertex buffer, 3: meshlet index buffer
		writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(meshShaderDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,static_cast<uint32_t>(writeDescriptorSets.size()), &vertexBufferDescriptor));
		writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(meshShaderDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,static_cast<uint32_t>(writeDescriptorSets.size()), &meshletsDescriptor));
		writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(meshShaderDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(writeDescriptorSets.size()), &meshletVerticesDescriptor));
		writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(meshShaderDescriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<uint32_t>(writeDescriptorSets.size()), &meshletIndicesDescriptor));

		vkUpdateDescriptorSets(device->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	// release
	for (auto& vertex : tempVerticesCPU)
	{
		delete vertex; vertex = nullptr;
	}

}

void myglTF::Model::bindBuffers(VkCommandBuffer commandBuffer)
{
	const VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
	vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
	buffersBound = true;
}

void myglTF::Model::drawNode(Node* node, VkCommandBuffer commandBuffer, uint32_t renderFlags,
                             VkPipelineLayout pipelineLayout, uint32_t bindImageSet)
{
	if (node->mesh) {
		for (Primitive* primitive : node->mesh->primitives) {
			bool skip = false;
			const myglTF::Material& material = primitive->material;
			if (renderFlags & RenderFlags::RenderOpaqueNodes) {
				skip = (material.alphaMode != Material::ALPHAMODE_OPAQUE);
			}
			if (renderFlags & RenderFlags::RenderAlphaMaskedNodes) {
				skip = (material.alphaMode != Material::ALPHAMODE_MASK);
			}
			if (renderFlags & RenderFlags::RenderAlphaBlendedNodes) {
				skip = (material.alphaMode != Material::ALPHAMODE_BLEND);
			}
			if (!skip) {
				VkDescriptorSet* pDescriptorSet = preTransform ? &rootUniformBuffer.descriptorSet : &node->mesh->uniformBuffer.descriptorSet;
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, pDescriptorSet, 0, nullptr);
				if (material.baseColorTexture) {
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, bindImageSet, 1, &material.descriptorSet, 0, nullptr);
				}

				// traditional pipeilne, using vertex shader
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, material.traditionalPipeline);
				vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
			}
		}
	}
	for (auto& child : node->children) {
		drawNode(child, commandBuffer, renderFlags, pipelineLayout, bindImageSet);
	}
}

void myglTF::Model::draw(VkCommandBuffer commandBuffer, uint32_t renderFlags, VkPipelineLayout pipelineLayout,
	uint32_t bindImageSet, PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT)
{
	if (vkCmdDrawMeshTasksEXT) // mesh shader
	{
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &rootUniformBuffer.descriptorSet, 0, nullptr);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 3, 1, &meshShaderDescriptorSet, 0, nullptr);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshShaderPipeline);
		uint32_t gridDimX = meshlets.count / WAVE_SIZE + 1; // num Thread Blocks
		vkCmdDrawMeshTasksEXT(commandBuffer, gridDimX, 1, 1);
	}
	else // traditional pipeline
	{
		if (!buffersBound) {
			{
				const VkDeviceSize offsets[1] = { 0 };
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
				vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
			}
		}
		for (auto& node : nodes) {
			drawNode(node, commandBuffer, renderFlags, pipelineLayout, bindImageSet);
		}
	}
}

void myglTF::Model::getNodeDimensions(Node* node, glm::vec3& min, glm::vec3& max)
{
	if (node->mesh) {
		for (Primitive* primitive : node->mesh->primitives) {
			glm::vec4 locMin = glm::vec4(primitive->dimensions.min, 1.0f) * node->getMatrix();
			glm::vec4 locMax = glm::vec4(primitive->dimensions.max, 1.0f) * node->getMatrix();
			if (locMin.x < min.x) { min.x = locMin.x; }
			if (locMin.y < min.y) { min.y = locMin.y; }
			if (locMin.z < min.z) { min.z = locMin.z; }
			if (locMax.x > max.x) { max.x = locMax.x; }
			if (locMax.y > max.y) { max.y = locMax.y; }
			if (locMax.z > max.z) { max.z = locMax.z; }
		}
	}
	for (auto child : node->children) {
		getNodeDimensions(child, min, max);
	}
}

void myglTF::Model::getSceneDimensions()
{
	dimensions.min = glm::vec3(FLT_MAX);
	dimensions.max = glm::vec3(-FLT_MAX);
	for (auto node : nodes) {
		getNodeDimensions(node, dimensions.min, dimensions.max);
	}
	dimensions.size = dimensions.max - dimensions.min;
	dimensions.center = (dimensions.min + dimensions.max) / 2.0f;
	dimensions.radius = glm::distance(dimensions.min, dimensions.max) / 2.0f;
}

void myglTF::Model::updateAnimation(uint32_t index, float time)
{
	if (index > static_cast<uint32_t>(animations.size()) - 1) {
		std::cout << "No animation with index " << index << std::endl;
		return;
	}
	Animation& animation = animations[index];

	bool updated = false;
	for (auto& channel : animation.channels) {
		myglTF::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
		if (sampler.inputs.size() > sampler.outputsVec4.size()) {
			continue;
		}

		for (auto i = 0; i < sampler.inputs.size() - 1; i++) {
			if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
				float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
				if (u <= 1.0f) {
					switch (channel.path) {
					case myglTF::AnimationChannel::PathType::TRANSLATION: {
						glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->translation = glm::vec3(trans);
						break;
					}
					case myglTF::AnimationChannel::PathType::SCALE: {
						glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->scale = glm::vec3(trans);
						break;
					}
					case myglTF::AnimationChannel::PathType::ROTATION: {
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
	if (updated) {
		for (auto& node : nodes) {
			node->update();
		}
	}
}

myglTF::Node* myglTF::Model::findNode(Node* parent, uint32_t index)
{
	Node* nodeFound = nullptr;
	if (parent->index == index) {
		return parent;
	}
	for (auto& child : parent->children) {
		nodeFound = findNode(child, index);
		if (nodeFound) {
			break;
		}
	}
	return nodeFound;
}

myglTF::Node* myglTF::Model::nodeFromIndex(uint32_t index)
{
	Node* nodeFound = nullptr;
	for (auto& node : nodes) {
		nodeFound = findNode(node, index);
		if (nodeFound) {
			break;
		}
	}
	return nodeFound;
}

void myglTF::Model::prepareNodeDescriptor(myglTF::Node* node, VkDescriptorSetLayout descriptorSetLayout)
{
	if (node->mesh) {
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
		descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocInfo.descriptorPool = descriptorPool;
		descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
		descriptorSetAllocInfo.descriptorSetCount = 1;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &node->mesh->uniformBuffer.descriptorSet));

		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = node->mesh->uniformBuffer.descriptorSet;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.pBufferInfo = &node->mesh->uniformBuffer.descriptor;

		vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}
	for (auto& child : node->children) {
		prepareNodeDescriptor(child, descriptorSetLayout);
	}
}

void myglTF::Texture::updateDescriptor()
{
	descriptor.sampler = sampler;
	descriptor.imageView = view;
	descriptor.imageLayout = imageLayout;
}

void myglTF::Texture::destroy()
{
	if (device)
	{
		vkDestroyImageView(device->logicalDevice, view, nullptr);
		vkDestroyImage(device->logicalDevice, image, nullptr);
		vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
		vkDestroySampler(device->logicalDevice, sampler, nullptr);
	}
}

void myglTF::Texture::fromglTfImage(tinygltf::Image& gltfimage, std::string path, vks::VulkanDevice* device,
	VkQueue copyQueue)
{
	this->device = device;

	bool isKtx = false;
	// Image points to an external ktx file
	if (gltfimage.uri.find_last_of(".") != std::string::npos) {
		if (gltfimage.uri.substr(gltfimage.uri.find_last_of(".") + 1) == "ktx") {
			isKtx = true;
		}
	}

	VkFormat format;

	if (!isKtx) {
		// Texture was loaded using STB_Image

		unsigned char* buffer = nullptr;
		VkDeviceSize bufferSize = 0;
		bool deleteBuffer = false;
		if (gltfimage.component == 3) {
			// Most devices don't support RGB only on Vulkan so convert if necessary
			// TODO: Check actual format support and transform only if required
			bufferSize = gltfimage.width * gltfimage.height * 4;
			buffer = new unsigned char[bufferSize];
			unsigned char* rgba = buffer;
			unsigned char* rgb = &gltfimage.image[0];
			for (size_t i = 0; i < gltfimage.width * gltfimage.height; ++i) {
				for (int32_t j = 0; j < 3; ++j) {
					rgba[j] = rgb[j];
				}
				rgba += 4;
				rgb += 3;
			}
			deleteBuffer = true;
		}
		else {
			buffer = &gltfimage.image[0];
			bufferSize = gltfimage.image.size();
		}
		assert(buffer);

		format = VK_FORMAT_R8G8B8A8_UNORM;

		VkFormatProperties formatProperties;

		width = gltfimage.width;
		height = gltfimage.height;
		mipLevels = static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

		vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);
		assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
		assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);

		VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		VkMemoryRequirements memReqs{};

		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = bufferSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));
		vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

		uint8_t* data{ nullptr };
		VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
		memcpy(data, buffer, bufferSize);
		vkUnmapMemory(device->logicalDevice, stagingMemory);

		VkImageCreateInfo imageCreateInfo{};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width, height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));
		vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

		VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;

		VkImageMemoryBarrier imageMemoryBarrier{};

		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageMemoryBarrier.srcAccessMask = 0;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;
		vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

		VkBufferImageCopy bufferCopyRegion = {};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = width;
		bufferCopyRegion.imageExtent.height = height;
		bufferCopyRegion.imageExtent.depth = 1;

		vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;
		vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

		device->flushCommandBuffer(copyCmd, copyQueue, true);

		vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
		vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);

		// Generate the mip chain (glTF uses jpg and png, so we need to create this manually)
		VkCommandBuffer blitCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		for (uint32_t i = 1; i < mipLevels; i++) {
			VkImageBlit imageBlit{};

			imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlit.srcSubresource.layerCount = 1;
			imageBlit.srcSubresource.mipLevel = i - 1;
			imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1));
			imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
			imageBlit.srcOffsets[1].z = 1;

			imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlit.dstSubresource.layerCount = 1;
			imageBlit.dstSubresource.mipLevel = i;
			imageBlit.dstOffsets[1].x = int32_t(width >> i);
			imageBlit.dstOffsets[1].y = int32_t(height >> i);
			imageBlit.dstOffsets[1].z = 1;

			VkImageSubresourceRange mipSubRange = {};
			mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			mipSubRange.baseMipLevel = i;
			mipSubRange.levelCount = 1;
			mipSubRange.layerCount = 1;

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = 0;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = mipSubRange;
				vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			vkCmdBlitImage(blitCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				imageMemoryBarrier.image = image;
				imageMemoryBarrier.subresourceRange = mipSubRange;
				vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}
		}

		subresourceRange.levelCount = mipLevels;
		imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;
		vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

		if (deleteBuffer) {
			delete[] buffer;
		}

		device->flushCommandBuffer(blitCmd, copyQueue, true);
	}
	else {
		// Texture is stored in an external ktx file
		std::string filename = path + "/" + gltfimage.uri;

		ktxTexture* ktxTexture;

		ktxResult result = KTX_SUCCESS;
#if defined(__ANDROID__)
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		if (!asset) {
			vks::tools::exitFatal("Could not load texture from " + filename + "\n\nMake sure the assets submodule has been checked out and is up-to-date.", -1);
		}
		size_t size = AAsset_getLength(asset);
		assert(size > 0);
		ktx_uint8_t* textureData = new ktx_uint8_t[size];
		AAsset_read(asset, textureData, size);
		AAsset_close(asset);
		result = ktxTexture_CreateFromMemory(textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
		delete[] textureData;
#else
		if (!vks::tools::fileExists(filename)) {
			vks::tools::exitFatal("Could not load texture from " + filename + "\n\nMake sure the assets submodule has been checked out and is up-to-date.", -1);
		}
		result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
#endif		
		assert(result == KTX_SUCCESS);

		this->device = device;
		width = ktxTexture->baseWidth;
		height = ktxTexture->baseHeight;
		mipLevels = ktxTexture->numLevels;

		ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
		ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);
		format = ktxTexture_GetVkFormat(ktxTexture);

		// Get device properties for the requested texture format
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);

		VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
		bufferCreateInfo.size = ktxTextureSize;
		// This buffer is used as a transfer source for the buffer copy
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

		uint8_t* data{ nullptr };
		VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
		memcpy(data, ktxTextureData, ktxTextureSize);
		vkUnmapMemory(device->logicalDevice, stagingMemory);

		std::vector<VkBufferImageCopy> bufferCopyRegions;
		for (uint32_t i = 0; i < mipLevels; i++)
		{
			ktx_size_t offset;
			KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0, &offset);
			assert(result == KTX_SUCCESS);
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = i;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = std::max(1u, ktxTexture->baseWidth >> i);
			bufferCopyRegion.imageExtent.height = std::max(1u, ktxTexture->baseHeight >> i);
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = offset;
			bufferCopyRegions.push_back(bufferCopyRegion);
		}

		// Create optimal tiled target image
		VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width, height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

		vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = mipLevels;
		subresourceRange.layerCount = 1;

		vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
		vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());
		vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		device->flushCommandBuffer(copyCmd, copyQueue);
		this->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
		vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);

		ktxTexture_Destroy(ktxTexture);
	}

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	samplerInfo.maxLod = (float)mipLevels;
	samplerInfo.maxAnisotropy = 8.0f;
	samplerInfo.anisotropyEnable = VK_TRUE;
	VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.layerCount = 1;
	viewInfo.subresourceRange.levelCount = mipLevels;
	VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &view));

	descriptor.sampler = sampler;
	descriptor.imageView = view;
	descriptor.imageLayout = imageLayout;
}

myglTF::Material::~Material()
{
	if (traditionalPipeline)
		vkDestroyPipeline(device->logicalDevice, traditionalPipeline, nullptr);
}

void myglTF::Material::createDescriptorSet(VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout,
                                           uint32_t descriptorBindingFlags)
{
	VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
	descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocInfo.descriptorPool = descriptorPool;
	descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
	descriptorSetAllocInfo.descriptorSetCount = 1;
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &descriptorSet));
	std::vector<VkDescriptorImageInfo> imageDescriptors{};
	std::vector<VkWriteDescriptorSet> writeDescriptorSets{};
	if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
		imageDescriptors.push_back(baseColorTexture->descriptor);
		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = descriptorSet;
		writeDescriptorSet.dstBinding = static_cast<uint32_t>(writeDescriptorSets.size());
		writeDescriptorSet.pImageInfo = &baseColorTexture->descriptor;
		writeDescriptorSets.push_back(writeDescriptorSet);
	}
	if (normalTexture && descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
		imageDescriptors.push_back(normalTexture->descriptor);
		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = descriptorSet;
		writeDescriptorSet.dstBinding = static_cast<uint32_t>(writeDescriptorSets.size());
		writeDescriptorSet.pImageInfo = &normalTexture->descriptor;
		writeDescriptorSets.push_back(writeDescriptorSet);
	}
	vkUpdateDescriptorSets(device->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void myglTF::Primitive::setDimensions(glm::vec3 min, glm::vec3 max)
{
	dimensions.min = min;
	dimensions.max = max;
	dimensions.size = max - min;
	dimensions.center = (min + max) / 2.0f;
	dimensions.radius = glm::distance(min, max) / 2.0f;
}

myglTF::Mesh::Mesh(vks::VulkanDevice* device, glm::mat4 matrix, bool createUniformBuffer, bool hasSkin)
{
	this->device = device;
	this->uniformBlock.matrix = matrix;

	if (createUniformBuffer == false)
		return;

	VkDeviceSize blockSize = hasSkin ? sizeof(UniformBlock) : sizeof(glm::mat4);
	VK_CHECK_RESULT(device->createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		blockSize,
		&uniformBuffer.buffer,
		&uniformBuffer.memory,
		&uniformBlock));
	VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, uniformBuffer.memory, 0, blockSize, 0, &uniformBuffer.mapped));
	uniformBuffer.descriptor = { uniformBuffer.buffer, 0, blockSize };

}

myglTF::Mesh::~Mesh()
{
	if (uniformBuffer.buffer)
	{
		vkDestroyBuffer(device->logicalDevice, uniformBuffer.buffer, nullptr);
		vkFreeMemory(device->logicalDevice, uniformBuffer.memory, nullptr);		
	}
	for (auto primitive : primitives)
	{
		delete primitive;
	}
}

glm::mat4 myglTF::Node::localMatrix()
{
	return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix;
}

glm::mat4 myglTF::Node::getMatrix()
{
	glm::mat4 m = localMatrix();
	myglTF::Node* p = parent;
	while (p) {
		m = p->localMatrix() * m;
		p = p->parent;
	}
	return m;
}

void myglTF::Node::update()
{
	if (mesh) {
		glm::mat4 m = getMatrix();
		if (skin) {
			mesh->uniformBlock.matrix = m;
			// Update join matrices
			glm::mat4 inverseTransform = glm::inverse(m);
			for (size_t i = 0; i < skin->joints.size(); i++) {
				myglTF::Node* jointNode = skin->joints[i];
				glm::mat4 jointMat = jointNode->getMatrix() * skin->inverseBindMatrices[i];
				jointMat = inverseTransform * jointMat;
				mesh->uniformBlock.jointMatrix[i] = jointMat;
			}
			mesh->uniformBlock.jointcount = (float)skin->joints.size();
			memcpy(mesh->uniformBuffer.mapped, &mesh->uniformBlock, sizeof(mesh->uniformBlock));
		}
		else {
			memcpy(mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
		}
	}

	for (auto& child : children) {
		child->update();
	}
}

myglTF::Node::~Node()
{
	if (mesh) {
		delete mesh;
	}
	for (auto& child : children) {
		delete child;
	}
}

myglTF::Texture* myglTF::Model::getTexture(uint32_t index)
{
	if (index < textures.size()) {
		return &textures[index];
	}
	return nullptr;
}

void myglTF::Model::createEmptyTexture(VkQueue transferQueue)
{
	emptyTexture.device = device;
	emptyTexture.width = 1;
	emptyTexture.height = 1;
	emptyTexture.layerCount = 1;
	emptyTexture.mipLevels = 1;

	size_t bufferSize = emptyTexture.width * emptyTexture.height * 4;
	unsigned char* buffer = new unsigned char[bufferSize];
	memset(buffer, 0, bufferSize);

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;
	VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
	bufferCreateInfo.size = bufferSize;
	// This buffer is used as a transfer source for the buffer copy
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

	VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
	VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

	// Copy texture data into staging buffer
	uint8_t* data{ nullptr };
	VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
	memcpy(data, buffer, bufferSize);
	vkUnmapMemory(device->logicalDevice, stagingMemory);

	VkBufferImageCopy bufferCopyRegion = {};
	bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	bufferCopyRegion.imageSubresource.layerCount = 1;
	bufferCopyRegion.imageExtent.width = emptyTexture.width;
	bufferCopyRegion.imageExtent.height = emptyTexture.height;
	bufferCopyRegion.imageExtent.depth = 1;

	// Create optimal tiled target image
	VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCreateInfo.extent = { emptyTexture.width, emptyTexture.height, 1 };
	imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &emptyTexture.image));

	vkGetImageMemoryRequirements(device->logicalDevice, emptyTexture.image, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &emptyTexture.deviceMemory));
	VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, emptyTexture.image, emptyTexture.deviceMemory, 0));

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.layerCount = 1;

	VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	vks::tools::setImageLayout(copyCmd, emptyTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
	vkCmdCopyBufferToImage(copyCmd, stagingBuffer, emptyTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
	vks::tools::setImageLayout(copyCmd, emptyTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
	device->flushCommandBuffer(copyCmd, transferQueue);
	emptyTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// Clean up staging resources
	vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);

	VkSamplerCreateInfo samplerCreateInfo = vks::initializers::samplerCreateInfo();
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerCreateInfo.maxAnisotropy = 1.0f;
	VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &emptyTexture.sampler));

	VkImageViewCreateInfo viewCreateInfo = vks::initializers::imageViewCreateInfo();
	viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	viewCreateInfo.subresourceRange.levelCount = 1;
	viewCreateInfo.image = emptyTexture.image;
	VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &emptyTexture.view));

	emptyTexture.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	emptyTexture.descriptor.imageView = emptyTexture.view;
	emptyTexture.descriptor.sampler = emptyTexture.sampler;
}


void myglTF::Model::generateMeshlets(const std::vector<VertexType*>& originalVertices, const std::vector<uint32_t>& originalIndices, std::vector<uint32_t>&
                                     outMeshletVertices, std::vector<uint32_t>& outMeshletPackedTriangles, meshopt_Meshlet** outMeshlets, uint32_t&
                                     outNumMeshlets)
{
	// Strongly influenced by DirectX-Graphics-Samples https://github.com/microsoft/directx-graphics-samples/tree/master/Samples/Desktop/D3D12MeshShaders

	std::vector<glm::vec3> vertexPositions;
	std::vector<meshopt_Meshlet> meshlets;
	std::vector<uint8_t> meshletTriangles; // meshletTriangle means 3 indices for meshletVertex

	if (originalVertices.empty() || originalIndices.empty())
	{
		vks::tools::exitFatal("Geometry Infos in CPU are empty", -1);
		return;
	}

	// Fill meshletVertices, meshletTriangles
	{
		// Fill vertexPositions vector
		for (const auto& vertex : originalVertices)
		{
			vertexPositions.emplace_back(vertex->pos);
		}

		size_t numVertices = originalVertices.size();
		size_t numIdices = originalIndices.size();

		const size_t kMaxVertices = 64; // max num of vertices MeshShader Output
		const size_t kMaxTriangles = 124; // max num of triangles MeshShader Output
		const float  kConeWeight = 0.0f;

		const size_t maxMeshlets = meshopt_buildMeshletsBound(numIdices, kMaxVertices, kMaxTriangles);

		meshlets.resize(maxMeshlets);
		outMeshletVertices.resize(maxMeshlets * kMaxVertices);
		meshletTriangles.resize(maxMeshlets * kMaxTriangles * 3);

		size_t meshletCount = meshopt_buildMeshlets(
			meshlets.data(),
			outMeshletVertices.data(),
			meshletTriangles.data(),
			reinterpret_cast<const uint32_t*>(originalIndices.data()), // Original Indices
			numIdices,
			reinterpret_cast<const float*>(vertexPositions.data()), // Position of vertex - Optimizer Only needs position info
			numVertices,
			sizeof(glm::vec3),
			kMaxVertices,
			kMaxTriangles,
			kConeWeight);

		const meshopt_Meshlet& lastMeshlet = meshlets[meshletCount - 1];
		outMeshletVertices.resize(lastMeshlet.vertex_offset + lastMeshlet.vertex_count);
		// make meshlet indices(triangles) aligned to 4 bytes
		meshletTriangles.resize(lastMeshlet.triangle_offset + ((lastMeshlet.triangle_count * 3 + 3) & ~3));
		meshlets.resize(meshletCount);
	}

	// generate packed triangle
	for (auto& m : meshlets)
	{
		// Save triangle offset for current meshlet
		uint32_t triangleOffset = static_cast<uint32_t>(outMeshletPackedTriangles.size());

		// Repack to uint32_t
		for (uint32_t i = 0; i < m.triangle_count; ++i)
		{
			uint32_t i0 = 3 * i + 0 + m.triangle_offset;
			uint32_t i1 = 3 * i + 1 + m.triangle_offset;
			uint32_t i2 = 3 * i + 2 + m.triangle_offset;

			uint8_t  vIdx0 = meshletTriangles[i0];
			uint8_t  vIdx1 = meshletTriangles[i1];
			uint8_t  vIdx2 = meshletTriangles[i2];
			uint32_t packed = ((static_cast<uint32_t>(vIdx0) & 0xFF) << 0) |
				((static_cast<uint32_t>(vIdx1) & 0xFF) << 8) |
				((static_cast<uint32_t>(vIdx2) & 0xFF) << 16);
			outMeshletPackedTriangles.push_back(packed);
		}

		// Update triangle offset for current meshlet
		m.triangle_offset = triangleOffset;
	}

	// move meshlets to outParam
	outNumMeshlets = static_cast<uint32_t>(meshlets.size());
	*outMeshlets = new meshopt_Meshlet[meshlets.size()]; // released after buffer created
	std::move(meshlets.begin(), meshlets.end(), *outMeshlets);
}

//-----------------------------

void myglTF::ModelRT::initClusters(std::vector<uint32_t>& originalIndices, const std::vector<glm::vec3>& vertexPositions)
{
	// Do Cluster things - Strongly influenced by https://github.com/nvpro-samples/vk_animated_clusters
	uint32_t clusterTriangles = 64;
	uint32_t clusterVertices = 64;
	size_t minTriangles = (clusterTriangles / 4) & ~3; // allow smaller clusters to be generated when that significantly improves their bounds
	size_t maxVerticesPerMeshlet = clusterVertices; // Same for MeshShader
	size_t maxIndicesPerMeshlet = minTriangles; // If MeshShader:124
	float clusterMeshoptSpatialFill = 0.5f;

	// build geometry clusters - Use MeshOptimizer(https://github.com/zeux/meshoptimizer)
	{
		std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(originalIndices.size(), 64, minTriangles));

		tempCusterLocalIndicesCPU.resize(meshlets.size() * clusterTriangles * 3);
		tempClusterLocalVerticesCPU.resize(meshlets.size() * clusterVertices);

		size_t numClusters = meshopt_buildMeshletsSpatial(
			meshlets.data(),
			tempClusterLocalVerticesCPU.data(),
			tempCusterLocalIndicesCPU.data(),
			originalIndices.data(),
			originalIndices.size(),
			reinterpret_cast<const float*>(vertexPositions.data()),
			vertexPositions.size(),
			sizeof(glm::vec3),
			std::min(255u, clusterVertices),
			minTriangles,
			clusterTriangles,
			clusterMeshoptSpatialFill);

		m_numClusters = static_cast<uint32_t>(numClusters);

		if (m_numClusters)
		{
			tempClustersCPU.resize(m_numClusters);
			tempClustersCPU.shrink_to_fit();

			// Fill Cluster Data
			uint64_t clusterIdx = 0;
			for (; clusterIdx < numClusters; ++clusterIdx)
			{
				meshopt_Meshlet& meshlet = meshlets[clusterIdx];
				ClusterRT& cluster = tempClustersCPU[clusterIdx];
				cluster = {};
				cluster.numTriangles = static_cast<uint16_t>(meshlet.triangle_count);
				cluster.numVertices = static_cast<uint16_t>(meshlet.vertex_count);
				cluster.firstLocalTriangle = meshlet.triangle_offset;
				cluster.firstLocalVertex = meshlet.vertex_offset;
			}

			ClusterRT& lastCluster = tempClustersCPU[clusterIdx - 1];
			tempCusterLocalIndicesCPU.resize(lastCluster.firstLocalTriangle + lastCluster.numTriangles * 3);
			tempClusterLocalVerticesCPU.resize(lastCluster.firstLocalVertex + lastCluster.numVertices);
			tempCusterLocalIndicesCPU.shrink_to_fit();
			tempClusterLocalVerticesCPU.shrink_to_fit();
			m_numClusterVertices = static_cast<uint32_t>(tempClusterLocalVerticesCPU.size());
		}
	}

	// Fill Cluster BBoxes Data
	{
		tempClusterBBoxesCPU.resize(m_numClusters);
		for (uint64_t clusterIdx = 0; clusterIdx < tempClustersCPU.size(); ++clusterIdx)
		{
			ClusterRT& cluster = tempClustersCPU[clusterIdx];
			BBox bbox = { {FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX} };
			for (uint32_t vertexLocalIdx = 0; vertexLocalIdx < cluster.numVertices; ++vertexLocalIdx)
			{
				uint32_t  vertexGlobalIdx = tempClusterLocalVerticesCPU[cluster.firstLocalVertex + vertexLocalIdx];
				glm::vec3 pos = vertexPositions[vertexGlobalIdx];

				bbox.min = glm::min(bbox.min, pos);
				bbox.max = glm::max(bbox.max, pos);
			}
			tempClusterBBoxesCPU[clusterIdx] = bbox;
		}
	}
	// Re-order Global(Model's) Index Array in order of Clusters
	{
		for (uint64_t clusterIdx = 0; clusterIdx < tempClustersCPU.size(); ++clusterIdx)
		{
			ClusterRT& cluster = tempClustersCPU[clusterIdx];
			for (uint32_t t = 0; t < cluster.numTriangles; ++t) // per triangle in Cluster
			{
				// cur triangle in clusrter
				glm::uvec3 localVertices = { tempCusterLocalIndicesCPU[cluster.firstLocalTriangle + t * 3 + 0],
											tempCusterLocalIndicesCPU[cluster.firstLocalTriangle + t * 3 + 1],
											tempCusterLocalIndicesCPU[cluster.firstLocalTriangle + t * 3 + 2] };

				assert(localVertices.x < cluster.numVertices);
				assert(localVertices.y < cluster.numVertices);
				assert(localVertices.z < cluster.numVertices);

				glm::uvec3 globalVertices;

				if (true) // !m_config.clusterDedicatedVertices from scene.cpp(https://github.com/nvpro-samples/vk_animated_clusters/blob/main/src/scene.cpp)
				{
					// need one more indirection
					globalVertices = { tempClusterLocalVerticesCPU[globalVertices.x], tempClusterLocalVerticesCPU[globalVertices.y],
									  tempClusterLocalVerticesCPU[globalVertices.z] };
				}
				else
				{
					globalVertices = { localVertices.x + cluster.firstLocalVertex, localVertices.y + cluster.firstLocalVertex,
											  localVertices.z + cluster.firstLocalVertex };
				}

				originalIndices[cluster.firstTriangle + t + 0] = globalVertices.x;
				originalIndices[cluster.firstTriangle + t + 1] = globalVertices.y;
				originalIndices[cluster.firstTriangle + t + 2] = globalVertices.z;
			}
		}
	}
}

myglTF::ModelRT::~ModelRT()
{
	CleanBufferMemory(primitives);
	CleanBufferMemory(geometryNodes);
	CleanBufferMemory(clusterVerticesGPU);
	CleanBufferMemory(clusterIndicesGPU);
	CleanBufferMemory(clusterBBoxesGPU);
	CleanBufferMemory(clustersGPU);
	CleanBufferMemory(vertices);
	CleanBufferMemory(indices);


	vkDestroyBuffer(device->logicalDevice, rootUniformBuffer.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, rootUniformBuffer.memory, nullptr);

	for (auto& texture : textures) {
		texture.destroy();
	}
	for (auto& node : nodes) {
		delete node;
	}
	for (auto& skin : skins) {
		delete skin;
	}
	if (descriptorSetLayoutUbo != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayoutUbo, nullptr);
		descriptorSetLayoutUbo = VK_NULL_HANDLE;
	}
	if (descriptorSetLayoutImage != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(device->logicalDevice, descriptorSetLayoutImage, nullptr);
		descriptorSetLayoutImage = VK_NULL_HANDLE;
	}

	vkDestroyDescriptorPool(device->logicalDevice, descriptorPool, nullptr);
	emptyTexture.destroy();
}

void myglTF::ModelRT::loadNode(myglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex,
	const tinygltf::Model& model, std::vector<uint32_t>& indexBuffer, std::vector<VertexType*>& vertices,
	float globalscale)
{
	myglTF::Node* newNode = new Node{};
	newNode->index = nodeIndex;
	newNode->parent = parent;
	newNode->name = node.name;
	newNode->skinIndex = node.skin;
	newNode->matrix = glm::mat4(1.0f);

	// Generate local node matrix
	glm::vec3 translation = glm::vec3(0.0f);
	if (node.translation.size() == 3) {
		translation = glm::make_vec3(node.translation.data());
		newNode->translation = translation;
	}
	glm::mat4 rotation = glm::mat4(1.0f);
	if (node.rotation.size() == 4) {
		glm::quat q = glm::make_quat(node.rotation.data());
		newNode->rotation = glm::mat4(q);
	}
	glm::vec3 scale = glm::vec3(1.0f);
	if (node.scale.size() == 3) {
		scale = glm::make_vec3(node.scale.data());
		newNode->scale = scale;
	}
	if (node.matrix.size() == 16) {
		newNode->matrix = glm::make_mat4x4(node.matrix.data());
		if (globalscale != 1.0f) {
			//newNode->matrix = glm::scale(newNode->matrix, glm::vec3(globalscale));
		}
	};

	// Node with children
	if (node.children.size() > 0) {
		for (auto i = 0; i < node.children.size(); i++) {
			loadNode(newNode, model.nodes[node.children[i]], node.children[i], model, indexBuffer, vertices, globalscale);
		}
	}

	// Node contains mesh data
	if (node.mesh > -1) {
		const tinygltf::Mesh mesh = model.meshes[node.mesh];
		Mesh* newMesh = new Mesh(device, newNode->matrix, !preTransform, newNode->skin);
		newMesh->name = mesh.name;
		for (size_t j = 0; j < mesh.primitives.size(); j++) {
			const tinygltf::Primitive& primitive = mesh.primitives[j];
			if (primitive.indices < 0) {
				continue;
			}
			uint32_t indexStart = static_cast<uint32_t>(indexBuffer.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertices.size());
			uint32_t indexCount = 0;
			uint32_t vertexCount = 0;
			glm::vec3 posMin{};
			glm::vec3 posMax{};
			bool hasSkin = false;
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

				const tinygltf::Accessor& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
				const tinygltf::BufferView& posView = model.bufferViews[posAccessor.bufferView];
				bufferPos = reinterpret_cast<const float*>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
				posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
				posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

				if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
					const tinygltf::Accessor& normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView& normView = model.bufferViews[normAccessor.bufferView];
					bufferNormals = reinterpret_cast<const float*>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
				}

				if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
					bufferTexCoords = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
				{
					const tinygltf::Accessor& colorAccessor = model.accessors[primitive.attributes.find("COLOR_0")->second];
					const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor.bufferView];
					// Color buffer are either of type vec3 or vec4
					numColorComponents = colorAccessor.type == TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 ? 3 : 4;
					bufferColors = reinterpret_cast<const float*>(&(model.buffers[colorView.buffer].data[colorAccessor.byteOffset + colorView.byteOffset]));
				}

				if (primitive.attributes.find("TANGENT") != primitive.attributes.end())
				{
					const tinygltf::Accessor& tangentAccessor = model.accessors[primitive.attributes.find("TANGENT")->second];
					const tinygltf::BufferView& tangentView = model.bufferViews[tangentAccessor.bufferView];
					bufferTangents = reinterpret_cast<const float*>(&(model.buffers[tangentView.buffer].data[tangentAccessor.byteOffset + tangentView.byteOffset]));
				}

				// Skinning
				// Joints
				if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& jointAccessor = model.accessors[primitive.attributes.find("JOINTS_0")->second];
					const tinygltf::BufferView& jointView = model.bufferViews[jointAccessor.bufferView];
					bufferJoints = reinterpret_cast<const uint16_t*>(&(model.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]));
				}

				if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor& uvAccessor = model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
					const tinygltf::BufferView& uvView = model.bufferViews[uvAccessor.bufferView];
					bufferWeights = reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				hasSkin = (bufferJoints && bufferWeights);

				vertexCount = static_cast<uint32_t>(posAccessor.count);

				for (size_t v = 0; v < posAccessor.count; v++) {
					/*
					 * if skin:VertexSkiniing, else: VertexSimple
					 * allocated in here, released in "loadfromfile()"
					 * pushed into param::vertexBuffer
					 */
					VertexType* vert = hasSkin ? new VertexSkinning{} : new VertexSimple{};

					vert->pos = glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
					//if (bool preTransform) // apply node's transform to vertices while loading
					//{
					//	vert->pos = newNode->getMatrix() * glm::vec4(vert->pos, 1.f);
					//}

					vert->normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
					vert->uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
					if (bufferColors) {
						switch (numColorComponents) {
						case 3:
							vert->color = glm::vec4(glm::make_vec3(&bufferColors[v * 3]), 1.0f);
							break;
						case 4:
							vert->color = glm::make_vec4(&bufferColors[v * 4]);
							break;
						}
					}
					else {
						vert->color = glm::vec4(1.0f);
					}
					vert->tangent = bufferTangents ? glm::vec4(glm::make_vec4(&bufferTangents[v * 4])) : glm::vec4(0.0f);
					if (hasSkin)
					{
						static_cast<VertexSkinning*>(vert)->joint0 = glm::vec4(glm::make_vec4(&bufferJoints[v * 4]));
						static_cast<VertexSkinning*>(vert)->weight0 = glm::vec4(glm::make_vec4(&bufferWeights[v * 4]));
					}
					vertices.push_back(vert);
				}
			}
			// Indices
			{
				const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
				const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

				indexCount = static_cast<uint32_t>(accessor.count);

				switch (accessor.componentType) {
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
					uint32_t* buf = new uint32_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
					uint16_t* buf = new uint16_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
					uint8_t* buf = new uint8_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				default:
					std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
					return;
				}
			}
			Primitive* newPrimitive = new Primitive(indexStart, indexCount, primitive.material > -1 ? materials[primitive.material] : materials.back());
			newPrimitive->firstVertex = vertexStart;
			newPrimitive->vertexCount = vertexCount;
			newPrimitive->setDimensions(posMin, posMax);
			newMesh->primitives.push_back(newPrimitive);
		}
		newNode->mesh = newMesh;
	}
	if (parent) {
		parent->children.push_back(newNode);
	}
	else {
		nodes.push_back(newNode);
	}
	linearNodes.push_back(newNode);
}

void myglTF::ModelRT::loadSkins(tinygltf::Model& gltfModel)
{
	for (tinygltf::Skin& source : gltfModel.skins) {
		Skin* newSkin = new Skin{};
		newSkin->name = source.name;

		// Find skeleton root node
		if (source.skeleton > -1) {
			newSkin->skeletonRoot = nodeFromIndex(source.skeleton);
		}

		// Find joint nodes
		for (int jointIndex : source.joints) {
			Node* node = nodeFromIndex(jointIndex);
			if (node) {
				newSkin->joints.push_back(nodeFromIndex(jointIndex));
			}
		}

		// Get inverse bind matrices from buffer
		if (source.inverseBindMatrices > -1) {
			const tinygltf::Accessor& accessor = gltfModel.accessors[source.inverseBindMatrices];
			const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];
			newSkin->inverseBindMatrices.resize(accessor.count);
			memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
		}

		skins.push_back(newSkin);
	}
}

void myglTF::ModelRT::loadImages(tinygltf::Model& gltfModel, vks::VulkanDevice* device, VkQueue transferQueue)
{
	for (tinygltf::Image& image : gltfModel.images) {
		myglTF::Texture texture;
		texture.fromglTfImage(image, path, device, transferQueue);
		texture.index = static_cast<uint32_t>(textures.size());
		textures.push_back(texture);
	}
	// Create an empty texture to be used for empty material images
	createEmptyTexture(transferQueue);
}

void myglTF::ModelRT::loadMaterials(tinygltf::Model& gltfModel)
{
	for (tinygltf::Material& mat : gltfModel.materials) {
		myglTF::Material material(device);
		if (mat.values.find("baseColorTexture") != mat.values.end()) {
			material.baseColorTexture = getTexture(gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source);
		}
		// Metallic roughness workflow
		if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
			material.metallicRoughnessTexture = getTexture(gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source);
		}
		if (mat.values.find("roughnessFactor") != mat.values.end()) {
			material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
		}
		if (mat.values.find("metallicFactor") != mat.values.end()) {
			material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
		}
		if (mat.values.find("baseColorFactor") != mat.values.end()) {
			material.baseColorFactor = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
		}
		if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
			material.normalTexture = getTexture(gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source);
		}
		else {
			material.normalTexture = &emptyTexture;
		}
		if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
			material.emissiveTexture = getTexture(gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
			material.occlusionTexture = getTexture(gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
			tinygltf::Parameter param = mat.additionalValues["alphaMode"];
			if (param.string_value == "BLEND") {
				material.alphaMode = myglTF::Material::ALPHAMODE_BLEND;
			}
			if (param.string_value == "MASK") {
				material.alphaMode = myglTF::Material::ALPHAMODE_MASK;
			}
		}
		if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
			material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
		}

		materials.push_back(material);
	}
	// Push a default material at the end of the list for meshes with no material assigned
	materials.push_back(myglTF::Material(device));
}

void myglTF::ModelRT::loadAnimations(tinygltf::Model& gltfModel)
{
	for (tinygltf::Animation& anim : gltfModel.animations) {
		myglTF::Animation animation{};
		animation.name = anim.name;
		if (anim.name.empty()) {
			animation.name = std::to_string(animations.size());
		}

		// Samplers
		for (auto& samp : anim.samplers) {
			myglTF::AnimationSampler sampler{};

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
				const tinygltf::Accessor& accessor = gltfModel.accessors[samp.input];
				const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

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
				const tinygltf::Accessor& accessor = gltfModel.accessors[samp.output];
				const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

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
			myglTF::AnimationChannel channel{};

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
			channel.node = nodeFromIndex(source.target_node);
			if (!channel.node) {
				continue;
			}

			animation.channels.push_back(channel);
		}

		animations.push_back(animation);
	}
}

void myglTF::ModelRT::loadFromFile(std::string filename, vks::VulkanDevice* device, VkQueue transferQueue,
	uint32_t fileLoadingFlags, float scale)
{
	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF gltfContext;
	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = MyDeviceFuncTable::Get()->vkGetBufferDeviceAddressKHR;
	// flag things
	const bool isGeometryNodePerPrimitive = fileLoadingFlags & myglTF::FileLoadingFlags::GeometryNodePerPrimitive;
	const bool bMakeClusters = fileLoadingFlags & myglTF::FileLoadingFlags::MakeClusters;
	auto getBufferDeviceAddress = [&](VkBuffer buffer)
	{
		VkBufferDeviceAddressInfoKHR bufferDeviceAI{};
		bufferDeviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		bufferDeviceAI.buffer = buffer;
		return vkGetBufferDeviceAddressKHR(device->logicalDevice, &bufferDeviceAI);
	};

	if (fileLoadingFlags & FileLoadingFlags::DontLoadImages) {
		gltfContext.SetImageLoader(loadImageDataFuncEmpty, nullptr);
	}
	else {
		gltfContext.SetImageLoader(loadImageDataFunc, nullptr);
	}
#if defined(__ANDROID__)
	// On Android all assets are packed with the apk in a compressed form, so we need to open them using the asset manager
	// We let tinygltf handle this, by passing the asset manager of our app
	tinygltf::asset_manager = androidApp->activity->assetManager;
#endif
	size_t pos = filename.find_last_of('/');
	path = filename.substr(0, pos);

	std::string error, warning;

	this->device = device;

#if defined(__ANDROID__)
	// On Android all assets are packed with the apk in a compressed form, so we need to open them using the asset manager
	// We let tinygltf handle this, by passing the asset manager of our app
	tinygltf::asset_manager = androidApp->activity->assetManager;
#endif
	bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename);
	std::vector<VertexType*> tempVerticesCPU;
	std::vector<uint32_t> tempIndicesCPU;
	bool isSkinningModel = gltfModel.skins.size() > 0;
	preTransform = fileLoadingFlags & FileLoadingFlags::PreTransformVertices;
	uint32_t vertexSize = isSkinningModel ? sizeof(VertexSkinning) : sizeof(VertexSimple);
	if (fileLoaded) {
		if (!(fileLoadingFlags & FileLoadingFlags::DontLoadImages)) {
			loadImages(gltfModel, device, transferQueue);
		}
		loadMaterials(gltfModel);
		const tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
		for (size_t i = 0; i < scene.nodes.size(); i++) {
			const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
			loadNode(nullptr, node, scene.nodes[i], gltfModel, tempIndicesCPU, tempVerticesCPU, scale);
		}
		if (gltfModel.animations.size() > 0) {
			loadAnimations(gltfModel);
		}

		loadSkins(gltfModel);
		for (auto node : linearNodes) {
			// Assign skins
			if (node->skinIndex > -1) {
				node->skin = skins[node->skinIndex];
			}
			// Initial pose
			if (preTransform == false && node->mesh) {
				node->update();
			}
		}
	}
	else {
		vks::tools::exitFatal("Could not load glTF file \"" + filename + "\": " + error, -1);
		return;
	}

	// Pre-Calculations for requested features
	if ((fileLoadingFlags & FileLoadingFlags::PreTransformVertices) || (fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors) || (fileLoadingFlags & FileLoadingFlags::FlipY)) {

		const bool preMultiplyColor = fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors;
		const bool flipY = fileLoadingFlags & FileLoadingFlags::FlipY;
		for (Node* node : linearNodes) {
			if (node->mesh) {
				const glm::mat4 localMatrix = node->getMatrix();
				for (Primitive* primitive : node->mesh->primitives) {
					for (uint32_t i = 0; i < primitive->vertexCount; i++) {
						VertexType* vertex = tempVerticesCPU[primitive->firstVertex + i];
						// Pre-transform vertex positions by node-hierarchy
						if (preTransform) {
							vertex->pos = glm::vec3(localMatrix * glm::vec4(vertex->pos, 1.0f));
							vertex->normal = glm::normalize(glm::mat3(localMatrix) * vertex->normal);
						}
						// Flip Y-Axis of vertex positions
						if (flipY) {
							vertex->pos.y *= -1.0f;
							vertex->normal.y *= -1.0f;
						}
						// Pre-Multiply vertex colors with material base color
						if (preMultiplyColor) {
							vertex->color = primitive->material.baseColorFactor * vertex->color;
						}
					}
				}
			}
		}
	}

	for (auto& extension : gltfModel.extensionsUsed) {
		if (extension == "KHR_materials_pbrSpecularGlossiness") {
			std::cout << "Required extension: " << extension;
			metallicRoughnessWorkflow = false;
		}
	}
	// A vector used to store vertex data in byte form, becuase of two different type vertex(VertexSimple/VertexSkinning)
	std::vector<byte> vertexBufferByte(vertexSize * tempVerticesCPU.size());
	uint64_t byteOffset = 0;
	for (VertexType*& vertex : tempVerticesCPU)
	{
		memcpy(&vertexBufferByte[byteOffset], vertex, vertexSize);
		byteOffset += vertexSize;
	}

	// reorder index array for better cache hit
	meshopt_optimizeVertexCache(tempIndicesCPU.data(), tempIndicesCPU.data(), tempIndicesCPU.size(), tempVerticesCPU.size());

	size_t vertexBufferSize = tempVerticesCPU.size() * vertexSize;
	size_t indexBufferSize = tempIndicesCPU.size() * sizeof(uint32_t);
	indices.count = static_cast<uint32_t>(tempIndicesCPU.size());
	vertices.count = static_cast<uint32_t>(tempVerticesCPU.size());
	//uint32_t numTriangles = indices.count / 3;

	// Do Cluster Things
	if (bMakeClusters)
	{
		for (auto& node : linearNodes)
		{
			if (node->mesh)
			{
				std::vector<glm::vec3> vertexPositions;
				for (const auto& vertex : tempVerticesCPU) // Fill vertexPositions vector
				{
					vertexPositions.emplace_back(vertex->pos);
				}
				initClusters(tempIndicesCPU, vertexPositions);
			}
		}		
	}

	getSceneDimensions();


	struct StagingBuffer {
		VkBuffer buffer;
		VkDeviceMemory memory;
	} vertexStaging{}, indexStaging{}, clusterVertexStaging{}, clusterIndexStaging{}, clusterBBoxStaging{}, clusterStaging{}, geometryNodeStaging{}, primitiveStaging{};

	// Create Vertex/Index buffer First
	// Vertex data
	VK_CHECK_RESULT(device->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		vertexBufferSize,
		&vertexStaging.buffer,
		&vertexStaging.memory,
		vertexBufferByte.data()));
	// Index data
	VK_CHECK_RESULT(device->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		indexBufferSize,
		&indexStaging.buffer,
		&indexStaging.memory,
		tempIndicesCPU.data()));
	// Vertex buffer
	VK_CHECK_RESULT(device->createBuffer2(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		vertexBufferSize,
		&vertices.buffer,
		&vertices.memory));
	// Index buffer
	VK_CHECK_RESULT(device->createBuffer2(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
		| VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		indexBufferSize,
		&indices.buffer,
		&indices.memory));
	// Copy from staging buffers
	VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	VkBufferCopy copyRegion = {};

	copyRegion.size = vertexBufferSize;
	vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, vertices.buffer, 1, &copyRegion);
	copyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(copyCmd, indexStaging.buffer, indices.buffer, 1, &copyRegion);
	device->flushCommandBuffer(copyCmd, transferQueue, false);



	// Process Raytracing Geometrynode per primitive or mesh
	uint32_t primitiveStartOffset = 0;
	uint32_t vertexStartOffset = 0;
	uint32_t indexStartOffset = 0;
	std::vector<MeshPrimitive> tempPrimitives; // for GeometryNodePerMesh
	
	for (auto& node : linearNodes)
	{
		uint32_t vertexCountInMesh = 0u;
		if (node->mesh)
		{
			uint32_t vertexStartOffsetInMesh = 0u;
			uint32_t indexStartOffsetInMesh = 0u;
			if (isGeometryNodePerPrimitive)
			{
				for (const auto& primitive : node->mesh->primitives)
				{
					GeometryNodePerPrimitiveRT geometryNode{};
					VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
					VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
					vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(vertices.buffer);// bindless vertices
					indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(indices.buffer) + primitive->firstIndex * sizeof(uint32_t);
					geometryNode.vertexBufferDeviceAddress = vertexBufferDeviceAddress.deviceAddress;
					geometryNode.indexBufferDeviceAddress = indexBufferDeviceAddress.deviceAddress;
					geometryNode.textureIndexBaseColor = primitive->material.baseColorTexture->index;
					geometryNode.textureIndexOcclusion = primitive->material.occlusionTexture ? primitive->material.occlusionTexture->index : -1;
					geometryNodesPerPrimitive.push_back(geometryNode);
				}
			}
			else if (fileLoadingFlags & myglTF::FileLoadingFlags::GeometryNodePerMesh)
			{				
				GeometryNodePerMeshRT geometryNode{};
				geometryNode.vertexStartOffset = vertexStartOffset;
				geometryNode.indexStartOffset = indexStartOffset;
				geometryNode.primitiveStartOffset = primitiveStartOffset;

				for (const auto& primitive : node->mesh->primitives)
				{
					const Material& material = primitive->material;
					MeshPrimitive primitiveRT{};
					primitiveRT.textureIndexBaseColor = static_cast<int32_t>(material.baseColorTexture->index);
					primitiveRT.textureIndexOcclusion = primitive->material.occlusionTexture ? material.occlusionTexture->index : -1;
					primitiveRT.vertexStartOffsetInMesh = vertexStartOffsetInMesh;
					primitiveRT.IndexStartOffsetInMesh = indexStartOffsetInMesh;
					tempPrimitives.push_back(primitiveRT); ++primitiveStartOffset;
					vertexStartOffset += primitive->vertexCount;
					indexStartOffset += primitive->indexCount;
					vertexStartOffsetInMesh += primitive->vertexCount;
					indexStartOffsetInMesh += primitive->indexCount;
				}
				geometryNodesPerMesh.push_back(geometryNode);
			}
		}
	}


	// Create staging buffers
	if (bMakeClusters)
	{
		size_t clusterVertexBufferSize = m_numClusterVertices * sizeof(uint32_t);
		size_t clusterIndexBufferSize = tempCusterLocalIndicesCPU.size() * sizeof(uint8_t);
		size_t clusterBBoxBufferSize = tempClusterBBoxesCPU.size() * sizeof(BBox);
		size_t clusterBufferSize = m_numClusters * sizeof(ClusterRT);
		// Staging Buffer - Cluster Vertex
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			clusterVertexBufferSize,
			&clusterVertexStaging.buffer,
			&clusterVertexStaging.memory,
			tempClusterLocalVerticesCPU.data()));
		// Staging Buffer - Cluster Index
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			clusterIndexBufferSize,
			&clusterIndexStaging.buffer,
			&clusterIndexStaging.memory,
			tempCusterLocalIndicesCPU.data()));
		// Staging Buffer - Cluster BBox
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			clusterBBoxBufferSize,
			&clusterBBoxStaging.buffer,
			&clusterBBoxStaging.memory,
			tempClusterBBoxesCPU.data()));
		// Staging Buffer - Cluster
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			clusterBufferSize,
			&clusterStaging.buffer,
			&clusterStaging.memory,
			tempClustersCPU.data()));

		// Cluster Vertex buffer
		VK_CHECK_RESULT(device->createBuffer2(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			clusterVertexBufferSize,
			&clusterVerticesGPU.buffer,
			&clusterVerticesGPU.memory));
		// Cluster Index buffer
		VK_CHECK_RESULT(device->createBuffer2(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			clusterIndexBufferSize,
			&clusterIndicesGPU.buffer,
			&clusterIndicesGPU.memory));
		// Cluster BBox buffer
		VK_CHECK_RESULT(device->createBuffer2(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			clusterBBoxBufferSize,
			&clusterBBoxesGPU.buffer,
			&clusterBBoxesGPU.memory));
		// Cluster buffer
		VK_CHECK_RESULT(device->createBuffer2(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			clusterBufferSize,
			&clustersGPU.buffer,
			&clustersGPU.memory));


		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

		copyRegion.size = clusterVertexBufferSize;
		vkCmdCopyBuffer(copyCmd, clusterVertexStaging.buffer, clusterVerticesGPU.buffer, 1, &copyRegion);
		copyRegion.size = clusterIndexBufferSize;
		vkCmdCopyBuffer(copyCmd, clusterIndexStaging.buffer, clusterIndicesGPU.buffer, 1, &copyRegion);
		copyRegion.size = clusterBBoxBufferSize;
		vkCmdCopyBuffer(copyCmd, clusterBBoxStaging.buffer, clusterBBoxesGPU.buffer, 1, &copyRegion);
		copyRegion.size = clusterBufferSize;
		vkCmdCopyBuffer(copyCmd, clusterStaging.buffer, clustersGPU.buffer, 1, &copyRegion);

		device->flushCommandBuffer(copyCmd, transferQueue, false);
	}
	size_t geometryNodeBufferSize = isGeometryNodePerPrimitive ? geometryNodesPerPrimitive.size() * sizeof(GeometryNodePerPrimitiveRT) :
		geometryNodesPerMesh.size() * sizeof(GeometryNodePerMeshRT);

	// Staging Buffer - GeometryNodes
	void* geometryNodesData = isGeometryNodePerPrimitive ? (void*)geometryNodesPerPrimitive.data() : (void*)geometryNodesPerMesh.data();
	VK_CHECK_RESULT(device->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		geometryNodeBufferSize,
		&geometryNodeStaging.buffer,
		&geometryNodeStaging.memory,
		geometryNodesData));


	// Create device local buffers
	// GeometryNode buffer
	VK_CHECK_RESULT(device->createBuffer2(
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		geometryNodeBufferSize,
		&geometryNodes.buffer,
		&geometryNodes.memory));

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));
	copyRegion.size = geometryNodeBufferSize;
	vkCmdCopyBuffer(copyCmd, geometryNodeStaging.buffer, geometryNodes.buffer, 1, &copyRegion);

	// Create Descriptor for Raytracing
	{
		// Create Descriptor
		geometryNodes.descriptor = { geometryNodes.buffer, 0, geometryNodeBufferSize };
		// TODO for CLAS

	}

	// For Primitives
	if (fileLoadingFlags & myglTF::FileLoadingFlags::GeometryNodePerMesh)
	{
		size_t primitiveBufferSize = tempPrimitives.size() * sizeof(MeshPrimitive);
		// Staging Buffer - Primitives
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			primitiveBufferSize,
			&primitiveStaging.buffer,
			&primitiveStaging.memory,
			tempPrimitives.data()));
		// Primitive buffer
		VK_CHECK_RESULT(device->createBuffer2(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			primitiveBufferSize,
			&primitives.buffer,
			&primitives.memory));
		copyRegion.size = primitiveBufferSize;
		vkCmdCopyBuffer(copyCmd, primitiveStaging.buffer, primitives.buffer, 1, &copyRegion);

		primitives.descriptor = { primitives.buffer, 0, primitiveBufferSize };
	}

	device->flushCommandBuffer(copyCmd, transferQueue, false);
	vkDestroyBuffer(device->logicalDevice, vertexStaging.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, vertexStaging.memory, nullptr);
	vkDestroyBuffer(device->logicalDevice, indexStaging.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, indexStaging.memory, nullptr);

	vkDestroyBuffer(device->logicalDevice, clusterVertexStaging.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, clusterVertexStaging.memory, nullptr);
	vkDestroyBuffer(device->logicalDevice, clusterIndexStaging.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, clusterIndexStaging.memory, nullptr);
	vkDestroyBuffer(device->logicalDevice, clusterBBoxStaging.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, clusterBBoxStaging.memory, nullptr);
	vkDestroyBuffer(device->logicalDevice, clusterStaging.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, clusterStaging.memory, nullptr);
	vkDestroyBuffer(device->logicalDevice, geometryNodeStaging.buffer, nullptr);
	vkFreeMemory(device->logicalDevice, geometryNodeStaging.memory, nullptr);
	if (primitiveStaging.buffer || primitiveStaging.memory)
	{
		vkDestroyBuffer(device->logicalDevice, primitiveStaging.buffer, nullptr);
		vkFreeMemory(device->logicalDevice, primitiveStaging.memory, nullptr);
	}


	// Setup descriptors
	uint32_t uboCount{ 0 };
	uint32_t imageCount{ 0 };
	if (preTransform == false)
	{
		for (auto& node : linearNodes) {
			if (node->mesh) {
				uboCount++;
			}
		}
	}
	else uboCount = 1;

	for (auto& material : materials) {
		if (material.baseColorTexture != nullptr) {
			imageCount++;
		}
	}
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uboCount },
	};
	if (imageCount > 0) {
		if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
			poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount });
		}
		if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
			poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount });
		}
	}
	VkDescriptorPoolCreateInfo descriptorPoolCI{};
	descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	descriptorPoolCI.pPoolSizes = poolSizes.data();
	descriptorPoolCI.maxSets = uboCount + imageCount * 2;
	VK_CHECK_RESULT(vkCreateDescriptorPool(device->logicalDevice, &descriptorPoolCI, nullptr, &descriptorPool));

	// Descriptors for per-node uniform buffers
	{
		// Layout is global, so only create if it hasn't already been created before
		if (descriptorSetLayoutUbo == VK_NULL_HANDLE) {
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				// [model matrix] or [modelMat + Skinning info]
				vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_MESH_BIT_EXT, 0),
			};
			VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
			descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			descriptorLayoutCI.pBindings = setLayoutBindings.data();
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI, nullptr, &descriptorSetLayoutUbo));
		}
		if (preTransform)
		{
			// Create bufffer
			VK_CHECK_RESULT(device->createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				sizeof(UniformData),
				&rootUniformBuffer.buffer,
				&rootUniformBuffer.memory,
				&uniformBlock));
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, rootUniformBuffer.memory, 0, sizeof(UniformData), 0, &rootUniformBuffer.mapped));
			rootUniformBuffer.descriptor = { rootUniformBuffer.buffer, 0, sizeof(UniformData) };

			// allocate descriptor
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorPool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayoutUbo;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &rootUniformBuffer.descriptorSet));

			// update
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSet.descriptorCount = 1;
			writeDescriptorSet.dstSet = rootUniformBuffer.descriptorSet;
			writeDescriptorSet.dstBinding = 0;
			writeDescriptorSet.pBufferInfo = &rootUniformBuffer.descriptor;

			vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
		}
		else // prepare all meshes' ubo
		{
			for (auto node : nodes) {
				prepareNodeDescriptor(node, descriptorSetLayoutUbo);
			}
		}
	}


	// Descriptors for per-material images
	{
		// Layout is global, so only create if it hasn't already been created before
		if (descriptorSetLayoutImage == VK_NULL_HANDLE) {
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
			setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(setLayoutBindings.size())));
			setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(setLayoutBindings.size())));
			/*if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
			}
			if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
			}*/
			VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{};
			descriptorLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			descriptorLayoutCI.pBindings = setLayoutBindings.data();
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI, nullptr, &descriptorSetLayoutImage));
		}
		for (auto& material : materials) {
			if (material.baseColorTexture != nullptr) {
				material.createDescriptorSet(descriptorPool, descriptorSetLayoutImage, descriptorBindingFlags);
			}
		}
	}

	// release
	for (auto& vertex : tempVerticesCPU)
	{
		delete vertex; vertex = nullptr;
	}

}

void myglTF::ModelRT::bindBuffers(VkCommandBuffer commandBuffer)
{
	const VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
	vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
	buffersBound = true;
}

void myglTF::ModelRT::getNodeDimensions(Node* node, glm::vec3& min, glm::vec3& max)
{
	if (node->mesh) {
		for (Primitive* primitive : node->mesh->primitives) {
			glm::vec4 locMin = glm::vec4(primitive->dimensions.min, 1.0f) * node->getMatrix();
			glm::vec4 locMax = glm::vec4(primitive->dimensions.max, 1.0f) * node->getMatrix();
			if (locMin.x < min.x) { min.x = locMin.x; }
			if (locMin.y < min.y) { min.y = locMin.y; }
			if (locMin.z < min.z) { min.z = locMin.z; }
			if (locMax.x > max.x) { max.x = locMax.x; }
			if (locMax.y > max.y) { max.y = locMax.y; }
			if (locMax.z > max.z) { max.z = locMax.z; }
		}
	}
	for (auto child : node->children) {
		getNodeDimensions(child, min, max);
	}
}

void myglTF::ModelRT::getSceneDimensions()
{
	dimensions.min = glm::vec3(FLT_MAX);
	dimensions.max = glm::vec3(-FLT_MAX);
	for (auto node : nodes) {
		getNodeDimensions(node, dimensions.min, dimensions.max);
	}
	dimensions.size = dimensions.max - dimensions.min;
	dimensions.center = (dimensions.min + dimensions.max) / 2.0f;
	dimensions.radius = glm::distance(dimensions.min, dimensions.max) / 2.0f;
}

void myglTF::ModelRT::updateAnimation(uint32_t index, float time)
{
	if (index > static_cast<uint32_t>(animations.size()) - 1) {
		std::cout << "No animation with index " << index << std::endl;
		return;
	}
	Animation& animation = animations[index];

	bool updated = false;
	for (auto& channel : animation.channels) {
		myglTF::AnimationSampler& sampler = animation.samplers[channel.samplerIndex];
		if (sampler.inputs.size() > sampler.outputsVec4.size()) {
			continue;
		}

		for (auto i = 0; i < sampler.inputs.size() - 1; i++) {
			if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
				float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
				if (u <= 1.0f) {
					switch (channel.path) {
					case myglTF::AnimationChannel::PathType::TRANSLATION: {
						glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->translation = glm::vec3(trans);
						break;
					}
					case myglTF::AnimationChannel::PathType::SCALE: {
						glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->scale = glm::vec3(trans);
						break;
					}
					case myglTF::AnimationChannel::PathType::ROTATION: {
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
	if (updated) {
		for (auto& node : nodes) {
			node->update();
		}
	}
}

myglTF::Node* myglTF::ModelRT::findNode(Node* parent, uint32_t index)
{
	Node* nodeFound = nullptr;
	if (parent->index == index) {
		return parent;
	}
	for (auto& child : parent->children) {
		nodeFound = findNode(child, index);
		if (nodeFound) {
			break;
		}
	}
	return nodeFound;
}

myglTF::Node* myglTF::ModelRT::nodeFromIndex(uint32_t index)
{
	Node* nodeFound = nullptr;
	for (auto& node : nodes) {
		nodeFound = findNode(node, index);
		if (nodeFound) {
			break;
		}
	}
	return nodeFound;
}

void myglTF::ModelRT::prepareNodeDescriptor(myglTF::Node* node, VkDescriptorSetLayout descriptorSetLayout)
{
	if (node->mesh) {
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
		descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocInfo.descriptorPool = descriptorPool;
		descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
		descriptorSetAllocInfo.descriptorSetCount = 1;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &node->mesh->uniformBuffer.descriptorSet));

		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = node->mesh->uniformBuffer.descriptorSet;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.pBufferInfo = &node->mesh->uniformBuffer.descriptor;

		vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}
	for (auto& child : node->children) {
		prepareNodeDescriptor(child, descriptorSetLayout);
	}
}
myglTF::Texture* myglTF::ModelRT::getTexture(uint32_t index)
{
	if (index < textures.size()) {
		return &textures[index];
	}
	return nullptr;
}

void myglTF::ModelRT::createEmptyTexture(VkQueue transferQueue)
{
	emptyTexture.device = device;
	emptyTexture.width = 1;
	emptyTexture.height = 1;
	emptyTexture.layerCount = 1;
	emptyTexture.mipLevels = 1;

	size_t bufferSize = emptyTexture.width * emptyTexture.height * 4;
	unsigned char* buffer = new unsigned char[bufferSize];
	memset(buffer, 0, bufferSize);

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;
	VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
	bufferCreateInfo.size = bufferSize;
	// This buffer is used as a transfer source for the buffer copy
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

	VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
	VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

	// Copy texture data into staging buffer
	uint8_t* data{ nullptr };
	VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
	memcpy(data, buffer, bufferSize);
	vkUnmapMemory(device->logicalDevice, stagingMemory);

	VkBufferImageCopy bufferCopyRegion = {};
	bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	bufferCopyRegion.imageSubresource.layerCount = 1;
	bufferCopyRegion.imageExtent.width = emptyTexture.width;
	bufferCopyRegion.imageExtent.height = emptyTexture.height;
	bufferCopyRegion.imageExtent.depth = 1;

	// Create optimal tiled target image
	VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCreateInfo.extent = { emptyTexture.width, emptyTexture.height, 1 };
	imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &emptyTexture.image));

	vkGetImageMemoryRequirements(device->logicalDevice, emptyTexture.image, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &emptyTexture.deviceMemory));
	VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, emptyTexture.image, emptyTexture.deviceMemory, 0));

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.layerCount = 1;

	VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	vks::tools::setImageLayout(copyCmd, emptyTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
	vkCmdCopyBufferToImage(copyCmd, stagingBuffer, emptyTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
	vks::tools::setImageLayout(copyCmd, emptyTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
	device->flushCommandBuffer(copyCmd, transferQueue);
	emptyTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// Clean up staging resources
	vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);

	VkSamplerCreateInfo samplerCreateInfo = vks::initializers::samplerCreateInfo();
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerCreateInfo.maxAnisotropy = 1.0f;
	VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &emptyTexture.sampler));

	VkImageViewCreateInfo viewCreateInfo = vks::initializers::imageViewCreateInfo();
	viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	viewCreateInfo.subresourceRange.levelCount = 1;
	viewCreateInfo.image = emptyTexture.image;
	VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &emptyTexture.view));

	emptyTexture.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	emptyTexture.descriptor.imageView = emptyTexture.view;
	emptyTexture.descriptor.sampler = emptyTexture.sampler;
}