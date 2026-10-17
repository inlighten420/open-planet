// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "WheelSetup.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace geoffb
{
	const std::vector<std::string>& ControlNames()
	{
		static const std::vector<std::string> Names = { "LookLeft", "LookRight", "LookUp", "LookDown", "LookBack",
			"SeatForward", "SeatBack", "SeatUp", "SeatDown", "ChangeView" };
		return Names;
	}

	std::string ControlLabel(const std::string& Name)
	{
		std::string Label;
		for (size_t i = 0; i < Name.size(); ++i)
		{
			const char C = Name[i];
			if (i > 0 && std::isupper(static_cast<unsigned char>(C)))
			{
				Label += ' ';
				Label += static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
			}
			else
			{
				Label += C;
			}
		}
		return Label;
	}

	std::string DescribeCode(int32_t Code)
	{
		if (Code >= 1000)
		{
			static const char* Directions[4] = { "up", "right", "down", "left" };
			return "Hat " + std::to_string((Code - 1000) / 4 + 1) + " " + Directions[(Code - 1000) % 4];
		}
		return "Button " + std::to_string(Code + 1);
	}

	std::string DescribeControl(const WheelSettings& Settings, const std::string& Name)
	{
		const auto Found = Settings.Controls.find(Name);
		return Found != Settings.Controls.end() ? DescribeCode(Found->second) : std::string("Not set");
	}

	static bool HatToward(int32_t Hat, int32_t Direction)
	{
		if (Hat < 0)
		{
			return false;
		}
		const int32_t Difference = std::abs(Hat - Direction * 9000);
		return std::min(Difference, 36000 - Difference) <= 6750;
	}

	void DownControls(const WheelInput& Input, std::vector<int32_t>& Out)
	{
		for (int32_t i = 0; i < NumButtons; ++i)
		{
			if (Input.Buttons[i])
			{
				Out.push_back(i);
			}
		}
		for (int32_t Hat = 0; Hat < NumHats; ++Hat)
		{
			for (int32_t Direction = 0; Direction < 4; ++Direction)
			{
				if (HatToward(Input.Hats[Hat], Direction))
				{
					Out.push_back(1000 + Hat * 4 + Direction);
				}
			}
		}
	}

	bool IsControlDown(const WheelSettings& Settings, const WheelInput& Input, const std::string& Name)
	{
		const auto Found = Settings.Controls.find(Name);
		if (Found == Settings.Controls.end())
		{
			return false;
		}
		const int32_t Code = Found->second;
		if (Code >= 1000)
		{
			const int32_t Hat = (Code - 1000) / 4;
			return Hat < NumHats && HatToward(Input.Hats[Hat], (Code - 1000) % 4);
		}
		return Code >= 0 && Code < NumButtons && Input.Buttons[Code];
	}

	int32_t PedalAxis(const WheelSettings& Settings, const std::string& Pedal)
	{
		return Pedal == "throttle" ? Settings.ThrottleAxis : Pedal == "brake" ? Settings.BrakeAxis : Pedal == "clutch" ? Settings.ClutchAxis : -1;
	}

	void PedalCapture::Begin(const std::string& InPedal, bool bConnected)
	{
		if (!bConnected)
		{
			Pedal.clear();
			Status = "No wheel connected";
			return;
		}
		Pedal = InPedal;
		Start.clear();
		Axis = -1;
		Status = "Press the " + Pedal + " pedal all the way down and hold it";
	}

	bool PedalCapture::Update(WheelSettings& Settings, const WheelInput& Input, double Now)
	{
		if (Pedal.empty() || !Input.bConnected)
		{
			return false;
		}
		// remember where every axis rests, then whichever one moves most (other than steering) is the pedal being
		// pressed; after it's held for a moment, its rest and fully pressed positions become its calibration
		if (Start.size() != size_t(NumAxes))
		{
			Start.assign(Input.Axes, Input.Axes + NumAxes);
			Axis = -1;
			Time = Now;
			return false;
		}
		if (Axis < 0)
		{
			int32_t Best = -1, BestMove = 0;
			for (int32_t i = 0; i < NumAxes; ++i)
			{
				const int32_t Move = std::abs(Input.Axes[i] - Start[i]);
				if (i != Settings.SteeringAxis && Move > BestMove)
				{
					Best = i;
					BestMove = Move;
				}
			}
			if (Best >= 0 && BestMove > (AxisMax - AxisMin) * 3 / 10)
			{
				Axis = Best;
				Extreme = Input.Axes[Best];
				Time = Now;
				Status = std::string("Found it (") + AxisName(Best) + "). Keep it pressed...";
			}
			else if (Now - Time > 10.0)
			{
				Status = "No pedal moved. Try " + Pedal + " again.";
				Pedal.clear();
			}
			return false;
		}
		if (std::abs(Input.Axes[Axis] - Start[Axis]) > std::abs(Extreme - Start[Axis]))
		{
			Extreme = Input.Axes[Axis];
		}
		if (Now - Time <= 1.5)
		{
			return false;
		}
		// one pedal per axis
		for (int32_t* Other : { &Settings.ThrottleAxis, &Settings.BrakeAxis, &Settings.ClutchAxis })
		{
			if (*Other == Axis)
			{
				*Other = -1;
			}
		}
		Settings.AxisRest[Axis] = Start[Axis];
		Settings.AxisPressed[Axis] = Extreme;
		(Pedal == "throttle" ? Settings.ThrottleAxis : Pedal == "brake" ? Settings.BrakeAxis : Settings.ClutchAxis) = Axis;
		std::string Name = Pedal;
		Name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Name[0])));
		Status = Name + " set (" + AxisName(Axis) + ")";
		Pedal.clear();
		return true;
	}

	void ControlCapture::Begin(const std::string& InControl, const WheelInput& Input, double Now)
	{
		if (!Input.bConnected)
		{
			Control.clear();
			Status = "No wheel connected";
			return;
		}
		std::vector<int32_t> Down;
		DownControls(Input, Down);
		Held = std::set<int32_t>(Down.begin(), Down.end());
		Control = InControl;
		Time = Now;
		std::string Label = ControlLabel(InControl);
		Label[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(Label[0])));
		Status = "Press the button or push the stick for: " + Label;
	}

	bool ControlCapture::Update(WheelSettings& Settings, const WheelInput& Input, double Now)
	{
		if (Control.empty() || !Input.bConnected)
		{
			return false;
		}
		std::vector<int32_t> Down;
		DownControls(Input, Down);
		for (auto It = Held.begin(); It != Held.end();)
		{
			It = std::find(Down.begin(), Down.end(), *It) == Down.end() ? Held.erase(It) : std::next(It);
		}
		for (const int32_t Code : Down)
		{
			if (Held.count(Code))
			{
				continue;
			}
			// one control does one thing
			for (auto It = Settings.Controls.begin(); It != Settings.Controls.end();)
			{
				It = It->second == Code ? Settings.Controls.erase(It) : std::next(It);
			}
			Settings.Controls[Control] = Code;
			Status = ControlLabel(Control) + ": " + DescribeCode(Code);
			Control.clear();
			return true;
		}
		if (Now - Time > 10.0)
		{
			Status = "Nothing pressed. Click Set and try again.";
			Control.clear();
		}
		return false;
	}
}
