// Geodrive for Godot - GPL (see Core/README.md)

#include "geodrive_car_sound.h"
#include "geodrive_car.h"

#include "GeoCar/CarSound.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream_generator.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace godot;

GeodriveCarSound::GeodriveCarSound() = default;
GeodriveCarSound::~GeodriveCarSound() = default;

void GeodriveCarSound::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_car", "car"), &GeodriveCarSound::set_car);
	ClassDB::bind_method(D_METHOD("get_car"), &GeodriveCarSound::get_car);
	ClassDB::bind_method(D_METHOD("get_level"), &GeodriveCarSound::get_level);
#define GEODRIVE_SOUND_PROPERTY(Name) \
	ClassDB::bind_method(D_METHOD("set_" #Name, "value"), &GeodriveCarSound::set_##Name); \
	ClassDB::bind_method(D_METHOD("get_" #Name), &GeodriveCarSound::get_##Name); \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #Name), "set_" #Name, "get_" #Name);
	GEODRIVE_SOUND_PROPERTY(volume)
	GEODRIVE_SOUND_PROPERTY(engine_gain)
	GEODRIVE_SOUND_PROPERTY(rasp)
	GEODRIVE_SOUND_PROPERTY(pipe)
	GEODRIVE_SOUND_PROPERTY(crackle)
#undef GEODRIVE_SOUND_PROPERTY
}

void GeodriveCarSound::set_car(Node* InCar) { Car = Object::cast_to<GeodriveCar>(InCar); }
Node* GeodriveCarSound::get_car() const { return Car; }
void GeodriveCarSound::set_volume(double V) { Volume = float(V); }
double GeodriveCarSound::get_volume() const { return Volume; }
void GeodriveCarSound::set_engine_gain(double V) { EngineGain = float(V); }
double GeodriveCarSound::get_engine_gain() const { return EngineGain; }
void GeodriveCarSound::set_rasp(double V) { Rasp = float(V); }
double GeodriveCarSound::get_rasp() const { return Rasp; }
void GeodriveCarSound::set_pipe(double V) { Pipe = float(V); }
double GeodriveCarSound::get_pipe() const { return Pipe; }
void GeodriveCarSound::set_crackle(double V) { Crackle = float(V); }
double GeodriveCarSound::get_crackle() const { return Crackle; }

void GeodriveCarSound::_ready()
{
	set_process_mode(PROCESS_MODE_ALWAYS);	// (to fade out while paused rather than stop dead)
	const float Rate = float(AudioServer::get_singleton()->get_mix_rate());
	Synth = std::make_unique<geocar::SoundSynth>(Rate);
	Ref<AudioStreamGenerator> Stream;
	Stream.instantiate();
	Stream->set_mix_rate(Rate);
	Stream->set_buffer_length(0.1f);	// a frame or two ahead, so the engine answers the pedal straight away
	// (from the car: full within ~8 m, as in it or behind it; fainter and duller with distance, as through air; none
	// past 400 m)
	Player = memnew(AudioStreamPlayer3D);
	Player->set_stream(Stream);
	Player->set_attenuation_model(AudioStreamPlayer3D::ATTENUATION_INVERSE_DISTANCE);
	Player->set_unit_size(8.0f);
	Player->set_max_distance(400.0f);
	Player->set_attenuation_filter_cutoff_hz(6000.0f);
	Player->set_attenuation_filter_db(-18.0f);
	Player->set_doppler_tracking(AudioStreamPlayer3D::DOPPLER_TRACKING_DISABLED);
	add_child(Player);
	Player->play();
	Playback = Player->get_stream_playback();
}

void GeodriveCarSound::_process(double)
{
	if (Playback.is_null() || !Synth)
	{
		return;
	}
	if (Car && Car->get_body() != Body)
	{
		Body = Car->get_body();
		Synth->SetEngine(Car->engine_sound());
	}
	geocar::SoundInput Input;
	if (Car && Car->is_inside_tree())
	{
		Input = geocar::SoundFromState(Car->state(), Car->controls().Throttle);
	}
	if (Car && Car->is_inside_tree())
	{
		Player->set_global_position(Car->get_global_position());
	}
	// (quiet: no car, paused, or switched off and stopped turning)
	const bool bOff = Car && !Car->get_engine_on() && Car->get_rpm() < 250.0;
	const bool bQuiet = !Car || !Car->is_inside_tree() || (get_tree() && get_tree()->is_paused()) || bOff;
	geocar::SoundMix Mix;
	Mix.Volume = Volume;
	Mix.EngineGain = EngineGain;
	Mix.RaspGain = Rasp;
	Mix.PipeGain = Pipe;
	Mix.CrackleGain = Crackle;

	const int32_t Frames = int32_t(Playback->get_frames_available());
	if (Frames <= 0)
	{
		return;
	}
	std::vector<float> Samples(static_cast<size_t>(Frames), 0.0f);
	Synth->Generate(Input, Mix, Samples.data(), Frames, 1);
	PackedVector2Array Buffer;
	Buffer.resize(Frames);
	Vector2* Out = Buffer.ptrw();
	const float Step = 1.0f / (0.2f * float(AudioServer::get_singleton()->get_mix_rate()));	// 0.2 s fades
	double Sum = 0.0;
	for (int32_t i = 0; i < Frames; ++i)
	{
		Fade = bQuiet ? std::max(0.0f, Fade - Step) : std::min(1.0f, Fade + Step);
		const float S = Samples[size_t(i)] * Fade;
		Sum += double(S) * S;
		Out[i] = Vector2(S, S);
	}
	Level = float(std::sqrt(Sum / Frames));
	Playback->push_buffer(Buffer);
}
