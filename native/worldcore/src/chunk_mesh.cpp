#include "worldcore/chunk_mesh.hpp"

#include <algorithm>
#include <limits>

#include "worldcore/world.hpp"

namespace worldcore {

ChunkMesh build_chunk_mesh(const Field& field, int32_t cx, int32_t cz, int lod) {
    lod = std::clamp(lod, 0, 2);
    const int seg = kLodSeg[lod];
    const double step = kChunk / seg;
    const int wide = seg + 3;  // grid (seg+1) + skirt on both sides
    const size_t count = static_cast<size_t>(wide) * wide;

    ChunkMesh m;
    m.cx = cx;
    m.cz = cz;
    m.lod = lod;
    m.wide = wide;
    m.positions.resize(count * 3);
    m.normals.resize(count * 3);
    m.colors.resize(count * 3);
    m.mask.resize(count * 3);

    const double ox = cx * kChunk, oz = cz * kChunk;
    float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();

    size_t p = 0;
    for (int j = 0; j < wide; j++) {
        const int gj = std::min(seg, std::max(0, j - 1));
        const bool skirt_j = j == 0 || j == wide - 1;
        for (int i = 0; i < wide; i++) {
            const int gi = std::min(seg, std::max(0, i - 1));
            const bool skirt = skirt_j || i == 0 || i == wide - 1;

            const double lx = gi * step, lz = gj * step;
            const double wx = ox + lx, wz = oz + lz;

            const FieldPoint f = field.compute(wx, wz);
            const auto n = field.normal(wx, wz);
            const FieldClass c = field.classify_from(wx, wz, f, n[1]);

            const float y = static_cast<float>(skirt ? f.h - kSkirt : f.h);
            lo = std::min(lo, y);
            hi = std::max(hi, y);

            m.positions[p] = static_cast<float>(lx);
            m.positions[p + 1] = y;
            m.positions[p + 2] = static_cast<float>(lz);
            m.normals[p] = static_cast<float>(n[0]);
            m.normals[p + 1] = static_cast<float>(n[1]);
            m.normals[p + 2] = static_cast<float>(n[2]);
            m.colors[p] = static_cast<float>(c.r);
            m.colors[p + 1] = static_cast<float>(c.g);
            m.colors[p + 2] = static_cast<float>(c.b);
            m.mask[p] = static_cast<float>(c.rock);
            m.mask[p + 1] = static_cast<float>(c.wet);
            m.mask[p + 2] = static_cast<float>(c.snow);
            p += 3;
        }
    }
    m.min_y = lo;
    m.max_y = hi;

    // Three.js fronts are counter-clockwise, Godot's are clockwise, so each
    // triangle here is the JS triangle with two corners swapped.
    m.indices.resize(static_cast<size_t>(wide - 1) * (wide - 1) * 6);
    size_t q = 0;
    for (int j = 0; j < wide - 1; j++) {
        for (int i = 0; i < wide - 1; i++) {
            const int32_t a = j * wide + i;
            const int32_t b = a + 1;
            const int32_t c = a + wide;
            const int32_t d = c + 1;
            m.indices[q] = a;
            m.indices[q + 1] = b;
            m.indices[q + 2] = c;
            m.indices[q + 3] = b;
            m.indices[q + 4] = d;
            m.indices[q + 5] = c;
            q += 6;
        }
    }
    return m;
}

}  // namespace worldcore
