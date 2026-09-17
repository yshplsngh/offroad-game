// vehicle_mesh.cpp - see vehicle_mesh.hpp. Part dimensions, placements and the
// reasoning behind them follow the reference builders; comments here only
// note where the port had to differ.
#include "worldcore/vehicle_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace worldcore {

namespace {

constexpr double PI = std::numbers::pi;
constexpr double TAU = 2 * std::numbers::pi;
constexpr double HALF_PI = std::numbers::pi / 2;
constexpr double PANEL = 0.022;
constexpr double GAP = 0.007;

/* ------------------------------------------------------------ palette -- */

float srgb(double c) {
    return static_cast<float>(c < 0.04045 ? c * 0.0773993808 : std::pow(c * 0.9478672986 + 0.0521327014, 2.4));
}

Look look(uint32_t hex, float roughness, float metallic, float clearcoat = 0, uint8_t layer = 0, float emission = 0) {
    Look l;
    l.albedo = {srgb(((hex >> 16) & 255) / 255.0), srgb(((hex >> 8) & 255) / 255.0), srgb((hex & 255) / 255.0)};
    l.roughness = roughness;
    l.metallic = metallic;
    l.clearcoat = clearcoat;
    l.layer = layer;
    l.emission = emission;
    return l;
}

const Look RUBBER = look(0x101114, 0.95f, 0.0f);   // carcass and tread voids: deep, dark rubber
const Look TREAD = look(0x2c2d31, 0.88f, 0.0f);    // block faces, scuffed lighter by the ground
const Look RAW_STEEL = look(0x9aa0a8, 0.42f, 0.9f);
const Look BLACK_STEEL = look(0x2a2c30, 0.48f, 0.8f);
const Look CAST_IRON = look(0x4e5258, 0.74f, 0.75f);
const Look ZINC = look(0xc8ccd2, 0.28f, 0.95f);
const Look CHROME = look(0xf2f4f8, 0.08f, 1.0f);
const Look TRIM = look(0x232529, 0.86f, 0.0f);
const Look BEDLINER = look(0x1b1d21, 0.95f, 0.05f);
const Look DIAMOND = look(0x8d939b, 0.45f, 0.9f);
const Look GRILLE = look(0x17191c, 0.55f, 0.8f);
const Look SEAT = look(0x2e2a26, 0.95f, 0.0f);
const Look DASH = look(0x1e2024, 0.9f, 0.0f);
const Look REFLECTOR = look(0xffffff, 0.14f, 1.0f);
Look powder(uint32_t hex) { return look(hex, 0.58f, 0.35f); }
Look lens(uint32_t hex, int kind) { return look(hex, 0.12f, 0.0f, 0.0f, 3, static_cast<float>(kind)); }

/* ------------------------------------------------------------ helpers -- */

/// Object3D placement: scale, then Euler XYZ rotation (Z, then Y, then X), then translate.
Mesh place(Mesh m, const Vec3& pos, const Vec3& rot = {}, const Vec3& scale = {1, 1, 1}) {
    if (scale.x != 1 || scale.y != 1 || scale.z != 1) m.scale(scale.x, scale.y, scale.z);
    if (rot.z != 0) m.rotate_z(rot.z);
    if (rot.y != 0) m.rotate_y(rot.y);
    if (rot.x != 0) m.rotate_x(rot.x);
    m.translate(pos.x, pos.y, pos.z);
    return m;
}

/// A group of parts that can be placed as a whole (the reference's THREE.Group).
struct Group {
    PaintedMesh mesh;
    void add(const Mesh& m, const Look& l) { mesh.add(m, l); }
    void add(const Mesh& m, const Look& l, const Vec3& pos, const Vec3& rot = {}) { mesh.add(place(m, pos, rot), l); }
    void add(const Group& g) { mesh.add(g.mesh); }
};

PaintedMesh transformed(PaintedMesh p, const Vec3& pos, const Vec3& rot = {}, const Vec3& scale = {1, 1, 1}) {
    Mesh m{p.positions, p.normals, p.indices};
    m = place(std::move(m), pos, rot, scale);
    p.positions = std::move(m.positions);
    p.normals = std::move(m.normals);
    p.indices = std::move(m.indices);
    return p;
}

/// The reference's mirrorX(): the same part at -x with Y and Z rotations negated.
void mirror_x(Group& g, const Mesh& m, const Look& l, const Vec3& pos, const Vec3& rot = {}) {
    g.add(m, l, pos, rot);
    g.add(m, l, {-pos.x, pos.y, pos.z}, {rot.x, -rot.y, -rot.z});
}

std::vector<Vec2> arc(double cx, double cy, double r, double a0, double a1, int steps) {
    return arc_points(cx, cy, r, a0, a1, steps);
}

/// Wheel arch outline along the sill, centred on the AXLE.
std::vector<Vec2> arch_outline(double axleZ, double axleY, double archR, double sillY, int steps) {
    const double dy = sillY - axleY;
    if (dy >= archR) return {{axleZ, sillY}};
    const double dz = std::sqrt(archR * archR - dy * dy);
    const double a0 = std::atan2(dy, -dz);  // rear intersection
    const double a1 = std::atan2(dy, dz);   // front intersection: sweep up and over the top
    return arc(axleZ, axleY, archR, a0, a1, steps);
}

std::vector<Vec2> rect_hole(double z0, double z1, double y0, double y1, double r) {
    const double w = z1 - z0, h = y1 - y0;
    auto pts = rounded_rect(w, h, std::min({r, w / 2, h / 2}), 3);
    for (auto& p : pts) {
        p.x += z0 + w / 2;
        p.y += y0 + h / 2;
    }
    return pts;
}

/// Extrude a (z, y) outline into a panel of thickness `t` lying in the YZ plane,
/// thickness toward -X. (The reference's rotateY(+90deg) mirrored z; see header.)
Mesh side_panel(const std::vector<Vec2>& outline, const std::vector<std::vector<Vec2>>& holes, double t = PANEL,
                double bevel = 0.005) {
    Mesh m = extrude(outline, holes, std::max(1e-4, t - bevel * 2), bevel);
    m.rotate_y(-HALF_PI);  // shape x -> +z, extrusion -> -x
    return m;
}

Mesh plate(const std::vector<Vec2>& pts, double depth, double bevel = 0.006) {
    Mesh m = extrude(pts, {}, std::max(1e-4, depth - bevel * 2), bevel);
    m.translate(0, 0, -(depth - bevel * 2) / 2);
    return m;
}

Mesh hex_bolt(double radius = 0.008, double height = 0.006) {
    Mesh g = cylinder(radius, radius * 0.94, height, 6);
    g.translate(0, height / 2, 0);
    Mesh w = cylinder(radius * 1.55, radius * 1.55, height * 0.28, 10);
    w.translate(0, height * 0.14, 0);
    g.append(w);
    g.rotate_x(-HALF_PI);
    return g;
}

/* ---------------------------------------------------------------- body -- */

Group headlamp(double radius) {
    Group g;
    Mesh bowl = lathe({{0, -0.09}, {radius * 0.45, -0.07}, {radius * 0.85, -0.02}, {radius, 0.01}}, 20);
    bowl.rotate_x(HALF_PI);
    g.add(bowl, REFLECTOR);
    Mesh ln = lathe({{0, 0.03}, {radius * 0.7, 0.025}, {radius, 0.005}, {radius, -0.005}}, 20);
    ln.rotate_x(HALF_PI);
    g.add(ln, lens(0xfff2dc, LAMP_HEAD));
    g.add(torus(radius * 1.03, 0.012, 8, 24), CHROME);
    return g;
}

Group lamp(double w, double h, uint32_t color, int kind) {
    Group g;
    g.add(rounded_box(w, h, 0.05, 0.018, 0.005, 2), lens(color, kind));
    g.add(rounded_box(w * 1.14, h * 1.12, 0.03, 0.02, 0.005, 1), TRIM, {0, 0, -0.03});
    return g;
}

Group flare(const BodyMetrics& m, int side, double axleZ) {
    const double r = m.archR + 0.03;
    const double dy = m.sillY - m.archY;
    const double span = dy >= r ? PI : PI - 2 * std::asin(std::clamp(dy / r, -1.0, 1.0));
    Mesh t = torus(r, 0.05, 8, 26, span);
    t.rotate_z((PI - span) / 2);
    t.rotate_y(HALF_PI);
    t.scale(1.5, 1, 1);
    Group g;
    g.add(t, TRIM, {side * (m.halfW - 0.01), m.archY, axleZ});
    return g;
}

Group greenhouse(const VehicleDesign& d, const BodyMetrics& m, const Look& paint, int detail) {
    Group g;
    const double wsTop = m.zCowl - m.rake;
    const double roofRear = m.zCabRear;
    const double pillar = 0.05;
    Look gl = look(0x141c24, 0.055f, 0.0f, 1.0f, 2);
    gl.emission = static_cast<float>(detail > 1 ? 0.38 + d.body.glassTint * 0.5 : 0.62);  // glass opacity

    for (int s : {-1, 1}) {
        g.add(tube({{s * (m.halfW - pillar / 2), m.beltY - 0.02, m.zCowl - 0.06}, {s * (m.halfW - pillar / 2), m.roofY, wsTop}},
                   pillar / 2, 8),
              paint);
    }
    g.add(rounded_box(d.body.width - pillar, pillar, pillar, 0.018, 0.005, 2), paint, {0, m.roofY, wsTop});
    g.add(rounded_box(d.body.width - pillar, pillar * 0.8, pillar, 0.018, 0.005, 2), paint, {0, m.beltY - 0.02, m.zCowl - 0.06});

    const double wsH = std::hypot(m.roofY - m.beltY, m.rake - 0.06);
    g.add(quad(d.body.width - pillar * 1.6, wsH), gl, {0, (m.beltY + m.roofY) / 2 - 0.01, (m.zCowl - 0.06 + wsTop) / 2},
          {-std::atan2(m.rake - 0.06, m.roofY - m.beltY), 0, 0});

    const double roofLen = std::fabs(wsTop - roofRear);
    g.add(rounded_box(d.body.width - 0.02, 0.035, roofLen, 0.06, 0.01, 3), paint, {0, m.roofY + 0.012, (wsTop + roofRear) / 2});
    for (int s : {-1, 1}) {
        g.add(rounded_box(0.03, 0.028, roofLen, 0.01, 0.004, 1), paint, {s * (m.halfW - 0.005), m.roofY - 0.005, (wsTop + roofRear) / 2});
    }
    std::vector<double> pillarZs = d.body.doors == 4 ? std::vector<double>{wsTop - roofLen * 0.42, roofRear + 0.04}
                                                     : std::vector<double>{wsTop - roofLen * 0.5, roofRear + 0.04};
    for (int s : {-1, 1}) {
        for (double z : pillarZs) {
            g.add(rounded_box(pillar, m.roofY - m.beltY, pillar, 0.015, 0.004, 1), paint, {s * (m.halfW - pillar / 2), (m.beltY + m.roofY) / 2, z});
        }
        std::vector<double> bounds{wsTop - 0.05};
        bounds.insert(bounds.end(), pillarZs.begin(), pillarZs.end());
        for (size_t i = 0; i + 1 < bounds.size(); i++) {
            const double z0 = bounds[i], z1 = bounds[i + 1];
            g.add(quad(std::fabs(z1 - z0) - pillar, m.roofY - m.beltY - 0.04), gl,
                  {s * (m.halfW - 0.012), (m.beltY + m.roofY) / 2, (z0 + z1) / 2}, {0, s * HALF_PI, 0});
        }
    }
    g.add(quad(d.body.width - pillar * 1.8, m.roofY - m.beltY - 0.04), gl, {0, (m.beltY + m.roofY) / 2, roofRear + 0.02});
    g.add(rounded_box(d.body.width - 0.02, pillar * 0.8, pillar, 0.015, 0.004, 1), paint, {0, m.roofY, roofRear + 0.02});
    return g;
}

Group bed(const VehicleDesign& d, const BodyMetrics& m, const Look& paint, int detail) {
    Group g;
    const double z0 = m.zRear, z1 = m.zCabRear - 0.04;
    const double len = std::fabs(z1 - z0);
    const double sideH = m.beltY - m.sillY - 0.06;
    std::vector<Vec2> outline{{z0, m.sillY}};
    auto arch = arch_outline(m.rearAxleZ, m.archY, m.archR, m.sillY, 14);
    outline.insert(outline.end(), arch.begin(), arch.end());
    outline.push_back({z1, m.sillY});
    outline.push_back({z1, m.sillY + sideH});
    outline.push_back({z0, m.sillY + sideH});
    Mesh panel = side_panel(outline, {});
    g.add(panel, paint, {m.halfW, 0, 0});
    g.add(panel, paint, {-m.halfW + PANEL, 0, 0});

    g.add(rounded_box(d.body.width - PANEL * 2, 0.03, len, 0.01, 0.004, 1), BEDLINER, {0, m.sillY + 0.12, (z0 + z1) / 2});
    g.add(rounded_box(d.body.width - 0.04, sideH, PANEL, 0.025, 0.006, 2), paint, {0, m.sillY + sideH / 2, z0 + PANEL / 2});
    if (d.features.flares) {
        for (int s : {-1, 1}) g.add(flare(m, s, m.rearAxleZ));
    }
    if (detail > 1) {
        for (int s : {-1, 1}) {
            g.add(rounded_box(0.05, 0.03, len, 0.012, 0.004, 1), TRIM, {s * (m.halfW - 0.02), m.sillY + sideH + 0.015, (z0 + z1) / 2});
            for (double t : {0.25, 0.75}) {
                g.add(torus(0.022, 0.006, 6, 12), ZINC, {s * (m.halfW - 0.05), m.sillY + sideH - 0.06, z0 + len * t}, {0, HALF_PI, 0});
            }
        }
    }
    return g;
}

Group body(const VehicleDesign& d, const BodyMetrics& m, int detail) {
    Group g;
    Look paint = look(0xffffff, d.body.matte ? 0.72f : 0.3f, d.body.matte ? 0.05f : 0.2f, d.body.matte ? 0.1f : 1.0f, 1);
    const bool pickup = d.body.pickup;
    const double bodyRear = pickup ? m.zCabRear : m.zRear;

    // Side panels with real door cut-outs.
    const int doorCount = d.body.doors;
    constexpr double CLEAR = 0.06;
    const double doorZ0 = std::min(m.zCowl - 0.14, m.frontAxleZ - m.archHalf - CLEAR);
    const double doorZMin = pickup ? bodyRear + 0.14 : std::max(bodyRear + 0.16, m.rearAxleZ + m.archHalf + CLEAR);
    const double doorSpan = std::max(0.5, doorZ0 - doorZMin);
    const double doorLen = doorCount == 4 ? doorSpan / 2 - GAP * 2 : std::min(doorSpan, 1.18);
    const double doorY0 = m.sillY + 0.2, doorY1 = m.beltY - 0.012;

    std::vector<std::vector<Vec2>> openings;
    std::vector<std::pair<double, double>> doorRects;
    for (int i = 0; i < doorCount / 2; i++) {
        const double z1 = doorZ0 - i * (doorLen + GAP * 2);
        const double z0 = z1 - doorLen;
        openings.push_back(rect_hole(z0, z1, doorY0, doorY1, 0.05));
        doorRects.push_back({z0, z1});
    }

    // Tub outline (z, y): rear sill, rear arch, front arch, up the fender, cowl, belt.
    std::vector<Vec2> tub{{bodyRear, m.sillY}};
    if (!pickup) {
        auto a = arch_outline(m.rearAxleZ, m.archY, m.archR, m.sillY, 16);
        tub.insert(tub.end(), a.begin(), a.end());
    }
    auto fa = arch_outline(m.frontAxleZ, m.archY, m.archR, m.sillY, 16);
    tub.insert(tub.end(), fa.begin(), fa.end());
    tub.push_back({m.zFront, m.sillY});
    tub.push_back({m.zFront, m.hoodY});
    tub.push_back({m.zCowl, m.hoodY});
    tub.push_back({m.zCowl - 0.06, m.beltY});
    tub.push_back({bodyRear, m.beltY});
    Mesh panel = side_panel(tub, openings);
    // Thickness runs toward -X from the placement, so the right side sits flush at halfW.
    g.add(panel, paint, {m.halfW, 0, 0});
    g.add(panel, paint, {-m.halfW + PANEL, 0, 0});

    // Doors sitting in the openings, with handles and lock barrels.
    for (const auto& [z0, z1] : doorRects) {
        const double w = z1 - z0 - GAP * 2, h = doorY1 - doorY0 - GAP * 2;
        Group door;
        Mesh skin = extrude(rounded_rect(w, h, 0.045, 3), {}, PANEL - 0.012, 0.006);
        skin.rotate_y(-HALF_PI);
        door.add(skin, paint, {PANEL / 2, (doorY0 + doorY1) / 2, (z0 + z1) / 2});
        door.add(rounded_box(0.03, 0.035, 0.14, 0.012, 0.004, 1), TRIM, {0.026, doorY1 - 0.13, (z0 + z1) / 2 - w * 0.22});
        if (detail > 1) {
            door.add(cylinder(0.011, 0.011, 0.012, 10), CHROME, {0.026, doorY1 - 0.13, (z0 + z1) / 2 - w * 0.36}, {0, 0, HALF_PI});
        }
        for (int s : {-1, 1}) {
            g.mesh.add(transformed(door.mesh, {s * (m.halfW - PANEL / 2), 0, 0}, {}, {static_cast<double>(s), 1, 1}));
        }
    }

    // Floor, firewall, rear.
    g.add(rounded_box(d.body.width - PANEL * 2, 0.03, std::fabs(bodyRear - m.zCowl) + 0.5, 0.02, 0.005, 1), BEDLINER,
          {0, m.sillY + 0.02, (bodyRear + m.zCowl) / 2 - 0.1});
    g.add(rounded_box(d.body.width - PANEL * 2, m.beltY - m.sillY, PANEL, 0.02, 0.005, 1), paint, {0, (m.sillY + m.beltY) / 2, m.zCowl + 0.02});
    if (!pickup) {
        g.add(rounded_box(d.body.width - 0.06, m.beltY - m.sillY - 0.04, PANEL, 0.03, 0.006, 2), paint, {0, (m.sillY + m.beltY) / 2, bodyRear + PANEL / 2});
        g.add(rounded_box(0.16, 0.05, 0.03, 0.012, 0.004, 1), TRIM, {0, m.beltY - 0.14, bodyRear - 0.01});
    } else {
        g.add(rounded_box(d.body.width - PANEL * 2, m.beltY - m.sillY, PANEL, 0.02, 0.005, 1), paint, {0, (m.sillY + m.beltY) / 2, bodyRear});
        g.add(bed(d, m, paint, detail));
    }

    // Hood, power bulge, latches.
    const double hoodLen = m.zFront - m.zCowl;
    g.add(rounded_box(d.body.width - 0.1, 0.035, hoodLen - GAP * 2, 0.03, 0.008, 2), paint, {0, m.hoodY + 0.012, (m.zFront + m.zCowl) / 2});
    if (detail > 1) {
        g.add(rounded_box(0.44, 0.045, hoodLen * 0.5, 0.05, 0.01, 3), paint, {0, m.hoodY + 0.042, (m.zFront + m.zCowl) / 2 + 0.05});
        for (int s : {-1, 1}) {
            g.add(rounded_box(0.05, 0.02, 0.07, 0.008, 0.003, 1), RAW_STEEL, {s * (d.body.width * 0.3), m.hoodY + 0.03, m.zFront - 0.06});
        }
    }
    // Upper front fenders: close the gap between the side panels and the hood.
    for (int s : {-1, 1}) {
        g.add(rounded_box(0.06, 0.03, hoodLen, 0.012, 0.004, 1), paint, {s * (m.halfW - 0.03), m.hoodY - 0.005, (m.zFront + m.zCowl) / 2});
    }

    // Front fascia, grille, headlamps and indicators.
    const double grilleH = m.hoodY - m.sillY - 0.1;
    g.add(rounded_box(d.body.width - 0.14, grilleH, 0.04, 0.03, 0.006, 2), TRIM, {0, m.sillY + 0.06 + grilleH / 2, m.zFront + 0.01});
    // Grille bars stand in for the reference's alpha-tested mesh texture.
    {
        const double gw = d.body.width - 0.26, gh = grilleH - 0.06;
        const int bars = detail > 1 ? 9 : 5;
        for (int i = 0; i < bars; i++) {
            const double x = -gw / 2 + gw * (i + 0.5) / bars;
            g.add(rounded_box(0.012, gh, 0.02, 0.004, 0.002, 1), GRILLE, {x, m.sillY + 0.06 + grilleH / 2, m.zFront + 0.035});
        }
        g.add(rounded_box(gw, gh, 0.01, 0.01, 0.0, 1), look(0x08090a, 0.9f, 0.0f), {0, m.sillY + 0.06 + grilleH / 2, m.zFront + 0.028});
    }
    const double lampY = m.sillY + 0.06 + grilleH * 0.62;
    for (int s : {-1, 1}) {
        g.mesh.add(transformed(headlamp(d.body.headlampR).mesh, {s * (d.body.width / 2 - 0.16), lampY, m.zFront + 0.03}));
        g.mesh.add(transformed(lamp(0.075, 0.06, 0xff8a1e, LAMP_INDICATOR).mesh, {s * (d.body.width / 2 - 0.16), lampY - 0.17, m.zFront + 0.02}));
    }
    const double tailZ = pickup ? m.zRear : bodyRear;
    for (int s : {-1, 1}) {
        g.mesh.add(transformed(lamp(0.09, 0.2, 0xd81f26, LAMP_TAIL).mesh, {s * (d.body.width / 2 - 0.12), m.sillY + 0.34, tailZ - 0.01}, {0, PI, 0}));
    }

    if (d.features.flares) {
        for (int s : {-1, 1}) {
            for (double z : {m.frontAxleZ, m.rearAxleZ}) {
                if (pickup && z == m.rearAxleZ) continue;
                g.add(flare(m, s, z));
            }
        }
    }
    g.add(greenhouse(d, m, paint, detail));

    for (int s : {-1, 1}) {
        Group mir;
        mir.add(rounded_box(0.03, 0.03, 0.16, 0.012, 0.004, 1), TRIM, {s * 0.06, 0, -0.05}, {0, 0, s * 0.5});
        mir.add(rounded_box(0.045, 0.16, 0.13, 0.025, 0.006, 2), TRIM);
        mir.add(quad(0.115, 0.14), CHROME, {s * 0.024, 0, 0}, {0, s * HALF_PI, 0});
        g.mesh.add(transformed(mir.mesh, {s * (m.halfW + 0.1), m.beltY + 0.1, m.zCowl - 0.16}));
    }
    return g;
}

/* ------------------------------------------------------------ chassis -- */

Group ladder_frame(const VehicleDesign& d) {
    Group g;
    const auto& f = d.frame;
    const Look steel = powder(0x1d1f23);
    const double zFront = f.wheelbase / 2 + f.frontOverhang;
    const double zRear = -(f.wheelbase / 2 + f.rearOverhang);
    const double kick = f.railHeight * 0.55;
    const std::vector<Vec2> profile{
        {zRear, f.railY + kick}, {zRear + f.rearOverhang * 0.55, f.railY + kick}, {-f.wheelbase / 2 + 0.12, f.railY},
        {f.wheelbase / 2 - 0.12, f.railY}, {zFront - f.frontOverhang * 0.55, f.railY + kick * 0.8}, {zFront, f.railY + kick * 0.8},
    };
    std::vector<Vec2> outline;
    for (auto it = profile.rbegin(); it != profile.rend(); ++it) outline.push_back({it->x, it->y + f.railHeight / 2});
    for (const auto& p : profile) outline.push_back({p.x, p.y - f.railHeight / 2});
    Mesh rail = extrude(outline, {}, 0.075 - 0.016, 0.008);
    rail.translate(0, 0, -(0.075 - 0.016) / 2);
    rail.rotate_y(-HALF_PI);
    mirror_x(g, rail, steel, {f.frameWidth / 2, 0, 0});

    struct Cross { double z, yOff, r; };
    for (const Cross& c : {Cross{zRear + 0.1, kick, 0.05}, Cross{-f.wheelbase / 2 + 0.05, 0.0, 0.045}, Cross{0, -0.01, 0.04},
                           Cross{f.wheelbase / 2 - 0.05, 0.0, 0.045}, Cross{zFront - 0.12, kick * 0.8, 0.05}}) {
        g.add(cylinder(c.r, c.r, f.frameWidth, 10), steel, {0, f.railY + c.yOff, c.z}, {0, 0, HALF_PI});
    }
    for (double z : {zRear + 0.25, -f.wheelbase * 0.2, f.wheelbase * 0.25, zFront - 0.3}) {
        for (int s : {-1, 1}) g.add(cylinder(0.032, 0.032, 0.05, 10), RUBBER, {s * f.frameWidth / 2, f.railY + f.railHeight / 2 + 0.02, z});
    }
    return g;
}

PaintedMesh solid_axle(const VehicleDesign& d, bool steering, int detail) {
    Group g;
    const double half = d.axle.track / 2;
    const double diffOffset = d.axle.diffOffset * (steering ? -1 : 1);
    for (int s : {-1, 1}) {
        g.add(cylinder(d.axle.tubeR, d.axle.tubeR, half - 0.09, detail > 1 ? 16 : 10), CAST_IRON,
              {s * (half - 0.09) / 2 + s * 0.045, 0, 0}, {0, 0, HALF_PI});
    }
    Mesh pump = sphere(0.135, detail > 1 ? 20 : 12, detail > 1 ? 16 : 10);
    pump.scale(1.0, 1.05, 0.82);
    g.add(pump, CAST_IRON, {diffOffset, 0, 0});
    Mesh cover = lathe({{0, -0.1}, {0.11, -0.115}, {0.125, -0.06}, {0.125, 0.0}, {0, 0.0}}, detail > 1 ? 20 : 12);
    cover.rotate_x(-HALF_PI);
    cover.rotate_y(PI);
    g.add(cover, CAST_IRON, {diffOffset, 0, -0.1});
    g.add(cylinder(0.05, 0.062, 0.16, 12), CAST_IRON, {diffOffset, 0.01, 0.14}, {HALF_PI, 0, 0});
    for (int s : {-1, 1}) {
        g.add(rounded_box(0.1, 0.16, 0.16, 0.03, 0.006, 2), BLACK_STEEL, {s * (half - 0.05), 0, 0});
        g.add(cylinder(0.075, 0.075, 0.03, 12), BLACK_STEEL, {s * (half - 0.005), 0, 0}, {0, 0, HALF_PI});
    }
    if (steering) {
        g.add(cylinder(0.018, 0.018, d.axle.track - 0.16, 8), ZINC, {0, 0.02, -0.14}, {0, 0, HALF_PI});
        g.add(rod({half - 0.08, 0.02, -0.14}, {diffOffset - 0.1, 0.09, -0.2}, 0.016), ZINC);
    }
    if (detail > 1) {
        Mesh guard = lathe({{0.12, -0.02}, {0.145, 0.0}, {0.145, 0.04}, {0.12, 0.05}}, 16);
        guard.rotate_x(-HALF_PI);
        g.add(guard, RAW_STEEL, {diffOffset, 0, -0.06});
    }
    return g.mesh;
}

Group drivetrain(const VehicleDesign& d, int detail) {
    Group g;
    const auto& f = d.frame;
    const double ez = f.wheelbase * 0.28;
    g.add(rounded_box(0.44, 0.4, 0.5, 0.04, 0.008, 2), CAST_IRON, {0, f.railY + 0.26, ez});
    g.add(rounded_box(0.4, 0.09, 0.44, 0.03, 0.006, 2), powder(0x2b2f35), {0, f.railY + 0.5, ez});
    g.add(rounded_box(0.3, 0.1, 0.24, 0.04, 0.006, 2), powder(0x1a1c20), {0, f.railY + 0.59, ez + 0.02});
    g.add(lathe({{0.14, 0}, {0.14, 0.2}, {0.1, 0.42}, {0.1, 0.6}, {0, 0.6}}, 14), CAST_IRON, {0, f.railY + 0.16, ez - 0.25}, {-HALF_PI, 0, 0});
    g.add(rounded_box(0.26, 0.28, 0.26, 0.05, 0.008, 2), CAST_IRON, {-0.04, f.railY + 0.1, ez - 0.62});
    g.add(rod({-0.04, f.railY + 0.08, ez - 0.7}, {d.axle.diffOffset * -1, f.railY - 0.16, f.wheelbase / 2 - 0.05}, 0.028), ZINC);
    g.add(rod({-0.04, f.railY + 0.08, ez - 0.7}, {d.axle.diffOffset * 0.6, f.railY - 0.16, -f.wheelbase / 2 + 0.1}, 0.028), ZINC);
    if (detail > 1) {
        g.add(tube({{0.22, f.railY + 0.18, ez - 0.1}, {0.3, f.railY - 0.02, f.wheelbase * 0.1},
                    {f.frameWidth / 2 + 0.03, f.railY - 0.06, -f.wheelbase * 0.2}, {f.frameWidth / 2 + 0.05, f.railY - 0.04, -f.wheelbase / 2 - 0.15}},
                   0.032, 10),
              RAW_STEEL);
        g.add(lathe({{0, 0}, {0.075, 0.02}, {0.075, 0.34}, {0, 0.36}}, 14), RAW_STEEL,
              {f.frameWidth / 2 + 0.03, f.railY - 0.06, -f.wheelbase * 0.12}, {-HALF_PI, 0, 0});
    }
    g.add(rounded_box(f.frameWidth * 0.82, 0.22, 0.5, 0.05, 0.008, 2), BLACK_STEEL, {-0.03, f.railY - 0.06, -f.wheelbase * 0.36});
    return g;
}

Group armour(const VehicleDesign& d, int detail) {
    Group g;
    const auto& f = d.frame;
    struct Skid { double z, len, w; };
    for (const Skid& s : {Skid{f.wheelbase * 0.3, 0.72, f.frameWidth + 0.06}, Skid{f.wheelbase * 0.28 - 0.6, 0.55, f.frameWidth * 0.8}}) {
        Mesh skid = plate({{-s.w / 2, 0}, {-s.w / 2 + 0.05, -s.len * 0.5}, {s.w / 2 - 0.05, -s.len * 0.5}, {s.w / 2, 0}}, 0.014, 0.004);
        skid.rotate_x(-HALF_PI);
        g.add(skid, DIAMOND, {0, f.railY - 0.12, s.z + s.len * 0.25});
    }
    if (d.features.sliders) {
        const double y = f.railY - 0.1;
        const double z0 = f.wheelbase / 2 - 0.35, z1 = -f.wheelbase / 2 + 0.3;
        for (int s : {-1, 1}) {
            const double x = f.frameWidth / 2 + 0.13;
            g.add(tube({{s * (f.frameWidth / 2 - 0.02), y + 0.03, z0 + 0.22}, {s * x, y, z0}, {s * x, y, z1},
                        {s * (f.frameWidth / 2 - 0.02), y + 0.03, z1 - 0.2}},
                       0.036, 10),
                  powder(0x1b1d21));
            if (detail > 1) {
                Mesh step = plate({{-0.06, 0}, {0.06, 0}, {0.06, z0 - z1}, {-0.06, z0 - z1}}, 0.01, 0.003);
                step.rotate_x(HALF_PI);
                g.add(step, DIAMOND, {s * (x - 0.02), y + 0.035, z1});
            }
        }
    }
    return g;
}

/* ------------------------------------------------------------- wheels -- */

/// Tyre cross-section geometry shared by the carcass and the tread.
struct TireSection {
    double R;       // overall radius, to the tops of the tread blocks
    double hw;      // half section width
    double rimR;    // bead seat radius
    double depth;   // tread block height
    double Rc;      // carcass crown radius (tread blocks stand on this)
    double sr;      // shoulder radius: how round the tyre's corners are
    double bulge;   // sidewall bulge beyond the section width
};

TireSection tire_section(double R, double width, double rimR) {
    TireSection t;
    t.R = R;
    t.hw = width / 2;
    t.rimR = rimR;
    t.depth = R * 0.1;  // deep mud-terrain blocks (~4.5 cm on a 35" tyre)
    t.Rc = R - t.depth * 0.75;
    t.sr = std::min(t.hw * 0.55, (t.Rc - rimR) * 0.42);
    t.bulge = (t.Rc - rimR) * 0.09;
    return t;
}

/// Round-shouldered carcass: bead, bulging sidewall, a true circular shoulder,
/// slightly crowned tread face, and back. Revolved around the wheel axis (X).
Mesh tire_carcass(const TireSection& t, int seg) {
    std::vector<Vec2> half;  // from the bead to the crown centre, on the -axial side
    half.push_back({t.rimR, -t.hw * 0.86});
    half.push_back({t.rimR * 1.03, -t.hw * 0.97});
    const int sideSteps = 5;
    for (int k = 0; k <= sideSteps; k++) {
        const double u = static_cast<double>(k) / sideSteps;
        const double r = t.rimR * 1.08 + (t.Rc - t.sr - t.rimR * 1.08) * u;
        half.push_back({r, -t.hw - t.bulge * std::sin(PI * u)});
    }
    const int arcSteps = 7;
    for (int k = 1; k <= arcSteps; k++) {
        const double th = HALF_PI * k / arcSteps;
        half.push_back({t.Rc - t.sr + t.sr * std::sin(th), -t.hw + t.sr - t.sr * std::cos(th)});
    }
    std::vector<Vec2> p = half;
    p.push_back({t.Rc * 1.006, 0.0});  // slight crown
    for (auto it = half.rbegin(); it != half.rend(); ++it) p.push_back({it->x, -it->y});
    p.push_back(p.front());  // close the section through the bead seat
    Mesh g = lathe(p, seg);
    g.rotate_z(HALF_PI);
    return g;
}

/// Tread blocks. Frame before placement: X axial, Y radial, Z along the rolling direction.
Mesh tread(const TireSection& t, int lugCount, int detail) {
    Mesh parts;
    const double hw = t.hw, depth = t.depth;
    const double pitch = TAU * t.Rc / lugCount;  // circumferential spacing
    const double corner = detail > 1 ? 0.006 : 0.012;
    const double bevel = detail > 1 ? 0.004 : 0.003;

    // A block standing on the carcass surface at axial offset `ax`, with its
    // base sunk a little into the rubber, leaning outward by `lean` (radians,
    // follows the shoulder), yawed by `yaw`, at spin angle `a`.
    auto block = [&](double w, double h, double len, double ax, double radial, double lean, double yaw, double a) {
        Mesh g = rounded_box(w, h, len, std::min(corner, std::min(w, len) * 0.3), bevel, 1);
        g.translate(0, h * 0.5 - h * 0.18, 0);  // base 18% below the surface
        g.rotate_y(yaw);
        g.rotate_z(-lean);
        g.translate(ax, radial, 0);
        g.rotate_x(a);
        parts.append(g);
    };

    for (int i = 0; i < lugCount; i++) {
        const double a = static_cast<double>(i) / lugCount * TAU;
        const double sgn = i % 2 == 0 ? 1.0 : -1.0;  // alternate the stagger
        const bool big = i % 2 == 0;

        // Centre row: two staggered, angled blocks with a deep void between them.
        const double cw = hw * 0.62, cl = pitch * 0.46;
        block(cw, depth, cl, sgn * hw * 0.3, t.Rc * 1.004, 0.0, sgn * 0.32, a);
        block(cw * 0.8, depth * 0.96, cl * 0.8, -sgn * hw * 0.28, t.Rc * 1.004, 0.0, -sgn * 0.22, a + TAU / lugCount * 0.5);

        // Shoulder blocks sit ON the rounded corner and follow its curve, so the
        // tyre reads as round and the bite wraps from the tread onto the sidewall.
        for (int side : {-1, 1}) {
            const double cx = side * (hw - t.sr), cr = t.Rc - t.sr;
            const double phi = big ? 0.62 : 0.5;  // angle around the shoulder from the crown
            const double ax = cx + side * t.sr * std::sin(phi), radial = cr + t.sr * std::cos(phi);
            const double sw = hw * 0.62, sl = pitch * (big ? 0.62 : 0.5);
            const double off = side > 0 ? TAU / lugCount * 0.5 : 0.0;
            block(sw, depth * 1.05, sl, ax, radial, side * phi, side * 0.08, a + off);

            // Wrap-around sidewall lug below the shoulder on every other pitch.
            if (big && detail > 0) {
                const double phi2 = 1.28;
                const double ax2 = cx + side * t.sr * std::sin(phi2), radial2 = cr + t.sr * std::cos(phi2);
                block(depth * 0.9, hw * 0.34, pitch * 0.42, ax2 + side * depth * 0.1, radial2 - hw * 0.1, side * phi2, 0.0, a + off);
            }
        }
    }
    return parts;
}

PaintedMesh wheel(const VehicleDesign& d, int side, int detail) {
    Group g;
    const double R = d.tire.diameter / 2;
    const double rimR = d.tire.rimInch * 0.0254 / 2;
    const int seg = detail > 1 ? 64 : detail > 0 ? 36 : 20;
    const int lugs = detail > 1 ? 22 : detail > 0 ? 16 : 12;
    const TireSection section = tire_section(R, d.tire.width, rimR);
    g.add(tire_carcass(section, seg), RUBBER);
    g.add(tread(section, lugs, detail), TREAD);  // even the far LOD keeps blocks: the silhouette is knobbly
    if (detail > 1) {
        for (int s : {-1, 1}) {
            Mesh ring = torus(section.rimR + (section.Rc - section.rimR) * 0.45, 0.005, 6, 48);
            ring.rotate_y(HALF_PI);
            g.add(ring, RUBBER, {s * (section.hw + section.bulge * 0.9), 0, 0});
        }
    }

    // Rim: barrel, hub boss, spokes, beadlock ring and bolts, lug nuts.
    const double width = d.tire.width * 0.88;
    const double hw = width / 2;
    const int rseg = detail > 1 ? 40 : 24;
    const double faceX = width * 0.28;
    const Look rim = powder(d.tire.rimColor);
    Mesh barrel = lathe({{rimR * 0.98, -hw}, {rimR, -hw * 0.94}, {rimR * 0.9, -hw * 0.88}, {rimR * 0.88, -hw * 0.2}, {rimR * 0.9, hw * 0.4},
                         {rimR * 0.9, hw * 0.88}, {rimR, hw * 0.94}, {rimR * 0.98, hw}, {rimR * 0.94, hw}, {rimR * 0.86, hw * 0.9},
                         {rimR * 0.84, -hw * 0.2}, {rimR * 0.86, -hw * 0.9}, {rimR * 0.94, -hw}, {rimR * 0.98, -hw}},
                        rseg);
    barrel.rotate_z(HALF_PI);
    g.add(barrel, rim);
    Mesh boss = lathe({{0, faceX - 0.012}, {0.055, faceX - 0.012}, {0.06, faceX - 0.004}, {0.062, faceX + 0.01}, {0.05, faceX + 0.012}, {0, faceX + 0.012}}, rseg);
    boss.rotate_z(HALF_PI);
    g.add(boss, rim);
    if (detail > 0) {
        for (int i = 0; i < d.tire.spokes; i++) {
            const double inner = 0.052, outer = rimR * 0.93, thick = 0.022, wIn = 0.062, wOut = 0.088;
            Mesh s = extrude({{-wIn / 2, 0}, {wIn / 2, 0}, {wOut / 2, outer - inner}, {-wOut / 2, outer - inner}}, {}, thick, 0.006);
            s.rotate_y(HALF_PI);
            s.translate(faceX - thick / 2, inner, 0);
            s.rotate_x(static_cast<double>(i) / d.tire.spokes * TAU);
            g.add(s, rim);
        }
    } else {
        // Far LOD: a solid dish instead of spokes.
        Mesh dish = cylinder(rimR * 0.9, rimR * 0.9, 0.02, 16);
        dish.rotate_z(HALF_PI);
        g.add(dish, rim, {faceX, 0, 0});
    }
    if (d.tire.beadlock && detail > 0) {
        Mesh ring = lathe({{rimR * 0.9, hw}, {rimR * 1.0, hw}, {rimR * 1.0, hw + 0.014}, {rimR * 0.9, hw + 0.014}, {rimR * 0.9, hw}}, rseg);
        ring.rotate_z(HALF_PI);
        g.add(ring, BLACK_STEEL);
        const int bolts = detail > 1 ? 24 : 12;
        for (int i = 0; i < bolts; i++) {
            const double a = static_cast<double>(i) / bolts * TAU;
            Mesh b = hex_bolt(0.0075, 0.007);
            b.rotate_y(HALF_PI);
            b.translate(hw + 0.014, std::cos(a) * rimR * 0.95, std::sin(a) * rimR * 0.95);
            g.add(b, ZINC);
        }
    }
    if (detail > 0) {
        for (int i = 0; i < 6; i++) {
            const double a = i / 6.0 * TAU;
            Mesh n = cylinder(0.011, 0.012, 0.016, 6);
            n.rotate_z(HALF_PI);
            n.translate(faceX + 0.014, std::cos(a) * 0.038, std::sin(a) * 0.038);
            g.add(n, ZINC);
        }
    }
    // Mirror so the tread V points the same way on both sides.
    return side < 0 ? transformed(g.mesh, {}, {}, {-1, 1, 1}) : g.mesh;
}

PaintedMesh brakes(const VehicleDesign& d, int side, int detail) {
    Group g;
    const double rimR = d.tire.rimInch * 0.0254 / 2;
    const double discR = rimR * 0.76;
    for (double x : {-0.016, 0.016}) g.add(cylinder(discR, discR, 0.008, detail > 1 ? 32 : 18), CAST_IRON, {x, 0, 0}, {0, 0, HALF_PI});
    Mesh hat = lathe({{0.045, -0.02}, {discR * 0.55, -0.02}, {discR * 0.55, 0.02}, {0.045, 0.02}, {0.045, -0.02}}, detail > 1 ? 24 : 14);
    hat.rotate_z(HALF_PI);
    g.add(hat, CAST_IRON);
    Group cal;
    cal.add(rounded_box(0.075, 0.075, 0.13, 0.016, 0.006, 2), powder(0x8a2020), {0, discR * 0.86, -0.01});
    for (double x : {-0.03, 0.03}) cal.add(rounded_box(0.018, 0.06, 0.1, 0.008, 0.004, 1), powder(0x8a2020), {x, discR * 0.78, -0.01});
    g.mesh.add(transformed(cal.mesh, {}, {side > 0 ? 0.35 : -0.35, 0, 0}));
    return g.mesh;
}

/* --------------------------------------------------------- suspension -- */

Mesh unit_rod(double radius, int seg) {
    Mesh g = cylinder(radius, radius, 1, seg);
    g.rotate_x(HALF_PI);
    g.translate(0, 0, 0.5);
    return g;
}

Mesh unit_box_link(double w, double h) {
    Mesh g = rounded_box(w, h, 1, std::min(w, h) * 0.3, 0.004, 1);
    g.translate(0, 0, 0.5);
    return g;
}

/* ------------------------------------------------------------ interior -- */

Group seat(bool harness) {
    Group g;
    g.add(rounded_box(0.46, 0.12, 0.48, 0.05, 0.01, 2), SEAT, {0, 0.22, 0});
    for (int s : {-1, 1}) g.add(rounded_box(0.09, 0.14, 0.42, 0.04, 0.008, 2), SEAT, {s * 0.2, 0.26, 0.01}, {0, 0, -s * 0.12});
    Group back;
    back.add(rounded_box(0.44, 0.62, 0.11, 0.05, 0.01, 2), SEAT, {0, 0.31, 0});
    for (int s : {-1, 1}) back.add(rounded_box(0.08, 0.56, 0.14, 0.04, 0.008, 2), SEAT, {s * 0.19, 0.3, 0.03}, {0, -s * 0.12, 0});
    back.add(rounded_box(0.26, 0.2, 0.12, 0.05, 0.01, 2), SEAT, {0, 0.71, 0.01});
    g.mesh.add(transformed(back.mesh, {0, 0.26, -0.22}, {0.2, 0, 0}));
    g.add(rounded_box(0.4, 0.04, 0.42, 0.01, 0.004, 1), BLACK_STEEL, {0, 0.13, 0});
    for (int s : {-1, 1}) g.add(rounded_box(0.03, 0.11, 0.4, 0.008, 0.003, 1), BLACK_STEEL, {s * 0.18, 0.06, 0});
    if (harness) {
        for (int s : {-1, 1}) g.add(rounded_box(0.055, 0.5, 0.012, 0.004, 0.002, 1), powder(0x9a2b2b), {s * 0.13, 0.62, -0.13}, {0.28, 0, -s * 0.06});
        g.add(rounded_box(0.07, 0.07, 0.03, 0.008, 0.003, 1), ZINC, {0, 0.3, -0.02});
    }
    return g;
}

Group interior(const VehicleDesign& d, const BodyMetrics& m, int detail, VehicleRig& rig) {
    Group g;
    const int side = d.rhd ? -1 : 1;
    const double seatZ = m.zCowl - 0.95;
    const double floorY = m.sillY + 0.04;
    for (int s : {-1, 1}) g.mesh.add(transformed(seat(d.features.cage).mesh, {s * (d.body.width * 0.24), floorY, seatZ}));
    if (d.body.doors == 4 && detail > 1) {
        g.add(rounded_box(d.body.width - 0.2, 0.14, 0.46, 0.05, 0.01, 2), SEAT, {0, floorY + 0.2, seatZ - 0.82});
        g.add(rounded_box(d.body.width - 0.2, 0.56, 0.12, 0.05, 0.01, 2), SEAT, {0, floorY + 0.48, seatZ - 1.04}, {0.16, 0, 0});
    }

    // Dash.
    const double z = m.zCowl - 0.18, w = d.body.width - 0.1, y = m.beltY - 0.1;
    g.add(rounded_box(w, 0.22, 0.34, 0.05, 0.01, 2), DASH, {0, y, z - 0.1}, {0.18, 0, 0});
    g.add(rounded_box(w, 0.24, 0.16, 0.03, 0.008, 2), DASH, {0, y - 0.22, z - 0.04});
    const double bx = side * (d.body.width * 0.24);
    g.add(rounded_box(0.34, 0.18, 0.2, 0.05, 0.01, 2), DASH, {bx, y + 0.08, z - 0.14}, {0.3, 0, 0});
    for (double dx : {-0.075, 0.075}) {
        g.add(cylinder(0.062, 0.062, 0.012, 18), look(0x2a3a44, 0.2f, 0.0f), {bx + dx, y + 0.1, z - 0.23}, {HALF_PI - 0.3, 0, 0});
    }
    g.add(rounded_box(0.3, 0.42, 0.1, 0.03, 0.008, 2), DASH, {0, y - 0.08, z - 0.16});
    g.add(rounded_box(0.22, 0.13, 0.02, 0.012, 0.004, 1), look(0x1b2a33, 0.15f, 0.0f), {0, y + 0.02, z - 0.21});
    if (detail > 1) {
        for (int i = 0; i < 6; i++) {
            g.add(rounded_box(0.035, 0.022, 0.015, 0.004, 0.002, 1), TRIM, {-0.09 + (i % 3) * 0.09, y - 0.13 - (i / 3) * 0.04, z - 0.21});
        }
        for (int s : {-1, 1}) g.add(rounded_box(0.13, 0.06, 0.03, 0.012, 0.004, 1), TRIM, {s * 0.28, y + 0.04, z - 0.2});
        g.add(tube({{-side * (d.body.width * 0.18), y + 0.03, z - 0.2}, {-side * (d.body.width * 0.3), y + 0.06, z - 0.22},
                    {-side * (d.body.width * 0.36), y - 0.02, z - 0.19}},
                   0.016, 8),
              BLACK_STEEL);
    }

    // Steering column (static) and the rig slot for the wheel.
    const Vec3 colPos{side * (d.body.width * 0.24), m.beltY - 0.14, m.zCowl - 0.42};
    const double tilt = -0.42;
    Mesh column = cylinder(0.026, 0.03, 0.34, 12);
    column.rotate_x(HALF_PI);
    column.translate(0, 0, -0.18);
    column.rotate_x(tilt);
    column.translate(colPos.x, colPos.y, colPos.z);
    g.add(column, TRIM);
    rig.steeringPos = colPos;
    rig.steeringTilt = tilt;

    const double floorZ = m.zCowl - 0.7;
    g.add(rounded_box(0.34, 0.26, 1.0, 0.06, 0.01, 2), BEDLINER, {0, floorY + 0.1, floorZ});
    struct Lever { double dx, len; };
    for (const Lever& l : {Lever{-0.02, 0.3}, Lever{0.13, 0.24}}) {
        const Vec3 base{l.dx, floorY + 0.22, m.zCowl - 0.62};
        g.add(cylinder(0.013, 0.016, l.len, 8), ZINC, {base.x, base.y + l.len / 2, base.z});
        g.add(sphere(0.032, 14, 10), TRIM, {base.x, base.y + l.len, base.z});
        g.add(cylinder(0.0, 0.05, 0.07, 12), RUBBER, {base.x, base.y + 0.03, base.z});
    }
    for (int i = 0; i < 3; i++) {
        g.add(rounded_box(0.06, 0.13, 0.02, 0.01, 0.003, 1), BLACK_STEEL, {side * (d.body.width * 0.24) + (i - 1) * 0.085, floorY + 0.14, m.zCowl - 0.26}, {-0.35, 0, 0});
    }
    g.add(rounded_box(0.22, 0.06, 0.03, 0.012, 0.004, 1), TRIM, {0, m.roofY - 0.1, m.zCowl - m.rake + 0.12}, {0.2, 0, 0});
    g.add(rounded_box(d.body.width - 0.16, 0.012, 1.3, 0.03, 0.004, 1), BEDLINER, {0, floorY + 0.008, m.zCowl - 0.8});
    return g;
}

PaintedMesh steering_wheel(double radius) {
    Group g;
    g.add(torus(radius, 0.018, 10, 30), RUBBER);
    for (int i = 0; i < 3; i++) {
        const double a = i / 3.0 * TAU + HALF_PI;
        Mesh spoke = rounded_box(0.03, radius * 0.92, 0.014, 0.006, 0.002, 1);
        spoke.translate(0, radius * 0.46, 0);
        spoke.rotate_z(a - HALF_PI);
        g.add(spoke, BLACK_STEEL);
    }
    Mesh hub = lathe({{0, 0}, {0.05, 0.006}, {0.052, 0.03}, {0, 0.034}}, 16);
    hub.rotate_x(HALF_PI);
    g.add(hub, TRIM);
    return g.mesh;
}

/* --------------------------------------------------------- accessories -- */

Group roll_cage(const VehicleDesign& d, const BodyMetrics& m) {
    Group g;
    constexpr double R = 0.028;
    const Look mat = powder(0xd8d2c4);
    const double x = m.halfW - 0.08, yTop = m.roofY - 0.05, yFloor = m.sillY + 0.04;
    const double zHoop = d.body.doors == 4 ? -d.frame.wheelbase * 0.02 : m.zCowl - 1.0;
    const double zWs = m.zCowl - m.rake;
    g.add(tube({{x, yFloor, zHoop}, {x, m.beltY, zHoop}, {x - 0.02, yTop - 0.04, zHoop}, {x - 0.06, yTop, zHoop},
                {-(x - 0.06), yTop, zHoop}, {-(x - 0.02), yTop - 0.04, zHoop}, {-x, m.beltY, zHoop}, {-x, yFloor, zHoop}},
               R, 10),
          mat);
    for (int s : {-1, 1}) {
        g.add(tube({{s * x, yFloor, m.zCowl - 0.1}, {s * x, m.beltY, m.zCowl - 0.08}, {s * (x - 0.05), yTop, zWs + 0.05}, {s * (x - 0.06), yTop, zHoop}}, R, 10), mat);
        g.add(tube({{s * (x - 0.06), yTop, zHoop}, {s * (x - 0.02), m.beltY + 0.1, zHoop - 0.5}, {s * x, yFloor + 0.05, m.zCabRear + 0.12}}, R * 0.92, 8), mat);
        g.add(rod({s * x, m.beltY - 0.06, zHoop - 0.02}, {s * x, yFloor + 0.18, m.zCowl - 0.12}, R * 0.85, 8), mat);
    }
    g.add(rod({x - 0.05, yTop, zWs + 0.05}, {-(x - 0.05), yTop, zWs + 0.05}, R * 0.9, 8), mat);
    g.add(rod({x - 0.02, m.beltY + 0.14, zHoop - 0.02}, {-(x - 0.02), m.beltY + 0.14, zHoop - 0.02}, R * 0.85, 8), mat);
    for (int s : {-1, 1}) {
        for (double z : {zHoop, m.zCowl - 0.1, m.zCabRear + 0.12}) g.add(rounded_box(0.1, 0.008, 0.1, 0.014, 0.003, 1), RAW_STEEL, {s * x, yFloor, z});
        g.add(cylinder(R * 1.7, R * 1.7, 0.3, 10), RUBBER, {s * x, m.beltY + 0.2, zHoop});
    }
    return g;
}

Group front_bumper(const VehicleDesign& d, const BodyMetrics& m, bool winch) {
    Group g;
    const double z = m.zFront + 0.11, y = m.sillY + 0.06, x = m.halfW + 0.04;
    const Look mat = powder(0x1b1d21);
    g.add(tube({{-x, y, z - 0.26}, {-x * 0.94, y, z - 0.02}, {-x * 0.6, y, z}, {x * 0.6, y, z}, {x * 0.94, y, z - 0.02}, {x, y, z - 0.26}}, 0.055, 12), mat);
    g.add(rounded_box(d.body.width - 0.06, 0.2, 0.05, 0.03, 0.006, 2), mat, {0, y + 0.1, z - 0.03});
    g.add(tube({{-x * 0.8, y - 0.14, z - 0.06}, {0, y - 0.16, z - 0.02}, {x * 0.8, y - 0.14, z - 0.06}}, 0.032, 8), mat);
    for (int s : {-1, 1}) {
        g.add(rounded_box(0.012, 0.09, 0.07, 0.006, 0.003, 1), RAW_STEEL, {s * 0.3, y + 0.04, z - 0.02});
        g.add(torus(0.042, 0.011, 8, 18, PI * 1.55), ZINC, {s * 0.31, y + 0.02, z + 0.01}, {0, HALF_PI, -0.4});
    }
    if (winch) {
        Group w;
        w.add(cylinder(0.06, 0.06, 0.24, 16), ZINC, {}, {0, 0, HALF_PI});
        w.add(cylinder(0.082, 0.082, 0.22, 20), TRIM, {}, {0, 0, HALF_PI});
        for (int s : {-1, 1}) {
            w.add(lathe({{0, 0}, {0.055, 0.01}, {0.058, 0.13}, {0.03, 0.15}, {0, 0.15}}, 14), powder(0x8c1f1f), {s * 0.13, 0, 0}, {0, 0, -s * HALF_PI});
        }
        g.mesh.add(transformed(w.mesh, {0, y + 0.12, z - 0.13}));
        g.add(rounded_box(0.16, 0.07, 0.035, 0.02, 0.005, 2), RAW_STEEL, {0, y + 0.1, z + 0.02});
        g.add(torus(0.032, 0.01, 8, 16, PI * 1.7), ZINC, {0, y + 0.06, z + 0.05}, {HALF_PI, 0, 0});
    }
    return g;
}

Group rear_bumper(const VehicleDesign&, const BodyMetrics& m) {
    Group g;
    const double z = m.zRear - 0.1, y = m.sillY + 0.04, x = m.halfW + 0.03;
    const Look mat = powder(0x1b1d21);
    g.add(tube({{-x, y, z + 0.22}, {-x * 0.9, y, z}, {x * 0.9, y, z}, {x, y, z + 0.22}}, 0.05, 12), mat);
    g.add(rounded_box(0.34, 0.012, 0.12, 0.012, 0.004, 1), DIAMOND, {0, y + 0.05, z - 0.01});
    g.add(rounded_box(0.07, 0.07, 0.2, 0.008, 0.003, 1), RAW_STEEL, {0, y - 0.02, z + 0.1});
    for (int s : {-1, 1}) g.add(torus(0.04, 0.011, 8, 18, PI * 1.55), ZINC, {s * 0.26, y - 0.01, z - 0.01}, {0, HALF_PI, 0.4});
    return g;
}

Group snorkel(const BodyMetrics& m, int side) {
    Group g;
    const double x = side * (m.halfW + 0.03);
    std::vector<Vec3> path{{x - side * 0.06, m.hoodY - 0.12, m.zCowl + 0.18}, {x, m.hoodY + 0.02, m.zCowl + 0.1},
                           {x, m.beltY + 0.12, m.zCowl - 0.02}, {x, m.roofY - 0.14, m.zCowl - m.rake * 0.55}};
    g.add(tube(path, 0.045, 12), TRIM);
    g.add(lathe({{0, 0}, {0.058, 0.01}, {0.062, 0.12}, {0.05, 0.14}, {0.05, 0.02}, {0, 0.02}}, 16), TRIM,
          {x, m.roofY - 0.1, m.zCowl - m.rake * 0.55}, {-HALF_PI, 0, 0});
    for (double t : {0.25, 0.6}) {
        const size_t i = static_cast<size_t>(std::floor(t * (path.size() - 1)));
        g.add(torus(0.048, 0.006, 6, 14), ZINC, path[i], {HALF_PI - 0.4, 0, 0});
    }
    return g;
}

Group light_bar(const VehicleDesign& d, const BodyMetrics& m) {
    Group g;
    const double w = d.body.width - 0.14, yy = m.roofY + 0.08, zz = m.zCowl - m.rake + 0.02;
    constexpr int pods = 6;
    g.add(rounded_box(w, 0.07, 0.06, 0.012, 0.004, 1), powder(0x17191d), {0, yy, zz});
    for (int i = 0; i < pods; i++) {
        const double px = -w / 2 + (w / pods) * (i + 0.5);
        g.add(cylinder(w / pods * 0.36, w / pods * 0.36, 0.02, 14), lens(0xf2f8ff, LAMP_AUX), {px, yy, zz + 0.032}, {HALF_PI, 0, 0});
    }
    for (int s : {-1, 1}) g.add(rounded_box(0.03, 0.09, 0.05, 0.008, 0.003, 1), powder(0x17191d), {s * (w / 2 - 0.03), yy - 0.07, zz});
    return g;
}

Group roof_rack(const VehicleDesign& d, const BodyMetrics& m, bool cargo) {
    Group g;
    const double zFront = m.zCowl - m.rake - 0.02, zRear = m.zCabRear + 0.02;
    const double len = std::fabs(zFront - zRear), w = d.body.width - 0.04, y = m.roofY + 0.055;
    const Look mat = powder(0x202329);
    for (const auto& [a, b] : std::vector<std::pair<Vec3, Vec3>>{{{-w / 2, y, zRear}, {-w / 2, y, zFront}}, {{-w / 2, y, zFront}, {w / 2, y, zFront}},
                                                                 {{w / 2, y, zFront}, {w / 2, y, zRear}}, {{w / 2, y, zRear}, {-w / 2, y, zRear}}}) {
        g.add(rod(a, b, 0.022, 8), mat);
        g.add(sphere(0.022, 8, 6), mat, a);
    }
    const int n = std::max(5, static_cast<int>(std::lround(len / 0.13)));
    for (int i = 0; i < n; i++) {
        g.add(rounded_box(w - 0.03, 0.012, 0.045, 0.005, 0.002, 1), mat, {0, y - 0.012, zRear + (len / (n - 1)) * i * (zFront > zRear ? 1 : -1)});
    }
    for (int s : {-1, 1}) {
        for (double t : {0.15, 0.5, 0.85}) g.add(rounded_box(0.03, 0.055, 0.05, 0.008, 0.003, 1), mat, {s * (w / 2 - 0.01), y - 0.04, zRear + (zFront - zRear) * t});
    }
    if (cargo) {
        for (int i = 0; i < 2; i++) {
            Group can;
            can.add(rounded_box(0.17, 0.35, 0.09, 0.022, 0.006, 2), powder(0x3f4a37));
            can.add(rounded_box(0.09, 0.02, 0.06, 0.008, 0.003, 1), powder(0x2e3629), {0, 0.18, 0});
            g.mesh.add(transformed(can.mesh, {-w / 2 + 0.11 + i * 0.12, y + 0.17, zRear + len * 0.22}, {0, HALF_PI, 0}));
        }
        for (int i = 0; i < 2; i++) {
            g.add(rounded_box(0.28, 0.022, len * 0.5, 0.02, 0.005, 1), powder(0xd2761f), {w / 2 - 0.2 - i * 0.03, y + 0.02 + i * 0.026, zRear + len * 0.55});
        }
        g.add(torus(0.075, 0.028, 8, 18), powder(0x8d3030), {0, y + 0.03, zFront - 0.12}, {HALF_PI, 0, 0});
    }
    return g;
}

Group spare(const VehicleDesign& d, const BodyMetrics& m, int detail) {
    Group g;
    PaintedMesh w = wheel(d, 1, std::max(0, detail - 1));
    const bool pickup = d.body.pickup;
    if (pickup) {
        g.mesh.add(transformed(w, {m.halfW - 0.3, m.sillY + 0.3, m.zCabRear - 0.3}, {0, 0, HALF_PI}));
    } else {
        const Vec3 pos{0, (m.sillY + m.beltY) / 2 + 0.1, m.zRear - 0.22};
        g.mesh.add(transformed(w, pos, {0, HALF_PI, 0}));
        g.add(rounded_box(0.06, 0.06, 0.16, 0.012, 0.004, 1), powder(0x1b1d21), {pos.x, pos.y, pos.z + 0.14}, {0, HALF_PI, 0});
    }
    return g;
}

Group misc_kit(const BodyMetrics& m) {
    Group g;
    g.add(cylinder(0.004, 0.002, 1.2, 6), TRIM, {m.halfW - 0.02, m.hoodY + 0.62, m.zCowl + 0.04}, {0, 0, -0.08});
    g.add(cylinder(0.014, 0.014, 0.03, 10), BLACK_STEEL, {m.halfW - 0.02, m.hoodY + 0.02, m.zCowl + 0.04});
    g.add(rounded_box(0.03, 0.05, 1.0, 0.006, 0.002, 1), RAW_STEEL, {-(m.halfW + 0.02), m.sillY + 0.3, m.zCowl - 0.75}, {0.1, 0, 0});
    return g;
}

}  // namespace

