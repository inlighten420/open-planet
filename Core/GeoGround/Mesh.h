// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Plain mesh data as Core builds it: triangles with their own three corners (positions in world cm, x north, y east,
// z up), per-corner normals, colours (linear) and UVs. The float vectors are laid out as three / four / two floats, the
// same as the engines' own, so a host can copy a whole mesh across in one go.

#pragma once

#include "GeoRoads/RoadGraph.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace geoground
{
	using georoads::Vec2;

	struct Vec2f
	{
		float X = 0.0f, Y = 0.0f;
	};

	struct Vec3f
	{
		float X = 0.0f, Y = 0.0f, Z = 0.0f;

		Vec3f() = default;
		Vec3f(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}

		Vec3f operator+(const Vec3f& O) const { return { X + O.X, Y + O.Y, Z + O.Z }; }
		Vec3f operator-(const Vec3f& O) const { return { X - O.X, Y - O.Y, Z - O.Z }; }
		Vec3f operator-() const { return { -X, -Y, -Z }; }
		Vec3f operator*(float S) const { return { X * S, Y * S, Z * S }; }

		/** A unit vector, or zero if too short to have a direction */
		Vec3f Normal() const
		{
			const float SquareSum = X * X + Y * Y + Z * Z;
			if (SquareSum == 1.0f)
			{
				return *this;
			}
			if (SquareSum < 1.0e-8f)
			{
				return Vec3f();
			}
			const float Scale = 1.0f / std::sqrt(SquareSum);
			return { X * Scale, Y * Scale, Z * Scale };
		}
	};

	inline Vec3f Cross(const Vec3f& A, const Vec3f& B) { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
	inline float Dot(const Vec3f& A, const Vec3f& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
	inline Vec3f Lerp(const Vec3f& A, const Vec3f& B, float T) { return A + (B - A) * T; }

	struct Vec4f
	{
		float X = 0.0f, Y = 0.0f, Z = 0.0f, W = 0.0f;

		Vec4f operator+(const Vec4f& O) const { return { X + O.X, Y + O.Y, Z + O.Z, W + O.W }; }
		Vec4f operator-(const Vec4f& O) const { return { X - O.X, Y - O.Y, Z - O.Z, W - O.W }; }
		Vec4f operator*(float S) const { return { X * S, Y * S, Z * S, W * S }; }
	};

	inline Vec4f Lerp(const Vec4f& A, const Vec4f& B, float T) { return A + (B - A) * T; }

	/** Small maths shared by the ground code */
	constexpr double Pi = 3.141592653589793238462643383279502884197169399;
	inline float LerpF(float A, float B, float T) { return A + T * (B - A); }
	inline float SmoothStepF(float A, float B, float X)
	{
		if (X < A) { return 0.0f; }
		if (X >= B) { return 1.0f; }
		const float T = (X - A) / (B - A);
		return T * T * (3.0f - 2.0f * T);
	}
	inline int32_t FloorToInt(double X) { return static_cast<int32_t>(std::floor(X)); }
	inline int32_t CeilToInt(double X) { return static_cast<int32_t>(std::ceil(X)); }
	inline int32_t RoundToInt(double X) { return static_cast<int32_t>(std::floor(X + 0.5)); }

	static_assert(sizeof(Vec2f) == 8 && sizeof(Vec3f) == 12 && sizeof(Vec4f) == 16, "laid out as plain floats");

	/** An axis-aligned box on the ground (cm); IsInside is strict, as the engines' are */
	struct Box2
	{
		Vec2 Min, Max;
		bool bValid = false;

		Box2() = default;
		Box2(const Vec2& InMin, const Vec2& InMax) : Min(InMin), Max(InMax), bValid(true) {}

		Box2& operator+=(const Vec2& P)
		{
			if (bValid)
			{
				Min = { std::fmin(Min.X, P.X), std::fmin(Min.Y, P.Y) };
				Max = { std::fmax(Max.X, P.X), std::fmax(Max.Y, P.Y) };
			}
			else
			{
				Min = Max = P;
				bValid = true;
			}
			return *this;
		}
		Box2 ExpandBy(double W) const { Box2 Out(Min - Vec2(W, W), Max + Vec2(W, W)); Out.bValid = bValid; return Out; }
		bool IsInside(const Vec2& P) const { return P.X > Min.X && P.X < Max.X && P.Y > Min.Y && P.Y < Max.Y; }
		Vec2 Size() const { return Max - Min; }
	};

	struct MeshData
	{
		std::vector<Vec3f> Positions;
		std::vector<Vec3f> Normals;
		std::vector<Vec2f> UVs;
		std::vector<Vec4f> Colors;	// one per corner, linear
		std::vector<int32_t> Indices;

		/** Who each triangle belongs to (roads: the edge, junction surfaces: -(node + 1)); for checking only */
		std::vector<int32_t> Tags;
		int32_t Tag = -1;

		bool IsEmpty() const { return Indices.empty(); }

		/** A triangle facing along Normal (wound so, with every corner the same normal) */
		void AddTriangle(const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& Normal, const Vec2f& UVA, const Vec2f& UVB, const Vec2f& UVC)
		{
			const int32_t Base = static_cast<int32_t>(Positions.size());
			// a triangle is front-facing when (B - A) x (C - A) points away from the viewer, so orient against the normal
			const bool bFlip = Dot(Cross(B - A, C - A), Normal) > 0.0f;
			Positions.insert(Positions.end(), { A, B, C });
			UVs.insert(UVs.end(), { UVA, UVB, UVC });
			Normals.insert(Normals.end(), { Normal, Normal, Normal });
			if (!Colors.empty())
			{
				Colors.insert(Colors.end(), { Vec4f{ 1, 1, 1, 1 }, Vec4f{ 1, 1, 1, 1 }, Vec4f{ 1, 1, 1, 1 } });
			}
			Indices.insert(Indices.end(), bFlip ? std::initializer_list<int32_t>{ Base, Base + 2, Base + 1 } : std::initializer_list<int32_t>{ Base, Base + 1, Base + 2 });
			Tags.push_back(Tag);
		}

		/** A triangle with its own normal and colour at each corner (ground) */
		void AddTriangle(const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& NA, const Vec3f& NB, const Vec3f& NC,
			const Vec4f& CA, const Vec4f& CB, const Vec4f& CC)
		{
			const int32_t Base = static_cast<int32_t>(Positions.size());
			const Vec3f FaceNormal = NA + NB + NC;
			const bool bFlip = Dot(Cross(B - A, C - A), FaceNormal) > 0.0f;
			Positions.insert(Positions.end(), { A, B, C });
			UVs.insert(UVs.end(), { Vec2f{ A.X / 400.0f, A.Y / 400.0f }, Vec2f{ B.X / 400.0f, B.Y / 400.0f }, Vec2f{ C.X / 400.0f, C.Y / 400.0f } });
			Normals.insert(Normals.end(), { NA, NB, NC });
			Colors.insert(Colors.end(), { CA, CB, CC });
			Indices.insert(Indices.end(), bFlip ? std::initializer_list<int32_t>{ Base, Base + 2, Base + 1 } : std::initializer_list<int32_t>{ Base, Base + 1, Base + 2 });
			Tags.push_back(Tag);
		}
	};

	/** The signed area of a polygon (cm2): positive when it runs counter-clockwise (x right, y up) */
	double SignedArea(const std::vector<Vec2>& Polygon);

	/** Ear-clipping triangulation of a simple counter-clockwise polygon (indices into it) */
	void Triangulate(const std::vector<Vec2>& Polygon, std::vector<int32_t>& OutIndices);
}
