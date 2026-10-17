// Geodrive for Godot - GPL (see Core/README.md)

#include "geodrive_car.h"

#include "GeoCar/CarBody.h"
#include "GeoCar/CarMotion.h"
#include "GeoCar/CarSim.h"
#include "GeoCar/CarTelemetry.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>

using namespace godot;

namespace
{
	/** The glass's surface (vertex alpha x 100) */
	constexpr int GlassKind = 5;

	/** Core's world frame (x north, y east, z up; left-handed) and Godot's (x east, y up, z south; right-handed), both
	 *  in metres. Angular velocity is an axial vector, so it also changes sign between the two. */
	geocar::Vec3 ToCore(const Vector3& V) { return { -V.z, V.x, V.y }; }
	Vector3 ToGodot(const geocar::Vec3& V) { return Vector3(float(V.Y), float(V.Z), float(-V.X)); }

	/** A car mesh (cm, x forward, y right, z up, about Origin) in the car's Godot frame (m, -z forward, x right, y up),
	 *  one surface per kind of finish (the vertex alpha: paint, glass, metal, rubber, lights...). Mirror turns a right
	 *  wheel into a left one. The axes swap changes handedness, so the corners go the other way round (twice mirrored:
	 *  they stay). */
	Ref<ArrayMesh> ToGodot(const geocar::CarMesh& Mesh, const geocar::Vec3& Origin, bool bMirror, int OnlyKind = -1, int SkipKind = -1)
	{
		std::map<int, std::vector<int32_t>> Kinds;	// surface (alpha x 100) -> triangles
		for (size_t t = 0; t + 2 < Mesh.Triangles.size(); t += 3)
		{
			const int Kind = int(std::lround(Mesh.Colors[size_t(Mesh.Triangles[t])].A * 100.0f));
			if ((OnlyKind >= 0 && Kind != OnlyKind) || Kind == SkipKind)
			{
				continue;
			}
			std::vector<int32_t>& List = Kinds[Kind];
			List.insert(List.end(), { Mesh.Triangles[t], Mesh.Triangles[t + 1], Mesh.Triangles[t + 2] });
		}
		Ref<ArrayMesh> Out;
		Out.instantiate();
		const float Side = bMirror ? -1.0f : 1.0f;
		std::vector<int32_t> Remap(Mesh.Positions.size(), -1);	// (Core's corner -> the surface's, per surface)
		std::vector<int32_t> Used;
		for (const auto& Entry : Kinds)
		{
			for (const int32_t V : Used)
			{
				Remap[size_t(V)] = -1;
			}
			Used.clear();
			const std::vector<int32_t>& List = Entry.second;
			for (const int32_t V : List)
			{
				if (Remap[size_t(V)] < 0)
				{
					Remap[size_t(V)] = int32_t(Used.size());
					Used.push_back(V);
				}
			}
			PackedVector3Array Positions, Normals;
			PackedColorArray Colours;
			PackedInt32Array Indices;
			Positions.resize(int64_t(Used.size()));
			Normals.resize(int64_t(Used.size()));
			Colours.resize(int64_t(Used.size()));
			Indices.resize(int64_t(List.size()));
			Vector3* PositionsOut = Positions.ptrw();
			Vector3* NormalsOut = Normals.ptrw();
			Color* ColoursOut = Colours.ptrw();
			int32_t* IndicesOut = Indices.ptrw();
			for (size_t i = 0; i < Used.size(); ++i)
			{
				const geocar::Vec3 P = Mesh.Positions[size_t(Used[i])] - Origin;
				const geocar::Vec3& N = Mesh.Normals[size_t(Used[i])];
				const geocar::Color& C = Mesh.Colors[size_t(Used[i])];
				PositionsOut[i] = Vector3(Side * float(P.Y), float(P.Z), float(-P.X)) / 100.0f;
				NormalsOut[i] = Vector3(Side * float(N.Y), float(N.Z), float(-N.X));
				ColoursOut[i] = Color(C.R, C.G, C.B, 1.0f);
			}
			for (size_t t = 0; t + 2 < List.size(); t += 3)
			{
				const int32_t A = Remap[size_t(List[t])], B = Remap[size_t(List[t + 1])], C = Remap[size_t(List[t + 2])];
				IndicesOut[t] = A;
				IndicesOut[t + 1] = bMirror ? B : C;
				IndicesOut[t + 2] = bMirror ? C : B;
			}
			Array Arrays;
			Arrays.resize(Mesh::ARRAY_MAX);
			Arrays[Mesh::ARRAY_VERTEX] = Positions;
			Arrays[Mesh::ARRAY_NORMAL] = Normals;
			Arrays[Mesh::ARRAY_COLOR] = Colours;
			Arrays[Mesh::ARRAY_INDEX] = Indices;
			Out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Arrays);

			// the finish: colour from the corners; glossy paint, glass (car_glass.gdshader: see-through, a mirror at a
			// glancing angle), metal, matt rubber, glowing lights
			if (Entry.first == 5)
			{
				const Ref<Shader> GlassShader = ResourceLoader::get_singleton()->load("res://car_glass.gdshader");	// (loaded once: the loader keeps it)
				Ref<ShaderMaterial> Glass;
				Glass.instantiate();
				Glass->set_shader(GlassShader);
				Out->surface_set_material(Out->get_surface_count() - 1, Glass);
				continue;
			}
			const float Surface = float(Entry.first) / 100.0f;
			Ref<StandardMaterial3D> Material;
			Material.instantiate();
			Material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
			if (Entry.first == 0)
			{
				Material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
			}
			else
			{
				Material->set_roughness(Surface);
				Material->set_metallic(Entry.first == 25 ? 0.9f : 0.0f);
				if (Entry.first == 30)
				{
					Material->set_feature(BaseMaterial3D::FEATURE_CLEARCOAT, true);
					Material->set_clearcoat(1.0f);
					Material->set_clearcoat_roughness(0.08f);
				}
			}
			Out->surface_set_material(Out->get_surface_count() - 1, Material);
		}
		return Out;
	}

	/** Each kind of car's design, built once (~12 ms) and copied for every car of its kind after (the player's, the
	 *  traffic's): built for each one, it was a jolt as each traffic car came in */
	std::shared_ptr<const geocar::CarDesign> DesignOf(geocar::CarBody Body)
	{
		static std::mutex Lock;
		static std::map<int, std::shared_ptr<const geocar::CarDesign>> Designs;
		std::lock_guard<std::mutex> Hold(Lock);
		std::shared_ptr<const geocar::CarDesign>& Design = Designs[int(Body)];
		if (!Design)
		{
			Design = std::make_shared<const geocar::CarDesign>(geocar::designs::Build(Body, geocar::designs::DefaultTemplate()));
		}
		return Design;
	}

	/** Each kind of car's look in Godot (its body, glass, steering wheel and wheels), made once and shared by every car of
	 *  its kind (on the main thread: ~40 ms the first time). A traffic car's own paint goes on its own node */
	struct CarLook
	{
		Ref<ArrayMesh> Body, Glass, SteeringWheel, Wheels[geocar::NumWheels];
	};

	/** How far back (m) a wheel's cast starts, to catch it starting inside the ground */
	constexpr float LeadIn = 1.0f;

	/** The car's view of the Godot world: sphere casts in the physics space, forces into the body */
	class GodotCarWorld final : public geocar::CarWorld
	{
	public:
		GodotCarWorld(PhysicsDirectBodyState3D& InState, const Ref<PhysicsShapeQueryParameters3D>& InQuery, const Ref<SphereShape3D>& InSphere)
			: State(InState), Query(InQuery), Sphere(InSphere), Space(InState.get_space_state()), Origin(InState.get_transform().origin)
		{
		}

		bool SweepSphere(const geocar::Vec3& Start, const geocar::Vec3& End, double Radius, geocar::SweepHit& Hit) override
		{
			if (!Space)
			{
				return false;
			}
			// Godot's cast ignores what the sphere starts inside, so a wheel pushed into the ground (landing hard,
			// off a kerb at speed) would find nothing and the car would sink. The cast starts a metre further back
			// instead: ground found before the real start means the wheel starts in it (full bump, as the game's
			// engine reports it), and the suspension pushes the car back out.
			const Vector3 RealFrom = ToGodot(Start), Motion = ToGodot(End) - RealFrom;
			const float Length = Motion.length();
			if (Length < 1.0e-4f)
			{
				return false;
			}
			const Vector3 Back = -Motion / Length * LeadIn;
			const Vector3 From = RealFrom + Back, Full = Motion - Back;
			Sphere->set_radius(float(Radius));
			Query->set_transform(Transform3D(Basis(), From));
			Query->set_motion(Full);
			const PackedFloat32Array Fractions = Space->cast_motion(Query);
			if (Fractions.size() < 2 || Fractions[1] >= 1.0f)
			{
				return false;
			}
			const float Safe = Fractions[0], Unsafe = Fractions[1];
			const float Along = Safe * (Length + LeadIn);	// from the lead-in's start
			Hit.bStartPenetrating = Along < LeadIn;
			Hit.Location = ToCore(Hit.bStartPenetrating ? RealFrom : From + Full * Safe);

			// where it touches: the contact at the first overlapping position
			Query->set_transform(Transform3D(Basis(), From + Full * Unsafe));
			Query->set_motion(Vector3());
			const Dictionary Rest = Space->get_rest_info(Query);
			if (Rest.has("point"))
			{
				Hit.ImpactPoint = ToCore(Rest["point"]);
				Hit.ImpactNormal = ToCore(Rest["normal"]);
			}
			else
			{
				Hit.ImpactPoint = ToCore(From + Full * Safe + Vector3(0.0f, -float(Radius), 0.0f));
				Hit.ImpactNormal = ToCore(Vector3(0.0f, 1.0f, 0.0f));
			}
			return true;
		}

		void AddForceAtPosition(const geocar::Vec3& Force, const geocar::Vec3& Position) override
		{
			State.apply_force(ToGodot(Force), ToGodot(Position) - Origin);
		}

	private:
		PhysicsDirectBodyState3D& State;
		Ref<PhysicsShapeQueryParameters3D> Query;
		Ref<SphereShape3D> Sphere;
		PhysicsDirectSpaceState3D* Space;
		Vector3 Origin;
	};
}