/* ================================================================ public == */

BodyMetrics body_metrics(const VehicleDesign& d) {
    BodyMetrics m{};
    const auto& f = d.frame;
    const auto& b = d.body;
    m.sillY = f.railY + f.railHeight / 2 + 0.05;
    m.beltY = m.sillY + b.sideHeight;
    m.hoodY = m.sillY + b.sideHeight - b.hoodDrop;
    m.roofY = m.sillY + b.sideHeight + b.glassHeight;
    m.zFront = f.wheelbase / 2 + f.frontOverhang - 0.08;
    m.zRear = -(f.wheelbase / 2 + f.rearOverhang - 0.08);
    m.zCowl = f.wheelbase / 2 - b.cowlSetback;
    m.zCabRear = b.pickup ? -f.wheelbase * 0.06 : -(f.wheelbase / 2 + f.rearOverhang - 0.1);
    m.halfW = b.width / 2;
    m.archY = d.tire.diameter / 2;
    m.archR = d.tire.diameter / 2 + b.archClearance;
    const double dy = m.sillY - d.tire.diameter / 2;
    m.archHalf = dy >= m.archR ? 0 : std::sqrt(m.archR * m.archR - dy * dy);
    m.frontAxleZ = f.wheelbase / 2;
    m.rearAxleZ = -f.wheelbase / 2;
    m.rake = b.windshieldRake;
    return m;
}

