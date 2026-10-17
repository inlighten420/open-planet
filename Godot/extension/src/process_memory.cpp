// Geodrive Godot client - GPL (see Core/README.md)
//
// What this game itself uses, for the overlay: its memory (Windows' private bytes for this process: everything it has
// allocated, Godot and the world's C++ pieces and caches, apart from other programs) and its share of the processor.

#include "process_memory.h"

#include <algorithm>
#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

double ProcessMemoryBytes()
{
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS_EX Counters{};
	if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&Counters), sizeof(Counters)))
	{
		return double(Counters.PrivateUsage);
	}
#endif
	return 0.0;
}

double ProcessCpuPercent()
{
#ifdef _WIN32
	// the processor time this game used since the last call, over the time that passed times the processor's threads
	static double LastBusy = -1.0;
	static std::chrono::steady_clock::time_point LastTime;
	static double LastPercent = 0.0;
	FILETIME Created, Exited, Kernel, User;
	if (!GetProcessTimes(GetCurrentProcess(), &Created, &Exited, &Kernel, &User))
	{
		return 0.0;
	}
	auto Seconds = [](const FILETIME& Time) { return double((uint64_t(Time.dwHighDateTime) << 32) | Time.dwLowDateTime) * 1e-7; };
	const double Busy = Seconds(Kernel) + Seconds(User);
	const auto Now = std::chrono::steady_clock::now();
	if (LastBusy >= 0.0)
	{
		const double Passed = std::chrono::duration<double>(Now - LastTime).count();
		if (Passed < 0.25)
		{
			return LastPercent;	// (too soon to tell: the last reading)
		}
		const double Threads = double(std::max(1u, std::thread::hardware_concurrency()));
		LastPercent = 100.0 * (Busy - LastBusy) / (Passed * Threads);
	}
	LastBusy = Busy;
	LastTime = Now;
	return LastPercent;
#else
	return 0.0;
#endif
}
