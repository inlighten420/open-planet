// Geodrive Core - GPL (see Core/README.md)

#include "Aircraft.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace geomesh
{
	using geoground::MeshData;
	using geoground::Vec2f;
	using geoground::Vec3f;
	using geoground::Vec4f;

	namespace AircraftImpl
	{
		constexpr float Pi = 3.14159265f;

		using geoground::Cross;
		using geoground::Dot;
		Vec3f Normalised(const Vec3f& V)
		{
			const float L = std::sqrt(Dot(V, V));
			return L > 1e-6f ? Vec3f(V.X / L, V.Y / L, V.Z / L) : Vec3f(0.0f, 0.0f, 1.0f);
		}

		/** Builds in metres (the aircraft's numbers are), writes cm; every face flat (the low-poly look) */
		struct Builder
		{
			MeshData& Out;

			/** A flat triangle facing away from Inside, in Colour */
			void Triangle(const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& Inside, const Vec4f& Colour)
			{
				Vec3f N = Normalised(Cross(B - A, C - A));
				const Vec3f Middle((A.X + B.X + C.X) / 3.0f, (A.Y + B.Y + C.Y) / 3.0f, (A.Z + B.Z + C.Z) / 3.0f);
				const bool bInward = Dot(N, Middle - Inside) < 0.0f;
				if (bInward)
				{
					N = Vec3f(-N.X, -N.Y, -N.Z);
				}
				const int32_t First = int32_t(Out.Positions.size());
				for (const Vec3f* P : { &A, &B, &C })
				{
					Out.Positions.push_back(Vec3f(P->X * 100.0f, P->Y * 100.0f, P->Z * 100.0f));
					Out.Normals.push_back(N);
					Out.UVs.push_back(Vec2f{ 0.0f, 0.0f });
					Out.Colors.push_back(Colour);
				}
				// (wound as Core's meshes are: front-facing when (B - A) x (C - A) points away from the viewer)
				const bool bFlip = Dot(Cross(B - A, C - A), N) > 0.0f;
				Out.Indices.insert(Out.Indices.end(), { First, bFlip ? First + 2 : First + 1, bFlip ? First + 1 : First + 2 });
				Out.Tags.push_back(-1);
			}

			void Quad(const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& D, const Vec3f& Inside, const Vec4f& Colour)
			{
				Triangle(A, B, C, Inside, Colour);
				Triangle(A, C, D, Inside, Colour);
			}

			/** A body of revolution (roughly): rings at X with half widths Wide, half heights Tall, centred at Z */
			struct Ring { float X, Wide, Tall, Z; };
			void Loft(const std::vector<Ring>& Rings, int32_t Sides, const Vec4f& Colour, float Y = 0.0f)
			{
				for (size_t r = 0; r + 1 < Rings.size(); ++r)
				{
					const Ring& A = Rings[r];
					const Ring& B = Rings[r + 1];
					const Vec3f Inside((A.X + B.X) * 0.5f, Y, (A.Z + B.Z) * 0.5f);
					for (int32_t i = 0; i < Sides; ++i)
					{
						const float A0 = 2.0f * Pi * float(i) / float(Sides), A1 = 2.0f * Pi * float(i + 1) / float(Sides);
						auto At = [&](const Ring& R, float Angle) { return Vec3f(R.X, Y + std::cos(Angle) * R.Wide, R.Z + std::sin(Angle) * R.Tall); };
						const Vec3f P0 = At(A, A0), P1 = At(A, A1), P2 = At(B, A1), P3 = At(B, A0);
						if (A.Wide <= 0.0f)
						{
							Triangle(P0, P2, P3, Inside, Colour);
						}
						else if (B.Wide <= 0.0f)
						{
							Triangle(P0, P1, P2, Inside, Colour);
						}
						else
						{
							Quad(P0, P1, P2, P3, Inside, Colour);
						}
					}
				}
			}

			/** A slab between a root and a tip (wings, tails, fins): each a leading and trailing edge point, Thick through */
			void Slab(const Vec3f& RootFront, const Vec3f& RootBack, const Vec3f& TipFront, const Vec3f& TipBack, const Vec3f& Through, const Vec4f& Colour)
			{
				const Vec3f H(Through.X * 0.5f, Through.Y * 0.5f, Through.Z * 0.5f);
				const Vec3f C[8] = { RootFront + H, RootBack + H, TipBack + H, TipFront + H, RootFront - H, RootBack - H, TipBack - H, TipFront - H };
				const Vec3f Inside((RootFront.X + RootBack.X + TipFront.X + TipBack.X) * 0.25f, (RootFront.Y + RootBack.Y + TipFront.Y + TipBack.Y) * 0.25f,
					(RootFront.Z + RootBack.Z + TipFront.Z + TipBack.Z) * 0.25f);
				Quad(C[0], C[1], C[2], C[3], Inside, Colour);
				Quad(C[4], C[5], C[6], C[7], Inside, Colour);
				Quad(C[0], C[3], C[7], C[4], Inside, Colour);
				Quad(C[1], C[2], C[6], C[5], Inside, Colour);
				Quad(C[3], C[2], C[6], C[7], Inside, Colour);
				Quad(C[0], C[1], C[5], C[4], Inside, Colour);
			}

			/** A tube along x (engines, wheels' axles turned: see Wheel) */
			void Tube(float Front, float Back, float Radius, float Y, float Z, int32_t Sides, const Vec4f& Colour, const Vec4f& FrontColour)
			{
				Loft({ { Front, 0.0f, 0.0f, Z }, { Front, Radius, Radius, Z }, { Back, Radius * 0.85f, Radius * 0.85f, Z }, { Back, 0.0f, 0.0f, Z } }, Sides, Colour, Y);
				// (the intake's dark face)
				for (int32_t i = 0; i < Sides; ++i)
				{
					const float A0 = 2.0f * Pi * float(i) / float(Sides), A1 = 2.0f * Pi * float(i + 1) / float(Sides);
					Triangle(Vec3f(Front + 0.01f, Y, Z), Vec3f(Front + 0.01f, Y + std::cos(A0) * Radius * 0.9f, Z + std::sin(A0) * Radius * 0.9f),
						Vec3f(Front + 0.01f, Y + std::cos(A1) * Radius * 0.9f, Z + std::sin(A1) * Radius * 0.9f), Vec3f(Front - 1.0f, Y, Z), FrontColour);
				}
			}

			/** A wheel: a short drum across (along y) */
			void Wheel(const Vec3f& Centre, float Radius, float Width, const Vec4f& Colour)
			{
				constexpr int32_t Sides = 10;
				const Vec3f Inside = Centre;
				for (int32_t i = 0; i < Sides; ++i)
				{
					const float A0 = 2.0f * Pi * float(i) / float(Sides), A1 = 2.0f * Pi * float(i + 1) / float(Sides);
					auto At = [&](float Angle, float Side) { return Vec3f(Centre.X + std::cos(Angle) * Radius, Centre.Y + Side, Centre.Z + std::sin(Angle) * Radius); };
					const float L = -Width * 0.5f, R = Width * 0.5f;
					Quad(At(A0, L), At(A1, L), At(A1, R), At(A0, R), Inside, Colour);
					Triangle(Vec3f(Centre.X, Centre.Y + L, Centre.Z), At(A0, L), At(A1, L), Vec3f(Centre.X, Centre.Y, Centre.Z), Colour);
					Triangle(Vec3f(Centre.X, Centre.Y + R, Centre.Z), At(A0, R), At(A1, R), Vec3f(Centre.X, Centre.Y, Centre.Z), Colour);
				}
			}

			/** A strut from Top down to Bottom */
			void Strut(const Vec3f& Top, const Vec3f& Bottom, float Thick, const Vec4f& Colour)
			{
				Slab(Vec3f(Top.X + Thick * 0.5f, Top.Y, Top.Z), Vec3f(Top.X - Thick * 0.5f, Top.Y, Top.Z),
					Vec3f(Bottom.X + Thick * 0.5f, Bottom.Y, Bottom.Z), Vec3f(Bottom.X - Thick * 0.5f, Bottom.Y, Bottom.Z), Vec3f(0.0f, Thick, 0.0f), Colour);
			}
		};

		const Vec4f White{ 0.78f, 0.79f, 0.8f, 1.0f };
		const Vec4f Belly{ 0.55f, 0.57f, 0.6f, 1.0f };
		const Vec4f FighterGrey{ 0.25f, 0.28f, 0.31f, 1.0f };
		const Vec4f Glass{ 0.02f, 0.03f, 0.05f, 1.0f };
		const Vec4f Metal{ 0.45f, 0.46f, 0.48f, 1.0f };
		const Vec4f Dark{ 0.04f, 0.04f, 0.045f, 1.0f };
		const Vec4f Tyre{ 0.03f, 0.03f, 0.03f, 1.0f };
		const Vec4f Navy{ 0.03f, 0.08f, 0.3f, 1.0f };
		const Vec4f Green{ 0.02f, 0.22f, 0.1f, 1.0f };

		/** Both sides of the aircraft (y and -y) */
		template <typename F> void Mirrored(F&& Make)
		{
			Make(1.0f);
			Make(-1.0f);
		}

		/** An airliner's shape: fuselage Radius, Length (nose to tail, the middle Nose metres back from the nose), wings
		 *  from Root to Tip, engines, tailplane and fin; Tail the fin's colour */
		struct Airframe
		{
			float Nose, Tail, Radius;
			float WingRootY, WingRootFront, WingRootBack, WingRootZ, WingTipY, WingTipFront, WingTipBack, WingTipZ, WingThick;
			float EngineY, EngineZ, EngineRadius, EngineFront, EngineBack;
			float TailRootY, TailRootFront, TailRootBack, TailTipY, TailTipFront, TailTipBack, TailZ;
			float FinRootZ, FinRootFront, FinRootBack, FinTipZ, FinTipFront, FinTipBack;
			Vec4f FinColour;
		};

		void BuildAirliner(Builder& B, const Airframe& A)
		{
			const float R = A.Radius;
			B.Loft({ { A.Nose, 0.0f, 0.0f, -0.15f * R }, { A.Nose - 0.6f, 0.45f * R, 0.4f * R, -0.12f * R }, { A.Nose - 2.0f, 0.85f * R, 0.85f * R, -0.03f * R },
				{ A.Nose - 4.0f, R, R, 0.0f }, { A.Tail + 11.0f, R, R, 0.0f }, { A.Tail + 5.0f, 0.7f * R, 0.75f * R, 0.25f * R },
				{ A.Tail, 0.18f * R, 0.2f * R, 0.65f * R }, { A.Tail - 0.01f, 0.0f, 0.0f, 0.65f * R } }, 12, White);
			// the cockpit's windows: a dark band across the nose's top
			B.Slab(Vec3f(A.Nose - 1.3f, -0.55f * R, 0.42f * R), Vec3f(A.Nose - 2.3f, -0.75f * R, 0.62f * R), Vec3f(A.Nose - 1.3f, 0.55f * R, 0.42f * R),
				Vec3f(A.Nose - 2.3f, 0.75f * R, 0.62f * R), Vec3f(0.0f, 0.0f, 0.12f), Glass);
			Mirrored([&](float S)
			{
				B.Slab(Vec3f(A.WingRootFront, S * A.WingRootY, A.WingRootZ), Vec3f(A.WingRootBack, S * A.WingRootY, A.WingRootZ),
					Vec3f(A.WingTipFront, S * A.WingTipY, A.WingTipZ), Vec3f(A.WingTipBack, S * A.WingTipY, A.WingTipZ), Vec3f(0.0f, 0.0f, A.WingThick), White);
				// a winglet
				B.Slab(Vec3f(A.WingTipFront - 0.3f, S * A.WingTipY, A.WingTipZ), Vec3f(A.WingTipBack, S * A.WingTipY, A.WingTipZ),
					Vec3f(A.WingTipBack - 0.4f, S * (A.WingTipY + 0.2f), A.WingTipZ + 1.6f), Vec3f(A.WingTipBack + 0.2f, S * (A.WingTipY + 0.2f), A.WingTipZ + 1.6f),
					Vec3f(0.0f, 0.1f, 0.0f), A.FinColour);
				B.Tube(A.EngineFront, A.EngineBack, A.EngineRadius, S * A.EngineY, A.EngineZ, 12, Metal, Dark);
				// the pylon up to the wing
				B.Slab(Vec3f(A.EngineFront - 0.8f, S * A.EngineY, A.EngineZ + A.EngineRadius * 0.7f), Vec3f(A.EngineBack - 0.6f, S * A.EngineY, A.EngineZ + A.EngineRadius * 0.7f),
					Vec3f(A.EngineFront - 1.6f, S * A.EngineY, A.EngineZ + A.EngineRadius + 0.9f), Vec3f(A.EngineBack - 1.2f, S * A.EngineY, A.EngineZ + A.EngineRadius + 0.9f),
					Vec3f(0.0f, 0.35f, 0.0f), White);
				B.Slab(Vec3f(A.TailRootFront, S * A.TailRootY, A.TailZ), Vec3f(A.TailRootBack, S * A.TailRootY, A.TailZ),
					Vec3f(A.TailTipFront, S * A.TailTipY, A.TailZ + 0.3f), Vec3f(A.TailTipBack, S * A.TailTipY, A.TailZ + 0.3f), Vec3f(0.0f, 0.0f, 0.25f), White);
			});
			B.Slab(Vec3f(A.FinRootFront, 0.0f, A.FinRootZ), Vec3f(A.FinRootBack, 0.0f, A.FinRootZ), Vec3f(A.FinTipFront, 0.0f, A.FinTipZ),
				Vec3f(A.FinTipBack, 0.0f, A.FinTipZ), Vec3f(0.0f, 0.3f, 0.0f), A.FinColour);
		}

		void BuildFighter(Builder& B)
		{
			// an F-16's shape: 15 m long, a 9.96 m wing swept 40 degrees, one fin, the intake under the cockpit
			B.Loft({ { 7.6f, 0.0f, 0.0f, 0.0f }, { 6.4f, 0.32f, 0.32f, 0.05f }, { 4.5f, 0.6f, 0.65f, 0.12f }, { 2.0f, 0.78f, 0.72f, 0.05f },
				{ -3.0f, 0.82f, 0.68f, 0.0f }, { -6.0f, 0.62f, 0.55f, 0.0f }, { -7.4f, 0.48f, 0.48f, 0.0f }, { -7.41f, 0.0f, 0.0f, 0.0f } }, 10, FighterGrey);
			// the canopy, and the nozzle's dark end
			B.Loft({ { 6.0f, 0.0f, 0.0f, 0.55f }, { 5.3f, 0.3f, 0.3f, 0.6f }, { 3.8f, 0.36f, 0.42f, 0.62f }, { 2.6f, 0.2f, 0.25f, 0.55f }, { 2.0f, 0.0f, 0.0f, 0.5f } }, 10, Glass);
			B.Loft({ { -7.3f, 0.5f, 0.5f, 0.0f }, { -7.6f, 0.45f, 0.45f, 0.0f }, { -7.61f, 0.0f, 0.0f, 0.0f } }, 10, Dark);
			// the intake under the fuselage
			B.Slab(Vec3f(4.0f, -0.5f, -0.75f), Vec3f(0.5f, -0.5f, -0.75f), Vec3f(4.0f, 0.5f, -0.75f), Vec3f(0.5f, 0.5f, -0.75f), Vec3f(0.0f, 0.0f, 0.6f), FighterGrey);
			B.Slab(Vec3f(4.02f, -0.4f, -0.8f), Vec3f(3.9f, -0.4f, -0.8f), Vec3f(4.02f, 0.4f, -0.8f), Vec3f(3.9f, 0.4f, -0.8f), Vec3f(0.0f, 0.0f, 0.45f), Dark);
			Mirrored([&](float S)
			{
				// the strakes blending into the wings, the wings, the tailplanes
				B.Slab(Vec3f(5.0f, S * 0.6f, 0.0f), Vec3f(2.0f, S * 0.7f, 0.0f), Vec3f(2.3f, S * 1.1f, 0.0f), Vec3f(1.6f, S * 1.1f, 0.0f), Vec3f(0.0f, 0.0f, 0.1f), FighterGrey);
				B.Slab(Vec3f(2.2f, S * 0.7f, 0.0f), Vec3f(-2.8f, S * 0.7f, 0.0f), Vec3f(-2.0f, S * 4.98f, -0.05f), Vec3f(-3.3f, S * 4.98f, -0.05f), Vec3f(0.0f, 0.0f, 0.14f), FighterGrey);
				B.Slab(Vec3f(-4.6f, S * 0.6f, -0.1f), Vec3f(-7.0f, S * 0.6f, -0.1f), Vec3f(-6.4f, S * 2.75f, -0.25f), Vec3f(-7.6f, S * 2.75f, -0.25f), Vec3f(0.0f, 0.0f, 0.1f), FighterGrey);
			});
			B.Slab(Vec3f(-3.8f, 0.0f, 0.5f), Vec3f(-7.2f, 0.0f, 0.5f), Vec3f(-6.0f, 0.0f, 3.0f), Vec3f(-7.2f, 0.0f, 3.0f), Vec3f(0.0f, 0.14f, 0.0f), FighterGrey);
		}

		void BuildGear(Builder& B, AircraftKind Kind)
		{
			// the wheels where the flight model has them standing (their struts squashed half their travel by its weight, as
			// the flight model's springs are; drawn fully out they were sunk in the runway), struts up into the body
			struct Leg { float X, Y, Bottom, Top, Radius; int Pairs; };
			float Travel = 0.4f;
			std::vector<Leg> Legs;
			switch (Kind)
			{
			case AircraftKind::FighterJet:
				Travel = 0.25f;
				Legs = { { 4.0f, 0.0f, -1.85f, -0.6f, 0.3f, 1 }, { -0.6f, 1.18f, -1.85f, -0.5f, 0.38f, 1 }, { -0.6f, -1.18f, -1.85f, -0.5f, 0.38f, 1 } };
				break;
			case AircraftKind::Airliner:
				Travel = 0.45f;
				Legs = { { 11.5f, 0.0f, -3.4f, -1.5f, 0.38f, 2 }, { -1.2f, 3.8f, -3.4f, -1.3f, 0.57f, 2 }, { -1.2f, -3.8f, -3.4f, -1.3f, 0.57f, 2 } };
				break;
			case AircraftKind::RegionalJet:
			default:
				Legs = { { 13.5f, 0.0f, -2.9f, -1.1f, 0.33f, 2 }, { -1.2f, 2.97f, -2.9f, -0.9f, 0.5f, 2 }, { -1.2f, -2.97f, -2.9f, -0.9f, 0.5f, 2 } };
				break;
			}
			for (const Leg& L : Legs)
			{
				const float Axle = L.Bottom + Travel * 0.5f + L.Radius;
				B.Strut(Vec3f(L.X, L.Y, L.Top), Vec3f(L.X, L.Y, Axle), L.Radius * 0.35f, Metal);
				for (int p = 0; p < L.Pairs; ++p)
				{
					const float Off = L.Pairs == 1 ? 0.0f : (p == 0 ? -1.0f : 1.0f) * L.Radius * 0.75f;
					B.Wheel(Vec3f(L.X, L.Y + Off, Axle), L.Radius, L.Radius * 0.6f, Tyre);
				}
			}
		}
	}

	void BuildAircraft(AircraftKind Kind, bool bGear, MeshData& Out)
	{
		using namespace AircraftImpl;
		Out = MeshData();
		Builder B{ Out };
		if (bGear)
		{
			BuildGear(B, Kind);
			return;
		}
		switch (Kind)
		{
		case AircraftKind::FighterJet:
			BuildFighter(B);
			break;
		case AircraftKind::Airliner:
		{
			// an A320's shape: 37.6 m long, 3.95 m across, 34.1 m of wing swept 25 degrees, the engines under it
			Airframe A{ 17.6f, -20.0f, 1.97f,
				1.9f, 3.5f, -3.5f, -1.2f, 17.05f, -5.5f, -7.0f, 0.1f, 0.35f,
				5.75f, -1.7f, 1.0f, 4.5f, 0.5f,	// (the nacelles ~0.5 m off the runway, standing)
				0.8f, -13.0f, -17.0f, 6.2f, -16.5f, -18.3f, 0.8f,
				1.8f, -12.5f, -18.3f, 8.4f, -16.6f, -18.8f, Navy };
			BuildAirliner(B, A);
			break;
		}
		case AircraftKind::RegionalJet:
		default:
		{
			// an E195's shape: 38.7 m long, 3 m across, 28.7 m of wing, the engines under it
			Airframe A{ 19.0f, -19.6f, 1.5f,
				1.45f, 3.0f, -2.8f, -0.9f, 14.36f, -4.5f, -5.8f, 0.2f, 0.3f,
				5.2f, -1.55f, 0.75f, 3.5f, 0.3f,
				0.6f, -14.0f, -17.5f, 5.3f, -17.0f, -18.8f, 0.6f,
				1.4f, -13.0f, -18.5f, 7.6f, -16.8f, -19.2f, Green };
			BuildAirliner(B, A);
			break;
		}
		}
	}
}
