// Geodrive Core - GPL (see Core/README.md)

#include "CarMotion.h"

namespace geocar
{
	namespace
	{
		// (CarSim's frame to GeoBody's and back: x and y swapped. A turning rate is a handed quantity: swapped, it also
		// turns the other way)
		geobody::Vec3 ToBody(const Vec3& V) { return geobody::Vec3(V.Y, V.X, V.Z); }
		Vec3 FromBody(const geobody::Vec3& V) { return Vec3(V.Y, V.X, V.Z); }
		geobody::Vec3 SpinToBody(const Vec3& W) { return geobody::Vec3(-W.Y, -W.X, -W.Z); }
		Vec3 SpinFromBody(const geobody::Vec3& W) { return Vec3(-W.Y, -W.X, -W.Z); }

		/** The host's world for CarSim's sweeps; its pushes kept, to push the body with each of its steps */
		class Collector final : public CarWorld
		{
		public:
			explicit Collector(CarWorld& InHost) : Host(InHost) {}
			bool SweepSphere(const Vec3& Start, const Vec3& End, double Radius, SweepHit& Hit) override { return Host.SweepSphere(Start, End, Radius, Hit); }
			// (kept from the centre of mass, as it was: the car moves along during the step, its pushes with it)
			void AddForceAtPosition(const Vec3& Force, const Vec3& Position) override { Pushes.push_back({ ToBody(Force), ToBody(Position) - Centre }); }
			struct Push { geobody::Vec3 Force, Arm; };
			geobody::Vec3 Centre;
			std::vector<Push> Pushes;

		private:
			CarWorld& Host;
		};
	}

	void CarMotion::Use(const std::vector<geobody::Part>& Parts)
	{
		Frame.SetParts(Parts);
	}

	void CarMotion::Sync(const BodyState& S)
	{
		// (its own axes: x forward, y left, z up)
		const geobody::Quat Rotation = geobody::Quat::FromAxes(ToBody(S.Forward), ToBody(-S.Right), ToBody(S.Up));
		Frame.SetState(ToBody(S.Position), Rotation, ToBody(S.Velocity), SpinToBody(S.AngularVelocity));
	}

	BodyState CarMotion::State() const
	{
		BodyState S;
		const geobody::Quat& R = Frame.Rotation();
		S.Position = FromBody(Frame.Origin());
		S.Forward = FromBody(R.Rotate(geobody::Vec3(1.0, 0.0, 0.0)));
		S.Right = FromBody(R.Rotate(geobody::Vec3(0.0, -1.0, 0.0)));
		S.Up = FromBody(R.Rotate(geobody::Vec3(0.0, 0.0, 1.0)));
		S.CenterOfMass = FromBody(Frame.CentreOfMass());
		S.Velocity = FromBody(Frame.Velocity());
		S.AngularVelocity = SpinFromBody(Frame.Spin());
		return S;
	}

	void CarMotion::Step(CarWorld& World, CarSim& Sim, const CarSetup& Setup, const Controls& Input, const Vec3 (&WheelPositions)[NumWheels], float Dt)
	{
		// the car's forces from where it is now (CarSim sweeps its wheels once a step), then they and gravity move its body
		Collector Forces(World);
		Forces.Centre = Frame.CentreOfMass();
		Sim.Step(Forces, State(), Setup, Input, WheelPositions, Dt);
		geobody::Surroundings Round;
		Round.Gravity = geobody::Vec3(0.0, 0.0, -9.81);
		Frame.Step(Dt, Round, [&Forces](geobody::Body& On, double)
		{
			for (const Collector::Push& P : Forces.Pushes)
			{
				On.Push(P.Force, On.CentreOfMass() + P.Arm);
			}
		});
	}
}
