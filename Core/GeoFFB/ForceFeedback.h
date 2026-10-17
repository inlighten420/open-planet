// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// The wheel's own thread, running at about 500 Hz whatever the game's frame rate: it owns the device, reads it, and
// turns the game's latest command (or, for our own cars, the car's telemetry straight from the physics) into forces,
// smoothed, with the tyres' shakes on top (lock-up, ABS, scrub, loose surfaces, road bumps). If the game stops
// sending commands (paused, a hitch, in the background) the force fades to nothing by itself.

#pragma once

#include "WheelTypes.h"
#include "GeoCar/CarTelemetry.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace geoffb
{
	class ForceFeedback
	{
	public:
		using LogFunction = std::function<void(const std::string&)>;

		~ForceFeedback();

		/** Starts the thread. Window: the game's top-level window (an HWND on Windows). Log is called from the thread. */
		void Start(void* Window, LogFunction Log = nullptr);

		/** Stops the thread: every force goes to zero and the device is let go */
		void Stop();

		bool IsRunning() const { return Thread.joinable(); }

		/** The latest wheel state */
		WheelInput GetInput() const;

		/** What to do now; Telemetry (when the command says to use it) is read by the thread at its own rate */
		void SetCommand(const Command& NewCommand, std::shared_ptr<const geocar::CarTelemetry> Telemetry);

		Status GetStatus() const;

	private:
		void Run(void* Window, LogFunction Log);

		std::thread Thread;
		std::atomic<bool> bStop { false };

		mutable std::mutex Lock;
		WheelInput Input;
		Command Wanted;
		std::shared_ptr<const geocar::CarTelemetry> CarTelemetry;
		Status Loop;
	};
}
