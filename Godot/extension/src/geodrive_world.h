// Geodrive for Godot - AGPL-3.0 (see LICENSE)
//
// GeodriveWorld: the real world built by Geodrive's engine-free Core and streamed in as you drive, the way the Unreal
// game does it. A new area builds its first piece (Core/GeoWorld: a box ~12 km across) on a worker thread; then, round
// the focus (the car), the ground comes in as detailed chunks near it, coarse ones further out and distant blocks to the
// horizon, the roads and buildings as 1 km tiles, each made on a worker and put in a few milliseconds a frame, nearest
// first. When less than 4 km is built ahead of the car the next piece is built, joined to the last at the seam; old
// pieces go once nothing near needs them. Missing height tiles are downloaded.

#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <memory>
#include <vector>

namespace godot
{
	class GeodriveWorld : public Node3D
	{
		GDCLASS(GeodriveWorld, Node3D)

	public:
		GeodriveWorld();
		~GeodriveWorld();

		/** Where the game keeps its downloads (regions/, terrain/, heights/): %LOCALAPPDATA%/Geodrive/Cache */
		void set_cache_dir(const String& Dir);
		String get_cache_dir() const;

		/** A piece's size (km); 0 = by the memory there is, as the Unreal game picks it */
		void set_piece_km(double Km);
		double get_piece_km() const;

		/** How far away buildings are drawn (m); 0 = as far as the road tiles (7 km) */
		void set_building_distance(double Metres);
		double get_building_distance() const;
		/** All the log: the detailed lines (heights lifted, ground checks, slow chunks) too; off, only the main ones */
		void set_verbose(bool bOn) { bVerbose = bOn; }
		bool get_verbose() const { return bVerbose; }

		/** The road style (true: follow the land, the default; false: legal design grades, engineered cuttings) and kerbs
		 *  on town roads: for the pieces built from now on */
		void set_follow_land(bool bOn);
		bool get_follow_land() const;
		void set_kerbs(bool bOn);
		/** The truth test: roads solved as if there were no LiDAR, to measure them against it (--truth) */
		void set_truth(bool bOn);
		bool get_kerbs() const;

		/** Scan every road of each piece built for bumps, jumps and holes (Core's road scan): the summary to the log, every
		 *  fault to Saved/RoadReports/scan_<time>_piece<n>.csv */
		/** How far the land goes (m): beyond the built pieces, plain ground from coarse heights (the far horizon) out to here */
		/** How far roads, buildings and the detailed ground are built round you (m; 7000 as the Unreal game) */
		void set_detail_distance(double Metres);
		double get_detail_distance() const;

		void set_view_distance(double Metres);
		double get_view_distance() const;

		void set_scan_roads(bool bOn);
		bool get_scan_roads() const;

		/** Starts a new area round a place: everything already built goes, the first piece is built on a worker. Watch
		 *  is_ready(), then put the car at get_spawn(). */
		void start_area(double Lat, double Lon);

		/** The first piece is built and the ground round the start is in (and solid near it) */
		bool is_ready() const;

		/** A new area is being built (the loading screen's time) */
		bool is_loading() const;

		/** Why the area couldn't be built, if it couldn't */
		String get_error() const;

		/** Where to start: "position" (on a main road near the place), "heading", "road" */
		Dictionary get_spawn() const;

		/** The world streams in round this point (Godot axes, m); Velocity says which way the next piece should go */
		void set_focus(const Vector3& Position, const Vector3& Velocity);

		/** The ground and roads round a point are in and solid (for putting the car there) */
		bool is_solid_at(const Vector3& Position) const;

		/** What it's doing, for a HUD line */
		String get_status() const;

		/** What a new area is being built up to, for the loading screen: a stage ("Reading the map", "Road heights", ...,
		 *  "Putting in the ground and roads"), "Ready", or the error */
		String get_loading_status() const;

		/** The place the area was started round: lat, lon (degrees) */
		PackedFloat64Array get_start() const;

		/** The road nearest a point (Godot axes, m) within MaxDistance (m): "found", "position" (its centreline at the
		 *  road's height), "direction" (along it, level), "name", "highway", "distance"; by distance along the ground, as the
		 *  Unreal game finds it. Drivable only: not footpaths, steps, cycle paths, tracks and the like */
		Dictionary nearest_road(const Vector3& Position, double MaxDistance, bool bDrivableOnly = true) const;
		/** Every road of the area (as solved: heights, levels, bridges, tunnels) to a JSON file, for the road diagram (Tools/road_levels.py) */
		bool dump_roads(const String& Path) const;

