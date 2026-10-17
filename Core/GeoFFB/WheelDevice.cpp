// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "WheelDevice.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <string>

const char* geoffb::AxisName(int32_t Axis)
{
	static const char* Names[NumAxes] = { "X", "Y", "Z", "RX", "RY", "RZ", "Slider0", "Slider1" };
	return (Axis >= 0 && Axis < NumAxes) ? Names[Axis] : "None";
}

double geoffb::Now()
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

#pragma comment(lib, "winmm.lib")
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int Period);
extern "C" __declspec(dllimport) unsigned int __stdcall timeEndPeriod(unsigned int Period);

void geoffb::BeginLoopTiming()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	timeBeginPeriod(1);	// 1 ms sleeps (the system default is ~15.6 ms, which would hold the loop to ~64 Hz)
}

void geoffb::EndLoopTiming()
{
	timeEndPeriod(1);
}

namespace geoffb
{
	namespace WheelDeviceImpl
	{
		/** Ignore force changes smaller than this (in DirectInput units, 0-10000) to avoid flooding the driver */
		constexpr LONG ForceEpsilon = 20;
		constexpr LONG Unset = LONG_MAX;

		struct EnumContext
		{
			GUID Guid = {};
			std::wstring Name;
			bool bFound = false;
		};

		BOOL CALLBACK EnumFirstDevice(LPCDIDEVICEINSTANCEW Instance, LPVOID Context)
		{
			EnumContext* Ctx = static_cast<EnumContext*>(Context);
			Ctx->Guid = Instance->guidInstance;
			Ctx->Name = Instance->tszProductName;
			Ctx->bFound = true;
			return DIENUM_STOP;
		}

		template <typename T>
		void InitHeader(T& Prop, DWORD How = DIPH_DEVICE, DWORD Obj = 0)
		{
			Prop.diph.dwSize = sizeof(T);
			Prop.diph.dwHeaderSize = sizeof(DIPROPHEADER);
			Prop.diph.dwHow = How;
			Prop.diph.dwObj = Obj;
		}

