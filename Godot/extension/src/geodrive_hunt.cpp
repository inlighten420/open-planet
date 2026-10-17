// Geodrive Godot extension - GPL (see Core/README.md)

#include "geodrive_hunt.h"

#include "geodrive_world.h"

#include "GeoHunt/CreatureFile.h"
#include "GeoHunt/Hunt.h"

#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>

using namespace godot;

void GeodriveHunt::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("start", "world", "outline", "water", "stock", "seed", "avoid_buildings", "spawn"), &GeodriveHunt::start, DEFVAL(false), DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("set_people", "at", "on_foot"), &GeodriveHunt::set_people, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("set_cars", "cars"), &GeodriveHunt::set_cars);
	ClassDB::bind_method(D_METHOD("put_back", "near", "within"), &GeodriveHunt::put_back);
	ClassDB::bind_method(D_METHOD("take_hits"), &GeodriveHunt::take_hits);
	ClassDB::bind_method(D_METHOD("take_caught"), &GeodriveHunt::take_caught);
	ClassDB::bind_method(D_METHOD("stop"), &GeodriveHunt::stop);
	ClassDB::bind_method(D_METHOD("is_running"), &GeodriveHunt::is_running);
	ClassDB::bind_method(D_METHOD("step", "delta"), &GeodriveHunt::step);
	ClassDB::bind_method(D_METHOD("draw", "eye", "reach", "zoom"), &GeodriveHunt::draw, DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("shoot", "load", "eye", "aim", "exclude", "month", "taken"), &GeodriveHunt::shoot);
	ClassDB::bind_method(D_METHOD("take_news"), &GeodriveHunt::take_news);
	ClassDB::bind_method(D_METHOD("heard", "at"), &GeodriveHunt::heard);
	ClassDB::bind_method(D_METHOD("pack"), &GeodriveHunt::pack);
	ClassDB::bind_method(D_METHOD("apply", "bytes"), &GeodriveHunt::apply);
	ClassDB::bind_method(D_METHOD("set_follower", "on"), &GeodriveHunt::set_follower);
	ClassDB::bind_method(D_METHOD("is_follower"), &GeodriveHunt::is_follower);
	ClassDB::bind_method(D_METHOD("apply_shot", "index", "at", "down", "dies_after", "wounded"), &GeodriveHunt::apply_shot);
	ClassDB::bind_method(D_METHOD("get_count"), &GeodriveHunt::get_count);
	ClassDB::bind_method(D_METHOD("animal", "index"), &GeodriveHunt::animal);
	ClassDB::bind_method(D_METHOD("shift", "offset"), &GeodriveHunt::shift);
	ClassDB::bind_static_method("GeodriveHunt", D_METHOD("species_list"), &GeodriveHunt::species_list);
	ClassDB::bind_static_method("GeodriveHunt", D_METHOD("loads"), &GeodriveHunt::loads);
	ClassDB::bind_static_method("GeodriveHunt", D_METHOD("species_data", "id"), &GeodriveHunt::species_data);
	ClassDB::bind_static_method("GeodriveHunt", D_METHOD("add_species", "data"), &GeodriveHunt::add_species);
	ClassDB::bind_static_method("GeodriveHunt", D_METHOD("species_text", "id"), &GeodriveHunt::species_text);
	ClassDB::bind_static_method("GeodriveHunt", D_METHOD("body_parts", "id", "head_down"), &GeodriveHunt::body_parts, DEFVAL(0.0));
	ClassDB::bind_static_method("GeodriveHunt", D_METHOD("add_load", "data"), &GeodriveHunt::add_load);
	ClassDB::bind_static_method("GeodriveHunt", D_METHOD("read_file", "text", "kind", "where"), &GeodriveHunt::read_file);
	ClassDB::bind_static_method("GeodriveHunt", D_METHOD("sound", "kind", "rate"), &GeodriveHunt::sound);
}

GeodriveHunt::GeodriveHunt() : Random(std::random_device{}())
{
	// the animals' parts: rounded (an ellipsoid each: a fine one near, a coarse one far) and the horns (cones), sized,
	// turned and coloured by their copies (three draw calls for them all), their coats painted by animal.gdshader
	const Ref<Shader> CoatShader = ResourceLoader::get_singleton()->load("res://animal.gdshader");
	Ref<ShaderMaterial> Coat;
	Coat.instantiate();
	Coat->set_shader(CoatShader);
	for (int k = 0; k < 4; ++k)
	{
		Bodies[k] = memnew(MultiMeshInstance3D);
		Bodies[k]->set_name(k == 0 ? "Animals" : k == 1 ? "AnimalsFarther" : k == 2 ? "Horns" : "AnimalsFar");
		Ref<MultiMesh> Mesh;
		Mesh.instantiate();
		Mesh->set_transform_format(MultiMesh::TRANSFORM_3D);
		Mesh->set_use_colors(true);
		Mesh->set_use_custom_data(true);
		if (k != 2)
		{
			// (~140 triangles near, ~40 far)
			Ref<SphereMesh> Round;
			Round.instantiate();
			Round->set_radius(0.5f);
			Round->set_height(1.0f);
			Round->set_radial_segments(k == 0 ? 12 : 6);
			Round->set_rings(k == 0 ? 6 : 3);
			if (k == 3)
			{
				// (far off their shadows are under a pixel, and would cost the sun's every cascade)
				Bodies[k]->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			}
			Mesh->set_mesh(Round);
		}
		else
		{
			Ref<CylinderMesh> Cone;
			Cone.instantiate();
			Cone->set_top_radius(0.0f);
			Cone->set_bottom_radius(0.5f);
			Cone->set_height(1.0f);
			Cone->set_radial_segments(6);
			Cone->set_rings(0);
			Cone->set_cap_top(false);
			Mesh->set_mesh(Cone);
		}
		Bodies[k]->set_multimesh(Mesh);
		Bodies[k]->set_material_override(Coat);
		add_child(Bodies[k]);
	}
}

GeodriveHunt::~GeodriveHunt() = default;

