// Geodrive Core - GPL (see Core/README.md)

#include "Plants.h"

#include <algorithm>
#include <cmath>

namespace geomesh
{
	using geoground::MeshData;
	using geoground::Vec2f;
	using geoground::Vec3f;
	using geoground::Vec4f;

	namespace PlantsImpl
	{
		constexpr float Pi = 3.14159265f;

		/** A small, steady random number source (the same plant for the same seed) */
		struct Dice
		{
			uint32_t State;
			explicit Dice(uint32_t Seed) : State(Seed * 747796405u + 2891336453u) {}
			float Next()	// 0-1
			{
				State ^= State << 13;
				State ^= State >> 17;
				State ^= State << 5;
				return float(State & 0xFFFFFF) / float(0xFFFFFF);
			}
			float Between(float A, float B) { return A + (B - A) * Next(); }
		};

		Vec3f Normalised(const Vec3f& V)
		{
			const float L = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
			return L > 1e-6f ? Vec3f(V.X / L, V.Y / L, V.Z / L) : Vec3f(0.0f, 0.0f, 1.0f);
		}

		Vec3f CrossOf(const Vec3f& A, const Vec3f& B) { return Vec3f(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X); }

		struct Builder
		{
			MeshData& Out;
			float Top = 1.0f;	// the plant's height (cm), for how far up each corner is
			float Own = 0.0f;	// the plant's own number

			void Corner(const Vec3f& P, const Vec3f& N, const Vec2f& UV, float Part)
			{
				Out.Positions.push_back(P);
				Out.Normals.push_back(N);
				Out.UVs.push_back(UV);
				Out.Colors.push_back(Vec4f{ Part, std::clamp(P.Z / Top, 0.0f, 1.0f), Own, 1.0f });
			}

			/** A triangle facing along Facing (wound as Core's meshes are: see MeshData::AddTriangle) */
			void Triangle(int32_t A, int32_t B, int32_t C, const Vec3f& Facing)
			{
				const Vec3f& PA = Out.Positions[size_t(A)];
				const Vec3f& PB = Out.Positions[size_t(B)];
				const Vec3f& PC = Out.Positions[size_t(C)];
				const Vec3f Face = CrossOf(PB - PA, PC - PA);
				const bool bFlip = Face.X * Facing.X + Face.Y * Facing.Y + Face.Z * Facing.Z > 0.0f;
				Out.Indices.insert(Out.Indices.end(), { A, bFlip ? C : B, bFlip ? B : C });
				Out.Tags.push_back(-1);
			}

			/** A tapering wooden limb from A (radius RA) to B (RB), Sides round, bark tiled every 1.5 m up it */
			void Limb(const Vec3f& A, const Vec3f& B, float RA, float RB, int32_t Sides)
			{
				const Vec3f Axis = Normalised(B - A);
				const Vec3f Side = Normalised(std::fabs(Axis.Z) < 0.9f ? CrossOf(Axis, Vec3f(0.0f, 0.0f, 1.0f)) : CrossOf(Axis, Vec3f(1.0f, 0.0f, 0.0f)));
				const Vec3f Other = CrossOf(Axis, Side);
				const float Length = std::sqrt((B.X - A.X) * (B.X - A.X) + (B.Y - A.Y) * (B.Y - A.Y) + (B.Z - A.Z) * (B.Z - A.Z));
				const int32_t First = int32_t(Out.Positions.size());
				for (int32_t i = 0; i <= Sides; ++i)
				{
					const float Angle = 2.0f * Pi * float(i) / float(Sides);
					const Vec3f Out_ = Side * std::cos(Angle) + Other * std::sin(Angle);
					Corner(A + Out_ * RA, Out_, Vec2f{ float(i) / float(Sides) * 2.0f, 0.0f }, 0.0f);
					Corner(B + Out_ * RB, Out_, Vec2f{ float(i) / float(Sides) * 2.0f, Length / 150.0f }, 0.0f);
				}
				for (int32_t i = 0; i < Sides; ++i)
				{
					const int32_t L0 = First + i * 2, U0 = L0 + 1, L1 = L0 + 2, U1 = L0 + 3;
					const float Angle = 2.0f * Pi * (float(i) + 0.5f) / float(Sides);
					const Vec3f Facing = Side * std::cos(Angle) + Other * std::sin(Angle);
					Triangle(L0, L1, U1, Facing);
					Triangle(L0, U1, U0, Facing);
				}
			}
		};

