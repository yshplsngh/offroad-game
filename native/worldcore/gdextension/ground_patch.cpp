#include "ground_patch.hpp"

#include <chrono>
#include <cmath>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void GroundPatch::configure(const Ref<TerrainField>& terrain, Node3D* target) {
    field_ = terrain.is_valid() ? terrain->core() : nullptr;
    target_ = target;
    ERR_FAIL_COND_MSG(!field_, "GroundPatch.configure: terrain is required");
    ERR_FAIL_NULL_MSG(target_, "GroundPatch.configure: target is required");

    if (shape_node_ == nullptr) {
        shape_.instantiate();
        shape_->set_map_width(kSamples);
        shape_->set_map_depth(kSamples);
        heights_.resize(kSamples * kSamples);
        shape_node_ = memnew(CollisionShape3D);
        shape_node_->set_name("Heightfield");
        shape_node_->set_shape(shape_);
        // HeightMapShape3D samples are 1 unit apart; the node's XZ scale sets
        // the world spacing. Height (Y) stays unscaled: the data are metres.
        shape_node_->set_scale(Vector3(kSpacing, 1.0, kSpacing));
        add_child(shape_node_);
    }
    centered_ = false;
    set_physics_process(true);
}

void GroundPatch::recenter(double cx, double cz) {
    const auto t0 = std::chrono::steady_clock::now();
    const int half = kSamples / 2;
    float* h = heights_.ptrw();
    for (int iz = 0; iz < kSamples; iz++) {
        const double z = cz + (iz - half) * kSpacing;
        for (int ix = 0; ix < kSamples; ix++) {
            const double x = cx + (ix - half) * kSpacing;
            h[iz * kSamples + ix] = static_cast<float>(field_->height(x, z) - kBias);
        }
    }
    shape_->set_map_data(heights_);
    set_global_position(Vector3(static_cast<real_t>(cx), 0.0f, static_cast<real_t>(cz)));
    cx_ = cx;
    cz_ = cz;
    centered_ = true;
    refills_++;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms > worst_refill_ms_) worst_refill_ms_ = ms;
}

void GroundPatch::_notification(int what) {
    if (what == NOTIFICATION_READY) {
        set_physics_process(field_ != nullptr);
        return;
    }
    if (what != NOTIFICATION_PHYSICS_PROCESS || !field_ || Engine::get_singleton()->is_editor_hint()) return;
    if (target_ == nullptr || !target_->is_inside_tree()) return;
    const Vector3 p = target_->get_global_position();
    if (centered_ && std::abs(p.x - cx_) < kRecenterDist && std::abs(p.z - cz_) < kRecenterDist) return;
    // Snap to the sample lattice so a recenter shifts by whole cells and every
    // sample lands on world-stable positions.
    recenter(std::round(p.x / kSpacing) * kSpacing, std::round(p.z / kSpacing) * kSpacing);
}

Dictionary GroundPatch::stats() const {
    Dictionary d;
    d["refills"] = refills_;
    d["worst_refill_ms"] = worst_refill_ms_;
    return d;
}

void GroundPatch::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "terrain", "target"), &GroundPatch::configure);
    ClassDB::bind_method(D_METHOD("stats"), &GroundPatch::stats);
}

}  // namespace godot
