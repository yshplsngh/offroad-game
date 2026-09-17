// chunk_mesh.hpp - raw vertex/index arrays for one terrain cell.
//
// Port of buildChunk() in src/world/terrain/index.js (browser reference,
// git a59773d): a (seg+1)^2 grid plus a one-vertex skirt ring hanging SKIRT metres down, analytic normals, vertex
// colour and a rock/wet/snow mask. Plain arrays only, so a worker thread can
// build it and the engine thread turns it into a mesh (PLAN: "build raw
// vertex/index/instance arrays on native worker threads").
#pragma once

#include <cstdint>
#include <vector>

#include "worldcore/field.hpp"

namespace worldcore {

inline constexpr int kLodSeg[3] = {64, 32, 16};
inline constexpr double kSkirt = 9.0;

struct ChunkMesh {
    int32_t cx = 0, cz = 0;
    int lod = 0;
    int wide = 0;                  // vertices per side, including the skirt ring
    std::vector<float> positions;  // xyz, cell-local: origin at (cx, cz) * CHUNK
    std::vector<float> normals;    // xyz
    std::vector<float> colors;     // rgb, linear
    std::vector<float> mask;       // rock, wet, snow
    std::vector<int32_t> indices;  // clockwise front faces (Godot convention)
    float min_y = 0, max_y = 0;    // exact vertical extent, for the cell AABB

    size_t vertex_count() const { return positions.size() / 3; }
};

ChunkMesh build_chunk_mesh(const Field& field, int32_t cx, int32_t cz, int lod);

}  // namespace worldcore
