// Geodrive Godot extension - AGPL-3.0 (see LICENSE)
//
// An aircraft in Godot: Core's flight model (GeoFlight/Flight.h) flown in Godot's world. Each physics step it asks
// Core to fly on with the pilot's controls, answering Core's "what's along this line" with Godot's ray casts (the
// ground, roads, buildings), and puts itself where Core says it is. It has no collision of its own: Core's hard points
// (nose, tail, wingtips, engines) are its body. Godot's frame: x east, y up, z south, m; Core's: x east, y north, z up.

#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <memory>

namespace geoflight
{
	class Aircraft;
}

namespace godot
{
	class GeodriveAircraft : public Node3D
	{
		GDCLASS(GeodriveAircraft, Node3D)

	public:
		GeodriveAircraft();
		~GeodriveAircraft();

		/** Which aircraft: 0 the fighter jet, 1 the airliner, 2 the regional jet */
		void setup(int Kind);
		int get_kind() const { return Kind; }
		/** On the ground at a point (its wheels there), facing a heading (degrees clockwise from north), at a speed (m/s) */
		void place(const Vector3& Ground, double Heading, double Speed);
		/** Fly on: controls pitch, roll, yaw (-1 to 1), throttle (0 to 1), afterburner, flaps (setting), gear (down),
		 *  brakes (0 to 1), speed_brake */
		void fly(double Delta, const Dictionary& Controls);
		/** The instruments (knots, feet, feet a minute, degrees, g...) */
		Dictionary readout() const;
		/** Godot's 0 moved (GeodriveWorld's origin_shifted): the aircraft with it */
		void shift(const Vector3& Offset);
		/** The pilot's eyes (this node's frame) and the chase camera's distance (m) */
		Vector3 get_eye() const;
		double get_chase_distance() const;
		String get_design_name() const;
		PackedStringArray get_flap_names() const;
		/** Each wheel's strut pushed in (m), and the gear's state (0 up, 1 down) */
		double get_gear() const;
		bool is_crashed() const;

	protected:
		static void _bind_methods();

	private:
		void Show();

		std::unique_ptr<geoflight::Aircraft> Sim;
		int Kind = 0;
	};
}
