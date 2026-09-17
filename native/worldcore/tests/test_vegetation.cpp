// test_vegetation.cpp - per-cell instancing batches: counts match placement,
// LOD rules for ground cover and deadfall, and every instance's transformed
// prototype lies inside its batch AABB (culling must never hide a visible tree).
#include <cmath>
#include <cstdio>
#include <string>

#include "worldcore/vegetation.hpp"

using namespace worldcore;

namespace {
int g_fail = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    g_checks++;
    if (!ok && g_fail++ < 25) std::fprintf(stderr, "FAIL %s\n", what.c_str());
}
}  // namespace

int main() {
    Field field(1337);
    const auto flora = build_all_flora();
    const auto extents = flora_extents(flora);

    for (auto [cx, cz] : {std::pair{0, 0}, {3, -2}, {-5, 7}, {12, 4}}) {
        const CellPlacement placed = place_cell(field, 1337, cx, cz);
        for (int lod = 0; lod < 3; lod++) {
            const VegetationCell cell = build_vegetation_cell(field, 1337, cx, cz, lod, extents);
            const std::string at = "cell " + std::to_string(cx) + "," + std::to_string(cz) + " lod " + std::to_string(lod) + " ";
            size_t expected = 0;
            for (const auto& p : placed.items) expected += lod <= max_far_lod(p.species) ? 1 : 0;
            check(cell.instances() == expected, at + "instance count " + std::to_string(cell.instances()) + " vs " + std::to_string(expected));

            size_t outside = 0;
            for (const auto& b : cell.batches) {
                check(b.detail == 2 - lod, at + "detail");
                check(b.buffer.size() == b.count * 16, at + "buffer size");
                check(lod <= max_far_lod(b.species), at + "species allowed at this LOD");
                // find the prototype mesh for this batch
                const FloraPrototype* proto = nullptr;
                for (const auto& p : flora) {
                    if (p.species == b.species && p.detail == b.detail && p.variant == b.variant) proto = &p;
                }
                check(proto != nullptr, at + "prototype exists");
                if (!proto) continue;
                for (size_t i = 0; i < b.count; i++) {
                    const float* m = &b.buffer[i * 16];
                    for (size_t v = 0; v < proto->mesh.positions.size(); v += 3) {
                        const double x = proto->mesh.positions[v], y = proto->mesh.positions[v + 1], z = proto->mesh.positions[v + 2];
                        const double wxp = m[0] * x + m[1] * y + m[2] * z + m[3];
                        const double wyp = m[4] * x + m[5] * y + m[6] * z + m[7];
                        const double wzp = m[8] * x + m[9] * y + m[10] * z + m[11];
                        if (wxp < b.aabbMin.x - 1e-3 || wxp > b.aabbMax.x + 1e-3 || wyp < b.aabbMin.y - 1e-3 ||
                            wyp > b.aabbMax.y + 1e-3 || wzp < b.aabbMin.z - 1e-3 || wzp > b.aabbMax.z + 1e-3)
                            outside++;
                    }
                    // rows of the rotation are orthogonal with length = scale
                    const double r0 = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
                    const double r1 = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
                    const double dot = m[0] * m[4] + m[1] * m[5] + m[2] * m[6];
                    if (std::fabs(r0 - r1) > 1e-4 || std::fabs(dot) > 1e-4) outside++;
                }
            }
            check(outside == 0, at + "all prototype vertices inside batch AABBs (" + std::to_string(outside) + " outside)");
            if (cx == 3 && cz == -2) {
                std::printf("cell 3,-2 lod %d: %zu instances in %zu batches\n", lod, cell.instances(), cell.batches.size());
            }
        }
    }
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
