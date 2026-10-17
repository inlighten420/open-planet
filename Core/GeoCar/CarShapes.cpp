// Geodrive Core - GPL (see Core/README.md)

#include "CarBody.h"

#include <algorithm>
#include <cmath>

namespace geocar
{
	namespace CarShapesImpl
	{
		constexpr double Pi = 3.1415926535897932384626433832795;
		constexpr double HalfPi = 1.5707963267948966192313216916398;

		float DegreesToRadians(float Degrees) { return Degrees * static_cast<float>(Pi / 180.0); }
		float Sign(float V) { return V > 0.0f ? 1.0f : (V < 0.0f ? -1.0f : 0.0f); }
		float Lerp(float A, float B, float T) { return A + T * (B - A); }

		/** The game's safe normal: unchanged if already unit length, zero if tiny */
		Vec3 SafeNormal(const Vec3& V)
		{
			const double S = V.X * V.X + V.Y * V.Y + V.Z * V.Z;
			if (S == 1.0)
			{
				return V;
			}
			if (S < 1.0e-8)
			{
				return Vec3();
			}
			return V * (1.0 / std::sqrt(S));
		}

		Vec2d SafeNormal(const Vec2d& V)
		{
			const double S = V.X * V.X + V.Y * V.Y;
			if (S == 1.0)
			{
				return V;
			}
			if (S < 1.0e-8)
			{
				return Vec2d();
			}
			const double Scale = 1.0 / std::sqrt(S);
			return { V.X * Scale, V.Y * Scale };
		}

		Vec3 Mul(const Vec3& A, const Vec3& B) { return { A.X * B.X, A.Y * B.Y, A.Z * B.Z }; }
		Vec3 Div(const Vec3& A, const Vec3& B) { return { A.X / B.X, A.Y / B.Y, A.Z / B.Z }; }

		/** Turns a vector by a rotator (the game's rotation matrix) */
		Vec3 Rotate(const Rotator& R, const Vec3& V)
		{
			const double P = DegreesToRadians(R.Pitch), Y = DegreesToRadians(R.Yaw), Ro = DegreesToRadians(R.Roll);
			const double SP = std::sin(P), CP = std::cos(P), SY = std::sin(Y), CY = std::cos(Y), SR = std::sin(Ro), CR = std::cos(Ro);
			const Vec3 AxisX(CP * CY, CP * SY, SP);
			const Vec3 AxisY(SR * SP * CY - CR * SY, SR * SP * SY + CR * CY, -SR * CP);
			const Vec3 AxisZ(-(CR * SP * CY + SR * SY), CY * SR - CR * SP * SY, CR * CP);
			return AxisX * V.X + AxisY * V.Y + AxisZ * V.Z;
		}

		/** A quad from four corners with their own normals, all one colour, facing along the average normal */
		void Quad(CarMesh& Mesh, const Vec3 P[4], const Vec3 N[4], const Color& C)
		{
			const int32_t Base = static_cast<int32_t>(Mesh.Positions.size());
			for (int32_t i = 0; i < 4; ++i)
			{
				Mesh.AddVertex(P[i], N[i], C);
			}
			const Vec3 Facing = N[0] + N[1] + N[2] + N[3];
			Mesh.AddTriangle(Base, Base + 1, Base + 2, Facing);
			Mesh.AddTriangle(Base, Base + 2, Base + 3, Facing);
		}

		/** A flat polygon (convex, corners in order) facing Normal */
		void Fan(CarMesh& Mesh, const std::vector<Vec3>& Corners, const Vec3& Normal, const Color& C)
		{
			if (Corners.size() < 3)
			{
				return;
			}
			Vec3 Centre;
			for (const Vec3& Corner : Corners)
			{
				Centre = Centre + Corner;
			}
			Centre = Centre * (1.0 / static_cast<double>(Corners.size()));
			const int32_t Base = static_cast<int32_t>(Mesh.Positions.size());
			Mesh.AddVertex(Centre, Normal, C);
			for (const Vec3& Corner : Corners)
			{
				Mesh.AddVertex(Corner, Normal, C);
			}
			const int32_t Num = static_cast<int32_t>(Corners.size());
			for (int32_t i = 0; i < Num; ++i)
			{
				Mesh.AddTriangle(Base, Base + 1 + i, Base + 1 + (i + 1) % Num, Normal);
			}
		}

