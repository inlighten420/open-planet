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
			// (it sits Sink below the raster so the built ground is on top where they meet, but land is never sunk under the
			// water: in flat country like a river delta most of it is a few metres high, and 15 m down it was all sea)
			// how much to sink it: by how rough the ground round it is (a 120 m raster misses a finer ground's valleys, which would
			// otherwise be filled by this), 4.5 m on flat land (the distant blocks are 3 m down) to the full Sink on mountains
			float Low = Height, High = Height;
			for (int32_t DX = -2; DX <= 2; ++DX)
			{
				for (int32_t DY = -2; DY <= 2; ++DY)
				{
					const float Near = H(X + DX, Y + DY);
					Low = std::min(Low, Near);
					High = std::max(High, Near);
				}
			}
			const float Sunk = std::clamp((High - Low) + 450.0f, 450.0f, Options.Sink);
			const float Down = std::min(Sunk, std::max(Height - 30.0f, 0.0f));
			Out.Positions.push_back(Vec3f(float(P.X), float(P.Y), Height - Down));
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
				// the shore runs through this square (the land crosses sea level in it): cut finer, the raster sampled at each new corner,
				// so the coast is a curve at the raster's own detail and not a chain of straight 300 m lines
				const float LowH = std::min(std::min(H(X, Y), H(X + 1, Y)), std::min(H(X, Y + 1), H(X + 1, Y + 1)));
				const float HighH = std::max(std::max(H(X, Y), H(X + 1, Y)), std::max(H(X, Y + 1), H(X + 1, Y + 1)));
				if (LowH < 0.0f && HighH > 0.0f)
				{
					constexpr int32_t Sub = 6;
					const double Step = S / Sub;
					float Relief = 0.0f;
					{
						float Low = H(X, Y), High = H(X, Y);
						for (int32_t DX = -2; DX <= 3; ++DX)
						{
							for (int32_t DY = -2; DY <= 3; ++DY)
							{
								Low = std::min(Low, H(X + DX, Y + DY));
								High = std::max(High, H(X + DX, Y + DY));
							}
						}
						Relief = High - Low;
					}
					const float Sunk = std::clamp(Relief + 450.0f, 450.0f, Options.Sink);
					std::vector<float> Fine(size_t(Sub + 1) * size_t(Sub + 1));
					auto F = [&](int32_t I, int32_t J) -> float& { return Fine[size_t(I) * size_t(Sub + 1) + size_t(J)]; };
					auto FineAt = [&](int32_t I, int32_t J) { return Options.Centre + Vec2((X - Half) * S + I * Step, (Y - Half) * S + J * Step); };
					for (int32_t I = 0; I <= Sub; ++I)
					{
						for (int32_t J = 0; J <= Sub; ++J)
						{
							double Lat = 0.0, Lon = 0.0;
							Projection.ToLatLon(FineAt(I, J), Lat, Lon);
							F(I, J) = Raster.Sample(Lat, Lon) * 100.0f;
						}
					}
					auto FineVertex = [&](int32_t I, int32_t J)
					{
						const float Height = F(I, J);
						const int32_t I0 = std::max(I - 1, 0), I1 = std::min(I + 1, Sub), J0 = std::max(J - 1, 0), J1 = std::min(J + 1, Sub);
						const Vec3f Normal = Vec3f(-(F(I1, J) - F(I0, J)) / float(I1 - I0), -(F(I, J1) - F(I, J0)) / float(J1 - J0), static_cast<float>(Step)).Normal();
						Vec4f Colour = GrassColor;
						if (Height < 0.0f)
						{
							Colour = SeabedColor;
						}
						else if (Height < 300.0f)
						{
							Colour = Lerp(SandColor, GrassColor, Height / 300.0f);
						}
						Colour = Lerp(Colour, RockColor, std::clamp((0.86f - Normal.Z) / 0.1f, 0.0f, 1.0f));
						const Vec2 P = FineAt(I, J) - Options.Centre;
						const float Down = std::min(Sunk, std::max(Height - 30.0f, 0.0f));
						Out.Positions.push_back(Vec3f(float(P.X), float(P.Y), Height - Down));
						Out.Normals.push_back(Normal);
						Out.UVs.push_back(Vec2f());
						Out.Colors.push_back(Colour);
						return int32_t(Out.Positions.size()) - 1;
					};
					for (int32_t I = 0; I < Sub; ++I)
					{
						for (int32_t J = 0; J < Sub; ++J)
						{
							const int32_t A0 = FineVertex(I, J), B0 = FineVertex(I + 1, J), C0 = FineVertex(I + 1, J + 1), D0 = FineVertex(I, J + 1);
							Out.Indices.insert(Out.Indices.end(), { A0, D0, C0, A0, C0, B0 });
						}
					}
					continue;
				}
				const int32_t A = Vertex(X, Y), B = Vertex(X + 1, Y), C = Vertex(X + 1, Y + 1), D = Vertex(X, Y + 1);
				// facing up in the drive's frame (x north, y east, z up): counter-clockwise seen from above
				Out.Indices.insert(Out.Indices.end(), { A, D, C, A, C, B });
			}
		}
	}
}
