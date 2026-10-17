// Geodrive Godot extension - GPL (see Core/README.md)

#include "geodrive_aircraft.h"

#include "GeoFlight/Flight.h"

#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>
#include <cmath>

using namespace godot;

namespace
{
	// Core's frame (x east, y north, z up) to Godot's (x east, y up, z south) and back
	geoflight::Vec3 ToCore(const Vector3& V) { return geoflight::Vec3(V.x, -V.z, V.y); }
	Vector3 ToGodot(const geoflight::Vec3& V) { return Vector3(float(V.X), float(V.Z), float(-V.Y)); }
	constexpr double Knots = 1.94384, Feet = 3.28084;
}

GeodriveAircraft::GeodriveAircraft() = default;
GeodriveAircraft::~GeodriveAircraft() = default;

void GeodriveAircraft::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("setup", "kind"), &GeodriveAircraft::setup);
	ClassDB::bind_method(D_METHOD("get_kind"), &GeodriveAircraft::get_kind);
	ClassDB::bind_method(D_METHOD("place", "ground", "heading", "speed"), &GeodriveAircraft::place);
	ClassDB::bind_method(D_METHOD("fly", "delta", "controls"), &GeodriveAircraft::fly);
	ClassDB::bind_method(D_METHOD("readout"), &GeodriveAircraft::readout);
	ClassDB::bind_method(D_METHOD("shift", "offset"), &GeodriveAircraft::shift);
	ClassDB::bind_method(D_METHOD("get_eye"), &GeodriveAircraft::get_eye);
	ClassDB::bind_method(D_METHOD("get_chase_distance"), &GeodriveAircraft::get_chase_distance);
	ClassDB::bind_method(D_METHOD("get_design_name"), &GeodriveAircraft::get_design_name);
	ClassDB::bind_method(D_METHOD("get_flap_names"), &GeodriveAircraft::get_flap_names);
	ClassDB::bind_method(D_METHOD("get_gear"), &GeodriveAircraft::get_gear);
	ClassDB::bind_method(D_METHOD("is_crashed"), &GeodriveAircraft::is_crashed);
}

void GeodriveAircraft::setup(int InKind)
{
	Kind = std::clamp(InKind, 0, 2);
	Sim = std::make_unique<geoflight::Aircraft>(Kind == 0 ? geoflight::FighterJet() : Kind == 1 ? geoflight::Airliner() : geoflight::RegionalJet());
}

void GeodriveAircraft::place(const Vector3& Ground, double Heading, double Speed)
{
	if (!Sim)
	{
		setup(Kind);
	}
	Sim->Place(ToCore(Ground), geoflight::Quat::FromHeading(Heading * 3.14159265358979 / 180.0), Speed);
	Show();
}

void GeodriveAircraft::fly(double Delta, const Dictionary& Controls)
{
	if (!Sim || !is_inside_tree())
	{
		return;
	}
	geoflight::Controls In;
	In.Pitch = double(Controls.get("pitch", 0.0));
	In.Roll = double(Controls.get("roll", 0.0));
	In.Yaw = double(Controls.get("yaw", 0.0));
	In.Throttle = double(Controls.get("throttle", 0.0));
	In.bAfterburner = bool(Controls.get("afterburner", false));
	In.Flaps = int(Controls.get("flaps", 0));
	In.bGearDown = bool(Controls.get("gear", true));
	In.Brakes = double(Controls.get("brakes", 0.0));
	In.bSpeedBrake = bool(Controls.get("speed_brake", false));
	In.Reverse = double(Controls.get("reverse", 0.0));
	PhysicsDirectSpaceState3D* Space = get_world_3d().is_valid() ? get_world_3d()->get_direct_space_state() : nullptr;
	const geoflight::RayFn Ray = [Space](const geoflight::Vec3& From, const geoflight::Vec3& To, geoflight::Vec3& OutHit, geoflight::Vec3& OutNormal)
	{
		if (!Space)
		{
			return false;
		}
		// (the world and other vehicles: layer 1. People (walker.gd: layer 2) never crash it: one getting out by its
		// wing isn't the ground)
		const Ref<PhysicsRayQueryParameters3D> Query = PhysicsRayQueryParameters3D::create(ToGodot(From), ToGodot(To), 1u);
		const Dictionary Hit = Space->intersect_ray(Query);
		if (Hit.is_empty())
		{
			return false;
		}
		OutHit = ToCore(Vector3(Hit["position"]));
		OutNormal = ToCore(Vector3(Hit["normal"]));
		return true;
	};
	Sim->Step(Delta, In, Ray);
	Show();
}