		float CatmullRom(float P0, float P1, float P2, float P3, float T)
		{
			const float T2 = T * T, T3 = T2 * T;
			return 0.5f * (2.0f * P1 + (P2 - P0) * T + (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * T2 + (3.0f * P1 - P0 - 3.0f * P2 + P3) * T3);
		}

		LoftSection Interpolate(const std::vector<LoftSection>& S, int32_t i, float T)
		{
			const int32_t Last = static_cast<int32_t>(S.size()) - 1;
			const LoftSection& A = S[std::max(i - 1, 0)];
			const LoftSection& B = S[i];
			const LoftSection& C = S[std::min(i + 1, Last)];
			const LoftSection& D = S[std::min(i + 2, Last)];
			LoftSection Out;
			Out.X = Lerp(B.X, C.X, T);	// evenly along the car, so the steps don't bunch up
			Out.HalfWidth = std::max(0.5f, CatmullRom(A.HalfWidth, B.HalfWidth, C.HalfWidth, D.HalfWidth, T));
			Out.Bottom = CatmullRom(A.Bottom, B.Bottom, C.Bottom, D.Bottom, T);
			Out.Top = std::max(Out.Bottom + 0.5f, CatmullRom(A.Top, B.Top, C.Top, D.Top, T));
			Out.TopWidth = std::min(std::max(CatmullRom(A.TopWidth, B.TopWidth, C.TopWidth, D.TopWidth, T), 0.05f), 1.2f);
			Out.Roundness = std::max(1.5f, CatmullRom(A.Roundness, B.Roundness, C.Roundness, D.Roundness, T));
			Out.CentreY = CatmullRom(A.CentreY, B.CentreY, C.CentreY, D.CentreY, T);
			return Out;
		}

		/** A point on a section's outline; Angle 0 is the right side, going up over the top */
		Vec3 RingPoint(const LoftSection& S, float Angle)
		{
			const float Exponent = 2.0f / S.Roundness;
			const float Cos = std::cos(Angle), Sin = std::sin(Angle);
			const float U = Sign(Cos) * std::pow(std::fabs(Cos), Exponent);
			const float V = Sign(Sin) * std::pow(std::fabs(Sin), Exponent);
			const float Centre = (S.Bottom + S.Top) * 0.5f, Half = (S.Top - S.Bottom) * 0.5f;
			const float Narrowing = V > 0.0f ? Lerp(1.0f, S.TopWidth, V) : Lerp(1.0f, 0.9f, -V);
			return Vec3(S.X, S.CentreY + U * S.HalfWidth * Narrowing, Centre + V * Half);
		}

		/** Revolves a profile in (radius, y) around the Y axis. Each segment faces the side its normal (dy, -dr) points to. */
		void Revolve(CarMesh& Mesh, const std::vector<Vec2d>& Profile, const Color& C, int32_t Around)
		{
			for (size_t p = 0; p + 1 < Profile.size(); ++p)
			{
				const Vec2d A = Profile[p], B = Profile[p + 1];
				const Vec2d Normal2D = SafeNormal(Vec2d{ B.Y - A.Y, -(B.X - A.X) });
				for (int32_t k = 0; k < Around; ++k)
				{
					const float Phi0 = static_cast<float>(2.0f * Pi * k / Around), Phi1 = static_cast<float>(2.0f * Pi * (k + 1) / Around);
					auto At = [](const Vec2d& RY, float Phi) { return Vec3(RY.X * std::cos(Phi), RY.Y, RY.X * std::sin(Phi)); };
					auto NormalAt = [&](float Phi) { return Vec3(Normal2D.X * std::cos(Phi), Normal2D.Y, Normal2D.X * std::sin(Phi)); };
					const Vec3 P[4] = { At(A, Phi0), At(B, Phi0), At(B, Phi1), At(A, Phi1) };
					const Vec3 N[4] = { NormalAt(Phi0), NormalAt(Phi0), NormalAt(Phi1), NormalAt(Phi1) };
					Quad(Mesh, P, N, C);
				}
			}
		}
	}

	void CarMesh::AddVertex(const Vec3& Position, const Vec3& Normal, const Color& C)
	{
		Positions.push_back(Position);
		Normals.push_back(CarShapesImpl::SafeNormal(Normal));
		Colors.push_back(C);
	}

