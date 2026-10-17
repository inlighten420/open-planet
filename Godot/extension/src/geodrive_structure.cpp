// Geodrive Godot extension - GPL (see Core/README.md)

#include "geodrive_structure.h"

#include "GeoBuild/Build.h"

#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>

using namespace godot;

namespace
{
	// Core's frame (x, y along the grid, z up) and Godot's (x right, y up, z back)
	geobody::Vec3 ToCore(const Vector3& V) { return geobody::Vec3(V.x, -V.z, V.y); }
	Vector3 ToGodot(const geobody::Vec3& V) { return Vector3(float(V.X), float(V.Z), float(-V.Y)); }

	geobuild::Slot SlotOf(const Dictionary& D)
	{
		geobuild::Slot S;
		S.X = int32_t(D.get("x", 0));
		S.Y = int32_t(D.get("y", 0));
		S.Level = int32_t(D.get("level", 0));
		S.Side = int32_t(D.get("side", 0));
		S.Turn = int32_t(D.get("turn", 0));
		S.Part = int32_t(D.get("part", 0));
		return S;
	}

	/** A plain wall's size (geobuild::SizeFlag), given with its slot as "size" */
	uint8_t SizeOf(const Dictionary& D) { return uint8_t(int32_t(D.get("size", 0)) & 7); }

	Dictionary SlotDict(const geobuild::Slot& S)
	{
		Dictionary D;
		D["x"] = S.X;
		D["y"] = S.Y;
		D["level"] = S.Level;
		D["side"] = S.Side;
		D["turn"] = S.Turn;
		D["part"] = S.Part;
		return D;
	}

	geobuild::Kind KindOf(int K) { return geobuild::Kind(std::clamp(K, 0, int(geobuild::Kind::Count) - 1)); }
	geobuild::Material MaterialOf(int M) { return geobuild::Material(std::clamp(M, 0, int(geobuild::Material::Count) - 1)); }

	enum class Part { All, Solid, Glass };	// (which of its triangles: the panes, or all but them)

	/** Core's triangles (counter-clockwise from outside) as a Godot mesh (its front faces clockwise: the corners turned) */
	Ref<ArrayMesh> ToMesh(const geobuild::Mesh& M, Part Which = Part::All)
	{
		Ref<ArrayMesh> Out;
		Out.instantiate();
		PackedVector3Array Positions, Normals;
		PackedColorArray Colours;
		PackedInt32Array Indices;
		for (size_t i = 0; i < M.Positions.size(); ++i)
		{
			Positions.push_back(ToGodot(M.Positions[i]));
			Normals.push_back(ToGodot(M.Normals[i]));
			// (the finish in alpha, for structure.gdshader: Finish / 8)
			Colours.push_back(Color(M.Colours[i * 4], M.Colours[i * 4 + 1], M.Colours[i * 4 + 2], M.Colours[i * 4 + 3] / 8.0f));
		}
		const float GlassFinish = float(geobuild::Finish::Glass);
		for (size_t t = 0; t + 2 < M.Indices.size(); t += 3)
		{
			const bool bGlass = M.Colours[size_t(M.Indices[t]) * 4 + 3] == GlassFinish;
			if ((Which == Part::Solid && bGlass) || (Which == Part::Glass && !bGlass))
			{
				continue;
			}
			Indices.push_back(M.Indices[t]);
			Indices.push_back(M.Indices[t + 2]);
			Indices.push_back(M.Indices[t + 1]);
		}
		if (Indices.is_empty())
		{
			return Out;
		}
		Array Arrays;
		Arrays.resize(Mesh::ARRAY_MAX);
		Arrays[Mesh::ARRAY_VERTEX] = Positions;
		Arrays[Mesh::ARRAY_NORMAL] = Normals;
		Arrays[Mesh::ARRAY_COLOR] = Colours;
		Arrays[Mesh::ARRAY_INDEX] = Indices;
		Out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Arrays);
		return Out;
	}
}

