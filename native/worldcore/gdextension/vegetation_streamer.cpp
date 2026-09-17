#include "vegetation_streamer.hpp"

#include <cstring>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/performance.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

namespace {

constexpr int kMaxVariants = 8;

size_t proto_index(int species, int detail, int variant) {
    return static_cast<size_t>(species) * 3 * kMaxVariants + static_cast<size_t>(detail) * kMaxVariants +
           static_cast<size_t>(variant);
}

double now_s() { return Time::get_singleton()->get_ticks_usec() / 1e6; }

}  // namespace

/// Worker output: engine-ready instance buffers for one cell.
struct VegetationStreamer::Payload {
    struct Item {
        int species, detail, variant;
        int64_t count;
        PackedFloat32Array buffer;
        AABB aabb;
    };
    std::vector<Item> items;
};

VegetationStreamer::VegetationStreamer() {
    config_.viewCells = 4;  // the reference's scatter view; trees beyond it are fog anyway
}

VegetationStreamer::~VegetationStreamer() {
    scheduler_.reset();
    release_all();
    if (RenderingServer* rs = RenderingServer::get_singleton()) {
        for (const RID& m : prototypeMeshes_) {
            if (m.is_valid()) rs->free_rid(m);
        }
    }
}

bool VegetationStreamer::load_prototypes() {
    if (prototypesLoaded_) return true;
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(prototypesPath_);
    std::vector<uint8_t> data(static_cast<size_t>(bytes.size()));
    if (!data.empty()) std::memcpy(data.data(), bytes.ptr(), data.size());
    std::vector<worldcore::FloraPrototype> set;
    std::string error;
    if (!worldcore::deserialize_flora(data, set, &error)) {
        UtilityFunctions::push_error("VegetationStreamer: cannot load ", prototypesPath_, ": ", String(error.c_str()),
                                     " (run the worldcore_bake build step)");
        return false;
    }

    RenderingServer* rs = RenderingServer::get_singleton();
    prototypeMeshes_.assign(static_cast<size_t>(worldcore::SPECIES_COUNT) * 3 * kMaxVariants, RID());
    for (const auto& p : set) {
        if (p.variant >= kMaxVariants) continue;
        const int64_t n = static_cast<int64_t>(p.mesh.positions.size() / 3);
        PackedVector3Array pos, nrm;
        PackedColorArray col;
        pos.resize(n);
        nrm.resize(n);
        col.resize(n);
        for (int64_t v = 0; v < n; v++) {
            // Reference fronts are counter-clockwise, Godot's clockwise: swap the
            // second and third vertex of every triangle.
            const int64_t k = v % 3;
            const int64_t src = k == 1 ? v + 1 : k == 2 ? v - 1 : v;
            const size_t s = static_cast<size_t>(src) * 3;
            pos.set(v, Vector3(p.mesh.positions[s], p.mesh.positions[s + 1], p.mesh.positions[s + 2]));
            nrm.set(v, Vector3(p.mesh.normals[s], p.mesh.normals[s + 1], p.mesh.normals[s + 2]));
            col.set(v, Color(p.mesh.colors[s], p.mesh.colors[s + 1], p.mesh.colors[s + 2]));
        }
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = pos;
        arrays[Mesh::ARRAY_NORMAL] = nrm;
        arrays[Mesh::ARRAY_COLOR] = col;
        const RID mesh = rs->mesh_create();
        rs->mesh_add_surface_from_arrays(mesh, RenderingServer::PRIMITIVE_TRIANGLES, arrays);
        const Ref<Material>& mat = p.doubleSided ? leafMaterial_ : solidMaterial_;
        if (mat.is_valid()) rs->mesh_surface_set_material(mesh, 0, mat->get_rid());
        prototypeMeshes_[proto_index(p.species, p.detail, p.variant)] = mesh;
    }
    extents_ = std::make_shared<worldcore::FloraExtents>(worldcore::flora_extents(set));
    prototypesLoaded_ = true;
    return true;
}