double GeodriveHunt::GroundAt(double X, double Y) const
{
	const GeodriveWorld* W = Object::cast_to<GeodriveWorld>(World);
	return W ? W->ground_height(ToGodot(X, Y, 0.0)) : 0.0;
}

void GeodriveHunt::start(Node* InWorld, const PackedVector3Array& Outline, const PackedVector3Array& Water, const Array& Stock, int Seed, bool bAvoidBuildings, const Dictionary& Spawn)
{
	World = InWorld;
	// the farm's middle, and its outline and water in its own frame
	Vector3 Sum;
	for (int64_t i = 0; i < Outline.size(); ++i)
	{
		Sum += Outline[i];
	}
	Centre = Outline.size() > 0 ? Sum / float(Outline.size()) : Vector3();
	Centre.y = 0.0f;
	geohunt::Range Farm;
	for (int64_t i = 0; i < Outline.size(); ++i)
	{
		Farm.Outline.emplace_back(Outline[i].x - Centre.x, -(Outline[i].z - Centre.z));
	}
	for (int64_t i = 0; i < Water.size(); ++i)
	{
		Farm.Water.emplace_back(Water[i].x - Centre.x, -(Water[i].z - Centre.z));
	}
	// (a town: its buildings kept out of, a grid of 2 m squares)
	if (bAvoidBuildings && Outline.size() > 0)
	{
		const double T0 = Time::get_singleton()->get_ticks_usec();
		Vector3 Min = Outline[0], Max = Outline[0];
		for (int64_t i = 0; i < Outline.size(); ++i)
		{
			Min = Min.min(Outline[i]);
			Max = Max.max(Outline[i]);
		}
		std::vector<std::vector<Vector3>> Footprints;
		if (GeodriveWorld* Map = Object::cast_to<GeodriveWorld>(InWorld))
		{
			Map->footprints(Min, Max, Footprints);
		}
		std::vector<geohunt::Vec2> Polygon;
		for (const std::vector<Vector3>& Footprint : Footprints)
		{
			Polygon.clear();
			for (const Vector3& Corner : Footprint)
			{
				Polygon.emplace_back(Corner.x - Centre.x, -(Corner.z - Centre.z));
			}
			Farm.Block(Polygon);
		}
		UtilityFunctions::print("Geodrive: kept out of ", int64_t(Footprints.size()), " buildings (", Farm.Columns, " x ", Farm.Rows,
			" squares of 2 m) in ", int64_t((Time::get_singleton()->get_ticks_usec() - T0) / 1000.0), " ms");
	}
	std::vector<std::pair<int, int>> Kinds;
	for (int64_t i = 0; i < Stock.size(); ++i)
	{
		const Array Entry = Stock[i];
		const int Kind = geohunt::FindSpecies(String(Entry[0]).utf8().get_data());
		if (Kind >= 0 && int(Entry[1]) > 0)
		{
			Kinds.emplace_back(Kind, int(Entry[1]));
		}
	}
	Herds = std::make_unique<geohunt::Herds>();
	Herds->Start(Farm, Kinds, uint32_t(Seed));
	// (a pool round the people, if the area's file says so)
	geohunt::Spawning How;
	How.bAroundPeople = Spawn.get("around_people", false);
	How.Nearest = Spawn.get("nearest", How.Nearest);
	How.Farthest = Spawn.get("farthest", How.Farthest);
	How.LeavePast = Spawn.get("leave_past", How.LeavePast);
	How.DeadFor = Spawn.get("dead_for", How.DeadFor);
	Herds->SetSpawning(How);
	Ground.assign(Herds->Animals().size(), 0.0);
	for (MultiMeshInstance3D* Part : Bodies)
	{
		Part->get_multimesh()->set_visible_instance_count(0);
	}
	// (its box: round the farm, from a little under its lowest corner to over its highest, set as they move: a box
	// far bigger than that put it in every shadow cascade at a huge depth, ~16 ms a frame on the graphics card)
	FitBox();
}

void GeodriveHunt::set_people(const PackedVector3Array& At, const Array& OnFoot)
{
	if (!Herds)
	{
		return;
	}
	std::vector<geohunt::Vec2> People;
	std::vector<bool> Catchable;
	for (int64_t i = 0; i < At.size(); ++i)
	{
		People.emplace_back(At[i].x - Centre.x, -(At[i].z - Centre.z));
		Catchable.push_back(i < OnFoot.size() ? bool(OnFoot[i]) : true);
	}
	Herds->SetPeople(People, Catchable);
}

void GeodriveHunt::set_cars(const Array& Cars)
{
	if (!Herds)
	{
		return;
	}
	std::vector<geohunt::Car> Now;
	for (int64_t i = 0; i < Cars.size(); ++i)
	{
		const Dictionary D = Cars[i];
		const Vector3 At = D.get("position", Vector3());
		const Vector3 Forward = D.get("forward", Vector3(1, 0, 0));
		const Vector3 Velocity = D.get("velocity", Vector3());
		geohunt::Car C;
		C.At = geohunt::Vec2(At.x - Centre.x, -(At.z - Centre.z));
		C.Forward = geohunt::Vec2(Forward.x, -Forward.z);
		C.Velocity = geohunt::Vec2(Velocity.x, -Velocity.z);
		C.HalfLength = D.get("half_length", C.HalfLength);
		C.HalfWidth = D.get("half_width", C.HalfWidth);
		C.Mass = D.get("mass", C.Mass);
		Now.push_back(C);
	}
	Herds->SetCars(Now);
}

void GeodriveHunt::put_back(const Vector3& Near, double Within)
{
	if (Herds && !bFollower)
	{
		Herds->PutBack(geohunt::Vec2(Near.x - Centre.x, -(Near.z - Centre.z)), Within);
	}
}

