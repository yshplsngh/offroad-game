// test_field.cpp - the C++ terrain must answer exactly what the browser oracle
// answered (tests/golden/terrain.json, frozen from the removed browser reference, git a59773d).
//
// Hashes and PRNG are integer maths and compare with ==. Everything that goes
// through pow/hypot/cos is allowed a tiny tolerance, because libm and V8 may
// differ in the last bit; that is far below anything a wheel or tree can see.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"
#include "worldcore/chunk_mesh.hpp"
#include "worldcore/field.hpp"
#include "worldcore/hash.hpp"
#include "worldcore/noise.hpp"

using namespace worldcore;

namespace {

int g_fail = 0;
int g_checks = 0;
double g_worst = 0;

void check_eq(bool ok, const std::string& what) {
    g_checks++;
    if (!ok && g_fail++ < 20) std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

void check_near(double got, double want, double tol, const std::string& what) {
    g_checks++;
    const double err = std::fabs(got - want);
    if (err > g_worst) g_worst = err;
    if (!(err <= tol) && g_fail++ < 20) {
        std::fprintf(stderr, "FAIL %s: got %.17g want %.17g (err %.3g)\n", what.c_str(), got, want, err);
    }
}

constexpr double kTol = 1e-9;

}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "tests/golden/terrain.json";
    const json::Value g = json::load(path);

    for (const auto& row : g["hash2"].a) {
        const double got = hash2(static_cast<int32_t>(row[0].n), static_cast<int32_t>(row[1].n),
                                 static_cast<int32_t>(row[2].n));
        check_eq(got == row[3].n, "hash2");
    }

    for (const auto& m : g["mulberry32"].a) {
        Mulberry32 rnd(static_cast<uint32_t>(to_int32(m["seed"].n)));
        for (const auto& v : m["values"].a) check_eq(rnd() == v.n, "mulberry32 seed " + std::to_string(m["seed"].n));
    }

    for (const auto& row : g["noise"].a) {
        const double x = row[0].n, z = row[1].n;
        const int32_t s = static_cast<int32_t>(row[2].n);
        check_near(perlin2(x, z, s), row[3].n, kTol, "perlin2");
        check_near(fbm(x, z, s, 4, 2.07, 0.5), row[4].n, kTol, "fbm");
        check_near(ridged(x, z, s, 5, 2.04, 0.46), row[5].n, kTol, "ridged");
    }

    for (const auto& fset : g["fields"].a) {
        const int32_t seed = static_cast<int32_t>(fset["seed"].n);
        Field field(seed);
        const std::string tag = "seed " + std::to_string(seed);
        for (const auto& s : fset["samples"].a) {
            const double x = s["x"].n, z = s["z"].n;
            const std::string at = tag + " (" + std::to_string(x) + "," + std::to_string(z) + ") ";
            const FieldPoint p = field.compute(x, z);
            check_near(p.h, s["h"].n, 1e-7, at + "h");
            check_near(p.bed, s["bed"].n, 1e-7, at + "bed");
            check_near(p.mtn, s["mtn"].n, kTol, at + "mtn");
            check_near(p.rivN, s["rivN"].n, kTol, at + "rivN");
            check_near(p.bogW, s["bogW"].n, kTol, at + "bogW");
            check_near(p.flat, s["flat"].n, kTol, at + "flat");
            check_eq(p.water == s["water"].b, at + "water");

            const auto n = field.normal(x, z);
            for (int k = 0; k < 3; k++) check_near(n[k], s["n"][k].n, 1e-6, at + "normal");

            // Classify from the oracle's own normal so a 1e-15 normal wobble on a
            // threshold cannot mask a real classification bug, or fake one.
            const FieldClass c = field.classify(x, z, s["n"][1].n);
            check_eq(c.surface == static_cast<int>(s["surface"].n), at + "surface");
            check_eq(c.biome == static_cast<int>(s["biome"].n), at + "biome");
            check_near(c.wet, s["wet"].n, kTol, at + "wet");
            check_near(c.rock, s["rock"].n, kTol, at + "rock");
            check_near(c.snow, s["snow"].n, 1e-7, at + "snow");
            check_near(c.r, s["rgb"][0].n, kTol, at + "r");
            check_near(c.g, s["rgb"][1].n, kTol, at + "g");
            check_near(c.b, s["rgb"][2].n, kTol, at + "b");
        }
    }

    for (const auto& sp : g["spawns"].a) {
        Field field(static_cast<int32_t>(sp["seed"].n));
        const Spawn got = field.find_spawn(sp["qx"].n, sp["qz"].n);
        const std::string at = "spawn seed " + std::to_string(sp["seed"].n) + " ";
        check_eq(got.ok == sp["ok"].b, at + "ok");
        check_near(got.x, sp["x"].n, 1e-6, at + "x");
        check_near(got.z, sp["z"].n, 1e-6, at + "z");
        check_near(got.y, sp["y"].n, 1e-6, at + "y");
    }

    // Workers query one Field concurrently. Every thread must see the oracle's
    // numbers while tiles are being built, shared and evicted underneath it.
    {
        const auto& fset = g["fields"][0];
        Field field(static_cast<int32_t>(fset["seed"].n));
        std::atomic<int> bad{0};
        std::vector<std::thread> pool;
        for (int t = 0; t < 8; t++) {
            pool.emplace_back([&, t] {
                for (int pass = 0; pass < 3; pass++) {
                    for (size_t i = 0; i < fset["samples"].size(); i++) {
                        const auto& s = fset["samples"][(i * 7 + t * 131) % fset["samples"].size()];
                        if (std::fabs(field.height(s["x"].n, s["z"].n) - s["h"].n) > 1e-7) bad++;
                    }
                    if (t == 0) field.clear_caches();
                }
            });
        }
        for (auto& th : pool) th.join();
        check_eq(bad == 0, "threaded heights (" + std::to_string(bad.load()) + " wrong)");
    }

    // Chunk mesh: interior vertices sit exactly on the field, the skirt ring
    // hangs SKIRT below its edge vertex, and every index is in range.
    {
        Field field(1337);
        for (int lod = 0; lod < 3; lod++) {
            const ChunkMesh m = build_chunk_mesh(field, -1, 2, lod);
            const int seg = kLodSeg[lod];
            check_eq(m.wide == seg + 3 && m.vertex_count() == size_t(m.wide) * m.wide, "chunk size");
            const double step = kChunk / seg;
            const int j = seg / 2 + 1, i = 1;  // an interior vertex on the west edge
            const size_t v = size_t(j * m.wide + i) * 3;
            check_near(m.positions[v + 1], field.height(-kChunk + 0, 2 * kChunk + (j - 1) * step), 1e-4, "chunk vertex height");
            check_near(m.positions[v + 1] - m.positions[v - 3 + 1], kSkirt, 1e-4, "chunk skirt drop");
            bool in_range = true;
            for (int32_t idx : m.indices) in_range &= idx >= 0 && size_t(idx) < m.vertex_count();
            check_eq(in_range, "chunk indices");
            check_eq(m.min_y <= m.max_y - kSkirt + 1e-3, "chunk extent");
        }
    }

    std::printf("%d checks, %d failed, worst abs error %.3g\n", g_checks, g_fail, g_worst);
    return g_fail == 0 ? 0 : 1;
}
