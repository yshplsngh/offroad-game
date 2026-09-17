// flora.cpp - see flora.hpp. Geometry generators reproduce Three.js's
// CylinderGeometry, PlaneGeometry and IcosahedronGeometry vertex order, and the
// species builders draw random numbers in the reference's order, because every
// draw decides what the rest of the prototype looks like.
#include "worldcore/flora.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <numbers>

#include "worldcore/hash.hpp"

namespace worldcore {

namespace {

constexpr double PI = std::numbers::pi;

/* ------------------------------------------------------------- colours -- */

struct Color {
    double r = 0, g = 0, b = 0;
    Color lerp(const Color& o, double t) const { return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t}; }
    Color operator*(double s) const { return {r * s, g * s, b * s}; }
};

double srgb_to_linear(double c) { return c < 0.04045 ? c * 0.0773993808 : std::pow(c * 0.9478672986 + 0.0521327014, 2.4); }

/// THREE.Color().setHex(hex, SRGBColorSpace): stored in the linear working space.
Color hex(uint32_t h) {
    return {srgb_to_linear(((h >> 16) & 255) / 255.0), srgb_to_linear(((h >> 8) & 255) / 255.0),
            srgb_to_linear((h & 255) / 255.0)};
}

const Color BARK_PINE = hex(0x4a3526), BARK_PINE_LIT = hex(0x6b4f39);
const Color NEEDLE = hex(0x2d4426), NEEDLE_TIP = hex(0x4a6b35);
const Color BARK_BIRCH = hex(0xcfc9ba), BARK_BIRCH_DARK = hex(0x3b3a36);
const Color LEAF = hex(0x5c7a32), LEAF_PALE = hex(0x87a047);
const Color DEAD = hex(0x6a5c4a), STONE = hex(0x6e6a64), LICHEN = hex(0x7d8a4e);
const Color FERN = hex(0x466b2e), GRASS = hex(0x5f7434), GRASS_DRY = hex(0x8a8443);

/* --------------------------------------------------------------- noise -- */

double hash3(int32_t x, int32_t y, int32_t z) {
    uint32_t h = (static_cast<uint32_t>(x) * 374761393u) ^ (static_cast<uint32_t>(y) * 668265263u) ^
                 (static_cast<uint32_t>(z) * 2147483647u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<double>(h ^ (h >> 16)) / 4294967296.0;
}

double smooth01(double t) { return t * t * (3 - 2 * t); }
double mix(double a, double b, double t) { return a + (b - a) * t; }

double vnoise3(double x, double y, double z) {
    const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int32_t xi = to_int32(fx), yi = to_int32(fy), zi = to_int32(fz);
    const double u = smooth01(x - fx), v = smooth01(y - fy), w = smooth01(z - fz);
    auto c = [&](int dx, int dy, int dz) { return hash3(xi + dx, yi + dy, zi + dz); };
    return mix(mix(mix(c(0, 0, 0), c(1, 0, 0), u), mix(c(0, 1, 0), c(1, 1, 0), u), v),
               mix(mix(c(0, 0, 1), c(1, 0, 1), u), mix(c(0, 1, 1), c(1, 1, 1), u), v), w);
}

double fbm3(double x, double y, double z, int octaves = 3) {
    double sum = 0, amp = 1, f = 1, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += vnoise3(x * f, y * f, z * f) * amp;
        norm += amp;
        amp *= 0.5;
        f *= 2.03;
    }
    return sum / norm;
}

/* ------------------------------------------------------------ geometry -- */

/// Indexed or non-indexed triangles in the reference's vertex order.
struct Geo {
    std::vector<Vec3> v;
    std::vector<uint32_t> idx;  // empty = non-indexed

