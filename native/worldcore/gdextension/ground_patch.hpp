// ground_patch.hpp - chassis heightfield collider patch (milestone 3).
//
// The analytic terrain answers wheel raycasts everywhere, so no ground
// geometry exists in the physics world at all - a rolled truck's chassis box
// had nothing to collide with and fell through the world. This static body
// keeps a small Jolt heightfield sampled from the analytic field under the
// target vehicle: the chassis lands on it when rolled, launched or bottoming
// out, while wheels stay on the analytic queries and never touch it.
//
// The patch recenters by whole lattice steps when the target drifts from its
// centre, so sample positions are world-stable. Heights sit a small bias
// below the analytic surface: bilinear interpolation between samples must
// never poke above the true ground into the chassis during normal driving.
#pragma once

#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/height_map_shape3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "terrain_field.hpp"

namespace godot {

class GroundPatch : public StaticBody3D {
    GDCLASS(GroundPatch, StaticBody3D)

public:
    /// Follow `target` (the vehicle) over `terrain`'s analytic ground.
    void configure(const Ref<TerrainField>& terrain, Node3D* target);

    /// {refills, worst_refill_ms}: refill cost is measured, per PLAN's budget rule.
    Dictionary stats() const;

protected:
    static void _bind_methods();
    void _notification(int what);

private:
    void recenter(double cx, double cz);

    static constexpr int kSamples = 33;         // per side; 16 m x 16 m at kSpacing
    static constexpr double kSpacing = 0.5;     // metres between samples
    static constexpr double kRecenterDist = 2.0;  // recenter when the target drifts this far
    static constexpr double kBias = 0.02;       // heights sit this far below the analytic surface

    std::shared_ptr<const worldcore::Field> field_;
    Node3D* target_ = nullptr;
    CollisionShape3D* shape_node_ = nullptr;
    Ref<HeightMapShape3D> shape_;
    PackedFloat32Array heights_;
    double cx_ = 0, cz_ = 0;
    bool centered_ = false;
    int refills_ = 0;
    double worst_refill_ms_ = 0;
};

}  // namespace godot
