// test_meshgen.cpp - the mesh kernel: triangulation areas, watertight solids
// (every edge shared by exactly two faces, consistent winding), outward normals.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "worldcore/meshgen.hpp"

using namespace worldcore;

namespace {
int g_fail = 0, g_checks = 0;
void check(bool ok, const std::string& what) {
    g_checks++;
    if (!ok && g_fail++ < 25) std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

double poly_area(const std::vector<Vec2>& p) {
    double s = 0;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) s += (p[j].x + p[i].x) * (p[i].y - p[j].y);
    return std::fabs(s / 2);
}

/// Watertight + consistently wound: each directed edge appears once and its reverse once.
bool watertight(const Mesh& m, std::string* why) {
    auto q = [](double v) { return std::llround(v * 1e5); };
    using P = std::tuple<long long, long long, long long>;
    std::map<std::pair<P, P>, int> edges;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        for (int k = 0; k < 3; k++) {
            const Vec3& a = m.positions[m.indices[t + k]];
            const Vec3& b = m.positions[m.indices[t + (k + 1) % 3]];
            P pa{q(a.x), q(a.y), q(a.z)}, pb{q(b.x), q(b.y), q(b.z)};
            if (pa == pb) continue;
            edges[{pa, pb}]++;
        }
    }
    for (const auto& [e, n] : edges) {
        auto rev = edges.find({e.second, e.first});
        if (n != 1 || rev == edges.end() || rev->second != 1) {
            if (why) *why = "edge used " + std::to_string(n) + " times, reverse " + std::to_string(rev == edges.end() ? 0 : rev->second);
            return false;
        }
    }
    return true;
}

/// Signed volume > 0 means faces point outward.
double volume(const Mesh& m) {
    double v = 0;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const Vec3& a = m.positions[m.indices[t]];
        const Vec3& b = m.positions[m.indices[t + 1]];
        const Vec3& c = m.positions[m.indices[t + 2]];
        v += a.dot(b.cross(c)) / 6;
    }
    return v;
}
}  // namespace

int main() {
    // Triangulation covers exactly the polygon area, holes removed.
    const auto outer = std::vector<Vec2>{{0, 0}, {4, 0}, {4, 2}, {2.5, 2}, {2, 1}, {1.5, 2}, {0, 2}};
    const std::vector<std::vector<Vec2>> holes = {rounded_rect(0.6, 0.6, 0.1, 3), {{3, 0.4}, {3.6, 0.4}, {3.6, 1.2}, {3, 1.2}}};
    auto shifted = holes;
    for (auto& v : shifted[0]) { v.x += 0.8; v.y += 0.6; }
    const auto tris = triangulate(outer, shifted);
    std::vector<Vec2> all = outer;
    for (const auto& h : shifted) all.insert(all.end(), h.begin(), h.end());
    double area = 0;
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const Vec2& a = all[tris[t]], &b = all[tris[t + 1]], &c = all[tris[t + 2]];
        const double sa = ((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2;
        check(sa > -1e-12, "triangles counter-clockwise");
        area += std::fabs(sa);
    }
    const double want = poly_area(outer) - poly_area(shifted[0]) - poly_area(shifted[1]);
    check(std::fabs(area - want) < 1e-9, "triangulated area " + std::to_string(area) + " vs " + std::to_string(want));

    std::string why;
    struct Case { const char* name; Mesh m; };
    std::vector<Case> solids = {
        {"rounded_box", rounded_box(0.5, 0.3, 0.2, 0.05, 0.01, 3)},
        {"extrude with holes", extrude(outer, shifted, 0.05, 0.005)},
        {"cylinder", cylinder(0.1, 0.2, 0.5, 12, true)},
        {"cone", cylinder(0.0, 0.2, 0.5, 12, true)},
        {"sphere", sphere(0.3, 16, 12)},
        {"torus", torus(0.3, 0.05, 8, 24)},
        {"tube", tube({{0, 0, 0}, {0.5, 0.2, 0}, {1, 0, 0.4}, {1.2, 0.6, 0.6}}, 0.03, 10)},
        {"rod", rod({0, 0, 0}, {0.3, -0.4, 0.2}, 0.02, 8)},
        {"lathe closed profile", lathe({{0, -0.1}, {0.1, -0.1}, {0.12, 0.0}, {0.1, 0.1}, {0, 0.1}}, 16)},
    };
    for (auto& c : solids) {
        check(c.m.triangles() > 0, std::string(c.name) + " has triangles");
        check(watertight(c.m, &why), std::string(c.name) + " watertight: " + why);
        check(volume(c.m) > 0, std::string(c.name) + " outward (volume " + std::to_string(volume(c.m)) + ")");
        bool unit = c.m.normals.size() == c.m.positions.size();
        for (const auto& n : c.m.normals) unit &= std::fabs(n.length() - 1) < 1e-6;
        check(unit, std::string(c.name) + " unit normals");
    }
    // Box volume matches its dimensions (rounded corners and chamfers remove a little).
    const double bv = volume(rounded_box(0.5, 0.3, 0.2, 0.0, 0.0, 1));
    check(std::fabs(bv - 0.5 * 0.3 * 0.2) < 1e-9, "sharp box volume " + std::to_string(bv));
    // Mirroring keeps solids outward.
    Mesh mirrored = rounded_box(0.5, 0.3, 0.2, 0.05, 0.01, 3);
    mirrored.scale(-1, 1, 1);
    check(volume(mirrored) > 0, "mirrored box stays outward");

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
