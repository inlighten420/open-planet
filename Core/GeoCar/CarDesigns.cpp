// Geodrive Core - GPL (see Core/README.md)
//
// The game's cars, made in code: the RS 4.0 (after a Porsche 911 GT3 RS, the 991 of 2016), the Formula car, the
// Hatchback 1.6 (after a Suzuki Swift Sport) and the Bakkie 3.0 TDi (after a 2007 Isuzu KB300 TDi LX double cab), each
// with its wheels, cameras, physics and engine sound.

#include "CarBody.h"
#include "CarParts.h"

#include <algorithm>
#include <cmath>

namespace geocar
{
namespace CarDesignsImpl
{
	constexpr float Pi = 3.14159265358979323846f;

	float Lerp(float A, float B, float T) { return A + T * (B - A); }
	float Sq(float V) { return V * V; }
	double Sq(double V) { return V * V; }
	float Rad(float Degrees) { return Degrees * (Pi / 180.0f); }
	float Deg(float Radians) { return Radians * (180.0f / Pi); }
	Color Color4(float R, float G, float B, float A = 1.0f) { return { R, G, B, A }; }
	Rotator Rot(float Pitch, float Yaw, float Roll) { return { Pitch, Yaw, Roll }; }
}

namespace CarDesignsImpl
{
	using namespace shapes;

	/** Removes body panels where a wheel turns: within the arch's radius of the axle, beyond the inside of the tyre */
	bool OutsideArches(const Vec3& P, const CarFrame& Frame, float Clearance, float Inner)
	{
		for (int32_t i = 0; i < NumWheels; ++i)
		{
			const Vec3& W = Frame.Wheels[i];
			const float Arch = Frame.Radius[i] + Clearance;
			if (Sq(P.X - W.X) + Sq(P.Z - W.Z) < Arch * Arch && std::fabs(P.Y) > Inner && P.Y * W.Y > 0.0)
			{
				return false;
			}
		}
		return true;
	}

	/** Linear in between points (x, value), held at the ends; x runs either way */
	float Profile(const std::vector<Vec2d>& Points, float X)
	{
		std::vector<Vec2d> Sorted = Points;
		std::sort(Sorted.begin(), Sorted.end(), [](const Vec2d& A, const Vec2d& B) { return A.X < B.X; });
		if (X <= Sorted[0].X)
		{
			return static_cast<float>(Sorted[0].Y);
		}
		for (int32_t i = 0; i + 1 < static_cast<int32_t>(Sorted.size()); ++i)
		{
			if (X <= Sorted[i + 1].X)
			{
				const double T = (X - Sorted[i].X) / std::max(Sorted[i + 1].X - Sorted[i].X, 1e-3);
				return static_cast<float>(Sorted[i].Y + T * (Sorted[i + 1].Y - Sorted[i].Y));
			}
		}
		return static_cast<float>(Sorted.back().Y);
	}

	/** A cabin's colours, as a real one is laid out: the roof panel (from RoofFront back to RoofRear, along the car)
	 *  and a frame round each window in body colour; glass in the windscreen (ahead of the roof), the back window
	 *  (behind it; an upright one up to BackTop) and the side windows (from the waist, Belt, to the roof rail, but for
	 *  the B and C pillars at Pillars' X). Sections: the cabin's own loft (its width and height along the car) */
	std::function<Color(const Vec3&)> Glasshouse(const std::vector<LoftSection>& Sections, float RoofFront, float RoofRear, float Belt,
		const std::vector<float>& Pillars, const Color& Body, float BackTop = 1e6f)
	{
		return [Sections, RoofFront, RoofRear, Belt, Pillars, Body, BackTop](const Vec3& P) -> Color
		{
			const float X = static_cast<float>(P.X), Z = static_cast<float>(P.Z);
			// the section here (between the two either side of X)
			LoftSection Here = Sections.front();
			for (size_t i = 0; i + 1 < Sections.size(); ++i)
			{
				const LoftSection& A = Sections[i];
				const LoftSection& B = Sections[i + 1];
				if ((X - A.X) * (X - B.X) <= 0.0f)
				{
					const float T = std::fabs(B.X - A.X) > 1e-3f ? (X - A.X) / (B.X - A.X) : 0.0f;
					Here.HalfWidth = Lerp(A.HalfWidth, B.HalfWidth, T);
					Here.Bottom = Lerp(A.Bottom, B.Bottom, T);
					Here.Top = Lerp(A.Top, B.Top, T);
					Here.TopWidth = Lerp(A.TopWidth, B.TopWidth, T);
					break;
				}
			}
			// cm of body round each window: 6 along the roof, but 3 each side of the A pillar's line (the windscreen's edge
			// and the door glass's; that close to the driver, wider made a pillar a hand's width across)
			const float Frame = X > RoofFront ? 3.0f : 6.0f;
			if (Z < Belt + 1.0f)
			{
				return Body;
			}
			const float Up = std::clamp((Z - Here.Bottom) / std::max(Here.Top - Here.Bottom, 1.0f), 0.0f, 1.0f);
			const float Half = Here.HalfWidth * Lerp(1.0f, Here.TopWidth, Up);
			const float Y = static_cast<float>(std::fabs(P.Y));
			if (Y > Half * 0.84f)
			{
				// a side window, up to the roof rail, but for the pillars between them
				if (Z > Here.Top - Frame)
				{
					return Body;
				}
				for (const float Pillar : Pillars)
				{
					if (std::fabs(X - Pillar) < 5.5f)
					{
						return Body;
					}
				}
				return Glass();
			}
			// the roof, the header rails at its ends, the pillars beside the windscreen and back window
			if ((X <= RoofFront + Frame && X >= RoofRear - Frame) || Y > Half * 0.84f - Frame || (X < RoofRear && Z > BackTop))
			{
				return Body;
			}
			return Glass();
		};
	}

	/** The inside of what was just built (from FirstTriangle): a lining facing in, Inset cm inside it, of Inside's
	 *  colour, behind everything but the glass (a cabin's headlining and pillar trim, seen from the seats) */
	void Lining(CarMesh& Mesh, size_t FirstTriangle, const Color& Inside, float Inset)
	{
		const size_t End = Mesh.Triangles.size();
		for (size_t t = FirstTriangle; t + 2 < End; t += 3)
		{
			const int32_t A = Mesh.Triangles[t], B = Mesh.Triangles[t + 1], C = Mesh.Triangles[t + 2];
			if (Mesh.Colors[size_t(A)].A == Glass().A)
			{
				continue;
			}
			int32_t Corners[3];
			const int32_t From[3] = { A, B, C };
			Vec3 Facing;
			for (int32_t k = 0; k < 3; ++k)
			{
				const Vec3 N = Mesh.Normals[size_t(From[k])];
				Facing = Facing + N;
				Mesh.AddVertex(Mesh.Positions[size_t(From[k])] - N * Inset, -N, Inside);
				Corners[k] = int32_t(Mesh.Positions.size()) - 1;
			}
			Mesh.AddTriangle(Corners[0], Corners[1], Corners[2], -Facing);
		}
	}

	/** A soft interior finish (cloth, vinyl) */
	Color Cloth(const Color& C) { return { C.R, C.G, C.B, 0.9f }; }

	/** A steering wheel round its hub, the rim facing the driver (-X): a rim on three spokes, a mark at the top */
	void SteeringWheel(CarMesh& Mesh, float Radius, const Color& Mark, bool bFourSpokes = false, const Color& Grip = Rubber())
	{
		std::vector<Vec3> Rim;
		for (int32_t Step = 0; Step <= 40; ++Step)
		{
			const float A = 2.0f * Pi * Step / 40.0f;
			Rim.push_back(Vec3(0.0f, Radius * std::cos(A), Radius * std::sin(A)));
		}
		Tube(Mesh, Rim, 1.9f, Grip, 10);
		// the spokes: the two sides and down; or four, two each side sloping down (a bakkie's, round an airbag)
		const std::vector<float> Spokes = bFourSpokes ? std::vector<float>{ -15.0f, 195.0f, 240.0f, 300.0f } : std::vector<float>{ 0.0f, 180.0f, 270.0f };
		for (const float Degrees : Spokes)
		{
			const float A = Rad(Degrees);
			Tube(Mesh, { Vec3(1.5f, 0.0f, 0.0f), Vec3(0.0f, Radius * std::cos(A), Radius * std::sin(A)) }, bFourSpokes ? 1.8f : 1.3f, bFourSpokes ? Grip : Carbon(), 8);
		}
		if (bFourSpokes)
		{
			Tube(Mesh, { Vec3(3.5f, 0.0f, -1.0f), Vec3(-0.5f, 0.0f, -1.0f) }, Radius * 0.42f, Grip, 24);	// the airbag's round boss
		}
		else
		{
			Box(Mesh, Vec3(2.0f, 0.0f, 0.0f), Vec3(2.5f, 6.0f, 5.0f), Carbon());
		}
		Box(Mesh, Vec3(0.0f, 0.0f, Radius), Vec3(2.0f, 0.7f, 2.0f), Mark);
	}

