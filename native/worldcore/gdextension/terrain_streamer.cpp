#include "terrain_streamer.hpp"

#include <cstring>

#include <godot_cpp/classes/engine.hpp>
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
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "worldcore/world.hpp"

namespace godot {

namespace {

/// Worker side: the mesh as the arrays mesh_add_surface_from_arrays expects.
std::shared_ptr<void> to_godot_arrays(const worldcore::ChunkMesh& m) {
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
    Vector3* pw = pos.ptrw();
    Vector3* nw = nrm.ptrw();
    Color* cw = col.ptrw();
    for (int64_t v = 0; v < n; v++) {
        const size_t p = static_cast<size_t>(v) * 3;
        pw[v] = Vector3(m.positions[p], m.positions[p + 1], m.positions[p + 2]);
        nw[v] = Vector3(m.normals[p], m.normals[p + 1], m.normals[p + 2]);
        cw[v] = Color(m.colors[p], m.colors[p + 1], m.colors[p + 2]);
    }
    std::memcpy(mask.ptrw(), m.mask.data(), m.mask.size() * sizeof(float));
    std::memcpy(idx.ptrw(), m.indices.data(), m.indices.size() * sizeof(int32_t));

    auto arrays = std::make_shared<Array>();
    arrays->resize(Mesh::ARRAY_MAX);
    (*arrays)[Mesh::ARRAY_VERTEX] = pos;
    (*arrays)[Mesh::ARRAY_NORMAL] = nrm;
    (*arrays)[Mesh::ARRAY_COLOR] = col;
    (*arrays)[Mesh::ARRAY_CUSTOM0] = mask;
    (*arrays)[Mesh::ARRAY_INDEX] = idx;
    return arrays;
}

}  // namespace

TerrainStreamer::~TerrainStreamer() {
    scheduler_.reset();  // join workers before anything they touch goes away
    release_all();
}

double TerrainStreamer::now() const { return Time::get_singleton()->get_ticks_usec() / 1e6; }

void TerrainStreamer::ensure_scheduler() {
    if (scheduler_ || terrain_.is_null()) return;
    scheduler_ = std::make_unique<worldcore::StreamScheduler>(terrain_->core(), config_, to_godot_arrays);
    if (is_inside_tree()) RenderingServer::get_singleton()->viewport_set_measure_render_time(get_viewport()->get_viewport_rid(), true);
}

worldcore::StreamView TerrainStreamer::make_view() const {
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

void TerrainStreamer::attach(const worldcore::ReadyCell& cell) {
    RenderingServer* rs = RenderingServer::get_singleton();
    const auto& arrays = *std::static_pointer_cast<Array>(cell.payload);
    const RID mesh = rs->mesh_create();
    const int64_t flags = static_cast<int64_t>(RenderingServer::ARRAY_CUSTOM_RGB_FLOAT)
        << RenderingServer::ARRAY_FORMAT_CUSTOM0_SHIFT;
    rs->mesh_add_surface_from_arrays(mesh, RenderingServer::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(),
                                     static_cast<BitField<RenderingServer::ArrayFormat>>(flags));
    rs->mesh_set_custom_aabb(mesh, AABB(Vector3(0, cell.mesh->min_y, 0),
                                        Vector3(worldcore::kChunk, cell.mesh->max_y - cell.mesh->min_y, worldcore::kChunk)));
    if (material_.is_valid()) rs->mesh_surface_set_material(mesh, 0, material_->get_rid());
    const RID inst = rs->instance_create2(mesh, get_world_3d()->get_scenario());
    rs->instance_set_transform(inst, Transform3D(Basis(), Vector3(cell.key.cx * worldcore::kChunk, 0,
                                                                  cell.key.cz * worldcore::kChunk)));
    rs->instance_geometry_set_cast_shadows_setting(inst, RenderingServer::SHADOW_CASTING_SETTING_OFF);
    instances_[cell.key] = {mesh, inst};
}

void TerrainStreamer::release(const worldcore::CellKey& key) {
    auto it = instances_.find(key);
    if (it == instances_.end()) return;
    RenderingServer* rs = RenderingServer::get_singleton();
    rs->free_rid(it->second.instance);
    rs->free_rid(it->second.mesh);
    instances_.erase(it);
}

void TerrainStreamer::release_all() {
    if (instances_.empty()) return;
    RenderingServer* rs = RenderingServer::get_singleton();
    for (auto& [k, inst] : instances_) {
        rs->free_rid(inst.instance);
        rs->free_rid(inst.mesh);
    }
    instances_.clear();
}

double TerrainStreamer::frame_cost_ms() const {
    // Measured work only. Performance::TIME_PROCESS is NOT usable here: it spans
    // the whole frame including the vsync wait (16.7-70 ms on a 60 Hz display
    // while the GPU does ~4 ms), which kept this gate permanently shut.
    // Work = physics step + render CPU + GPU + frame setup + this streamer's own
    // attach/retire time last frame. Other scripts' process time is not counted.
    RenderingServer* rs = RenderingServer::get_singleton();
    const RID vp = get_viewport()->get_viewport_rid();
    return Performance::get_singleton()->get_monitor(Performance::TIME_PHYSICS_PROCESS) * 1000.0 +
           rs->viewport_get_measured_render_time_cpu(vp) + rs->viewport_get_measured_render_time_gpu(vp) +
           rs->get_frame_setup_time_cpu() + lastAttachMs_;
}

int TerrainStreamer::prime() {
    ensure_scheduler();
    ERR_FAIL_COND_V_MSG(!scheduler_ || !camera_, 0, "TerrainStreamer.prime needs terrain and camera");
    const auto cells = scheduler_->prime(make_view(), now());
    for (const auto& c : cells) attach(c);
    return static_cast<int>(cells.size());
}

void TerrainStreamer::_notification(int what) {
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
            const double t = now();
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

Dictionary TerrainStreamer::stats() const {
    Dictionary d;
    if (!scheduler_) return d;
    const auto s = scheduler_->stats();
    d["wanted_visible"] = static_cast<int64_t>(s.wantedVisible);
    d["wanted_prefetch"] = static_cast<int64_t>(s.wantedPrefetch);
    d["queued"] = static_cast<int64_t>(s.queued);
    d["building"] = static_cast<int64_t>(s.building);
    d["ready"] = static_cast<int64_t>(s.ready);
    d["live"] = static_cast<int64_t>(s.live);
    d["missing_visible"] = static_cast<int64_t>(s.missingVisible);
    d["jobs_started"] = static_cast<int64_t>(s.jobsStarted);
    d["jobs_built"] = static_cast<int64_t>(s.jobsBuilt);
    d["cancelled"] = static_cast<int64_t>(s.cancelledQueued);
    d["dropped_stale"] = static_cast<int64_t>(s.droppedStale);
    d["attached"] = static_cast<int64_t>(s.attached);
    d["retired"] = static_cast<int64_t>(s.retired);
    d["deferred_over_budget"] = static_cast<int64_t>(s.deferredOverBudget);
    d["rolling_p95_ms"] = s.rollingP95Ms;
    d["attach_ms"] = lastAttachMs_;
    d["worst_attach_ms"] = worstAttachMs_;
    return d;
}

void TerrainStreamer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_terrain", "terrain"), &TerrainStreamer::set_terrain);
    ClassDB::bind_method(D_METHOD("get_terrain"), &TerrainStreamer::get_terrain);
    ClassDB::bind_method(D_METHOD("set_material", "material"), &TerrainStreamer::set_material);
    ClassDB::bind_method(D_METHOD("get_material"), &TerrainStreamer::get_material);
    ClassDB::bind_method(D_METHOD("set_camera", "camera"), &TerrainStreamer::set_camera);
    ClassDB::bind_method(D_METHOD("set_prefetch_velocity", "velocity"), &TerrainStreamer::set_prefetch_velocity);
    ClassDB::bind_method(D_METHOD("prime"), &TerrainStreamer::prime);
    ClassDB::bind_method(D_METHOD("stats"), &TerrainStreamer::stats);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "terrain", PROPERTY_HINT_RESOURCE_TYPE, "TerrainField"), "set_terrain", "get_terrain");
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_material", "get_material");

#define INT_PROP(name) \
    ClassDB::bind_method(D_METHOD("set_" #name, "value"), &TerrainStreamer::set_##name); \
    ClassDB::bind_method(D_METHOD("get_" #name), &TerrainStreamer::get_##name); \
    ADD_PROPERTY(PropertyInfo(Variant::INT, #name), "set_" #name, "get_" #name);
#define FLOAT_PROP(name) \
    ClassDB::bind_method(D_METHOD("set_" #name, "value"), &TerrainStreamer::set_##name); \
    ClassDB::bind_method(D_METHOD("get_" #name), &TerrainStreamer::get_##name); \
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #name), "set_" #name, "get_" #name);
    INT_PROP(view_cells)
    INT_PROP(workers)
    FLOAT_PROP(evict_age)
    INT_PROP(max_attach)
    INT_PROP(max_upload_kb)
    INT_PROP(max_retire)
    FLOAT_PROP(frame_budget_ms)
#undef INT_PROP
#undef FLOAT_PROP
}

}  // namespace godot