struct GeodriveCar::Car
{
	geocar::CarDesign Design;
	geocar::CarSim Sim;
	geocar::CarMotion Motion;	// (its body: the shared motion moves it; Godot resolves its collisions)
	geocar::Controls Controls;
	std::shared_ptr<geocar::CarTelemetry> Telemetry = std::make_shared<geocar::CarTelemetry>();
	geocar::Vec3 WheelPositions[geocar::NumWheels];	// Core's car frame (m; x forward, y right, z up)
	Vector3 GodotWheels[geocar::NumWheels];			// the car's own frame in Godot
	geocar::Vec3 Origin;	// the car's origin in its design's frame (cm): on the ground, halfway between the axles
	Node3D* WheelNodes[geocar::NumWheels] = {};
	MeshInstance3D* GlassNode = nullptr;
	MeshInstance3D* SteeringWheelNode = nullptr;
	int View = 0;	// 0 chase, 1 cockpit, 2 cockpit without the steering wheel, 3 bonnet
	float SteeringRatio = 12.0f;	// steering-wheel degrees per road-wheel degree, for the one in the cockpit
	Ref<PhysicsShapeQueryParameters3D> Query;
	Ref<SphereShape3D> Sphere;

	const geocar::CarSetup& Setup() const { return Design.Setup; }