		/** A point in an egg-shaped crown (Middle, half sizes Radius across and Tall up), between Inner and 1 of the way out */
		Vec3f InCrown(const Vec3f& Middle, float Radius, float Tall, float Inner, Dice& Roll)
		{
			const float Z = Roll.Between(-1.0f, 1.0f);
			const float Angle = Roll.Between(0.0f, 2.0f * Pi);
			const float Ring = std::sqrt(std::max(0.0f, 1.0f - Z * Z));
			const float Out = Roll.Between(Inner, 1.0f);
			return Middle + Vec3f(Ring * std::cos(Angle) * Radius * Out, Ring * std::sin(Angle) * Radius * Out, Z * Tall * Out);
		}
			/** A lumpy round blob of leaves (Middle, half sizes Radius across and Tall up), its corners pushed in and out by
		 *  the dice so the outline isn't smooth; normals half its own, half out from the crown's middle (Crown), so a
		 *  crown of blobs shades as one round thing */
		void Blob(Builder& B, const Vec3f& Middle, float Radius, float Tall, const Vec3f& Crown, int32_t Rings, int32_t Sides, Dice& Roll)
		{
			const int32_t First = int32_t(B.Out.Positions.size());
			for (int32_t r = 0; r <= Rings; ++r)
			{
				const float Up = Pi * float(r) / float(Rings);	// 0 the top, Pi the bottom
				for (int32_t i = 0; i < Sides; ++i)
				{
					const float Angle = 2.0f * Pi * (float(i) + (r % 2) * 0.5f) / float(Sides);
					const float Bump = (r == 0 || r == Rings) ? 1.0f : Roll.Between(0.8f, 1.12f);
					const Vec3f Dir(std::sin(Up) * std::cos(Angle), std::sin(Up) * std::sin(Angle), std::cos(Up));
					const Vec3f P = Middle + Vec3f(Dir.X * Radius * Bump, Dir.Y * Radius * Bump, Dir.Z * Tall * (r == Rings ? 0.6f : Bump));
					const Vec3f N = Normalised(Normalised(Dir) * 0.5f + Normalised(P - Crown) * 0.5f);
					B.Corner(P, N, Vec2f{ float(i) / float(Sides), float(r) / float(Rings) }, 1.0f);
				}
			}
			for (int32_t r = 0; r < Rings; ++r)
			{
				for (int32_t i = 0; i < Sides; ++i)
				{
					const int32_t A = First + r * Sides + i, Bn = First + r * Sides + (i + 1) % Sides;
					const int32_t C = A + Sides, D = Bn + Sides;
					const Vec3f Out = Normalised(B.Out.Positions[size_t(A)] + B.Out.Positions[size_t(D)] - Middle * 2.0f);
					if (r > 0)
					{
						B.Triangle(A, Bn, D, Out);
					}
					if (r + 1 < Rings)
					{
						B.Triangle(A, D, C, Out);
					}
				}
			}
		}

		/** A conifer's tier: a cone of needles from a skirt (radius Radius at Z0, drooping a little) up to a point at Z1,
		 *  with a shallow cone underneath (seen from below, driving past) */
		void Tier(Builder& B, float Z0, float Z1, float Radius, int32_t Sides, Dice& Roll)
		{
			const Vec3f Axis(0.0f, 0.0f, (Z0 + Z1) * 0.5f);
			const int32_t Top = int32_t(B.Out.Positions.size());
			B.Corner(Vec3f(0.0f, 0.0f, Z1), Vec3f(0.0f, 0.0f, 1.0f), Vec2f{ 0.5f, 0.0f }, 1.0f);
			const int32_t Under = Top + 1;
			B.Corner(Vec3f(0.0f, 0.0f, Z0 + (Z1 - Z0) * 0.3f), Vec3f(0.0f, 0.0f, -1.0f), Vec2f{ 0.5f, 1.0f }, 1.0f);
			const int32_t Skirt = Top + 2;
			for (int32_t i = 0; i < Sides; ++i)
			{
				const float Angle = 2.0f * Pi * (float(i) + Roll.Between(-0.15f, 0.15f)) / float(Sides);
				const float R = Radius * Roll.Between(0.85f, 1.1f);
				const Vec3f P(std::cos(Angle) * R, std::sin(Angle) * R, Z0 - Roll.Between(0.0f, 0.12f) * (Z1 - Z0));
				B.Corner(P, Normalised(Normalised(P - Axis) + Vec3f(0.0f, 0.0f, 0.4f)), Vec2f{ float(i) / float(Sides), 1.0f }, 1.0f);
			}
			for (int32_t i = 0; i < Sides; ++i)
			{
				const int32_t A = Skirt + i, Bn = Skirt + (i + 1) % Sides;
				const Vec3f Out = Normalised(B.Out.Positions[size_t(A)] + B.Out.Positions[size_t(Bn)] - Axis * 2.0f);
				B.Triangle(Top, A, Bn, Out + Vec3f(0.0f, 0.0f, 0.5f));
				B.Triangle(Under, Bn, A, Vec3f(0.0f, 0.0f, -1.0f));
			}
		}
	}

