// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "Horizon.h"

#include <algorithm>
#include <cmath>

namespace geoground
{
	void BuildHorizon(const HeightRaster& Raster, const georoads::Projection& Projection, const HorizonOptions& Options, MeshData& Out)
	{
		// the near ground's colours (Terrain.cpp), so the two meet without a seam of colour
		const Vec4f GrassColor{ 0.020f, 0.045f, 0.008f, 1.0f };
		const Vec4f SandColor{ 0.30f, 0.23f, 0.11f, 1.0f };
		const Vec4f RockColor{ 0.045f, 0.040f, 0.033f, 1.0f };
		const Vec4f SeabedColor{ 0.10f, 0.08f, 0.04f, 1.0f };

		const int32_t Half = int32_t(std::ceil(Options.Radius / Options.Spacing));
		const int32_t Side = Half * 2 + 1;
		const double S = Options.Spacing;
		std::vector<float> Heights(size_t(Side) * size_t(Side), 0.0f);
		auto At = [&](int32_t X, int32_t Y) { return Options.Centre + Vec2((X - Half) * S, (Y - Half) * S); };
		for (int32_t X = 0; X < Side; ++X)
		{
			for (int32_t Y = 0; Y < Side; ++Y)
			{
				double Lat = 0.0, Lon = 0.0;
				Projection.ToLatLon(At(X, Y), Lat, Lon);
				Heights[size_t(X) * size_t(Side) + size_t(Y)] = Raster.Sample(Lat, Lon) * 100.0f;
			}
		}
		auto H = [&](int32_t X, int32_t Y) { return Heights[size_t(std::clamp(X, 0, Side - 1)) * size_t(Side) + size_t(std::clamp(Y, 0, Side - 1))]; };

		// one vertex per grid point (shared by its quads), its normal from its neighbours
		std::vector<int32_t> Index(size_t(Side) * size_t(Side), -1);
		auto Vertex = [&](int32_t X, int32_t Y)
		{
			int32_t& Known = Index[size_t(X) * size_t(Side) + size_t(Y)];
			if (Known >= 0)
			{
				return Known;
			}
			const float Height = H(X, Y);
			const Vec3f Normal = Vec3f(float(H(X - 1, Y) - H(X + 1, Y)), float(H(X, Y - 1) - H(X, Y + 1)), float(2.0 * S)).Normal();
			Vec4f Colour = GrassColor;
			if (Height < 0.0f)
			{
				Colour = SeabedColor;
			}
			else if (Height < 300.0f)
			{
				Colour = Lerp(SandColor, GrassColor, Height / 300.0f);	// sand along the shoreline
			}
			Colour = Lerp(Colour, RockColor, std::clamp((0.86f - Normal.Z) / 0.1f, 0.0f, 1.0f));	// rock on steep slopes
			const Vec2 P = At(X, Y) - Options.Centre;
			Out.Positions.push_back(Vec3f(float(P.X), float(P.Y), Height - Options.Sink));
			Out.Normals.push_back(Normal);
			Out.UVs.push_back(Vec2f());
			Out.Colors.push_back(Colour);
			Known = int32_t(Out.Positions.size()) - 1;
			return Known;
		};
		const double Reach2 = Options.Radius * Options.Radius;
		for (int32_t X = 0; X + 1 < Side; ++X)
		{
			for (int32_t Y = 0; Y + 1 < Side; ++Y)
			{
				// a round horizon, and nothing where the built ground is
				const Vec2 Middle = At(X, Y) + Vec2(S * 0.5, S * 0.5);
				if ((Middle - Options.Centre).SizeSquared() > Reach2)
				{
					continue;
				}
				bool bBuilt = false;
				for (const Box2& Hole : Options.Holes)
				{
					bBuilt = bBuilt || (Hole.IsInside(At(X, Y)) && Hole.IsInside(At(X + 1, Y)) && Hole.IsInside(At(X, Y + 1)) && Hole.IsInside(At(X + 1, Y + 1)));
				}
				if (bBuilt)
				{
					continue;
				}
				const int32_t A = Vertex(X, Y), B = Vertex(X + 1, Y), C = Vertex(X + 1, Y + 1), D = Vertex(X, Y + 1);
				// facing up in the drive's frame (x north, y east, z up): counter-clockwise seen from above
				Out.Indices.insert(Out.Indices.end(), { A, D, C, A, C, B });
			}
		}
	}
}