VehicleMeshes build_vehicle_meshes(const VehicleDesign& d) {
    VehicleMeshes out;
    out.id = d.id;
    const BodyMetrics m = body_metrics(d);
    auto& rig = out.rig;
    rig.wheelRadius = d.tire.diameter / 2;
    rig.track = d.axle.track;
    rig.axleZ = {d.frame.wheelbase / 2, -d.frame.wheelbase / 2};
    rig.axleY = d.tire.diameter / 2;
    rig.travel = d.suspension.travel;

    // Suspension geometry (buildFourLink), shared by every LOD.
    const auto& f = d.frame;
    for (int ai = 0; ai < 2; ai++) {
        const double axleZ = rig.axleZ[static_cast<size_t>(ai)];
        const double dir = axleZ >= 0 ? 1 : -1;
        const Vec3 rest{0, rig.axleY, axleZ};
        auto local = [&](const Vec3& v) { return v - rest; };
        for (int s : {-1, 1}) {
            rig.links.push_back({ai, 0, {s * (f.frameWidth / 2 - 0.02), f.railY - 0.06, axleZ - dir * 0.62},
                                 local({s * (d.axle.track / 2 - 0.34), rig.axleY - 0.03, axleZ - dir * 0.02})});
            rig.links.push_back({ai, 1, {s * (f.frameWidth / 2 - 0.06), f.railY + 0.12, axleZ - dir * 0.48},
                                 local({s * 0.19, rig.axleY + 0.14, axleZ - dir * 0.03})});
        }
        rig.links.push_back({ai, 2, {-(f.frameWidth / 2 - 0.01), f.railY - 0.02, axleZ + dir * 0.1},
                             local({d.axle.track / 2 - 0.3, rig.axleY + 0.1, axleZ + dir * 0.1})});
        for (int s : {-1, 1}) {
            rig.shocks.push_back({ai, {s * (d.axle.track / 2 - 0.26), f.railY + 0.2, axleZ + dir * 0.06},
                                  local({s * (d.axle.track / 2 - 0.24), rig.axleY + 0.02, axleZ + dir * 0.06})});
        }
    }

    for (int detail = 0; detail < 3; detail++) {
        VehicleLod& lod = out.lods[static_cast<size_t>(detail)];
        Group chassis;
        chassis.add(ladder_frame(d));
        chassis.add(drivetrain(d, detail));
        chassis.add(armour(d, detail));
        chassis.add(body(d, m, detail));
        if (detail > 0) chassis.add(interior(d, m, detail, rig));  // the far LOD has no cabin
        // Fixed suspension hardware: bump stops, steering box.
        for (int ai = 0; ai < 2; ai++) {
            const double axleZ = rig.axleZ[static_cast<size_t>(ai)];
            for (int s : {-1, 1}) chassis.add(cylinder(0.03, 0.038, 0.09, 10), RUBBER, {s * (d.axle.track / 2 - 0.42), f.railY - 0.1, axleZ});
            if (ai == 0) chassis.add(rounded_box(0.11, 0.14, 0.11, 0.02, 0.005, 1), CAST_IRON, {f.frameWidth / 2 + 0.04, f.railY + 0.02, axleZ - 0.16});
        }
        if (d.features.cage && detail > 0) chassis.add(roll_cage(d, m));
        chassis.add(front_bumper(d, m, d.features.winch));
        if (d.features.rearBumper) chassis.add(rear_bumper(d, m));
        if (d.features.snorkel) chassis.add(snorkel(m, d.rhd ? -1 : 1));
        if (d.features.roofRack) chassis.add(roof_rack(d, m, detail > 1));
        if (d.features.spare) chassis.add(spare(d, m, detail));
        if (d.features.kit && detail > 1) chassis.add(misc_kit(m));
        if (d.features.lightBar) chassis.add(light_bar(d, m));
        lod.chassis = std::move(chassis.mesh);

        for (int side = 0; side < 2; side++) {
            lod.wheel[static_cast<size_t>(side)] = wheel(d, side == 0 ? -1 : 1, detail);
            lod.brake[static_cast<size_t>(side)] = brakes(d, side == 0 ? -1 : 1, detail);
        }
        lod.axle[0] = solid_axle(d, true, detail);
        lod.axle[1] = solid_axle(d, false, detail);
        const Look arm = powder(0x24262b);
        lod.link[0].add(unit_box_link(0.05, 0.062), arm);
        lod.link[1].add(unit_rod(0.024, 8), arm);
        lod.link[2].add(unit_rod(0.02, 8), ZINC);

        // Coilover: body + reservoir + top eye hang from the mount; shaft + bottom
        // eye rise from the axle; the spring stretches between them.
        const double length = d.suspension.travel * 1.7;
        const Look shockCol = powder(d.suspension.shockColor);
        Group shock;
        shock.add(cylinder(0.026, 0.028, length * 0.5, 14), shockCol, {0, -length * 0.25, 0});
        shock.add(cylinder(0.019, 0.019, length * 0.3, 10), shockCol, {0.05, -length * 0.22, 0}, {0, 0, -0.12});
        shock.add(torus(0.022, 0.011, 8, 14), BLACK_STEEL, {0, 0.01, 0}, {0, HALF_PI, 0});
        lod.shockBody = std::move(shock.mesh);
        Group shaft;
        shaft.add(cylinder(0.013, 0.013, length * 0.55, 10), ZINC, {0, length * 0.275, 0});
        shaft.add(torus(0.022, 0.011, 8, 14), BLACK_STEEL, {0, -0.01, 0}, {0, HALF_PI, 0});
        lod.shockShaft = std::move(shaft.mesh);
        lod.spring.add(coil_spring(0.058, 1.0, 8, 0.011, detail > 1 ? 6 : 4), powder(0xd8d8dc));
        lod.steeringWheel = steering_wheel(d.body.steeringR);
    }
    return out;
}

