// Geodrive Godot extension - AGPL-3.0 (see LICENSE)
//
// A hunt on a game farm in Godot: Core's animals (GeoHunt/Hunt.h: their days, their bodies, the law, the loads) on
// the farm's ground, drawn as multimeshes of their rounded parts (a fine and a coarse mesh by how near they look,
// and the horns' cones: three draw calls for them all, their coats painted by animal.gdshader), and shots flown
// through it (Core's ballistics, each step checked against the ground, buildings and fence by Godot's physics and
// against the animals' bodies by Core). The farm's frame: Godot's metres round the farm's middle (x east, y north,
// z up: Godot's x, -z, y).

#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <memory>
#include <random>
#include <vector>

namespace geohunt
{
	class Herds;
}

namespace godot
{
	class MultiMeshInstance3D;

	class GeodriveHunt : public Node3D
	{
		GDCLASS(GeodriveHunt, Node3D)

	public:
		GeodriveHunt();
		~GeodriveHunt();

		/** The farm stocked and its animals let loose: its outline and water (Godot positions now), its stock
		 *  ([species id, how many]...), the world (for the ground's height); AvoidBuildings: the map's buildings in it
		 *  kept out of (a town's); Spawn: a pool round the people ({around_people, nearest, farthest, leave_past,
		 *  dead_for}: Core's Spawning) */
		void start(Node* World, const PackedVector3Array& Outline, const PackedVector3Array& Water, const Array& Stock, int Seed, bool bAvoidBuildings = false, const Dictionary& Spawn = Dictionary());
		/** Where the people are now (for creatures that go for them; OnFoot: each one's, true if not given: one in a car
		 *  is come for, not caught); whom they've caught since (indices) */
		void set_people(const PackedVector3Array& At, const Array& OnFoot);
		PackedInt32Array take_caught();
		/** The cars among them now: [{position, forward, velocity, half_length, half_width, mass}] (Godot's frame) */
		void set_cars(const Array& Cars);
		/** The pool's creatures near a spot (Godot's) put back farther off: someone getting up there */
		void put_back(const Vector3& Near, double Within);
		/** The cars' hits since: [{index, car, name, speed (m/s), way (from the car), killed, wounded, car_loses (m/s)}] */
		Array take_hits();
		/** The animals gone (out through the gate) */
		void stop();
		bool is_running() const;
		/** Their next Dt seconds */
		void step(double Dt);
		/** Their bodies drawn (those within Reach m of Eye); Zoom: how many times nearer they look (a scope's ~9) */
		void draw(const Vector3& Eye, double Reach, double Zoom = 1.0);
		/** A shot (a load's id) from Eye along Aim; Exclude: what the shot can't hit (the shooter); Month (1-12) and
		 *  what's been taken today ({species id: count}) for the law: {hit, index, species, name, zone, distance,
		 *  energy, speed, legal, why, point, path} */
		Dictionary shoot(const String& Load, const Vector3& Eye, const Vector3& Aim, const Array& Exclude, int Month, const Dictionary& Taken);
		/** What's happened (a lion's kill, an animal falling), taken once read */
		PackedStringArray take_news();
		/** Someone else's shot heard from At (Godot): the game within earshot looks up towards it */
		void heard(const Vector3& At);

		// --- shared in a multiplayer game: the host's animals, the others showing them ---
		/** The animals as they are now (the host's, sent to the others ~10 times a second) */
		PackedByteArray pack() const;
		/** The host's (pack's) shown here: false if they don't fit (another farm, other stock) */
		bool apply(const PackedByteArray& Bytes);
		/** Following the host's: nothing decided here, each animal carried on between its updates */
		void set_follower(bool bOn);
		bool is_follower() const { return bFollower; }
		/** A shot that hit one of them, here (the host): someone else's (from At, Godot), its effect as they found it */
		void apply_shot(int Index, const Vector3& At, bool bDown, double DiesAfter, bool bWounded);
		int get_count() const;
		/** One animal: {species, name, doing, position, dead, shot} */
		Dictionary animal(int Index) const;
		/** Godot's 0 moved: the farm with it */
		void shift(const Vector3& Offset);

		/** Every species ({id, name, mass, protected, season [from, to], bag}) and load ({id, name, grains, muzzle,
		 *  magazine, arrow, energy}) there is */
		static Array species_list();
		static Array loads();
		/** A creature as a pack's file has it (creatures/*.json): every figure, its body's shape and its behaviour; as a
		 *  dictionary, or the file's text */
		static Dictionary species_data(const String& Id);
		static String species_text(const String& Id);
		/** A creature's body standing (its head down HeadDown: 0 up, 1 grazing), facing -z, its feet at 0: each part's
		 *  {frame (a unit sphere's, or a cone's up its y, to it), colour, horn}: the creature maker's picture of it */
		static Array body_parts(const String& Id, double HeadDown = 0.0);
		/** A creature (or weapon) from a file's dictionary: added, or the one with its id replaced; a creature's "like"
		 *  starts it from another (only what differs given). Its index, or -1 (what's wrong printed). Before any hunt */
		static int add_species(const Dictionary& Data);
		static int add_load(const Dictionary& Data);
		/** A pack's file read by Core (Kind: "creatures" or "weapons"): how many taken; each one turned away printed, and
		 *  why, with Where (the file) */
		static int read_file(const String& Text, const String& Kind, const String& Where);
		/** A sound made here (16-bit mono at Rate a second, for an AudioStreamWAV): "rifle" (the shot as the shooter
		 *  hears it: the muzzle's blast, then its echoes rolling back off the hills), "bow" (the string), "hit" (a
		 *  bullet striking an animal, heard from afar), "arrow_hit", "bolt" (worked: back and forward), "reload" */
		static PackedByteArray sound(const String& Kind, int Rate);

	protected:
		static void _bind_methods();

	private:
		std::unique_ptr<geohunt::Herds> Herds;
		Vector3 Centre;	// the farm's middle (Godot, y 0)
		Node* World = nullptr;
		std::vector<double> Ground;	// each animal's ground (Godot y)
		MultiMeshInstance3D* Bodies[4] = {};	// the parts near, the parts farther, the horns, the parts far (no shadows)
		std::mt19937 Random;
		bool bFollower = false;

		Vector3 ToGodot(double X, double Y, double Z) const { return Vector3(float(Centre.x + X), float(Z), float(Centre.z - Y)); }
		double GroundAt(double X, double Y) const;
		void FitBox();	// (the drawn animals' box: round them)
	};
}
