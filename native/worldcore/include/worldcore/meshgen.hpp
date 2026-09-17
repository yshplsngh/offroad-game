// meshgen.hpp - procedural mesh kernel for build-time asset baking.
//
// A small, dependency-free replacement for the Three.js geometry the reference
// vehicle builders used: polygon triangulation with holes, extrusion with
// bevels, rounded boxes, cylinders, cones, spheres, tori, lathes, Catmull-Rom
// tubes and coil springs. Everything is indexed triangles with smooth normals
// split at hard edges, counter-clockwise front faces, +Y up.
//
// Every vertex also carries a material "look" (albedo, roughness, metallic,
// clearcoat, emission) so a whole vehicle can merge into very few draw calls
// and be shaded by one shader.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "worldcore/vmath.hpp"

namespace worldcore {

struct Vec2 {
    double x = 0, y = 0;
};

/// Surface description baked into vertices.
struct Look {
    std::array<float, 3> albedo{0.5f, 0.5f, 0.5f};  // linear
    float roughness = 0.6f;
    float metallic = 0.0f;
    float clearcoat = 0.0f;
    float emission = 0.0f;  // multiplier on albedo when a lamp is lit
    uint8_t layer = 0;      // 0 opaque, 1 paint (tintable), 2 glass, 3 lamp lens
};

struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<uint32_t> indices;  // CCW front faces

    size_t triangles() const { return indices.size() / 3; }
    void append(const Mesh& other);
    Mesh& translate(double x, double y, double z);
    Mesh& rotate_x(double a);
    Mesh& rotate_y(double a);
    Mesh& rotate_z(double a);
    Mesh& rotate(const Quat& q);
    Mesh& scale(double x, double y, double z);  // flips winding for mirrors
    /// Recompute smooth normals, split where faces meet at more than `creaseDeg`.
    Mesh& smooth(double creaseDeg = 40.0);
    Mesh& flip();
    /// Remove zero-area triangles (cone apexes, lathe poles).
    Mesh& drop_degenerate();
};

/* ---------------------------------------------------------- 2D shapes -- */

/// Triangulate a simple polygon with optional holes (ear clipping with hole
/// bridging). Returns indices into the concatenation outer + holes.
std::vector<uint32_t> triangulate(const std::vector<Vec2>& outer, const std::vector<std::vector<Vec2>>& holes);

std::vector<Vec2> rounded_rect(double w, double h, double r, int cornerSeg = 3);
std::vector<Vec2> arc_points(double cx, double cy, double r, double a0, double a1, int steps);

/* ---------------------------------------------------------- 3D solids -- */

/// Extrude a polygon in XY along +Z by `depth` (starting at z = 0), with a
/// chamfer `bevel` that grows the outline outward on the caps' edges.
Mesh extrude(const std::vector<Vec2>& outer, const std::vector<std::vector<Vec2>>& holes, double depth,
             double bevel = 0.0);

/// Box with rounded corners in XY and chamfered front/back, centred on the origin.
Mesh rounded_box(double w, double h, double d, double r = 0.02, double bevel = 0.008, int cornerSeg = 3);

/// Y-axis cylinder or cone frustum centred on the origin.
Mesh cylinder(double radiusTop, double radiusBottom, double height, int radial, bool capped = true);
Mesh sphere(double radius, int widthSeg, int heightSeg);
/// Torus in the XY plane around Z; `arc` < 2 pi gives an open arc starting on +X.
Mesh torus(double radius, double tube, int radialSeg, int tubularSeg, double arc = 6.283185307179586);
/// Revolve a (radius, y) profile around Y.
Mesh lathe(const std::vector<Vec2>& profile, int segments);
/// Round tube along a centripetal Catmull-Rom through `points`, with end caps.
Mesh tube(const std::vector<Vec3>& points, double radius, int radialSeg = 10, bool closed = false, bool caps = true);
/// Straight capped rod between two points.
Mesh rod(const Vec3& a, const Vec3& b, double radius, int radialSeg = 8);
/// Coil spring around +Y from y = 0 to `length`.
Mesh coil_spring(double radius, double length, int coils, double wire, int radialSeg = 6);
/// Double-sided quad in XY facing +Z (and -Z).
Mesh quad(double w, double h);

/* ------------------------------------------------------------- output -- */

/// A mesh with a look on every vertex, ready to merge with others.
struct PaintedMesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Look> looks;
    std::vector<uint32_t> indices;

    void add(const Mesh& m, const Look& look);
    void add(const PaintedMesh& other);
    size_t triangles() const { return indices.size() / 3; }
    /// Split by Look::layer so each layer can use its own material.
    std::array<PaintedMesh, 4> by_layer() const;
};

}  // namespace worldcore