/* ------------------------------------------------------------ bake I/O -- */

namespace {

struct Writer {
    std::vector<uint8_t> b;
    void u32(uint32_t v) {
        for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
    void f32(double v) {
        const float f = static_cast<float>(v);
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        u32(bits);
    }
    void vec(const Vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void str(const std::string& s) {
        u32(static_cast<uint32_t>(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    }
    void u16(uint16_t v) {
        b.push_back(static_cast<uint8_t>(v & 0xFF));
        b.push_back(static_cast<uint8_t>(v >> 8));
    }
    /// Looks repeat heavily (a few dozen materials per part), so they are stored
    /// once in a palette with a 2-byte index per vertex.
    void mesh(const PaintedMesh& m) {
        std::vector<Look> palette;
        std::vector<uint16_t> ids(m.looks.size());
        for (size_t i = 0; i < m.looks.size(); i++) {
            const Look& l = m.looks[i];
            size_t found = palette.size();
            for (size_t k = 0; k < palette.size(); k++) {
                const Look& p = palette[k];
                if (p.albedo == l.albedo && p.roughness == l.roughness && p.metallic == l.metallic &&
                    p.clearcoat == l.clearcoat && p.emission == l.emission && p.layer == l.layer) {
                    found = k;
                    break;
                }
            }
            if (found == palette.size()) palette.push_back(l);
            ids[i] = static_cast<uint16_t>(found);
        }
        u32(static_cast<uint32_t>(palette.size()));
        for (const Look& l : palette) {
            f32(l.albedo[0]);
            f32(l.albedo[1]);
            f32(l.albedo[2]);
            f32(l.roughness);
            f32(l.metallic);
            f32(l.clearcoat);
            f32(l.emission);
            u32(l.layer);
        }
        u32(static_cast<uint32_t>(m.positions.size()));
        for (size_t i = 0; i < m.positions.size(); i++) {
            vec(m.positions[i]);
            vec(m.normals[i]);
            u16(ids[i]);
        }
        u32(static_cast<uint32_t>(m.indices.size()));
        for (uint32_t i : m.indices) u32(i);
    }
};

struct Reader {
    const std::vector<uint8_t>& b;
    size_t at = 0;
    bool ok = true;
    uint32_t u32() {
        if (at + 4 > b.size()) {
            ok = false;
            return 0;
        }
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(b[at + i]) << (8 * i);
        at += 4;
        return v;
    }
    float f32() {
        const uint32_t bits = u32();
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    Vec3 vec() {
        const double x = f32(), y = f32(), z = f32();
        return {x, y, z};
    }
    std::string str() {
        const uint32_t n = u32();
        if (!ok || at + n > b.size()) {
            ok = false;
            return {};
        }
        std::string s(b.begin() + static_cast<long>(at), b.begin() + static_cast<long>(at + n));
        at += n;
        return s;
    }
    uint16_t u16() {
        if (at + 2 > b.size()) {
            ok = false;
            return 0;
        }
        const uint16_t v = static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
        at += 2;
        return v;
    }
    PaintedMesh mesh() {
        PaintedMesh m;
        const uint32_t np = u32();
        if (!ok || np > 65536) {
            ok = false;
            return m;
        }
        std::vector<Look> palette(np);
        for (uint32_t i = 0; i < np && ok; i++) {
            Look& l = palette[i];
            l.albedo = {f32(), f32(), f32()};
            l.roughness = f32();
            l.metallic = f32();
            l.clearcoat = f32();
            l.emission = f32();
            l.layer = static_cast<uint8_t>(u32());
        }
        const uint32_t n = u32();
        if (!ok || n > 50'000'000) {
            ok = false;
            return m;
        }
        m.positions.reserve(n);
        m.normals.reserve(n);
        m.looks.reserve(n);
        for (uint32_t i = 0; i < n && ok; i++) {
            m.positions.push_back(vec());
            m.normals.push_back(vec());
            const uint16_t id = u16();
            if (id >= palette.size()) {
                ok = false;
                break;
            }
            m.looks.push_back(palette[id]);
        }
        const uint32_t ni = u32();
        if (!ok || ni > 150'000'000) {
            ok = false;
            return m;
        }
        m.indices.resize(ni);
        for (uint32_t i = 0; i < ni && ok; i++) m.indices[i] = u32();
        return m;
    }
};

}  // namespace

std::vector<uint8_t> serialize_vehicles(const std::vector<VehicleMeshes>& set) {
    Writer w;
    const char magic[8] = {'R', 'L', 'V', 'E', 'H', 'I', 'C', '2'};
    w.b.insert(w.b.end(), magic, magic + 8);
    w.u32(static_cast<uint32_t>(set.size()));
    for (const auto& v : set) {
        w.str(v.id);
        const auto& r = v.rig;
        w.f32(r.wheelRadius);
        w.f32(r.track);
        w.f32(r.axleZ[0]);
        w.f32(r.axleZ[1]);
        w.f32(r.axleY);
        w.f32(r.travel);
        w.vec(r.steeringPos);
        w.f32(r.steeringTilt);
        w.u32(static_cast<uint32_t>(r.links.size()));
        for (const auto& l : r.links) {
            w.u32(static_cast<uint32_t>(l.axle));
            w.u32(static_cast<uint32_t>(l.part));
            w.vec(l.pivot);
            w.vec(l.target);
        }
        w.u32(static_cast<uint32_t>(r.shocks.size()));
        for (const auto& s : r.shocks) {
            w.u32(static_cast<uint32_t>(s.axle));
            w.vec(s.mount);
            w.vec(s.anchor);
        }
        for (const auto& lod : v.lods) {
            w.mesh(lod.chassis);
            for (const auto& m : lod.wheel) w.mesh(m);
            for (const auto& m : lod.brake) w.mesh(m);
            for (const auto& m : lod.axle) w.mesh(m);
            for (const auto& m : lod.link) w.mesh(m);
            w.mesh(lod.shockBody);
            w.mesh(lod.shockShaft);
            w.mesh(lod.spring);
            w.mesh(lod.steeringWheel);
        }
    }
    return std::move(w.b);
}

bool deserialize_vehicles(const std::vector<uint8_t>& bytes, std::vector<VehicleMeshes>& out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RLVEHIC2", 8) != 0) return fail("not a RLVEHIC2 file");
    Reader r{bytes, 8};
    const uint32_t count = r.u32();
    out.clear();
    for (uint32_t i = 0; i < count && r.ok; i++) {
        VehicleMeshes v;
        v.id = r.str();
        auto& g = v.rig;
        g.wheelRadius = r.f32();
        g.track = r.f32();
        g.axleZ[0] = r.f32();
        g.axleZ[1] = r.f32();
        g.axleY = r.f32();
        g.travel = r.f32();
        g.steeringPos = r.vec();
        g.steeringTilt = r.f32();
        const uint32_t nl = r.u32();
        for (uint32_t k = 0; k < nl && r.ok && k < 64; k++) {
            LinkRig l;
            l.axle = static_cast<int>(r.u32());
            l.part = static_cast<int>(r.u32());
            l.pivot = r.vec();
            l.target = r.vec();
            g.links.push_back(l);
        }
        const uint32_t ns = r.u32();
        for (uint32_t k = 0; k < ns && r.ok && k < 64; k++) {
            ShockRig s;
            s.axle = static_cast<int>(r.u32());
            s.mount = r.vec();
            s.anchor = r.vec();
            g.shocks.push_back(s);
        }
        for (auto& lod : v.lods) {
            lod.chassis = r.mesh();
            for (auto& m : lod.wheel) m = r.mesh();
            for (auto& m : lod.brake) m = r.mesh();
            for (auto& m : lod.axle) m = r.mesh();
            for (auto& m : lod.link) m = r.mesh();
            lod.shockBody = r.mesh();
            lod.shockShaft = r.mesh();
            lod.spring = r.mesh();
            lod.steeringWheel = r.mesh();
        }
        if (!r.ok) return fail("truncated vehicle data");
        out.push_back(std::move(v));
    }
    return r.ok ? true : fail("truncated vehicle file");
}

}  // namespace worldcore