Array GeodriveHunt::take_hits()
{
	Array Out;
	if (!Herds)
	{
		return Out;
	}
	for (const geohunt::CarHit& H : Herds->TakeHits())
	{
		const geohunt::Species& S = geohunt::AllSpecies()[size_t(Herds->Animals()[size_t(H.Index)].Species)];
		Dictionary D;
		D["index"] = H.Index;
		D["car"] = H.CarIndex;
		D["species"] = String::utf8(S.Id.c_str());
		D["name"] = String::utf8(S.Name.c_str());
		D["speed"] = H.Speed;
		D["way"] = Vector3(float(H.Way.X), 0.0f, float(-H.Way.Y));
		D["killed"] = H.bKilled;
		D["wounded"] = H.bWounded;
		D["car_loses"] = H.CarLoses;
		Out.push_back(D);
	}
	return Out;
}

PackedInt32Array GeodriveHunt::take_caught()
{
	PackedInt32Array Out;
	if (Herds)
	{
		for (const int Person : Herds->TakeCaught())
		{
			Out.push_back(Person);
		}
	}
	return Out;
}

void GeodriveHunt::stop()
{
	Herds.reset();
	Ground.clear();
	for (MultiMeshInstance3D* Part : Bodies)
	{
		Part->get_multimesh()->set_visible_instance_count(0);
		Part->get_multimesh()->set_instance_count(0);
	}
}

bool GeodriveHunt::is_running() const { return Herds != nullptr; }

void GeodriveHunt::step(double Dt)
{
	if (!Herds)
	{
		return;
	}
	// (in steps of at most a tenth of a second; following the host's, only carried on)
	for (double Left = Dt; Left > 1e-6;)
	{
		const double Part = std::min(Left, 0.1);
		if (bFollower)
		{
			Herds->Follow(Part);
		}
		else
		{
			Herds->Step(Part);
		}
		Left -= Part;
	}
}

namespace
{
	// A part's colour: the coat; hooves, horns and a muzzle darker; a mane darker still (a lion's a darker gold); and how
	// its coat's pattern lies on it (animal.gdshader: 0 along the body, 1 down a leg or the neck, 2 none)
	void PartLook(const geohunt::Species& S, const geohunt::BodyPart& P, float& R, float& G, float& B, float& Lies)
	{
		using geohunt::PartKind;
		R = S.R, G = S.G, B = S.B, Lies = 0.0f;
		switch (P.Kind)
		{
		case PartKind::Hoof:
		case PartKind::Horn:
			R = 0.035f, G = 0.03f, B = 0.025f, Lies = 2.0f;
			break;
		case PartKind::Muzzle:
			R *= 0.55f, G *= 0.55f, B *= 0.55f;
			break;
		case PartKind::Mane:
			if (S.Shape.ManeKind == geohunt::BodyShape::Mane::Lion)
			{
				R *= 0.55f, G *= 0.4f, B *= 0.3f;
			}
			else
			{
				R = 0.03f, G = 0.03f, B = 0.03f;
			}
			Lies = 2.0f;
			break;
		case PartKind::Ear:
			R *= 0.85f, G *= 0.85f, B *= 0.85f;
			Lies = 1.0f;
			break;
		case PartKind::Neck:
		case PartKind::Leg:
		case PartKind::Shin:
		case PartKind::Tail:
			Lies = 1.0f;
			break;
		default:
			break;
		}
	}

	// A part's frame in Godot's for an animal at Base facing Forward (its left Left): the mesh's x the part's x, its y
	// (the long axis, a cone's tip) the part's z, its z the part's -y (turned about z after: a head looking round)
	Transform3D PartFrame(const geohunt::BodyPart& P, const Vector3& Base, const Vector3& Forward, const Vector3& Left)
	{
		const Vector3 Up(0.0f, 1.0f, 0.0f);
		auto Into = [&](const geohunt::Vec3& V) { return Forward * float(V.X) + Left * float(V.Y) + Up * float(V.Z); };
		auto Roll = [&P](const geohunt::Vec3& V)
		{
			const geohunt::Vec3 R(V.X, V.Y * std::cos(P.Roll) - V.Z * std::sin(P.Roll), V.Y * std::sin(P.Roll) + V.Z * std::cos(P.Roll));
			return geohunt::Vec3(R.X * std::cos(P.Yaw) - R.Y * std::sin(P.Yaw), R.X * std::sin(P.Yaw) + R.Y * std::cos(P.Yaw), R.Z);
		};
		const Vector3 AX = Into(Roll(geohunt::Vec3(std::cos(P.Pitch), 0.0, std::sin(P.Pitch)))) * float(P.Half.X * 2.0);
		const Vector3 AY = Into(Roll(geohunt::Vec3(0.0, 1.0, 0.0))) * float(P.Half.Y * 2.0);
		const Vector3 AZ = Into(Roll(geohunt::Vec3(-std::sin(P.Pitch), 0.0, std::cos(P.Pitch)))) * float(P.Half.Z * 2.0);
		return Transform3D(Basis(AX, AZ, -AY), Base + Into(P.Centre));	// (Basis(x, y, z): its columns)
	}
}

Array GeodriveHunt::body_parts(const String& Id, double HeadDown)
{
	Array Out;
	const int I = geohunt::FindSpecies(Id.utf8().get_data());
	if (I < 0)
	{
		return Out;
	}
	const geohunt::Species& S = geohunt::AllSpecies()[size_t(I)];
	geohunt::Pose Standing;
	Standing.HeadDown = HeadDown;
	for (const geohunt::BodyPart& P : geohunt::Body(S, Standing))
	{
		float R, G, B, Lies;
		PartLook(S, P, R, G, B, Lies);
		Dictionary D;
		D["frame"] = PartFrame(P, Vector3(), Vector3(0.0f, 0.0f, -1.0f), Vector3(-1.0f, 0.0f, 0.0f));
		D["colour"] = Color(R, G, B);
		D["horn"] = P.Kind == geohunt::PartKind::Horn;
		Out.push_back(D);
	}
	return Out;
}