void GeodriveAircraft::Show()
{
	// its node where Core has it: Godot's x is the aircraft's right (-left), y its up, z its back (-forward)
	const geoflight::Quat& Q = Sim->Rotation();
	const Vector3 Right = ToGodot(-Q.Rotate(geoflight::Vec3(0.0, 1.0, 0.0)));
	const Vector3 Up = ToGodot(Q.Rotate(geoflight::Vec3(0.0, 0.0, 1.0)));
	const Vector3 Back = ToGodot(-Q.Rotate(geoflight::Vec3(1.0, 0.0, 0.0)));
	set_global_transform(Transform3D(Basis(Right, Up, Back), ToGodot(Sim->Position())));	// (the axes as columns)
}

Dictionary GeodriveAircraft::readout() const
{
	Dictionary Out;
	if (!Sim)
	{
		return Out;
	}
	const geoflight::Readout& R = Sim->Read();
	const geoflight::Design& D = Sim->GetDesign();
	Out["airspeed"] = R.Airspeed * Knots;
	Out["true_airspeed"] = R.TrueAirspeed * Knots;
	Out["ground_speed"] = R.GroundSpeed * Knots;
	Out["mach"] = R.Mach;
	Out["altitude"] = R.Altitude * Feet;
	Out["height"] = R.Height < 0.0 ? -1.0 : R.Height * Feet;
	Out["climb"] = R.Climb * Feet * 60.0;
	Out["heading"] = R.Heading;
	Out["pitch"] = R.Pitch;
	Out["bank"] = R.Bank;
	Out["alpha"] = R.Alpha;
	Out["g"] = R.G;
	Out["engine"] = R.Engine;
	Out["afterburner"] = R.bAfterburner;
	Out["reverse"] = R.bReverse;
	Out["spoilers"] = R.bSpoilers;
	Out["gear"] = R.Gear;
	Out["on_ground"] = R.bOnGround;
	Out["stall"] = R.bStall;
	Out["crashed"] = R.bCrashed;
	Out["crash"] = String::utf8(R.Crash.c_str());
	Out["has_afterburner"] = D.ThrustAfterburner > D.Thrust;
	return Out;
}

void GeodriveAircraft::shift(const Vector3& Offset)
{
	if (Sim)
	{
		Sim->Move(ToCore(Offset));	// (as it is: same speed, same attitude)
		Show();
	}
}

Vector3 GeodriveAircraft::get_eye() const
{
	const geoflight::Vec3 E = Sim ? Sim->GetDesign().Eye : geoflight::Vec3();
	return Vector3(float(-E.Y), float(E.Z), float(-E.X));
}

double GeodriveAircraft::get_chase_distance() const
{
	return Sim ? Sim->GetDesign().ChaseDistance : 20.0;
}

String GeodriveAircraft::get_design_name() const
{
	return Sim ? String::utf8(Sim->GetDesign().Name.c_str()) : String();
}

PackedStringArray GeodriveAircraft::get_flap_names() const
{
	PackedStringArray Out;
	if (Sim)
	{
		for (const std::string& Name : Sim->GetDesign().FlapNames)
		{
			Out.push_back(String::utf8(Name.c_str()));
		}
	}
	return Out;
}

double GeodriveAircraft::get_gear() const
{
	return Sim ? Sim->Read().Gear : 1.0;
}

bool GeodriveAircraft::is_crashed() const
{
	return Sim && Sim->Read().bCrashed;
}