	geocar::CarBody Body = geocar::CarBody::Bakkie;
	Color Paint;
	bool bPaint = false;
	bool bAutopilot = false;	// driven by the game (the robot, traffic): a steering wheel leaves it alone
	// someone else's car in a networked game: held where their updates put it, its wheels turned from them
	bool bRemote = false;
	float RemoteSteer = 0.0f, RemoteSpeed = 0.0f, RemoteSpin = 0.0f;

	void Use(geocar::CarBody Body)
	{
		this->Body = Body;
		Design = *DesignOf(Body);
		Motion.Use(Design.Parts);
		const geocar::CarFrame& Frame = Design.Frame;
		Origin = geocar::Vec3((Frame.FrontX() + Frame.RearX()) * 0.5, 0.0, Frame.Ground());
		for (int i = 0; i < geocar::NumWheels; ++i)
		{
			const geocar::Vec3 W = (Frame.Wheels[i] - Origin) * 0.01;
			WheelPositions[i] = W;
			GodotWheels[i] = Vector3(float(W.Y), float(W.Z), float(-W.X));
		}
	}
};

GeodriveCar::GeodriveCar()
	: Sim(std::make_unique<Car>())
{
	Sim->Use(geocar::CarBody::Bakkie);
}

GeodriveCar::~GeodriveCar() = default;

namespace
{
	std::map<int, CarLook> Looks;	// (let go as the extension closes: forget_looks, while Godot's renderer is still there)

	const CarLook& LookOf(int Body)
	{
		CarLook& Look = Looks[Body];
		if (Look.Body.is_null())
		{
			const std::shared_ptr<const geocar::CarDesign> Design = DesignOf(geocar::CarBody(Body));
			const geocar::CarFrame& Frame = Design->Frame;
			const geocar::Vec3 Origin((Frame.FrontX() + Frame.RearX()) * 0.5, 0.0, Frame.Ground());	// (as Car::Use)
			Look.Body = ToGodot(Design->Body, Origin, false, -1, GlassKind);
			Look.Glass = ToGodot(Design->Body, Origin, false, GlassKind);
			Look.SteeringWheel = ToGodot(Design->SteeringWheel, geocar::Vec3(), false);
			for (int i = 0; i < geocar::NumWheels; ++i)
			{
				Look.Wheels[i] = ToGodot(Design->Wheel[geocar::IsFront(i) ? 0 : 1], geocar::Vec3(), geocar::IsLeft(i));
			}
		}
		return Look;
	}
}

