// Geodrive for Godot - AGPL-3.0 (see LICENSE)
//
// GeodriveWheel: a steering wheel and pedals with force feedback, through Geodrive's engine-free Core (Core/GeoFFB:
// the DirectInput device, the ~500 Hz force thread with the tyres' effects, driving a car from the wheel), the same
// code the Unreal game uses. Give it the car (set_car); while a wheel is connected it drives the car, and the car's
// steering torque and tyres come back through the rim.

#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <memory>

namespace godot
{
	class GeodriveCar;

	class GeodriveWheel : public Node
	{
		GDCLASS(GeodriveWheel, Node)

	public:
		GeodriveWheel();
		~GeodriveWheel();

		/** The car the wheel drives (a GeodriveCar), or null */
		void set_car(Node* Car);
		Node* get_car() const;

		/** A wheel is connected and answering */
		bool is_wheel_connected() const;

		/** The wheel's name, the loop's rate and the force going out, for a HUD line */
		String get_status() const;

		/** Everything the wheel is doing, as the Unreal game's wheel overlay: the device and loop, every axis (and which
		 *  pedal it is), buttons held, the steering chain, the pedals, the force going out */
		String get_debug() const;

		/** Reads the wheel's settings the Unreal game saved (its GameUserSettings.ini: axes, calibration, rotation, ratio,
		 *  force strengths, paddles); false if it isn't there */
		bool load_settings(const String& IniPath);

		/** Settings (the same as the Unreal game's geo.Wheel / geo.FFB ones) */
		void set_gain(double Gain);
		double get_gain() const;
		void set_wheel_rotation(double Degrees);
		double get_wheel_rotation() const;
		void set_steering_ratio(double Ratio);
		double get_steering_ratio() const;
		void set_max_force_nm(double Nm);
		double get_max_force_nm() const;
		void set_invert_force(bool bInvert);
		bool get_invert_force() const;
		void set_invert_steering(bool bInvert);
		bool get_invert_steering() const;
		void set_force_enabled(bool bEnabled);
		bool get_force_enabled() const;
		void set_throttle_axis(int Axis);
		int get_throttle_axis() const;
		void set_brake_axis(int Axis);
		int get_brake_axis() const;
		void set_clutch_axis(int Axis);
		int get_clutch_axis() const;

		/** Setting a pedal ("throttle", "brake", "clutch"): press it all the way down and hold it (Core's WheelSetup, as the
		 *  Unreal game does it); the status says how it's going */
		void begin_pedal_capture(const String& Pedal);
		String get_pedal_status() const;
		int get_pedal_axis(const String& Pedal) const;

		/** The wheel's own buttons for the camera and seat (control_names): set by pressing one */
		void begin_control_capture(const String& Control);
		void clear_control(const String& Control);
		String describe_control(const String& Control) const;
		String get_control_status() const;
		bool is_control_down(const String& Control) const;
		static PackedStringArray control_names();
		static String control_label(const String& Control);

		/** True once after a pedal or control was set (or cleared): then save saved_values() */
		bool take_changed();

		/** The wheel's setup as the Unreal game saves it: key -> Array of the values (pedal axes, calibration, controls) */
		Dictionary saved_values() const;

		void _ready() override;
		void _process(double Delta) override;
		void _exit_tree() override;
		void _physics_process(double Delta) override;

	protected:
		static void _bind_methods();

	private:
		struct State;
		std::unique_ptr<State> Wheel;
		GeodriveCar* Car = nullptr;
	};
}
