// Geodrive for Godot - AGPL-3.0 (see LICENSE)

#include "geodrive_wheel.h"
#include "geodrive_car.h"

#include "GeoFFB/ForceFeedback.h"
#include "GeoFFB/WheelDriver.h"
#include "GeoFFB/WheelSetup.h"

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

using namespace godot;

struct GeodriveWheel::State
{
	geoffb::ForceFeedback Loop;
	geoffb::WheelSettings Settings;
	geoffb::DriverState Driver;
	geoffb::PedalCapture Pedal;
	geoffb::ControlCapture Control;
	bool bChanged = false;	// a pedal or control was set: the game saves the settings

	/** Lines from the wheel's thread, printed on the main thread */
	std::mutex LogLock;
	std::vector<std::string> LogLines;
};

GeodriveWheel::GeodriveWheel()
	: Wheel(std::make_unique<State>())
{
}

GeodriveWheel::~GeodriveWheel()
{
	Wheel->Loop.Stop();
}

void GeodriveWheel::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_car", "car"), &GeodriveWheel::set_car);
	ClassDB::bind_method(D_METHOD("get_car"), &GeodriveWheel::get_car);
	ClassDB::bind_method(D_METHOD("is_wheel_connected"), &GeodriveWheel::is_wheel_connected);
	ClassDB::bind_method(D_METHOD("get_status"), &GeodriveWheel::get_status);
	ClassDB::bind_method(D_METHOD("load_settings", "ini_path"), &GeodriveWheel::load_settings);
	ClassDB::bind_method(D_METHOD("begin_pedal_capture", "pedal"), &GeodriveWheel::begin_pedal_capture);
	ClassDB::bind_method(D_METHOD("get_pedal_status"), &GeodriveWheel::get_pedal_status);
	ClassDB::bind_method(D_METHOD("get_pedal_axis", "pedal"), &GeodriveWheel::get_pedal_axis);
	ClassDB::bind_method(D_METHOD("begin_control_capture", "control"), &GeodriveWheel::begin_control_capture);
	ClassDB::bind_method(D_METHOD("clear_control", "control"), &GeodriveWheel::clear_control);
	ClassDB::bind_method(D_METHOD("describe_control", "control"), &GeodriveWheel::describe_control);
	ClassDB::bind_method(D_METHOD("get_control_status"), &GeodriveWheel::get_control_status);
	ClassDB::bind_method(D_METHOD("is_control_down", "control"), &GeodriveWheel::is_control_down);
	ClassDB::bind_static_method("GeodriveWheel", D_METHOD("control_names"), &GeodriveWheel::control_names);
	ClassDB::bind_static_method("GeodriveWheel", D_METHOD("control_label", "control"), &GeodriveWheel::control_label);
	ClassDB::bind_method(D_METHOD("take_changed"), &GeodriveWheel::take_changed);
	ClassDB::bind_method(D_METHOD("saved_values"), &GeodriveWheel::saved_values);
