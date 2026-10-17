// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Tyre model: a simplified port of the TMeasy equations from Project Chrono
// (src/chrono_vehicle/wheeled_vehicle/tire/ChTMeasyTire.cpp), BSD-3-Clause.
// See ThirdPartyNotices.md in this folder for the licence.

#include "Tire.h"

#include <algorithm>
#include <cmath>

namespace geocar
{
	namespace TireImpl
	{
		constexpr float SmallNumber = 1.0e-4f;

		/** Slip velocity floor (m/s): keeps slip finite near standstill */
		constexpr float SlipVelocityFloor = 0.5f;

		/** Below BlendStart m/s the low-speed sticky model takes over completely; above BlendEnd the tyre model does */
		constexpr float BlendStart = 1.0f;
		constexpr float BlendEnd = 3.0f;

		/** Sticky contact stiffness at nominal load (N/m) */
		constexpr float StickStiffness = 300000.0f;

		float Clamp(float V, float Lo, float Hi) { return std::min(std::max(V, Lo), Hi); }

		float SmoothStep(float A, float B, float X)
		{
			if (X < A)
			{
				return 0.0f;
			}
			if (X >= B)
			{
				return 1.0f;
			}
			const float T = (X - A) / (B - A);
			return T * T * (3.0f - 2.0f * T);
		}

		/** Rill's load dependence: value at Load given values at nominal load (X1) and twice nominal (X2) */
		float LoadCurve(float X1, float X2, float Load, float NominalLoad)
		{
			const float Q = Clamp(Load / NominalLoad, 0.0f, 3.0f);
			return std::max(Q * (2.0f * X1 - 0.5f * X2 - (X1 - 0.5f * X2) * Q), 0.0f);
		}

		/** TMeasy force curve: rational rise to the peak, smooth cubic fall to the sliding force, then flat */
		float CombinedForce(float S, float Stiffness, float PeakForce, float PeakSlip, float SlideForce, float SlideSlip)
		{
			if (PeakForce <= SmallNumber || PeakSlip <= SmallNumber)
			{
				return 0.0f;
			}

			// the rational curve needs at least this slope to reach the peak without overshooting
			Stiffness = std::max(Stiffness, 2.0f * PeakForce / PeakSlip);

			if (S < PeakSlip)
			{
				const float Sigma = S / PeakSlip;
				const float P = Stiffness * PeakSlip / PeakForce - 2.0f;
				return Stiffness * PeakSlip * Sigma / (1.0f + (Sigma + P) * Sigma);
			}
			if (S < SlideSlip)
			{
				const float U = (S - PeakSlip) / (SlideSlip - PeakSlip);
				return PeakForce - (PeakForce - SlideForce) * U * U * (3.0f - 2.0f * U);
			}
			return SlideForce;
		}

		/** Normalised pneumatic trail: full at zero slip, slightly negative past the limit, zero when sliding */
		float NormalisedTrail(float AbsSlipY, float TrailFactor, float ZeroSlip, float EndSlip)
		{
			const float W = ZeroSlip / std::max(EndSlip, ZeroSlip);
			if (AbsSlipY <= ZeroSlip)
			{
				const float U = AbsSlipY / ZeroSlip;
				return TrailFactor * ((1.0f - W) * (1.0f - U) + W * (1.0f - (3.0f - 2.0f * U) * U * U));
			}
			if (AbsSlipY <= EndSlip)
			{
				const float Over = (AbsSlipY - ZeroSlip) / ZeroSlip;
				const float Fade = (EndSlip - AbsSlipY) / (EndSlip - ZeroSlip);
				return -TrailFactor * (1.0f - W) * Over * Fade * Fade;
			}
			return 0.0f;
		}
	}

