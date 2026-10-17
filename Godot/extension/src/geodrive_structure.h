// Geodrive Godot extension - AGPL-3.0 (see LICENSE)
//
// A building in Godot: Core's structure (GeoBuild/Build.h: pieces on a grid that stand while something holds them up)
// drawn and made solid. Its node is the structure's anchor, turned to its heading; points are given in its own frame
// (to_local), Godot's axes (x right, y up, z back), and turned into Core's (x along its rows, y along its columns, z
// up) here. The builder (builder.gd) asks it where a piece would snap, whether it can go, and puts it there.

#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <memory>

namespace geobuild
{
	class Structure;
}

namespace godot
{
	class MeshInstance3D;
	class StaticBody3D;

	class GeodriveStructure : public Node3D
	{
		GDCLASS(GeodriveStructure, Node3D)

	public:
		GeodriveStructure();
		~GeodriveStructure();

		/** Where a kind of piece would go, aimed at a point looking a way (both its own frame): {x, y, level, side, turn} */
		Dictionary snap(int Kind, const Vector3& Point, const Vector3& Facing) const;
		/** Why it can't go there ("" it can); a foundation's ground: the lowest and highest under its square (its own
		 *  frame's heights, Godot's y) */
		String can_place(int Kind, const Dictionary& Slot, double GroundLow, double GroundHigh) const;
		String place(int Kind, int Material, const Dictionary& Slot, double GroundLow, double GroundHigh);
		/** How firmly it'd be held there (0-1) */
		double support_if_placed(int Kind, const Dictionary& Slot) const;
		/** The piece nearest a point (-1 none), and taking it out (how many came down with it) */
		int piece_at(const Vector3& Point) const;
		int remove_piece(int Index);
		/** A door opened or shut (whether it's open now; false if it isn't a door) */
		bool toggle_door(int Index);
		/** Which kind a piece is (-1 none) */
		int get_piece_kind(int Index) const;
		int get_piece_count() const;
		/** Its tonnes (its pieces' materials) */
		double get_mass() const;

		/** Its pieces, to save and load: [{kind, material, x, y, level, side, turn}] */
		Array get_pieces() const;
		void set_pieces(const Array& Pieces);

		/** One piece on its own, in a structure's frame (the builder's preview) */
		static Ref<ArrayMesh> make_piece_mesh(int Kind, int Material, const Dictionary& Slot);
		static String kind_name(int Kind);
		static String material_name(int Material);
		static int kind_count();
		static int material_count();
		static double cell();
		static double storey();

	protected:
		static void _bind_methods();

	private:
		void Rebuild();	// (its look and its solids, again)

		std::unique_ptr<geobuild::Structure> Core;
		MeshInstance3D* Look = nullptr;
		MeshInstance3D* Glass = nullptr;	// (its panes on their own: see-through, casting no shadow)
		StaticBody3D* Solid = nullptr;
	};
}
