#include "openCubeMesh.h"

OpenCubeMesh::~OpenCubeMesh()
{
    if (vertexBuffer.buffer)
    {
        vkDestroyBuffer(device, vertexBuffer.buffer, nullptr);
        vkFreeMemory(device, vertexBuffer.memory, nullptr);
    }
    if (indexBuffer.buffer)
    {
        vkDestroyBuffer(device, indexBuffer.buffer, nullptr);
        vkFreeMemory(device, indexBuffer.memory, nullptr);
    }
}

void OpenCubeMesh::init(glm::vec3 min, glm::vec3 max, vks::VulkanDevice* vksDevice, VkQueue transferQueue)
{
    device = vksDevice->logicalDevice;
    glm::vec3 moreScale = glm::vec3(0.45f);

    // Vertex array only position (x, y, z), VK Corrdinate System
	glm::vec3 verticesCPU[] = 
    {
        // Bottom face (y = -1)
        {-1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f,  1.0f},
        {-1.0f, -1.0f,  1.0f},

        // Top face (y = 1)
        {-1.0f,  1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f},
        { 1.0f,  1.0f,  1.0f},
        {-1.0f,  1.0f,  1.0f},

        // Back face (z = -1)
        {-1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f},
        {-1.0f,  1.0f, -1.0f},

        // Left face (x = -1)
        {-1.0f, -1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f},
        {-1.0f,  1.0f,  1.0f},
        {-1.0f,  1.0f, -1.0f},

        // Right face (x = 1)
        { 1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f},
        { 1.0f,  1.0f, -1.0f}
        // No Front face
    };

    uint16_t indicesCPU[] =
    {
        // Bottom
        0, 1, 2, 0, 2, 3,
        // Top
        4, 5, 6, 4, 6, 7,
        // Back
        8, 9, 10, 8, 10, 11,
        // Left
        12, 13, 14, 12, 14, 15,
        // Right
        16, 17, 18, 16, 18, 19
    };

    glm::vec3 scale = (max - min) / glm::vec3(2.0f); // (input) / originalScale
    for (auto& vPos : verticesCPU)
    {
        vPos *= (scale); // scale
        vPos.y += (min.y + scale.y); // adjustment for fitting feet pos
        vPos.z *= 2.f;

        //vPos *= (scale + moreScale); // scale
    }
    for (auto& vPos : verticesCPU)
    {
        worldMin = glm::min(worldMin, vPos);
        worldMax = glm::max(worldMax, vPos);
    }


    // Creat GPU Buffer
    size_t vertexBufferSize = sizeof(glm::vec3) * 20;
    size_t indexBufferSize = sizeof(uint16_t) * 30;
    vksDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vertexBufferSize, &vertexBuffer.buffer, &vertexBuffer.memory, transferQueue, verticesCPU);
    vksDevice->CreateBuffer_DeviceLocal(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        indexBufferSize, &indexBuffer.buffer, &indexBuffer.memory, transferQueue, indicesCPU);

    vertexBuffer.count = 20;
    indexBuffer.count = 30;
}
