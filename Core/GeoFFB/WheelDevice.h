// Geodrive Core - GPL (see Core/README.md)
//
// One steering wheel through DirectInput 8 (Windows): finds the first force-feedback game controller (or any game
// controller, for input only), reads its axes and buttons and drives a constant force, a damper and a friction on
// the steering axis. Elsewhere it never opens. Not thread safe: create, use and close it on one thread.

#pragma once

#include "WheelTypes.h"

#include <functional>
#include <memory>

namespace geoffb
{
	/** For the wheel loop's thread: above normal priority (its timing matters more than a frame's) and 1 ms sleeps */
	void BeginLoopTiming();
	void EndLoopTiming();

	class WheelDevice
	{
	public:
		WheelDevice();
		~WheelDevice();

		/** Opens the wheel. Window: the game's top-level window handle (an HWND; DirectInput wants it for exclusive
		 *  access). Log gets a line about what happened. */
		bool Open(void* Window, const std::function<void(const std::string&)>& Log);

		/** Stops every force and lets the device go */
		void Close();

		bool IsOpen() const;

		/** Reads the device; false if it was lost and couldn't be got back */
		bool Poll(WheelInput& Out);

		void SetConstantForce(float Force);	// [-1, 1]
		void SetDamper(float Damper);		// [0, 1]
		void SetFriction(float Friction);	// [0, 1]

	private:
		struct Impl;
		std::unique_ptr<Impl> Device;
	};
}