void GeodriveHunt::draw(const Vector3& Eye, double Reach, double Zoom)
{
	if (!Herds)
	{
		return;
	}
	const auto& Kinds = geohunt::AllSpecies();
	const auto& Animals = Herds->Animals();
	// (each copy: its transform's 12, its colour's 4, its own 4: the pattern, how the part lies, the head, the animal's own)
	constexpr int Floats = 20;
	const int Capacity = int(Animals.size()) * 26;
	PackedFloat32Array Buffers[4];
	float* Out[4];
	int Count[4] = { 0, 0, 0, 0 };
	for (int k = 0; k < 4; ++k)
	{
		Buffers[k].resize(int64_t(Capacity) * Floats);
		Out[k] = Buffers[k].ptrw();
	}
	// (near: within ~80 m as it looks, the fine mesh and every part; beyond, the coarse mesh, and past ~300 m the small
	// parts left out (an ear or hoof there is under a pixel) and no shadows)
	const double Near = 80.0 * std::max(1.0, Zoom), Far = 300.0 * std::max(1.0, Zoom);
	PhysicsDirectSpaceState3D* Space = is_inside_tree() && get_world_3d().is_valid() ? get_world_3d()->get_direct_space_state() : nullptr;
	for (size_t i = 0; i < Animals.size(); ++i)
	{
		const geohunt::Animal& A = Animals[i];
		const Vector3 Feet = ToGodot(A.At.X, A.At.Y, 0.0);
		const double Distance = Vector2(Feet.x - Eye.x, Feet.z - Eye.z).length();
		if (Distance > Reach)
		{
			continue;
		}
		Ground[i] = GroundAt(A.At.X, A.At.Y);
		// (near: on what's really there underfoot, as you are: a road or a pavement stands a little over the ground)
		if (Distance < 120.0 && Space)
		{
			const Vector3 Over(Feet.x, float(Ground[i]) + 2.5f, Feet.z), Under(Feet.x, float(Ground[i]) - 1.0f, Feet.z);
			const Dictionary Hit = Space->intersect_ray(PhysicsRayQueryParameters3D::create(Over, Under));
			// (only the world's own: not a car, not someone)
			if (!Hit.is_empty() && Object::cast_to<StaticBody3D>(Object::cast_to<Object>(Hit["collider"])))
			{
				Ground[i] = double(Vector3(Hit["position"]).y);
			}
		}
		const geohunt::Species& S = Kinds[size_t(A.Species)];
		const float Pattern = float(int(geohunt::PatternOf(S)));
		const float Own = float((i * 2654435761u) % 1000u) / 1000.0f;
		// its frame in Godot's: forward (its heading: from east, anticlockwise), left, up
		const Vector3 Forward(float(std::cos(A.Heading)), 0.0f, float(-std::sin(A.Heading)));
		const Vector3 Left(float(-std::sin(A.Heading)), 0.0f, float(-std::cos(A.Heading)));
		const Vector3 Up(0.0f, 1.0f, 0.0f);
		auto Into = [&](const geohunt::Vec3& V) { return Forward * float(V.X) + Left * float(V.Y) + Up * float(V.Z); };
		const Vector3 Base(Feet.x, float(Ground[i]), Feet.z);
		const bool bFine = Distance < Near, bSmall = Distance < Far;
		for (const geohunt::BodyPart& P : geohunt::Body(S, A.Now))
		{
			using geohunt::PartKind;
			const bool bHorn = P.Kind == PartKind::Horn;
			if (!bSmall && (P.Kind == PartKind::Ear || P.Kind == PartKind::Hoof || P.Kind == PartKind::Tail || (bHorn && P.Half.Z < 0.2)))
			{
				continue;
			}
			const int k = bHorn ? 2 : bFine ? 0 : bSmall ? 1 : 3;
			if (Count[k] >= Capacity)
			{
				continue;
			}
			// the part's axes in the animal's frame, then Godot's (PartFrame); its colour (PartLook)
			const Transform3D F = PartFrame(P, Base, Forward, Left);
			float* T = Out[k] + size_t(Count[k]) * Floats;
			const float Rows[12] = { F.basis[0][0], F.basis[0][1], F.basis[0][2], F.origin.x, F.basis[1][0], F.basis[1][1], F.basis[1][2], F.origin.y,
				F.basis[2][0], F.basis[2][1], F.basis[2][2], F.origin.z };
			std::copy(Rows, Rows + 12, T);
			float R, G, B, Lies;
			PartLook(S, P, R, G, B, Lies);
			T[12] = R, T[13] = G, T[14] = B, T[15] = 1.0f;
			T[16] = Pattern, T[17] = Lies, T[18] = P.Kind == PartKind::Head ? 1.0f : 0.0f, T[19] = Own;
			++Count[k];
		}
	}
	for (int k = 0; k < 4; ++k)
	{
		Ref<MultiMesh> Mesh = Bodies[k]->get_multimesh();
		if (Mesh->get_instance_count() != Capacity)
		{
			Mesh->set_instance_count(Capacity);
		}
		// (the unused rest zeroed: the buffer is the whole capacity's)
		std::fill(Out[k] + size_t(Count[k]) * Floats, Out[k] + size_t(Capacity) * Floats, 0.0f);
		Mesh->set_buffer(Buffers[k]);
		Mesh->set_visible_instance_count(Count[k]);
	}
	FitBox();
}

void GeodriveHunt::FitBox()
{
	if (!Herds || Herds->Animals().empty())
	{
		return;
	}
	Vector3 Low(1e30f, 1e30f, 1e30f), High(-1e30f, -1e30f, -1e30f);
	for (size_t i = 0; i < Herds->Animals().size(); ++i)
	{
		const geohunt::Animal& A = Herds->Animals()[i];
		const Vector3 P(ToGodot(A.At.X, A.At.Y, 0.0).x, float(i < Ground.size() ? Ground[i] : 0.0), ToGodot(A.At.X, A.At.Y, 0.0).z);
		Low = Low.min(P);
		High = High.max(P);
	}
	// (to_local: the node's at Godot's 0, but kept right if it isn't; the tallest, a giraffe's head, ~5 m)
	for (MultiMeshInstance3D* Part : Bodies)
	{
		Part->set_custom_aabb(AABB(Part->to_local(Low) - Vector3(10.0f, 5.0f, 10.0f), High - Low + Vector3(20.0f, 15.0f, 20.0f)));
	}
}

