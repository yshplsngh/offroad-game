// test_ground_texture.cpp - the baked grass detail: tiles without a seam, keeps
// biome brightness (mean multiplier 1.0), valid normal map, deterministic,
// round-trips through the bake format.
#include <cmath>
#include <cstdio>
#include <string>

#include "worldcore/ground_texture.hpp"

using namespace worldcore;

namespace {
int g_fail = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    g_checks++;
    if (!ok && g_fail++ < 25) std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

/// Mean absolute difference between two columns (or rows) of a channel.
double line_diff(const std::vector<uint8_t>& px, int size, int a, int b, bool columns, int channel) {
    double sum = 0;
    for (int k = 0; k < size; k++) {
        const int ia = columns ? k * size + a : a * size + k;
        const int ib = columns ? k * size + b : b * size + k;
        sum += std::fabs(static_cast<double>(px[static_cast<size_t>(ia) * 4 + channel]) - px[static_cast<size_t>(ib) * 4 + channel]);
    }
    return sum / size;
}
}  // namespace

int main() {
    const int size = 256;
    const GroundTextures t = build_ground_textures(size, 1337);
    check(t.size == size && t.albedo.size() == size_t(size) * size * 4 && t.normal.size() == t.albedo.size(), "sizes");

    // Seams: wrapping from the last column/row to the first must look like any
    // other neighbouring pair, not like a cut.
    for (int ch = 0; ch < 3; ch++) {
        double interior = 0;
        for (int x = 0; x + 1 < size; x += 17) interior += line_diff(t.albedo, size, x, x + 1, true, ch);
        interior /= (size - 1 + 16) / 17;
        const double seamX = line_diff(t.albedo, size, size - 1, 0, true, ch);
        const double seamY = line_diff(t.albedo, size, size - 1, 0, false, ch);
        std::printf("channel %d: neighbour diff %.2f, seam x %.2f, seam y %.2f\n", ch, interior, seamX, seamY);
        check(seamX < interior * 1.6 + 1 && seamY < interior * 1.6 + 1, "seamless tile, channel " + std::to_string(ch));
    }

    double mean = 0;
    for (size_t i = 0; i < t.albedo.size(); i += 4) mean += (t.albedo[i] + t.albedo[i + 1] + t.albedo[i + 2]) / 3.0;
    mean /= t.albedo.size() / 4;
    std::printf("mean albedo byte %.1f (128 = x1.0)\n", mean);
    check(std::fabs(mean - 127.5) < 3, "mean detail multiplier is 1.0");

    bool unit = true, up = true, variation = false;
    for (size_t i = 0; i < t.normal.size(); i += 4) {
        const double nx = t.normal[i] / 127.5 - 1, ny = t.normal[i + 1] / 127.5 - 1, nz = t.normal[i + 2] / 127.5 - 1;
        unit &= std::fabs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1) < 0.03;
        up &= nz > 0.3;
        variation |= std::fabs(nx) > 0.2;
    }
    check(unit && up && variation, "normal map: unit, facing up, not flat");

    const GroundTextures again = build_ground_textures(size, 1337);
    check(again.albedo == t.albedo && again.normal == t.normal, "deterministic");
    GroundTextures back;
    std::string err;
    check(deserialize_ground(serialize_ground(t), back, &err) && back.albedo == t.albedo && back.normal == t.normal, "round-trip: " + err);

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