void GeodriveStructure::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("snap", "kind", "point", "facing"), &GeodriveStructure::snap);
	ClassDB::bind_method(D_METHOD("can_place", "kind", "slot", "ground_low", "ground_high"), &GeodriveStructure::can_place, DEFVAL(0.0), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("place", "kind", "material", "slot", "ground_low", "ground_high"), &GeodriveStructure::place, DEFVAL(0.0), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("support_if_placed", "kind", "slot"), &GeodriveStructure::support_if_placed);
	ClassDB::bind_method(D_METHOD("piece_at", "point"), &GeodriveStructure::piece_at);
	ClassDB::bind_method(D_METHOD("remove_piece", "index"), &GeodriveStructure::remove_piece);
	ClassDB::bind_method(D_METHOD("toggle_door", "index"), &GeodriveStructure::toggle_door);
	ClassDB::bind_method(D_METHOD("get_piece_kind", "index"), &GeodriveStructure::get_piece_kind);
	ClassDB::bind_method(D_METHOD("get_piece_count"), &GeodriveStructure::get_piece_count);
	ClassDB::bind_method(D_METHOD("get_mass"), &GeodriveStructure::get_mass);
	ClassDB::bind_method(D_METHOD("get_pieces"), &GeodriveStructure::get_pieces);
	ClassDB::bind_method(D_METHOD("set_pieces", "pieces"), &GeodriveStructure::set_pieces);
	ClassDB::bind_static_method("GeodriveStructure", D_METHOD("make_piece_mesh", "kind", "material", "slot"), &GeodriveStructure::make_piece_mesh);
	ClassDB::bind_static_method("GeodriveStructure", D_METHOD("kind_name", "kind"), &GeodriveStructure::kind_name);
	ClassDB::bind_static_method("GeodriveStructure", D_METHOD("material_name", "material"), &GeodriveStructure::material_name);
	ClassDB::bind_static_method("GeodriveStructure", D_METHOD("kind_count"), &GeodriveStructure::kind_count);
	ClassDB::bind_static_method("GeodriveStructure", D_METHOD("material_count"), &GeodriveStructure::material_count);
	ClassDB::bind_static_method("GeodriveStructure", D_METHOD("cell"), &GeodriveStructure::cell);
	ClassDB::bind_static_method("GeodriveStructure", D_METHOD("storey"), &GeodriveStructure::storey);
}

GeodriveStructure::GeodriveStructure() : Core(std::make_unique<geobuild::Structure>())
{
	Look = memnew(MeshInstance3D);
	Look->set_name("Look");
	add_child(Look);	// (its look: builder.gd's, structure.gdshader)
	Glass = memnew(MeshInstance3D);
	Glass->set_name("Glass");
	Glass->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);	// (the sun in through the windows)
	add_child(Glass);	// (its look: builder.gd's, building_glass.gdshader)
	Solid = memnew(StaticBody3D);
	Solid->set_name("Solid");
	add_child(Solid);
}

GeodriveStructure::~GeodriveStructure() = default;

Dictionary GeodriveStructure::snap(int Kind, const Vector3& Point, const Vector3& Facing) const
{
	const geobody::Vec3 F = ToCore(Facing);
	return SlotDict(Core->Snap(KindOf(Kind), ToCore(Point), F.X, F.Y));
}

String GeodriveStructure::can_place(int Kind, const Dictionary& Slot, double GroundLow, double GroundHigh) const
{
	return String::utf8(geobuild::Why(Core->CanPlace(KindOf(Kind), SlotOf(Slot), GroundLow, GroundHigh, SizeOf(Slot))));
}

String GeodriveStructure::place(int Kind, int Material, const Dictionary& Slot, double GroundLow, double GroundHigh)
{
	const geobuild::Refusal R = Core->Place(KindOf(Kind), MaterialOf(Material), SlotOf(Slot), GroundLow, GroundHigh, SizeOf(Slot));
	if (R == geobuild::Refusal::None)
	{
		Rebuild();
	}
	return String::utf8(geobuild::Why(R));
}

double GeodriveStructure::support_if_placed(int Kind, const Dictionary& Slot) const
{
	return Core->SupportIfPlaced(KindOf(Kind), SlotOf(Slot), SizeOf(Slot));
}

int GeodriveStructure::piece_at(const Vector3& Point) const
{
	return Core->PieceAt(ToCore(Point));
}

int GeodriveStructure::remove_piece(int Index)
{
	const int Down = Core->Remove(Index);
	if (Down > 0)
	{
		Rebuild();
	}
	return Down;
}

bool GeodriveStructure::toggle_door(int Index)
{
	const bool bOpen = Core->Toggle(Index);
	Rebuild();
	return bOpen;
}