Dictionary GeodriveHunt::shoot(const String& LoadId, const Vector3& Eye, const Vector3& Aim, const Array& Exclude, int Month, const Dictionary& Taken)
{
	Dictionary Result;
	Result["hit"] = false;
	const int LoadIndex = geohunt::FindLoad(LoadId.utf8().get_data());
	if (LoadIndex < 0)
	{
		return Result;
	}
	const geohunt::Load& L = geohunt::AllLoads()[size_t(LoadIndex)];
	// the shot in the farm's frame
	const geohunt::Vec3 From(Eye.x - Centre.x, -(Eye.z - Centre.z), Eye.y);
	const geohunt::Vec3 Along(Aim.x, -Aim.z, Aim.y);
	const std::vector<geohunt::ShotPoint> Path = geohunt::Fly(L, From, Along, L.bArrow ? 3.0 : 1.5, 0.002, [](double, double) { return -1.0e9; });
	PhysicsDirectSpaceState3D* Space = get_world_3d().is_valid() ? get_world_3d()->get_direct_space_state() : nullptr;
	const auto& Kinds = geohunt::AllSpecies();
	PackedVector3Array Trail;
	Trail.push_back(ToGodot(From.X, From.Y, From.Z));
	int Struck = -1;
	geohunt::Zone StruckZone = geohunt::Zone::None;
	geohunt::Vec3 StruckAt;
	double StruckSpeed = 0.0;
	int64_t StruckCollider = 0;	// (stopped by something solid: what (its instance id): the ground, a building, a person)
	bool bStopped = false;
	for (size_t s = 1; s < Path.size() && !bStopped; ++s)
	{
		const geohunt::Vec3 A = Path[s - 1].At, B = Path[s].At;
		double Nearest = 2.0;	// (0-1 along this step)
		// the animals near this step: into each one's frame, against its body
		if (Herds)
		{
			const auto& Animals = Herds->Animals();
			for (size_t i = 0; i < Animals.size(); ++i)
			{
				const geohunt::Animal& An = Animals[i];
				const geohunt::Species& S = Kinds[size_t(An.Species)];
				const geohunt::Vec2 Mid((A.X + B.X) * 0.5, (A.Y + B.Y) * 0.5);
				if ((Mid - An.At).Length() > S.Length + (B - A).Length())
				{
					continue;
				}
				const double Floor = Ground[i] != 0.0 ? Ground[i] : GroundAt(An.At.X, An.At.Y);
				const double C = std::cos(An.Heading), Sn = std::sin(An.Heading);
				auto Local = [&](const geohunt::Vec3& P)
				{
					const double DX = P.X - An.At.X, DY = P.Y - An.At.Y;
					return geohunt::Vec3(DX * C + DY * Sn, -DX * Sn + DY * C, P.Z - Floor);
				};
				geohunt::Vec3 Where;
				geohunt::Zone Z;
				if (geohunt::HitBody(S, An.Now, Local(A), Local(B), Where, Z))
				{
					const geohunt::Vec3 LA = Local(A), LB = Local(B);
					const double T = (Where - LA).Length() / std::max((LB - LA).Length(), 1e-9);
					if (T < Nearest)
					{
						Nearest = T;
						Struck = int(i);
						StruckCollider = 0;
						StruckZone = Z;
						StruckAt = A + (B - A) * T;
						StruckSpeed = Path[s - 1].Speed + (Path[s].Speed - Path[s - 1].Speed) * T;
					}
				}
			}
		}
		// the ground, buildings, the fence, trees: Godot's physics along this step
		if (Space)
		{
			Ref<PhysicsRayQueryParameters3D> Ray = PhysicsRayQueryParameters3D::create(ToGodot(A.X, A.Y, A.Z), ToGodot(B.X, B.Y, B.Z));
			TypedArray<RID> Skip;
			for (int64_t k = 0; k < Exclude.size(); ++k)
			{
				Skip.push_back(Exclude[k]);
			}
			Ray->set_exclude(Skip);
			const Dictionary Hit = Space->intersect_ray(Ray);
			if (!Hit.is_empty())
			{
				const Vector3 P = Hit["position"];
				const geohunt::Vec3 Q(P.x - Centre.x, -(P.z - Centre.z), P.y);
				const double T = (Q - A).Length() / std::max((B - A).Length(), 1e-9);
				if (T < Nearest)
				{
					Nearest = T;
					Struck = -1;
					StruckAt = Q;
					StruckSpeed = Path[s].Speed;
					StruckCollider = int64_t(Hit["collider_id"]);
				}
			}
		}
		if (Nearest <= 1.0)
		{
			bStopped = true;
			Trail.push_back(ToGodot(StruckAt.X, StruckAt.Y, StruckAt.Z));
			break;
		}
		if (s % 8 == 0)
		{
			Trail.push_back(ToGodot(B.X, B.Y, B.Z));
		}
	}
	Result["path"] = Trail;
	// (a rifle's heard all round: the game within earshot looks up towards it; a bow's near silent)
	if (Herds && !L.bArrow)
	{
		Herds->Heard(geohunt::Vec2(From.X, From.Y));
	}
	if (!bStopped)
	{
		return Result;
	}
	const double Distance = (StruckAt - From).Length();
	Result["point"] = ToGodot(StruckAt.X, StruckAt.Y, StruckAt.Z);
	Result["distance"] = Distance;
	Result["speed"] = StruckSpeed;
	Result["energy"] = geohunt::EnergyFtLb(L.Grains, StruckSpeed);
	if (Struck < 0)
	{
		Result["collider_id"] = StruckCollider;
		// into the ground (or a building): the herd nearest it, if within 15 m, runs
		if (Herds)
		{
			int Nearest = -1;
			double Least = 15.0;
			for (size_t i = 0; i < Herds->Animals().size(); ++i)
			{
				const double D = (Herds->Animals()[i].At - geohunt::Vec2(StruckAt.X, StruckAt.Y)).Length();
				if (D < Least)
				{
					Least = D;
					Nearest = int(i);
				}
			}
			if (Nearest >= 0)
			{
				Herds->Shot(Nearest, geohunt::Vec2(From.X, From.Y), nullptr);
			}
		}
		return Result;
	}
	const geohunt::Animal& An = Herds->Animals()[size_t(Struck)];
	const geohunt::Species& S = Kinds[size_t(An.Species)];
	const double Energy = geohunt::EnergyFtLb(L.Grains, StruckSpeed);
	const int AlreadyTaken = int(Taken.get(String::utf8(S.Id.c_str()), 0));
	const geohunt::Verdict V = geohunt::Judge(S, Month, AlreadyTaken, L.bArrow, L.Grains, Energy, false, L.Action == "auto");
	const bool bWasDead = An.What == geohunt::Doing::Dead;
	// (what it did: Outcome's, and one wound too many is down)
	const geohunt::Effect Rolled = geohunt::Outcome(S, StruckZone, Energy, Random);
	const geohunt::Effect E = Herds->Shot(Struck, geohunt::Vec2(From.X, From.Y), &Rolled);
	Result["hit"] = true;
	Result["index"] = Struck;
	Result["species"] = String::utf8(S.Id.c_str());
	Result["name"] = String::utf8(S.Name.c_str());
	Result["zone"] = String(geohunt::Name(StruckZone));
	Result["legal"] = V == geohunt::Verdict::Legal;
	Result["why"] = String(geohunt::Why(V));
	Result["down"] = E.bDown;
	Result["dies"] = E.DiesAfter >= 0.0;
	Result["dies_after"] = E.DiesAfter;
	Result["wounded"] = E.bWounded;
	Result["flees"] = S.Acts.bFlees;
	Result["already_dead"] = bWasDead;
	return Result;
}

