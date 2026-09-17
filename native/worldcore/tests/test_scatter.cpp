// test_scatter.cpp - placement against the reference's frozen scatter oracle
// (tests/golden/scatter.json: trees, snags, deadfall and boulders per chunk for
// two seeds, recorded from the browser reference, git a59773d), plus the
// properties the native rule must have: immutable, thread-safe, density-scaled.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"
#include "worldcore/scatter.hpp"

using namespace worldcore;

namespace {

int g_fail = 0, g_checks = 0;
double g_worst = 0;

void check(bool ok, const std::string& what) {
    g_checks++;
    if (!ok && g_fail++ < 25) std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

void near(double got, double want, double tol, const std::string& what) {
    g_checks++;
    const double err = std::fabs(got - want);
    g_worst = std::max(g_worst, err);
    if (!(err <= tol) && g_fail++ < 25) std::fprintf(stderr, "FAIL %s: got %.17g want %.17g\n", what.c_str(), got, want);
}

bool same(const CellPlacement& a, const CellPlacement& b) {
    if (a.items.size() != b.items.size()) return false;
    for (size_t i = 0; i < a.items.size(); i++) {
        const auto& p = a.items[i];
        const auto& q = b.items[i];
        if (p.species != q.species || p.variant != q.variant || p.position.x != q.position.x ||
            p.position.z != q.position.z || p.scale != q.scale || p.rotation.w != q.rotation.w)
            return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "tests/golden/scatter.json";
    const json::Value g = json::load(path);

    size_t obstacles = 0;
    for (const auto& session : g["sessions"].a) {
        const auto seed = static_cast<int32_t>(session["seed"].n);
        Field field(seed);
        for (const auto& chunk : session["chunks"].a) {
            const auto cx = static_cast<int32_t>(chunk["cx"].n), cz = static_cast<int32_t>(chunk["cz"].n);
            const int ring = static_cast<int>(chunk["ring"].n);
            const std::string at = "seed " + std::to_string(seed) + " cell " + std::to_string(cx) + "," +
                                   std::to_string(cz) + " ring " + std::to_string(ring) + " ";
            const CellPlacement cell = place_cell_reference(field, seed, cx, cz, ring);
            std::vector<const Placement*> solid;
            for (const auto& p : cell.items) {
                if (kSpecies[p.species].radius > 0) solid.push_back(&p);
            }
            const auto& want = chunk["obstacles"];
            check(solid.size() == want.size(),
                  at + "obstacle count " + std::to_string(solid.size()) + " vs " + std::to_string(want.size()));
            for (size_t i = 0; i < std::min(solid.size(), want.size()); i++) {
                const auto& w = want[i];
                const Placement& p = *solid[i];
                check(kSpecies[p.species].name == w[0].s, at + "kind " + kSpecies[p.species].name + " vs " + w[0].s);
                near(p.position.x, w[1].n, 1e-9, at + "x");
                near(p.position.z, w[2].n, 1e-9, at + "z");
                near(p.groundY, w[3].n, 1e-7, at + "y");
                near(p.collision_radius(), w[4].n, 1e-9, at + "r");
            }
            obstacles += want.size();

            // The native rule is the reference's ring-0 rule, for every ring.
            if (ring == 0) check(same(place_cell(field, seed, cx, cz), cell), at + "native == reference at ring 0");
        }
    }

    // Immutable and thread-safe: the same cell from many threads, many times.
    {
        Field field(1337);
        const CellPlacement once = place_cell(field, 1337, 3, -2);
        std::atomic<int> bad{0};
        std::vector<std::thread> pool;
        for (int t = 0; t < 6; t++) {
            pool.emplace_back([&] {
                for (int k = 0; k < 4; k++) {
                    if (!same(place_cell(field, 1337, 3, -2), once)) bad++;
                }
            });
        }
        for (auto& th : pool) th.join();
        check(bad == 0 && !once.items.empty(), "placement is immutable across calls and threads");

        const CellPlacement thin = place_cell(field, 1337, 3, -2, 0.5);
        std::printf("cell 3,-2: %zu placements at density 1, %zu at 0.5\n", once.items.size(), thin.items.size());
        check(thin.items.size() < once.items.size(), "density scales placement");
        size_t cover = 0;
        for (const auto& p : once.items) cover += kSpecies[p.species].groundCover ? 1 : 0;
        check(cover > 0, "native placement includes ground cover");
    }

    std::printf("%d checks (%zu reference obstacles), %d failed, worst abs error %.3g\n", g_checks, obstacles, g_fail,
                g_worst);
    return g_fail == 0 ? 0 : 1;
}
