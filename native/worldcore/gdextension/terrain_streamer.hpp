// terrain_streamer.hpp - worldcore::StreamScheduler driving render resources.
//
// Workers: field evaluation, meshing, and conversion to Godot packed arrays.
// Engine thread (per frame, budgeted): mesh_add_surface_from_arrays + one
// RenderingServer instance per cell, and capped disposal of retired cells.
// No nodes per cell: instances live directly in the world's scenario.
#pragma once

#include <memory>
#include <unordered_map>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rid.hpp>

#include "terrain_field.hpp"
#include "worldcore/stream.hpp"

namespace godot {

class TerrainStreamer : public Node3D {
    GDCLASS(TerrainStreamer, Node3D)

public:
    ~TerrainStreamer() override;

    void set_terrain(const Ref<TerrainField>& terrain) { terrain_ = terrain; }
    Ref<TerrainField> get_terrain() const { return terrain_; }
    void set_material(const Ref<Material>& material) { material_ = material; }
    Ref<Material> get_material() const { return material_; }
    void set_camera(Camera3D* camera) { camera_ = camera; }
    void set_prefetch_velocity(const Vector3& v) { velocity_ = v; }

    void set_view_cells(int v) { config_.viewCells = v; }
    int get_view_cells() const { return config_.viewCells; }
    void set_workers(int v) { config_.workers = v; }
    int get_workers() const { return config_.workers; }
    void set_evict_age(double v) { config_.evictAge = v; }
    double get_evict_age() const { return config_.evictAge; }
    void set_max_attach(int v) { budget_.maxAttach = v; }
    int get_max_attach() const { return budget_.maxAttach; }
    void set_max_upload_kb(int v) { budget_.maxUploadBytes = static_cast<size_t>(v) * 1024; }
    int get_max_upload_kb() const { return static_cast<int>(budget_.maxUploadBytes / 1024); }
    void set_max_retire(int v) { budget_.maxRetire = v; }
    int get_max_retire() const { return budget_.maxRetire; }
    void set_frame_budget_ms(double v) { budget_.frameBudgetMs = v; }
    double get_frame_budget_ms() const { return budget_.frameBudgetMs; }

    /// Build the camera-visible cells synchronously (boot). Returns how many.
    int prime();
    Dictionary stats() const;

protected:
    static void _bind_methods();
    void _notification(int what);

private:
    struct Instance {
        RID mesh, instance;
    };

    void ensure_scheduler();
    worldcore::StreamView make_view() const;
    void attach(const worldcore::ReadyCell& cell);
    void release(const worldcore::CellKey& key);
    void release_all();
    double frame_cost_ms() const;
    double now() const;

    Ref<TerrainField> terrain_;
    Ref<Material> material_;
    Camera3D* camera_ = nullptr;
    Vector3 velocity_;
    worldcore::StreamConfig config_;
    worldcore::FrameBudget budget_;
    std::unique_ptr<worldcore::StreamScheduler> scheduler_;
    std::unordered_map<worldcore::CellKey, Instance, worldcore::CellKeyHash> instances_;
    double lastAttachMs_ = 0, worstAttachMs_ = 0;
};

}  // namespace godot