void GeodriveCar::prepare(int Body)
{
	LookOf(std::clamp(Body, 1, int(geocar::CarBody::Count) - 1));
}

void GeodriveCar::forget_looks()
{
	Looks.clear();
}

void GeodriveCar::set_body(int Body)
{
	if (is_inside_tree())
	{
		return;	// (its meshes and mass are made in _ready)
	}
	Sim->Use(geocar::CarBody(std::clamp(Body, 1, int(geocar::CarBody::Count) - 1)));
}

int GeodriveCar::get_body() const { return int(Sim->Body); }

void GeodriveCar::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_input", "steer", "throttle", "brake", "handbrake"), &GeodriveCar::set_input);
	ClassDB::bind_method(D_METHOD("set_handbrake", "handbrake"), &GeodriveCar::set_handbrake);
	ClassDB::bind_method(D_METHOD("shift_up"), &GeodriveCar::shift_up);
	ClassDB::bind_method(D_METHOD("shift_down"), &GeodriveCar::shift_down);
	ClassDB::bind_method(D_METHOD("set_automatic", "automatic"), &GeodriveCar::set_automatic);
	ClassDB::bind_method(D_METHOD("get_automatic"), &GeodriveCar::get_automatic);
	ClassDB::bind_method(D_METHOD("set_diff_lock", "locked"), &GeodriveCar::set_diff_lock);
	ClassDB::bind_method(D_METHOD("get_diff_lock"), &GeodriveCar::get_diff_lock);
	ClassDB::bind_method(D_METHOD("set_engine_on", "on"), &GeodriveCar::set_engine_on);
	ClassDB::bind_method(D_METHOD("get_engine_on"), &GeodriveCar::get_engine_on);
	ClassDB::bind_method(D_METHOD("reset_car"), &GeodriveCar::reset_car);
	ClassDB::bind_method(D_METHOD("get_speed_kmh"), &GeodriveCar::get_speed_kmh);
	ClassDB::bind_method(D_METHOD("get_rpm"), &GeodriveCar::get_rpm);
	ClassDB::bind_method(D_METHOD("get_gear"), &GeodriveCar::get_gear);
	ClassDB::bind_method(D_METHOD("get_wheel_transform", "wheel"), &GeodriveCar::get_wheel_transform);
	ClassDB::bind_method(D_METHOD("get_wheel_radius", "wheel"), &GeodriveCar::get_wheel_radius);
	ClassDB::bind_method(D_METHOD("get_status"), &GeodriveCar::get_status);
	ClassDB::bind_method(D_METHOD("get_debug"), &GeodriveCar::get_debug);
	ClassDB::bind_method(D_METHOD("get_car_name"), &GeodriveCar::get_car_name);
	ClassDB::bind_method(D_METHOD("get_cockpit_camera"), &GeodriveCar::get_cockpit_camera);
	ClassDB::bind_method(D_METHOD("get_cockpit_ceiling"), &GeodriveCar::get_cockpit_ceiling);
	ClassDB::bind_method(D_METHOD("get_bonnet_camera"), &GeodriveCar::get_bonnet_camera);
	ClassDB::bind_method(D_METHOD("set_view", "view"), &GeodriveCar::set_view);
	ClassDB::bind_method(D_METHOD("get_view"), &GeodriveCar::get_view);
	ClassDB::bind_method(D_METHOD("set_steering_ratio", "ratio"), &GeodriveCar::set_steering_ratio);
	ClassDB::bind_method(D_METHOD("set_body", "body"), &GeodriveCar::set_body);
	ClassDB::bind_method(D_METHOD("get_body"), &GeodriveCar::get_body);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "body"), "set_body", "get_body");
	ClassDB::bind_method(D_METHOD("set_paint", "colour"), &GeodriveCar::set_paint);
	ClassDB::bind_static_method("GeodriveCar", D_METHOD("prepare", "body"), &GeodriveCar::prepare);
	ClassDB::bind_method(D_METHOD("set_remote", "remote"), &GeodriveCar::set_remote);
	ClassDB::bind_method(D_METHOD("get_steer_degrees"), &GeodriveCar::get_steer_degrees);
	ClassDB::bind_method(D_METHOD("is_stalled"), &GeodriveCar::is_stalled);
	ClassDB::bind_method(D_METHOD("has_diff_lock"), &GeodriveCar::has_diff_lock);
	ClassDB::bind_method(D_METHOD("is_remote"), &GeodriveCar::is_remote);
	ClassDB::bind_method(D_METHOD("set_remote_state", "steer_degrees", "speed_ms"), &GeodriveCar::set_remote_state);
	ClassDB::bind_method(D_METHOD("set_autopilot", "on"), &GeodriveCar::set_autopilot);
	ClassDB::bind_method(D_METHOD("get_autopilot"), &GeodriveCar::get_autopilot);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autopilot"), "set_autopilot", "get_autopilot");
	ClassDB::bind_method(D_METHOD("get_max_steer_degrees"), &GeodriveCar::get_max_steer_degrees);
	ClassDB::bind_method(D_METHOD("get_wheelbase"), &GeodriveCar::get_wheelbase);
	ClassDB::bind_method(D_METHOD("set_abs_level", "level"), &GeodriveCar::set_abs_level);
	ClassDB::bind_method(D_METHOD("get_abs_level"), &GeodriveCar::get_abs_level);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "abs_level"), "set_abs_level", "get_abs_level");
	ClassDB::bind_method(D_METHOD("set_tc_level", "level"), &GeodriveCar::set_tc_level);
	ClassDB::bind_method(D_METHOD("get_tc_level"), &GeodriveCar::get_tc_level);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "tc_level"), "set_tc_level", "get_tc_level");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "automatic"), "set_automatic", "get_automatic");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "diff_lock"), "set_diff_lock", "get_diff_lock");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "engine_on"), "set_engine_on", "get_engine_on");
}