	void CarMesh::AddTriangle(int32_t A, int32_t B, int32_t C, const Vec3& Facing)
	{
		// corners clockwise seen from the front
		const Vec3 Normal = Cross(Positions[C] - Positions[A], Positions[B] - Positions[A]);
		if (Dot(Normal, Facing) >= 0.0)
		{
			Triangles.insert(Triangles.end(), { A, B, C });
		}
		else
		{
			Triangles.insert(Triangles.end(), { A, C, B });
		}
	}

	Color shapes::Paint(const Color& C) { return { C.R, C.G, C.B, 0.3f }; }
	Color shapes::Glass() { return { 0.035f, 0.05f, 0.05f, 0.05f }; }	// (a faint green-grey tint: the host draws it see-through)
	Color shapes::Rubber() { return { 0.025f, 0.025f, 0.025f, 0.9f }; }
	Color shapes::Carbon() { return { 0.02f, 0.02f, 0.022f, 0.45f }; }
	Color shapes::Metal(float Brightness) { return { Brightness, Brightness, Brightness * 1.02f, 0.25f }; }
	Color shapes::Light(const Color& C) { return { C.R, C.G, C.B, 0.0f }; }

	void shapes::Loft(CarMesh& Mesh, std::vector<LoftSection> Sections, int32_t Steps, int32_t Around,
		const std::function<Color(const Vec3&)>& ColorAt, const std::function<bool(const Vec3&)>& Keep)
	{
		using namespace CarShapesImpl;
		if (Sections.size() < 2)
		{
			return;
		}
		std::sort(Sections.begin(), Sections.end(), [](const LoftSection& A, const LoftSection& B) { return A.X > B.X; });

		// the sections smoothed along the car
		std::vector<LoftSection> Rings;
		for (int32_t i = 0; i + 1 < static_cast<int32_t>(Sections.size()); ++i)
		{
			for (int32_t Step = 0; Step < Steps; ++Step)
			{
				Rings.push_back(Interpolate(Sections, i, static_cast<float>(Step) / Steps));
			}
		}
		Rings.push_back(Sections.back());

		// the outline points, and smooth normals from the neighbours either way
		const int32_t NumRings = static_cast<int32_t>(Rings.size());
		std::vector<Vec3> Grid(size_t(NumRings) * size_t(Around));
		for (int32_t k = 0; k < NumRings; ++k)
		{
			for (int32_t j = 0; j < Around; ++j)
			{
				Grid[size_t(k) * Around + j] = RingPoint(Rings[k], static_cast<float>(2.0f * Pi * j / Around));
			}
		}
		auto P = [&](int32_t k, int32_t j) { return Grid[size_t(std::min(std::max(k, 0), NumRings - 1)) * Around + ((j % Around) + Around) % Around]; };
		auto Normal = [&](int32_t k, int32_t j)
		{
			const Vec3 Along = P(k + 1, j) - P(k - 1, j);
			const Vec3 Round = P(k, j + 1) - P(k, j - 1);
			Vec3 N = SafeNormal(Cross(Round, Along));
			const LoftSection& S = Rings[k];
			const Vec3 Centre(S.X, S.CentreY, (S.Bottom + S.Top) * 0.5f);
			if (Dot(N, P(k, j) - Centre) < 0.0)
			{
				N = -N;
			}
			const bool bZero = std::fabs(N.X) <= 1.0e-4 && std::fabs(N.Y) <= 1.0e-4 && std::fabs(N.Z) <= 1.0e-4;
			return bZero ? SafeNormal(P(k, j) - Centre) : N;
		};

		for (int32_t k = 0; k + 1 < NumRings; ++k)
		{
			for (int32_t j = 0; j < Around; ++j)
			{
				const Vec3 Corners[4] = { P(k, j), P(k + 1, j), P(k + 1, j + 1), P(k, j + 1) };
				const Vec3 Middle = (Corners[0] + Corners[1] + Corners[2] + Corners[3]) * 0.25;
				if (Keep && !Keep(Middle))
				{
					continue;
				}
				const Vec3 Normals[4] = { Normal(k, j), Normal(k + 1, j), Normal(k + 1, j + 1), Normal(k, j + 1) };
				Quad(Mesh, Corners, Normals, ColorAt(Middle));
			}
		}

		// the ends
		for (const bool bFront : { true, false })
		{
			const int32_t k = bFront ? 0 : NumRings - 1;
			std::vector<Vec3> Corners;
			for (int32_t j = 0; j < Around; ++j)
			{
				Corners.push_back(P(k, j));
			}
			const LoftSection& S = Rings[k];
			Fan(Mesh, Corners, Vec3(bFront ? 1.0 : -1.0, 0.0, 0.0), ColorAt(Vec3(S.X, S.CentreY, (S.Bottom + S.Top) * 0.5f)));
		}
	}