		std::string Utf8(const std::wstring& Text)
		{
			if (Text.empty())
			{
				return std::string();
			}
			const int Size = WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), int(Text.size()), nullptr, 0, nullptr, nullptr);
			std::string Out(size_t(Size), '\0');
			WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), int(Text.size()), Out.data(), Size, nullptr, nullptr);
			return Out;
		}

		LONG Round(float V) { return LONG(std::lround(V)); }
	}

	struct WheelDevice::Impl
	{
		IDirectInput8W* DirectInput = nullptr;
		IDirectInputDevice8W* Device = nullptr;
		IDirectInputEffect* ConstantEffect = nullptr;
		IDirectInputEffect* DamperEffect = nullptr;
		IDirectInputEffect* FrictionEffect = nullptr;

		std::string Name;
		bool bHasFFB = false;

		LONG LastForce = WheelDeviceImpl::Unset;
		LONG LastDamper = WheelDeviceImpl::Unset;
		LONG LastFriction = WheelDeviceImpl::Unset;

		/** Creates an infinite effect on the X (steering) axis */
		IDirectInputEffect* CreateEffect(REFGUID Type, DWORD ParamsSize, void* Params)
		{
			DWORD Axes[1] = { DIJOFS_X };
			LONG Direction[1] = { 0 };

			DIEFFECT Effect = {};
			Effect.dwSize = sizeof(DIEFFECT);
			Effect.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
			Effect.dwDuration = INFINITE;
			Effect.dwGain = DI_FFNOMINALMAX;
			Effect.dwTriggerButton = DIEB_NOTRIGGER;
			Effect.cAxes = 1;
			Effect.rgdwAxes = Axes;
			Effect.rglDirection = Direction;
			Effect.cbTypeSpecificParams = ParamsSize;
			Effect.lpvTypeSpecificParams = Params;

			IDirectInputEffect* Result = nullptr;
			if (FAILED(Device->CreateEffect(Type, &Effect, &Result, nullptr)))
			{
				return nullptr;
			}
			Result->Start(1, 0);
			return Result;
		}

		void StartEffects()
		{
			for (IDirectInputEffect* Effect : { ConstantEffect, DamperEffect, FrictionEffect })
			{
				if (Effect)
				{
					Effect->Start(1, 0);
				}
			}
		}

		void UpdateEffect(IDirectInputEffect* Effect, DWORD ParamsSize, void* Params)
		{
			DIEFFECT Update = {};
			Update.dwSize = sizeof(DIEFFECT);
			Update.cbTypeSpecificParams = ParamsSize;
			Update.lpvTypeSpecificParams = Params;

			const HRESULT Result = Effect->SetParameters(&Update, DIEP_TYPESPECIFICPARAMS);
			if (Result == DIERR_INPUTLOST || Result == DIERR_NOTACQUIRED)
			{
				// the device was lost (a focus change): get it back, restart the effects, try once more
				if (SUCCEEDED(Device->Acquire()))
				{
					StartEffects();
					Effect->SetParameters(&Update, DIEP_TYPESPECIFICPARAMS);
				}
			}
		}

		/** Sets a condition effect's coefficient (damper, friction), skipping tiny changes */
		void UpdateCondition(IDirectInputEffect* Effect, LONG& LastCoefficient, float Strength)
		{
			if (!Effect)
			{
				return;
			}
			const LONG Coefficient = WheelDeviceImpl::Round(std::clamp(Strength, 0.0f, 1.0f) * DI_FFNOMINALMAX);
			if (LastCoefficient != WheelDeviceImpl::Unset && std::labs(Coefficient - LastCoefficient) < WheelDeviceImpl::ForceEpsilon && !(Coefficient == 0 && LastCoefficient != 0))
			{
				return;
			}
			LastCoefficient = Coefficient;

			DICONDITION Condition = {};
			Condition.lPositiveCoefficient = Coefficient;
			Condition.lNegativeCoefficient = Coefficient;
			Condition.dwPositiveSaturation = DI_FFNOMINALMAX;
			Condition.dwNegativeSaturation = DI_FFNOMINALMAX;
			UpdateEffect(Effect, sizeof(DICONDITION), &Condition);
		}
	};

	WheelDevice::WheelDevice() : Device(std::make_unique<Impl>()) {}

	WheelDevice::~WheelDevice()
	{
		Close();
	}

	bool WheelDevice::IsOpen() const
	{
		return Device->Device != nullptr;
	}

	bool WheelDevice::Open(void* Window, const std::function<void(const std::string&)>& Log)
	{
		using namespace WheelDeviceImpl;
		Close();
		Impl& D = *Device;

		if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&D.DirectInput), nullptr)))
		{
			if (Log)
			{
				Log("DirectInput8Create failed");
			}
			return false;
		}

		// a force-feedback wheel first, else any game controller (input only)
		EnumContext Ctx;
		D.DirectInput->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumFirstDevice, &Ctx, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
		D.bHasFFB = Ctx.bFound;
		if (!Ctx.bFound)
		{
			D.DirectInput->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumFirstDevice, &Ctx, DIEDFL_ATTACHEDONLY);
		}
		if (!Ctx.bFound || FAILED(D.DirectInput->CreateDevice(Ctx.Guid, &D.Device, nullptr)))
		{
			Close();
			return false;
		}
		D.Name = Utf8(Ctx.Name);

		// exclusive access is needed for force feedback; background, so it keeps reading when another window has focus
		if (FAILED(D.Device->SetDataFormat(&c_dfDIJoystick2))
			|| FAILED(D.Device->SetCooperativeLevel(static_cast<HWND>(Window), DISCL_EXCLUSIVE | DISCL_BACKGROUND)))
		{
			if (Log)
			{
				Log("Could not configure " + D.Name);
			}
			Close();
			return false;
		}

		// the same range on every axis
		DIPROPRANGE Range = {};
		InitHeader(Range);
		Range.lMin = AxisMin;
		Range.lMax = AxisMax;
		D.Device->SetProperty(DIPROP_RANGE, &Range.diph);

		// the wheel's own centring spring would fight our forces
		DIPROPDWORD AutoCenter = {};
		InitHeader(AutoCenter);
		AutoCenter.dwData = DIPROPAUTOCENTER_OFF;
		D.Device->SetProperty(DIPROP_AUTOCENTER, &AutoCenter.diph);

		D.Device->Acquire();

		if (D.bHasFFB)
		{
			DICONSTANTFORCE Constant = {};
			D.ConstantEffect = D.CreateEffect(GUID_ConstantForce, sizeof(DICONSTANTFORCE), &Constant);
			DICONDITION Condition = {};
			D.DamperEffect = D.CreateEffect(GUID_Damper, sizeof(DICONDITION), &Condition);
			D.FrictionEffect = D.CreateEffect(GUID_Friction, sizeof(DICONDITION), &Condition);
			if (!D.ConstantEffect && Log)
			{
				Log(D.Name + ": could not create the constant force effect");
			}
		}

		if (Log)
		{
			Log("Opened " + D.Name + " (force feedback: " + (D.bHasFFB ? "yes" : "no") + ")");
		}
		return true;
	}

	void WheelDevice::Close()
	{
		Impl& D = *Device;
		if (D.Device)
		{
			SetConstantForce(0.0f);
			SetDamper(0.0f);
			SetFriction(0.0f);
		}
		for (IDirectInputEffect** Effect : { &D.ConstantEffect, &D.DamperEffect, &D.FrictionEffect })
		{
			if (*Effect)
			{
				(*Effect)->Stop();
				(*Effect)->Release();
				*Effect = nullptr;
			}
		}
		if (D.Device)
		{
			D.Device->Unacquire();
			D.Device->Release();
			D.Device = nullptr;
		}
		if (D.DirectInput)
		{
			D.DirectInput->Release();
			D.DirectInput = nullptr;
		}
		D.LastForce = D.LastDamper = D.LastFriction = WheelDeviceImpl::Unset;
	}

	bool WheelDevice::Poll(WheelInput& Out)
	{
		Impl& D = *Device;
		if (!D.Device)
		{
			return false;
		}
		if (FAILED(D.Device->Poll()))
		{
			if (FAILED(D.Device->Acquire()))
			{
				return false;
			}
			D.StartEffects();
			D.LastForce = D.LastDamper = D.LastFriction = WheelDeviceImpl::Unset;
			D.Device->Poll();
		}

		DIJOYSTATE2 State = {};
		if (FAILED(D.Device->GetDeviceState(sizeof(DIJOYSTATE2), &State)))
		{
			return false;
		}

		Out.bConnected = true;
		Out.bHasFFB = D.ConstantEffect != nullptr;
		Out.DeviceName = D.Name;
		const LONG Axes[NumAxes] = { State.lX, State.lY, State.lZ, State.lRx, State.lRy, State.lRz, State.rglSlider[0], State.rglSlider[1] };
		for (int32_t i = 0; i < NumAxes; ++i)
		{
			Out.Axes[i] = Axes[i];
		}
		for (int32_t i = 0; i < NumButtons; ++i)
		{
			Out.Buttons[i] = (State.rgbButtons[i] & 0x80) != 0;
		}
		for (int32_t i = 0; i < NumHats; ++i)
		{
			Out.Hats[i] = LOWORD(State.rgdwPOV[i]) == 0xFFFF ? -1 : int32_t(State.rgdwPOV[i]);
		}
		return true;
	}

	void WheelDevice::SetConstantForce(float Force)
	{
		Impl& D = *Device;
		if (!D.ConstantEffect)
		{
			return;
		}
		const LONG Magnitude = WheelDeviceImpl::Round(std::clamp(Force, -1.0f, 1.0f) * DI_FFNOMINALMAX);
		if (D.LastForce != WheelDeviceImpl::Unset && std::labs(Magnitude - D.LastForce) < WheelDeviceImpl::ForceEpsilon && !(Magnitude == 0 && D.LastForce != 0))
		{
			return;
		}
		D.LastForce = Magnitude;
		DICONSTANTFORCE Constant = { Magnitude };
		D.UpdateEffect(D.ConstantEffect, sizeof(DICONSTANTFORCE), &Constant);
	}

	void WheelDevice::SetDamper(float Damper)
	{
		Device->UpdateCondition(Device->DamperEffect, Device->LastDamper, Damper);
	}

	void WheelDevice::SetFriction(float Friction)
	{
		Device->UpdateCondition(Device->FrictionEffect, Device->LastFriction, Friction);
	}
}

#else // not Windows: no wheel (yet)

void geoffb::BeginLoopTiming() {}
void geoffb::EndLoopTiming() {}

namespace geoffb
{
	struct WheelDevice::Impl {};
	WheelDevice::WheelDevice() : Device(std::make_unique<Impl>()) {}
	WheelDevice::~WheelDevice() {}
	bool WheelDevice::Open(void*, const std::function<void(const std::string&)>&) { return false; }
	void WheelDevice::Close() {}
	bool WheelDevice::IsOpen() const { return false; }
	bool WheelDevice::Poll(WheelInput&) { return false; }
	void WheelDevice::SetConstantForce(float) {}
	void WheelDevice::SetDamper(float) {}
	void WheelDevice::SetFriction(float) {}
}

#endif