void GeodriveCar::_ready()
{
	Car& C = *Sim;
	const geocar::CarSetup& S = C.Setup();

	// the body as Core builds it, and its wheels (the left ones mirrored, so their outer faces look out)
	const CarLook& Look = LookOf(int(C.Body));
	MeshInstance3D* BodyNode = memnew(MeshInstance3D);
	BodyNode->set_name("Body");
	BodyNode->set_mesh(Look.Body);
	if (C.bPaint)
	{
		// a traffic car's own colour: the glossy paint (clearcoat) surfaces take it in place of the design's (on this
		// car's node: the mesh is every car of its kind's)
		const Ref<ArrayMesh>& Body = Look.Body;
		for (int32_t s = 0; s < Body->get_surface_count(); ++s)
		{
			Ref<StandardMaterial3D> Material = Body->surface_get_material(s);
			if (Material.is_valid() && Material->get_feature(BaseMaterial3D::FEATURE_CLEARCOAT))
			{
				Ref<StandardMaterial3D> Paint = Material->duplicate();
				Paint->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, false);
				Paint->set_albedo(C.Paint);
				BodyNode->set_surface_override_material(s, Paint);
			}
		}
	}
	add_child(BodyNode);

	// the glass on its own, so it can go from inside the cockpit; and the steering wheel, there only
	C.GlassNode = memnew(MeshInstance3D);
	C.GlassNode->set_name("Glass");
	C.GlassNode->set_mesh(Look.Glass);
	add_child(C.GlassNode);
	C.SteeringWheelNode = memnew(MeshInstance3D);
	C.SteeringWheelNode->set_name("SteeringWheel");
	C.SteeringWheelNode->set_mesh(Look.SteeringWheel);
	C.SteeringWheelNode->set_visible(false);
	add_child(C.SteeringWheelNode);
	for (int i = 0; i < geocar::NumWheels; ++i)
	{
		C.WheelNodes[i] = memnew(Node3D);
		C.WheelNodes[i]->set_name(vformat("Wheel%d", i));
		MeshInstance3D* WheelMesh = memnew(MeshInstance3D);
		WheelMesh->set_mesh(Look.Wheels[i]);
		C.WheelNodes[i]->add_child(WheelMesh);
		add_child(C.WheelNodes[i]);
	}

	// the box it collides as: the body's bounds, above the wheel centres (the wheels themselves are the sweeps)
	Vector3 Min(1e9f, 1e9f, 1e9f), Max(-1e9f, -1e9f, -1e9f);
	for (const geocar::Vec3& P : C.Design.Body.Positions)
	{
		const geocar::Vec3 L = (P - C.Origin) * 0.01;
		const Vector3 G(float(L.Y), float(L.Z), float(-L.X));
		Min = Min.min(G);
		Max = Max.max(G);
	}
	Min.y = std::max(Min.y, C.GodotWheels[0].y);
	const Vector3 Size = Max - Min;

	// its mass, where its weight sits (over the wheels by the front share, at its height), and its inertia (its parts',
	// or a box's guess: Godot's x across, y up, z along)
	set_mass(S.Mass);
	set_center_of_mass_mode(CENTER_OF_MASS_MODE_CUSTOM);
	const float FrontZ = C.GodotWheels[0].z, RearZ = C.GodotWheels[2].z;
	set_center_of_mass(Vector3(0.0f, S.CoGHeight, RearZ + (FrontZ - RearZ) * S.FrontWeight));
	const float M = S.Mass / 12.0f;
	if (S.Inertia.X > 0.0)
	{
		set_inertia(Vector3(float(S.Inertia.Y), float(S.Inertia.Z), float(S.Inertia.X)));
	}
	else
	{
		set_inertia(Vector3(M * (Size.z * Size.z + Size.y * Size.y), M * (Size.z * Size.z + Size.x * Size.x), M * (Size.x * Size.x + Size.y * Size.y)));
	}
	// (where its weight sits: exactly its parts' centre of mass when it has them, its own frame: x right, y up, z back)
	if (!C.Design.Parts.empty())
	{
		const geobody::Vec3 Centre = C.Motion.GetBody().GetMass().CentreOfMass;
		set_center_of_mass(Vector3(float(-Centre.Y), float(Centre.Z), float(-Centre.X)));
	}
	// the shared motion (CarMotion) moves it: Godot only resolves its collisions (custom integration: no gravity or
	// moving of its own; the velocities it's handed each step, its contacts on top)
	set_use_custom_integrator(true);
	set_linear_damp_mode(DAMP_MODE_REPLACE);
	set_angular_damp_mode(DAMP_MODE_REPLACE);
	set_linear_damp(0.0f);
	set_angular_damp(0.0f);
	set_can_sleep(false);
	set_use_continuous_collision_detection(true);	// fast enough to pass through thin ground between steps otherwise

	Ref<BoxShape3D> Box;
	Box.instantiate();
	Box->set_size(Size);
	CollisionShape3D* Shape = memnew(CollisionShape3D);
	Shape->set_shape(Box);
	Shape->set_position((Min + Max) * 0.5f);
	add_child(Shape);

	C.Sphere.instantiate();
	C.Query.instantiate();
	C.Query->set_shape(C.Sphere);
	C.Query->set_collide_with_bodies(true);
	C.Query->set_collide_with_areas(false);
	TypedArray<RID> Exclude;
	Exclude.push_back(get_rid());
	C.Query->set_exclude(Exclude);
	set_process(true);
}