PackedStringArray GeodriveHunt::take_news()
{
	PackedStringArray Out;
	if (Herds)
	{
		for (const std::string& N : Herds->TakeNews())
		{
			Out.push_back(String::utf8(N.c_str()));
		}
	}
	return Out;
}

int GeodriveHunt::get_count() const { return Herds ? int(Herds->Animals().size()) : 0; }

Dictionary GeodriveHunt::animal(int Index) const
{
	Dictionary Out;
	if (!Herds || Index < 0 || Index >= int(Herds->Animals().size()))
	{
		return Out;
	}
	const geohunt::Animal& A = Herds->Animals()[size_t(Index)];
	const geohunt::Species& S = geohunt::AllSpecies()[size_t(A.Species)];
	Out["species"] = String::utf8(S.Id.c_str());
	Out["name"] = String::utf8(S.Name.c_str());
	// (a lion "grazing" is lying at rest)
	Out["doing"] = A.What == geohunt::Doing::Graze && geohunt::AllSpecies()[size_t(A.Species)].Acts.bRests ? String("resting") : String(geohunt::Name(A.What));
	// (where it stands as drawn and as shots meet it: on what's underfoot near you, else the ground)
	const double Floor = size_t(Index) < Ground.size() && Ground[size_t(Index)] != 0.0 ? Ground[size_t(Index)] : GroundAt(A.At.X, A.At.Y);
	Out["position"] = Vector3(ToGodot(A.At.X, A.At.Y, 0.0).x, float(Floor), ToGodot(A.At.X, A.At.Y, 0.0).z);
	Out["facing"] = Vector3(float(std::cos(A.Heading)), 0.0f, float(-std::sin(A.Heading)));	// (Godot's frame)
	Out["dead"] = A.What == geohunt::Doing::Dead;
	Out["shot"] = A.bShot;
	Out["wounded"] = A.bWounded;
	return Out;
}

void GeodriveHunt::shift(const Vector3& Offset)
{
	Centre += Vector3(Offset.x, 0.0f, Offset.z);
	for (double& Y : Ground)
	{
		Y += Offset.y;
	}
}

Array GeodriveHunt::species_list()
{
	Array Out;
	for (const geohunt::Species& S : geohunt::AllSpecies())
	{
		Dictionary D;
		D["id"] = String::utf8(S.Id.c_str());
		D["name"] = String::utf8(S.Name.c_str());
		D["mass"] = S.Mass;
		D["shoulder"] = S.Shoulder;
		D["length"] = S.Length;
		D["protected"] = S.bProtected;
		D["season"] = Array::make(S.SeasonFrom, S.SeasonTo);
		D["bag"] = S.BagPerDay;
		Out.push_back(D);
	}
	return Out;
}

// --- a pack's files: read and written by Core (CreatureFile.h), as every host reads them ---

namespace
{
	// Core's JSON as Godot's (a dictionary) and back, by its text
	Dictionary AsDictionary(const geojson::Value& V)
	{
		const Variant Parsed = JSON::parse_string(String::utf8(geojson::Write(V).c_str()));
		return Parsed.get_type() == Variant::DICTIONARY ? Dictionary(Parsed) : Dictionary();
	}

	bool AsValue(const Dictionary& Data, geojson::Value& Out, std::string& Error)
	{
		const String Text = JSON::stringify(Data, "", false, true);	// (full precision: read back the same)
		return geojson::Parse(Text.utf8().get_data(), Out, Error);
	}

	void Tell(const String& Kind, const std::vector<std::string>& Errors, const String& Where)
	{
		for (const std::string& E : Errors)
		{
			UtilityFunctions::push_error("Geodrive: " + Kind + (Where.is_empty() ? String() : " in " + Where) + ": " + String::utf8(E.c_str()));
		}
	}
}

Dictionary GeodriveHunt::species_data(const String& Id)
{
	const int I = geohunt::FindSpecies(Id.utf8().get_data());
	return I < 0 ? Dictionary() : AsDictionary(geohunt::CreatureValue(geohunt::AllSpecies()[size_t(I)]));
}

