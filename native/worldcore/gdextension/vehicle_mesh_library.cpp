#include "vehicle_mesh_library.hpp"

#include <algorithm>
#include <cstring>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

namespace {

Vector3 gv(const worldcore::Vec3& v) { return Vector3(v.x, v.y, v.z); }

Ref<ArrayMesh> to_mesh(const worldcore::PaintedMesh& painted) {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    Array layers;
    const auto split = painted.by_layer();
    for (int layer = 0; layer < 4; layer++) {
        const worldcore::PaintedMesh& m = split[static_cast<size_t>(layer)];
        if (m.indices.empty()) continue;
        const int64_t n = static_cast<int64_t>(m.positions.size());
        PackedVector3Array pos, nrm;
        PackedColorArray col;
        PackedFloat32Array custom;
        PackedInt32Array idx;
        pos.resize(n);
        nrm.resize(n);
        col.resize(n);
        custom.resize(n * 4);
        for (int64_t v = 0; v < n; v++) {
            const auto& l = m.looks[static_cast<size_t>(v)];
            pos.set(v, gv(m.positions[static_cast<size_t>(v)]));
            nrm.set(v, gv(m.normals[static_cast<size_t>(v)]));
            col.set(v, Color(l.albedo[0], l.albedo[1], l.albedo[2]));
            custom.set(v * 4, l.roughness);
            custom.set(v * 4 + 1, l.metallic);
            custom.set(v * 4 + 2, l.clearcoat);
            custom.set(v * 4 + 3, l.emission);
        }
        idx.resize(static_cast<int64_t>(m.indices.size()));
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
            // Counter-clockwise (baked) to clockwise (Godot) fronts.
            idx.set(static_cast<int64_t>(t), static_cast<int32_t>(m.indices[t]));
            idx.set(static_cast<int64_t>(t + 1), static_cast<int32_t>(m.indices[t + 2]));
            idx.set(static_cast<int64_t>(t + 2), static_cast<int32_t>(m.indices[t + 1]));
        }
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = pos;
        arrays[Mesh::ARRAY_NORMAL] = nrm;
        arrays[Mesh::ARRAY_COLOR] = col;
        arrays[Mesh::ARRAY_CUSTOM0] = custom;
        arrays[Mesh::ARRAY_INDEX] = idx;
        const int64_t flags = static_cast<int64_t>(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), flags);
        layers.append(layer);
    }
    mesh->set_meta("layers", layers);
    return mesh;
}

}  // namespace

bool VehicleMeshLibrary::load(const String& path) {
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
    std::vector<uint8_t> data(static_cast<size_t>(bytes.size()));
    if (!data.empty()) std::memcpy(data.data(), bytes.ptr(), data.size());
    std::string error;
    if (!worldcore::deserialize_vehicles(data, vehicles_, &error)) {
        UtilityFunctions::push_error("VehicleMeshLibrary: cannot load ", path, ": ", String(error.c_str()),
                                     " (run the worldcore_bake build step)");
        vehicles_.clear();
        return false;
    }
    return true;
}

PackedStringArray VehicleMeshLibrary::get_ids() const {
    PackedStringArray ids;
    for (const auto& v : vehicles_) ids.append(String(v.id.c_str()));
    return ids;
}

const worldcore::VehicleMeshes* VehicleMeshLibrary::find(const String& id) const {
    const std::string key = id.utf8().get_data();
    for (const auto& v : vehicles_) {
        if (v.id == key) return &v;
    }
    return nullptr;
}

Dictionary VehicleMeshLibrary::get_rig(const String& id) const {
    Dictionary d;
    const auto* v = find(id);
    ERR_FAIL_COND_V_MSG(!v, d, "VehicleMeshLibrary: unknown vehicle " + id);
    const auto& r = v->rig;
    d["wheel_radius"] = r.wheelRadius;
    d["track"] = r.track;
    Array axleZ;
    axleZ.append(r.axleZ[0]);
    axleZ.append(r.axleZ[1]);
    d["axle_z"] = axleZ;
    d["axle_y"] = r.axleY;
    d["travel"] = r.travel;
    d["steering_pos"] = gv(r.steeringPos);
    d["steering_tilt"] = r.steeringTilt;
    Array links;
    for (const auto& l : r.links) {
        Dictionary e;
        e["axle"] = l.axle;
        e["part"] = l.part;
        e["pivot"] = gv(l.pivot);
        e["target"] = gv(l.target);
        links.append(e);
    }
    d["links"] = links;
    Array shocks;
    for (const auto& s : r.shocks) {
        Dictionary e;
        e["axle"] = s.axle;
        e["mount"] = gv(s.mount);
        e["anchor"] = gv(s.anchor);
        shocks.append(e);
    }
    d["shocks"] = shocks;
    return d;
}

Dictionary VehicleMeshLibrary::build_parts(const String& id, int lod) const {
    Dictionary d;
    const auto* v = find(id);
    ERR_FAIL_COND_V_MSG(!v, d, "VehicleMeshLibrary: unknown vehicle " + id);
    const auto& l = v->lods[static_cast<size_t>(std::clamp(lod, 0, 2))];
    d["chassis"] = to_mesh(l.chassis);
    d["wheel_left"] = to_mesh(l.wheel[0]);
    d["wheel_right"] = to_mesh(l.wheel[1]);
    d["brake_left"] = to_mesh(l.brake[0]);
    d["brake_right"] = to_mesh(l.brake[1]);
    d["axle_front"] = to_mesh(l.axle[0]);
    d["axle_rear"] = to_mesh(l.axle[1]);
    d["link_lower"] = to_mesh(l.link[0]);
    d["link_upper"] = to_mesh(l.link[1]);
    d["link_panhard"] = to_mesh(l.link[2]);
    d["shock_body"] = to_mesh(l.shockBody);
    d["shock_shaft"] = to_mesh(l.shockShaft);
    d["spring"] = to_mesh(l.spring);
    d["steering_wheel"] = to_mesh(l.steeringWheel);
    return d;
}

void VehicleMeshLibrary::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load", "path"), &VehicleMeshLibrary::load);
    ClassDB::bind_method(D_METHOD("get_ids"), &VehicleMeshLibrary::get_ids);
    ClassDB::bind_method(D_METHOD("get_rig", "id"), &VehicleMeshLibrary::get_rig);
    ClassDB::bind_method(D_METHOD("build_parts", "id", "lod"), &VehicleMeshLibrary::build_parts);
}

}  // namespace godot
