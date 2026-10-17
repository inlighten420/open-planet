// Geodrive Core - GPL (see Core/README.md)
//
// A car on the shared physics (GeoBody): its body is its parts (CarParts), and each step the car's own forces (CarSim:
// its tyres, suspension, engine and aero, from where it is and how it's moving) and gravity push that body, and the one
// motion function every body uses moves it (its full inertia, the spin's own effects). A host that also resolves the
// car's collisions (Godot's) gives it the state it ends up in each step (Sync), and takes back how it's moving.
//
// CarSim's frame is the game's (x north, y east, z up: left-handed); GeoBody's is right-handed (x east, y north, z
// up): the two are the same but for x and y swapped.

#pragma once

#include "CarSim.h"
#include "GeoBody/Body.h"

#include <vector>

namespace geocar
{
	class CarMotion
	{
	public:
		/** Its body: what it's made of (a design's Parts) */
		void Use(const std::vector<geobody::Part>& Parts);

		/** Where it is and how it's moving, as the host has it (its collisions may have changed it) */
		void Sync(const BodyState& State);

		/** One step: the car's forces (Sim, sweeping its wheels in World) and gravity on its body, and the body moves */
		void Step(CarWorld& World, CarSim& Sim, const CarSetup& Setup, const Controls& Input, const Vec3 (&WheelPositions)[NumWheels], float Dt);

		/** Where it is and how it's moving now (CarSim's frame) */
		BodyState State() const;
		const geobody::Body& GetBody() const { return Frame; }

	private:
		geobody::Body Frame;
	};
}
