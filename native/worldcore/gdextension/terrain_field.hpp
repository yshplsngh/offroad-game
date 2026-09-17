// terrain_field.hpp - Godot face of worldcore::Field.
//
// Queries (height/sample/spawn) are answered analytically anywhere in the
// world; build_chunk() turns one cell into an ArrayMesh with an exact AABB.
#pragma once

#include <memory>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "worldcore/field.hpp"

namespace godot {

class TerrainField : public RefCounted {
    GDCLASS(TerrainField, RefCounted)

public:
    TerrainField();

    void set_seed(int seed);
    int get_seed() const;

    double height(double x, double z) const;
    Vector3 normal(double x, double z) const;
    /// {height, normal, surface, wetness, biome}
    Dictionary sample(double x, double z) const;
    /// Vector3 spawn point; y is ground height. Falls back to the query point.
    Vector3 find_spawn(double x, double z) const;
    /// One cell as a mesh positioned cell-local (place the node at cx*128, 0, cz*128).
    Ref<ArrayMesh> build_chunk(int cx, int cz, int lod) const;

    double chunk_size() const;

    /// The shared analytic field, for other native classes (the vehicle). Shared
    /// ownership: set_seed() replaces the field, and holders keep the old one alive.
    std::shared_ptr<const worldcore::Field> core() const { return field_; }

protected:
    static void _bind_methods();

private:
    std::shared_ptr<worldcore::Field> field_;
};

}  // namespace godot
