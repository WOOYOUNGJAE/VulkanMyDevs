#pragma once
#include <chrono>
#include <string>

#include "vulkan.h"

namespace myUtils
{
	class ScopedCPUTimer
	{
	public:
		ScopedCPUTimer()
		{
			std::string msg = name + " Starts\n";
			printf(msg.c_str());
			startTime = std::chrono::high_resolution_clock::now();
		}
		ScopedCPUTimer(const char* timerName) : name(std::string(timerName))
		{
			std::string msg = name + " Starts\n";
			printf(msg.c_str());
			startTime = std::chrono::high_resolution_clock::now();
		}
		~ScopedCPUTimer()
		{
			double duration = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - startTime).count();
			std::string msg = name + " Result : " + std::to_string(duration) + "(ms)\n";
			printf(msg.c_str());
		}
	private:
		std::chrono::time_point<std::chrono::high_resolution_clock> startTime{};
		std::string name = "Scoped CPU Timer";
	};

	class CPUTimer
	{
	public:
		CPUTimer(bool startWhenCreated = false)
		{
			if (startWhenCreated) start();
		}
		CPUTimer(const char* timerName) : name(std::string(timerName)) {};
		void start()
		{
			startTime = std::chrono::high_resolution_clock::now();
		}
		void record(bool print = false)
		{
			duration = std::chrono::high_resolution_clock::now() - startTime;
			accDuration += duration;
			++count;
			if (print) printResult();
		}
		double timerAccumulatedResultMilli()
		{
			double ret = accDuration.count();
			accDuration = std::chrono::duration<double, std::milli>::zero();
			return ret;
		}

		double timerResultMilli()
		{
			return duration.count();
		}
		double timerResultSecond()
		{
			return duration.count() * 0.001;
		}
		void printResult()
		{
			std::string msg = name + " Result : " + std::to_string(timerResultMilli()) + "(ms)\n";
			printf(msg.c_str());
		}
	public:
		std::chrono::time_point<std::chrono::high_resolution_clock> startTime{};
		std::chrono::duration<double, std::milli> duration{};
		std::chrono::duration<double, std::milli> accDuration{};
		std::string name = "CPU Timer";
		uint32_t count = 0;
	};

	
	class GPUDebug // Singleton class
	{
		GPUDebug() = default;
		GPUDebug(GPUDebug&) = delete;
	public:
		~GPUDebug() = default;
		static GPUDebug* Get()
		{
			if (m_pInstance == nullptr)
				m_pInstance = new GPUDebug();

			return m_pInstance;
		}
	public:
		void init(VkInstance vkInstnace, VkDevice device)
		{
			m_device = device;
			vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(vkInstnace, "vkCreateDebugUtilsMessengerEXT"));
			vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(vkInstnace, "vkDestroyDebugUtilsMessengerEXT"));
			vkCmdBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(vkInstnace, "vkCmdBeginDebugUtilsLabelEXT"));
			vkCmdInsertDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(vkGetInstanceProcAddr(vkInstnace, "vkCmdInsertDebugUtilsLabelEXT"));
			vkCmdEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(vkInstnace, "vkCmdEndDebugUtilsLabelEXT"));
			vkQueueBeginDebugUtilsLabelEXT = reinterpret_cast<PFN_vkQueueBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(vkInstnace, "vkQueueBeginDebugUtilsLabelEXT"));
			vkQueueInsertDebugUtilsLabelEXT = reinterpret_cast<PFN_vkQueueInsertDebugUtilsLabelEXT>(vkGetInstanceProcAddr(vkInstnace, "vkQueueInsertDebugUtilsLabelEXT"));
			vkQueueEndDebugUtilsLabelEXT = reinterpret_cast<PFN_vkQueueEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(vkInstnace, "vkQueueEndDebugUtilsLabelEXT"));
			vkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(vkInstnace, "vkSetDebugUtilsObjectNameEXT"));
		}
		/*
		* Code from Sacha - debugutils
		*
		* Copyright (C) 2016-2023 by Sascha Willems - www.saschawillems.de
		*
		* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
		*/

		void cmdBeginLabel(VkCommandBuffer command_buffer, const char* label_name, float r, float g, float b)
		{
			float color[4] = { r, g, b, 1.f };
			VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			label.pLabelName = label_name;
			memcpy(label.color, color, sizeof(float) * 4);
			vkCmdBeginDebugUtilsLabelEXT(command_buffer, &label);
		}

		void cmdInsertLabel(VkCommandBuffer command_buffer, const char* label_name, float r, float g, float b)
		{
			float color[4] = { r, g, b, 1.f };
			VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			label.pLabelName = label_name;
			memcpy(label.color, color, sizeof(float) * 4);
			vkCmdInsertDebugUtilsLabelEXT(command_buffer, &label);
		}

		void cmdEndLabel(VkCommandBuffer command_buffer)
		{
			vkCmdEndDebugUtilsLabelEXT(command_buffer);
		}

		// Functions for putting labels into a queue
		// Labels consist of a name and an optional color
		// How or if these are diplayed depends on the debugger used (RenderDoc e.g. displays both)

		void queueBeginLabel(VkQueue queue, const char* label_name, float r, float g, float b)
		{
			float color[4] = { r, g, b, 1.f };
			VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			label.pLabelName = label_name;
			memcpy(label.color, color, sizeof(float) * 4);
			vkQueueBeginDebugUtilsLabelEXT(queue, &label);
		}

		void queueInsertLabel(VkQueue queue, const char* label_name, float r, float g, float b)
		{
			float color[4] = { r, g, b, 1.f };
			VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			label.pLabelName = label_name;
			memcpy(label.color, color, sizeof(float) * 4);
			vkQueueInsertDebugUtilsLabelEXT(queue, &label);
		}

		void queueEndLabel(VkQueue queue)
		{
			vkQueueEndDebugUtilsLabelEXT(queue);
		}

		// Function for naming Vulkan objects
		// In Vulkan, all objects (that can be named) are opaque unsigned 64 bit handles, and can be cased to uint64_t
		// example:	setObjectName(device, VK_OBJECT_TYPE_BUFFER, (uint64_t)uniformBuffer.buffer, "Scene uniform buffer block");
		void setObjectName(VkObjectType object_type, uint64_t object_handle, const char* object_name)
		{
			VkDebugUtilsObjectNameInfoEXT name_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
			name_info.objectType = object_type;
			name_info.objectHandle = object_handle;
			name_info.pObjectName = object_name;
			vkSetDebugUtilsObjectNameEXT(m_device, &name_info);
		}
	private:
		PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT{ nullptr };
		PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT{ nullptr };
		PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT{ nullptr };
		PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabelEXT{ nullptr };
		PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT{ nullptr };
		PFN_vkQueueBeginDebugUtilsLabelEXT vkQueueBeginDebugUtilsLabelEXT{ nullptr };
		PFN_vkQueueInsertDebugUtilsLabelEXT vkQueueInsertDebugUtilsLabelEXT{ nullptr };
		PFN_vkQueueEndDebugUtilsLabelEXT vkQueueEndDebugUtilsLabelEXT{ nullptr };
		PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT{ nullptr };
		VkDevice m_device = VK_NULL_HANDLE;
		static GPUDebug* m_pInstance;
	};

	namespace vk
	{

		inline uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
		{
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if (typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
					return i;
				}
			}
			return ~0;
		}

		inline VkCommandBuffer createCommandBuffer(VkDevice device, VkCommandBufferLevel level, VkCommandPool pool, bool begin = false)
		{
			VkCommandBufferAllocateInfo cmdBufAllocateInfo
			{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = pool,
				.level = level,
				.commandBufferCount = 1,
			};
			VkCommandBuffer cmdBuffer;
			vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &cmdBuffer);
			// If requested, also start recording for the new command buffer
			if (begin)
			{
				VkCommandBufferBeginInfo cmdBufferBeginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
				(vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo));
			}
			return cmdBuffer;
		}

		inline void flushCommandBuffer(VkDevice device, VkCommandBuffer commandBuffer, VkQueue queue, VkCommandPool pool, bool free)
		{
			if (commandBuffer == VK_NULL_HANDLE)
			{
				return;
			}

			vkEndCommandBuffer(commandBuffer);

			VkSubmitInfo submitInfo
			{
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.commandBufferCount = 1,
				.pCommandBuffers = &commandBuffer,
			};
			// Create fence to ensure that the command buffer has finished executing
			VkFenceCreateInfo fenceInfo { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			VkFence fence;
			(vkCreateFence(device, &fenceInfo, nullptr, &fence));
			// Submit to the queue
			(vkQueueSubmit(queue, 1, &submitInfo, fence));
			// Wait for the fence to signal that command buffer has finished executing
			(vkWaitForFences(device, 1, &fence, VK_TRUE, 100000000000));
			vkDestroyFence(device, fence, nullptr);
			if (free)
			{
				vkFreeCommandBuffers(device, pool, 1, &commandBuffer);
			}
		}

		/**
		 * @param data src data
		 * @param ppMappedPtr if this is not null, do not unmap and use this mapped pointer outside
		 */
		inline void CreateBuffer_HostVisible(VkPhysicalDevice physicalDevice, VkDevice device, VkBufferUsageFlags usageFlags, VkDeviceSize size,
			VkBuffer* buffer, VkDeviceMemory* memory, bool isHostCoherent = true, void* data = nullptr, void** ppMappedPtr = nullptr)
		{
			VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			if (isHostCoherent)
				memoryPropertyFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			// Create the buffer handle
			VkBufferCreateInfo bufferCreateInfo
			{
				.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				.size = size,
				.usage = usageFlags
			};
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			(vkCreateBuffer(device, &bufferCreateInfo, nullptr, buffer));

			// Create the memory backing up the buffer handle
			VkMemoryRequirements memReqs;
			vkGetBufferMemoryRequirements(device, *buffer, &memReqs);
			VkMemoryAllocateInfo memAlloc{};
			memAlloc.allocationSize = memReqs.size;
			// Find a memory type index that fits the properties of the buffer
			memAlloc.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, memoryPropertyFlags);
			// If the buffer has VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT set we also need to enable the appropriate flag during allocation
			VkMemoryAllocateFlagsInfoKHR allocFlagsInfo{};
			if (usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
				allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
				allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
				memAlloc.pNext = &allocFlagsInfo;
			}
			(vkAllocateMemory(device, &memAlloc, nullptr, memory));

			// If a pointer to the buffer data has been passed, map the buffer and copy over the data
			if (data != nullptr)
			{
				if (ppMappedPtr)
				{
					(vkMapMemory(device, *memory, 0, size, 0, ppMappedPtr));
					memcpy(*ppMappedPtr, data, size);
				}
				else
				{
					void* mapped;
					(vkMapMemory(device, *memory, 0, size, 0, &mapped));
					memcpy(mapped, data, size);
					vkUnmapMemory(device, *memory);
				}
				// If host coherency hasn't been requested, do a manual flush to make writes visible
				if (isHostCoherent == false)
				{
					VkMappedMemoryRange mappedRange
					{
						.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
						.memory = *memory,
						.offset = 0,
						.size = size
					};
					vkFlushMappedMemoryRanges(device, 1, &mappedRange);
				}
			}
			else if (ppMappedPtr) // no copy, just mapping. unmap outside
			{
				(vkMapMemory(device, *memory, 0, size, 0, ppMappedPtr));
			}

			// Attach the memory to the buffer object
			(vkBindBufferMemory(device, *buffer, *memory, 0));
		}

		/**
		 * if param data exists, create staging buffer and transfer
		 */
		inline void CreateBuffer_DeviceLocal(VkPhysicalDevice physicalDevice, VkDevice device, VkBufferUsageFlags usageFlags, VkDeviceSize size,
			VkBuffer* buffer, VkDeviceMemory* memory, VkCommandPool cmdPool, VkQueue transferQueue, void* data)
		{
			usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			if (data)
				usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

			// Create the buffer handle
			VkBufferCreateInfo bufferCreateInfo
			{
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.size = size,
				.usage = usageFlags,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE
			};
			(vkCreateBuffer(device, &bufferCreateInfo, nullptr, buffer));

			// Create the memory backing up the buffer handle
			VkMemoryRequirements memReqs;
			VkMemoryAllocateInfo memAlloc{};
			vkGetBufferMemoryRequirements(device, *buffer, &memReqs);
			memAlloc.allocationSize = memReqs.size;
			// Find a memory type index that fits the properties of the buffer
			memAlloc.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, memoryPropertyFlags);
			// If the buffer has VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT set we also need to enable the appropriate flag during allocation
			VkMemoryAllocateFlagsInfoKHR allocFlagsInfo{};
			allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
			allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
			memAlloc.pNext = &allocFlagsInfo;

			(vkAllocateMemory(device, &memAlloc, nullptr, memory));

			// Attach the memory to the buffer object
			(vkBindBufferMemory(device, *buffer, *memory, 0));

			// If a pointer to the buffer data has been passed, Create Staging buffer and Transfer
			if (data && transferQueue)
			{
				VkBuffer stagingBuffer = VK_NULL_HANDLE;
				VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
				CreateBuffer_HostVisible(physicalDevice, device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size, &stagingBuffer, &stagingMemory, true, data);

				VkCommandBuffer copyCmd = createCommandBuffer(device, VK_COMMAND_BUFFER_LEVEL_PRIMARY, cmdPool, true);

				VkBufferCopy copyRegion = {};
				copyRegion.size = size;
				vkCmdCopyBuffer(copyCmd, stagingBuffer, *buffer, 1, &copyRegion);
				flushCommandBuffer(device, copyCmd, transferQueue, cmdPool, false);


				vkDestroyBuffer(device, stagingBuffer, nullptr);
				vkFreeMemory(device, stagingMemory, nullptr);
			}
		}
	}

}