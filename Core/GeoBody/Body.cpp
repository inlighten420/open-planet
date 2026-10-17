// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "Body.h"

#include <algorithm>

namespace geobody
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;

		Mat3 Skew(const Vec3& V)
		{
			Mat3 Out;
			Out.M[0][1] = -V.Z; Out.M[0][2] = V.Y;
			Out.M[1][0] = V.Z;  Out.M[1][2] = -V.X;
			Out.M[2][0] = -V.Y; Out.M[2][1] = V.X;
			return Out;
		}

		/** A solid shape's inertia about its middle (its own axes), for a mass */
		Vec3 SolidInertia(Shape Form, const Vec3& Size, double Mass)
		{
			switch (Form)
			{
			case Shape::Box:
				return Vec3(Size.Y * Size.Y + Size.Z * Size.Z, Size.X * Size.X + Size.Z * Size.Z, Size.X * Size.X + Size.Y * Size.Y) * (Mass / 12.0);
			case Shape::Cylinder:
			{
				const double R2 = Size.Y * Size.Y * 0.25, L2 = Size.X * Size.X;
				return Vec3(Mass * R2 * 0.5, Mass * (3.0 * R2 + L2) / 12.0, Mass * (3.0 * R2 + L2) / 12.0);
			}
			case Shape::Sphere:
			default:
			{
				const double I = 0.4 * Mass * Size.X * Size.X * 0.25;
				return Vec3(I, I, I);
			}
			}
		}

		double SolidVolume(Shape Form, const Vec3& Size)
		{
			switch (Form)
			{
			case Shape::Box: return std::max(Size.X, 0.0) * std::max(Size.Y, 0.0) * std::max(Size.Z, 0.0);
			case Shape::Cylinder: return Pi * Size.Y * Size.Y * 0.25 * std::max(Size.X, 0.0);
			case Shape::Sphere:
			default: return Pi / 6.0 * Size.X * Size.X * Size.X;
			}
		}

		/** A shell's hollow: its outside less the wall all round (a cylinder's ends closed, as a tank's) */
		Vec3 InnerSize(const Part& P)
		{
			const double T2 = 2.0 * P.Wall;
			switch (P.Form)
			{
			case Shape::Box: return Vec3(std::max(P.Size.X - T2, 0.0), std::max(P.Size.Y - T2, 0.0), std::max(P.Size.Z - T2, 0.0));
			case Shape::Cylinder: return Vec3(std::max(P.Size.X - T2, 0.0), std::max(P.Size.Y - T2, 0.0), 0.0);
			case Shape::Sphere:
			default: return Vec3(std::max(P.Size.X - T2, 0.0), 0.0, 0.0);
			}
		}

		bool Inside(Shape Form, const Vec3& Size, const Vec3& P)
		{
			switch (Form)
			{
			case Shape::Box: return true;
			case Shape::Cylinder: return P.Y * P.Y + P.Z * P.Z <= Size.Y * Size.Y * 0.25;
			case Shape::Sphere:
			default: return P.Dot(P) <= Size.X * Size.X * 0.25;
			}
		}

		Vec3 Extent(const Part& P)
		{
			switch (P.Form)
			{
			case Shape::Box: return P.Size;
			case Shape::Cylinder: return Vec3(P.Size.X, P.Size.Y, P.Size.Y);
			case Shape::Sphere:
			default: return Vec3(P.Size.X, P.Size.X, P.Size.X);
			}
		}
	}

	// --- the maths ---

	Mat3 Mat3::Diagonal(const Vec3& D)
	{
		Mat3 Out;
		Out.M[0][0] = D.X;
		Out.M[1][1] = D.Y;
		Out.M[2][2] = D.Z;
		return Out;
	}

	Vec3 Mat3::operator*(const Vec3& V) const
	{
		return { M[0][0] * V.X + M[0][1] * V.Y + M[0][2] * V.Z, M[1][0] * V.X + M[1][1] * V.Y + M[1][2] * V.Z, M[2][0] * V.X + M[2][1] * V.Y + M[2][2] * V.Z };
	}

	Mat3 Mat3::operator*(const Mat3& B) const
	{
		Mat3 Out;
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				Out.M[i][j] = M[i][0] * B.M[0][j] + M[i][1] * B.M[1][j] + M[i][2] * B.M[2][j];
			}
		}
		return Out;
	}

	Mat3 Mat3::operator+(const Mat3& B) const
	{
		Mat3 Out;
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				Out.M[i][j] = M[i][j] + B.M[i][j];
			}
		}
		return Out;
	}

	Mat3 Mat3::operator*(double S) const
	{
		Mat3 Out;
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				Out.M[i][j] = M[i][j] * S;
			}
		}
		return Out;
	}

	Mat3 Mat3::Transposed() const
	{
		Mat3 Out;
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				Out.M[i][j] = M[j][i];
			}
		}
		return Out;
	}

	Mat3 Mat3::Inverse() const
	{
		const double A = M[1][1] * M[2][2] - M[1][2] * M[2][1];
		const double B = M[1][2] * M[2][0] - M[1][0] * M[2][2];
		const double C = M[1][0] * M[2][1] - M[1][1] * M[2][0];
		const double Det = M[0][0] * A + M[0][1] * B + M[0][2] * C;
		Mat3 Out;
		if (std::abs(Det) < 1e-30)
		{
			return Out;
		}
		const double I = 1.0 / Det;
		Out.M[0][0] = A * I;
		Out.M[0][1] = (M[0][2] * M[2][1] - M[0][1] * M[2][2]) * I;
		Out.M[0][2] = (M[0][1] * M[1][2] - M[0][2] * M[1][1]) * I;
		Out.M[1][0] = B * I;
		Out.M[1][1] = (M[0][0] * M[2][2] - M[0][2] * M[2][0]) * I;
		Out.M[1][2] = (M[0][2] * M[1][0] - M[0][0] * M[1][2]) * I;
		Out.M[2][0] = C * I;
		Out.M[2][1] = (M[0][1] * M[2][0] - M[0][0] * M[2][1]) * I;
		Out.M[2][2] = (M[0][0] * M[1][1] - M[0][1] * M[1][0]) * I;
		return Out;
	}

	Vec3 Quat::Rotate(const Vec3& V) const
	{
		const Vec3 Q(X, Y, Z);
		const Vec3 T = Q.Cross(V) * 2.0;
		return V + T * W + Q.Cross(T);
	}

	Vec3 Quat::Unrotate(const Vec3& V) const
	{
		const Quat C{ W, -X, -Y, -Z };
		return C.Rotate(V);
	}

	Quat Quat::operator*(const Quat& B) const
	{
		return { W * B.W - X * B.X - Y * B.Y - Z * B.Z, W * B.X + X * B.W + Y * B.Z - Z * B.Y, W * B.Y - X * B.Z + Y * B.W + Z * B.X,
			W * B.Z + X * B.Y - Y * B.X + Z * B.W };
	}

	Quat Quat::Normal() const
	{
		const double L = std::sqrt(W * W + X * X + Y * Y + Z * Z);
		return L > 1e-12 ? Quat{ W / L, X / L, Y / L, Z / L } : Quat();
	}

	Mat3 Quat::Matrix() const
	{
		Mat3 Out;
		const Vec3 A = Rotate(Vec3(1.0, 0.0, 0.0)), B = Rotate(Vec3(0.0, 1.0, 0.0)), C = Rotate(Vec3(0.0, 0.0, 1.0));
		const Vec3 Columns[3] = { A, B, C };
		for (int j = 0; j < 3; ++j)
		{
			Out.M[0][j] = Columns[j].X;
			Out.M[1][j] = Columns[j].Y;
			Out.M[2][j] = Columns[j].Z;
		}
		return Out;
	}

	Quat Quat::FromAxisAngle(const Vec3& Axis, double Angle)
	{
		const Vec3 N = Axis.Normal();
		const double S = std::sin(Angle * 0.5);
		return { std::cos(Angle * 0.5), N.X * S, N.Y * S, N.Z * S };
	}

	Quat Quat::FromAxes(const Vec3& X, const Vec3& Y, const Vec3& Z)
	{
		// (the matrix with these columns, as a quaternion: Shepperd's way, from its largest term)
		const double M[3][3] = { { X.X, Y.X, Z.X }, { X.Y, Y.Y, Z.Y }, { X.Z, Y.Z, Z.Z } };
		const double Trace = M[0][0] + M[1][1] + M[2][2];
		Quat Q;
		if (Trace > 0.0)
		{
			const double S = std::sqrt(Trace + 1.0) * 2.0;
			Q = { 0.25 * S, (M[2][1] - M[1][2]) / S, (M[0][2] - M[2][0]) / S, (M[1][0] - M[0][1]) / S };
		}
		else if (M[0][0] > M[1][1] && M[0][0] > M[2][2])
		{
			const double S = std::sqrt(1.0 + M[0][0] - M[1][1] - M[2][2]) * 2.0;
			Q = { (M[2][1] - M[1][2]) / S, 0.25 * S, (M[0][1] + M[1][0]) / S, (M[0][2] + M[2][0]) / S };
		}
		else if (M[1][1] > M[2][2])
		{
			const double S = std::sqrt(1.0 + M[1][1] - M[0][0] - M[2][2]) * 2.0;
			Q = { (M[0][2] - M[2][0]) / S, (M[0][1] + M[1][0]) / S, 0.25 * S, (M[1][2] + M[2][1]) / S };
		}
		else
		{
			const double S = std::sqrt(1.0 + M[2][2] - M[0][0] - M[1][1]) * 2.0;
			Q = { (M[1][0] - M[0][1]) / S, (M[0][2] + M[2][0]) / S, (M[1][2] + M[2][1]) / S, 0.25 * S };
		}
		return Q.Normal();
	}

	// --- parts ---

	double Part::GetOuterVolume() const
	{
		return SolidVolume(Form, Size);
	}

	double Part::GetVolume() const
	{
		return Wall > 0.0 ? SolidVolume(Form, Size) - SolidVolume(Form, InnerSize(*this)) : SolidVolume(Form, Size);
	}

	double Part::GetMass() const
	{
		return Mass > 0.0 ? Mass : GetVolume() * Of.Density;
	}

	Mat3 Part::GetInertia() const
	{
		Vec3 Own;
		if (Mass > 0.0 || Wall <= 0.0)
		{
			Own = SolidInertia(Form, Size, GetMass());	// (a lump: as if its mass were even through its room)
		}
		else
		{
			// a shell: its outside, solid, less its hollow
			const Vec3 InSize = InnerSize(*this);
			Own = SolidInertia(Form, Size, SolidVolume(Form, Size) * Of.Density) - SolidInertia(Form, InSize, SolidVolume(Form, InSize) * Of.Density);
		}
		const Mat3 R = Turn.Matrix();
		return R * Mat3::Diagonal(Own) * R.Transposed();
	}

	Part Box(const std::string& Name, const Vec3& At, const Vec3& Size, const Material& Of, double Wall)
	{
		Part P;
		P.Name = Name;
		P.Form = Shape::Box;
		P.At = At;
		P.Size = Size;
		P.Of = Of;
		P.Wall = Wall;
		return P;
	}

	Part Cylinder(const std::string& Name, const Vec3& At, double Length, double Diameter, const Vec3& Axis, const Material& Of, double Wall)
	{
		Part P;
		P.Name = Name;
		P.Form = Shape::Cylinder;
		P.At = At;
		P.Size = Vec3(Length, Diameter, Diameter);
		P.Of = Of;
		P.Wall = Wall;
		// its own x along the axis
		const Vec3 A = Axis.Normal(), X(1.0, 0.0, 0.0);
		const Vec3 C = X.Cross(A);
		const double D = X.Dot(A);
		if (C.Length() > 1e-9)
		{
			P.Turn = Quat::FromAxisAngle(C, std::atan2(C.Length(), D));
		}
		else if (D < 0.0)
		{
			P.Turn = Quat::FromAxisAngle(Vec3(0.0, 0.0, 1.0), Pi);
		}
		return P;
	}

	Part Sphere(const std::string& Name, const Vec3& At, double Diameter, const Material& Of, double Wall)
	{
		Part P;
		P.Name = Name;
		P.Form = Shape::Sphere;
		P.At = At;
		P.Size = Vec3(Diameter, Diameter, Diameter);
		P.Of = Of;
		P.Wall = Wall;
		return P;
	}

	Part Lump(const std::string& Name, const Vec3& At, const Vec3& Size, double Kg, const Material& MostlyOf)
	{
		Part P = Box(Name, At, Size, MostlyOf);
		P.Mass = Kg;
		return P;
	}

	MassProperties Measure(const std::vector<Part>& Parts)
	{
		MassProperties Out;
		Vec3 Moment;
		for (const Part& P : Parts)
		{
			const double M = P.GetMass();
			Out.Mass += M;
			Moment += P.At * M;
			Out.Displaced += P.GetDisplaced();
		}
		if (Out.Mass <= 0.0)
		{
			return Out;
		}
		Out.CentreOfMass = Moment * (1.0 / Out.Mass);
		// each part's own, and its mass's at its distance from the centre (the parallel axis theorem)
		for (const Part& P : Parts)
		{
			const double M = P.GetMass();
			const Vec3 D = P.At - Out.CentreOfMass;
			Mat3 Offset = Mat3::Diagonal(Vec3(1.0, 1.0, 1.0) * D.Dot(D));
			const double Dv[3] = { D.X, D.Y, D.Z };
			for (int i = 0; i < 3; ++i)
			{
				for (int j = 0; j < 3; ++j)
				{
					Offset.M[i][j] -= Dv[i] * Dv[j];
				}
			}
			Out.Inertia = Out.Inertia + P.GetInertia() + Offset * M;
		}
		return Out;
	}

	// --- the body ---

	void Body::SetParts(std::vector<Part> InParts)
	{
		const Vec3 WasOrigin = Origin();
		Parts = std::move(InParts);
		Props = Measure(Parts);
		InverseInertia = Props.Inertia.Inverse();
		Pos = WasOrigin + Rot.Rotate(Props.CentreOfMass);
		// the points the water's checked at: a grid through each part (those inside its shape), each standing for an
		// equal share of what the part displaces
		Samples.clear();
		for (const Part& P : Parts)
		{
			const int32_t N = 3;
			const Vec3 E = Extent(P);
			std::vector<Vec3> Points;
			for (int32_t i = 0; i < N; ++i)
			{
				for (int32_t j = 0; j < N; ++j)
				{
					for (int32_t k = 0; k < N; ++k)
					{
						const Vec3 Own((i + 0.5) / N - 0.5, (j + 0.5) / N - 0.5, (k + 0.5) / N - 0.5);
						const Vec3 L(Own.X * E.X, Own.Y * E.Y, Own.Z * E.Z);
						if (Inside(P.Form, P.Size, L))
						{
							Points.push_back(P.At + P.Turn.Rotate(L));
						}
					}
				}
			}
			if (Points.empty())
			{
				Points.push_back(P.At);
			}
			Samples.push_back(std::move(Points));
		}
	}

	void Body::Place(const Vec3& InOrigin, const Quat& Rotation)
	{
		Rot = Rotation.Normal();
		Pos = InOrigin + Rot.Rotate(Props.CentreOfMass);
		Vel = Vec3();
		Omega = Vec3();
	}

	void Body::Push(const Vec3& InForce, const Vec3& WorldPoint)
	{
		Force += InForce;
		Torque += (WorldPoint - Pos).Cross(InForce);
	}

	void Body::Twist(const Vec3& WorldTorque)
	{
		Torque += WorldTorque;
	}

	double Body::Energy() const
	{
		return 0.5 * Props.Mass * Vel.Dot(Vel) + 0.5 * Omega.Dot(Props.Inertia * Omega);
	}

	void Body::Step(double Seconds, const Surroundings& Round, const PushFn& Also)
	{
		if (Props.Mass <= 0.0 || Seconds <= 0.0)
		{
			return;
		}
		const int32_t Steps = std::max(1, int32_t(std::ceil(Seconds * 240.0 - 1e-9)));
		for (int32_t i = 0; i < Steps; ++i)
		{
			Substep(Seconds / Steps, Round, Also);
		}
	}

	void Body::Float(const Surroundings& Round)
	{
		// Archimedes: each point under the surface lifted by the water it stands for (a point's share of its part's
		// displacement, as a slice of it, so it goes in smoothly), where it is; and the water's drag on it, roughly
		// (each slice's face, Cd 1: enough to settle a body as water would; a hull's own drag is a force part's)
		Under = 0.0;
		if (!Round.Water)
		{
			return;
		}
		const Vec3 Up = (-Round.Gravity).Normal();
		const double G = Round.Gravity.Length();
		for (size_t p = 0; p < Parts.size(); ++p)
		{
			const std::vector<Vec3>& Points = Samples[p];
			const Part& P = Parts[p];
			const double Share = P.GetDisplaced() / double(Points.size());
			if (Share <= 0.0)
			{
				continue;
			}
			// each point's slice of the part, as tall as the slice stands now (a flat part's slices are flat) and as
			// wide as its share over that
			const Vec3 Cell = Extent(P) * (1.0 / 3.0);
			const Quat Turned = Rot * P.Turn;
			const double Tall = std::abs(Turned.Rotate(Vec3(Cell.X, 0.0, 0.0)).Dot(Up)) + std::abs(Turned.Rotate(Vec3(0.0, Cell.Y, 0.0)).Dot(Up))
				+ std::abs(Turned.Rotate(Vec3(0.0, 0.0, Cell.Z)).Dot(Up));
			const double Face = Share / std::max(Tall, 1e-6);
			for (const Vec3& Local : Points)
			{
				const Vec3 At = ToWorld(Local);
				double Level = 0.0;
				if (!Round.Water(At.X, At.Y, Level))
				{
					continue;
				}
				const double Depth = std::clamp((Level - At.Z) / Tall + 0.5, 0.0, 1.0);
				if (Depth <= 0.0)
				{
					continue;
				}
				const double Volume = Share * Depth;
				Under += Volume;
				Push(Up * (Round.WaterDensity * G * Volume), At);
				const Vec3 V = VelocityAt(At);
				Push(V * (-0.5 * Round.WaterDensity * Face * Depth * V.Length()), At);
			}
		}
	}

	void Body::Substep(double Dt, const Surroundings& Round, const PushFn& Also)
	{
		Force = Round.Gravity * Props.Mass;
		Torque = Vec3();
		Float(Round);
		for (const std::shared_ptr<ForcePart>& F : Forces)
		{
			F->Apply(*this, Round, Dt);
		}
		if (Also)
		{
			Also(*this, Dt);
		}

		// moving: semi-implicit (the new speed moves it)
		Vel += Force * (Dt / Props.Mass);
		Pos += Vel * Dt;

		// spinning, in its own frame: the twist, then the spin's own effect (w x Iw) solved implicitly (one Newton step,
		// as Catto's), so a fast spin stays stable and a tumbling box tumbles as a real one does
		const Mat3& I = Props.Inertia;
		Vec3 W = Omega + InverseInertia * Rot.Unrotate(Torque) * Dt;
		const Vec3 W0 = W;
		const Vec3 F = I * (W - W0) + W.Cross(I * W) * Dt;
		const Mat3 J = I + (Skew(W) * I + Skew(I * W) * -1.0) * Dt;
		W -= J.Inverse() * F;
		Omega = W;
		// turning: q' = q (1 + w dt / 2)
		const Quat Spin{ 0.0, Omega.X * Dt * 0.5, Omega.Y * Dt * 0.5, Omega.Z * Dt * 0.5 };
		const Quat Turned = Rot * Spin;
		Rot = Quat{ Rot.W + Turned.W, Rot.X + Turned.X, Rot.Y + Turned.Y, Rot.Z + Turned.Z }.Normal();
	}
}
