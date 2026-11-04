// register_types.cpp - GDExtension entry point for libworldcore (.so/.dll/.dylib).
#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "ground_patch.hpp"
#include "offroad_vehicle.hpp"
#include "terrain_field.hpp"
#include "terrain_streamer.hpp"
#include "vegetation_streamer.hpp"
#include "vehicle_mesh_library.hpp"

using namespace godot;

namespace {

void initialize_worldcore(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(TerrainField);
    GDREGISTER_CLASS(OffroadVehicle);
    GDREGISTER_CLASS(GroundPatch);
    GDREGISTER_CLASS(TerrainStreamer);
    GDREGISTER_CLASS(VegetationStreamer);
    GDREGISTER_CLASS(VehicleMeshLibrary);
}

void uninitialize_worldcore(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
}

}  // namespace

extern "C" {
GDExtensionBool GDE_EXPORT worldcore_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                                  const GDExtensionClassLibraryPtr p_library,
                                                  GDExtensionInitialization* r_initialization) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_worldcore);
    init_obj.register_terminator(uninitialize_worldcore);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
}
