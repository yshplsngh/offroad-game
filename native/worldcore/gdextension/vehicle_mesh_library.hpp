// vehicle_mesh_library.hpp - baked procedural vehicles (res://generated/vehicles.bin)
// turned into Godot meshes.
//
// Each part becomes an ArrayMesh whose surfaces are split by material layer;
// the mesh's "layers" meta lists the layer of each surface (0 opaque, 1 paint,
// 2 glass, 3 lamp lens) so the script assigns one material per layer. Vertex
// COLOR carries the linear albedo, CUSTOM0 = (roughness, metallic, clearcoat,
// emission): lamp kind on lenses, opacity on glass.
#pragma once

#include <vector>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "worldcore/vehicle_mesh.hpp"

namespace godot {

class VehicleMeshLibrary : public RefCounted {
    GDCLASS(VehicleMeshLibrary, RefCounted)

public:
    /// Load the bake. Returns false (and pushes an error) if it is missing or corrupt.
    bool load(const String& path);
    PackedStringArray get_ids() const;
    /// Rig description: wheel_radius, track, axle_z (Array), axle_y, travel,
    /// steering_pos, steering_tilt, links (Array of {axle, part, pivot, target}),
    /// shocks (Array of {axle, mount, anchor}).
    Dictionary get_rig(const String& id) const;
    /// Parts for one LOD (0 far .. 2 near): chassis, wheel_left, wheel_right,
    /// brake_left, brake_right, axle_front, axle_rear, link_lower, link_upper,
    /// link_panhard, shock_body, shock_shaft, spring, steering_wheel.
    Dictionary build_parts(const String& id, int lod) const;

protected:
    static void _bind_methods();

private:
    const worldcore::VehicleMeshes* find(const String& id) const;
    std::vector<worldcore::VehicleMeshes> vehicles_;
};

}  // namespace godot