void GeodriveCar::_process(double Delta)
{
	if (Sim->bRemote)
	{
		// (their wheels roll at their speed)
		Sim->RemoteSpin += float(Delta) * Sim->RemoteSpeed / std::max(float(get_wheel_radius(0)), 0.1f);
	}
	for (int i = 0; i < geocar::NumWheels; ++i)
	{
		if (Sim->WheelNodes[i])
		{
			Sim->WheelNodes[i]->set_transform(get_wheel_transform(i));
		}
	}

	// the steering wheel: tilted on its column (top towards the driver), turned by the road wheels' angle through the ratio
	if (Sim->SteeringWheelNode && Sim->SteeringWheelNode->is_visible())
	{
		const geocar::CarDesign& D = Sim->Design;
		const geocar::Vec3 At = (D.SteeringWheelAt - Sim->Origin) * 0.01;
		const float Road = std::clamp(Sim->Controls.SteerDegrees, -Sim->Setup().Front.MaxSteerAngle, Sim->Setup().Front.MaxSteerAngle);
		const float Turn = -Road * Sim->SteeringRatio * 3.14159265f / 180.0f;
		const Basis Tilt(Vector3(1.0f, 0.0f, 0.0f), D.SteeringColumnPitch * 3.14159265f / 180.0f);
		Sim->SteeringWheelNode->set_transform(Transform3D(Tilt * Basis(Vector3(0.0f, 0.0f, 1.0f), Turn), Vector3(float(At.Y), float(At.Z), float(-At.X))));
	}
}

void GeodriveCar::set_view(int View)
{
	Sim->View = View;
	// (the glass stays: see-through, it's there from inside as a real windscreen is)
	if (Sim->SteeringWheelNode)
	{
		Sim->SteeringWheelNode->set_visible(View == 1);
	}
}

int GeodriveCar::get_view() const { return Sim->View; }
void GeodriveCar::set_steering_ratio(double Ratio) { Sim->SteeringRatio = float(std::max(Ratio, 1.0)); }

Vector3 GeodriveCar::get_bonnet_camera() const
{
	const geocar::Vec3 L = (Sim->Design.BonnetCamera - Sim->Origin) * 0.01;
	return Vector3(float(L.Y), float(L.Z), float(-L.X));
}