	void shapes::Wheel(CarMesh& Mesh, float Radius, float Width, float RimRadius, int32_t Spokes, const Color& RimColor)
	{
		using namespace CarShapesImpl;
		constexpr int32_t Around = 40;
		const float Half = Width * 0.5f;
		const float Shoulder = std::min(Width * 0.18f, (Radius - RimRadius) * 0.45f);

		// the tyre: inner sidewall up, round the shoulder, across the tread, round the other shoulder, outer sidewall down
		std::vector<Vec2d> Tyre;
		Tyre.push_back({ RimRadius, -Half * 0.9f });
		Tyre.push_back({ Radius - Shoulder, -Half });
		for (int32_t i = 1; i <= 3; ++i)
		{
			const float A = static_cast<float>(HalfPi * i / 4.0f);
			Tyre.push_back({ Radius - Shoulder + Shoulder * std::sin(A), -Half + Shoulder - Shoulder * std::cos(A) });
		}
		Tyre.push_back({ Radius, -Half + Shoulder });
		Tyre.push_back({ Radius, Half - Shoulder });
		for (int32_t i = 1; i <= 3; ++i)
		{
			const float A = static_cast<float>(HalfPi * i / 4.0f);
			Tyre.push_back({ Radius - Shoulder + Shoulder * std::cos(A), Half - Shoulder + Shoulder * std::sin(A) });
		}
		Tyre.push_back({ Radius - Shoulder, Half });
		Tyre.push_back({ RimRadius, Half * 0.9f });
		Revolve(Mesh, Tyre, Rubber(), Around);

		// the rim lip, then the dark brake and barrel seen between the spokes, then the inner side
		const float Face = Half * 0.8f;
		Revolve(Mesh, { { RimRadius, Half * 0.9f }, { RimRadius * 0.9f, Face } }, RimColor, Around);
		Revolve(Mesh, { { RimRadius * 0.9f, Face }, { RimRadius * 0.9f, Face * 0.2f } }, { 0.08f, 0.08f, 0.085f, 0.5f }, Around);
		Revolve(Mesh, { { RimRadius * 0.9f, Face * 0.2f }, { 0.0f, Face * 0.2f } }, { 0.05f, 0.05f, 0.05f, 0.6f }, Around);
		Revolve(Mesh, { { 0.0f, -Half * 0.85f }, { RimRadius, -Half * 0.85f } }, { 0.03f, 0.03f, 0.03f, 0.7f }, Around);

		// spokes from the hub to the lip, and the hub
		for (int32_t s = 0; s < Spokes; ++s)
		{
			const float Angle = 360.0f * s / Spokes;
			const float Middle = RimRadius * 0.55f;
			const Vec3 Centre(Middle * std::cos(DegreesToRadians(Angle)), Face * 0.85f, Middle * std::sin(DegreesToRadians(Angle)));
			Box(Mesh, Centre, Vec3(RimRadius * 0.38f, Face * 0.12f, RimRadius * 0.07f), RimColor, Rotator{ Angle, 0.0f, 0.0f });
		}
		Revolve(Mesh, { { RimRadius * 0.24f, Face * 0.7f }, { RimRadius * 0.24f, Face }, { 0.0f, Face } }, RimColor, 16);
	}