String GeodriveHunt::species_text(const String& Id)
{
	const int I = geohunt::FindSpecies(Id.utf8().get_data());
	return I < 0 ? String() : String::utf8(geojson::Write(geohunt::CreatureValue(geohunt::AllSpecies()[size_t(I)])).c_str());
}

int GeodriveHunt::add_species(const Dictionary& Data)
{
	geojson::Value V;
	std::string Error;
	int Index = AsValue(Data, V, Error) ? geohunt::ReadCreature(V, Error) : -1;
	if (Index < 0)
	{
		Tell("creature", { String(Data.get("id", "?")).utf8().get_data() + std::string(": ") + Error }, String());
	}
	return Index;
}

int GeodriveHunt::add_load(const Dictionary& Data)
{
	geojson::Value V;
	std::string Error;
	int Index = AsValue(Data, V, Error) ? geohunt::ReadLoad(V, Error) : -1;
	if (Index < 0)
	{
		Tell("weapon", { String(Data.get("id", "?")).utf8().get_data() + std::string(": ") + Error }, String());
	}
	return Index;
}

int GeodriveHunt::read_file(const String& Text, const String& Kind, const String& Where)
{
	std::vector<std::string> Errors;
	const std::string Bytes = Text.utf8().get_data();
	const int Taken = Kind == "weapons" ? geohunt::ReadLoads(Bytes, Errors) : geohunt::ReadCreatures(Bytes, Errors);
	Tell(Kind == "weapons" ? "weapon" : "creature", Errors, Where);
	return Taken;
}

Array GeodriveHunt::loads()
{
	Array Out;
	for (const geohunt::Load& L : geohunt::AllLoads())
	{
		Dictionary D;
		D["id"] = String::utf8(L.Id.c_str());
		D["name"] = String::utf8(L.Name.c_str());
		D["grains"] = L.Grains;
		D["muzzle"] = L.Muzzle;
		D["zero"] = L.ZeroAt;
		D["magazine"] = L.Magazine;
		D["arrow"] = L.bArrow;
		D["energy"] = geohunt::EnergyFtLb(L.Grains, L.Muzzle);
		D["bc"] = L.BC;
		D["sight_height"] = L.SightHeight;
		D["action"] = String::utf8(L.Action.c_str());
		D["rate"] = L.Rate;
		D["rounds"] = L.Rounds;
		D["reload_time"] = L.ReloadTime;
		D["sight"] = String::utf8(L.Sight.c_str());
		D["kick"] = L.Kick;
		D["model"] = String::utf8(L.Model.c_str());
		if (!L.Look.empty())
		{
			D["look"] = JSON::parse_string(String::utf8(L.Look.c_str()));	// (its parts and its sight: part_model.gd's)
		}
		// (what it carries: its energy 100 m out, and how far it drops there under the sights, for the workshop)
		geohunt::Vec3 Position(0.0, 0.0, 0.0), Velocity;
		const double Tilt = geohunt::ZeroAngle(L);
		Position.Z = -L.SightHeight;
		Velocity = geohunt::Vec3(std::cos(Tilt), 0.0, std::sin(Tilt)) * L.Muzzle;
		for (int i = 0; i < 200000 && Position.X < 100.0; ++i)
		{
			geohunt::Advance(L, Position, Velocity, 0.0005);
		}
		D["energy_100"] = geohunt::EnergyFtLb(L.Grains, Velocity.Length());
		D["drop_100"] = -Position.Z;
		Out.push_back(D);
	}
	return Out;
}