	/** The driver's view: a padded dashboard under the windscreen with an instrument cluster (two round dials) behind
	 *  the wheel, a centre stack (vents, the radio), a console with the gear lever and handbrake, door cards with
	 *  armrests, seats with headrests and, under a roof (RoofLine), a mirror on the windscreen. Front = the dashboard's
	 *  front edge (at the windscreen), Back = its edge by the driver's knees */
	void Cockpit(CarDesign& D, float Front, float Back, float HalfWidth, float Top, float Floor, float SeatX, const Color& Trim,
		const std::vector<Vec2d>* RoofLine = nullptr, int32_t Dials = 2, const Color* DashColour = nullptr)
	{
		// (Dials: 2, the rev counter and speedometer; 3, the rev counter, a bigger speedometer in the middle and the fuel
		// and temperature; DashColour: the dashboard's, black if none)
		const Color Dash = DashColour ? *DashColour : Carbon();
		const Color Pad = DashColour ? *DashColour : Cloth(Color4(0.03f, 0.03f, 0.032f));
		const Color Seat = Cloth(Trim);
		const Vec3& Wheel = D.SteeringWheelAt;
		const float DriverY = static_cast<float>(Wheel.Y);
		const float Mid = (Front + Back) * 0.5f;
		// the dashboard: its face, and a padded top sloping down to the windscreen
		Box(D.Body, Vec3(Mid, 0.0f, Top - 12.0f), Vec3((Front - Back) * 0.5f, HalfWidth, 11.0f), Dash);
		Box(D.Body, Vec3(Mid + 2.0f, 0.0f, Top - 1.0f), Vec3((Front - Back) * 0.5f + 3.0f, HalfWidth, 1.5f), Pad, Rot(-6.0f, 0.0f, 0.0f));
		// the instrument cluster under its hood: the speedometer and the rev counter, white marks, orange needles
		Box(D.Body, Vec3(Back + 8.0f, DriverY, Top + 2.5f), Vec3(9.0f, 15.0f, 3.0f), Pad, Rot(-8.0f, 0.0f, 0.0f));
		Box(D.Body, Vec3(Back + 3.0f, DriverY, Top - 5.0f), Vec3(1.0f, 14.0f, 6.5f), Dash);
		struct DialAt { float Y, Radius; };
		const std::vector<DialAt> Faces = Dials == 3 ? std::vector<DialAt>{ { -9.5f, 4.2f }, { 0.0f, 5.4f }, { 9.5f, 4.2f } }
			: std::vector<DialAt>{ { -6.8f, 5.0f }, { 6.8f, 5.0f } };
		for (const DialAt& F : Faces)
		{
			const Vec3 Dial(Back + 1.8f, DriverY + F.Y, Top - 5.0f);
			Tube(D.Body, { Dial + Vec3(0.2f, 0.0f, 0.0f), Dial - Vec3(0.2f, 0.0f, 0.0f) }, F.Radius + 0.6f, Metal(0.55f), 24);	// the bezel
			Tube(D.Body, { Dial, Dial - Vec3(0.5f, 0.0f, 0.0f) }, F.Radius, Carbon(), 24);	// the face
			for (int32_t Mark = 0; Mark < 9; ++Mark)
			{
				const float A = Rad(-135.0f + Mark * 33.75f);
				Box(D.Body, Dial + Vec3(-0.55f, F.Radius * 0.84f * std::sin(A), F.Radius * 0.84f * std::cos(A)), Vec3(0.05f, 0.25f, 0.6f), Light(Color4(0.8f, 0.8f, 0.78f)), Rot(0.0f, 0.0f, -Deg(A)));
			}
			const float Needle = Rad(-110.0f);	// (resting, on its stop)
			Box(D.Body, Dial + Vec3(-0.6f, F.Radius * 0.38f * std::sin(Needle), F.Radius * 0.38f * std::cos(Needle)), Vec3(0.05f, 0.18f, F.Radius * 0.4f), Light(Color4(0.95f, 0.4f, 0.05f)), Rot(0.0f, 0.0f, -Deg(Needle)));
		}
		// the centre stack: two vents and the radio's display
		Box(D.Body, Vec3(Back + 3.0f, 0.0f, Top - 13.0f), Vec3(3.0f, 12.0f, 10.0f), Dash);
		for (const float Side : { -1.0f, 1.0f })
		{
			Box(D.Body, Vec3(Back - 0.2f, Side * 6.0f, Top - 6.5f), Vec3(0.3f, 4.5f, 2.8f), Metal(0.45f));
			Box(D.Body, Vec3(Back - 0.4f, Side * 6.0f, Top - 6.5f), Vec3(0.3f, 3.8f, 2.1f), Dash);
		}
		Box(D.Body, Vec3(Back - 0.2f, 0.0f, Top - 14.0f), Vec3(0.3f, 8.0f, 2.5f), Light(Color4(0.05f, 0.18f, 0.2f)));
		for (const float Y : { -6.0f, 0.0f, 6.0f })	// the heater's knobs
		{
			Tube(D.Body, { Vec3(Back - 0.2f, Y, Top - 19.5f), Vec3(Back - 1.6f, Y, Top - 19.5f) }, 1.6f, Carbon(), 12);
		}
		// the steering column
		Tube(D.Body, { Wheel + Vec3(3.0f, 0.0f, 0.0f), Vec3(Back + 4.0f, DriverY, Wheel.Z + 6.0f) }, 3.0f, Dash, 10);
		// the console between the seats, the gear lever and handbrake on it
		const float ConsoleTop = (Top + Floor) * 0.5f - 6.0f + (Top - Floor) * 0.5f - 8.0f;
		Box(D.Body, Vec3((Back + SeatX) * 0.5f, 0.0f, (Top + Floor) * 0.5f - 6.0f), Vec3((Back - SeatX) * 0.5f + 4.0f, 10.0f, (Top - Floor) * 0.5f - 8.0f), Dash);
		const float Lever = Back - (Back - SeatX) * 0.35f;
		Tube(D.Body, { Vec3(Lever, 0.0f, ConsoleTop), Vec3(Lever - 3.0f, 0.0f, ConsoleTop + 14.0f) }, 0.9f, Metal(0.4f), 8);
		Sphere(D.Body, Vec3(Lever - 3.0f, 0.0f, ConsoleTop + 15.5f), Vec3(2.4f, 2.4f, 2.6f), Pad, 10);
		Box(D.Body, Vec3(SeatX + 12.0f, -DriverY * 0.25f, ConsoleTop + 3.0f), Vec3(10.0f, 1.6f, 1.5f), Dash, Rot(-14.0f, 0.0f, 0.0f));
		for (const float Side : { -1.0f, 1.0f })
		{
			// a door card with an armrest
			const float DoorY = Side * (HalfWidth + 7.0f);
			Box(D.Body, Vec3((Back + SeatX) * 0.5f - 10.0f, DoorY, (Top + Floor) * 0.5f + 2.0f), Vec3((Back - SeatX) * 0.5f + 18.0f, 1.5f, (Top - Floor) * 0.5f), Seat);
			Box(D.Body, Vec3(SeatX + 10.0f, DoorY - Side * 4.0f, Floor + 40.0f), Vec3(14.0f, 3.0f, 2.0f), Pad);
			// the seat: cushion, backrest, headrest
			const float Y = Side * std::fabs(DriverY);
			Box(D.Body, Vec3(SeatX, Y, Floor + 12.0f), Vec3(25.0f, 24.0f, 6.0f), Seat);
			Box(D.Body, Vec3(SeatX - 28.0f, Y, Floor + 45.0f), Vec3(6.0f, 24.0f, 32.0f), Seat, Rot(15.0f, 0.0f, 0.0f));
			Box(D.Body, Vec3(SeatX - 40.0f, Y, Floor + 82.0f), Vec3(4.5f, 13.0f, 8.0f), Seat, Rot(15.0f, 0.0f, 0.0f));
		}
		// the mirror, at the windscreen's top, above the driver's line of sight
		if (RoofLine)
		{
			const float Eye = static_cast<float>(D.CockpitCamera.Z);
			float X = Front;
			while (X > Back - 150.0f && Profile(*RoofLine, X) < Eye + 17.0f)
			{
				X -= 1.0f;
			}
			const Vec3 At(X - 4.0f, 0.0f, Eye + 12.0f);
			Tube(D.Body, { Vec3(X - 1.0f, 0.0f, Profile(*RoofLine, X - 1.0f)), At }, 0.8f, Dash, 8);
			Box(D.Body, At, Vec3(1.8f, 11.0f, 3.2f), Dash);
			Box(D.Body, At - Vec3(1.9f, 0.0f, 0.0f), Vec3(0.1f, 10.4f, 2.7f), Metal(0.85f));
		}
	}