		/** The roads' centrelines round a point (within Radius m, as their height solver left them): pairs of points, for
		 *  drawing (the Unreal game's geo.World.Debug) */
		PackedVector3Array road_lines(const Vector3& Position, double Radius) const;

		/** The roads round a point (within Radius m), whole, for the road editor: each a Dictionary of its id, name,
		 *  class, level, half width (m), the junctions at its ends and its points (Godot's, on its surface) */
		Array road_points(const Vector3& Position, double Radius) const;

		/** The M map over the view from above (the Unreal game's): the piece's main roads as lines (trunk orange, the rest
		 *  white), and street names that don't overlap, drawn onto a canvas item through the camera */
		void draw_map(const RID& CanvasItem, Node* Camera, const Ref<Font>& Font, int FontSize) const;

		/** Latitude, longitude (degrees) of a point, and back (on the ground) */
		PackedFloat64Array to_lat_lon(const Vector3& Position) const;
		Vector3 from_lat_lon(double Lat, double Lon) const;

		/** Whether a built piece holds a point (degrees) */
		bool contains_lat_lon(double Lat, double Lon) const;

		/** The map's buildings: the one at a point (inside it: {found, id, outline (its footprint round its top, Godot's
		 *  frame)}), and taking them out (by their map id: left out of the tiles, those loaded built again) */
		Dictionary building_at(const Vector3& Position) const;
		/** Every map building's footprint (Godot's frame, on the ground's plane) over the box between A and B (its loaded
		 *  pieces'; not those taken out): for what keeps out of buildings (GeodriveHunt) */
		void footprints(const Vector3& A, const Vector3& B, std::vector<std::vector<Vector3>>& Out) const;
		void remove_building(int64_t Id);
		void set_removed_buildings(const PackedInt64Array& Ids);
		PackedInt64Array get_removed_buildings() const;
		/** The plants cut down (trees: channel 0, bushes 1; each by its fine square, world-fixed): left out wherever
		 *  they'd be placed (place_plants takes the set as it is) */
		void remove_plant(int Channel, int FX, int FZ);
		void set_removed_plants(const Array& Plants);	// ([channel, fx, fz] each)
		int get_removed_plant_count() const;

		/** The ground's height under a point (m), roads' corridors included */
		double ground_height(const Vector3& Position) const;

		/** What collides at a point, for finding holes */
		String collision_status(const Vector3& Position) const;

		/** The ready-made areas (Core's, as in the Unreal game): id, name, description, south, west, north, east, places
		 *  (name, lat, lon) */
		static Array areas();

		/** Traffic (Core's, as in the Unreal game): Wanted cars round the player, a third parked, driven one frame. Call
		 *  every frame with the player's car (Godot axes, m and m/s) */
		void update_traffic(double Delta, const Vector3& Position, const Vector3& Forward, const Vector3& Velocity, int Wanted);
		String get_traffic_status() const;
		String get_traffic_debug() const;
		int get_traffic_count() const;
		void clear_traffic();

		/** The robot test driver (Core's, as the Unreal game's geo.Robot): drives the car along routes ("n2", "town" with
		 *  TownRoutes trips, "all") and reports problems to Saved/RoadReports. While it drives, call update_robot every
		 *  physics step and put its "steer", "throttle", "brake" on the car; it asks for the car to be placed
		 *  (robot_place_car) and for pictures (robot_screenshot). Shard / Shards: one of several copies at once drives
		 *  every Shards-th route */
		bool start_robot(Node* Car, const String& Mode, int TownRoutes, int Shard = 0, int Shards = 1);
		Dictionary update_robot(double Delta, Node* Car);
		void stop_robot();
		bool is_robot_driving() const;
		String get_robot_status() const;

		/** The credits the data's licences ask for (OpenStreetMap, and the height data the built pieces used) */
		String get_attribution() const;

