// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Bodies made of parts. Each part is a shape (a box, a cylinder, a sphere; solid or a shell with a wall) of a real
// material (steel, aluminium, glass, pine...), or a weighed lump (an engine, a person: a real mass in a box's room).
// From the parts come the body's mass, where its weight sits (the centre of mass) and how hard it is to turn about
// each axis (its inertia), with nothing typed in by hand: build a thing and it weighs what it would.
//
// One motion function moves every body: forces at points (gravity, the water's lift, and the force parts a body
// carries: tyres, wings, engines) push and turn it, and it moves and spins as a rigid body does, the spin's own
// effects included (a box thrown spinning about its middle axis tumbles, as a real one does).
//
// Floating comes from the parts too: each part knows its volume, so the water pushes up on what's under its surface
// (Archimedes), where it's under, and a body settles as deep and as level as its weight and shape make it.
//
// Engine-free, as the rest of Core: metres, kilograms, seconds; the world's frame x east, y north, z up; a body's own
// x forward, y left, z up (right-handed, as GeoFlight).

#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace geobody
{
	struct Vec3
	{
		double X = 0.0, Y = 0.0, Z = 0.0;
		Vec3() = default;
		Vec3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}
		Vec3 operator+(const Vec3& B) const { return { X + B.X, Y + B.Y, Z + B.Z }; }
		Vec3 operator-(const Vec3& B) const { return { X - B.X, Y - B.Y, Z - B.Z }; }
		Vec3 operator*(double S) const { return { X * S, Y * S, Z * S }; }
		Vec3 operator-() const { return { -X, -Y, -Z }; }
		Vec3& operator+=(const Vec3& B) { X += B.X; Y += B.Y; Z += B.Z; return *this; }
		Vec3& operator-=(const Vec3& B) { X -= B.X; Y -= B.Y; Z -= B.Z; return *this; }
		double Dot(const Vec3& B) const { return X * B.X + Y * B.Y + Z * B.Z; }
		Vec3 Cross(const Vec3& B) const { return { Y * B.Z - Z * B.Y, Z * B.X - X * B.Z, X * B.Y - Y * B.X }; }
		double Length() const { return std::sqrt(Dot(*this)); }
		Vec3 Normal() const { const double L = Length(); return L > 1e-12 ? *this * (1.0 / L) : Vec3(); }
	};

	/** A 3 x 3 matrix (an inertia, a rotation), rows */
	struct Mat3
	{
		double M[3][3] = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } };
		static Mat3 Diagonal(const Vec3& D);
		Vec3 operator*(const Vec3& V) const;
		Mat3 operator*(const Mat3& B) const;
		Mat3 operator+(const Mat3& B) const;
		Mat3 operator*(double S) const;
		Mat3 Transposed() const;
		Mat3 Inverse() const;
	};

	/** A rotation (a body's to the world's) */
	struct Quat
	{
		double W = 1.0, X = 0.0, Y = 0.0, Z = 0.0;
		Vec3 Rotate(const Vec3& V) const;
		Vec3 Unrotate(const Vec3& V) const;
		Quat operator*(const Quat& B) const;
		Quat Normal() const;
		Mat3 Matrix() const;
		static Quat FromAxisAngle(const Vec3& Axis, double Angle);
		/** The rotation that turns the body's own x, y and z into these (unit, at right angles) */
		static Quat FromAxes(const Vec3& X, const Vec3& Y, const Vec3& Z);
	};

	/** A material: what a cubic metre of it weighs (kg). Real values (handbook densities, room temperature) */
	struct Material
	{
		const char* Name;
		double Density;
	};
	namespace Materials
	{
		// metals
		inline constexpr Material Steel{ "steel", 7850.0 };
		inline constexpr Material StainlessSteel{ "stainless steel", 8000.0 };
		inline constexpr Material CastIron{ "cast iron", 7200.0 };
		inline constexpr Material Aluminium{ "aluminium", 2700.0 };
		inline constexpr Material Titanium{ "titanium", 4500.0 };
		inline constexpr Material Copper{ "copper", 8960.0 };
		inline constexpr Material Lead{ "lead", 11340.0 };
		// glass, plastics, composites, rubber
		inline constexpr Material Glass{ "glass", 2500.0 };
		inline constexpr Material Polypropylene{ "polypropylene", 905.0 };	// (bumpers, trim)
		inline constexpr Material ABS{ "ABS plastic", 1050.0 };
		inline constexpr Material Polycarbonate{ "polycarbonate", 1200.0 };
		inline constexpr Material CarbonFibre{ "carbon fibre", 1600.0 };
		inline constexpr Material Fibreglass{ "fibreglass", 1850.0 };
		inline constexpr Material Rubber{ "tyre rubber", 1150.0 };
		inline constexpr Material Foam{ "seat foam", 50.0 };
		// wood and building
		inline constexpr Material Pine{ "pine", 500.0 };
		inline constexpr Material Oak{ "oak", 750.0 };
		inline constexpr Material Plywood{ "plywood", 600.0 };
		inline constexpr Material Concrete{ "concrete", 2400.0 };
		inline constexpr Material Brick{ "brick", 1900.0 };
		// liquids
		inline constexpr Material Water{ "water", 1000.0 };
		inline constexpr Material SeaWater{ "sea water", 1025.0 };
		inline constexpr Material Diesel{ "diesel", 832.0 };
		inline constexpr Material Petrol{ "petrol", 745.0 };
		inline constexpr Material JetFuel{ "Jet A-1", 804.0 };
		// air at sea level (15 C)
		inline constexpr Material Air{ "air", 1.225 };
	}

	enum class Shape
	{
		Box,		// Size: its length (x), width (y), height (z)
		Cylinder,	// Size.X its length along its own x, Size.Y its diameter
		Sphere		// Size.X its diameter
	};

	/** A part of a body */
	struct Part
	{
		std::string Name;
		Shape Form = Shape::Box;
		Vec3 Size = Vec3(1.0, 1.0, 1.0);	// m (see Shape)
		Vec3 At;							// its middle, in the body's frame (m)
		Quat Turn;							// turned in the body's frame (a cylinder along y: about z by 90 degrees)
		Material Of = Materials::Steel;
		double Wall = 0.0;		// m: a shell this thick (a panel, a tube, a tank), 0 solid
		double Mass = 0.0;		// kg: a weighed lump (an engine, a person, a load) in its shape's room, not its material's
		bool bSealed = false;	// a shell that keeps the water out: it displaces its whole outside (a hull, a tank)

		/** Its own (each worked out from its shape and material, or its weighed mass) */
		double GetVolume() const;		// its material's (a shell's walls), m3
		double GetOuterVolume() const;	// inside its outside, m3
		double GetMass() const;
		Mat3 GetInertia() const;		// about its own middle, in the body's frame
		/** The water it pushes aside wholly under (m3): a sealed shell its outside, a lump its mass over its material's
		 *  density (Of: what it's mostly made of, on average), anything else its material */
		double GetDisplaced() const { return bSealed ? GetOuterVolume() : (Mass > 0.0 ? Mass / Of.Density : GetVolume()); }
	};

	/** A part's helpers, to build bodies from */
	Part Box(const std::string& Name, const Vec3& At, const Vec3& Size, const Material& Of, double Wall = 0.0);
	Part Cylinder(const std::string& Name, const Vec3& At, double Length, double Diameter, const Vec3& Axis, const Material& Of, double Wall = 0.0);
	Part Sphere(const std::string& Name, const Vec3& At, double Diameter, const Material& Of, double Wall = 0.0);
	Part Lump(const std::string& Name, const Vec3& At, const Vec3& Size, double Kg, const Material& MostlyOf = Materials::Steel);

	/** A body's mass, where it sits, and how hard it is to turn (about the centre of mass, the body's axes) */
	struct MassProperties
	{
		double Mass = 0.0;
		Vec3 CentreOfMass;	// the body's frame
		Mat3 Inertia;		// kg m2
		double Displaced = 0.0;	// m3 of water it would push aside, wholly under
	};
	MassProperties Measure(const std::vector<Part>& Parts);

	/** What's round a body: gravity, the air, the water */
	struct Surroundings
	{
		Vec3 Gravity = Vec3(0.0, 0.0, -9.80665);
		double AirDensity = 1.225;
		double WaterDensity = 1025.0;	// (the sea's)
		/** The water's surface under a point (world x, y): false where there's none */
		std::function<bool(double X, double Y, double& OutLevel)> Water;
	};

	class Body;

	/** Something on a body that pushes it: a tyre, a wing, an engine, a propeller. Each step it looks at the body (where
	 *  it is, how it's moving) and pushes it (Body::Push) */
	class ForcePart
	{
	public:
		virtual ~ForcePart() = default;
		virtual void Apply(Body& On, const Surroundings& Round, double Dt) = 0;
	};

	/** A rigid body of parts, moved by the one motion function */
	class Body
	{
	public:
		Body() = default;
		explicit Body(std::vector<Part> InParts) { SetParts(std::move(InParts)); }

		/** Its parts (measured again: its mass, centre of mass and inertia follow) */
		void SetParts(std::vector<Part> InParts);
		const std::vector<Part>& GetParts() const { return Parts; }
		const MassProperties& GetMass() const { return Props; }

		/** Something that pushes it, kept and applied every step */
		void AddForcePart(std::shared_ptr<ForcePart> Part) { Forces.push_back(std::move(Part)); }

		/** Put it somewhere (its own origin, as the parts are measured from), still */
		void Place(const Vec3& Origin, const Quat& Rotation);

		/** Steps it (cut into steps of no more than 1/240 s): gravity, the water, its force parts, Also (the owner's own
		 *  pushes each step, as a vehicle's from its state: an aircraft's wings and engines), then the motion */
		using PushFn = std::function<void(Body& On, double Dt)>;
		void Step(double Seconds, const Surroundings& Round, const PushFn& Also = nullptr);

		/** Moved as it is (the game's origin moved) */
		void Move(const Vec3& By) { Pos += By; }

		/** A push (N) at a point (world), this step: force parts call it */
		void Push(const Vec3& Force, const Vec3& WorldPoint);
		void Twist(const Vec3& WorldTorque);

		// where it is and how it's moving
		Vec3 Origin() const { return Pos - Rot.Rotate(Props.CentreOfMass); }
		const Vec3& CentreOfMass() const { return Pos; }	// (world)
		const Quat& Rotation() const { return Rot; }
		const Vec3& Velocity() const { return Vel; }	// of its centre of mass (world)
		Vec3 Spin() const { return Rot.Rotate(Omega); }	// rad/s (world)
		const Vec3& OwnSpin() const { return Omega; }	// (its own frame)
		Vec3 ToWorld(const Vec3& Local) const { return Origin() + Rot.Rotate(Local); }
		Vec3 VelocityAt(const Vec3& WorldPoint) const { return Vel + Spin().Cross(WorldPoint - Pos); }
		void SetVelocity(const Vec3& V) { Vel = V; }
		void SetSpin(const Vec3& WorldOmega) { Omega = Rot.Unrotate(WorldOmega); }
		/** Where it is and how it's moving, all at once (a host that moves it too: its collisions) */
		void SetState(const Vec3& InOrigin, const Quat& Rotation, const Vec3& Velocity, const Vec3& WorldOmega)
		{
			Rot = Rotation.Normal();
			Pos = InOrigin + Rot.Rotate(Props.CentreOfMass);
			Vel = Velocity;
			Omega = Rot.Unrotate(WorldOmega);
		}
		double Energy() const;	// its kinetic energy (J): moving and spinning

		/** How much of it the water holds up (m3 under, this step) */
		double Submerged() const { return Under; }

	private:
		void Substep(double Dt, const Surroundings& Round, const PushFn& Also);
		void Float(const Surroundings& Round);

		std::vector<Part> Parts;
		MassProperties Props;
		Mat3 InverseInertia;
		std::vector<std::shared_ptr<ForcePart>> Forces;
		std::vector<std::vector<Vec3>> Samples;	// each part's points (its frame) the water's checked at

		Vec3 Pos, Vel, Omega;	// Pos the centre of mass (world); Omega in its own frame
		Quat Rot;
		Vec3 Force, Torque;		// this step's (world)
		double Under = 0.0;
	};
}
