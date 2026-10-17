// Geodrive for Godot - GPL (see Core/README.md)
//
// GeodriveCarSound: the car's sound, synthesised live by Geodrive's Core (Core/GeoCar/CarSound: the engine firing into
// its exhaust, turbo, pops, tyre squeal, road rumble and wind), the same synthesiser the Unreal game uses. Give it the
// car; it fills a Godot audio stream generator a frame ahead, and goes quiet while the game is paused.

#pragma once

#include <godot_cpp/classes/audio_stream_generator_playback.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/node.hpp>

#include <memory>

namespace geocar { class SoundSynth; }

namespace godot
{
	class GeodriveCar;

	class GeodriveCarSound : public Node
	{
		GDCLASS(GeodriveCarSound, Node)

	public:
		GeodriveCarSound();
		~GeodriveCarSound();

		void set_car(Node* Car);
		Node* get_car() const;

		/** The menu's sound settings (1 = as the car is meant to sound): volume 0-2, the engine against the tyres and
		 *  wind, rasp (brightness), how much the exhaust rings, pops on lift-off */
		void set_volume(double V);
		double get_volume() const;
		void set_engine_gain(double V);
		double get_engine_gain() const;
		void set_rasp(double V);
		double get_rasp() const;
		void set_pipe(double V);
		double get_pipe() const;
		void set_crackle(double V);
		double get_crackle() const;

		/** How loud the last sound out was (RMS, 0-1), for checking it's there */
		double get_level() const { return Level; }

		void _ready() override;
		void _process(double Delta) override;

	protected:
		static void _bind_methods();

	private:
		GeodriveCar* Car = nullptr;
		int Body = -1;
		float Volume = 1.0f, EngineGain = 1.0f, Rasp = 1.0f, Pipe = 1.0f, Crackle = 1.0f;
		float Level = 0.0f;
		float Fade = 0.0f;	// eases in and out (paused, no car) so it never clicks
		AudioStreamPlayer3D* Player = nullptr;	// (at the car: heard from it, fainter and duller farther off)
		Ref<AudioStreamGeneratorPlayback> Playback;
		std::unique_ptr<geocar::SoundSynth> Synth;
	};
}