	// --- Formula: an open-wheel single seater, modelled on today's grand prix cars ---
	CarDesign BuildFormula(const CarFrame& Template)
	{
		CarDesign D;
		D.Name = "Formula";
		D.bOwnPhysics = true;

		// 3.6 m wheelbase, 1.6 m track, 18 inch wheels; the wheel heights stay where the car's physics body expects them
		CarFrame& Frame = D.Frame;
		const float Mid = (Template.FrontX() + Template.RearX()) * 0.5f;
		Frame.Wheels[0] = Vec3(Mid + 180.0f, -80.0f, Template.Wheels[0].Z + 2.0f);
		Frame.Wheels[1] = Vec3(Mid + 180.0f, 80.0f, Template.Wheels[1].Z + 2.0f);
		Frame.Wheels[2] = Vec3(Mid - 180.0f, -78.0f, Template.Wheels[2].Z);
		Frame.Wheels[3] = Vec3(Mid - 180.0f, 78.0f, Template.Wheels[3].Z);
		for (float& Radius : Frame.Radius)
		{
			Radius = 36.0f;
		}
		const float G = Frame.Ground(), F = Frame.FrontX(), R = Frame.RearX();
		const Color Main = Paint(Color4(0.015f, 0.03f, 0.16f));
		const Color Accent = Paint(Color4(0.9f, 0.32f, 0.01f));

		// nose, survival cell, airbox and engine cover down to the gearbox
		std::vector<LoftSection> Spine = {
			{ F + 125.0f, 6.0f, G + 15.0f, G + 22.0f, 0.8f, 2.4f },
			{ F + 100.0f, 9.0f, G + 15.0f, G + 27.0f, 0.8f, 2.6f },
			{ F + 50.0f, 13.0f, G + 17.0f, G + 36.0f, 0.8f, 2.8f },
			{ F, 17.0f, G + 18.0f, G + 46.0f, 0.8f, 3.0f },
			{ F - 60.0f, 22.0f, G + 16.0f, G + 55.0f, 0.8f, 3.2f },
			{ F - 120.0f, 28.0f, G + 10.0f, G + 62.0f, 0.78f, 3.4f },
			{ F - 180.0f, 34.0f, G + 8.0f, G + 64.0f, 0.75f, 3.4f },
			{ F - 220.0f, 36.0f, G + 8.0f, G + 66.0f, 0.7f, 3.4f },
			{ F - 260.0f, 30.0f, G + 8.0f, G + 82.0f, 0.5f, 3.0f },
			{ F - 300.0f, 24.0f, G + 8.0f, G + 72.0f, 0.55f, 3.0f },
			{ R + 10.0f, 15.0f, G + 9.0f, G + 52.0f, 0.6f, 3.0f },
			{ R - 30.0f, 9.0f, G + 12.0f, G + 38.0f, 0.7f, 3.0f },
			{ R - 55.0f, 6.0f, G + 16.0f, G + 30.0f, 0.7f, 2.6f } };
		Loft(D.Body, Spine, 6, 36, [&](const Vec3& P)
		{
			return P.X > F + 95.0f || (P.Z > G + 70.0f && P.X < F - 240.0f) ? Accent : Main;
		});

		// sidepods either side of the cockpit
		for (const float Side : { -1.0f, 1.0f })
		{
			std::vector<LoftSection> Pod = {
				{ F - 130.0f, 2.0f, G + 20.0f, G + 50.0f, 0.8f, 3.0f, Side * 40.0f },
				{ F - 150.0f, 18.0f, G + 12.0f, G + 56.0f, 0.75f, 3.2f, Side * 46.0f },
				{ F - 220.0f, 20.0f, G + 10.0f, G + 52.0f, 0.7f, 3.2f, Side * 44.0f },
				{ F - 290.0f, 14.0f, G + 10.0f, G + 38.0f, 0.7f, 3.0f, Side * 32.0f },
				{ R + 20.0f, 6.0f, G + 12.0f, G + 26.0f, 0.7f, 2.8f, Side * 22.0f } };
			Loft(D.Body, Pod, 6, 28, [&](const Vec3& P) { return P.X > F - 165.0f ? Carbon() : Main; });
		}

		// the floor, and its edges
		Box(D.Body, Vec3((F - 120.0f + R + 10.0f) * 0.5f, 0.0f, G + 5.0f), Vec3((F - 120.0f - R - 10.0f) * 0.5f, 80.0f, 1.5f), Carbon());

		// front wing: a main plane right across, two flaps on the outer parts, endplates
		Wing(D.Body, F + 122.0f, G + 9.0f, 32.0f, 0.10f, 3.0f, -98.0f, 98.0f, Main);
		for (const float Side : { -1.0f, 1.0f })
		{
			const float Y0 = Side * 30.0f, Y1 = Side * 97.0f;
			Wing(D.Body, F + 98.0f, G + 15.0f, 20.0f, 0.08f, 16.0f, std::min(Y0, Y1), std::max(Y0, Y1), Accent);
			Wing(D.Body, F + 82.0f, G + 22.0f, 14.0f, 0.08f, 28.0f, std::min(Y0, Y1), std::max(Y0, Y1), Main);
			Box(D.Body, Vec3(F + 104.0f, Side * 99.0f, G + 17.0f), Vec3(24.0f, 1.0f, 10.0f), Main);
		}

		// rear wing: main plane, flap, endplates and the lower beam wing, on a central pillar
		Wing(D.Body, R - 30.0f, G + 86.0f, 30.0f, 0.10f, 6.0f, -50.0f, 50.0f, Main);
		Wing(D.Body, R - 56.0f, G + 95.0f, 18.0f, 0.09f, 32.0f, -50.0f, 50.0f, Accent);
		Wing(D.Body, R - 38.0f, G + 40.0f, 16.0f, 0.10f, 6.0f, -36.0f, 36.0f, Carbon());
		for (const float Side : { -1.0f, 1.0f })
		{
			Box(D.Body, Vec3(R - 46.0f, Side * 51.0f, G + 84.0f), Vec3(22.0f, 1.0f, 17.0f), Main);
		}
		Box(D.Body, Vec3(R - 42.0f, 0.0f, G + 62.0f), Vec3(5.0f, 1.5f, 26.0f), Carbon());
		Box(D.Body, Vec3(R - 58.0f, 0.0f, G + 40.0f), Vec3(1.0f, 4.0f, 3.0f), Light(Color4(1.0f, 0.03f, 0.02f)));

		// the halo over the cockpit, and the driver's helmet
		const float Head = F - 205.0f;
		Tube(D.Body, { Vec3(F - 150.0f, 0.0f, G + 60.0f), Vec3(F - 160.0f, 0.0f, G + 78.0f), Vec3(F - 165.0f, 0.0f, G + 84.0f) }, 2.5f, Carbon());
		Tube(D.Body, { Vec3(Head - 32.0f, -27.0f, G + 66.0f), Vec3(Head - 22.0f, -28.0f, G + 80.0f), Vec3(Head, -27.0f, G + 86.0f),
			Vec3(Head + 25.0f, -20.0f, G + 86.0f), Vec3(F - 165.0f, 0.0f, G + 84.0f), Vec3(Head + 25.0f, 20.0f, G + 86.0f),
			Vec3(Head, 27.0f, G + 86.0f), Vec3(Head - 22.0f, 28.0f, G + 80.0f), Vec3(Head - 32.0f, 27.0f, G + 66.0f) }, 2.5f, Carbon());
		Sphere(D.Body, Vec3(Head, 0.0f, G + 76.0f), Vec3(14.0f, 12.0f, 13.0f), Paint(Color4(0.85f, 0.75f, 0.02f)));
		Box(D.Body, Vec3(Head + 6.0f, 0.0f, G + 79.0f), Vec3(8.0f, 9.0f, 3.0f), Glass());

		// suspension arms from the chassis to each wheel, and mirrors
		for (int32_t i = 0; i < NumWheels; ++i)
		{
			const Vec3& W = Frame.Wheels[i];
			const float Side = W.Y > 0.0 ? 1.0f : -1.0f;
			const float Root = IsFront(i) ? 15.0f : 10.0f;
			const Vec3 Hub(W.X, W.Y - Side * 14.0f, W.Z);
			for (const float Z : { 10.0f, -10.0f })
			{
				Tube(D.Body, { Vec3(W.X + 22.0f, Side * Root, W.Z + Z), Hub + Vec3(0.0f, 0.0f, Z * 0.9f), Vec3(W.X - 28.0f, Side * Root, W.Z + Z) }, 1.6f, Carbon(), 8);
			}
		}
		for (const float Side : { -1.0f, 1.0f })
		{
			Box(D.Body, Vec3(F - 150.0f, Side * 48.0f, G + 64.0f), Vec3(4.0f, 7.0f, 3.5f), Main);
		}

		// wheels: black rims with ten spokes, a coloured stripe on the sidewall
		Wheel(D.Wheel[0], 36.0f, 30.0f, 23.0f, 10, Metal(0.12f));
		Wheel(D.Wheel[1], 36.0f, 40.0f, 23.0f, 10, Metal(0.12f));

		D.CockpitCamera = Vec3(Head + 2.0f, 0.0f, G + 92.0f);
		D.BonnetCamera = Vec3(Head - 48.0f, 0.0f, G + 95.0f);	// on the airbox, as on television

		// a small wheel just under the cockpit's rim, nearly upright
		D.SteeringWheelAt = Vec3(F - 168.0f, 0.0f, G + 68.0f);
		D.SteeringColumnPitch = 10.0f;
		SteeringWheel(D.SteeringWheel, 13.0f, Accent);

		// physics: about 800 kg with the driver, near 1000 hp from a 1.6 V6 turbo hybrid, eight gears, slicks and big downforce
		CarSetup& S = D.Setup;
		// (from its parts: CarParts)
		D.Parts = FormulaParts();
		const geobody::MassProperties Parts = geobody::Measure(D.Parts);
		S.Mass = float(Parts.Mass);
		S.FrontWeight = float((Parts.CentreOfMass.X + FormulaWheelbase * 0.5) / FormulaWheelbase);
		S.CoGHeight = float(Parts.CentreOfMass.Z);
		S.Inertia = Vec3(Parts.Inertia.M[0][0], Parts.Inertia.M[1][1], Parts.Inertia.M[2][2]);
		S.MechanicalTrail = 0.03f;
		S.PowerSteeringAssist = 0.5f;
		S.HighSpeedAssist = 0.25f;
		for (AxleSetup* Axle : { &S.Front, &S.Rear })
		{
			Axle->WheelRadius = 0.36f;
			Axle->SpringRate = 150000.0f;
			Axle->BumpDamping = 8000.0f;
			Axle->ReboundDamping = 10000.0f;
			Axle->AntiRollBar = 60000.0f;
			Axle->BumpTravel = 0.045f;
			Axle->DroopTravel = 0.06f;
		}
		S.Front.WheelInertia = 1.0f;
		S.Front.MaxSteerAngle = 22.0f;
		S.Front.MaxBrakeTorque = 4200.0f;
		S.Front.bDriven = false;
		S.Rear.WheelInertia = 1.3f;
		S.Rear.MaxSteerAngle = 0.0f;
		S.Rear.MaxBrakeTorque = 3000.0f;
		S.Rear.MaxHandbrakeTorque = 2000.0f;
		S.Rear.bDriven = true;

		S.Tire.NominalLoad = 3000.0f;
		S.Tire.PeakForceY = 5700.0f;
		S.Tire.PeakForceY2 = 10400.0f;
		S.Tire.StiffnessY = 110000.0f;
		S.Tire.StiffnessY2 = 200000.0f;
		S.Tire.PeakSlipY = 0.11f;
		S.Tire.StiffnessX = 150000.0f;
		S.Tire.StiffnessX2 = 260000.0f;

		S.Engine.TorqueCurve = { { 4000.0, 300.0 }, { 7000.0, 450.0 }, { 9000.0, 560.0 }, { 10500.0, 610.0 },
			{ 12000.0, 600.0 }, { 13500.0, 540.0 }, { 15000.0, 470.0 } };
		S.Engine.IdleRPM = 4000.0f;
		S.Engine.LimiterRPM = 15000.0f;
		S.Engine.Inertia = 0.03f;	// (a grand prix engine's crank and tiny flywheel)
		S.Engine.LaunchRPM = 10000.0f;	// (a race start's)
		S.Engine.EngineBrakeTorque = 90.0f;
		S.Engine.ClutchTorque = 1100.0f;
		S.Gearbox.GearRatios = { 3.10f, 2.45f, 2.02f, 1.72f, 1.48f, 1.29f, 1.14f, 1.0f };
		S.Gearbox.FinalDrive = 6.0f;
		S.Gearbox.UpshiftTime = 0.03f;
		S.Gearbox.DownshiftTime = 0.05f;
		S.Gearbox.AutoUpshiftRPM = 14200.0f;
		S.Gearbox.AutoDownshiftRPM = 9500.0f;
		S.Aero.DownforceArea = 4.6f;
		S.Aero.DragArea = 1.1f;
		S.Aero.FrontBalance = 0.44f;

		// a 1.6 litre V6 turbo: even firing, one collector into the turbo, and the electric motor's whine on top
		D.Sound.FiringAngles = { 0, 120, 240, 360, 480, 600 };
		D.Sound.Banks = { 0, 1, 0, 1, 0, 1 };
		D.Sound.PipeLength[0] = 0.55f;
		D.Sound.PipeLength[1] = 0.58f;
		D.Sound.Openness = 0.7f;
		D.Sound.Turbo = 0.7f;
		D.Sound.Hybrid = 0.5f;
		D.Sound.Crackle = 0.2f;
		D.Sound.RedlineRPM = 15000.0f;
		return D;
	}
}

namespace CarDesignsImpl
{
	// --- Hatchback 1.6: a small, tall five-door hatch, modelled on a Suzuki Swift Sport (2.43 m wheelbase, 3.89 m long,
	// 1.70 m wide, 1.51 m tall; 1.6 litre four, 100 kW and 160 Nm, six speeds, front-wheel drive, 1045 kg) ---
	CarDesign BuildHatch(const CarFrame& Template)
	{
		CarDesign D;
		D.Name = "Hatchback 1.6";
		D.bOwnPhysics = true;

		// its own wheels, standing on the same ground as the template car's (195/45 R17: 31 cm radius)
		CarFrame& Frame = D.Frame;
		const float Mid = (Template.FrontX() + Template.RearX()) * 0.5f + 10.0f;
		const float Ground = Template.Ground();
		for (float& Radius : Frame.Radius)
		{
			Radius = 31.0f;
		}
		Frame.Wheels[0] = Vec3(Mid + 121.5f, -74.5f, Ground + 31.0f);
		Frame.Wheels[1] = Vec3(Mid + 121.5f, 74.5f, Ground + 31.0f);
		Frame.Wheels[2] = Vec3(Mid - 121.5f, -75.0f, Ground + 31.0f);
		Frame.Wheels[3] = Vec3(Mid - 121.5f, 75.0f, Ground + 31.0f);
		const float G = Frame.Ground(), F = Frame.FrontX(), R = Frame.RearX();
		const Color Body = Paint(Color4(0.85f, 0.62f, 0.02f));	// champion yellow

		// the lower body: a short nose, high waist, a short tail that drops off nearly straight
		std::vector<LoftSection> Lower = {
			{ F + 80.0f, 62.0f, G + 28.0f, G + 60.0f, 0.80f, 2.6f },
			{ F + 72.0f, 76.0f, G + 22.0f, G + 70.0f, 0.82f, 3.0f },
			{ F + 50.0f, 83.0f, G + 20.0f, G + 80.0f, 0.86f, 3.5f },
			{ F + 15.0f, 85.0f, G + 20.0f, G + 86.0f, 0.88f, 3.8f },
			{ F - 30.0f, 85.0f, G + 20.0f, G + 90.0f, 0.88f, 4.0f },
			{ F - 70.0f, 85.0f, G + 20.0f, G + 95.0f, 0.88f, 4.0f },
			{ (F + R) * 0.5f, 84.0f, G + 20.0f, G + 97.0f, 0.88f, 4.0f },
			{ R + 30.0f, 85.0f, G + 21.0f, G + 98.0f, 0.88f, 4.0f },
			{ R, 86.0f, G + 22.0f, G + 98.0f, 0.88f, 4.0f },
			{ R - 40.0f, 84.0f, G + 24.0f, G + 97.0f, 0.88f, 3.8f },
			{ R - 62.0f, 80.0f, G + 30.0f, G + 94.0f, 0.86f, 3.4f },
			{ R - 66.0f, 76.0f, G + 36.0f, G + 90.0f, 0.84f, 3.0f } };
		const float Inner = static_cast<float>(std::fabs(Frame.Wheels[0].Y)) - 14.0f;
		Loft(D.Body, Lower, 12, 64, [&](const Vec3& P) { return P.Z < G + 30.0f ? Carbon() : Body; },
			[&](const Vec3& P) { return OutsideArches(P, Frame, 3.0f, Inner); });
		for (int32_t i = 0; i < NumWheels; ++i)
		{
			const Vec3& W = Frame.Wheels[i];
			const float Side = W.Y > 0.0 ? 1.0f : -1.0f;
			ArchLiner(D.Body, Vec3(W.X, 0.0, W.Z), Frame.Radius[i] + 3.0f, Side * Inner, Side * 87.0f, -10.0f, 190.0f, Carbon());
			std::vector<Vec3> Edge;
			for (int32_t Step = 0; Step <= 24; ++Step)
			{
				const float A = Rad(Lerp(-12.0f, 192.0f, Step / 24.0f));
				const float Arch = Frame.Radius[i] + 4.0f;
				Edge.push_back(Vec3(W.X + Arch * std::cos(A), Side * 86.0f, W.Z + Arch * std::sin(A)));
			}
			Tube(D.Body, Edge, 2.5f, Carbon(), 8);
		}

		// a tall glasshouse: steep windscreen, a long roof, a near-upright hatch; the roof and pillars in body colour
		std::vector<LoftSection> Cabin = {
			{ F - 45.0f, 70.0f, G + 91.0f, G + 93.0f, 0.9f, 3.0f },
			{ F - 80.0f, 72.0f, G + 93.0f, G + 128.0f, 0.84f, 3.2f },
			{ F - 120.0f, 70.0f, G + 95.0f, G + 146.0f, 0.8f, 3.4f },
			{ R + 20.0f, 68.0f, G + 97.0f, G + 148.0f, 0.78f, 3.4f },
			{ R - 30.0f, 66.0f, G + 97.0f, G + 143.0f, 0.78f, 3.2f },
			{ R - 58.0f, 64.0f, G + 96.0f, G + 120.0f, 0.8f, 3.0f },
			{ R - 64.0f, 62.0f, G + 95.0f, G + 100.0f, 0.82f, 3.0f } };
		const std::vector<Vec2d> RoofLine = { { F - 80.0f, G + 128.0f }, { F - 120.0f, G + 146.0f }, { R + 20.0f, G + 148.0f },
			{ R - 30.0f, G + 143.0f }, { R - 58.0f, G + 120.0f } };
		// (the B pillar between the doors, and the Swift's broad C pillar, its rear door handle hidden in it)
		const size_t HatchCabin = D.Body.Triangles.size();
		Loft(D.Body, Cabin, 36, 96, Glasshouse(Cabin, F - 120.0f, R + 18.0f, G + 97.0f, { F - 118.0f, R - 12.0f, R - 22.0f, R - 32.0f, R - 42.0f }, Body));
		Lining(D.Body, HatchCabin, Cloth(Color4(0.10f, 0.10f, 0.10f)), 1.5f);

		// swept-back headlights, a wide grille, upright tail lights, mirrors on the doors, a small roof spoiler
		for (const float Side : { -1.0f, 1.0f })
		{
			Box(D.Body, Vec3(F + 66.0f, Side * 58.0f, G + 68.0f), Vec3(9.0f, 15.0f, 6.0f), Light(Color4(1.0f, 0.95f, 0.85f)), Rot(0.0f, Side * -22.0f, 0.0f));
			Box(D.Body, Vec3(R - 62.0f, Side * 70.0f, G + 86.0f), Vec3(4.0f, 9.0f, 10.0f), Light(Color4(0.9f, 0.02f, 0.01f)), Rot(0.0f, Side * 15.0f, 0.0f));
			Box(D.Body, Vec3(F - 50.0f, Side * 88.0f, G + 96.0f), Vec3(6.0f, 7.0f, 4.5f), Body);
			Box(D.Body, Vec3(F - 48.0f, Side * 81.0f, G + 93.0f), Vec3(3.0f, 4.0f, 1.5f), Carbon());
		}
		Box(D.Body, Vec3(F + 79.0f, 0.0f, G + 46.0f), Vec3(4.0f, 34.0f, 8.0f), Carbon());
		Box(D.Body, Vec3(F + 80.0f, 0.0f, G + 31.0f), Vec3(3.0f, 40.0f, 4.0f), Carbon());
		Wing(D.Body, R - 44.0f, G + 146.0f, 18.0f, 0.06f, -4.0f, -60.0f, 60.0f, Body);
		Tube(D.Body, { Vec3(R - 60.0f, 32.0f, G + 26.0f), Vec3(R - 70.0f, 32.0f, G + 26.0f) }, 4.0f, Metal(0.6f), 12);

		// wheels: silver with seven spokes
		Wheel(D.Wheel[0], 31.0f, 20.0f, 31.0f * 0.68f, 7, Metal(0.75f));
		Wheel(D.Wheel[1], 31.0f, 20.0f, 31.0f * 0.68f, 7, Metal(0.75f));

		D.CockpitCamera = Vec3(F - 135.0f, 36.0f, G + 122.0f);	// the driver's eyes (right-hand drive)
		D.CockpitCeiling = G + 135.0f;
		D.BonnetCamera = Vec3(F + 40.0f, 0.0f, G + 98.0f);	// (near the bonnet's front, ~16 cm over it)

		// inside: a 37 cm wheel, a tall dashboard, dark cloth seats
		D.SteeringWheelAt = Vec3(F - 95.0f, 36.0f, G + 98.0f);
		D.SteeringColumnPitch = 24.0f;
		SteeringWheel(D.SteeringWheel, 18.0f, Body);
		Cockpit(D, F - 50.0f, F - 85.0f, 66.0f, G + 96.0f, G + 22.0f, F - 128.0f, Paint(Color4(0.03f, 0.03f, 0.035f)), &RoofLine);

		// physics: light, softly sprung, road tyres, front-wheel drive
		CarSetup& S = D.Setup;
		// (from its parts, with the driver and a full tank: CarParts)
		D.Parts = HatchParts();
		const geobody::MassProperties Parts = geobody::Measure(D.Parts);
		S.Mass = float(Parts.Mass);
		S.FrontWeight = float((Parts.CentreOfMass.X + HatchWheelbase * 0.5) / HatchWheelbase);
		S.CoGHeight = float(Parts.CentreOfMass.Z);
		S.Inertia = Vec3(Parts.Inertia.M[0][0], Parts.Inertia.M[1][1], Parts.Inertia.M[2][2]);
		S.MechanicalTrail = 0.022f;
		S.PowerSteeringAssist = 0.65f;
		S.HighSpeedAssist = 0.35f;
		for (AxleSetup* Axle : { &S.Front, &S.Rear })
		{
			Axle->WheelRadius = 0.31f;
			Axle->WheelInertia = 0.9f;
			Axle->BumpTravel = 0.08f;
			Axle->DroopTravel = 0.10f;
		}
		S.Front.SpringRate = 34000.0f;
		S.Front.BumpDamping = 2600.0f;
		S.Front.ReboundDamping = 3400.0f;
		S.Front.AntiRollBar = 16000.0f;
		S.Front.MaxSteerAngle = 34.0f;
		S.Front.MaxBrakeTorque = 1500.0f;
		S.Front.bDriven = true;
		S.Rear.SpringRate = 28000.0f;
		S.Rear.BumpDamping = 2200.0f;
		S.Rear.ReboundDamping = 3000.0f;
		S.Rear.AntiRollBar = 9000.0f;
		S.Rear.MaxSteerAngle = 0.0f;
		S.Rear.MaxBrakeTorque = 750.0f;
		S.Rear.MaxHandbrakeTorque = 1500.0f;
		S.Rear.bDriven = false;

		// 195/45 R17 road tyres: about 1.0 g, softer and more gradual than slicks
		S.Tire.NominalLoad = 3000.0f;
		S.Tire.PeakForceY = 3200.0f;
		S.Tire.PeakForceY2 = 5800.0f;
		S.Tire.StiffnessY = 60000.0f;
		S.Tire.StiffnessY2 = 105000.0f;
		S.Tire.PeakSlipY = 0.13f;
		S.Tire.StiffnessX = 80000.0f;
		S.Tire.StiffnessX2 = 140000.0f;
		S.Tire.SlideRatio = 0.82f;

		S.Engine.TorqueCurve = { { 1000.0, 105.0 }, { 2000.0, 128.0 }, { 3000.0, 145.0 }, { 4400.0, 160.0 },
			{ 5500.0, 155.0 }, { 6900.0, 138.0 }, { 7300.0, 118.0 } };
		S.Engine.IdleRPM = 800.0f;
		S.Engine.LimiterRPM = 7200.0f;
		S.Engine.Inertia = 0.11f;
		S.Engine.EngineBrakeTorque = 40.0f;
		S.Engine.ClutchTorque = 480.0f;
		S.Gearbox.GearRatios = { 3.364f, 2.059f, 1.481f, 1.152f, 0.922f, 0.775f };
		S.Gearbox.ReverseRatio = 3.27f;
		S.Gearbox.FinalDrive = 4.1f;
		S.Gearbox.UpshiftTime = 0.25f;
		S.Gearbox.DownshiftTime = 0.3f;
		S.Gearbox.AutoUpshiftRPM = 6900.0f;	// (its power peak)
		S.Gearbox.AutoDownshiftRPM = 2800.0f;
		S.Aero.DownforceArea = 0.0f;
		S.Aero.DragArea = 0.78f;	// (Cd 0.32 on ~2.2 m2, and the cooling's and wheels' drag: its 195 km/h)
		S.Aero.FrontBalance = 0.5f;

		// an inline four: even firing every half turn, all four into one pipe, a quiet road muffler
		D.Sound.FiringAngles = { 0, 180, 360, 540 };
		D.Sound.Banks = { 0, 0, 0, 0 };
		D.Sound.PipeLength[0] = 1.6f;
		D.Sound.PipeLength[1] = 1.7f;
		D.Sound.Openness = 0.35f;
		D.Sound.Crackle = 0.1f;
		D.Sound.RedlineRPM = 7200.0f;
		return D;
	}