	TireForces ComputeTire(const TireSetup& Setup, float Load, float Vx, float Vy, float Omega, float Radius,
		float DeltaTime, float GripScale, TireState& State)
	{
		using namespace TireImpl;

		TireForces Out;
		if (Load <= 0.0f)
		{
			State = TireState();
			return Out;
		}

		// --- slip ---
		// sliding velocity of the contact patch relative to the road; + slip = wheel faster than the ground (driving)
		const float WheelSpeed = Omega * Radius;
		const float TransportSpeed = std::fabs(WheelSpeed) + SlipVelocityFloor;
		const float SlideVelocityX = Vx - WheelSpeed;

		const float SlipX = -SlideVelocityX / TransportSpeed;
		const float SteadySlipY = -Vy / TransportSpeed;

		// lateral relaxation: side force builds up over the relaxation length as the tyre rolls
		const float RollSpeed = std::max(std::fabs(Vx), std::fabs(WheelSpeed)) + 0.1f;
		const float RelaxRate = RollSpeed / std::max(Setup.RelaxationLength, 0.01f);
		State.SlipY = SteadySlipY + (State.SlipY - SteadySlipY) * std::exp(-RelaxRate * DeltaTime);
		const float SlipY = State.SlipY;

		// --- load-dependent curve parameters ---
		const float Pn = std::max(Setup.NominalLoad, 100.0f);
		const float PeakY = LoadCurve(Setup.PeakForceY, Setup.PeakForceY2, Load, Pn) * GripScale;
		const float PeakX = PeakY * Setup.LongitudinalPeakScale;
		const float StiffY = LoadCurve(Setup.StiffnessY, Setup.StiffnessY2, Load, Pn);
		const float StiffX = LoadCurve(Setup.StiffnessX, Setup.StiffnessX2, Load, Pn);

		// --- combined slip (TMeasy): blend each curve parameter by the slip direction ---
		float ForceX, ForceY, Secant;
		const float S = std::sqrt(SlipX * SlipX + SlipY * SlipY);
		if (S < 1.0e-4f)
		{
			ForceX = StiffX * SlipX;
			ForceY = StiffY * SlipY;
			Secant = StiffX;
		}
		else
		{
			const float C = SlipX / S;
			const float Sn = SlipY / S;
			auto Blend = [C, Sn](float X, float Y) { return std::sqrt((X * C) * (X * C) + (Y * Sn) * (Y * Sn)); };

			const float Stiffness = Blend(StiffX, StiffY);
			const float PeakForce = Blend(PeakX, PeakY);
			const float PeakSlip = Blend(Setup.PeakSlipX, Setup.PeakSlipY);
			const float SlideSlip = std::max(Blend(Setup.SlideSlipX, Setup.SlideSlipY), PeakSlip * 1.05f);
			const float SlideForce = PeakForce * Setup.SlideRatio;

			const float F = CombinedForce(S, Stiffness, PeakForce, PeakSlip, SlideForce, SlideSlip);
			ForceX = F * C;
			ForceY = F * Sn;
			Secant = F / S;
		}

		// increasing wheel speed increases slip ratio, which increases drive force
		float DForceXDOmega = Secant * Radius / TransportSpeed;

		// --- low-speed sticky model: a damped spring anchored to the road, capped at peak friction ---
		const float GroundSpeed = std::sqrt(Vx * Vx + Vy * Vy);
		const float SpeedBlend = SmoothStep(BlendStart, BlendEnd, std::max(GroundSpeed, std::fabs(WheelSpeed)));
		if (SpeedBlend < 1.0f)
		{
			const float LoadRatio = Load / Pn;
			const float K = StickStiffness * LoadRatio;
			const float Damping = 2.0f * std::sqrt(K * Load / 9.81f);

			State.StickX += SlideVelocityX * DeltaTime;
			State.StickY += Vy * DeltaTime;
			float StickForceX = -(State.StickX * K + SlideVelocityX * Damping);
			float StickForceY = -(State.StickY * K + Vy * Damping);

			const float MaxForce = PeakY;
			const float Magnitude = std::sqrt(StickForceX * StickForceX + StickForceY * StickForceY);
			if (Magnitude > MaxForce && Magnitude > SmallNumber)
			{
				// sliding: cap the force and move the anchor so the spring doesn't wind up
				StickForceX *= MaxForce / Magnitude;
				StickForceY *= MaxForce / Magnitude;
				State.StickX = -(StickForceX + SlideVelocityX * Damping) / K;
				State.StickY = -(StickForceY + Vy * Damping) / K;
			}

			ForceX = StickForceX + (ForceX - StickForceX) * SpeedBlend;
			ForceY = StickForceY + (ForceY - StickForceY) * SpeedBlend;
			const float StickDerivative = (Damping + K * DeltaTime) * Radius;
			DForceXDOmega = StickDerivative + (DForceXDOmega - StickDerivative) * SpeedBlend;
		}
		else
		{
			State.StickX = State.StickY = 0.0f;
		}

		// --- pneumatic trail from contact patch length ---
		const float Deflection = Load / std::max(Setup.VerticalStiffness, 1000.0f);
		const float ContactLength = 2.0f * std::sqrt(Radius * Deflection);
		const float Trail = ContactLength * NormalisedTrail(std::fabs(SlipY), Setup.TrailFactor, Setup.TrailZeroSlip, Setup.TrailEndSlip);

		Out.ForceX = ForceX;
		Out.ForceY = ForceY;
		Out.DForceXDOmega = std::max(DForceXDOmega, 0.0f);
		Out.Trail = Trail * SpeedBlend;
		Out.SlipX = SlipX;
		Out.SlipY = SlipY;
		Out.SpeedBlend = SpeedBlend;
		return Out;
	}
}
