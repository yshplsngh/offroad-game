// vegetation_streamer.hpp - static vegetation per visible cell (milestone 3).
//
// Prototypes come from the build-time bake (res://generated/flora.bin) and are
// uploaded once. Cells stream through worldcore::StreamScheduler: placement and
// instance buffers are built on workers; the engine thread creates one
// RenderingServer multimesh per cell + species + LOD + variant, each with an
// exact AABB, shadows off, never updated again until the cell retires.
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/rid.hpp>

#include "terrain_field.hpp"
#include "worldcore/stream.hpp"
#include "worldcore/vegetation.hpp"

namespace godot {

class VegetationStreamer : public Node3D {
    GDCLASS(VegetationStreamer, Node3D)

public:
    VegetationStreamer();
    ~VegetationStreamer() override;

    void set_terrain(const Ref<TerrainField>& terrain) { terrain_ = terrain; }
    Ref<TerrainField> get_terrain() const { return terrain_; }
    void set_solid_material(const Ref<Material>& m) { solidMaterial_ = m; }
    Ref<Material> get_solid_material() const { return solidMaterial_; }
    void set_leaf_material(const Ref<Material>& m) { leafMaterial_ = m; }
    Ref<Material> get_leaf_material() const { return leafMaterial_; }
    void set_camera(Camera3D* camera) { camera_ = camera; }
    void set_prefetch_velocity(const Vector3& v) { velocity_ = v; }

    void set_prototypes_path(const String& p) { prototypesPath_ = p; }
    String get_prototypes_path() const { return prototypesPath_; }
    void set_density(double d) { density_ = d; }
    double get_density() const { return density_; }
    void set_view_cells(int v) { config_.viewCells = v; }
    int get_view_cells() const { return config_.viewCells; }
    void set_workers(int v) { config_.workers = v; }
    int get_workers() const { return config_.workers; }
    void set_max_attach(int v) { budget_.maxAttach = v; }
    int get_max_attach() const { return budget_.maxAttach; }
    void set_max_upload_kb(int v) { budget_.maxUploadBytes = static_cast<size_t>(v) * 1024; }
    int get_max_upload_kb() const { return static_cast<int>(budget_.maxUploadBytes / 1024); }

    /// Load prototypes and build the camera-visible cells (boot). Returns cells built, -1 on error.
    int prime();
    Dictionary stats() const;

protected:
    static void _bind_methods();
    void _notification(int what);

private:
    struct Batch {
        RID multimesh, instance;
    };
    struct Payload;

    bool load_prototypes();
    void ensure_scheduler();
    worldcore::StreamView make_view() const;
    void attach(const worldcore::ReadyCell& cell);
    void release(const worldcore::CellKey& key);
    void release_all();
    double frame_cost_ms() const;

    Ref<TerrainField> terrain_;
    Ref<Material> solidMaterial_, leafMaterial_;
    Camera3D* camera_ = nullptr;
    Vector3 velocity_;
    String prototypesPath_ = "res://generated/flora.bin";
    double density_ = 1.0;
    worldcore::StreamConfig config_;
    worldcore::FrameBudget budget_;

    std::shared_ptr<const worldcore::FloraExtents> extents_;
    std::vector<RID> prototypeMeshes_;  // flat index: species * 3 * 8 + detail * 8 + variant
    bool prototypesLoaded_ = false;
    std::unique_ptr<worldcore::StreamScheduler> scheduler_;
    std::unordered_map<worldcore::CellKey, std::vector<Batch>, worldcore::CellKeyHash> cells_;
    size_t liveBatches_ = 0, liveInstances_ = 0;
    double lastAttachMs_ = 0, worstAttachMs_ = 0;
};

}  // namespace godot
