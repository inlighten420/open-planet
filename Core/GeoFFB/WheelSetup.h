// Geodrive Core - GPL (see Core/README.md)
//
// Setting the wheel up by pressing things: a pedal (press it all the way down and hold it: whichever axis moves most
// becomes that pedal, its rest and fully pressed positions its calibration), and the wheel's own buttons and hats for the
// camera and seat (the next new press becomes that control; one control does one thing). The game calls Update with
// the wheel's input every frame while one is being set, and saves the settings when it says they changed.

#pragma once

#include "WheelDriver.h"

#include <set>
#include <string>
#include <vector>

namespace geoffb
{
	/** The camera and seat controls a wheel's buttons can do: LookLeft, LookRight, LookUp, LookDown, LookBack,
	 *  SeatForward, SeatBack, SeatUp, SeatDown, ChangeView */
	const std::vector<std::string>& ControlNames();

	/** "SeatForward" -> "Seat forward" */
	std::string ControlLabel(const std::string& Name);

	/** "Button 12", "Hat 1 left" */
	std::string DescribeCode(int32_t Code);

	/** What a control is set to, or "Not set" */
	std::string DescribeControl(const WheelSettings& Settings, const std::string& Name);

	/** Every button and hat direction held now (a diagonal counts for both its directions) */
	void DownControls(const WheelInput& Input, std::vector<int32_t>& Out);

	/** The control is held now */
	bool IsControlDown(const WheelSettings& Settings, const WheelInput& Input, const std::string& Name);

	/** Setting a pedal ("throttle", "brake" or "clutch") */
	struct PedalCapture
	{
		std::string Pedal;	// the one being set, empty when none
		std::string Status;	// what to tell the player

		void Begin(const std::string& Pedal, bool bConnected);
		bool IsActive() const { return !Pedal.empty(); }

		/** True when it has set the pedal (its axis and calibration in Settings) */
		bool Update(WheelSettings& Settings, const WheelInput& Input, double Now);

	private:
		std::vector<int32_t> Start;
		int32_t Axis = -1;
		int32_t Extreme = 0;
		double Time = 0.0;
	};

	/** The axis a pedal is on, -1 if none */
	int32_t PedalAxis(const WheelSettings& Settings, const std::string& Pedal);

	/** Setting a camera or seat control */
	struct ControlCapture
	{
		std::string Control;	// the one being set, empty when none
		std::string Status;

		void Begin(const std::string& Control, const WheelInput& Input, double Now);
		bool IsActive() const { return !Control.empty(); }

		/** True when it has set the control */
		bool Update(WheelSettings& Settings, const WheelInput& Input, double Now);

	private:
		std::set<int32_t> Held;	// what was already held when it started (released ones count again)
		double Time = 0.0;
	};
}