		/** In the UK (its roads' rules, mph) */
		static bool is_uk(double Lat, double Lon);
		/** The country being built (its road rules: {code, name, drive_left, mph}) */
		Dictionary get_country() const;
		/** Whether any region file has a map of a spot (else: none here yet, geotile region can make one) */
		bool has_map_at(double Lat, double Lon);
		/** New region and height files in the data folder (a region just made) taken up: how many */
		int refresh_maps();
		static double get_game_memory();
		static double get_game_cpu();
		/** The ground's material, from the next piece of ground on (the vertex colours say grass, sand, rock, seabed) */
		void set_ground_material(const Ref<Material>& Material);
		/** The road surfaces' materials: asphalt, paving, gravel */
		void set_road_materials(const Ref<Material>& Asphalt, const Ref<Material>& Paving, const Ref<Material>& Gravel);
		/** The buildings' material (their corners' colour says their kind, own number and walls; see Core's Buildings.cpp) */
		void set_building_material(const Ref<Material>& Material);

		/** The built pieces' boxes: south, west, north, east (degrees) each */
		Array get_built_boxes() const;

		/** The plants' map round the camera (512 x 512 cells of 10 m: trees, shrubs, grass, land class; columns east, rows
		 *  south), where its north-west corner is (Godot), and how wide it is (m); null before the first piece */
		Ref<Texture2D> get_plant_texture(bool bFar = false) const;	// (far: the far blanket's coarser, wider map)
		Vector3 get_plant_origin(bool bFar = false) const;
		double get_plant_size(bool bFar = false) const;
		Ref<Texture2D> get_plant_heights(bool bFar = false) const;
		/** Which plants grow round the eye, ring by ring ([{index, kind, channel, cell, fine_cell, inner, outer, grow,
		 *  tile}]), worked out on a worker as the plant shader chose them (false: one's already going, or no map yet);
		 *  take_plants hands them over when done: {anchor, ms, rings: {index: [{tile, count, buffer, low, high}]}} */
		bool place_plants(const Array& Rings, const Vector3& Eye, bool bCape);
		Dictionary take_plants();	// over the same cells: R the ground's height (m), G the room to the nearest road edge or wall (m)
		Ref<Texture2D> get_horizon_plants(bool bHeights = false) const;	// the horizon's plant map (to the view distance, 200 m cells), or its heights
		Vector3 get_horizon_plant_origin() const;
		double get_horizon_plant_size() const;
		/** Where Godot's 0 is in the drive (m, Godot's axes): a world-fixed position is a Godot one plus this */
		Vector3 get_render_offset() const;
		/** The roads painted into the ground far off (each cell: the offset to the nearest road, its half width, built up); 51 km round the camera, its north-west
		 *  corner (Godot) and size (m) */
		Ref<Texture2D> get_road_texture() const;
		Vector3 get_road_origin() const;
		double get_road_size() const;
		/** Nothing grows along these points (Godot's frame) within Reach m, nor (area) inside them as a ring: airports */
		void set_plant_clearings(const Array& Clearings);	// (the airports' runways, taxiways, aprons: no plants there)
		/** A plant made in Core (GeoMesh/Plants.h): kind 0 broadleaf, 1 conifer, 2 eucalyptus, 3 bush; detail 1 near, 0 light, -1 far */
		static Ref<ArrayMesh> make_plant_mesh(int Kind, int Seed, int Detail);
		/** A person of this height's measures (Core GeoPeople/Body.h; m, m/s): height, eye_standing, eye_seated, radius,
		 *  step_length, step_up, walk_speed, run_speed, sprint_speed, jump_speed */
		static Dictionary body_measures(double HeightCm);
		/** An aircraft to look at (Core GeoMesh/Aircraft.h): kind 0 fighter jet, 1 airliner, 2 regional jet; or its gear */
		static Ref<ArrayMesh> make_aircraft_mesh(int Kind, bool bGear);
		/** Their next velocity (Godot's frame, m/s) walking: forward and right -1 to 1, facing yaw (Godot's rotation.y) */
		static Vector3 walk(double HeightCm, double Forward, double Right, bool bRun, bool bSprint, bool bJump, double Yaw, bool bOnGround, const Vector3& Velocity, double Delta);

		void _process(double Delta) override;

	private:
		bool UpdatePlantMap(bool bFar);	// (the near or the far plant map; whether it was made again)
		void UpdateRoadMap();	// (the painted road map, on a worker)

	public:

	protected:
		static void _bind_methods();

	public:
		struct State;
		struct TrafficAccess;

	private:
		std::unique_ptr<State> World;
		bool bVerbose = false;
	};
}
