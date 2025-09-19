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
		ScopedCPUTimer(const std::string& timerName) : name(timerName)
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
		CPUTimer(const std::string& timerName) : name(timerName) {};
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
}