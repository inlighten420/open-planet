// Geodrive Core - GPL (see Core/README.md)

#include "Mesh.h"

namespace geoground
{
	namespace
	{
		double Cross2(const Vec2& A, const Vec2& B) { return A.X * B.Y - A.Y * B.X; }
	}

	double SignedArea(const std::vector<Vec2>& Polygon)
	{
		double Area = 0.0;
		for (size_t i = 0; i < Polygon.size(); ++i)
		{
			Area += Cross2(Polygon[i], Polygon[(i + 1) % Polygon.size()]);
		}
		return Area * 0.5;
	}

	void Triangulate(const std::vector<Vec2>& Polygon, std::vector<int32_t>& OutIndices)
	{
		std::vector<int32_t> Remaining;
		for (int32_t i = 0; i < static_cast<int32_t>(Polygon.size()); ++i)
		{
			Remaining.push_back(i);
		}

		int64_t Guard = int64_t(Polygon.size()) * int64_t(Polygon.size());
		while (Remaining.size() > 3 && Guard-- > 0)
		{
			bool bClipped = false;
			const int32_t Count = static_cast<int32_t>(Remaining.size());
			for (int32_t i = 0; i < Count; ++i)
			{
				const int32_t Prev = Remaining[(i + Count - 1) % Count];
				const int32_t Curr = Remaining[i];
				const int32_t Next = Remaining[(i + 1) % Count];
				const Vec2& A = Polygon[Prev];
				const Vec2& B = Polygon[Curr];
				const Vec2& C = Polygon[Next];

				if (Cross2(B - A, C - B) <= 0.0)
				{
					continue;	// reflex corner
				}

				bool bContainsPoint = false;
				for (const int32_t Other : Remaining)
				{
					if (Other == Prev || Other == Curr || Other == Next)
					{
						continue;
					}
					const Vec2& P = Polygon[Other];
					if (Cross2(B - A, P - A) >= 0.0 && Cross2(C - B, P - B) >= 0.0 && Cross2(A - C, P - C) >= 0.0)
					{
						bContainsPoint = true;
						break;
					}
				}
				if (bContainsPoint)
				{
					continue;
				}

				OutIndices.insert(OutIndices.end(), { Prev, Curr, Next });
				Remaining.erase(Remaining.begin() + i);
				bClipped = true;
				break;
			}
			if (!bClipped)
			{
				break;
			}
		}

		// whatever is left (normally one triangle) is fanned
		for (size_t i = 1; i + 1 < Remaining.size(); ++i)
		{
			OutIndices.insert(OutIndices.end(), { Remaining[0], Remaining[i], Remaining[i + 1] });
		}
	}
}
