// test_vehicle_mesh.cpp - procedural vehicles: every catalog rig builds at all
// three LODs, finer LODs carry more detail, triangle budgets hold, geometry is
// finite and inside the truck's own dimensions, and the bake round-trips.
#include <cmath>
#include <cstdio>
#include <string>

#include "json.hpp"
#include "vehicle_design_json.hpp"
#include "worldcore/vehicle_mesh.hpp"

using namespace worldcore;

namespace {
int g_fail = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    g_checks++;
    if (!ok && g_fail++ < 25) std::fprintf(stderr, "FAIL %s\n", what.c_str());
}
bool finite(const PaintedMesh& m) {
    for (size_t i = 0; i < m.positions.size(); i++) {
        const auto& p = m.positions[i];
        const auto& n = m.normals[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(n.x)) return false;
    }
    for (uint32_t idx : m.indices) {
        if (idx >= m.positions.size()) return false;
    }
    return m.looks.size() == m.positions.size();
}
}  // namespace

int main(int argc, char** argv) {
    const std::string data = argc > 1 ? argv[1] : "../godot/data";
    const auto designs = designs_from_json(json::load(data + "/vehicles.json"));
    check(designs.size() == 4, "four catalog vehicles");

    std::vector<VehicleMeshes> set;
    for (const auto& d : designs) {
        VehicleMeshes v = build_vehicle_meshes(d);
        const BodyMetrics m = body_metrics(d);
        size_t prev = 0;
        for (int l = 0; l < 3; l++) {
            const VehicleLod& lod = v.lods[static_cast<size_t>(l)];
            size_t moving = lod.wheel[0].triangles() * 2 + lod.brake[0].triangles() * 2 + lod.axle[0].triangles() +
                            lod.axle[1].triangles() + lod.link[0].triangles() * 4 + lod.link[1].triangles() * 4 +
                            lod.link[2].triangles() * 2 + (lod.shockBody.triangles() + lod.shockShaft.triangles() + lod.spring.triangles()) * 4 +
                            lod.steeringWheel.triangles();
            const size_t total = lod.chassis.triangles() + moving;
            std::printf("%-13s LOD%d: chassis %6zu tris, wheel %5zu, total %6zu\n", d.id.c_str(), l, lod.chassis.triangles(),
                        lod.wheel[0].triangles(), total);
            const std::string at = d.id + " LOD" + std::to_string(l) + " ";
            check(total >= prev, at + "no fewer triangles than the coarser LOD");
            prev = total;
            const size_t budget = l == 2 ? 450000 : l == 1 ? 200000 : 60000;
            check(total < budget, at + "triangle budget (" + std::to_string(total) + " < " + std::to_string(budget) + ")");
            for (const PaintedMesh* pm : {&lod.chassis, &lod.wheel[0], &lod.wheel[1], &lod.brake[0], &lod.axle[0], &lod.axle[1], &lod.link[0],
                                          &lod.shockBody, &lod.spring, &lod.steeringWheel}) {
                check(pm->triangles() > 0 && finite(*pm), at + "part finite and non-empty");
            }
            // The chassis stays inside the truck's footprint (with bumpers, mirrors, spare).
            bool inside = true;
            for (const auto& p : lod.chassis.positions) {
                if (std::fabs(p.x) > m.halfW + 0.35 || p.y < -0.05 || p.y > m.roofY + 1.2 || p.z > m.zFront + 0.5 || p.z < m.zRear - 0.8) inside = false;
            }
            check(inside, at + "chassis inside the vehicle's dimensions");
            // Every material layer that should exist does.
            const auto layers = lod.chassis.by_layer();
            check(layers[0].triangles() > 0 && layers[1].triangles() > 0 && layers[2].triangles() > 0 && layers[3].triangles() > 0,
                  at + "opaque, paint, glass and lamp layers present");
        }
        check(v.rig.links.size() == 10 && v.rig.shocks.size() == 4, d.id + " rig links and shocks");
        set.push_back(std::move(v));
    }

    const auto bytes = serialize_vehicles(set);
    std::vector<VehicleMeshes> back;
    std::string err;
    check(deserialize_vehicles(bytes, back, &err), "deserialize: " + err);
    bool same = back.size() == set.size();
    for (size_t i = 0; same && i < set.size(); i++) {
        same = back[i].id == set[i].id && back[i].lods[2].chassis.indices == set[i].lods[2].chassis.indices &&
               back[i].lods[2].chassis.positions.size() == set[i].lods[2].chassis.positions.size() &&
               back[i].rig.links.size() == set[i].rig.links.size();
    }
    check(same, "bake round-trips");
    std::printf("bake: %.1f MB\n", bytes.size() / 1048576.0);
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