PackedByteArray GeodriveHunt::sound(const String& Kind, int Rate)
{
	const std::string K = Kind.utf8().get_data();
	const double Seconds = K == "rifle" ? 3.0 : K == "smg" ? 0.8 : K == "bow" ? 0.6 : K == "bolt" ? 0.9 : K == "reload" ? 1.6 : 0.4;
	const int N = std::max(1, int(Seconds * Rate));
	std::vector<double> Out(size_t(N), 0.0);
	std::mt19937 Noise(1234);
	std::uniform_real_distribution<double> White(-1.0, 1.0);
	const double Dt = 1.0 / Rate;
	// (one-pole filters: a low pass at Hz)
	auto Alpha = [Dt](double Hz) { return 1.0 - std::exp(-2.0 * 3.14159265358979 * Hz * Dt); };
	// a click: a short burst of bright noise and a ring (metal on metal) at At s
	auto Click = [&](double At, double Loud, double Ring)
	{
		double Low = 0.0;
		for (int i = int(At * Rate); i < std::min(N, int((At + 0.06) * Rate)); ++i)
		{
			const double T = i * Dt - At;
			const double W = White(Noise);
			Low += (W - Low) * Alpha(2500.0);
			Out[size_t(i)] += Loud * ((W - Low) * std::exp(-T / 0.004) + 0.4 * std::sin(2.0 * 3.14159265358979 * Ring * T) * std::exp(-T / 0.02));
		}
	};
	if (K == "rifle")
	{
		// the blast: a sharp crack (bright noise, ~4 ms), the boom under it (low, ~60 ms), the air's thump (~80 Hz)
		double Low = 0.0, Lower = 0.0;
		for (int i = 0; i < std::min(N, int(0.5 * Rate)); ++i)
		{
			const double T = i * Dt;
			const double W = White(Noise);
			Low += (W - Low) * Alpha(1800.0);
			Lower += (W - Lower) * Alpha(250.0);
			Out[size_t(i)] += 1.0 * W * std::exp(-T / 0.004) + 2.2 * Low * std::exp(-T / 0.03) + 3.0 * Lower * std::exp(-T / 0.09)
				+ 0.6 * std::sin(2.0 * 3.14159265358979 * 80.0 * T) * std::exp(-T / 0.05);
		}
		// the echoes: off hills a few hundred metres to a kilometre or two off (back after 0.4-3 s), dull (the air takes
		// the highs), smeared into a roll, fading
		double Echo = 0.0, Echo2 = 0.0;
		for (int i = int(0.35 * Rate); i < N; ++i)
		{
			const double T = i * Dt;
			const double W = White(Noise);
			Echo += (W - Echo) * Alpha(500.0);
			Echo2 += (Echo - Echo2) * Alpha(300.0);
			const double Rise = std::min(1.0, (T - 0.35) / 0.15);
			const double Roll = 1.0 + 0.6 * std::sin(T * 9.0) * std::sin(T * 3.7);
			Out[size_t(i)] += 1.4 * Echo2 * Rise * Roll * std::exp(-(T - 0.35) / 0.7);
		}
		// (a distinct first echo off the nearest hill, ~0.6 s)
		double Back = 0.0;
		for (int i = int(0.6 * Rate); i < std::min(N, int(0.9 * Rate)); ++i)
		{
			const double T = i * Dt - 0.6;
			const double W = White(Noise);
			Back += (W - Back) * Alpha(400.0);
			Out[size_t(i)] += 1.5 * Back * std::exp(-T / 0.06);
		}
	}
	else if (K == "smg")
	{
		// a pistol calibre's report: a sharp crack, less boom under it (a little powder, a short barrel), a short slap
		// back off the buildings and quickly gone (a burst's rounds each their own)
		double Low = 0.0, Lower = 0.0, Back = 0.0;
		for (int i = 0; i < N; ++i)
		{
			const double T = i * Dt;
			const double W = White(Noise);
			Low += (W - Low) * Alpha(2600.0);
			Lower += (W - Lower) * Alpha(400.0);
			Back += (W - Back) * Alpha(700.0);
			Out[size_t(i)] += 1.1 * W * std::exp(-T / 0.003) + 1.6 * Low * std::exp(-T / 0.02) + 1.2 * Lower * std::exp(-T / 0.05)
				+ (T > 0.08 ? 0.5 * Back * std::exp(-(T - 0.08) / 0.15) : 0.0);
		}
	}
	else if (K == "bow")
	{
		// the string: a low twang (~90 Hz and its harmonics, damped fast), a thump of the limbs, the arrow's hiss
		double Low = 0.0;
		for (int i = 0; i < N; ++i)
		{
			const double T = i * Dt;
			const double W = White(Noise);
			Low += (W - Low) * Alpha(3000.0);
			double Tone = 0.0;
			for (int h = 1; h <= 4; ++h)
			{
				Tone += std::sin(2.0 * 3.14159265358979 * 92.0 * h * T * (1.0 + 0.02 * std::exp(-T / 0.02))) / h;
			}
			Out[size_t(i)] += 0.5 * Tone * std::exp(-T / 0.06) + 0.8 * W * std::exp(-T / 0.006) + 0.15 * Low * std::exp(-T / 0.15);
		}
	}
	else if (K == "hit" || K == "arrow_hit")
	{
		// a bullet into an animal, heard from afar: a dull "whop" (low, ~30 ms); an arrow's a softer thud
		const bool bArrow = K == "arrow_hit";
		double Low = 0.0;
		for (int i = 0; i < N; ++i)
		{
			const double T = i * Dt;
			const double W = White(Noise);
			Low += (W - Low) * Alpha(bArrow ? 400.0 : 600.0);
			Out[size_t(i)] += 2.5 * Low * std::exp(-T / (bArrow ? 0.02 : 0.035)) + 0.7 * std::sin(2.0 * 3.14159265358979 * (bArrow ? 140.0 : 110.0) * T) * std::exp(-T / 0.04);
		}
	}
	else if (K == "bolt")
	{
		// up and back (the case out), forward and down
		Click(0.05, 0.8, 1900.0);
		Click(0.22, 1.0, 2300.0);
		Click(0.55, 0.9, 2100.0);
		Click(0.68, 1.0, 1700.0);
	}
	else if (K == "reload")
	{
		// rounds pressed into the magazine one by one, then the bolt closed
		for (int r = 0; r < 4; ++r)
		{
			Click(0.1 + r * 0.28, 0.6, 2600.0 + r * 80.0);
		}
		Click(1.3, 0.9, 2100.0);
		Click(1.42, 1.0, 1700.0);
	}
	// to 16 bits, the loudest at ~90%
	double Peak = 1e-9;
	for (const double V : Out)
	{
		Peak = std::max(Peak, std::abs(V));
	}
	PackedByteArray Bytes;
	Bytes.resize(int64_t(N) * 2);
	uint8_t* B = Bytes.ptrw();
	for (int i = 0; i < N; ++i)
	{
		const int16_t V = int16_t(std::clamp(Out[size_t(i)] / Peak * 0.9, -1.0, 1.0) * 32767.0);
		B[i * 2] = uint8_t(V & 0xff);
		B[i * 2 + 1] = uint8_t((V >> 8) & 0xff);
	}
	return Bytes;
}

void GeodriveHunt::heard(const Vector3& At)
{
	if (Herds && !bFollower)
	{
		Herds->Heard(geohunt::Vec2(At.x - Centre.x, -(At.z - Centre.z)));
	}
}

PackedByteArray GeodriveHunt::pack() const
{
	PackedByteArray Out;
	if (!Herds)
	{
		return Out;
	}
	const std::vector<uint8_t> Bytes = Herds->Pack();
	Out.resize(int64_t(Bytes.size()));
	std::copy(Bytes.begin(), Bytes.end(), Out.ptrw());
	return Out;
}

bool GeodriveHunt::apply(const PackedByteArray& Bytes)
{
	if (!Herds)
	{
		return false;
	}
	const std::vector<uint8_t> In(Bytes.ptr(), Bytes.ptr() + Bytes.size());
	return Herds->Unpack(In);
}

void GeodriveHunt::set_follower(bool bOn) { bFollower = bOn; }

void GeodriveHunt::apply_shot(int Index, const Vector3& At, bool bDown, double DiesAfter, bool bWounded)
{
	if (!Herds || bFollower)
	{
		return;
	}
	geohunt::Effect E;
	E.bDown = bDown;
	E.DiesAfter = DiesAfter;
	E.bWounded = bWounded;
	Herds->Shot(Index, geohunt::Vec2(At.x - Centre.x, -(At.z - Centre.z)), &E);
}