void GeodriveCar::_integrate_forces(PhysicsDirectBodyState3D* BodyState)
{
	if (!BodyState || Sim->Query.is_null())
	{
		return;
	}
	const Transform3D T = BodyState->get_transform();
	geocar::BodyState Body;
	Body.Position = ToCore(T.origin);
	Body.Forward = ToCore(-T.basis.get_column(2));
	Body.Right = ToCore(T.basis.get_column(0));
	Body.Up = ToCore(T.basis.get_column(1));
	Body.CenterOfMass = ToCore(T.origin + BodyState->get_center_of_mass());
	Body.Velocity = ToCore(BodyState->get_linear_velocity());
	Body.AngularVelocity = -ToCore(BodyState->get_angular_velocity());

	GodotCarWorld World(*BodyState, Sim->Query, Sim->Sphere);
	// the shared motion: where Godot has it (its collisions included), the car's forces and gravity on its body, and
	// the velocities that come of it back to Godot, which moves it and resolves its contacts
	Sim->Motion.Sync(Body);
	Sim->Motion.Step(World, Sim->Sim, Sim->Setup(), Sim->Controls, Sim->WheelPositions, float(BodyState->get_step()));
	const geocar::BodyState After = Sim->Motion.State();
	BodyState->set_linear_velocity(ToGodot(After.Velocity));
	BodyState->set_angular_velocity(-ToGodot(After.AngularVelocity));
	geocar::Publish(Sim->Sim.GetState(), Sim->Setup().Tire, *Sim->Telemetry);
}

void GeodriveCar::set_input(double Steer, double Throttle, double Brake, double Handbrake)
{
	geocar::Controls& C = Sim->Controls;
	C.SteerDegrees = float(std::clamp(Steer, -1.0, 1.0)) * Sim->Setup().Front.MaxSteerAngle;
	C.Throttle = float(std::clamp(Throttle, 0.0, 1.0));
	C.Brake = float(std::clamp(Brake, 0.0, 1.0));
	C.Handbrake = float(std::clamp(Handbrake, 0.0, 1.0));
}

void GeodriveCar::set_remote(bool bOn)
{
	// held by its driver's updates (kinematic: it still pushes, and is pushed by nothing)
	Sim->bRemote = bOn;
	set_freeze_mode(FREEZE_MODE_KINEMATIC);
	set_freeze_enabled(bOn);
}

bool GeodriveCar::is_remote() const { return Sim->bRemote; }
double GeodriveCar::get_steer_degrees() const { return Sim->Sim.GetState().Wheels[0].SteerDegrees; }
bool GeodriveCar::is_stalled() const { return Sim->Sim.GetState().bStalled; }
bool GeodriveCar::has_diff_lock() const { return Sim->Setup().Gearbox.bHasDiffLock; }

void GeodriveCar::set_remote_state(double SteerDegrees, double SpeedMS)
{
	Sim->RemoteSteer = float(SteerDegrees);
	Sim->RemoteSpeed = float(SpeedMS);
}

void GeodriveCar::set_autopilot(bool bOn)
{
	// as the Unreal game's: the steering wheel's steering, pedals and clutch no longer reach the car (its own automatic
	// clutch takes over), and the handbrake comes off
	Sim->bAutopilot = bOn;
	Sim->Controls.Handbrake = 0.0f;
	if (bOn)
	{
		Sim->Controls.bClutchPedal = false;
		Sim->Controls.Clutch = 0.0f;
	}
}

bool GeodriveCar::get_autopilot() const { return Sim->bAutopilot; }

void GeodriveCar::set_paint(const Color& Colour)
{
	Sim->Paint = Colour;
	Sim->bPaint = true;
}

double GeodriveCar::get_max_steer_degrees() const { return Sim->Setup().Front.MaxSteerAngle; }
double GeodriveCar::get_wheelbase() const { return std::abs(Sim->GodotWheels[0].z - Sim->GodotWheels[2].z); }

void GeodriveCar::set_abs_level(int Level) { Sim->Controls.ABSLevel = std::clamp(Level, 0, 10); }
int GeodriveCar::get_abs_level() const { return Sim->Controls.ABSLevel; }
void GeodriveCar::set_tc_level(int Level) { Sim->Controls.TCLevel = std::clamp(Level, 0, 10); }
int GeodriveCar::get_tc_level() const { return Sim->Controls.TCLevel; }

geocar::Controls& GeodriveCar::controls() { return Sim->Controls; }
const geocar::VehicleState& GeodriveCar::state() const { return Sim->Sim.GetState(); }
std::shared_ptr<const geocar::CarTelemetry> GeodriveCar::telemetry() const { return Sim->Telemetry; }
const geocar::EngineSound& GeodriveCar::engine_sound() const { return Sim->Design.Sound; }

void GeodriveCar::set_handbrake(double Handbrake) { Sim->Controls.Handbrake = float(std::clamp(Handbrake, 0.0, 1.0)); }
void GeodriveCar::shift_up() { ++Sim->Controls.ShiftUpCount; }
void GeodriveCar::shift_down() { ++Sim->Controls.ShiftDownCount; }
void GeodriveCar::set_automatic(bool bAutomatic) { Sim->Controls.bAutomatic = bAutomatic; }