    void translate(double x, double y, double z) {
        for (auto& p : v) p = p + Vec3{x, y, z};
    }
    void rotate_x(double a) {
        const double c = std::cos(a), s = std::sin(a);
        for (auto& p : v) p = {p.x, c * p.y - s * p.z, s * p.y + c * p.z};
    }
    void rotate_y(double a) {
        const double c = std::cos(a), s = std::sin(a);
        for (auto& p : v) p = {c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
    }
    void rotate_z(double a) {
        const double c = std::cos(a), s = std::sin(a);
        for (auto& p : v) p = {c * p.x - s * p.y, s * p.x + c * p.y, p.z};
    }
    std::vector<Vec3> soup() const {
        if (idx.empty()) return v;
        std::vector<Vec3> out;
        out.reserve(idx.size());
        for (uint32_t i : idx) out.push_back(v[i]);
        return out;
    }
};

/// THREE.CylinderGeometry(radiusTop, radiusBottom, height, radialSegments, 1, openEnded)
Geo cylinder(double rTop, double rBottom, double height, int radial, bool openEnded) {
    Geo g;
    const double half = height / 2;
    std::vector<std::vector<uint32_t>> rows;
    for (int y = 0; y <= 1; y++) {
        std::vector<uint32_t> row;
        const double v = y;
        const double radius = v * (rBottom - rTop) + rTop;
        for (int x = 0; x <= radial; x++) {
            const double theta = static_cast<double>(x) / radial * PI * 2;
            g.v.push_back({radius * std::sin(theta), -v * height + half, radius * std::cos(theta)});
            row.push_back(static_cast<uint32_t>(g.v.size() - 1));
        }
        rows.push_back(row);
    }
    for (int x = 0; x < radial; x++) {
        const uint32_t a = rows[0][x], b = rows[1][x], c = rows[1][x + 1], d = rows[0][x + 1];
        g.idx.insert(g.idx.end(), {a, b, d, b, c, d});
    }
    if (!openEnded) {
        for (int top = 1; top >= 0; top--) {
            const double radius = top ? rTop : rBottom;
            const double sign = top ? 1 : -1;
            const auto centreStart = static_cast<uint32_t>(g.v.size());
            for (int x = 1; x <= radial; x++) g.v.push_back({0, half * sign, 0});
            const auto centreEnd = static_cast<uint32_t>(g.v.size());
            for (int x = 0; x <= radial; x++) {
                const double theta = static_cast<double>(x) / radial * PI * 2;
                g.v.push_back({radius * std::sin(theta), half * sign, radius * std::cos(theta)});
            }
            for (int x = 0; x < radial; x++) {
                const uint32_t c = centreStart + x, i = centreEnd + x;
                if (top) g.idx.insert(g.idx.end(), {i, i + 1, c});
                else g.idx.insert(g.idx.end(), {i + 1, i, c});
            }
        }
    }
    return g;
}

/// Tapered open stem, base at the origin (flora.js stem()).
Geo stem(double bottomR, double topR, double height, int sides, double offsetY = 0) {
    Geo g = cylinder(topR, bottomR, height, sides, true);
    g.translate(0, height / 2 + offsetY, 0);
    return g;
}

/// THREE.PlaneGeometry(width, height, ws, hs)
Geo plane(double width, double height, int ws, int hs) {
    Geo g;
    const double segW = width / ws, segH = height / hs;
    for (int iy = 0; iy <= hs; iy++) {
        const double y = iy * segH - height / 2;
        for (int ix = 0; ix <= ws; ix++) g.v.push_back({ix * segW - width / 2, -y, 0});
    }
    const uint32_t gx = static_cast<uint32_t>(ws + 1);
    for (uint32_t iy = 0; iy < static_cast<uint32_t>(hs); iy++) {
        for (uint32_t ix = 0; ix < static_cast<uint32_t>(ws); ix++) {
            const uint32_t a = ix + gx * iy, b = ix + gx * (iy + 1), c = ix + 1 + gx * (iy + 1), d = ix + 1 + gx * iy;
            g.idx.insert(g.idx.end(), {a, b, d, b, c, d});
        }
    }
    return g;
}

/// THREE.IcosahedronGeometry(radius, detail): subdivided, non-indexed.
Geo icosahedron(double radius, int detail) {
    const double t = (1 + std::sqrt(5.0)) / 2;
    const double V[] = {-1, t, 0, 1, t, 0, -1, -t, 0, 1, -t, 0, 0, -1, t, 0, 1, t,
                        0, -1, -t, 0, 1, -t, t, 0, -1, t, 0, 1, -t, 0, -1, -t, 0, 1};
    const int I[] = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                     3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    auto vert = [&](int i) { return Vec3{V[i * 3], V[i * 3 + 1], V[i * 3 + 2]}; };
    auto lerp = [](const Vec3& a, const Vec3& b, double k) { return a + (b - a) * k; };
    Geo g;
    const int cols = detail + 1;
    for (int f = 0; f < 20; f++) {
        const Vec3 a = vert(I[f * 3]), b = vert(I[f * 3 + 1]), c = vert(I[f * 3 + 2]);
        std::vector<std::vector<Vec3>> rows(static_cast<size_t>(cols + 1));
        for (int i = 0; i <= cols; i++) {
            const Vec3 aj = lerp(a, c, static_cast<double>(i) / cols);
            const Vec3 bj = lerp(b, c, static_cast<double>(i) / cols);
            const int n = cols - i;
            for (int j = 0; j <= n; j++) {
                rows[static_cast<size_t>(i)].push_back(j == 0 && i == cols ? aj : lerp(aj, bj, static_cast<double>(j) / n));
            }
        }
        for (int i = 0; i < cols; i++) {
            for (int j = 0; j < 2 * (cols - i) - 1; j++) {
                const size_t k = static_cast<size_t>(j / 2);
                const auto& ri = rows[static_cast<size_t>(i)];
                const auto& rn = rows[static_cast<size_t>(i + 1)];
                if (j % 2 == 0) g.v.insert(g.v.end(), {ri[k + 1], rn[k], ri[k]});
                else g.v.insert(g.v.end(), {ri[k + 1], rn[k + 1], rn[k]});
            }
        }
    }
    for (auto& p : g.v) p = p.normalized() * radius;
    return g;
}

/// geo.js needleWhorl()
Geo needle_whorl(double radius, int spokes, double droop, Mulberry32& rng, double width, double jitter, double lift = 0.07) {
    constexpr double fr[3] = {0.08, 0.58, 1.0};
    Geo g;
    for (int s = 0; s < spokes; s++) {
        const double a = static_cast<double>(s) / spokes * PI * 2 + (rng() - 0.5) * (PI * 2 / spokes) * jitter;
        const double len = radius * (1 - jitter * 0.45 + rng() * jitter * 0.9);
        const double ca = std::cos(a), sa = std::sin(a);
        const auto base = static_cast<uint32_t>(g.v.size());
        for (int r = 0; r < 3; r++) {
            const double t = fr[r];
            const double rad = len * t;
            const double y = -droop * len * t * t + lift * radius * (1 - t);
            const double hw = width * len * (1 - t * 0.92);
            const double up = lift * len * 0.55 * (1 - t);
            g.v.push_back({ca * rad - sa * hw, y, sa * rad + ca * hw});
            g.v.push_back({ca * rad, y + up, sa * rad});
            g.v.push_back({ca * rad + sa * hw, y, sa * rad - ca * hw});
        }
        for (uint32_t r = 0; r < 2; r++) {
            const uint32_t a0 = base + r * 3, b0 = base + (r + 1) * 3;
            g.idx.insert(g.idx.end(), {a0, b0, a0 + 1, a0 + 1, b0, b0 + 1});
            g.idx.insert(g.idx.end(), {a0 + 1, b0 + 1, a0 + 2, a0 + 2, b0 + 1, b0 + 2});
        }
    }
    return g;
}

/// geo.js ribbon()
Geo ribbon(const std::vector<Vec3>& pts, const std::vector<double>& halfWidths, const Vec3& side) {
    Geo g;
    for (size_t i = 0; i < pts.size(); i++) {
        g.v.push_back(pts[i] + side * halfWidths[i]);
        g.v.push_back(pts[i] - side * halfWidths[i]);
    }
    for (uint32_t i = 0; i + 1 < pts.size(); i++) {
        const uint32_t a = i * 2;
        g.idx.insert(g.idx.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
    return g;
}

/// geo.js blob(): lumpy icosphere.
Geo blob(double radius, int detail, Mulberry32& rng, double warp, double freq, Vec3 squash = {1, 1, 1},
         double flatten = 0) {
    Geo g = icosahedron(radius, detail);
    const double ox = rng() * 64, oy = rng() * 64, oz = rng() * 64;
    const double f = freq / std::max(0.05, radius);
    for (auto& p : g.v) {
        const double n = fbm3(p.x * f + ox, p.y * f + oy, p.z * f + oz, 2) - 0.5;
        const double k = 1 + n * warp * 2;
        Vec3 o{p.x * k * squash.x, p.y * k * squash.y, p.z * k * squash.z};
        if (flatten > 0 && o.y < -radius * flatten) o.y = -radius * flatten;
        p = o;
    }
    return g;
}

/* ------------------------------------------------------------ assembly -- */

using ColorFn = std::function<Color(const Vec3& p, const Vec3& n)>;

struct Group {
    std::vector<Geo> geoms;
    ColorFn color;
};

/// geo.js assemble(): merge each group to a flat-shaded soup, colour it, concatenate.
FloraMesh assemble(const std::vector<Group>& groups) {
    FloraMesh out;
    for (const Group& group : groups) {
        std::vector<Vec3> soup;
        for (const Geo& g : group.geoms) {
            const auto s = g.soup();
            soup.insert(soup.end(), s.begin(), s.end());
        }
        for (size_t t = 0; t + 2 < soup.size(); t += 3) {
            const Vec3 a = soup[t], b = soup[t + 1], c = soup[t + 2];
            // computeVertexNormals on non-indexed geometry: the face normal (cb x ab).
            Vec3 n = (c - b).cross(a - b).normalized();
            for (const Vec3& p : {a, b, c}) {
                const Color col = group.color(p, n);
                out.positions.insert(out.positions.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
                out.normals.insert(out.normals.end(), {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
                out.colors.insert(out.colors.end(), {static_cast<float>(col.r), static_cast<float>(col.g), static_cast<float>(col.b)});
            }
        }
    }
    return out;
}

/// geo.js bend(): parabolic lean baked into the positions (normals are left as they were).
void bend(FloraMesh& m, double kx, double kz, double height) {
    const double inv = 1 / std::max(0.001, height);
    for (size_t i = 0; i < m.positions.size(); i += 3) {
        const double t = std::max(0.0, m.positions[i + 1] * inv);
        const double s = t * t * height;
        m.positions[i] = static_cast<float>(m.positions[i] + kx * s);
        m.positions[i + 2] = static_cast<float>(m.positions[i + 2] + kz * s);
    }
}

/* ------------------------------------------------------------- species -- */

FloraMesh pine(Mulberry32& rng, int detail, FloraStyle style) {
    const double height = 9 + rng() * 11;
    const double trunkR = height * (0.016 + rng() * 0.006);
    const double crownBase = height * (0.20 + rng() * 0.14);
    const double spread = height * (0.15 + rng() * 0.05);

    if (detail == 0 && style == FloraStyle::Native) {
        Geo trunk = stem(trunkR * 1.5, trunkR * 0.5, crownBase + 0.5, 3);
        Geo cone = cylinder(0, spread * 1.15, height - crownBase, 6, true);
        cone.translate(0, crownBase + (height - crownBase) / 2, 0);
        return assemble({{{trunk}, [](const Vec3&, const Vec3&) { return BARK_PINE; }},
                         {{cone}, [=](const Vec3& p, const Vec3& n) {
                              const double t = std::min(1.0, std::max(0.0, (p.y - crownBase) / (height - crownBase)));
                              return NEEDLE.lerp(NEEDLE_TIP, t * 0.6) * (0.85 + 0.15 * std::max(0.0, n.y));
                          }}});
    }

    if (detail == 0) {
        std::vector<Geo> cards;
        for (int i = 0; i < 3; i++) {
            Geo g = plane(spread * 1.5 * 2, height, 1, 2);
            g.translate(0, height / 2, 0);
            g.rotate_y(i / 3.0 * PI);
            cards.push_back(g);
        }
        return assemble({{cards, [=](const Vec3& p, const Vec3&) {
                              return NEEDLE.lerp(NEEDLE_TIP, std::min(1.0, p.y / height)) *
                                     (0.8 + 0.2 * (1 - std::fabs(p.x) / spread));
                          }}});
    }

    std::vector<Geo> bark{stem(trunkR * 1.5, trunkR * 0.28, height, detail > 1 ? 6 : 5)};
    std::vector<Geo> foliage;
    const int whorls = detail > 1 ? 11 : 8;
    const int spokes = detail > 1 ? 8 : 6;
    const double needleWidth = detail > 1 ? 0.30 : 0.42;
    for (int i = 0; i < whorls; i++) {
        const double t = static_cast<double>(i) / (whorls - 1);
        const double y = crownBase + (height - crownBase) * t;
        const double shape = std::sin(std::min(1.0, (1 - t) * 1.35) * PI * 0.5);
        const double r = spread * shape * (0.82 + rng() * 0.36);
        if (r < 0.15) continue;
        const double droop = 0.34 + rng() * 0.22;
        Geo w = needle_whorl(r, spokes + (t < 0.4 ? 2 : 0), droop, rng, needleWidth, 0.5);
        const double tx = (rng() - 0.5) * r * 0.1;
        const double tz = (rng() - 0.5) * r * 0.1;
        w.translate(tx, y, tz);
        w.rotate_y(rng() * PI * 2);
        foliage.push_back(w);
    }
    const double lean = (rng() - 0.5) * 0.05;
    const double leanZ = (rng() - 0.5) * 0.05;

    FloraMesh m = assemble({
        {bark, [=](const Vec3& p, const Vec3& n) {
             return BARK_PINE.lerp(BARK_PINE_LIT, std::fabs(n.x) * 0.5 + (p.y / height) * 0.3);
         }},
        {foliage, [=](const Vec3& p, const Vec3&) {
             const double t = std::min(1.0, std::max(0.0, (p.y - crownBase) / (height - crownBase)));
             return NEEDLE.lerp(NEEDLE_TIP, t * 0.75 + std::min(0.25, std::fabs(p.x) * 0.1));
         }},
    });
    bend(m, lean, leanZ, height);
    return m;
}

FloraMesh birch(Mulberry32& rng, int detail, FloraStyle style) {
    const double height = 7 + rng() * 7;
    const double trunkR = height * 0.013;
    const double forkY = height * (0.42 + rng() * 0.12);

    if (detail == 0 && style == FloraStyle::Native) {
        // A closed crown cluster: reads as a tree from every angle at any distance.
        Geo trunk = stem(trunkR * 1.4, trunkR, forkY + height * 0.1, 3);
        Geo crown = blob(height * 0.34, 0, rng, 0.35, 1.6, {1, 0.85, 1});
        crown.translate(0, height * 0.66, 0);
        return assemble({{{trunk}, [](const Vec3&, const Vec3&) { return BARK_BIRCH; }},
                         {{crown}, [](const Vec3&, const Vec3& n) { return LEAF.lerp(LEAF_PALE, std::max(0.0, n.y) * 0.35); }}});
    }

    if (detail == 0) {
        std::vector<Geo> cards;
        for (int i = 0; i < 2; i++) {
            Geo g = plane(height * 0.62, height * 0.75, 1, 1);
            g.translate(0, height * 0.66, 0);
            g.rotate_y(i / 2.0 * PI + rng());
            cards.push_back(g);
        }
        Geo trunk = stem(trunkR * 1.4, trunkR, forkY, 3);
        return assemble({{{trunk}, [](const Vec3&, const Vec3&) { return BARK_BIRCH; }},
                         {cards, [](const Vec3&, const Vec3&) { return LEAF; }}});
    }

    std::vector<Geo> bark{stem(trunkR * 1.6, trunkR * 0.6, height * 0.92, detail > 1 ? 6 : 4)};
    std::vector<Geo> leaves;
    const int limbs = detail > 1 ? 7 : 5;
    for (int i = 0; i < limbs; i++) {
        const double t = static_cast<double>(i) / limbs;
        const double y = forkY + (height * 0.88 - forkY) * t;
        const double a = static_cast<double>(i) / limbs * PI * 2 + rng() * 0.8;
        const double len = height * (0.34 - t * 0.16) * (0.75 + rng() * 0.5);
        Geo limb = stem(trunkR * 0.45, trunkR * 0.16, len, 3);
        limb.rotate_z(PI / 2 - (0.5 + rng() * 0.5));
        limb.rotate_y(a);
        limb.translate(0, y, 0);
        bark.push_back(limb);

        Geo cluster = blob(len * 0.62, detail > 1 ? 1 : 0, rng, 0.42, 2.0, {1, 0.72, 1});
        const double lift = std::cos(0.5 + rng() * 0.3) * len * 0.62;
        cluster.translate(std::cos(a) * len * 0.62, y + lift * 0.4, std::sin(a) * len * 0.62);
        leaves.push_back(cluster);
    }
    Geo crown = blob(height * 0.26, detail > 1 ? 1 : 0, rng, 0.4, 1.8, {1, 0.78, 1});
    crown.translate(0, height * 0.82, 0);
    leaves.push_back(crown);

    FloraMesh m = assemble({
        {bark, [](const Vec3& p, const Vec3&) {
             const double n = fbm3(p.x * 6 + 11, p.y * 26, p.z * 6 + 3, 2);
             return (n > 0.54 ? BARK_BIRCH_DARK : BARK_BIRCH) * (0.78 + n * 0.34);
         }},
        {leaves, [](const Vec3& p, const Vec3& n) {
             return LEAF.lerp(LEAF_PALE, std::max(0.0, n.y) * 0.55 + fbm3(p.x * 3, p.y * 3, p.z * 3, 2) * 0.35);
         }},
    });
    const double kx = (rng() - 0.5) * 0.08;
    const double kz = (rng() - 0.5) * 0.08;
    bend(m, kx, kz, height);
    return m;
}

FloraMesh deadfall(Mulberry32& rng, int detail) {
    const double len = 3.5 + rng() * 5;
    const double r = 0.16 + rng() * 0.16;
    Geo log = cylinder(r * 0.72, r, len, detail > 1 ? 7 : 5, false);
    log.rotate_z(PI / 2);
    log.translate(0, r * 0.85, 0);
    std::vector<Geo> parts{log};
    if (detail > 1) {
        const int stubs = 2 + static_cast<int>(std::floor(rng() * 3));
        for (int i = 0; i < stubs; i++) {
            Geo s = cylinder(r * 0.1, r * 0.22, 0.3 + rng() * 0.5, 4, false);
            s.rotate_z((rng() - 0.5) * 1.2);
            s.rotate_x((rng() - 0.5) * 2.2);
            s.translate((rng() - 0.5) * len * 0.8, r * 1.1, 0);
            parts.push_back(s);
        }
    }
    return assemble({{parts, [=](const Vec3& p, const Vec3&) {
                          const double n = fbm3(p.x * 2.2, p.y * 5, p.z * 2.2, 2);
                          Color c = DEAD * (0.58 + n * 0.55);
                          if (p.y > r * 1.1) c = c.lerp(NEEDLE, std::min(0.55, (n - 0.35) * 1.4));
                          return c;
                      }}});
}

FloraMesh boulder(Mulberry32& rng, int detail) {
    const double r = 0.6 + rng() * 2.2;
    const double squashY = 0.66 + rng() * 0.3;
    Geo g = blob(r, detail > 1 ? 2 : 1, rng, 0.30, 1.5, {1, squashY, 1}, 0.55);
    g.translate(0, r * 0.5, 0);
    return assemble({{{g}, [](const Vec3& p, const Vec3& n) {
                          const double k = fbm3(p.x * 1.4, p.y * 1.4, p.z * 1.4, 3);
                          Color c = STONE * (0.52 + k * 0.72);
                          if (n.y > 0.45) c = c.lerp(LICHEN, std::max(0.0, k - 0.42) * 1.5);
                          return c;
                      }}});
}

FloraMesh fern(Mulberry32& rng) {
    const int fronds = 5 + static_cast<int>(std::floor(rng() * 4));
    std::vector<Geo> geoms;
    for (int i = 0; i < fronds; i++) {
        const double a = static_cast<double>(i) / fronds * PI * 2 + rng() * 0.6;
        const double len = 0.45 + rng() * 0.4;
        const double arc = 0.55 + rng() * 0.35;
        std::vector<Vec3> pts;
        std::vector<double> widths;
        for (int s = 0; s <= 5; s++) {
            const double t = s / 5.0;
            pts.push_back({std::cos(a) * len * t, len * (std::sin(t * arc * PI) * 0.62 + t * 0.15), std::sin(a) * len * t});
            widths.push_back(0.055 * len * (1 - t * 0.85) * (t < 0.12 ? t / 0.12 : 1));
        }
        geoms.push_back(ribbon(pts, widths, {-std::sin(a), 0, std::cos(a)}));
    }
    return assemble({{geoms, [](const Vec3& p, const Vec3&) { return FERN * (0.72 + p.y * 0.9); }}});
}

FloraMesh grass(Mulberry32& rng) {
    const int blades = 7 + static_cast<int>(std::floor(rng() * 6));
    std::vector<Geo> geoms;
    for (int i = 0; i < blades; i++) {
        const double a = rng() * PI * 2;
        const double h = 0.22 + rng() * 0.3;
        const double flop = 0.25 + rng() * 0.5;
        std::vector<Vec3> pts;
        std::vector<double> widths;
        for (int s = 0; s <= 3; s++) {
            const double t = s / 3.0;
            pts.push_back({std::cos(a) * h * flop * t * t, h * t * (1 - t * flop * 0.4), std::sin(a) * h * flop * t * t});
            widths.push_back(0.014 * (1 - t * 0.9));
        }
        geoms.push_back(ribbon(pts, widths, {-std::sin(a), 0, std::cos(a)}));
    }
    return assemble({{geoms, [](const Vec3& p, const Vec3&) { return GRASS.lerp(GRASS_DRY, std::min(1.0, p.y * 1.8)); }}});
}

FloraMesh shrub(Mulberry32& rng, int detail) {
    const double r = 0.4 + rng() * 0.6;
    const int lobes = detail > 1 ? 3 + static_cast<int>(std::floor(rng() * 3)) : 2;
    std::vector<Geo> geoms;
    for (int i = 0; i < lobes; i++) {
        const double lobeR = r * (0.5 + rng() * 0.5);
        Geo b = blob(lobeR, detail > 1 ? 1 : 0, rng, 0.45, 2.2);
        const double tx = (rng() - 0.5) * r;
        const double ty = r * (0.45 + rng() * 0.5);
        const double tz = (rng() - 0.5) * r;
        b.translate(tx, ty, tz);
        geoms.push_back(b);
    }
    return assemble({{geoms, [](const Vec3& p, const Vec3& n) {
                          return LEAF.lerp(LEAF_PALE, std::max(0.0, n.y) * 0.4 + fbm3(p.x * 4, p.y * 4, p.z * 4, 2) * 0.4) * 0.8;
                      }}});
}

FloraMesh snag(Mulberry32& rng, int detail) {
    const double height = 3.5 + rng() * 6;
    const double r = height * 0.018;
    std::vector<Geo> parts{stem(r * 1.7, r * 0.5, height, detail > 1 ? 5 : 4)};
    if (detail > 1) {
        for (int i = 0; i < 3; i++) {
            const double len = height * (0.14 + rng() * 0.12);
            Geo b = stem(r * 0.4, r * 0.1, len, 3);
            b.rotate_z(PI / 2 - (0.3 + rng() * 0.7));
            b.rotate_y(rng() * PI * 2);
            b.translate(0, height * (0.45 + rng() * 0.45), 0);
            parts.push_back(b);
        }
    }
    FloraMesh m = assemble({{parts, [](const Vec3& p, const Vec3&) {
                                 return DEAD * (0.55 + fbm3(p.x * 5, p.y * 3, p.z * 5, 2) * 0.6);
                             }}});
    const double kx = (rng() - 0.5) * 0.1;
    const double kz = (rng() - 0.5) * 0.1;
    bend(m, kx, kz, height);
    return m;
}

/// The reference instanced a detail only when it had both variants and pool capacity.
constexpr int kCaps[SPECIES_COUNT][3] = {
    {7000, 3400, 260}, {3200, 1600, 140}, {700, 340, 60}, {420, 260, 60},
    {2200, 1100, 180}, {0, 0, 460},       {0, 0, 1100},   {0, 0, 1900},
};

}  // namespace

FloraMesh build_flora(Species species, int detail, int variant, int32_t seed, FloraStyle style) {
    const int nameLen = static_cast<int>(std::strlen(kSpecies[species].name));
    // mulberry32(seed * 7919 + hash2(v, d, key.length) * 1e6): a non-integer double, ToUint32.
    const double s = static_cast<double>(seed) * 7919.0 + hash2(variant, detail, nameLen) * 1e6;
    Mulberry32 rng(static_cast<uint32_t>(static_cast<int64_t>(std::trunc(s)) & 0xFFFFFFFF));
    switch (species) {
        case SPECIES_PINE: return pine(rng, detail, style);
        case SPECIES_BIRCH: return birch(rng, detail, style);
        case SPECIES_SNAG: return snag(rng, detail);
        case SPECIES_DEADFALL: return deadfall(rng, detail);
        case SPECIES_BOULDER: return boulder(rng, detail);
        case SPECIES_SHRUB: return shrub(rng, detail);
        case SPECIES_FERN: return fern(rng);
        case SPECIES_GRASS: return grass(rng);
        default: return {};
    }
}

std::vector<FloraPrototype> build_all_flora(int32_t seed, FloraStyle style) {
    std::vector<FloraPrototype> out;
    for (int sp = 0; sp < SPECIES_COUNT; sp++) {
        const auto species = static_cast<Species>(sp);
        for (int d = 0; d <= 2; d++) {
            const int n = kSpecies[species].variants[static_cast<size_t>(d)];
            if (n == 0 || kCaps[sp][d] == 0) continue;
            const bool solid = species == SPECIES_BOULDER || species == SPECIES_DEADFALL || species == SPECIES_SNAG;
            for (int v = 0; v < n; v++) out.push_back({species, d, v, !solid, build_flora(species, d, v, seed, style)});
        }
    }
    return out;
}

/* ----------------------------------------------------------------- bake -- */

namespace {

void put_u32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void put_floats(std::vector<uint8_t>& b, const std::vector<float>& f) {
    for (float x : f) {
        uint32_t bits;
        std::memcpy(&bits, &x, 4);
        put_u32(b, bits);
    }
}

}  // namespace

std::vector<uint8_t> serialize_flora(const std::vector<FloraPrototype>& set) {
    std::vector<uint8_t> b;
    const char magic[8] = {'R', 'L', 'F', 'L', 'O', 'R', 'A', '1'};
    b.insert(b.end(), magic, magic + 8);
    put_u32(b, static_cast<uint32_t>(set.size()));
    for (const auto& p : set) {
        put_u32(b, p.species);
        put_u32(b, static_cast<uint32_t>(p.detail));
        put_u32(b, static_cast<uint32_t>(p.variant));
        put_u32(b, p.doubleSided ? 1 : 0);
        put_u32(b, static_cast<uint32_t>(p.mesh.positions.size() / 3));
        put_floats(b, p.mesh.positions);
        put_floats(b, p.mesh.normals);
        put_floats(b, p.mesh.colors);
    }
    return b;
}

bool deserialize_flora(const std::vector<uint8_t>& bytes, std::vector<FloraPrototype>& out, std::string* error) {
    size_t at = 0;
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    auto u32 = [&](uint32_t& v) {
        if (at + 4 > bytes.size()) return false;
        v = 0;
        for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(bytes[at + i]) << (8 * i);
        at += 4;
        return true;
    };
    auto floats = [&](std::vector<float>& f, size_t n) {
        if (at + n * 4 > bytes.size()) return false;
        f.resize(n);
        for (size_t i = 0; i < n; i++) {
            uint32_t bits = 0;
            for (int k = 0; k < 4; k++) bits |= static_cast<uint32_t>(bytes[at + k]) << (8 * k);
            std::memcpy(&f[i], &bits, 4);
            at += 4;
        }
        return true;
    };
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RLFLORA1", 8) != 0) return fail("not a RLFLORA1 file");
    at = 8;
    uint32_t count = 0;
    if (!u32(count)) return fail("truncated header");
    out.clear();
    for (uint32_t i = 0; i < count; i++) {
        uint32_t sp, d, v, ds, verts;
        if (!u32(sp) || !u32(d) || !u32(v) || !u32(ds) || !u32(verts)) return fail("truncated prototype header");
        if (sp >= SPECIES_COUNT || d > 2) return fail("bad species or detail");
        FloraPrototype p{static_cast<Species>(sp), static_cast<int>(d), static_cast<int>(v), ds != 0, {}};
        if (!floats(p.mesh.positions, verts * 3) || !floats(p.mesh.normals, verts * 3) || !floats(p.mesh.colors, verts * 3))
            return fail("truncated vertex data");
        out.push_back(std::move(p));
    }
    return true;
}

}  // namespace worldcore
