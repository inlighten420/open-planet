// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Cars made in code (no imported models): the building blocks (smooth lofted bodies, wheels, wings, tubes, boxes) and
// the cars built from them, each with its wheels, cameras, physics setup and engine sound. Every shape goes into one
// triangle list with a colour per corner; the colour's alpha is the surface: its roughness (0.3 paint, 0.05 glass,
// 0.9 rubber), with alpha 0 meaning a light that glows. Units are cm, in the car's own frame: X forward, Y right, Z up
// (left-handed, like the game's world). Triangles' corners run clockwise seen from the front (the Unreal way round);
// a right-handed host flips them.

#pragma once

#include "CarTypes.h"
#include "GeoBody/Body.h"

#include <functional>
#include <string>
#include <vector>

namespace geocar
{
	struct Vec2d
	{
		double X = 0.0, Y = 0.0;
	};

	/** Linear colour with the surface in alpha */
	struct Color
	{
		float R = 0.0f, G = 0.0f, B = 0.0f, A = 1.0f;
	};

	/** An orientation as the game gives it: degrees, pitch about Y (nose up), yaw about Z (to the right), roll about X */
	struct Rotator
	{
		float Pitch = 0.0f, Yaw = 0.0f, Roll = 0.0f;
	};

	struct CarMesh
	{
		std::vector<Vec3> Positions;
		std::vector<Vec3> Normals;
		std::vector<Color> Colors;
		std::vector<int32_t> Triangles;

		void AddVertex(const Vec3& Position, const Vec3& Normal, const Color& C);

		/** A triangle facing roughly along Facing (its winding is fixed up to match) */
		void AddTriangle(int32_t A, int32_t B, int32_t C, const Vec3& Facing);
	};

	/** One cross-section of a lofted body, as seen from the front: a rounded box that can narrow towards its top */
	struct LoftSection
	{
		float X = 0.0f;			// along the car
		float HalfWidth = 50.0f;	// at its widest
		float Bottom = 0.0f;
		float Top = 50.0f;
		float TopWidth = 1.0f;		// width at the top as a fraction of the widest (tumblehome)
		float Roundness = 3.0f;		// 2 = ellipse, higher = squarer
		float CentreY = 0.0f;		// for pieces off the middle (sidepods)
	};

	namespace shapes
	{
		/** Colours with their surface in alpha */
		Color Paint(const Color& C);
		Color Glass();
		Color Rubber();
		Color Carbon();
		Color Metal(float Brightness = 0.7f);
		Color Light(const Color& C);

		/** A smooth closed body through the sections (front to back or back to front), with its ends capped. ColorAt
		 *  picks each patch's colour from its centre; Keep (if set) leaves patches out, e.g. where the wheel arches are. */
		void Loft(CarMesh& Mesh, std::vector<LoftSection> Sections, int32_t Steps, int32_t Around,
			const std::function<Color(const Vec3&)>& ColorAt, const std::function<bool(const Vec3&)>& Keep = nullptr);

		/** A wheel around the Y axis at the origin, its outer face towards +Y: tyre with rounded shoulders, a rim with
		 *  spokes and a dark centre */
		void Wheel(CarMesh& Mesh, float Radius, float Width, float RimRadius, int32_t Spokes, const Color& RimColor);

		/** A box with this centre and half size, turned by Rotation about its centre */
		void Box(CarMesh& Mesh, const Vec3& Centre, const Vec3& HalfSize, const Color& C, const Rotator& Rotation = Rotator());

		/** A round tube along a path */
		void Tube(CarMesh& Mesh, const std::vector<Vec3>& Path, float Radius, const Color& C, int32_t Around = 10);

		/** A wing across the car (along Y): an aerofoil of this chord and thickness, its leading edge at LeadX/LeadZ,
		 *  tilted nose-down by Angle degrees, from Y0 to Y1 */
		void Wing(CarMesh& Mesh, float LeadX, float LeadZ, float Chord, float Thickness, float Angle, float Y0, float Y1, const Color& C);