#define GEODRIVE_WHEEL_PROPERTY(Type, Name) \
	ClassDB::bind_method(D_METHOD("set_" #Name, "value"), &GeodriveWheel::set_##Name); \
	ClassDB::bind_method(D_METHOD("get_" #Name), &GeodriveWheel::get_##Name); \
	ADD_PROPERTY(PropertyInfo(Variant::Type, #Name), "set_" #Name, "get_" #Name);
	GEODRIVE_WHEEL_PROPERTY(FLOAT, gain)
	GEODRIVE_WHEEL_PROPERTY(FLOAT, wheel_rotation)
	GEODRIVE_WHEEL_PROPERTY(FLOAT, steering_ratio)
	GEODRIVE_WHEEL_PROPERTY(FLOAT, max_force_nm)
	GEODRIVE_WHEEL_PROPERTY(BOOL, invert_force)
	GEODRIVE_WHEEL_PROPERTY(BOOL, invert_steering)
	GEODRIVE_WHEEL_PROPERTY(BOOL, force_enabled)
	GEODRIVE_WHEEL_PROPERTY(INT, throttle_axis)
	GEODRIVE_WHEEL_PROPERTY(INT, brake_axis)
	GEODRIVE_WHEEL_PROPERTY(INT, clutch_axis)
	ClassDB::bind_method(D_METHOD("get_debug"), &GeodriveWheel::get_debug);
#undef GEODRIVE_WHEEL_PROPERTY
}

void GeodriveWheel::set_car(Node* InCar) { Car = Object::cast_to<GeodriveCar>(InCar); }
Node* GeodriveWheel::get_car() const { return Car; }

void GeodriveWheel::set_gain(double V) { Wheel->Settings.Gain = float(V); }
double GeodriveWheel::get_gain() const { return Wheel->Settings.Gain; }
void GeodriveWheel::set_wheel_rotation(double V) { Wheel->Settings.WheelRotationDegrees = float(V); }
double GeodriveWheel::get_wheel_rotation() const { return Wheel->Settings.WheelRotationDegrees; }
void GeodriveWheel::set_steering_ratio(double V) { Wheel->Settings.SteeringRatio = float(V); }
double GeodriveWheel::get_steering_ratio() const { return Wheel->Settings.SteeringRatio; }
void GeodriveWheel::set_max_force_nm(double V) { Wheel->Settings.MaxForceNm = float(V); }
double GeodriveWheel::get_max_force_nm() const { return Wheel->Settings.MaxForceNm; }
void GeodriveWheel::set_invert_force(bool V) { Wheel->Settings.bInvertForce = V; }
bool GeodriveWheel::get_invert_force() const { return Wheel->Settings.bInvertForce; }
void GeodriveWheel::set_invert_steering(bool V) { Wheel->Settings.bInvertSteering = V; }
bool GeodriveWheel::get_invert_steering() const { return Wheel->Settings.bInvertSteering; }
void GeodriveWheel::set_force_enabled(bool V) { Wheel->Settings.bEnabled = V; }
bool GeodriveWheel::get_force_enabled() const { return Wheel->Settings.bEnabled; }
void GeodriveWheel::set_throttle_axis(int V) { Wheel->Settings.ThrottleAxis = V; }
int GeodriveWheel::get_throttle_axis() const { return Wheel->Settings.ThrottleAxis; }
void GeodriveWheel::set_brake_axis(int V) { Wheel->Settings.BrakeAxis = V; }
int GeodriveWheel::get_brake_axis() const { return Wheel->Settings.BrakeAxis; }
void GeodriveWheel::set_clutch_axis(int V) { Wheel->Settings.ClutchAxis = V; }
int GeodriveWheel::get_clutch_axis() const { return Wheel->Settings.ClutchAxis; }

bool GeodriveWheel::is_wheel_connected() const
{
	return Wheel->Loop.GetInput().bConnected;
}

String GeodriveWheel::get_status() const
{
	const geoffb::WheelInput Input = Wheel->Loop.GetInput();
	if (!Input.bConnected)
	{
		return "no wheel";
	}
	const geoffb::Status S = Wheel->Loop.GetStatus();
	return vformat("%s  force %d%%%s  %d Hz", String::utf8(Input.DeviceName.c_str()), int(std::lround(S.AppliedForce * 100.0f)),
		S.bClipping ? " CLIPPING" : "", int(std::lround(S.LoopHz)));
}

void GeodriveWheel::_ready()
{
	// the wheel wants the game's own window (DirectInput's exclusive access, for the forces)
	DisplayServer* Display = DisplayServer::get_singleton();
	void* Window = Display ? reinterpret_cast<void*>(Display->window_get_native_handle(DisplayServer::WINDOW_HANDLE)) : nullptr;
	State* S = Wheel.get();
	Wheel->Loop.Start(Window, [S](const std::string& Line)
	{
		std::lock_guard<std::mutex> Guard(S->LogLock);
		S->LogLines.push_back(Line);
	});
	set_physics_process(true);
	set_process(true);
	set_process_mode(PROCESS_MODE_ALWAYS);	// (the pedals and buttons are set from the menu, which pauses the game)
}

void GeodriveWheel::_exit_tree()
{
	Wheel->Loop.Stop();
}

void GeodriveWheel::_physics_process(double)
{
	{
		std::lock_guard<std::mutex> Guard(Wheel->LogLock);
		for (const std::string& Line : Wheel->LogLines)
		{
			UtilityFunctions::print(String::utf8(("Geodrive wheel: " + Line).c_str()));
		}
		Wheel->LogLines.clear();
	}

	const geoffb::WheelInput Input = Wheel->Loop.GetInput();
	if (!Car || !Input.bConnected || Car->get_autopilot())
	{
		Wheel->Loop.SetCommand(geoffb::Command(), nullptr);	// (no car, no wheel, or the game is driving: no forces)
		return;
	}

	// the wheel drives the car, and the car's steering torque comes back (none while paused or in the background)
	geoffb::Drive(Wheel->Settings, Input, Car->controls(), Wheel->Driver);
	Car->set_steering_ratio(Wheel->Settings.SteeringRatio);
	const geocar::VehicleState& Car_ = Car->state();
	DisplayServer* Display = DisplayServer::get_singleton();
	const bool bForeground = Display && Display->window_is_focused();
	const bool bPaused = get_tree() && get_tree()->is_paused();
	Wheel->Loop.SetCommand(geoffb::MakeCommand(Wheel->Settings, Car_.SpeedMS, Car_.KingpinTorque, Car_.SpeedFade, bForeground && !bPaused), Car->telemetry());
}

bool GeodriveWheel::load_settings(const String& IniPath)
{
	std::ifstream In(std::filesystem::u8path(IniPath.utf8().get_data()));
	if (!In)
	{
		return false;
	}
	// the game's section: Key=Value lines, arrays as the same key again
	std::map<std::string, std::vector<std::string>> Values;
	std::string Line;
	bool bInSection = false;
	while (std::getline(In, Line))
	{
		if (!Line.empty() && Line.back() == '\r')
		{
			Line.pop_back();
		}
		if (!Line.empty() && Line[0] == '[')
		{
			bInSection = Line == "[/Script/GeoFFB.GeoFFBSubsystem]";
			continue;
		}
		const size_t Equals = Line.find('=');
		if (bInSection && Equals != std::string::npos)
		{
			Values[Line.substr(0, Equals)].push_back(Line.substr(Equals + 1));
		}
	}
	if (Values.empty())
	{
		return false;
	}
	geoffb::WheelSettings& S = Wheel->Settings;
	auto Number = [&Values](const char* Key, auto& Out)
	{
		const auto Found = Values.find(Key);
		if (Found != Values.end() && !Found->second.empty())
		{
			Out = static_cast<std::remove_reference_t<decltype(Out)>>(std::atof(Found->second.back().c_str()));
		}
	};
	auto Flag = [&Values](const char* Key, bool& Out)
	{
		const auto Found = Values.find(Key);
		if (Found != Values.end() && !Found->second.empty())
		{
			Out = Found->second.back() == "True";
		}
	};
	Number("SteeringAxis", S.SteeringAxis);
	Number("ThrottleAxis", S.ThrottleAxis);
	Number("BrakeAxis", S.BrakeAxis);
	Number("ClutchAxis", S.ClutchAxis);
	Flag("bUseClutchPedal", S.bUseClutchPedal);
	for (const char* Key : { "AxisRest", "AxisPressed" })
	{
		const auto Found = Values.find(Key);
		if (Found != Values.end())
		{
			int32_t* Out = std::string(Key) == "AxisRest" ? S.AxisRest : S.AxisPressed;
			for (size_t i = 0; i < Found->second.size() && i < size_t(geoffb::NumAxes); ++i)
			{
				Out[i] = std::atoi(Found->second[i].c_str());
			}
		}
	}
	Flag("bInvertSteering", S.bInvertSteering);
	Number("WheelRotationDegrees", S.WheelRotationDegrees);
	Number("SteeringRatio", S.SteeringRatio);
	Number("FFBGain", S.Gain);
	Number("FFBDamper", S.Damper);
	Number("FFBFriction", S.Friction);
	Number("RackFriction", S.RackFriction);
	Number("LockupGain", S.LockupGain);
	Number("ABSGain", S.ABSGain);
	Number("ScrubGain", S.ScrubGain);
	Number("RoadGain", S.RoadGain);
	Number("GeoMaxForceNm", S.MaxForceNm);
	Number("MinForce", S.MinForce);
	Flag("bInvertFFB", S.bInvertForce);
	Flag("bFFBEnabled", S.bEnabled);
	// Unreal's map: (("LookLeft", 11),("LookRight", 9))
	if (const auto Found = Values.find("WheelControls"); Found != Values.end())
	{
		S.Controls.clear();
		const std::string& Text = Found->second.back();
		for (size_t At = Text.find("(\""); At != std::string::npos; At = Text.find("(\"", At + 2))
		{
			const size_t NameEnd = Text.find('"', At + 2);
			const size_t Comma = NameEnd == std::string::npos ? NameEnd : Text.find(',', NameEnd);
			if (Comma == std::string::npos)
			{
				break;
			}
			S.Controls[Text.substr(At + 2, NameEnd - At - 2)] = std::atoi(Text.c_str() + Comma + 1);
		}
	}
	Number("ShiftUpButton", S.ShiftUpButton);
	Number("ShiftDownButton", S.ShiftDownButton);
	return true;
}

// --- setting the pedals and the wheel's own buttons (Core's WheelSetup) ---

static std::string Std(const String& S) { return S.utf8().get_data(); }

void GeodriveWheel::begin_pedal_capture(const String& Pedal) { Wheel->Pedal.Begin(Std(Pedal), is_wheel_connected()); }
String GeodriveWheel::get_pedal_status() const { return String::utf8(Wheel->Pedal.Status.c_str()); }
int GeodriveWheel::get_pedal_axis(const String& Pedal) const { return geoffb::PedalAxis(Wheel->Settings, Std(Pedal)); }

void GeodriveWheel::begin_control_capture(const String& Control)
{
	Wheel->Control.Begin(Std(Control), Wheel->Loop.GetInput(), geoffb::Now());
}

void GeodriveWheel::clear_control(const String& Control)
{
	Wheel->Settings.Controls.erase(Std(Control));
	Wheel->bChanged = true;
}

String GeodriveWheel::describe_control(const String& Control) const { return String::utf8(geoffb::DescribeControl(Wheel->Settings, Std(Control)).c_str()); }
String GeodriveWheel::get_control_status() const { return String::utf8(Wheel->Control.Status.c_str()); }

bool GeodriveWheel::is_control_down(const String& Control) const
{
	return !Wheel->Control.IsActive() && geoffb::IsControlDown(Wheel->Settings, Wheel->Loop.GetInput(), Std(Control));
}

PackedStringArray GeodriveWheel::control_names()
{
	PackedStringArray Out;
	for (const std::string& Name : geoffb::ControlNames())
	{
		Out.push_back(String::utf8(Name.c_str()));
	}
	return Out;
}

String GeodriveWheel::control_label(const String& Control) { return String::utf8(geoffb::ControlLabel(Std(Control)).c_str()); }

bool GeodriveWheel::take_changed()
{
	const bool bChanged = Wheel->bChanged;
	Wheel->bChanged = false;
	return bChanged;
}

Dictionary GeodriveWheel::saved_values() const
{
	const geoffb::WheelSettings& S = Wheel->Settings;
	Dictionary Out;
	auto One = [&Out](const char* Key, int Value)
	{
		Array Values;
		Values.push_back(Value);
		Out[Key] = Values;
	};
	One("ThrottleAxis", S.ThrottleAxis);
	One("BrakeAxis", S.BrakeAxis);
	One("ClutchAxis", S.ClutchAxis);
	Array Rest, Pressed;
	for (int i = 0; i < geoffb::NumAxes; ++i)
	{
		Rest.push_back(S.AxisRest[i]);
		Pressed.push_back(S.AxisPressed[i]);
	}
	Out["AxisRest"] = Rest;
	Out["AxisPressed"] = Pressed;
	// Unreal's map: (("LookLeft", 11),("LookRight", 9))
	std::string Map;
	for (const auto& Entry : S.Controls)
	{
		Map += (Map.empty() ? "(\"" : ",(\"") + Entry.first + "\", " + std::to_string(Entry.second) + ")";
	}
	Array Controls;
	Controls.push_back(String::utf8(("(" + Map + ")").c_str()));
	Out["WheelControls"] = Controls;
	return Out;
}

void GeodriveWheel::_process(double)
{
	const geoffb::WheelInput Input = Wheel->Loop.GetInput();
	const double Now = geoffb::Now();
	if (Wheel->Pedal.IsActive() && Wheel->Pedal.Update(Wheel->Settings, Input, Now))
	{
		Wheel->bChanged = true;
	}
	if (Wheel->Control.IsActive() && Wheel->Control.Update(Wheel->Settings, Input, Now))
	{
		Wheel->bChanged = true;
	}
}

String GeodriveWheel::get_debug() const
{
	const geoffb::WheelInput Input = Wheel->Loop.GetInput();
	if (!Input.bConnected)
	{
		return "Wheel: not connected (retrying)";
	}
	const geoffb::WheelSettings& S = Wheel->Settings;
	const geoffb::Status St = Wheel->Loop.GetStatus();
	std::string Out = "Wheel: " + Input.DeviceName + "  |  FFB " + (!Input.bHasFFB ? "unsupported" : S.bEnabled ? "on" : "off");
	char Line[256];
	std::snprintf(Line, sizeof(Line), "  |  %.0f Hz", St.LoopHz);
	Out += Line;
	// raw axes, so pedals can be identified, with a bar for each
	for (int32_t i = 0; i < geoffb::NumAxes; ++i)
	{
		std::string Role;
		Role += i == S.SteeringAxis ? " steering" : "";
		Role += i == S.ThrottleAxis ? " throttle" : "";
		Role += i == S.BrakeAxis ? " brake" : "";
		Role += i == S.ClutchAxis ? " clutch" : "";
		const int Filled = int(std::lround(double(Input.Axes[i] - geoffb::AxisMin) / double(geoffb::AxisMax - geoffb::AxisMin) * 20.0));
		std::snprintf(Line, sizeof(Line), "\n%d %-7s [%s%s] %6d%s", int(i), geoffb::AxisName(i), std::string(size_t(std::clamp(Filled, 0, 20)), '#').c_str(),
			std::string(size_t(20 - std::clamp(Filled, 0, 20)), '.').c_str(), int(Input.Axes[i]), Role.c_str());
		Out += Line;
	}
	std::string Pressed;
	for (int32_t i = 0; i < geoffb::NumButtons; ++i)
	{
		if (Input.Buttons[i])
		{
			Pressed += std::to_string(i) + " ";
		}
	}
	Out += "\nButtons: " + (Pressed.empty() ? std::string("-") : Pressed);
	const geoffb::DriverState& D = Wheel->Driver;
	std::snprintf(Line, sizeof(Line), "\nSteering: wheel %+.0f deg -> road wheels %+.1f deg  |  throttle %.2f  brake %.2f  clutch %.2f",
		D.WheelDegrees, D.RoadDegrees, D.Throttle, D.Brake, D.Clutch);
	Out += Line;
	std::snprintf(Line, sizeof(Line), "\nFFB: output %+.2f%s  damper %.2f  friction %.2f  |  gain %.2f  max %.0f Nm  min %.2f%s", St.AppliedForce,
		St.bClipping ? " CLIPPING" : "", St.AppliedDamper, St.AppliedFriction, S.Gain, S.MaxForceNm, S.MinForce, S.bInvertForce ? "  inverted" : "");
	Out += Line;
	return String::utf8(Out.c_str());
}
