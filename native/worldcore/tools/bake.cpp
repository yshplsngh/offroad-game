// bake.cpp - build-time asset baker. Writes the procedural flora prototypes,
// the procedural vehicle meshes (all catalog rigs x 3 LODs) and the procedural
// ground detail textures to binary files the game loads, so nothing is
// generated while the game runs.
//
//   worldcore_bake <output dir> <data dir containing vehicles.json>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

#include "json.hpp"
#include "vehicle_design_json.hpp"
#include "worldcore/flora.hpp"
#include "worldcore/ground_texture.hpp"
#include "worldcore/vehicle_mesh.hpp"

namespace {
bool write(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: worldcore_bake <output dir> <data dir>\n");
        return 2;
    }
    const std::string outDir = argv[1], dataDir = argv[2];
    const auto t0 = std::chrono::steady_clock::now();

    const auto flora = worldcore::build_all_flora();
    size_t floraTris = 0;
    for (const auto& p : flora) floraTris += p.mesh.triangles();
    if (!write(outDir + "/flora.bin", worldcore::serialize_flora(flora))) {
        std::fprintf(stderr, "worldcore_bake: cannot write %s/flora.bin\n", outDir.c_str());
        return 1;
    }

    std::vector<worldcore::VehicleMeshes> vehicles;
    try {
        for (const auto& d : designs_from_json(json::load(dataDir + "/vehicles.json"))) vehicles.push_back(worldcore::build_vehicle_meshes(d));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "worldcore_bake: %s\n", e.what());
        return 1;
    }
    const auto vbytes = worldcore::serialize_vehicles(vehicles);
    if (!write(outDir + "/vehicles.bin", vbytes)) {
        std::fprintf(stderr, "worldcore_bake: cannot write %s/vehicles.bin\n", outDir.c_str());
        return 1;
    }
    const auto ground = worldcore::build_ground_textures(512);
    if (!write(outDir + "/ground.bin", worldcore::serialize_ground(ground))) {
        std::fprintf(stderr, "worldcore_bake: cannot write %s/ground.bin\n", outDir.c_str());
        return 1;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("worldcore_bake: %zu flora prototypes (%zu tris), %zu vehicles (%.1f MB), ground %dpx in %.0f ms -> %s\n", flora.size(),
                floraTris, vehicles.size(), vbytes.size() / 1048576.0, ground.size, ms, outDir.c_str());
    return 0;
}
