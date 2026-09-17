// test_flora.cpp - flora prototypes against the reference's frozen triangle
// total (prototypeTris = 10116 for seed 1337, from the scatter provider's
// stats() in tests/golden/scatter.json). Triangle counts depend on every random
// draw that adds or skips a part, so an exact total checks the draw order.
#include <cmath>
#include <cstdio>
#include <string>

#include "json.hpp"
#include "worldcore/flora.hpp"

using namespace worldcore;

namespace {
int g_fail = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    g_checks++;
    if (!ok && g_fail++ < 25) std::fprintf(stderr, "FAIL %s\n", what.c_str());
}
}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "tests/golden/scatter.json";
    const json::Value g = json::load(path);
    const auto& session = g["sessions"][0];
    const auto seed = static_cast<int32_t>(session["seed"].n);
    const size_t want = static_cast<size_t>(session["stats"]["prototypeTris"].n);

    const auto set = build_all_flora(seed, FloraStyle::Reference);
    size_t tris = 0;
    for (const auto& p : set) {
        tris += p.mesh.triangles();
        const std::string at = std::string(kSpecies[p.species].name) + " d" + std::to_string(p.detail) + " v" +
                               std::to_string(p.variant) + " ";
        check(p.mesh.triangles() > 0, at + "has triangles");
        check(p.mesh.positions.size() == p.mesh.normals.size() && p.mesh.positions.size() == p.mesh.colors.size(),
              at + "attribute sizes");
        bool finite = true, grounded = false;
        float minY = 1e9f;
        for (size_t i = 0; i < p.mesh.positions.size(); i++) finite &= std::isfinite(p.mesh.positions[i]) && std::isfinite(p.mesh.normals[i]) && std::isfinite(p.mesh.colors[i]);
        for (size_t i = 1; i < p.mesh.positions.size(); i += 3) minY = std::min(minY, p.mesh.positions[i]);
        grounded = minY > -0.5f && minY < 0.5f;
        check(finite, at + "finite");
        check(grounded, at + "base near the origin (min y " + std::to_string(minY) + ")");
    }
    std::printf("%zu prototypes, %zu triangles (reference %zu)\n", set.size(), tris, want);
    check(tris == want, "total prototype triangles equal the reference");
    check(set.size() == 2 + 4 + 4 + 2 + 3 + 3 + 1 + 2 + 3 + 1 + 2 + 3 + 2 + 3 + 4 + 3 + 3 + 3, "prototype count");

    // Deterministic, and the bake round-trips exactly.
    const auto again = build_all_flora(seed, FloraStyle::Reference);

    // The shipping set differs only where documented: far pine and far birch.
    const auto native = build_all_flora(seed, FloraStyle::Native);
    bool onlyBirchFar = native.size() == set.size();
    for (size_t i = 0; onlyBirchFar && i < set.size(); i++) {
        const bool changed = native[i].mesh.positions != set[i].mesh.positions;
        const bool allowed = (set[i].species == SPECIES_BIRCH || set[i].species == SPECIES_PINE) && set[i].detail == 0;
        if (changed != allowed) onlyBirchFar = false;
    }
    check(onlyBirchFar, "native flora differs from the reference only in far pine and far birch");
    bool same = again.size() == set.size();
    for (size_t i = 0; same && i < set.size(); i++) same = again[i].mesh.positions == set[i].mesh.positions;
    check(same, "deterministic");
    std::vector<FloraPrototype> loaded;
    std::string err;
    check(deserialize_flora(serialize_flora(set), loaded, &err), "deserialize: " + err);
    bool equal = loaded.size() == set.size();
    for (size_t i = 0; equal && i < set.size(); i++) {
        equal = loaded[i].species == set[i].species && loaded[i].detail == set[i].detail &&
                loaded[i].variant == set[i].variant && loaded[i].doubleSided == set[i].doubleSided &&
                loaded[i].mesh.positions == set[i].mesh.positions && loaded[i].mesh.colors == set[i].mesh.colors;
    }
    check(equal, "bake round-trips");

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
