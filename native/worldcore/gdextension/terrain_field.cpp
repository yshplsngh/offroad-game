#include "terrain_field.hpp"

#include <cstring>

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "worldcore/chunk_mesh.hpp"
#include "worldcore/world.hpp"

namespace godot {

TerrainField::TerrainField() : field_(std::make_shared<worldcore::Field>(1337)) {}

void TerrainField::set_seed(int seed) {
    if (seed != field_->seed()) field_ = std::make_shared<worldcore::Field>(seed);
}

int TerrainField::get_seed() const { return field_->seed(); }

double TerrainField::height(double x, double z) const { return field_->height(x, z); }

Vector3 TerrainField::normal(double x, double z) const {
    const auto n = field_->normal(x, z);
    return Vector3(n[0], n[1], n[2]);
}

Dictionary TerrainField::sample(double x, double z) const {
    const worldcore::GroundSample s = field_->sample(x, z);
    Dictionary d;
    d["height"] = s.height;
    d["normal"] = Vector3(s.nx, s.ny, s.nz);
    d["surface"] = s.surfaceId;
    d["wetness"] = s.wetness;
    d["biome"] = s.biome;
    return d;
}

Vector3 TerrainField::find_spawn(double x, double z) const {
    const worldcore::Spawn s = field_->find_spawn(x, z);
    return Vector3(s.x, s.y, s.z);
}

double TerrainField::chunk_size() const { return worldcore::kChunk; }

Ref<ArrayMesh> TerrainField::build_chunk(int cx, int cz, int lod) const {
    const worldcore::ChunkMesh m = worldcore::build_chunk_mesh(*field_, cx, cz, lod);
    const int64_t n = static_cast<int64_t>(m.vertex_count());

    PackedVector3Array pos, nrm;
    PackedColorArray col;
    PackedFloat32Array mask;
    PackedInt32Array idx;
    pos.resize(n);
    nrm.resize(n);
    col.resize(n);
    mask.resize(n * 3);
    idx.resize(static_cast<int64_t>(m.indices.size()));
    for (int64_t v = 0; v < n; v++) {
        const size_t p = static_cast<size_t>(v) * 3;
        pos.set(v, Vector3(m.positions[p], m.positions[p + 1], m.positions[p + 2]));
        nrm.set(v, Vector3(m.normals[p], m.normals[p + 1], m.normals[p + 2]));
        col.set(v, Color(m.colors[p], m.colors[p + 1], m.colors[p + 2]));
    }
    std::memcpy(mask.ptrw(), m.mask.data(), m.mask.size() * sizeof(float));
    std::memcpy(idx.ptrw(), m.indices.data(), m.indices.size() * sizeof(int32_t));

    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = pos;
    arrays[Mesh::ARRAY_NORMAL] = nrm;
    arrays[Mesh::ARRAY_COLOR] = col;
    arrays[Mesh::ARRAY_CUSTOM0] = mask;  // rock, wet, snow -> CUSTOM0.rgb in the shader
    arrays[Mesh::ARRAY_INDEX] = idx;

    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    const int64_t flags = static_cast<int64_t>(Mesh::ARRAY_CUSTOM_RGB_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), flags);
    // Exact cell AABB: frustum culling works at cell granularity and the engine
    // never walks the vertices to compute it.
    mesh->set_custom_aabb(AABB(Vector3(0, m.min_y, 0),
                               Vector3(worldcore::kChunk, m.max_y - m.min_y, worldcore::kChunk)));
    return mesh;
}

void TerrainField::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_seed", "seed"), &TerrainField::set_seed);
    ClassDB::bind_method(D_METHOD("get_seed"), &TerrainField::get_seed);
    ClassDB::bind_method(D_METHOD("height", "x", "z"), &TerrainField::height);
    ClassDB::bind_method(D_METHOD("normal", "x", "z"), &TerrainField::normal);
    ClassDB::bind_method(D_METHOD("sample", "x", "z"), &TerrainField::sample);
    ClassDB::bind_method(D_METHOD("find_spawn", "x", "z"), &TerrainField::find_spawn);
    ClassDB::bind_method(D_METHOD("build_chunk", "cx", "cz", "lod"), &TerrainField::build_chunk);
    ClassDB::bind_method(D_METHOD("chunk_size"), &TerrainField::chunk_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
}

}  // namespace godot