	void shapes::Box(CarMesh& Mesh, const Vec3& Centre, const Vec3& HalfSize, const Color& C, const Rotator& Rotation)
	{
		using namespace CarShapesImpl;
		auto Corner = [&](double X, double Y, double Z) { return Centre + Rotate(Rotation, Vec3(X * HalfSize.X, Y * HalfSize.Y, Z * HalfSize.Z)); };
		struct Face { Vec3 Normal; Vec3 U; Vec3 V; };
		const Face Faces[6] = {
			{ Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1) }, { Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1) },
			{ Vec3(0, 1, 0), Vec3(1, 0, 0), Vec3(0, 0, 1) }, { Vec3(0, -1, 0), Vec3(1, 0, 0), Vec3(0, 0, 1) },
			{ Vec3(0, 0, 1), Vec3(1, 0, 0), Vec3(0, 1, 0) }, { Vec3(0, 0, -1), Vec3(1, 0, 0), Vec3(0, 1, 0) } };
		for (const Face& F : Faces)
		{
			const Vec3 N = Rotate(Rotation, F.Normal);
			auto At = [&](double U, double V)
			{
				const Vec3 L = F.Normal + F.U * U + F.V * V;
				return Corner(L.X, L.Y, L.Z);
			};
			const Vec3 P[4] = { At(-1, -1), At(1, -1), At(1, 1), At(-1, 1) };
			const Vec3 Ns[4] = { N, N, N, N };
			Quad(Mesh, P, Ns, C);
		}
	}

	void shapes::Tube(CarMesh& Mesh, const std::vector<Vec3>& Path, float Radius, const Color& C, int32_t Around)
	{
		using namespace CarShapesImpl;
		if (Path.size() < 2)
		{
			return;
		}
		const int32_t Num = static_cast<int32_t>(Path.size());
		// rings carried along the path without twisting
		std::vector<Vec3> Sides, Ups;
		const Vec3 Forward = SafeNormal(Path[1] - Path[0]);
		Vec3 Side = SafeNormal(Cross(std::fabs(Forward.Z) < 0.9 ? Vec3(0, 0, 1) : Vec3(1, 0, 0), Forward));
		for (int32_t i = 0; i < Num; ++i)
		{
			const Vec3 Dir = SafeNormal(Path[std::min(i + 1, Num - 1)] - Path[std::max(i - 1, 0)]);
			Side = SafeNormal(Side - Dir * Dot(Side, Dir));
			Sides.push_back(Side);
			Ups.push_back(Cross(Dir, Side));
		}
		auto Ring = [&](int32_t i, int32_t j) { const float A = static_cast<float>(2.0f * Pi * j / Around); return Sides[i] * std::cos(A) + Ups[i] * std::sin(A); };
		for (int32_t i = 0; i + 1 < Num; ++i)
		{
			for (int32_t j = 0; j < Around; ++j)
			{
				const Vec3 N[4] = { Ring(i, j), Ring(i + 1, j), Ring(i + 1, j + 1), Ring(i, j + 1) };
				const Vec3 P[4] = { Path[i] + N[0] * Radius, Path[i + 1] + N[1] * Radius, Path[i + 1] + N[2] * Radius, Path[i] + N[3] * Radius };
				Quad(Mesh, P, N, C);
			}
		}
		for (const bool bStart : { true, false })
		{
			const int32_t i = bStart ? 0 : Num - 1;
			std::vector<Vec3> Corners;
			for (int32_t j = 0; j < Around; ++j)
			{
				Corners.push_back(Path[i] + Ring(i, j) * Radius);
			}
			const Vec3 Out = bStart ? SafeNormal(Path[0] - Path[1]) : SafeNormal(Path[Num - 1] - Path[Num - 2]);
			Fan(Mesh, Corners, Out, C);
		}
	}

	void shapes::Wing(CarMesh& Mesh, float LeadX, float LeadZ, float Chord, float Thickness, float Angle, float Y0, float Y1, const Color& C)
	{
		using namespace CarShapesImpl;
		// a NACA four-digit thickness shape, cambered for downforce (curved the other way up from an aircraft wing)
		constexpr int32_t Points = 12;
		std::vector<Vec2d> Outline;	// (along the chord backwards, up), round the upper side then back along the lower
		auto Shape = [&](float T, bool bUpper)
		{
			const float Half = 5.0f * Thickness * (0.2969f * std::sqrt(T) - 0.126f * T - 0.3516f * T * T + 0.2843f * T * T * T - 0.1036f * T * T * T * T);
			const float Camber = -0.06f * Chord * 4.0f * T * (1.0f - T);
			return Vec2d{ T * Chord, Camber + (bUpper ? Half : -Half) * Chord };
		};
		auto Square = [](float V) { return V * V; };
		for (int32_t i = 0; i <= Points; ++i)
		{
			Outline.push_back(Shape(Square(static_cast<float>(i) / Points), true));
		}
		for (int32_t i = Points - 1; i >= 1; --i)
		{
			Outline.push_back(Shape(Square(static_cast<float>(i) / Points), false));
		}
		const float Tilt = DegreesToRadians(Angle);
		auto Place = [&](const Vec2d& P, float Y)
		{
			// backwards along the car (-X), nose-down tilt raises the trailing edge
			const float X = static_cast<float>(-P.X * std::cos(Tilt) - P.Y * std::sin(Tilt));
			const float Z = static_cast<float>(P.Y * std::cos(Tilt) + P.X * std::sin(Tilt));
			return Vec3(LeadX + X, Y, LeadZ + Z);
		};
		const Vec2d Middle{ Chord * 0.4f, 0.0f };
		const int32_t Num = static_cast<int32_t>(Outline.size());
		for (int32_t i = 0; i < Num; ++i)
		{
			const Vec2d A = Outline[i], B = Outline[(i + 1) % Num];
			const Vec2d Out2D = SafeNormal(Vec2d{ (A.X + B.X) * 0.5f - Middle.X, (A.Y + B.Y) * 0.5f - Middle.Y });
			const Vec3 Out = SafeNormal(Place(Vec2d{ Middle.X + Out2D.X, Middle.Y + Out2D.Y }, 0.0f) - Place(Middle, 0.0f));
			const Vec3 P[4] = { Place(A, Y0), Place(B, Y0), Place(B, Y1), Place(A, Y1) };
			const Vec3 N[4] = { Out, Out, Out, Out };
			Quad(Mesh, P, N, C);
		}
		for (const float Y : { Y0, Y1 })
		{
			std::vector<Vec3> Corners;
			for (const Vec2d& P : Outline)
			{
				Corners.push_back(Place(P, Y));
			}
			Fan(Mesh, Corners, Vec3(0.0, Y == Y0 ? Sign(Y0 - Y1) : Sign(Y1 - Y0), 0.0), C);
		}
	}

	void shapes::Sphere(CarMesh& Mesh, const Vec3& Centre, const Vec3& Radii, const Color& C, int32_t Segments)
	{
		using namespace CarShapesImpl;
		const int32_t Rings = Segments / 2;
		auto Dir = [&](int32_t r, int32_t s)
		{
			const float Lat = static_cast<float>(Pi * r / Rings - HalfPi), Lon = static_cast<float>(2.0f * Pi * s / Segments);
			return Vec3(std::cos(Lat) * std::cos(Lon), std::cos(Lat) * std::sin(Lon), std::sin(Lat));
		};
		for (int32_t r = 0; r < Rings; ++r)
		{
			for (int32_t s = 0; s < Segments; ++s)
			{
				const Vec3 D[4] = { Dir(r, s), Dir(r + 1, s), Dir(r + 1, s + 1), Dir(r, s + 1) };
				const Vec3 P[4] = { Centre + Mul(D[0], Radii), Centre + Mul(D[1], Radii), Centre + Mul(D[2], Radii), Centre + Mul(D[3], Radii) };
				const Vec3 N[4] = { SafeNormal(Div(D[0], Radii)), SafeNormal(Div(D[1], Radii)), SafeNormal(Div(D[2], Radii)), SafeNormal(Div(D[3], Radii)) };
				Quad(Mesh, P, N, C);
			}
		}
	}

	void shapes::ArchLiner(CarMesh& Mesh, const Vec3& Centre, float Radius, float Y0, float Y1, float StartDegrees, float EndDegrees, const Color& C)
	{
		using namespace CarShapesImpl;
		constexpr int32_t Steps = 16;
		for (int32_t i = 0; i < Steps; ++i)
		{
			const float A0 = DegreesToRadians(Lerp(StartDegrees, EndDegrees, static_cast<float>(i) / Steps));
			const float A1 = DegreesToRadians(Lerp(StartDegrees, EndDegrees, static_cast<float>(i + 1) / Steps));
			const Vec3 D0(std::cos(A0), 0.0, std::sin(A0)), D1(std::cos(A1), 0.0, std::sin(A1));
			const Vec3 P[4] = { Centre + D0 * Radius + Vec3(0, Y0, 0), Centre + D1 * Radius + Vec3(0, Y0, 0),
				Centre + D1 * Radius + Vec3(0, Y1, 0), Centre + D0 * Radius + Vec3(0, Y1, 0) };
			const Vec3 N[4] = { -D0, -D1, -D1, -D0 };
			Quad(Mesh, P, N, C);
		}
	}
}
