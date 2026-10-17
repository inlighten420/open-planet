// Geodrive for Godot - GPL (see Core/README.md)
//
// GeodriveCar: a car driven by Geodrive's engine-free Core (Core/GeoCar: suspension, tyres, drivetrain, assists) on a
// Godot RigidBody3D. Each physics step it reads the body, sweeps a sphere down at each wheel, and pushes the forces
// back in. The car's frame: origin on the ground halfway between the axles at ride height, -Z forward, +X right, +Y up.
// Its body, wheels and setup are Core's (Core/GeoCar/CarDesigns.cpp, shared with the Unreal game): for now the bakkie
// (a 2007 Isuzu KB300 TDi LX 4x2).

#pragma once

#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/physics_direct_body_state3d.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <memory>

namespace geocar
{
	struct Controls;
	struct VehicleState;
	struct CarTelemetry;
	struct EngineSound;
}

namespace godot
{
	class GeodriveCar : public RigidBody3D
	{
		GDCLASS(GeodriveCar, RigidBody3D)

	public:
		GeodriveCar();
		~GeodriveCar();

		/** Makes a kind of car's design and look ahead (shared by every car of its kind), so the car itself comes in
		 *  without a jolt: during the loading screen, say */
		static void prepare(int Body);
		static void forget_looks();

		/** The driver: steering in [-1, 1] (+ = right), pedals in [0, 1] */
		void set_input(double Steer, double Throttle, double Brake, double Handbrake);
		void set_brake_reverses(bool bOn);	// (C++ only: traffic stops on its brakes and stays in drive)
		void set_handbrake(double Handbrake);
		void shift_up();
		void shift_down();
		void set_automatic(bool bAutomatic);
		bool get_automatic() const;
		void set_diff_lock(bool bLocked);
		bool get_diff_lock() const;
		/** The key: off, no power and the engine spins down (quiet once it's stopped); on, it starts */
		void set_engine_on(bool bOn);
		bool get_engine_on() const;

		/** Clears the wheels' spin and the gearbox (call after moving the car by hand) */
		void reset_car();

		double get_speed_kmh() const;
		double get_rpm() const;
		int get_gear() const;

		/** Wheel I (0 front left, 1 front right, 2 rear left, 3 rear right) in the car's frame: where it sits on its
		 *  suspension, steered and turned as it rolls */
		Transform3D get_wheel_transform(int Wheel) const;
		double get_wheel_radius(int Wheel) const;

		/** Speed, gear, revs and the like, for a HUD line */
		String get_status() const;

		/** Every wheel's contact, load, spin and forces */
		String get_debug() const;

		/** Which car it is (Core's designs: 1 Geodrive GT, 2 Formula, 3 Hatchback 1.6, 4 Bakkie 3.0 TDi): set before it
		 *  goes in the scene (to change it, the game puts in a new car) */
		void set_body(int Body);
		int get_body() const;

		/** Someone else's car (networked): held where their updates put it (set_global_transform), its wheels steered and
		 *  rolling by set_remote_state */
		void set_remote(bool bOn);
		bool is_remote() const;
		void set_remote_state(double SteerDegrees, double SpeedMS);
		double get_steer_degrees() const;

		/** The engine stalled (a manual clutch let out too low: push the clutch in to restart) */
		bool is_stalled() const;

		/** It has a diff lock to lock (the bakkie) */
		bool has_diff_lock() const;

		/** Driven by the game (the robot driver): a steering wheel leaves it alone, as the Unreal game's autopilot */
		void set_autopilot(bool bOn);
		bool get_autopilot() const;

		/** Its own paint colour (traffic cars): set before it goes in the scene */
		void set_paint(const Color& Colour);

		/** How far the front wheels turn (degrees) */
		double get_max_steer_degrees() const;

		/** Between the front and rear axles (m) */
		double get_wheelbase() const;

		/** Driver aids, 0-10 (0 = off, 3 = the default): ABS and traction control */
		void set_abs_level(int Level);
		int get_abs_level() const;
		void set_tc_level(int Level);
		int get_tc_level() const;

		/** The car's name, and the driver's eyes in the car's frame */
		String get_car_name() const;
		Vector3 get_cockpit_camera() const;
		double get_cockpit_ceiling() const;	// (m, as the cockpit camera's height: the eyes go no higher)
		Vector3 get_bonnet_camera() const;

		/** The camera view (0 chase, 1 cockpit, 2 cockpit without the steering wheel, 3 bonnet): from inside, the glass
		 *  goes; the steering wheel shows in the cockpit view, turning by the road wheels' angle x the steering ratio */
		void set_view(int View);
		int get_view() const;
		void set_steering_ratio(double Ratio);

		/** For the wheel (C++ only): the controls it drives, the state after the last step, the telemetry the force
		 *  feedback thread reads */
		geocar::Controls& controls();
		const geocar::VehicleState& state() const;
		std::shared_ptr<const geocar::CarTelemetry> telemetry() const;
		const geocar::EngineSound& engine_sound() const;

		void _ready() override;
		void _process(double Delta) override;
		void _integrate_forces(PhysicsDirectBodyState3D* BodyState) override;

	protected:
		static void _bind_methods();

	private:
		struct Car;
		std::unique_ptr<Car> Sim;
	};
}