int GeodriveStructure::get_piece_kind(int Index) const
{
	return Index >= 0 && Index < int(Core->Pieces().size()) ? int(Core->Pieces()[size_t(Index)].What) : -1;
}

int GeodriveStructure::get_piece_count() const
{
	return int(Core->Pieces().size());
}

double GeodriveStructure::get_mass() const
{
	double Kg = 0.0;
	for (const geobody::Part& P : Core->Parts())
	{
		Kg += P.GetMass();
	}
	return Kg / 1000.0;
}

Array GeodriveStructure::get_pieces() const
{
	Array Out;
	for (const geobuild::Piece& P : Core->Pieces())
	{
		Dictionary D = SlotDict(P.At);
		D["kind"] = int(P.What);
		D["material"] = int(P.Of);
		if (P.bOpen)
		{
			D["open"] = true;
		}
		if (P.Size != 0)
		{
			D["size"] = int32_t(P.Size);
		}
		Out.push_back(D);
	}
	return Out;
}

void GeodriveStructure::set_pieces(const Array& Pieces)
{
	std::vector<geobuild::Piece> All;
	for (int64_t i = 0; i < Pieces.size(); ++i)
	{
		const Dictionary D = Pieces[i];
		geobuild::Piece P;
		P.What = KindOf(int(D.get("kind", 0)));
		P.Of = MaterialOf(int(D.get("material", 0)));
		P.At = SlotOf(D);
		P.bOpen = bool(D.get("open", false));
		P.Size = geobuild::Sizable(P.What) ? SizeOf(D) : 0;
		All.push_back(P);
	}
	Core->SetPieces(All);
	Rebuild();
}

Ref<ArrayMesh> GeodriveStructure::make_piece_mesh(int Kind, int Material, const Dictionary& Slot)
{
	geobuild::Piece P;
	P.What = KindOf(Kind);
	P.Of = MaterialOf(Material);
	P.At = SlotOf(Slot);
	P.Size = geobuild::Sizable(P.What) ? SizeOf(Slot) : 0;
	return ToMesh(geobuild::Structure::PieceLook(P));
}

String GeodriveStructure::kind_name(int Kind) { return String::utf8(geobuild::Name(KindOf(Kind))); }
String GeodriveStructure::material_name(int Material) { return String::utf8(geobuild::Name(MaterialOf(Material))); }
int GeodriveStructure::kind_count() { return int(geobuild::Kind::Count); }
int GeodriveStructure::material_count() { return int(geobuild::Material::Count); }
double GeodriveStructure::cell() { return geobuild::Cell; }
double GeodriveStructure::storey() { return geobuild::Storey; }

void GeodriveStructure::Rebuild()
{
	const geobuild::Mesh Mesh = Core->Look();
	Look->set_mesh(ToMesh(Mesh, Part::Solid));
	Glass->set_mesh(ToMesh(Mesh, Part::Glass));
	// its solids: a box shape for each (walls, floors, each step), on one static body
	for (int64_t i = Solid->get_child_count() - 1; i >= 0; --i)
	{
		Node* Child = Solid->get_child(i);
		Solid->remove_child(Child);
		Child->queue_free();
	}
	for (const geobuild::Box& B : Core->Solids())
	{
		Ref<BoxShape3D> Shape;
		Shape.instantiate();
		Shape->set_size(Vector3(float(B.Half.X * 2.0), float(B.Half.Z * 2.0), float(B.Half.Y * 2.0)));
		CollisionShape3D* Node = memnew(CollisionShape3D);
		Node->set_shape(Shape);
		// its turn in Godot's frame: Core's turn (about z, the yaw; its x end raised) is about Godot's y; the box's own
		// axes carried across as its basis' columns
		const geobody::Quat Q = B.Turn();
		const Vector3 AxisX = ToGodot(Q.Rotate(geobody::Vec3(1.0, 0.0, 0.0)));
		const Vector3 AxisY = ToGodot(Q.Rotate(geobody::Vec3(0.0, 0.0, 1.0)));	// (Godot's box y: Core's z)
		const Vector3 AxisZ = ToGodot(Q.Rotate(geobody::Vec3(0.0, -1.0, 0.0)));	// (Godot's box z: Core's -y)
		Node->set_transform(Transform3D(Basis(AxisX, AxisY, AxisZ), ToGodot(B.Centre)));
		Solid->add_child(Node);
	}
}
