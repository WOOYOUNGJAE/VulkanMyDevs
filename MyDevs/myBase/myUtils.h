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
			if (print) printResult();
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
		std::string name = "CPU Timer";
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

		void cmdBeginLabel(VkCommandBuffer command_buffer, const char* label_name, std::vector<float> color)
		{
			VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			label.pLabelName = label_name;
			memcpy(label.color, color.data(), sizeof(float) * 4);
			vkCmdBeginDebugUtilsLabelEXT(command_buffer, &label);
		}

		void cmdInsertLabel(VkCommandBuffer command_buffer, const char* label_name, std::vector<float> color)
		{
			VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			label.pLabelName = label_name;
			memcpy(label.color, color.data(), sizeof(float) * 4);
			vkCmdInsertDebugUtilsLabelEXT(command_buffer, &label);
		}

		void cmdEndLabel(VkCommandBuffer command_buffer)
		{
			vkCmdEndDebugUtilsLabelEXT(command_buffer);
		}

		// Functions for putting labels into a queue
		// Labels consist of a name and an optional color
		// How or if these are diplayed depends on the debugger used (RenderDoc e.g. displays both)

		void queueBeginLabel(VkQueue queue, const char* label_name, std::vector<float> color)
		{
			VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			label.pLabelName = label_name;
			memcpy(label.color, color.data(), sizeof(float) * 4);
			vkQueueBeginDebugUtilsLabelEXT(queue, &label);
		}

		void queueInsertLabel(VkQueue queue, const char* label_name, std::vector<float> color)
		{
			VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			label.pLabelName = label_name;
			memcpy(label.color, color.data(), sizeof(float) * 4);
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
}