void VegetationStreamer::ensure_scheduler() {
    if (scheduler_ || terrain_.is_null() || !load_prototypes()) return;
    const std::shared_ptr<const worldcore::FloraExtents> extents = extents_;
    const int32_t seed = terrain_->get_seed();
    const double density = density_;
    auto builder = [extents, seed, density](const worldcore::Field& field, const worldcore::CellKey& key) {
        const worldcore::VegetationCell cell =
            worldcore::build_vegetation_cell(field, seed, key.cx, key.cz, key.lod, *extents, density);
        auto payload = std::make_shared<Payload>();
        size_t bytes = 0;
        for (const auto& b : cell.batches) {
            Payload::Item item{b.species, b.detail, b.variant, static_cast<int64_t>(b.count), {}, {}};
            item.buffer.resize(static_cast<int64_t>(b.buffer.size()));
            std::memcpy(item.buffer.ptrw(), b.buffer.data(), b.buffer.size() * sizeof(float));
            item.aabb = AABB(Vector3(b.aabbMin.x, b.aabbMin.y, b.aabbMin.z),
                             Vector3(b.aabbMax.x - b.aabbMin.x, b.aabbMax.y - b.aabbMin.y, b.aabbMax.z - b.aabbMin.z));
            bytes += b.buffer.size() * sizeof(float);
            payload->items.push_back(std::move(item));
        }
        return worldcore::BuiltCell{payload, bytes};
    };
    scheduler_ = std::make_unique<worldcore::StreamScheduler>(terrain_->core(), config_, builder);
    if (is_inside_tree()) RenderingServer::get_singleton()->viewport_set_measure_render_time(get_viewport()->get_viewport_rid(), true);
}

worldcore::StreamView VegetationStreamer::make_view() const {
    worldcore::StreamView v;
    const Transform3D t = camera_->get_global_transform();
    v.eye = {t.origin.x, t.origin.y, t.origin.z};
    const Vector3 f = -t.basis.get_column(2);
    v.forward = {f.x, f.y, f.z};
    v.velocity = {velocity_.x, velocity_.y, velocity_.z};
    const TypedArray<Plane> planes = camera_->get_frustum();
    for (int64_t i = 0; i < planes.size(); i++) {
        const Plane p = planes[i];
        v.planes.push_back({{p.normal.x, p.normal.y, p.normal.z}, p.d});
    }
    return v;
}

void VegetationStreamer::attach(const worldcore::ReadyCell& cell) {
    RenderingServer* rs = RenderingServer::get_singleton();
    const auto& payload = *std::static_pointer_cast<Payload>(cell.payload);
    std::vector<Batch> batches;
    for (const auto& item : payload.items) {
        const RID proto = prototypeMeshes_[proto_index(item.species, item.detail, item.variant)];
        if (!proto.is_valid() || item.count == 0) continue;
        const RID mm = rs->multimesh_create();
        rs->multimesh_allocate_data(mm, static_cast<int32_t>(item.count), RenderingServer::MULTIMESH_TRANSFORM_3D, true);
        rs->multimesh_set_mesh(mm, proto);
        rs->multimesh_set_buffer(mm, item.buffer);
        rs->multimesh_set_custom_aabb(mm, item.aabb);
        const RID inst = rs->instance_create2(mm, get_world_3d()->get_scenario());
        rs->instance_geometry_set_cast_shadows_setting(inst, RenderingServer::SHADOW_CASTING_SETTING_OFF);
        batches.push_back({mm, inst});
        liveInstances_ += static_cast<size_t>(item.count);
    }
    liveBatches_ += batches.size();
    cells_[cell.key] = std::move(batches);
}

void VegetationStreamer::release(const worldcore::CellKey& key) {
    auto it = cells_.find(key);
    if (it == cells_.end()) return;
    RenderingServer* rs = RenderingServer::get_singleton();
    for (const Batch& b : it->second) {
        liveInstances_ -= static_cast<size_t>(rs->multimesh_get_instance_count(b.multimesh));
        rs->free_rid(b.instance);
        rs->free_rid(b.multimesh);
    }
    liveBatches_ -= it->second.size();
    cells_.erase(it);
}

void VegetationStreamer::release_all() {
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return;
    for (auto& [key, batches] : cells_) {
        for (const Batch& b : batches) {
            rs->free_rid(b.instance);
            rs->free_rid(b.multimesh);
        }
    }
    cells_.clear();
    liveBatches_ = liveInstances_ = 0;
}

double VegetationStreamer::frame_cost_ms() const {
    // Measured work only; see TerrainStreamer::frame_cost_ms for why not TIME_PROCESS.
    RenderingServer* rs = RenderingServer::get_singleton();
    const RID vp = get_viewport()->get_viewport_rid();
    return Performance::get_singleton()->get_monitor(Performance::TIME_PHYSICS_PROCESS) * 1000.0 +
           rs->viewport_get_measured_render_time_cpu(vp) + rs->viewport_get_measured_render_time_gpu(vp) +
           rs->get_frame_setup_time_cpu() + lastAttachMs_;
}

