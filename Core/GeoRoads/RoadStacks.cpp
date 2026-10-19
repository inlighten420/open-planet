// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadStacks.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace georoads
{
	namespace
	{
		// --- the knobs ---

		/** How hard each kind of point holds to where it would be on its own (for a point with 5 m of road), so what
		 *  gives way first is what holds least: a tunnel or a bridge deck, then the road leading in (or a street under a
		 *  deck, or over a tunnel), then a junction in the open. Beyond the next junction out nothing moves at all. */
		constexpr double TunnelWeight = 1.0;
		constexpr double SpanWeight = 1.0;
		constexpr double OpenWeight = 10.0;
		constexpr double JunctionWeight = 100.0;
		/** A road on the ground measured by LiDAR (a junction or the road leading in): what it does is a fact, its cutting
		 *  is in the measurement; the spans fit round it */
		constexpr double MeasuredWeight = 1.0e5;
		constexpr double WeightSpacing = 500.0;	// (cm of road the weights above are for)

		/** The price of leaving a rule short, so every problem has an answer and what is left short is measured, not
		 *  guessed: clearance and the sea at ShortWeight (dearer than a tunnel being steeper than its class, which it may be up to AbsurdGrade), a tunnel's
		 *  own grade and curves at SteepWeight, and the rules a road on the ground already obeys so dear it never happens */
		constexpr double ShortWeight = 1.0e7;

		/** The least one road may stand over another (cm): a truck, a little air and the thinnest deck (the mesh makes a deck
		 *  only as thick as the room allows); BridgeClearance is what a road in the open has, wanted but not worth much */
		constexpr double MinClearance = 500.0;

		constexpr double PreferWeight = 1.0e3;
		constexpr double TieWeight = 1.0e4;
		constexpr double SteepWeight = 3.0e4;
		constexpr double HardWeight = 1.0e7;
		/** Grade and curve rules left broken by more than this (cm) after a solve are made dearer and it is solved again: by this factor each
		 *  time, up to the cap (under the clearance rules), for this many passes */
		constexpr double EscalateShort = 20.0, EscalateBy = 8.0, EscalateCap = 2.0e6;
		constexpr int32_t EscalatePasses = 3;

		/** A span (a tunnel, a bridge, a ramp to another level) may climb at least this steeply, whatever its class (a ramp
		 *  between levels in a town is steeper than a road in the open), and never more than AbsurdGrade: a car is thrown
		 *  off anything steeper, so a clearance left short is better than that */
		constexpr double SpanGrade = 0.12;
		constexpr double AbsurdGrade = 0.20;

		/** A tunnel is built gentler than a road in the open: a real one keeps to this (rise over run), whatever its class (6%
		 *  scored best on Monaco: 167 pieces too steep for their class, against 284 at 12% and 180 at 8%; nothing else moved) */
		constexpr double TunnelGrade = 0.06;

		/** A measured road leaves its measurement only the way a structure would hide: the road leading on to a bridge may
		 *  rise (the height data shows the ground under a structure, not the structure); the road leading into a tunnel,
		 *  or a street under a bridge, may sink (the data can't see a road under a cover). Never the other way by more
		 *  than this (cm) */
		constexpr double BeyondMeasured = 20.0;
		constexpr uint8_t Lifts = 1, Sinks = 2;

		/** A tunnel away from its mouths would rather run this far under the ground (cm: its tube and a cover of earth),
		 *  coming up to its mouth over PortalZone */
		constexpr double TunnelCoverDepth = 850.0;
		constexpr double PortalZone = 4000.0;

		/** How far the road leading in to a stack is walked out to the next junction (cm) */
		constexpr double ApproachReach = 20000.0;

		/** A road on the ground is never asked to be gentler than it already is: it is trusted there */
		constexpr double KeepGrade = 1.02;
		constexpr double KeepCurve = 1.05;

		/** The solve (ADMM, see SolveStack) runs until the heights and every rule's copy of them agree within Settled (cm),
		 *  or AdmmCap rounds; the price of disagreeing starts at InitialRho; each round's linear solve by conjugate gradients
		 *  to CgTolerance (relative), at most CgCap iterations */
		constexpr int32_t AdmmCap = 1000;

		/** The stacks' whole solve stops by then (s, from its start): a city's (Honolulu's 62 stacks, 480 000 rules) took
		 *  over 100 s to settle every last one; what isn't settled by then keeps the best it had. Re-solving with dearer
		 *  rules (the escalation) only starts while time is left (Monaco's take a second each; a city's first solve uses it all) */
		constexpr double StackBudget = 4.0;
		constexpr double Settled = 0.1;
		constexpr double InitialRho = 1000.0;
		constexpr double OverRelax = 1.6;
		constexpr int32_t CgCap = 300;
		constexpr double CgTolerance = 1.0e-8;

		/** A clearance short by less than this (cm) is not reported */
		constexpr double ShortTolerance = 10.0;

		double Seconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		int32_t FloorToInt(double X) { return static_cast<int32_t>(std::floor(X)); }
		uint64_t CellKey(int32_t X, int32_t Y) { return (static_cast<uint64_t>(static_cast<uint32_t>(X)) << 32) | static_cast<uint32_t>(Y); }
		double Lerp(double A, double B, double T) { return A + T * (B - A); }

		/** Coefficients of the change of slope (curvature, 1/cm) at the middle of three points */
		void BendCoefficients(double LengthAB, double LengthBC, double (&Out)[3])
		{
			const double Span = 0.5 * (LengthAB + LengthBC);
			Out[0] = 1.0 / (LengthAB * Span);
			Out[1] = -(1.0 / LengthAB + 1.0 / LengthBC) / Span;
			Out[2] = 1.0 / (LengthBC * Span);
		}

		/** What a point is, least movable first */
		enum class Kind : uint8_t { Outside, Fixed, Junction, Open, Span, Tunnel };

		double WeightOf(Kind K)
		{
			switch (K)
			{
			case Kind::Junction: return JunctionWeight;
			case Kind::Open: return OpenWeight;
			case Kind::Span: return SpanWeight;
			case Kind::Tunnel: return TunnelWeight;
			default: return 0.0;
			}
		}

		/** One rule: Sum(Coefficient * X[Index]) >= Bound; what it is short costs Weight per cm² */
		struct Row
		{
			int32_t Index[4] = { -1, -1, -1, -1 };
			double Coefficient[4] = { 0.0, 0.0, 0.0, 0.0 };
			int32_t Size = 0;
			double Bound = 0.0;
			double Weight = 0.0;
			uint8_t What = 0;	// (for the trace: 0 grade, 1 curve, 2 clearance, 3 sea, 4 tie)
		};
		const char* const WhatNames[6] = { "grade", "curve", "clearance", "sea", "tie", "ground" };

		struct Stack
		{
			std::vector<int32_t> Edges;
			std::vector<Row> Rows;
			int32_t Points = 0;
			int32_t Rounds = 0;
			int32_t Iterations = 0;
			bool bSettled = false;
			std::string Trace;
		};

		using Entry = std::tuple<int32_t, int32_t, double>;

		/** A symmetric sparse matrix in compressed rows: its pattern built once from (row, column) entries, its values filled
		 *  as often as needed */
		struct Sparse
		{
			std::vector<int32_t> RowStart;
			std::vector<int32_t> Columns;
			std::vector<double> Values;

			void Build(int32_t NumRows, std::vector<Entry>& Entries)
			{
				std::sort(Entries.begin(), Entries.end(), [](const Entry& A, const Entry& B)
				{
					return std::get<0>(A) != std::get<0>(B) ? std::get<0>(A) < std::get<0>(B) : std::get<1>(A) < std::get<1>(B);
				});
				RowStart.assign(NumRows + 1, 0);
				Columns.clear();
				Values.clear();
				int32_t PreviousRow = -1, PreviousColumn = -1;
				for (const Entry& E : Entries)
				{
					if (std::get<0>(E) == PreviousRow && std::get<1>(E) == PreviousColumn)
					{
						Values.back() += std::get<2>(E);
						continue;
					}
					Columns.push_back(std::get<1>(E));
					Values.push_back(std::get<2>(E));
					++RowStart[std::get<0>(E) + 1];
					PreviousRow = std::get<0>(E);
					PreviousColumn = std::get<1>(E);
				}
				for (int32_t Row = 0; Row < NumRows; ++Row)
				{
					RowStart[Row + 1] += RowStart[Row];
				}
			}

			/** Where (Row, Column) is kept, or -1 */
			int32_t Slot(int32_t Row, int32_t Column) const
			{
				const int32_t* Begin = Columns.data() + RowStart[Row];
				const int32_t* End = Columns.data() + RowStart[Row + 1];
				const int32_t* Found = std::lower_bound(Begin, End, Column);
				return Found != End && *Found == Column ? static_cast<int32_t>(Found - Columns.data()) : -1;
			}

			double At(int32_t Row, int32_t Column) const
			{
				const int32_t S = Slot(Row, Column);
				return S >= 0 ? Values[S] : 0.0;
			}

			void Multiply(const std::vector<double>& X, std::vector<double>& Y) const
			{
				const int32_t NumRows = static_cast<int32_t>(RowStart.size()) - 1;
				for (int32_t Row = 0; Row < NumRows; ++Row)
				{
					double Sum = 0.0;
					for (int32_t i = RowStart[Row]; i < RowStart[Row + 1]; ++i)
					{
						Sum += Values[i] * X[Columns[i]];
					}
					Y[Row] = Sum;
				}
			}
		};

		double DotProduct(const std::vector<double>& A, const std::vector<double>& B)
		{
			double Sum = 0.0;
			for (size_t i = 0; i < A.size(); ++i)
			{
				Sum += A[i] * B[i];
			}
			return Sum;
		}

		/**
		 *  Solves one stack: the heights nearest the targets (in the weights) with every rule's shortfall costing its weight.
		 *  The rules short at the moment are the active set; with that set the problem is one linear system, solved by
		 *  conjugate gradients preconditioned by each road's own band (its grade and curve rules link each point only to
		 *  its two neighbours either side, so a road on its own is solved exactly and the iterations only have to settle how
		 *  the roads connect); then the set is looked at again, round by round, until it stops changing.
		 */
		void SolveStack(Stack& S, std::vector<double>& X, const std::vector<double>& InvW, const std::vector<int32_t>& InnerK, bool bTrace, double Deadline)
		{
			// the stack's points that can move, in the solver's order (a road's inner points consecutive: its band)
			std::vector<int32_t> Global;
			for (const Row& R : S.Rows)
			{
				for (int32_t i = 0; i < R.Size; ++i)
				{
					if (InvW[R.Index[i]] > 0.0)
					{
						Global.push_back(R.Index[i]);
					}
				}
			}
			std::sort(Global.begin(), Global.end());
			Global.erase(std::unique(Global.begin(), Global.end()), Global.end());
			const int32_t N = static_cast<int32_t>(Global.size());
			S.Points = N;
			if (N == 0)
			{
				S.bSettled = true;
				return;
			}
			std::unordered_map<int32_t, int32_t> Local;
			for (int32_t j = 0; j < N; ++j)
			{
				Local[Global[j]] = j;
			}
			// how many of the points before it in the order are its own road's points before it (0, 1 or 2: the band)
			std::vector<int32_t> Band(N, 0);
			for (int32_t j = 0; j < N; ++j)
			{
				const int32_t g = Global[j];
				if (InnerK[g] >= 1 && j >= 1 && Global[j - 1] == g - 1)
				{
					Band[j] = InnerK[g] >= 2 && j >= 2 && Global[j - 2] == g - 2 ? 2 : 1;
				}
			}
			std::vector<double> W(N), T(N), Xl(N);
			for (int32_t j = 0; j < N; ++j)
			{
				W[j] = 1.0 / InvW[Global[j]];
				T[j] = Xl[j] = X[Global[j]];
			}

			// the matrix's pattern: every rule's points against each other, and the diagonal
			const int32_t NumRows = static_cast<int32_t>(S.Rows.size());
			std::vector<int32_t> RowLocal(NumRows * 4, -1);
			std::vector<Entry> Entries;
			for (int32_t j = 0; j < N; ++j)
			{
				Entries.emplace_back(j, j, 0.0);
			}
			for (int32_t k = 0; k < NumRows; ++k)
			{
				const Row& R = S.Rows[k];
				for (int32_t p = 0; p < R.Size; ++p)
				{
					const auto Found = Local.find(R.Index[p]);
					RowLocal[k * 4 + p] = Found != Local.end() ? Found->second : -1;
				}
				for (int32_t p = 0; p < R.Size; ++p)
				{
					for (int32_t q = 0; q < R.Size; ++q)
					{
						if (RowLocal[k * 4 + p] >= 0 && RowLocal[k * 4 + q] >= 0)
						{
							Entries.emplace_back(RowLocal[k * 4 + p], RowLocal[k * 4 + q], 0.0);
						}
					}
				}
			}
			Sparse Matrix;
			Matrix.Build(N, Entries);
			std::vector<int32_t> Slot(NumRows * 16, -1), DiagonalSlot(N);
			for (int32_t j = 0; j < N; ++j)
			{
				DiagonalSlot[j] = Matrix.Slot(j, j);
			}
			for (int32_t k = 0; k < NumRows; ++k)
			{
				for (int32_t p = 0; p < S.Rows[k].Size; ++p)
				{
					for (int32_t q = 0; q < S.Rows[k].Size; ++q)
					{
						if (RowLocal[k * 4 + p] >= 0 && RowLocal[k * 4 + q] >= 0)
						{
							Slot[k * 16 + p * 4 + q] = Matrix.Slot(RowLocal[k * 4 + p], RowLocal[k * 4 + q]);
						}
					}
				}
			}

			// --- ADMM: the heights and a copy of what each rule sees (its "row value") are solved in turn. The heights: the
			// pull to the targets plus every row pulled to its copy (one fixed matrix: the road bands factorised once, the
			// iterations warm-started). The copies: each rule's own one-line problem, its shortfall against the pull to the
			// heights. A running dual pulls the two together until they agree. Points that can't move are constants ---
			std::vector<double> Const(NumRows, 0.0), Zed(NumRows, 0.0), Dual(NumRows, 0.0), Ax(NumRows, 0.0);
			std::vector<double> Rhs(N), D(N), L1(N), L2(N), R(N), Z(N), Dir(N), ADir(N);
			for (int32_t k = 0; k < NumRows; ++k)
			{
				const Row& Rule = S.Rows[k];
				for (int32_t p = 0; p < Rule.Size; ++p)
				{
					if (RowLocal[k * 4 + p] < 0)
					{
						Const[k] += Rule.Coefficient[p] * X[Rule.Index[p]];
					}
				}
			}
			auto ApplyA = [&](std::vector<double>& Out)
			{
				for (int32_t k = 0; k < NumRows; ++k)
				{
					const Row& Rule = S.Rows[k];
					double Sum = Const[k];
					for (int32_t p = 0; p < Rule.Size; ++p)
					{
						if (RowLocal[k * 4 + p] >= 0)
						{
							Sum += Rule.Coefficient[p] * Xl[RowLocal[k * 4 + p]];
						}
					}
					Out[k] = Sum;
				}
			};
			double Rho = InitialRho;
			auto Assemble = [&]()
			{
				std::fill(Matrix.Values.begin(), Matrix.Values.end(), 0.0);
				for (int32_t j = 0; j < N; ++j)
				{
					Matrix.Values[DiagonalSlot[j]] = W[j];
				}
				for (int32_t k = 0; k < NumRows; ++k)
				{
					const Row& Rule = S.Rows[k];
					for (int32_t p = 0; p < Rule.Size; ++p)
					{
						for (int32_t q = 0; q < Rule.Size; ++q)
						{
							if (RowLocal[k * 4 + p] >= 0 && RowLocal[k * 4 + q] >= 0)
							{
								Matrix.Values[Slot[k * 16 + p * 4 + q]] += Rho * Rule.Coefficient[p] * Rule.Coefficient[q];
							}
						}
					}
				}
				// the band of each road, factorised
				for (int32_t j = 0; j < N; ++j)
				{
					L1[j] = L2[j] = 0.0;
					if (Band[j] >= 2)
					{
						L2[j] = Matrix.At(j, j - 2) / D[j - 2];
					}
					if (Band[j] >= 1)
					{
						L1[j] = (Matrix.At(j, j - 1) - (Band[j] >= 2 ? L2[j] * L1[j - 1] * D[j - 2] : 0.0)) / D[j - 1];
					}
					D[j] = Matrix.Values[DiagonalSlot[j]] - (Band[j] >= 1 ? L1[j] * L1[j] * D[j - 1] : 0.0) - (Band[j] >= 2 ? L2[j] * L2[j] * D[j - 2] : 0.0);
					D[j] = std::max(D[j], 1.0e-9);
				}
			};
			auto Precondition = [&](const std::vector<double>& In, std::vector<double>& Out)
			{
				for (int32_t j = 0; j < N; ++j)
				{
					Out[j] = In[j] - (Band[j] >= 1 ? L1[j] * Out[j - 1] : 0.0) - (Band[j] >= 2 ? L2[j] * Out[j - 2] : 0.0);
				}
				for (int32_t j = 0; j < N; ++j)
				{
					Out[j] /= D[j];
				}
				for (int32_t j = N - 1; j >= 0; --j)
				{
					Out[j] -= (j + 1 < N && Band[j + 1] >= 1 ? L1[j + 1] * Out[j + 1] : 0.0) + (j + 2 < N && Band[j + 2] >= 2 ? L2[j + 2] * Out[j + 2] : 0.0);
				}
			};
			Assemble();
			ApplyA(Zed);
			double Primal = 0.0, DualChange = 0.0;
			for (S.Rounds = 0; S.Rounds < AdmmCap; ++S.Rounds)
			{
				if (S.Rounds > 0 && Seconds() > Deadline)
				{
					break;	// (out of time: the best so far)
				}
				// the heights: pulled to the targets, and each row to its copy
				for (int32_t j = 0; j < N; ++j)
				{
					Rhs[j] = W[j] * T[j];
				}
				for (int32_t k = 0; k < NumRows; ++k)
				{
					const Row& Rule = S.Rows[k];
					const double V = Rho * (Zed[k] - Dual[k] - Const[k]);
					for (int32_t p = 0; p < Rule.Size; ++p)
					{
						if (RowLocal[k * 4 + p] >= 0)
						{
							Rhs[RowLocal[k * 4 + p]] += Rule.Coefficient[p] * V;
						}
					}
				}
				Matrix.Multiply(Xl, ADir);
				for (int32_t j = 0; j < N; ++j)
				{
					R[j] = Rhs[j] - ADir[j];
				}
				Precondition(R, Z);
				Dir = Z;
				double RZ = DotProduct(R, Z);
				const double RhsNorm = std::max(std::sqrt(DotProduct(Rhs, Rhs)), 1.0);
				double Residual = std::sqrt(DotProduct(R, R)) / RhsNorm;
				for (int32_t Iteration = 0; Iteration < CgCap && Residual > CgTolerance; ++Iteration, ++S.Iterations)
				{
					Matrix.Multiply(Dir, ADir);
					const double Alpha = RZ / std::max(DotProduct(Dir, ADir), 1.0e-300);
					for (int32_t j = 0; j < N; ++j)
					{
						Xl[j] += Alpha * Dir[j];
						R[j] -= Alpha * ADir[j];
					}
					Precondition(R, Z);
					const double NewRZ = DotProduct(R, Z);
					const double Beta = NewRZ / std::max(RZ, 1.0e-300);
					RZ = NewRZ;
					for (int32_t j = 0; j < N; ++j)
					{
						Dir[j] = Z[j] + Beta * Dir[j];
					}
					Residual = std::sqrt(DotProduct(R, R)) / RhsNorm;
				}

				// the copies: each rule on its own, short at a price, against the pull to the heights (over-relaxed)
				ApplyA(Ax);
				Primal = DualChange = 0.0;
				for (int32_t k = 0; k < NumRows; ++k)
				{
					const Row& Rule = S.Rows[k];
					const double Hat = OverRelax * Ax[k] + (1.0 - OverRelax) * Zed[k];
					const double V = Hat + Dual[k];
					const double Next = V >= Rule.Bound ? V : (Rule.Weight * Rule.Bound + Rho * V) / (Rule.Weight + Rho);
					DualChange = std::max(DualChange, std::abs(Next - Zed[k]));
					Primal = std::max(Primal, std::abs(Ax[k] - Next));
					Zed[k] = Next;
					Dual[k] += Hat - Next;
				}
				if (bTrace && (S.Rounds % 100 == 0))
				{
					S.Trace += Format(" %d:%.2f/%.2f", S.Rounds, Primal, DualChange);
				}
				if (Primal < Settled && DualChange < Settled)
				{
					++S.Rounds;
					S.bSettled = true;
					break;
				}
				// (the price of disagreeing, balanced now and then: the dual is in its units)
				if ((S.Rounds + 1) % 25 == 0)
				{
					if (Primal > 10.0 * DualChange && Rho < 1.0e6)
					{
						Rho *= 2.0;
						for (double& U : Dual) { U *= 0.5; }
						Assemble();
					}
					else if (DualChange > 10.0 * Primal && Rho > 1.0)
					{
						Rho *= 0.5;
						for (double& U : Dual) { U *= 2.0; }
						Assemble();
					}
				}
			}
			for (int32_t j = 0; j < N; ++j)
			{
				X[Global[j]] = Xl[j];
			}
			if (bTrace)
			{
				// what is still broken: the worst hard rule, and the slack the soft ones took
				double WorstHard = 0.0, SoftSlack = 0.0;
				uint8_t WorstWhat = 0;
				for (const Row& Rule : S.Rows)
				{
					double Residual = Rule.Bound;
					for (int32_t i = 0; i < Rule.Size; ++i)
					{
						Residual -= Rule.Coefficient[i] * X[Rule.Index[i]];
					}
					if (Rule.Weight >= HardWeight)
					{
						if (Residual > WorstHard)
						{
							WorstHard = Residual;
							WorstWhat = Rule.What;
						}
					}
					else
					{
						SoftSlack += std::max(Residual, 0.0);
					}
				}
				S.Trace += Format("; worst hard rule %.1f cm (%s), soft short %.0f cm in all", WorstHard, WhatNames[WorstWhat], SoftSlack);
			}
		}

		using MinHeap = std::priority_queue<std::pair<double, int32_t>, std::vector<std::pair<double, int32_t>>, std::greater<std::pair<double, int32_t>>>;

		/** Union-find over the roads */
		struct Groups
		{
			std::vector<int32_t> Parent;
			explicit Groups(int32_t N) : Parent(N) { for (int32_t i = 0; i < N; ++i) { Parent[i] = i; } }
			int32_t Find(int32_t A) { while (Parent[A] != A) { Parent[A] = Parent[Parent[A]]; A = Parent[A]; } return A; }
			void Join(int32_t A, int32_t B) { A = Find(A); B = Find(B); if (A != B) { Parent[A] = B; } }
		};
	}

	Numbering::Numbering(const RoadGraph& Graph, const std::vector<std::vector<Vec2>>& Points)
	{
		NumNodes = static_cast<int32_t>(Graph.Nodes.size());
		const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());
		EdgeBase.resize(NumEdges);
		EdgeStart.resize(NumEdges);
		EdgeEnd.resize(NumEdges);
		EdgePoints.resize(NumEdges);
		Count = NumNodes;
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			EdgeBase[EdgeIndex] = Count;
			EdgeStart[EdgeIndex] = Graph.Edges[EdgeIndex].StartNode;
			EdgeEnd[EdgeIndex] = Graph.Edges[EdgeIndex].EndNode;
			EdgePoints[EdgeIndex] = static_cast<int32_t>(Points[EdgeIndex].size());
			Count += std::max(0, EdgePoints[EdgeIndex] - 2);
		}
	}

	std::vector<LevelPair> FindLevelPairs(const RoadGraph& Graph, const std::function<const std::vector<Vec2>&(int32_t)>& PointsOf)
	{
		std::vector<LevelPair> Pairs;
		const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());
		constexpr double Cell = 5000.0;
		// where roads meet: each road's two end nodes and the nodes a short link away from them (a road forking into a tunnel is
		// two or three short roads at one place). Roads that share such a node meet there, and near it one is not over the other
		constexpr double LinkLength = 4000.0, MeetingReach = 4500.0;	// (cm)
		std::vector<std::vector<int32_t>> Meeting(NumEdges);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			for (const int32_t Node : { Graph.Edges[EdgeIndex].StartNode, Graph.Edges[EdgeIndex].EndNode })
			{
				if (Node < 0)
				{
					continue;
				}
				Meeting[EdgeIndex].push_back(Node);
				for (const int32_t Link : Graph.Nodes[Node].Edges)
				{
					const RoadEdge& Linked = Graph.Edges[Link];
					double Length = 0.0;
					const std::vector<Vec2>& LP = PointsOf(Link);
					for (size_t i = 0; i + 1 < LP.size(); ++i)
					{
						Length += Distance(LP[i], LP[i + 1]);
					}
					if (Length < LinkLength)
					{
						Meeting[EdgeIndex].push_back(Linked.StartNode == Node ? Linked.EndNode : Linked.StartNode);
					}
				}
			}
		}
		auto MeetNear = [&](int32_t A, int32_t B, const Vec2& At)
		{
			for (const int32_t Node : Meeting[A])
			{
				if (Node >= 0 && std::find(Meeting[B].begin(), Meeting[B].end(), Node) != Meeting[B].end() && Distance(Graph.Nodes[Node].Position, At) < MeetingReach)
				{
					return true;
				}
			}
			return false;
		};
		std::unordered_map<uint64_t, std::vector<std::pair<int32_t, int32_t>>> Grid;	// cell -> (edge, segment) by the segment's middle
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const std::vector<Vec2>& P = PointsOf(EdgeIndex);
			for (int32_t i = 0; i + 1 < static_cast<int32_t>(P.size()); ++i)
			{
				const Vec2 Mid = (P[i] + P[i + 1]) * 0.5;
				Grid[CellKey(FloorToInt(Mid.X / Cell), FloorToInt(Mid.Y / Cell))].emplace_back(EdgeIndex, i);
			}
		}
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const RoadEdge& Own = Graph.Edges[EdgeIndex];
			const std::vector<Vec2>& P = PointsOf(EdgeIndex);
			const int32_t RankOwn = LevelRank(Own);
			for (int32_t i = 0; i + 1 < static_cast<int32_t>(P.size()); ++i)
			{
				const Vec2 A = P[i], AB = P[i + 1] - A;
				const double LengthAB = AB.Size();
				if (LengthAB < 50.0)
				{
					continue;
				}
				const Vec2 Mid = A + AB * 0.5;
				const int32_t CX = FloorToInt(Mid.X / Cell), CY = FloorToInt(Mid.Y / Cell);
				for (int32_t GX = CX - 1; GX <= CX + 1; ++GX)
				{
					for (int32_t GY = CY - 1; GY <= CY + 1; ++GY)
					{
						const auto Found = Grid.find(CellKey(GX, GY));
						if (Found == Grid.end())
						{
							continue;
						}
						for (const auto& [Other, k] : Found->second)
						{
							// (each pair of roads once, from the lower-numbered one; never the same road, or one joined to it: they meet)
							if (Other <= EdgeIndex)
							{
								continue;
							}
							const RoadEdge& Them = Graph.Edges[Other];
							if (Own.StartNode == Them.StartNode || Own.StartNode == Them.EndNode || Own.EndNode == Them.StartNode || Own.EndNode == Them.EndNode)
							{
								continue;
							}
							const int32_t RankThem = LevelRank(Them);
							if (RankOwn == RankThem)
							{
								continue;
							}
							const std::vector<Vec2>& Q = PointsOf(Other);
							const Vec2 C = Q[k], CD = Q[k + 1] - C;
							const double LengthCD = CD.Size();
							if (LengthCD < 50.0)
							{
								continue;
							}
							// crossing: through both pieces; alongside: nearly parallel, one's middle within the other's carriageway (lanes that only
							// touch are a ramp or a road beside it, not a road over it)
							double T = -1.0, U = -1.0;
							const double Denominator = AB.X * CD.Y - AB.Y * CD.X;
							if (std::abs(Denominator) >= 1.0)
							{
								T = ((C.X - A.X) * CD.Y - (C.Y - A.Y) * CD.X) / Denominator;
								U = ((C.X - A.X) * AB.Y - (C.Y - A.Y) * AB.X) / Denominator;
							}
							if (T <= 0.0 || T >= 1.0 || U <= 0.0 || U >= 1.0)
							{
								T = U = -1.0;
								if (std::abs(Dot(AB, CD)) / (LengthAB * LengthCD) > 0.9)
								{
									const double On = std::clamp(Dot(Mid - C, CD) / (LengthCD * LengthCD), 0.0, 1.0);
									if (On > 0.0 && On < 1.0 && Distance(C + CD * On, Mid) < std::max(Own.HalfWidth, Them.HalfWidth))
									{
										T = 0.5;
										U = On;
									}
								}
							}
							if (T < 0.0 || MeetNear(EdgeIndex, Other, A + AB * T))
							{
								continue;
							}
							Pairs.push_back(RankOwn > RankThem ? LevelPair{ EdgeIndex, i, Other, k, T, U } : LevelPair{ Other, k, EdgeIndex, i, U, T });
						}
					}
				}
			}
		}

		// a bus has to fit across the whole of the overlap, not only where the middles cross (where roads cross at an angle
		// the one over the other is nearer the other's lanes on either side): each pair of roads found is looked at all
		// along the upper road, as many places as its lanes overlap the lower's
		constexpr double OverlapStep = 300.0;
		std::set<std::pair<int32_t, int32_t>> Stacked;
		for (const LevelPair& Pair : Pairs)
		{
			Stacked.emplace(Pair.UpperEdge, Pair.LowerEdge);
		}
		std::set<std::tuple<int32_t, int32_t, int32_t, int32_t>> Seen;
		for (const LevelPair& Pair : Pairs)
		{
			Seen.emplace(Pair.UpperEdge, Pair.LowerEdge, Pair.UpperSegment, static_cast<int32_t>(Pair.UpperT * 10.0));
		}
		const size_t Found = Pairs.size();
		for (const auto& [UpperIndex, LowerIndex] : Stacked)
		{
			const RoadEdge& Up = Graph.Edges[UpperIndex];
			const RoadEdge& Down = Graph.Edges[LowerIndex];
			const std::vector<Vec2>& P = PointsOf(UpperIndex);
			const std::vector<Vec2>& Q = PointsOf(LowerIndex);
			const double Reach = std::max(Up.HalfWidth, Down.HalfWidth);
			for (int32_t i = 0; i + 1 < static_cast<int32_t>(P.size()); ++i)
			{
				const Vec2 A = P[i], AB = P[i + 1] - A;
				const double LengthAB = AB.Size();
				if (LengthAB < 50.0)
				{
					continue;
				}
				const int32_t Looks = std::max(1, static_cast<int32_t>(std::ceil(LengthAB / OverlapStep)));
				for (int32_t Look = 0; Look < Looks; ++Look)
				{
					const double T = (Look + 0.5) / Looks;
					const Vec2 At = A + AB * T;
					int32_t BestSegment = -1;
					double BestU = 0.0, Best = Reach;
					for (int32_t k = 0; k + 1 < static_cast<int32_t>(Q.size()); ++k)
					{
						const Vec2 CD = Q[k + 1] - Q[k];
						const double Length2 = CD.SizeSquared();
						if (Length2 < 2500.0)
						{
							continue;
						}
						const double U = std::clamp(Dot(At - Q[k], CD) / Length2, 0.0, 1.0);
						const double Away = Distance(Q[k] + CD * U, At);
						if (Away < Best)
						{
							Best = Away;
							BestSegment = k;
							BestU = U;
						}
					}
					if (BestSegment < 0 || MeetNear(UpperIndex, LowerIndex, At) || !Seen.emplace(UpperIndex, LowerIndex, i, static_cast<int32_t>(T * 10.0)).second)
					{
						continue;
					}
					Pairs.push_back(LevelPair{ UpperIndex, i, LowerIndex, BestSegment, T, BestU });
				}
			}
		}
		(void)Found;
		return Pairs;
	}

	void SolveStacks(const StackInput& In, std::vector<double>& X)
	{
		const double Start = Seconds();
		const RoadGraph& Graph = In.Graph;
		const std::vector<std::vector<Vec2>>& Points = In.Points;
		const Numbering& Unknowns = In.Unknowns;
		const int32_t NumNodes = Unknowns.NumNodes;
		const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());
		const int32_t NumUnknowns = Unknowns.Count;
		const ParallelForFn ParallelFor = In.ParallelFor ? In.ParallelFor : ParallelForFn(&ThreadsParallelFor);
		auto Log = [&In](const std::string& Line) { if (In.Log) { In.Log(Line); } };
		auto Count = [&Unknowns](int32_t Edge) { return Unknowns.EdgePoints[Edge]; };
		auto U = [&Unknowns](int32_t Edge, int32_t Point) { return Unknowns.At(Edge, Point); };
		auto Length = [&Points](int32_t Edge, int32_t Segment) { return Distance(Points[Edge][Segment], Points[Edge][Segment + 1]); };
		auto IsOpen = [](const RoadEdge& Edge) { return !Edge.bTunnel && !Edge.bBridge && Edge.Layer == 0; };
		const std::vector<double> X0 = X;

		// --- 1. the level pairs: where the map puts one road over another ---
		const std::vector<LevelPair> Pairs = FindLevelPairs(Graph, [&Points](int32_t Edge) -> const std::vector<Vec2>& { return Points[Edge]; });

		// --- 2. which roads are in a stack: tunnels, bridges, other levels, anything in a pair, anything whose surface is
		// one with theirs; then the roads leading in, walked out to the next junction (which may move) and once more to
		// the junction beyond (which may not) ---
		std::vector<uint8_t> InStack(NumEdges, 0);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			InStack[EdgeIndex] = IsOpen(Graph.Edges[EdgeIndex]) ? 0 : 1;
		}
		for (const LevelPair& Pair : Pairs)
		{
			InStack[Pair.UpperEdge] = InStack[Pair.LowerEdge] = 1;
		}
		for (int32_t Pass = 0; Pass < 4; ++Pass)
		{
			for (const OverlapTie& Tie : In.Ties)
			{
				if (InStack[Tie.PointEdge] || InStack[Tie.OtherEdge])
				{
					InStack[Tie.PointEdge] = InStack[Tie.OtherEdge] = 1;
				}
			}
		}
		std::vector<Kind> KindOf(NumUnknowns, Kind::Outside);
		auto EdgeKind = [&](const RoadEdge& Edge)
		{
			return Edge.bTunnel || Edge.Layer < 0 ? Kind::Tunnel : Edge.bBridge || Edge.Layer > 0 ? Kind::Span : Kind::Open;
		};
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			if (!InStack[EdgeIndex])
			{
				continue;
			}
			const Kind K = EdgeKind(Graph.Edges[EdgeIndex]);
			for (int32_t i = 1; i + 1 < Count(EdgeIndex); ++i)
			{
				KindOf[U(EdgeIndex, i)] = K;
			}
		}
		// its nodes: a junction with a road in the open holds (it moves only when nothing else can); a node with only
		// tunnels meeting (the interchange under the Rocher) or only decks moves with them; a mouth moves with its tunnel
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			const std::vector<int32_t>& Here = Graph.Nodes[NodeIndex].Edges;
			Kind Most = Kind::Outside;
			bool bOpen = false;
			for (const int32_t EdgeIndex : Here)
			{
				bOpen |= IsOpen(Graph.Edges[EdgeIndex]);
				if (InStack[EdgeIndex])
				{
					Most = std::max(Most, EdgeKind(Graph.Edges[EdgeIndex]));
				}
			}
			if (Most != Kind::Outside)
			{
				KindOf[NodeIndex] = Here.size() >= 3 && bOpen ? Kind::Junction : Most;
			}
		}
		std::vector<int32_t> Movable;	// (the junctions found by the first walk: their roads are walked once more)
		std::vector<uint8_t> Leeway(NumEdges, 0), NodeLeeway(NumNodes, 0), Raised(NumEdges, 0);	// (Lifts: leads on to a bridge; Sinks: into a tunnel, or under a bridge)
		auto Walk = [&](int32_t From, int32_t FirstEdge, int32_t Depth, uint8_t Mode)
		{
			int32_t Node = From, Edge = FirstEdge;
			double Reach = 0.0;
			for (int32_t Steps = 0; Steps < 200; ++Steps)
			{
				if (InStack[Edge])
				{
					return;	// (already walked, or another stack's road)
				}
				InStack[Edge] = 2;
				Leeway[Edge] = Mode;
				for (int32_t i = 1; i + 1 < Count(Edge); ++i)
				{
					KindOf[U(Edge, i)] = Kind::Open;
				}
				for (int32_t i = 0; i + 1 < Count(Edge); ++i)
				{
					Reach += Length(Edge, i);
				}
				const RoadEdge& Road = Graph.Edges[Edge];
				Node = Road.StartNode == Node ? Road.EndNode : Road.StartNode;
				if (KindOf[Node] != Kind::Outside)
				{
					return;	// (a node already in a stack: joined there)
				}
				const std::vector<int32_t>& Next = Graph.Nodes[Node].Edges;
				if (Next.size() >= 3)
				{
					KindOf[Node] = Depth == 0 ? Kind::Junction : Kind::Fixed;
					NodeLeeway[Node] = Mode;
					if (Depth == 0)
					{
						Movable.push_back(Node);
					}
					return;
				}
				if (Next.size() < 2)
				{
					KindOf[Node] = Kind::Open;	// (a dead end: free to follow its road)
					return;
				}
				if (Reach >= ApproachReach)
				{
					KindOf[Node] = Kind::Fixed;
					return;
				}
				KindOf[Node] = Kind::Open;
				Edge = Next[0] == Edge ? Next[1] : Next[0];
			}
			KindOf[Node] = Kind::Fixed;
		};
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			if (KindOf[NodeIndex] == Kind::Outside)
			{
				continue;
			}
			const std::vector<int32_t> Here = Graph.Nodes[NodeIndex].Edges;
			uint8_t Mode = 0;
			for (const int32_t EdgeIndex : Here)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				Mode |= Edge.bTunnel || Edge.Layer < 0 ? Sinks : Edge.bBridge || Edge.Layer > 0 ? Lifts : 0;
			}
			NodeLeeway[NodeIndex] = Mode;
			for (const int32_t EdgeIndex : Here)
			{
				Walk(NodeIndex, EdgeIndex, 0, Mode);
			}
		}
		for (size_t m = 0; m < Movable.size(); ++m)
		{
			const int32_t NodeIndex = Movable[m];
			const std::vector<int32_t> Here = Graph.Nodes[NodeIndex].Edges;
			for (const int32_t EdgeIndex : Here)
			{
				Walk(NodeIndex, EdgeIndex, 1, NodeLeeway[NodeIndex]);
			}
		}

		for (const LevelPair& Pair : Pairs)
		{
			if (IsOpen(Graph.Edges[Pair.LowerEdge]))
			{
				Leeway[Pair.LowerEdge] |= Sinks;
			}
			// (a road over a tunnel: where the tunnel can't dive deep enough (near its mouth, or under a low hill) the road
			// rises into a short bridge, a truck's height over it. The data shows the ground there, never the deck)
			if (Graph.Edges[Pair.LowerEdge].bTunnel && IsOpen(Graph.Edges[Pair.UpperEdge]))
			{
				Leeway[Pair.UpperEdge] |= Lifts;
				Raised[Pair.UpperEdge] = 1;
			}
		}

		// --- 3. the stacks: roads joined by a point that moves, by a pair or by a tie; each solved on its own ---
		Groups Group(NumEdges);
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			if (KindOf[NodeIndex] == Kind::Outside || KindOf[NodeIndex] == Kind::Fixed)
			{
				continue;
			}
			int32_t First = -1;
			for (const int32_t EdgeIndex : Graph.Nodes[NodeIndex].Edges)
			{
				if (InStack[EdgeIndex])
				{
					if (First < 0) { First = EdgeIndex; } else { Group.Join(First, EdgeIndex); }
				}
			}
		}
		for (const LevelPair& Pair : Pairs)
		{
			Group.Join(Pair.UpperEdge, Pair.LowerEdge);
		}
		for (const OverlapTie& Tie : In.Ties)
		{
			if (InStack[Tie.PointEdge] && InStack[Tie.OtherEdge])
			{
				Group.Join(Tie.PointEdge, Tie.OtherEdge);
			}
		}
		std::vector<Stack> Stacks;
		std::vector<int32_t> StackOf(NumEdges, -1);
		{
			std::unordered_map<int32_t, int32_t> ByRoot;
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				if (!InStack[EdgeIndex])
				{
					continue;
				}
				const int32_t Root = Group.Find(EdgeIndex);
				auto Found = ByRoot.find(Root);
				if (Found == ByRoot.end())
				{
					Found = ByRoot.emplace(Root, static_cast<int32_t>(Stacks.size())).first;
					Stacks.emplace_back();
				}
				StackOf[EdgeIndex] = Found->second;
				Stacks[Found->second].Edges.push_back(EdgeIndex);
			}
		}
		if (Stacks.empty())
		{
			Log("[Heights] stacks: none");
			return;
		}

		// --- 4. where each point would like to be, and how hard it holds there. A tunnel: no higher than a cover of earth
		// under the ground away from its mouths ---
		std::vector<double> InvW(NumUnknowns, 0.0), Spacing(NumUnknowns, 0.0);
		std::vector<double> GroundOf(NumUnknowns, std::numeric_limits<double>::max());
		std::vector<double> FromMouth(NumUnknowns, std::numeric_limits<double>::max());
		{
			struct TubeLink { int32_t Other; double Length; };
			std::vector<std::vector<TubeLink>> Tube(NumUnknowns);
			MinHeap Heap;
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				if (!InStack[EdgeIndex])
				{
					continue;
				}
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				const std::vector<float>& D = In.Data[EdgeIndex];
				for (int32_t i = 0; i + 1 < Count(EdgeIndex); ++i)
				{
					const double L = Length(EdgeIndex, i);
					Spacing[U(EdgeIndex, i)] += 0.5 * L;
					Spacing[U(EdgeIndex, i + 1)] += 0.5 * L;
					if (Edge.bTunnel)
					{
						Tube[U(EdgeIndex, i)].push_back({ U(EdgeIndex, i + 1), L });
						Tube[U(EdgeIndex, i + 1)].push_back({ U(EdgeIndex, i), L });
					}
				}
				if (Edge.bTunnel)
				{
					for (int32_t i = 0; i < Count(EdgeIndex) && i < static_cast<int32_t>(D.size()); ++i)
					{
						GroundOf[U(EdgeIndex, i)] = std::min(GroundOf[U(EdgeIndex, i)], static_cast<double>(D[i]));
					}
					for (const int32_t Node : { Edge.StartNode, Edge.EndNode })
					{
						// (a mouth: a road not in a tunnel meets it there)
						const std::vector<int32_t>& Here = Graph.Nodes[Node].Edges;
						if (std::any_of(Here.begin(), Here.end(), [&](int32_t E) { return !Graph.Edges[E].bTunnel; }) && FromMouth[Node] > 0.0)
						{
							FromMouth[Node] = 0.0;
							Heap.emplace(0.0, Node);
						}
					}
				}
			}
			while (!Heap.empty())
			{
				const std::pair<double, int32_t> Top = Heap.top();
				Heap.pop();
				if (Top.first > FromMouth[Top.second])
				{
					continue;
				}
				for (const TubeLink& Next : Tube[Top.second])
				{
					if (Top.first + Next.Length < FromMouth[Next.Other])
					{
						FromMouth[Next.Other] = Top.first + Next.Length;
						Heap.emplace(FromMouth[Next.Other], Next.Other);
					}
				}
			}
		}
		std::vector<uint8_t> Measured(NumUnknowns, 0), May(NumUnknowns, 0);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			if (!InStack[EdgeIndex])
			{
				continue;
			}
			for (int32_t i = 0; i < Count(EdgeIndex); ++i)
			{
				Measured[U(EdgeIndex, i)] |= Graph.Edges[EdgeIndex].bMeasured ? 1 : 0;
				May[U(EdgeIndex, i)] |= Leeway[EdgeIndex];
			}
		}
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			May[NodeIndex] |= NodeLeeway[NodeIndex];
		}
		for (int32_t Point = 0; Point < NumUnknowns; ++Point)
		{
			double Weight = WeightOf(KindOf[Point]);
			if (Weight <= 0.0)
			{
				continue;
			}
			if (Measured[Point] && !May[Point] && (KindOf[Point] == Kind::Junction || KindOf[Point] == Kind::Open))
			{
				Weight = MeasuredWeight;
			}
			InvW[Point] = 1.0 / (Weight * std::max(Spacing[Point], 100.0) / WeightSpacing);
			if (KindOf[Point] == Kind::Tunnel && GroundOf[Point] < 1.0e8)
			{
				const double Cover = TunnelCoverDepth * std::clamp(FromMouth[Point] / PortalZone, 0.0, 1.0);
				X[Point] = std::min(X0[Point], GroundOf[Point] - Cover);
			}
		}

		// --- 5. the rules of each stack ---
		auto AddRow = [&](Stack& S, std::initializer_list<int32_t> Index, std::initializer_list<double> Coefficient, double Bound, double Weight, bool /*bEquality*/, uint8_t What)
		{
			Row R;
			R.What = What;
			double Largest = 0.0;
			bool bMoves = false;
			for (const int32_t I : Index)
			{
				R.Index[R.Size] = I;
				R.Coefficient[R.Size] = Coefficient.begin()[R.Size];
				Largest = std::max(Largest, std::abs(R.Coefficient[R.Size]));
				bMoves |= InvW[I] > 0.0;
				++R.Size;
			}
			if (!bMoves || Largest <= 0.0)
			{
				return;	// (nothing it holds can move)
			}
			// (every rule in cm: a curve's coefficients are 1/cm², and left so its weight would mean nothing)
			const double Scale = 1.0 / Largest;
			for (int32_t i = 0; i < R.Size; ++i)
			{
				R.Coefficient[i] *= Scale;
			}
			R.Bound = Bound * Scale;
			R.Weight = Weight;
			S.Rows.push_back(R);
		};
		// each piece's grade rule: a tunnel's, a span's (its class's, never under SpanGrade) or the open road's clipped limit;
		// a span joining a roundabout levels out over its landing before the ring, as the open roads do (RoadHeights)
		std::vector<uint8_t> OnRing(NumNodes, 0);
		for (const RoadEdge& Edge : Graph.Edges)
		{
			if (Edge.bRoundabout)
			{
				OnRing[Edge.StartNode] = OnRing[Edge.EndNode] = 1;
			}
		}
		auto GradeOf = [&](int32_t EdgeIndex, int32_t i)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			const RoadDesign& Rules = In.Design[EdgeIndex];
			const bool bSpan = Edge.bTunnel || Edge.bBridge || Edge.Layer != 0 || Raised[EdgeIndex];
			double Grade = Edge.bTunnel ? TunnelGrade : bSpan ? std::max({ static_cast<double>(Rules.MaxGrade), static_cast<double>(Rules.MaxGradeMountain), SpanGrade }) : In.GradeLimit[EdgeIndex][i];
			if (bSpan && !Edge.bRoundabout && (OnRing[Edge.StartNode] || OnRing[Edge.EndNode]))
			{
				double Before = 0.0, Total = 0.0;
				for (int32_t k = 0; k + 1 < Count(EdgeIndex); ++k)
				{
					Before += k < i ? Length(EdgeIndex, k) : 0.0;
					Total += Length(EdgeIndex, k);
				}
				const double After = Total - Before - Length(EdgeIndex, i);
				if ((OnRing[Edge.StartNode] && Before < Rules.LandingLength) || (OnRing[Edge.EndNode] && After < Rules.LandingLength))
				{
					Grade = std::min(Grade, static_cast<double>(Rules.LandingGrade));
				}
			}
			return Grade;
		};
		int32_t NumRoads = 0;
		std::vector<uint8_t> SeaDone(NumUnknowns, 0);
		for (Stack& S : Stacks)
		{
			for (const int32_t EdgeIndex : S.Edges)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				const RoadDesign& Rules = In.Design[EdgeIndex];
				const int32_t N = Count(EdgeIndex);
				// (a span: a tunnel, a bridge or a ramp on another level: its class's grade and curves, at a price, since its
				// heights are nobody's measurement; a road on the ground keeps what it does already)
				const bool bSpan = Edge.bTunnel || Edge.bBridge || Edge.Layer != 0 || Raised[EdgeIndex];
				const double Strength = bSpan ? SteepWeight : HardWeight;
				++NumRoads;
				// the grade of every piece, both ways
				for (int32_t i = 0; i + 1 < N; ++i)
				{
					const int32_t A = U(EdgeIndex, i), B = U(EdgeIndex, i + 1);
					const double L = Length(EdgeIndex, i);
					if (L < 1.0)
					{
						continue;
					}
					double Grade = GradeOf(EdgeIndex, i);
					if (!bSpan)
					{
						Grade = std::max(Grade, std::abs(X0[B] - X0[A]) / L * KeepGrade);
					}
					AddRow(S, { A, B }, { -1.0, 1.0 }, -Grade * L, Strength, false, 0);
					AddRow(S, { A, B }, { 1.0, -1.0 }, -Grade * L, Strength, false, 0);
					if (bSpan)
					{
						AddRow(S, { A, B }, { -1.0, 1.0 }, -AbsurdGrade * L, HardWeight, false, 0);
						AddRow(S, { A, B }, { 1.0, -1.0 }, -AbsurdGrade * L, HardWeight, false, 0);
					}
				}
				// its crests and dips no sharper than its class allows (K: m of road per 1% of grade change)
				for (int32_t i = 1; i + 1 < N; ++i)
				{
					const int32_t A = U(EdgeIndex, i - 1), B = U(EdgeIndex, i), C = U(EdgeIndex, i + 1);
					const double LAB = Length(EdgeIndex, i - 1), LBC = Length(EdgeIndex, i);
					if (LAB < 1.0 || LBC < 1.0)
					{
						continue;
					}
					double Coefficient[3];
					BendCoefficients(LAB, LBC, Coefficient);
					const double Now = Coefficient[0] * X0[A] + Coefficient[1] * X0[B] + Coefficient[2] * X0[C];
					double Crest = 1.0e-4 / Rules.CrestK, Sag = 1.0e-4 / Rules.SagK;
					if (!bSpan)
					{
						Crest = std::max(Crest, -Now * KeepCurve);
						Sag = std::max(Sag, Now * KeepCurve);
					}
					AddRow(S, { A, B, C }, { Coefficient[0], Coefficient[1], Coefficient[2] }, -Crest, Strength, false, 1);
					AddRow(S, { A, B, C }, { -Coefficient[0], -Coefficient[1], -Coefficient[2] }, -Sag, Strength, false, 1);
				}
				// above the sea
				for (int32_t i = 0; i < N; ++i)
				{
					const int32_t P = U(EdgeIndex, i);
					if (InvW[P] > 0.0 && !SeaDone[P])
					{
						SeaDone[P] = 1;
						AddRow(S, { P }, { 1.0 }, SeaFloor, HardWeight, false, 3);
					}
				}
			}
		}
		// a measured road with leeway one way still never goes the other way
		std::vector<uint8_t> GroundDone(NumUnknowns, 0);
		for (Stack& S : Stacks)
		{
			for (const int32_t EdgeIndex : S.Edges)
			{
				for (int32_t i = 0; i < Count(EdgeIndex); ++i)
				{
					const int32_t P = U(EdgeIndex, i);
					if (!Measured[P] || !May[P] || InvW[P] <= 0.0 || GroundDone[P] || (KindOf[P] != Kind::Junction && KindOf[P] != Kind::Open))
					{
						continue;
					}
					GroundDone[P] = 1;
					if (May[P] == Lifts)
					{
						AddRow(S, { P }, { 1.0 }, X0[P] - BeyondMeasured, HardWeight, false, 5);
					}
					else if (May[P] == Sinks)
					{
						AddRow(S, { P }, { -1.0 }, -(X0[P] + BeyondMeasured), HardWeight, false, 5);
					}
				}
			}
		}
		// a truck's height between the two roads of every pair
		for (const LevelPair& Pair : Pairs)
		{
			Stack& S = Stacks[StackOf[Pair.UpperEdge]];
			const int32_t UA = U(Pair.UpperEdge, Pair.UpperSegment), UB = U(Pair.UpperEdge, Pair.UpperSegment + 1);
			const int32_t LA = U(Pair.LowerEdge, Pair.LowerSegment), LB = U(Pair.LowerEdge, Pair.LowerSegment + 1);
			AddRow(S, { UA, UB, LA, LB }, { 1.0 - Pair.UpperT, Pair.UpperT, -(1.0 - Pair.LowerT), -Pair.LowerT }, MinClearance, ShortWeight, false, 2);
			AddRow(S, { UA, UB, LA, LB }, { 1.0 - Pair.UpperT, Pair.UpperT, -(1.0 - Pair.LowerT), -Pair.LowerT }, BridgeClearance, PreferWeight, false, 2);
		}
		// overlapping surfaces one surface: no further apart than they already are (the main solve ties them, but the grade
		// limits after it can part them a little, and held exactly together here they would contradict the grades)
		for (const OverlapTie& Tie : In.Ties)
		{
			if (InStack[Tie.PointEdge] && InStack[Tie.OtherEdge])
			{
				const double Now = X0[Tie.Point] - (1.0 - Tie.T) * X0[Tie.A] - Tie.T * X0[Tie.B];
				const double Apart = std::abs(Now) * KeepGrade + 0.5;
				Stack& S = Stacks[StackOf[Tie.PointEdge]];
				AddRow(S, { Tie.Point, Tie.A, Tie.B }, { 1.0, -(1.0 - Tie.T), -Tie.T }, -Apart, TieWeight, false, 4);
				AddRow(S, { Tie.Point, Tie.A, Tie.B }, { -1.0, 1.0 - Tie.T, Tie.T }, -Apart, TieWeight, false, 4);
			}
		}

		// --- 6. solve, each stack on its own ---
		std::vector<int32_t> InnerK(NumUnknowns, -1);	// (each inner point's place along its road: the solver's band)
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			if (InStack[EdgeIndex])
			{
				for (int32_t i = 1; i + 1 < Count(EdgeIndex); ++i)
				{
					InnerK[U(EdgeIndex, i)] = i - 1;
				}
			}
		}
		const bool bTrace = std::getenv("GEODRIVE_STACKLOG") != nullptr;	// (for finding solver problems: each stack's solve, round by round, in the log)
		const double Deadline = Seconds() + StackBudget;
		ParallelFor(static_cast<int32_t>(Stacks.size()), [&](int32_t Index)
		{
			Stack& S = Stacks[Index];
			SolveStack(S, X, InvW, InnerK, bTrace, Deadline);
			// a grade or curve rule still broken by more than EscalateShort is dearer next time round (each pass a factor of
			// EscalateBy, never past EscalateCap, so clearance and the sea still win): a road that carries on past what a compromise
			// left it (a ramp squeezed into a short climb by a road over it) makes the points at its ends give way instead
			for (int32_t Pass = 0; Pass < EscalatePasses && Seconds() < Deadline; ++Pass)
			{
				int32_t Dearer = 0;
				for (Row& Rule : S.Rows)
				{
					if (Rule.What > 1 || Rule.Weight >= EscalateCap)
					{
						continue;
					}
					double Residual = Rule.Bound;
					for (int32_t i = 0; i < Rule.Size; ++i)
					{
						Residual -= Rule.Coefficient[i] * X[Rule.Index[i]];
					}
					if (Residual > EscalateShort)
					{
						Rule.Weight = std::min(Rule.Weight * EscalateBy, EscalateCap);
						++Dearer;
					}
				}
				if (Dearer == 0)
				{
					break;
				}
				SolveStack(S, X, InvW, InnerK, bTrace, Deadline);
			}
		});
		if (bTrace)
		{
			for (size_t i = 0; i < Stacks.size(); ++i)
			{
				const Stack& S = Stacks[i];
				if (S.Edges.size() >= 20 || !S.bSettled)
				{
					Log(Format("[Stacks] stack %d: %d roads, %d points, %d rules, %d rounds, %d cg iterations%s; primal/dual by round%s", static_cast<int32_t>(i), static_cast<int32_t>(S.Edges.size()),
						S.Points, static_cast<int32_t>(S.Rows.size()), S.Rounds, S.Iterations, S.bSettled ? "" : " (not settled)", S.Trace.c_str()));
				}
			}
		}

		// --- 7. what came of it ---
		int32_t NumRows = 0, NumPoints = 0, MostRounds = 0, Unsettled = 0;
		for (const Stack& S : Stacks)
		{
			NumRows += static_cast<int32_t>(S.Rows.size());
			NumPoints += S.Points;
			MostRounds = std::max(MostRounds, S.Rounds);
			Unsettled += S.bSettled ? 0 : 1;
		}
		struct Short { double By; std::string Text; };
		std::vector<Short> Shorts;
		auto Named = [](const std::string& Name) { return Name.empty() ? std::string("unnamed") : Name; };
		for (const LevelPair& Pair : Pairs)
		{
			const double Upper = Lerp(X[U(Pair.UpperEdge, Pair.UpperSegment)], X[U(Pair.UpperEdge, Pair.UpperSegment + 1)], Pair.UpperT);
			const double Lower = Lerp(X[U(Pair.LowerEdge, Pair.LowerSegment)], X[U(Pair.LowerEdge, Pair.LowerSegment + 1)], Pair.LowerT);
			const double By = MinClearance - (Upper - Lower);
			if (By <= ShortTolerance)
			{
				continue;
			}
			const RoadEdge& Up = Graph.Edges[Pair.UpperEdge];
			const RoadEdge& Down = Graph.Edges[Pair.LowerEdge];
			const Vec2 At = Points[Pair.UpperEdge][Pair.UpperSegment] + (Points[Pair.UpperEdge][Pair.UpperSegment + 1] - Points[Pair.UpperEdge][Pair.UpperSegment]) * Pair.UpperT;
			Shorts.push_back({ By, Format("[Heights]   %s %s (layer %d%s%s) over %s %s (layer %d%s%s): %.1f m clear where %.1f m is needed, at x %.0f y %.0f",
				Named(Up.Name).c_str(), Up.Highway.c_str(), Up.Layer, Up.bBridge ? ", bridge" : "", Up.bTunnel ? ", tunnel" : "",
				Named(Down.Name).c_str(), Down.Highway.c_str(), Down.Layer, Down.bBridge ? ", bridge" : "", Down.bTunnel ? ", tunnel" : "",
				(Upper - Lower) / 100.0, MinClearance / 100.0, At.X, At.Y) });
		}
		std::sort(Shorts.begin(), Shorts.end(), [](const Short& L, const Short& R) { return L.By > R.By; });
		int32_t JunctionsMoved = 0, StillSteep = 0;
		double MostMoved = 0.0;
		std::vector<std::pair<double, int32_t>> Moves;
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			if (KindOf[NodeIndex] == Kind::Junction && std::abs(X[NodeIndex] - X0[NodeIndex]) > 50.0)
			{
				++JunctionsMoved;
				MostMoved = std::max(MostMoved, std::abs(X[NodeIndex] - X0[NodeIndex]));
				Moves.emplace_back(X[NodeIndex] - X0[NodeIndex], NodeIndex);
			}
		}
		if (bTrace)
		{
			std::sort(Moves.begin(), Moves.end(), [](const auto& L, const auto& R) { return std::abs(L.first) > std::abs(R.first); });
			for (size_t i = 0; i < Moves.size() && i < 8; ++i)
			{
				std::string Roads;
				for (const int32_t EdgeIndex : Graph.Nodes[Moves[i].second].Edges)
				{
					const RoadEdge& Edge = Graph.Edges[EdgeIndex];
					Roads += Format("%s%s %s (layer %d%s%s)", Roads.empty() ? "" : ", ", Named(Edge.Name).c_str(), Edge.Highway.c_str(), Edge.Layer, Edge.bBridge ? ", bridge" : "", Edge.bTunnel ? ", tunnel" : "");
				}
				Log(Format("[Stacks] junction %d moved %.1f m at x %.0f y %.0f: %s", Moves[i].second, Moves[i].first / 100.0, Graph.Nodes[Moves[i].second].Position.X, Graph.Nodes[Moves[i].second].Position.Y, Roads.c_str()));
			}
		}
		// what is still too steep (each road against its own rule, as its grade rows had it), and the ramps left stuck: too
		// steep or too close to another road, for RerouteRamps to give a longer line
		std::vector<StuckRoad> Stuck;
		std::vector<int32_t> StuckOf(NumEdges, -1);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			if (InStack[EdgeIndex] != 1)
			{
				continue;
			}
			const RoadDesign& Rules = In.Design[EdgeIndex];
			const bool bSpan = Edge.bTunnel || Edge.bBridge || Edge.Layer != 0 || Raised[EdgeIndex];
			const bool bRamp = EndsWith(Edge.Highway, "_link");
			double Excess = 0.0, Worst = 0.0, Grade = 0.0;
			Vec2 At;
			for (int32_t i = 0; i + 1 < Count(EdgeIndex); ++i)
			{
				const double L = Length(EdgeIndex, i);
				if (L < 1.0)
				{
					continue;
				}
				Grade = GradeOf(EdgeIndex, i);
				const double Over = std::abs(X[U(EdgeIndex, i + 1)] - X[U(EdgeIndex, i)]) - (Grade * L * 1.02 + 1.0);
				if (Over > 0.0)
				{
					StillSteep += Edge.bTunnel ? 1 : 0;
					Excess += Over;
					if (Over > Worst)
					{
						Worst = Over;
						At = (Points[EdgeIndex][i] + Points[EdgeIndex][i + 1]) * 0.5;
					}
				}
			}
			if (bRamp && Excess > ShortTolerance)
			{
				StuckOf[EdgeIndex] = static_cast<int32_t>(Stuck.size());
				Stuck.push_back({ EdgeIndex, -1, Excess, Grade, At });
			}
		}
		for (const LevelPair& Pair : Pairs)
		{
			const double Upper = Lerp(X[U(Pair.UpperEdge, Pair.UpperSegment)], X[U(Pair.UpperEdge, Pair.UpperSegment + 1)], Pair.UpperT);
			const double Lower = Lerp(X[U(Pair.LowerEdge, Pair.LowerSegment)], X[U(Pair.LowerEdge, Pair.LowerSegment + 1)], Pair.LowerT);
			const double By = MinClearance - (Upper - Lower);
			if (By <= ShortTolerance)
			{
				continue;
			}
			for (const int32_t Ramp : { Pair.UpperEdge, Pair.LowerEdge })
			{
				if (!EndsWith(Graph.Edges[Ramp].Highway, "_link"))
				{
					continue;
				}
				const int32_t Other = Ramp == Pair.UpperEdge ? Pair.LowerEdge : Pair.UpperEdge;
				const int32_t Segment = Ramp == Pair.UpperEdge ? Pair.UpperSegment : Pair.LowerSegment;
				if (StuckOf[Ramp] < 0)
				{
					StuckOf[Ramp] = static_cast<int32_t>(Stuck.size());
					Stuck.push_back({ Ramp, Other, 0.0, Graph.Edges[Ramp].bTunnel ? TunnelGrade : SpanGrade, Points[Ramp][Segment] });
				}
				StuckRoad& Entry = Stuck[StuckOf[Ramp]];
				if (By > Entry.Excess * 0.5)
				{
					Entry.Other = Other;
				}
				Entry.Excess = std::max(Entry.Excess, By);
			}
		}
		if (In.Stuck)
		{
			*In.Stuck = std::move(Stuck);
		}
		Log(Format("[Heights] stacks: %d stacks (%d points, %d roads, %d level pairs, %d rules), %d rounds at most, %d not settled; %d pairs still short (worst %.1f m), %d tunnel pieces still too steep, %d junctions moved over 0.5 m (most %.1f m), %.2f s",
			static_cast<int32_t>(Stacks.size()), NumPoints, NumRoads, static_cast<int32_t>(Pairs.size()), NumRows, MostRounds, Unsettled,
			static_cast<int32_t>(Shorts.size()), Shorts.empty() ? 0.0 : Shorts[0].By / 100.0, StillSteep, JunctionsMoved, MostMoved / 100.0, Seconds() - Start));
		for (size_t i = 0; i < Shorts.size() && i < 10; ++i)
		{
			Log(bTrace ? "[Stacks] short: " + Shorts[i].Text.substr(12) : Shorts[i].Text);
		}
	}
}