	// --- Bakkie 3.0 TDi: a double-cab pickup, modelled on a 2007 Isuzu KB300 TDi LX 4x2 (the high-riding one, before
	// the D-TEQ engine), to its published size: 3.05 m wheelbase, 4.90 m long, 1.80 m wide, 1.735 m tall, tracks 1.515 m
	// front and 1.52 m rear, 215 mm ground clearance (under the axle), a 12.4 m turning circle; the 3.0 litre 4JH1 turbo diesel,
	// 96 kW at 3800 rpm and 280 Nm at 2000, a 5-speed manual, an open rear diff with a locker, leaf springs at the back,
	// 245/70 R16 all-terrain tyres, about 1905 kg with the driver. Black, chrome grille, mirrors and side steps. ---
	CarDesign BuildBakkie(const CarFrame& Template)
	{
		CarDesign D;
		D.Name = "Bakkie 3.0 TDi";
		D.bOwnPhysics = true;

		// its own wheels on the template car's ground: 245/70 R16 (37.5 cm radius), 1.515 m track at the front, 1.52 m at the back
		CarFrame& Frame = D.Frame;
		const float Mid = (Template.FrontX() + Template.RearX()) * 0.5f + 10.0f;
		const float Ground = Template.Ground();
		for (float& Radius : Frame.Radius)
		{
			Radius = 37.5f;
		}
		Frame.Wheels[0] = Vec3(Mid + 152.5f, -75.75f, Ground + 37.5f);
		Frame.Wheels[1] = Vec3(Mid + 152.5f, 75.75f, Ground + 37.5f);
		Frame.Wheels[2] = Vec3(Mid - 152.5f, -76.0f, Ground + 37.5f);
		Frame.Wheels[3] = Vec3(Mid - 152.5f, 76.0f, Ground + 37.5f);
		const float G = Frame.Ground(), F = Frame.FrontX(), R = Frame.RearX();
		const Color Body = Paint(Color4(0.012f, 0.012f, 0.014f));	// black
		const Color Chrome = Metal(0.45f);

		// the front and the cab's lower half: a tall blunt nose, a high flat bonnet, flat sides; it ends behind the cab
		// where the load bed starts (just ahead of the rear wheels)
		const float CabEnd = R + 50.0f;
		// (a working truck's flat sides and upright nose, but its corners rolled: the nose's top and bottom corners
		// round, the corners by the headlights round in plan, the shoulder under the windows round)
		std::vector<LoftSection> Lower = {
			{ F + 84.5f, 72.0f, G + 56.0f, G + 97.0f, 0.92f, 4.0f },
			{ F + 82.0f, 82.0f, G + 48.0f, G + 105.0f, 0.93f, 5.5f },
			{ F + 77.0f, 87.0f, G + 43.0f, G + 110.0f, 0.93f, 6.5f },
			{ F + 60.0f, 88.0f, G + 42.0f, G + 112.0f, 0.93f, 7.0f },
			{ F + 10.0f, 88.0f, G + 42.0f, G + 113.0f, 0.93f, 7.0f },
			{ F - 55.0f, 88.0f, G + 42.0f, G + 115.0f, 0.93f, 7.0f },
			{ CabEnd + 10.0f, 88.0f, G + 42.0f, G + 116.0f, 0.93f, 7.0f },
			{ CabEnd, 86.0f, G + 44.0f, G + 115.0f, 0.93f, 6.0f } };
		const float Inner = static_cast<float>(std::fabs(Frame.Wheels[0].Y)) - 15.0f;
		Loft(D.Body, Lower, 12, 64, [&](const Vec3& P) { return P.Z < G + 48.0f ? Carbon() : Body; },
			[&](const Vec3& P) { return OutsideArches(P, Frame, 4.0f, Inner); });

		// the cab: a raked windscreen, a long flat roof over two rows of seats, an upright back window; roof and pillars
		// in body colour
		const std::vector<Vec2d> RoofLine = { { F - 55.0f, G + 116.0f }, { F - 82.0f, G + 143.0f }, { F - 110.0f, G + 171.0f }, { F - 124.0f, G + 173.5f },
			{ CabEnd + 8.0f, G + 173.5f }, { CabEnd + 2.0f, G + 168.0f } };
		std::vector<LoftSection> Cabin = {
			{ F - 55.0f, 84.0f, G + 114.0f, G + 116.0f, 0.92f, 7.0f },
			{ F - 82.0f, 84.0f, G + 115.0f, G + 143.0f, 0.89f, 7.0f },
			{ F - 110.0f, 84.0f, G + 115.0f, G + 171.0f, 0.88f, 7.0f },
			{ F - 124.0f, 84.0f, G + 115.0f, G + 173.5f, 0.88f, 7.0f },
			{ CabEnd + 8.0f, 84.0f, G + 115.0f, G + 173.5f, 0.88f, 7.0f },
			{ CabEnd + 2.0f, 83.0f, G + 115.0f, G + 170.0f, 0.89f, 7.0f } };
		const float BPillar = F - 205.0f;
		// (the B pillar between the doors; the cab's back corners)
		const size_t BakkieCabin = D.Body.Triangles.size();
		Loft(D.Body, Cabin, 36, 96, Glasshouse(Cabin, F - 124.0f, CabEnd + 7.0f, G + 117.0f, { BPillar, CabEnd + 10.0f, CabEnd + 20.0f }, Body, G + 162.0f));
		Lining(D.Body, BakkieCabin, Cloth(Color4(0.13f, 0.13f, 0.12f)), 1.5f);

		// the load bed: a floor over the rear wheels, sides that clear the arches, a tailgate and a step bumper
		const float BedEnd = R - 88.0f, BedTop = G + 116.0f, BedFloor = G + 80.0f;
		const float Arch = Frame.Radius[2] + 5.0f;
		Box(D.Body, Vec3((CabEnd + BedEnd) * 0.5f, 0.0f, BedFloor), Vec3((CabEnd - BedEnd) * 0.5f, 84.0f, 2.0f), Carbon());
		Box(D.Body, Vec3(CabEnd - 2.0f, 0.0f, (BedFloor + BedTop) * 0.5f), Vec3(2.0f, 86.0f, (BedTop - BedFloor) * 0.5f), Body);
		for (const float Side : { -1.0f, 1.0f })
		{
			const float Y = Side * 86.0f;
			// above the arch, then ahead of and behind it down to the sill
			Box(D.Body, Vec3((CabEnd + BedEnd) * 0.5f, Y, (G + 37.5f + Arch + BedTop) * 0.5f), Vec3((CabEnd - BedEnd) * 0.5f, 2.5f, (BedTop - (G + 37.5f + Arch)) * 0.5f), Body);
			// the panel down round the arch, in thin slices so the opening is round
			for (float DX = -Arch + 2.5f; DX < Arch; DX += 5.0f)
			{
				const float Edge = G + 37.5f + std::sqrt(std::max(Arch * Arch - DX * DX, 0.0f));
				const float Top = G + 37.5f + Arch;
				if (Top - Edge > 0.5f)
				{
					Box(D.Body, Vec3(R + DX, Y, (Edge + Top) * 0.5f), Vec3(2.6f, 2.5f, (Top - Edge) * 0.5f), Body);
				}
			}
			ArchLiner(D.Body, Vec3(Frame.Wheels[0].X, 0.0, G + 37.5f), Arch, Side * Inner, Side * 87.0f, -10.0f, 190.0f, Carbon());
			Box(D.Body, Vec3((CabEnd + R + Arch) * 0.5f, Y, (G + 44.0f + G + 37.5f + Arch) * 0.5f), Vec3((CabEnd - (R + Arch)) * 0.5f + 1.0f, 2.5f, (Arch - 6.5f) * 0.5f + 0.5f), Body);
			Box(D.Body, Vec3((R - Arch + BedEnd) * 0.5f, Y, (G + 50.0f + G + 37.5f + Arch) * 0.5f), Vec3(((R - Arch) - BedEnd) * 0.5f, 2.5f, (Arch - 12.5f) * 0.5f + 0.5f), Body);
			Box(D.Body, Vec3((CabEnd + BedEnd) * 0.5f, Side * 84.0f, BedTop + 1.0f), Vec3((CabEnd - BedEnd) * 0.5f, 5.0f, 1.5f), Carbon());	// the bed rail
			ArchLiner(D.Body, Vec3(R, 0.0, G + 37.5f), Arch, Side * Inner, Y, -5.0f, 185.0f, Carbon());
			// fender flares round all four arches
			for (const int32_t Wheel : { Side < 0.0f ? 0 : 1, Side < 0.0f ? 2 : 3 })
			{
				const Vec3& W = Frame.Wheels[Wheel];
				std::vector<Vec3> Edge;
				for (int32_t Step = 0; Step <= 24; ++Step)
				{
					const float A = Rad(Lerp(-8.0f, 188.0f, Step / 24.0f));
					Edge.push_back(Vec3(W.X + (Arch + 1.0f) * std::cos(A), Side * 86.5f, W.Z + (Arch + 1.0f) * std::sin(A)));
				}
				Tube(D.Body, Edge, 3.5f, Carbon(), 8);
			}
			// tall tail lights in the bed's back corners, chrome mirrors, chrome tube side steps, door handles
			Box(D.Body, Vec3(BedEnd - 1.6f, Side * 80.0f, G + 108.0f), Vec3(2.2f, 6.5f, 6.5f), Light(Color4(0.9f, 0.02f, 0.01f)));	// tail and brake
			Box(D.Body, Vec3(BedEnd - 1.6f, Side * 80.0f, G + 98.0f), Vec3(2.2f, 6.5f, 3.5f), Light(Color4(0.85f, 0.85f, 0.82f)));	// reversing
			Box(D.Body, Vec3(BedEnd - 1.6f, Side * 80.0f, G + 89.0f), Vec3(2.2f, 6.5f, 5.5f), Light(Color4(0.9f, 0.02f, 0.01f)));	// indicator, red
			Box(D.Body, Vec3(BedEnd + 0.8f, Side * 80.0f, G + 99.0f), Vec3(2.0f, 7.3f, 20.0f), Metal(0.5f));	// (their housing)
			// the mirrors: a black housing on a stalk from the door's corner, chrome-capped, its glass facing back
			Tube(D.Body, { Vec3(F - 60.0f, Side * 86.0f, G + 121.0f), Vec3(F - 62.0f, Side * 93.0f, G + 124.0f) }, 1.6f, Carbon(), 8);
			Box(D.Body, Vec3(F - 62.0f, Side * 100.0f, G + 127.0f), Vec3(5.0f, 10.0f, 9.0f), Carbon(), Rot(0.0f, Side * 6.0f, 0.0f));
			Box(D.Body, Vec3(F - 59.0f, Side * 100.5f, G + 127.0f), Vec3(2.5f, 10.4f, 9.4f), Chrome, Rot(0.0f, Side * 6.0f, 0.0f));
			Box(D.Body, Vec3(F - 67.2f, Side * 100.0f, G + 127.0f), Vec3(0.3f, 8.8f, 7.6f), Metal(0.85f), Rot(0.0f, Side * 6.0f, 0.0f));	// the glass
			// the doors' shut lines (the front door's leading edge, the B pillar's two, the rear door's trailing edge)
			// and a rubber strip along the bottom of the windows
			for (const float X : { F - 57.0f, BPillar + 2.0f, BPillar - 2.0f, CabEnd + 12.0f })
			{
				Box(D.Body, Vec3(X, Side * 88.6f, G + 82.0f), Vec3(0.35f, 0.4f, 33.0f), Carbon());
			}
			Box(D.Body, Vec3((F - 57.0f + CabEnd + 12.0f) * 0.5f, Side * 86.6f, G + 116.5f), Vec3((F - 57.0f - CabEnd - 12.0f) * 0.5f, 1.2f, 0.8f), Carbon());
			Tube(D.Body, { Vec3(F - 45.0f, Side * 84.0f, G + 40.0f), Vec3(F - 52.0f, Side * 87.0f, G + 36.0f), Vec3(CabEnd + 4.0f, Side * 87.0f, G + 36.0f),
				Vec3(CabEnd - 2.0f, Side * 84.0f, G + 40.0f) }, 3.5f, Chrome, 10);
			for (const float X : { F - 140.0f, BPillar - 60.0f })
			{
				Box(D.Body, Vec3(X, Side * 88.5f, G + 104.0f), Vec3(7.0f, 1.0f, 1.5f), Chrome);
			}
			// headlights (the 2007 KB's): tall, swept back and up into the wings; a round projector in a chrome
			// reflector, amber along the bottom, under a clear lens
			Box(D.Body, Vec3(F + 79.0f, Side * 59.0f, G + 97.0f), Vec3(3.0f, 15.0f, 10.5f), Metal(0.8f), Rot(0.0f, Side * -18.0f, 0.0f));	// the reflector
			Box(D.Body, Vec3(F + 68.0f, Side * 79.0f, G + 101.0f), Vec3(10.0f, 3.0f, 7.0f), Metal(0.8f), Rot(0.0f, Side * -12.0f, 0.0f));	// (round the corner)
			Tube(D.Body, { Vec3(F + 81.5f, Side * 56.0f, G + 100.0f), Vec3(F + 82.3f, Side * 56.0f, G + 100.0f) }, 4.2f, Light(Color4(1.0f, 0.97f, 0.9f)), 20);	// the projector
			Tube(D.Body, { Vec3(F + 81.2f, Side * 56.0f, G + 100.0f), Vec3(F + 81.6f, Side * 56.0f, G + 100.0f) }, 5.3f, Metal(0.9f), 20);
			Box(D.Body, Vec3(F + 80.5f, Side * 66.0f, G + 89.0f), Vec3(1.2f, 7.0f, 2.0f), Light(Color4(0.9f, 0.45f, 0.02f)), Rot(0.0f, Side * -18.0f, 0.0f));	// the indicator
			Box(D.Body, Vec3(F + 82.0f, Side * 59.0f, G + 97.0f), Vec3(0.4f, 15.5f, 11.0f), Glass(), Rot(0.0f, Side * -18.0f, 0.0f));	// the lens
			Box(D.Body, Vec3(F + 71.0f, Side * 81.5f, G + 101.0f), Vec3(10.5f, 0.4f, 7.5f), Glass(), Rot(0.0f, Side * -12.0f, 0.0f));
			// round fog lamps, low in the bumper's corners
			Tube(D.Body, { Vec3(F + 85.0f, Side * 63.0f, G + 60.0f), Vec3(F + 85.6f, Side * 63.0f, G + 60.0f) }, 5.6f, Carbon(), 20);
			Tube(D.Body, { Vec3(F + 85.4f, Side * 63.0f, G + 60.0f), Vec3(F + 86.0f, Side * 63.0f, G + 60.0f) }, 4.2f, Light(Color4(1.0f, 0.95f, 0.85f)), 20);
		}
		Box(D.Body, Vec3(BedEnd - 1.0f, 0.0f, (G + 52.0f + BedTop) * 0.5f), Vec3(2.0f, 72.5f, (BedTop - G - 52.0f) * 0.5f), Body);	// the tailgate, between the lights
		Box(D.Body, Vec3(BedEnd + 2.0f, 0.0f, BedTop - 12.0f), Vec3(1.5f, 12.0f, 3.0f), Chrome);
		Box(D.Body, Vec3(BedEnd - 6.0f, 0.0f, G + 46.0f), Vec3(6.0f, 84.0f, 7.0f), Chrome);	// the step bumper
		// underneath: the chassis as its parts are (CarParts: the frame, the axle, springs, tank, exhaust and the spare),
		// so what's seen under it is what it weighs. (Their frame: m, on the ground midway between the axles, y left)
		{
			const float Mid = (F + R) * 0.5f;
			auto ToCar = [&](const geobody::Vec3& V) { return Vec3(Mid + V.X * 100.0, -V.Y * 100.0, G + V.Z * 100.0); };
			for (const geobody::Part& P : BakkieParts())
			{
				const std::string& N = P.Name;
				const bool bSpare = P.At.X < -1.9 && (N == "tyre" || N == "steel rim");
				const bool bUnder = N == "frame rail" || N == "cross member" || N == "rear axle housing" || N.rfind("rear diff", 0) == 0
					|| N == "rear leaf spring" || N == "fuel tank" || N == "exhaust pipe" || N == "silencer" || N == "prop shaft" || N == "front anti-roll bar";
				if (!bUnder && !bSpare)
				{
					continue;
				}
				const Color C = N == "exhaust pipe" || N == "silencer" ? Metal(0.3f) : (N == "tyre" ? Rubber() : Carbon());
				const Vec3 At = ToCar(P.At);
				if (P.Form == geobody::Shape::Cylinder)
				{
					const geobody::Vec3 A = P.Turn.Rotate(geobody::Vec3(P.Size.X * 0.5, 0.0, 0.0));
					const Vec3 Half(A.X * 100.0, -A.Y * 100.0, A.Z * 100.0);
					Tube(D.Body, { At - Half, At + Half }, float(P.Size.Y * 50.0), C, 12);
				}
				else
				{
					Box(D.Body, At, Vec3(P.Size.X * 50.0, P.Size.Y * 50.0, P.Size.Z * 50.0), C);
				}
			}
		}
		Tube(D.Body, { Vec3(BedEnd + 5.0f, -50.0f, G + 38.0f), Vec3(BedEnd - 6.0f, -50.0f, G + 38.0f) }, 3.5f, Metal(0.5f), 12);

		// the chrome grille between the headlights, an Isuzu-style bar across it, a body-colour bumper with a black lip
		// (the 2007 KB's: a chrome surround, three thick bars across, the badge on the middle one)
		Box(D.Body, Vec3(F + 80.0f, 0.0f, G + 94.0f), Vec3(2.0f, 41.0f, 13.0f), Carbon());	// (black behind the bars)
		for (const float Z : { G + 106.0f, G + 82.0f })
		{
			Box(D.Body, Vec3(F + 81.5f, 0.0f, Z), Vec3(1.8f, 42.0f, 1.6f), Chrome);	// the surround, top and bottom
		}
		for (const float Side : { -1.0f, 1.0f })
		{
			Box(D.Body, Vec3(F + 81.5f, Side * 41.5f, G + 94.0f), Vec3(1.8f, 1.6f, 13.0f), Chrome);
		}
		for (const float Z : { G + 100.0f, G + 93.0f, G + 86.5f })
		{
			Box(D.Body, Vec3(F + 82.0f, 0.0f, Z), Vec3(1.5f, 40.0f, 2.2f), Chrome);	// the bars
		}
		Box(D.Body, Vec3(F + 83.7f, 0.0f, G + 93.0f), Vec3(0.4f, 9.0f, 1.7f), Carbon());	// the badge
		// the bonnet's scoop (the intercooler's air), a black slot at its front
		Box(D.Body, Vec3(F + 30.0f, 0.0f, G + 114.0f), Vec3(16.0f, 24.0f, 2.8f), Body, Rot(-3.0f, 0.0f, 0.0f));
		Box(D.Body, Vec3(F + 45.5f, 0.0f, G + 114.3f), Vec3(0.6f, 21.0f, 1.6f), Carbon());
		// the bumper's black intake under the plate
		Box(D.Body, Vec3(F + 85.2f, 0.0f, G + 53.5f), Vec3(0.6f, 32.0f, 4.0f), Carbon());
		// number plates (520 x 113 mm), white with a dark rim
		Box(D.Body, Vec3(F + 85.4f, 0.0f, G + 64.0f), Vec3(0.3f, 26.0f, 5.65f), Paint(Color4(0.85f, 0.85f, 0.82f)));
		Box(D.Body, Vec3(F + 85.2f, 0.0f, G + 64.0f), Vec3(0.3f, 26.8f, 6.4f), Carbon());
		Box(D.Body, Vec3(BedEnd - 12.4f, 0.0f, G + 46.0f), Vec3(0.3f, 26.0f, 5.65f), Paint(Color4(0.85f, 0.85f, 0.82f)));	// (in the step bumper)
		Box(D.Body, Vec3(BedEnd - 12.2f, 0.0f, G + 46.0f), Vec3(0.3f, 27.5f, 6.8f), Carbon());
		Box(D.Body, Vec3(F + 80.0f, 0.0f, G + 64.0f), Vec3(5.0f, 86.0f, 12.0f), Body);
		Box(D.Body, Vec3(F + 79.0f, 0.0f, G + 50.0f), Vec3(4.0f, 70.0f, 3.0f), Carbon());

		// wheels: silver six-spoke 16 inch alloys on chunky tyres
		Wheel(D.Wheel[0], 37.5f, 24.5f, 37.5f * 0.55f, 6, Metal(0.75f));
		Wheel(D.Wheel[1], 37.5f, 24.5f, 37.5f * 0.55f, 6, Metal(0.75f));

		D.CockpitCamera = Vec3(F - 145.0f, 38.0f, G + 150.0f);	// the driver's eyes, sitting high (right-hand drive)
		D.CockpitCeiling = G + 161.0f;
		D.BonnetCamera = Vec3(F + 45.0f, 0.0f, G + 129.0f);	// (near the bonnet's front, ~16 cm over it)

		// inside: a big 39 cm wheel on a steep column, a tall square dashboard, grey cloth
		D.SteeringWheelAt = Vec3(F - 108.0f, 38.0f, G + 122.0f);
		D.SteeringColumnPitch = 28.0f;
		// (the KB300's, from a photo of one: a grey four-spoke wheel round the airbag, three dials under one hood (the
		// rev counter, a bigger speedometer, fuel and temperature), two square vents over the radio, grey all through)
		const Color Grey = Cloth(Color4(0.16f, 0.16f, 0.17f));
		SteeringWheel(D.SteeringWheel, 19.5f, Grey, true, Grey);
		Cockpit(D, F - 58.0f, F - 95.0f, 74.0f, G + 118.0f, G + 62.0f, F - 152.0f, Paint(Color4(0.14f, 0.14f, 0.15f)), &RoofLine, 3, &Grey);

		// physics: heavy and tall, front-heavy when empty, softly sprung, an open diff with a locker, all-terrain tyres
		CarSetup& S = D.Setup;
		// its mass, the share on its front wheels, how high it sits and how hard it is to turn: from its parts (with the
		// driver and a full tank: ~1,880 kg against its 1,830 kg licensing mass and a 75 kg driver; 54% on the front)
		D.Parts = BakkieParts();
		const geobody::MassProperties Parts = geobody::Measure(D.Parts);
		S.Mass = float(Parts.Mass);
		S.FrontWeight = float((Parts.CentreOfMass.X + BakkieWheelbase * 0.5) / BakkieWheelbase);
		S.CoGHeight = float(Parts.CentreOfMass.Z);
		S.Inertia = Vec3(Parts.Inertia.M[0][0], Parts.Inertia.M[1][1], Parts.Inertia.M[2][2]);
		S.MechanicalTrail = 0.030f;
		S.KingpinInclination = 12.0f;
		S.PowerSteeringAssist = 0.70f;	// hydraulic: light when parking, still light-ish at speed
		S.HighSpeedAssist = 0.45f;
		for (AxleSetup* Axle : { &S.Front, &S.Rear })
		{
			Axle->WheelRadius = 0.375f;
			Axle->WheelInertia = 2.2f;
		}
		// front: double wishbones on torsion bars
		S.Front.SpringRate = 42000.0f;
		S.Front.BumpDamping = 2600.0f;
		S.Front.ReboundDamping = 4000.0f;
		S.Front.AntiRollBar = 18000.0f;
		S.Front.BumpTravel = 0.10f;
		S.Front.DroopTravel = 0.11f;
		S.Front.MaxSteerAngle = 33.0f;	// a 12.4 m turning circle
		S.Front.MaxBrakeTorque = 1900.0f;	// ventilated discs
		S.Front.bDriven = false;
		// rear: a live axle on leaf springs, stiff enough for a tonne in the bed, so it skips about when empty
		S.Rear.SpringRate = 50000.0f;
		S.Rear.BumpDamping = 2400.0f;
		S.Rear.ReboundDamping = 3800.0f;
		S.Rear.AntiRollBar = 5000.0f;
		S.Rear.BumpTravel = 0.12f;
		S.Rear.DroopTravel = 0.12f;
		S.Rear.MaxSteerAngle = 0.0f;
		S.Rear.MaxBrakeTorque = 900.0f;	// drums
		S.Rear.MaxHandbrakeTorque = 1500.0f;
		S.Rear.bDriven = true;

		// 245/70 R16 all-terrains: about 0.85 g, soft and gradual, tall sidewalls slow to build side force
		S.Tire.NominalLoad = 4500.0f;
		S.Tire.PeakForceY = 3900.0f;
		S.Tire.PeakForceY2 = 7200.0f;
		S.Tire.StiffnessY = 62000.0f;
		S.Tire.StiffnessY2 = 110000.0f;
		S.Tire.PeakSlipY = 0.16f;
		S.Tire.StiffnessX = 85000.0f;
		S.Tire.StiffnessX2 = 150000.0f;
		S.Tire.PeakSlipX = 0.12f;
		S.Tire.SlideRatio = 0.80f;
		S.Tire.VerticalStiffness = 220000.0f;
		S.Tire.RelaxationLength = 0.45f;

		// the 4JH1-TC: pulls from just off idle, flat torque to 2800, all done by 4000
		S.Engine.TorqueCurve = { { 800.0, 130.0 }, { 1200.0, 175.0 }, { 1600.0, 245.0 }, { 2000.0, 280.0 },
			{ 2800.0, 275.0 }, { 3400.0, 258.0 }, { 3800.0, 241.0 }, { 4100.0, 190.0 } };
		S.Engine.IdleRPM = 750.0f;
		S.Engine.LimiterRPM = 4100.0f;
		S.Engine.Inertia = 0.20f;	// a diesel's heavier flywheel (~12 kg) and crank
		S.Engine.EngineBrakeTorque = 55.0f;
		S.Engine.ClutchTorque = 520.0f;
		S.Gearbox.GearRatios = { 4.117f, 2.272f, 1.425f, 1.000f, 0.807f };
		S.Gearbox.ReverseRatio = 4.392f;
		S.Gearbox.FinalDrive = 4.10f;
		S.Gearbox.Efficiency = 0.90f;
		S.Gearbox.UpshiftTime = 0.35f;	// (a brisk driver's)
		S.Gearbox.DownshiftTime = 0.5f;
		S.Gearbox.AutoUpshiftRPM = 3800.0f;	// (its power peak: 96 kW at 3800)
		S.Gearbox.AutoDownshiftRPM = 1400.0f;
		S.Gearbox.DiffPreload = 0.0f;	// an open diff...
		S.Gearbox.DiffPowerRatio = 0.0f;
		S.Gearbox.DiffCoastRatio = 0.0f;
		S.Gearbox.bHasDiffLock = true;	// ...with a locker (L)
		S.Aero.DownforceArea = 0.0f;
		S.Aero.DragArea = 1.30f;	// a brick: Cd ~0.45 over 2.9 m2
		S.Aero.FrontBalance = 0.5f;

		// an inline four diesel: even firing, one pipe, a turbo, a deep quiet muffler, no crackle
		D.Sound.FiringAngles = { 0, 180, 360, 540 };
		D.Sound.Banks = { 0, 0, 0, 0 };
		D.Sound.PipeLength[0] = 2.4f;
		D.Sound.PipeLength[1] = 2.5f;
		D.Sound.Openness = 0.22f;
		D.Sound.Turbo = 0.45f;
		D.Sound.Crackle = 0.0f;
		D.Sound.RedlineRPM = 4000.0f;
		return D;
	}
	// --- RS 4.0: a rear-engined two-seat coupe, modelled on a Porsche 911 GT3 RS (the 992, 2023) to its published size and
	// the owner's photos: 2.457 m wheelbase, 4.572 m long, 1.900 m wide; 275/35 R20 tyres in front, 335/30 R21 behind; a
	// 4.0 litre flat six behind the rear axle, 386 kW at 8500 rpm and 465 Nm at 6300, a seven-speed twin-clutch gearbox,
	// rear-wheel drive; 1450 kg; 409 kg of downforce at 200 km/h (from the swan-neck wing over the roof) ---
	CarDesign BuildRS(const CarFrame& Template)
	{
		CarDesign D;
		D.Name = "RS 4.0";
		D.bOwnPhysics = true;

		// its own wheels on the template car's ground: 35.0 cm radius in front, 36.7 behind
		CarFrame& Frame = D.Frame;
		const float Mid = (Template.FrontX() + Template.RearX()) * 0.5f;
		const float Ground = Template.Ground();
		Frame.Radius[0] = Frame.Radius[1] = 35.0f;
		Frame.Radius[2] = Frame.Radius[3] = 36.7f;
		Frame.Wheels[0] = Vec3(Mid + 122.85f, -79.35f, Ground + 35.0f);
		Frame.Wheels[1] = Vec3(Mid + 122.85f, 79.35f, Ground + 35.0f);
		Frame.Wheels[2] = Vec3(Mid - 122.85f, -77.85f, Ground + 36.7f);
		Frame.Wheels[3] = Vec3(Mid - 122.85f, 77.85f, Ground + 36.7f);
		const float G = Frame.Ground(), F = Frame.FrontX(), R = Frame.RearX();
		const Color Body = Paint(Color4(0.55f, 0.012f, 0.01f));	// guards red
		const Color Chrome = Metal(0.6f);

		// the lower body: a low nose (0.97 m ahead of the front axle), the waist rising to wide rear haunches, a short
		// tail (1.12 m behind the rear axle) under the engine
		std::vector<LoftSection> Lower = {
			{ F + 97.0f, 70.0f, G + 16.0f, G + 46.0f, 0.80f, 2.6f },
			{ F + 92.0f, 84.0f, G + 11.0f, G + 54.0f, 0.84f, 3.0f },
			{ F + 72.0f, 90.0f, G + 10.0f, G + 61.0f, 0.86f, 3.4f },
			{ F + 35.0f, 91.0f, G + 10.0f, G + 65.0f, 0.86f, 3.6f },
			{ F - 20.0f, 90.0f, G + 10.0f, G + 70.0f, 0.86f, 3.8f },
			{ F - 60.0f, 89.0f, G + 10.0f, G + 76.0f, 0.84f, 3.8f },
			{ (F + R) * 0.5f, 89.0f, G + 10.0f, G + 80.0f, 0.84f, 3.8f },
			{ R + 55.0f, 92.0f, G + 10.0f, G + 84.0f, 0.86f, 3.8f },
			{ R + 15.0f, 94.0f, G + 11.0f, G + 87.0f, 0.88f, 3.8f },
			{ R - 35.0f, 93.0f, G + 13.0f, G + 87.0f, 0.88f, 3.6f },
			{ R - 80.0f, 88.0f, G + 18.0f, G + 84.0f, 0.86f, 3.2f },
			{ R - 104.0f, 80.0f, G + 24.0f, G + 78.0f, 0.82f, 2.8f },
			{ R - 112.0f, 70.0f, G + 30.0f, G + 72.0f, 0.80f, 2.4f } };
		const float Inner = static_cast<float>(std::fabs(Frame.Wheels[0].Y)) - 15.0f;
		Loft(D.Body, Lower, 12, 64, [&](const Vec3& P) { return P.Z < G + 16.0f ? Carbon() : Body; },
			[&](const Vec3& P) { return OutsideArches(P, Frame, 3.0f, Inner); });
		// the front wings standing up either side of the low bonnet, the headlights at their noses
		for (const float Side : { -1.0f, 1.0f })
		{
			std::vector<LoftSection> Wing = {
				{ F + 82.0f, 13.0f, G + 50.0f, G + 62.0f, 0.70f, 2.4f, Side * 66.0f },
				{ F + 70.0f, 19.0f, G + 48.0f, G + 69.0f, 0.75f, 2.6f, Side * 70.0f },
				{ F + 20.0f, 20.0f, G + 50.0f, G + 71.0f, 0.75f, 2.8f, Side * 71.0f },
				{ F - 30.0f, 19.0f, G + 56.0f, G + 74.0f, 0.75f, 2.8f, Side * 71.0f },
				{ F - 58.0f, 15.0f, G + 64.0f, G + 77.0f, 0.75f, 2.6f, Side * 72.0f } };
			Loft(D.Body, Wing, 10, 32, [&](const Vec3&) { return Body; }, [&](const Vec3& P) { return OutsideArches(P, Frame, 3.0f, Inner); });
			// the headlight: a tall oval lamp under a clear cover, the indicator below it
			Sphere(D.Body, Vec3(F + 78.0f, Side * 66.0f, G + 61.0f), Vec3(5.0f, 9.0f, 7.5f), Light(Color4(1.0f, 0.97f, 0.92f)), 16);
			Sphere(D.Body, Vec3(F + 79.0f, Side * 66.0f, G + 61.0f), Vec3(6.5f, 10.5f, 9.0f), Glass(), 16);
			Box(D.Body, Vec3(F + 92.0f, Side * 66.0f, G + 46.0f), Vec3(1.2f, 9.0f, 1.6f), Light(Color4(0.95f, 0.95f, 0.92f)));	// daytime light
			// the louvres on the wing's top (the RS's), the intake in the rear wing ahead of the wheel
			for (int32_t k = 0; k < 6; ++k)
			{
				Box(D.Body, Vec3(F + 8.0f - k * 4.0f, Side * 71.0f, G + 74.3f), Vec3(1.0f, 12.0f, 0.4f), Carbon());
			}
			Box(D.Body, Vec3(R + 64.0f, Side * 90.5f, G + 58.0f), Vec3(6.0f, 2.0f, 13.0f), Carbon(), Rot(0.0f, Side * 8.0f, 0.0f));	// the intake ahead of the rear wheel
			Box(D.Body, Vec3(F - 42.0f, Side * 90.6f, G + 40.0f), Vec3(5.0f, 1.0f, 22.0f), Carbon());	// the outlet behind the front wheel
			Box(D.Body, Vec3(F - 34.0f, Side * 89.5f, G + 60.0f), Vec3(12.0f, 2.0f, 3.0f), Carbon(), Rot(0.0f, 0.0f, Side * 20.0f));
			Box(D.Body, Vec3((F - 60.0f + R + 66.0f) * 0.5f, Side * 90.2f, G + 30.0f), Vec3((F - 60.0f - R - 66.0f) * 0.5f, 0.4f, 2.5f), Carbon());	// the stripe low on the doors
		}
		for (int32_t i = 0; i < NumWheels; ++i)
		{
			const Vec3& W = Frame.Wheels[i];
			const float Side = W.Y > 0.0 ? 1.0f : -1.0f;
			ArchLiner(D.Body, Vec3(W.X, 0.0, W.Z), Frame.Radius[i] + 3.0f, Side * Inner, Side * 94.0f, -10.0f, 190.0f, Carbon());
		}

		// the glasshouse: a raked windscreen, the roof's peak over the seats, then the fastback falling all the way to
		// the engine lid; a long door window, a small quarter window, a broad C pillar
		std::vector<LoftSection> Cabin = {
			{ F - 55.0f, 70.0f, G + 74.0f, G + 76.0f, 0.90f, 3.0f },
			{ F - 85.0f, 70.0f, G + 75.0f, G + 106.0f, 0.80f, 3.2f },
			{ F - 112.0f, 68.0f, G + 76.0f, G + 123.0f, 0.74f, 3.4f },
			{ F - 135.0f, 67.0f, G + 77.0f, G + 129.0f, 0.72f, 3.4f },
			{ R + 80.0f, 67.0f, G + 79.0f, G + 127.0f, 0.72f, 3.4f },
			{ R + 40.0f, 66.0f, G + 81.0f, G + 118.0f, 0.74f, 3.2f },
			{ R + 10.0f, 64.0f, G + 83.0f, G + 108.0f, 0.76f, 3.0f },
			{ R - 25.0f, 62.0f, G + 84.0f, G + 97.0f, 0.78f, 3.0f },
			{ R - 55.0f, 58.0f, G + 85.0f, G + 89.0f, 0.80f, 3.0f } };
		const std::vector<Vec2d> RoofLine = { { F - 55.0f, G + 76.0f }, { F - 85.0f, G + 106.0f }, { F - 112.0f, G + 123.0f }, { F - 135.0f, G + 129.0f },
			{ R + 80.0f, G + 127.0f }, { R + 40.0f, G + 118.0f }, { R + 10.0f, G + 108.0f }, { R - 25.0f, G + 97.0f }, { R - 55.0f, G + 89.0f } };
		const size_t RSCabin = D.Body.Triangles.size();
		Loft(D.Body, Cabin, 36, 96, Glasshouse(Cabin, F - 112.0f, R + 74.0f, G + 82.0f, { R + 70.0f, R + 16.0f, R + 6.0f, R - 4.0f, R - 14.0f, R - 24.0f }, Body));
		Lining(D.Body, RSCabin, Cloth(Color4(0.03f, 0.03f, 0.03f)), 1.5f);

		// the doors' shut lines, the handles, the mirrors on the doors
		for (const float Side : { -1.0f, 1.0f })
		{
			for (const float X : { F - 57.0f, R + 72.0f })
			{
				Box(D.Body, Vec3(X, Side * 89.6f, G + 50.0f), Vec3(0.35f, 0.4f, 30.0f), Carbon());
			}
			Box(D.Body, Vec3(R + 82.0f, Side * 89.8f, G + 74.0f), Vec3(6.0f, 0.6f, 1.2f), Chrome);
			Tube(D.Body, { Vec3(F - 62.0f, Side * 84.0f, G + 80.0f), Vec3(F - 64.0f, Side * 92.0f, G + 84.0f) }, 1.4f, Body, 8);
			Box(D.Body, Vec3(F - 64.0f, Side * 98.0f, G + 87.0f), Vec3(4.5f, 9.0f, 6.0f), Body, Rot(0.0f, Side * 6.0f, 0.0f));
			Box(D.Body, Vec3(F - 68.7f, Side * 98.0f, G + 87.0f), Vec3(0.3f, 8.0f, 5.0f), Metal(0.85f), Rot(0.0f, Side * 6.0f, 0.0f));
		}

		// the front: three big intakes and a carbon lip under the nose
		Box(D.Body, Vec3(F + 95.0f, 0.0f, G + 32.0f), Vec3(1.5f, 30.0f, 10.0f), Carbon());	// the radiator's inlet (the 992 RS's single big one)
		for (const float Side : { -1.0f, 1.0f })
		{
			Box(D.Body, Vec3(F + 88.0f, Side * 82.0f, G + 26.0f), Vec3(7.0f, 4.0f, 0.4f), Carbon(), Rot(0.0f, Side * -25.0f, -8.0f * Side));	// a canard
			Box(D.Body, Vec3(F + 50.0f, Side * 22.0f, G + 66.5f), Vec3(14.0f, 9.0f, 0.5f), Carbon(), Rot(-3.0f, 0.0f, 0.0f));	// a bonnet vent
		}
		for (const float Side : { -1.0f, 1.0f })
		{
			Box(D.Body, Vec3(F + 90.0f, Side * 60.0f, G + 30.0f), Vec3(1.5f, 10.0f, 6.0f), Carbon(), Rot(0.0f, Side * -20.0f, 0.0f));
		}
		Box(D.Body, Vec3(F + 87.0f, 0.0f, G + 11.5f), Vec3(5.0f, 64.0f, 1.0f), Carbon());	// (the lip, under the nose)

		// the back: the engine lid's vents, the wing on two uprights with end plates, thin tail lights, the
		// diffuser and two exhausts in the middle
		for (int32_t k = 0; k < 7; ++k)
		{
			Box(D.Body, Vec3(R - 62.0f - k * 4.5f, 0.0f, G + 87.5f - k * 0.6f), Vec3(1.2f, 30.0f, 0.5f), Carbon());
		}
		for (const float Side : { -1.0f, 1.0f })
		{
			// a swan neck: up from the engine lid, over, and down onto the wing's top
			Tube(D.Body, { Vec3(R - 62.0f, Side * 34.0f, G + 88.0f), Vec3(R - 70.0f, Side * 34.0f, G + 120.0f), Vec3(R - 80.0f, Side * 34.0f, G + 140.0f),
				Vec3(R - 92.0f, Side * 34.0f, G + 141.0f), Vec3(R - 96.0f, Side * 34.0f, G + 136.0f) }, 1.6f, Carbon(), 8);
			Box(D.Body, Vec3(R - 100.0f, Side * 80.0f, G + 132.0f), Vec3(20.0f, 0.6f, 9.0f), Carbon());	// the end plates
			Box(D.Body, Vec3(R - 108.5f, Side * 58.0f, G + 70.0f), Vec3(1.2f, 12.0f, 2.2f), Light(Color4(0.9f, 0.02f, 0.01f)));	// the tail lights' ends
			Tube(D.Body, { Vec3(R - 108.0f, Side * 9.0f, G + 30.0f), Vec3(R - 115.0f, Side * 9.0f, G + 30.0f) }, 4.5f, Metal(0.35f), 16);
		}
		Wing(D.Body, R - 84.0f, G + 130.0f, 36.0f, 0.12f, -12.0f, -80.0f, 80.0f, Carbon());	// the main plane, higher than the roof
		Wing(D.Body, R - 108.0f, G + 138.0f, 20.0f, 0.10f, -28.0f, -80.0f, 80.0f, Carbon());	// the flap over its trailing edge
		Box(D.Body, Vec3(R - 110.0f, 0.0f, G + 70.0f), Vec3(1.0f, 58.0f, 0.7f), Light(Color4(0.9f, 0.02f, 0.01f)));	// the light bar across the back
		Box(D.Body, Vec3(R - 106.0f, 0.0f, G + 24.0f), Vec3(4.0f, 56.0f, 4.0f), Carbon());	// the diffuser
		Box(D.Body, Vec3(R - 113.0f, 0.0f, G + 52.0f), Vec3(0.3f, 26.0f, 5.65f), Paint(Color4(0.85f, 0.85f, 0.82f)));	// the plate
		Box(D.Body, Vec3(F + 97.4f, 0.0f, G + 42.0f), Vec3(0.3f, 26.0f, 5.65f), Paint(Color4(0.85f, 0.85f, 0.82f)));

		// wheels: dark ten-spoke centre-lock rims
		Wheel(D.Wheel[0], 35.0f, 27.5f, 25.4f, 10, Metal(0.75f));	// (forged, silver)
		Wheel(D.Wheel[1], 36.7f, 33.5f, 26.7f, 10, Metal(0.75f));

		D.CockpitCamera = Vec3(F - 172.0f, 37.0f, G + 106.0f);	// the driver's eyes, low and back (right-hand drive)
		D.CockpitCeiling = G + 116.0f;
		D.BonnetCamera = Vec3(F + 50.0f, 0.0f, G + 80.0f);

		// inside: a 36 cm wheel, carbon bucket seats, black everywhere
		D.SteeringWheelAt = Vec3(F - 128.0f, 37.0f, G + 80.0f);
		D.SteeringColumnPitch = 22.0f;
		SteeringWheel(D.SteeringWheel, 18.0f, Paint(Color4(0.85f, 0.22f, 0.02f)));
		Cockpit(D, F - 62.0f, F - 98.0f, 66.0f, G + 76.0f, G + 14.0f, F - 152.0f, Paint(Color4(0.02f, 0.02f, 0.022f)), &RoofLine);

		// physics: 1450 kg and a driver, 40% on the front wheels (the engine behind the rear axle), low and stiff
		CarSetup& S = D.Setup;
		// (from its parts, with the driver: CarParts)
		D.Parts = RSParts();
		const geobody::MassProperties Parts = geobody::Measure(D.Parts);
		S.Mass = float(Parts.Mass);
		S.FrontWeight = float((Parts.CentreOfMass.X + RSWheelbase * 0.5) / RSWheelbase);
		S.CoGHeight = float(Parts.CentreOfMass.Z);
		S.Inertia = Vec3(Parts.Inertia.M[0][0], Parts.Inertia.M[1][1], Parts.Inertia.M[2][2]);
		S.MechanicalTrail = 0.025f;
		S.PowerSteeringAssist = 0.5f;
		S.HighSpeedAssist = 0.25f;
		S.Front.WheelRadius = 0.350f;
		S.Rear.WheelRadius = 0.367f;
		S.Front.WheelInertia = 1.3f;
		S.Rear.WheelInertia = 1.6f;
		for (AxleSetup* Axle : { &S.Front, &S.Rear })
		{
			Axle->BumpTravel = 0.05f;
			Axle->DroopTravel = 0.07f;
		}
		S.Front.SpringRate = 52000.0f;
		S.Front.BumpDamping = 4200.0f;
		S.Front.ReboundDamping = 5200.0f;
		S.Front.AntiRollBar = 30000.0f;
		S.Front.MaxSteerAngle = 30.0f;
		S.Front.MaxBrakeTorque = 3600.0f;	// (carbon ceramic discs: 410 mm in front, 390 behind)
		S.Front.bDriven = false;
		S.Rear.SpringRate = 90000.0f;
		S.Rear.BumpDamping = 6000.0f;
		S.Rear.ReboundDamping = 7600.0f;
		S.Rear.AntiRollBar = 26000.0f;
		S.Rear.MaxSteerAngle = 0.0f;
		S.Rear.MaxBrakeTorque = 2500.0f;
		S.Rear.MaxHandbrakeTorque = 1800.0f;
		S.Rear.bDriven = true;

		// road-legal semi-slicks: about 1.35 g, more gradual than racing slicks
		S.Tire.NominalLoad = 4000.0f;
		S.Tire.PeakForceY = 5400.0f;
		S.Tire.PeakForceY2 = 9800.0f;
		S.Tire.StiffnessY = 100000.0f;
		S.Tire.StiffnessY2 = 180000.0f;
		S.Tire.PeakSlipY = 0.11f;
		S.Tire.StiffnessX = 130000.0f;
		S.Tire.StiffnessX2 = 230000.0f;
		S.Tire.SlideRatio = 0.85f;

		// the flat six: 465 Nm at 6300, 386 kW (434 Nm) at 8500, the limiter at 9000
		S.Engine.TorqueCurve = { { 1000.0, 300.0 }, { 2000.0, 345.0 }, { 3000.0, 385.0 }, { 4000.0, 415.0 }, { 5000.0, 440.0 },
			{ 6300.0, 465.0 }, { 7300.0, 455.0 }, { 8500.0, 434.0 }, { 9000.0, 405.0 } };
		S.Engine.IdleRPM = 900.0f;
		S.Engine.LimiterRPM = 9000.0f;
		S.Engine.Inertia = 0.09f;	// (a light single-mass flywheel)
		S.Engine.LaunchRPM = 6500.0f;	// (its launch control)
		S.Engine.EngineBrakeTorque = 60.0f;
		S.Engine.ClutchTorque = 900.0f;
		// (the seven-speed twin clutch's gears as Porsche gives them for the GT3 RS; the 992's final drive 8% shorter
		// than the 991.2's 4.188, as its press kit says)
		S.Gearbox.GearRatios = { 3.75f, 2.381f, 1.70f, 1.344f, 1.114f, 0.957f, 0.844f };
		S.Gearbox.ReverseRatio = 3.417f;
		S.Gearbox.FinalDrive = 4.52f;
		S.Gearbox.UpshiftTime = 0.03f;
		S.Gearbox.DownshiftTime = 0.06f;
		S.Gearbox.AutoUpshiftRPM = 8800.0f;
		S.Gearbox.AutoDownshiftRPM = 4200.0f;
		S.Gearbox.DiffPreload = 60.0f;
		S.Gearbox.DiffPowerRatio = 0.35f;
		S.Gearbox.DiffCoastRatio = 0.25f;
		// 409 kg of downforce at 200 km/h, 860 at 285 (ClA about 2.15), 30% of it in front (most from the wing: less
		// on the front than its weight, so it steadies at speed; more had made it spin on a gentle bend at 250 km/h); drag
		// area about 1.0 m2 (its 296 km/h top speed on 386 kW)
		S.Aero.DownforceArea = 2.15f;
		S.Aero.DragArea = 1.0f;
		S.Aero.FrontBalance = 0.30f;

		// a flat six: firing every 120 degrees, banks alternating, a free-flowing sports exhaust
		D.Sound.FiringAngles = { 0, 120, 240, 360, 480, 600 };
		D.Sound.Banks = { 0, 1, 0, 1, 0, 1 };
		D.Sound.PipeLength[0] = 1.0f;
		D.Sound.PipeLength[1] = 1.05f;
		D.Sound.Openness = 0.6f;
		D.Sound.Crackle = 0.35f;
		D.Sound.RedlineRPM = 9000.0f;
		return D;
	}
}

const char* designs::Name(CarBody Body)
{
	switch (Body)
	{
	case CarBody::GT:      return "RS 4.0";
	case CarBody::Formula: return "Formula";
	case CarBody::Hatch:   return "Hatchback 1.6";
	case CarBody::Bakkie:  return "Bakkie 3.0 TDi";
	default:               return "Template sports car";
	}
}

CarDesign designs::Build(CarBody Body, const CarFrame& Template)
{
	switch (Body)
	{
	case CarBody::GT:      return CarDesignsImpl::BuildRS(Template);	// (the RS took the old coupe's place)
	case CarBody::Formula: return CarDesignsImpl::BuildFormula(Template);
	case CarBody::Hatch:   return CarDesignsImpl::BuildHatch(Template);
	case CarBody::Bakkie:  return CarDesignsImpl::BuildBakkie(Template);
	default:
	{
		// the template car's own body isn't ours: just its wheels, and a flat six's sound (even firing, two pipes)
		CarDesign D;
		D.Name = Name(Body);
		D.Frame = Template;
		return D;
	}
	}
}

CarFrame designs::DefaultTemplate()
{
	CarFrame Frame;
	Frame.Wheels[0] = Vec3(135.0, -90.0, 14.0);
	Frame.Wheels[1] = Vec3(135.0, 90.0, 14.0);
	Frame.Wheels[2] = Vec3(-129.2, -90.0, 14.5);
	Frame.Wheels[3] = Vec3(-129.2, 90.0, 14.5);
	return Frame;
}
}