		void Sphere(CarMesh& Mesh, const Vec3& Centre, const Vec3& Radii, const Color& C, int32_t Segments = 16);

		/** The inside of a wheel arch: part of a cylinder around the Y axis through Centre, from Y0 to Y1, from angle
		 *  StartDegrees to EndDegrees (0 = forward, 90 = up), facing inwards */
		void ArchLiner(CarMesh& Mesh, const Vec3& Centre, float Radius, float Y0, float Y1, float StartDegrees, float EndDegrees, const Color& C);
	}

	/** Which body a car wears. Template = the Unreal vehicle template's sports car (not ours to share: no mesh here). */
	enum class CarBody : uint8_t
	{
		Template,
		GT,	// the RS 4.0 (after a 911 GT3 RS)
		Formula,
		Hatch,
		Bakkie,
		Count
	};

	/** Where a car's wheels are, in its own frame (cm) */
	struct CarFrame
	{
		Vec3 Wheels[NumWheels];
		float Radius[NumWheels] = { 34.0f, 34.0f, 35.5f, 35.5f };

		float FrontX() const { return static_cast<float>(Wheels[0].X + Wheels[1].X) * 0.5f; }
		float RearX() const { return static_cast<float>(Wheels[2].X + Wheels[3].X) * 0.5f; }
		float Ground() const { return static_cast<float>(std::min(Wheels[0].Z - Radius[0], Wheels[2].Z - Radius[2])); }
	};

	/** How an engine sounds: what the engine-sound synthesiser is made from */
	struct EngineSound
	{
		std::vector<float> FiringAngles = { 0, 120, 240, 360, 480, 600 };	// crank angle of each firing over 720 degrees
		std::vector<int32_t> Banks = { 0, 1, 0, 1, 0, 1 };				// the exhaust bank each blows into
		float PipeLength[2] = { 1.10f, 1.16f };	// m, from each bank to the collector
		float Openness = 0.5f;		// muffler: 0 = deep and soft, 1 = open pipes
		float Turbo = 0.0f;			// 0 = none, 1 = big
		float Hybrid = 0.0f;		// electric whine with road speed
		float Crackle = 0.3f;		// pops when lifting off at high revs
		float RedlineRPM = 9000.0f;
	};

	/** Everything a body brings: its meshes, where its wheels go, its physics and its sound */
	struct CarDesign
	{
		std::string Name;
		CarMesh Body;
		CarMesh Wheel[2];	// front, rear; the outer face towards +Y
		CarFrame Frame;
		Vec3 CockpitCamera = Vec3(70.0, 0.0, 100.0);
		Vec3 BonnetCamera = Vec3(70.0, 0.0, 100.0);
		/** The highest the driver's eyes go (a tall driver's seat drops, as in a real car: their head under the roof,
		 *  not through it); 0 none (an open car) */
		float CockpitCeiling = 0.0f;

		/** The steering wheel, made round its hub with the rim facing the driver (-X); it sits at SteeringWheelAt with
		 *  the column tilted SteeringColumnPitch degrees (top towards the driver) and turns about the column */
		CarMesh SteeringWheel;
		Vec3 SteeringWheelAt;
		float SteeringColumnPitch = 20.0f;
		bool bOwnPhysics = false;	// false: keeps the car's current setup
		CarSetup Setup;
		/** What it's made of (CarParts; its frame: on the ground midway between the axles, x forward, y left, z up, m):
		 *  its body for the shared motion (CarMotion), and its Setup's mass, balance and inertia */
		std::vector<geobody::Part> Parts;
		EngineSound Sound;
	};

	namespace designs
	{
		const char* Name(CarBody Body);

		/** Builds a body for the car whose wheels are at Template (the GT keeps them; the others bring their own, on
		 *  the template's ground) */
		CarDesign Build(CarBody Body, const CarFrame& Template);

		/** The Unreal template car's wheels, for hosts without it: the frame the game's cars are built round */
		CarFrame DefaultTemplate();
	}
}
