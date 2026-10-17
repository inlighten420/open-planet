// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadGraph.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace georoads
{
	namespace ThreadPoolImpl
	{
		struct Pool
		{
			std::mutex Lock;
			std::condition_variable Wake, Finished;
			std::vector<std::thread> Workers;
			const std::function<void(int32_t)>* Body = nullptr;
			int32_t Count = 0;
			std::atomic<int32_t> Next{ 0 };
			int32_t Grain = 1;	// items taken at a time (a road's few points each: one at a time, the taking cost more than the work)
			int32_t Busy = 0;	// workers still on the current job
			uint64_t Job = 0;
			bool bStop = false;
			std::mutex CallLock;	// one job at a time

			int32_t WantedThreads = 0;
			std::function<void()> OnWorkerStart;
			std::once_flag Started;

			void Run(int32_t Index)
			{
				if (OnWorkerStart)
				{
					OnWorkerStart();
				}
				InsideWorker() = true;
				uint64_t Seen = 0;
				for (;;)
				{
					const std::function<void(int32_t)>* Work = nullptr;
					int32_t Total = 0, Take = 1;
					{
						std::unique_lock<std::mutex> Guard(Lock);
						Wake.wait(Guard, [&] { return bStop || Job != Seen; });
						if (bStop)
						{
							return;
						}
						Seen = Job;
						Work = Body;
						Total = Count;
						Take = Grain;
					}
					for (int32_t First = Next.fetch_add(Take); First < Total; First = Next.fetch_add(Take))
					{
						for (int32_t i = First, End = std::min(First + Take, Total); i < End; ++i)
						{
							(*Work)(i);
						}
					}
					std::lock_guard<std::mutex> Guard(Lock);
					if (--Busy == 0)
					{
						Finished.notify_all();
					}
				}
				(void)Index;
			}

			static bool& InsideWorker()
			{
				thread_local bool bInside = false;
				return bInside;
			}

			~Pool()
			{
				{
					std::lock_guard<std::mutex> Guard(Lock);
					bStop = true;
				}
				Wake.notify_all();
				for (std::thread& Worker : Workers)
				{
					if (Worker.joinable())
					{
						Worker.join();
					}
				}
			}
		};

		/** The one pool (its settings are set before it starts) */
		Pool& Settings()
		{
			static Pool Instance;
			return Instance;
		}

		Pool& Get()
		{
			Pool& Instance = Settings();
			std::call_once(Instance.Started, [&Instance]
			{
				const int32_t Cores = int32_t(std::max(1u, std::thread::hardware_concurrency()));
				const int32_t Threads = Instance.WantedThreads > 0 ? Instance.WantedThreads : Cores;
				for (int32_t t = 1; t < Threads; ++t)	// the caller is one of them
				{
					Instance.Workers.emplace_back([&Instance, t] { Instance.Run(t); });
				}
			});
			return Instance;
		}
	}

	void ConfigureThreadsParallelFor(int32_t Threads, std::function<void()> OnWorkerStart)
	{
		// (read when the pool starts, at its first use)
		ThreadPoolImpl::Pool& P = ThreadPoolImpl::Settings();
		P.WantedThreads = Threads;
		P.OnWorkerStart = std::move(OnWorkerStart);
	}

	void ThreadsParallelFor(int32_t Count, const std::function<void(int32_t)>& Body)
	{
		using namespace ThreadPoolImpl;
		auto Serial = [&]
		{
			for (int32_t i = 0; i < Count; ++i)
			{
				Body(i);
			}
		};
		if (Count <= 1 || Pool::InsideWorker())
		{
			Serial();
			return;
		}
		Pool& P = Get();
		std::unique_lock<std::mutex> Call(P.CallLock, std::try_to_lock);
		if (!Call.owns_lock() || P.Workers.empty())
		{
			Serial();
			return;
		}
		{
			std::lock_guard<std::mutex> Guard(P.Lock);
			P.Body = &Body;
			P.Count = Count;
			P.Next = 0;
			P.Grain = std::max(1, Count / (int32_t(P.Workers.size() + 1) * 16));	// (~16 takes a thread: even enough, little taking)
			P.Busy = int32_t(P.Workers.size());
			++P.Job;
		}
		P.Wake.notify_all();
		Pool::InsideWorker() = true;	// (a ParallelFor inside this one runs here, not on the busy pool)
		const int32_t Take = P.Grain;
		for (int32_t First = P.Next.fetch_add(Take); First < Count; First = P.Next.fetch_add(Take))
		{
			for (int32_t i = First, End = std::min(First + Take, Count); i < End; ++i)
			{
				Body(i);
			}
		}
		Pool::InsideWorker() = false;
		std::unique_lock<std::mutex> Guard(P.Lock);
		P.Finished.wait(Guard, [&P] { return P.Busy == 0; });
	}

	std::string Format(const char* Pattern, ...)
	{
		char Buffer[1024];
		va_list Args;
		va_start(Args, Pattern);
		const int N = std::vsnprintf(Buffer, sizeof(Buffer), Pattern, Args);
		va_end(Args);
		if (N < static_cast<int>(sizeof(Buffer)))
		{
			return std::string(Buffer, N > 0 ? N : 0);
		}
		std::string Long(static_cast<size_t>(N) + 1, '\0');
		va_start(Args, Pattern);
		std::vsnprintf(Long.data(), Long.size(), Pattern, Args);
		va_end(Args);
		Long.resize(N);
		return Long;
	}
}