void GeodriveCar::set_brake_reverses(bool bOn) { Sim->Controls.bBrakeReverses = bOn; }
bool GeodriveCar::get_automatic() const { return Sim->Controls.bAutomatic; }
void GeodriveCar::set_diff_lock(bool bLocked) { Sim->Controls.bDiffLock = bLocked && Sim->Setup().Gearbox.bHasDiffLock; }
bool GeodriveCar::get_diff_lock() const { return Sim->Controls.bDiffLock; }
void GeodriveCar::set_engine_on(bool bOn) { Sim->Controls.bEngineOff = !bOn; }
bool GeodriveCar::get_engine_on() const { return !Sim->Controls.bEngineOff; }
void GeodriveCar::reset_car() { ++Sim->Controls.ResetCount; }

double GeodriveCar::get_speed_kmh() const { return Sim->Sim.GetState().SpeedMS * 3.6; }
double GeodriveCar::get_rpm() const { return Sim->Sim.GetState().EngineRPM; }
int GeodriveCar::get_gear() const { return Sim->Sim.GetState().Gear; }
double GeodriveCar::get_wheel_radius(int Wheel) const { return Sim->Setup().Axle(std::clamp(Wheel, 0, geocar::NumWheels - 1)).WheelRadius; }
String GeodriveCar::get_car_name() const { return String::utf8(Sim->Design.Name.c_str()); }

double GeodriveCar::get_cockpit_ceiling() const
{
	const geocar::CarDesign& D = Sim->Design;
	return D.CockpitCeiling > 0.0f ? (D.CockpitCeiling - Sim->Origin.Z) * 0.01 : 1e6;
}

Vector3 GeodriveCar::get_cockpit_camera() const
{
	const geocar::Vec3 L = (Sim->Design.CockpitCamera - Sim->Origin) * 0.01;
	return Vector3(float(L.Y), float(L.Z), float(-L.X));
}

Transform3D GeodriveCar::get_wheel_transform(int Wheel) const
{
	Wheel = std::clamp(Wheel, 0, geocar::NumWheels - 1);
	const geocar::WheelState& W = Sim->Sim.GetState().Wheels[Wheel];
	if (Sim->bRemote)
	{
		const float Steer = geocar::IsFront(Wheel) ? -Sim->RemoteSteer * 3.14159265f / 180.0f : 0.0f;
		return Transform3D(Basis(Vector3(0.0f, 1.0f, 0.0f), Steer) * Basis(Vector3(1.0f, 0.0f, 0.0f), -Sim->RemoteSpin), Sim->GodotWheels[Wheel]);
	}
	// steering turns it about the car's up axis (+ = right, which is clockwise from above), rolling forward about its axle
	const float Steer = -W.SteerDegrees * 3.14159265f / 180.0f;
	const Basis Turned = Basis(Vector3(0.0f, 1.0f, 0.0f), Steer) * Basis(Vector3(1.0f, 0.0f, 0.0f), -W.SpinAngle);
	return Transform3D(Turned, Sim->GodotWheels[Wheel] + Vector3(0.0f, W.Compression, 0.0f));
}

String GeodriveCar::get_status() const
{
	const geocar::VehicleState& S = Sim->Sim.GetState();
	const String Gear = S.Gear < 0 ? String("R") : S.Gear == 0 ? String("N") : String::num_int64(S.Gear);
	return vformat("%d km/h  gear %s%s  %d rpm%s%s%s", int(std::lround(std::fabs(S.SpeedMS) * 3.6)), Gear, S.bAutomatic ? " (auto)" : "",
		int(std::lround(S.EngineRPM)), Sim->Controls.bDiffLock ? "  diff locked" : "", S.bTCActive ? "  TC" : "",
		S.Wheels[0].bABSActive || S.Wheels[1].bABSActive ? "  ABS" : "");
}

String GeodriveCar::get_debug() const
{
	const geocar::VehicleState& S = Sim->Sim.GetState();
	const Vector3 Forward = -get_global_transform().basis.get_column(2);
	String Out = vformat("speed %.2f m/s, pitch %.1f deg, rpm %d, gear %d", S.SpeedMS, Math::rad_to_deg(std::asin(std::clamp(double(Forward.y), -1.0, 1.0))),
		int(S.EngineRPM), S.Gear);
	for (int i = 0; i < geocar::NumWheels; ++i)
	{
		const geocar::WheelState& W = S.Wheels[i];
		Out += vformat("\n  wheel %d: contact %d load %d N, compression %d mm, omega %.1f, slip %.3f, Fx %d, Fy %d", i, W.bContact ? 1 : 0,
			int(W.Load), int(W.Compression * 1000.0f), W.AngularVelocity, W.SlipRatio, int(W.ForceX), int(W.ForceY));
	}
	return Out;
}
