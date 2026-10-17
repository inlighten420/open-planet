// Geodrive for Godot - GPL (see Core/README.md)

#include "geodrive_car.h"
#include "geodrive_aircraft.h"
#include "geodrive_structure.h"
#include "geodrive_hunt.h"
#include "geodrive_game.h"
#include "geodrive_car_sound.h"
#include "geodrive_wheel.h"
#include "geodrive_world.h"
#include "geodrive_world_index.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static void initialize_geodrive(ModuleInitializationLevel Level)
{
	if (Level == MODULE_INITIALIZATION_LEVEL_SCENE)
	{
		GDREGISTER_CLASS(GeodriveWorld);
		GDREGISTER_CLASS(GeodriveCar);
		GDREGISTER_CLASS(GeodriveCarSound);
		GDREGISTER_CLASS(GeodriveWheel);
		GDREGISTER_CLASS(GeodriveWorldIndex);
		GDREGISTER_CLASS(GeodriveAircraft);
		GDREGISTER_CLASS(GeodriveStructure);
		GDREGISTER_CLASS(GeodriveHunt);
		GDREGISTER_CLASS(GeodriveGame);
	}
}

static void uninitialize_geodrive(ModuleInitializationLevel Level)
{
	if (Level == MODULE_INITIALIZATION_LEVEL_SCENE)
	{
		GeodriveCar::forget_looks();	// (the cars' shared meshes: freed after the renderer they'd crash the exit)
	}
}

extern "C"
{
	GDExtensionBool GDE_EXPORT geodrive_library_init(GDExtensionInterfaceGetProcAddress GetProcAddress, GDExtensionClassLibraryPtr Library,
		GDExtensionInitialization* Initialization)
	{
		GDExtensionBinding::InitObject Init(GetProcAddress, Library, Initialization);
		Init.register_initializer(initialize_geodrive);
		Init.register_terminator(uninitialize_geodrive);
		Init.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
		return Init.init();
	}
}