	void BuildPlant(PlantKind Kind, uint32_t Seed, int32_t Detail, MeshData& Out)
	{
		using namespace PlantsImpl;
		Out = MeshData();
		Dice Roll(Seed + uint32_t(Kind) * 7919u);
		Builder B{ Out };
		B.Own = Roll.Next();
		const bool bFull = Detail > 0;
		const bool bFar = Detail < 0;	// (far off: the fewest corners that still read as the tree)
		const int32_t Rings = bFull ? 4 : 3, Sides = bFull ? 6 : 5;
		const int32_t Bark = bFull ? 6 : (bFar ? 3 : 4);
		switch (Kind)
		{
		case PlantKind::Broadleaf:
		{
			// a short trunk, a round crown of five to seven blobs
			B.Top = 900.0f;
			const Vec3f Crown(0.0f, 0.0f, 590.0f);
			B.Limb(Vec3f(0.0f, 0.0f, -100.0f), Vec3f(0.0f, 0.0f, 450.0f), 24.0f, 14.0f, Bark);
			Blob(B, Crown, bFar ? 360.0f : 290.0f, bFar ? 290.0f : 240.0f, Crown, Rings, Sides, Roll);
			const int32_t Blobs = bFull ? 5 : (bFar ? 0 : 3);
			for (int32_t i = 0; i < Blobs; ++i)
			{
				const Vec3f At = InCrown(Crown, 220.0f, 170.0f, 0.6f, Roll);
				Blob(B, At, Roll.Between(150.0f, 210.0f), Roll.Between(120.0f, 170.0f), Crown, Rings, Sides, Roll);
			}
			break;
		}
		case PlantKind::Conifer:
		{
			// a trunk and four or five cones of needles, smaller as they go up
			B.Top = 1600.0f;
			B.Limb(Vec3f(0.0f, 0.0f, -100.0f), Vec3f(0.0f, 0.0f, 600.0f), 26.0f, 16.0f, Bark);
			const int32_t Tiers = bFull ? 5 : (bFar ? 2 : 3);
			for (int32_t t = 0; t < Tiers; ++t)
			{
				const float Up = float(t) / float(Tiers);
				const float Z0 = 300.0f + Up * 1000.0f;
				Tier(B, Z0, Z0 + (bFar ? 800.0f : 520.0f) - Up * 120.0f, 380.0f * (1.0f - Up * 0.75f), bFull ? 9 : (bFar ? 5 : 6), Roll);
			}
			break;
		}
		case PlantKind::Eucalyptus:
		{
			// a pale trunk bare for the first 7 m, then a big loose crown of separate clumps (gaps between: gums do)
			B.Top = 1800.0f;
			const Vec3f Crown(0.0f, 0.0f, 1250.0f);
			B.Limb(Vec3f(0.0f, 0.0f, -100.0f), Vec3f(0.0f, 0.0f, 1300.0f), 30.0f, 9.0f, Bark);
			const int32_t Clumps = bFull ? 6 : (bFar ? 2 : 4);
			for (int32_t i = 0; i < Clumps; ++i)
			{
				Vec3f At = InCrown(Crown, 380.0f, 380.0f, 0.5f, Roll);
				At.Z = std::max(At.Z, 950.0f);
				if (bFull)
				{
					B.Limb(Vec3f(0.0f, 0.0f, 750.0f + 80.0f * float(i)), At, 9.0f, 4.0f, 4);
				}
				Blob(B, At, Roll.Between(170.0f, 230.0f) * (bFar ? 1.5f : 1.0f), Roll.Between(110.0f, 150.0f) * (bFar ? 1.4f : 1.0f), Crown, Rings, Sides, Roll);
			}
			break;
		}
		case PlantKind::Bush:
		default:
		{
			// two or three low blobs
			B.Top = 170.0f;
			const Vec3f Middle(0.0f, 0.0f, 50.0f);
			Blob(B, Middle, 110.0f, 75.0f, Middle, 3, 5, Roll);
			for (int32_t i = 0; i < (bFull ? 2 : 1); ++i)
			{
				Vec3f At = InCrown(Middle, 70.0f, 30.0f, 0.5f, Roll);
				At.Z = std::max(At.Z, 40.0f);
				Blob(B, At, Roll.Between(60.0f, 85.0f), Roll.Between(50.0f, 70.0f), Middle, 3, 5, Roll);
			}
			break;
		}
		}
	}
}