int VegetationStreamer::prime() {
    ensure_scheduler();
    ERR_FAIL_COND_V_MSG(!scheduler_ || !camera_, -1, "VegetationStreamer.prime needs terrain, camera and baked prototypes");
    const auto cells = scheduler_->prime(make_view(), now_s());
    for (const auto& c : cells) attach(c);
    return static_cast<int>(cells.size());
}

void VegetationStreamer::_notification(int what) {
    switch (what) {
        case NOTIFICATION_READY:
            set_process(true);
            break;
        case NOTIFICATION_EXIT_TREE:
            scheduler_.reset();
            release_all();
            break;
        case NOTIFICATION_PROCESS: {
            if (Engine::get_singleton()->is_editor_hint() || !camera_) return;
            ensure_scheduler();
            if (!scheduler_) return;
            const double t = now_s();
            scheduler_->update(make_view(), t);
            const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
            for (const auto& cell : scheduler_->take_ready(budget_, frame_cost_ms(), t)) attach(cell);
            for (const auto& gone : scheduler_->take_retirements(budget_.maxRetire, t)) release(gone.key);
            lastAttachMs_ = (Time::get_singleton()->get_ticks_usec() - t0) / 1000.0;
            worstAttachMs_ = std::max(worstAttachMs_, lastAttachMs_);
            break;
        }
        default:
            break;
    }
}

Dictionary VegetationStreamer::stats() const {
    Dictionary d;
    if (!scheduler_) return d;
    const auto s = scheduler_->stats();
    d["live_cells"] = static_cast<int64_t>(s.live);
    d["live_batches"] = static_cast<int64_t>(liveBatches_);
    d["live_instances"] = static_cast<int64_t>(liveInstances_);
    d["missing_visible"] = static_cast<int64_t>(s.missingVisible);
    d["queued"] = static_cast<int64_t>(s.queued);
    d["jobs_built"] = static_cast<int64_t>(s.jobsBuilt);
    d["stale"] = static_cast<int64_t>(s.cancelledQueued + s.droppedStale);
    d["retired"] = static_cast<int64_t>(s.retired);
    d["deferred_over_budget"] = static_cast<int64_t>(s.deferredOverBudget);
    d["attach_ms"] = lastAttachMs_;
    d["worst_attach_ms"] = worstAttachMs_;
    return d;
}

void VegetationStreamer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_terrain", "terrain"), &VegetationStreamer::set_terrain);
    ClassDB::bind_method(D_METHOD("get_terrain"), &VegetationStreamer::get_terrain);
    ClassDB::bind_method(D_METHOD("set_solid_material", "material"), &VegetationStreamer::set_solid_material);
    ClassDB::bind_method(D_METHOD("get_solid_material"), &VegetationStreamer::get_solid_material);
    ClassDB::bind_method(D_METHOD("set_leaf_material", "material"), &VegetationStreamer::set_leaf_material);
    ClassDB::bind_method(D_METHOD("get_leaf_material"), &VegetationStreamer::get_leaf_material);
    ClassDB::bind_method(D_METHOD("set_camera", "camera"), &VegetationStreamer::set_camera);
    ClassDB::bind_method(D_METHOD("set_prefetch_velocity", "velocity"), &VegetationStreamer::set_prefetch_velocity);
    ClassDB::bind_method(D_METHOD("prime"), &VegetationStreamer::prime);
    ClassDB::bind_method(D_METHOD("stats"), &VegetationStreamer::stats);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "terrain", PROPERTY_HINT_RESOURCE_TYPE, "TerrainField"), "set_terrain", "get_terrain");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "solid_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_solid_material", "get_solid_material");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "leaf_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_leaf_material", "get_leaf_material");

#define PROP(type, name) \
    ClassDB::bind_method(D_METHOD("set_" #name, "value"), &VegetationStreamer::set_##name); \
    ClassDB::bind_method(D_METHOD("get_" #name), &VegetationStreamer::get_##name); \
    ADD_PROPERTY(PropertyInfo(Variant::type, #name), "set_" #name, "get_" #name);
    PROP(STRING, prototypes_path)
    PROP(FLOAT, density)
    PROP(INT, view_cells)
    PROP(INT, workers)
    PROP(INT, max_attach)
    PROP(INT, max_upload_kb)
#undef PROP
}

}  // namespace godot
