// meshgen.cpp - see meshgen.hpp.
#include "worldcore/meshgen.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <numbers>
#include <unordered_map>

namespace worldcore {

namespace {

constexpr double PI = std::numbers::pi;
constexpr double TAU = 2 * std::numbers::pi;

Vec3 any_perpendicular(const Vec3& v) {
    const Vec3 a = std::fabs(v.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    return v.cross(a).normalized();
}

}  // namespace

/* ================================================================ Mesh == */

void Mesh::append(const Mesh& o) {
    const auto base = static_cast<uint32_t>(positions.size());
    positions.insert(positions.end(), o.positions.begin(), o.positions.end());
    normals.insert(normals.end(), o.normals.begin(), o.normals.end());
    for (uint32_t i : o.indices) indices.push_back(base + i);
}

Mesh& Mesh::translate(double x, double y, double z) {
    for (auto& p : positions) p = p + Vec3{x, y, z};
    return *this;
}

Mesh& Mesh::rotate_x(double a) {
    const double c = std::cos(a), s = std::sin(a);
    auto r = [&](const Vec3& p) { return Vec3{p.x, c * p.y - s * p.z, s * p.y + c * p.z}; };
    for (auto& p : positions) p = r(p);
    for (auto& n : normals) n = r(n);
    return *this;
}

Mesh& Mesh::rotate_y(double a) {
    const double c = std::cos(a), s = std::sin(a);
    auto r = [&](const Vec3& p) { return Vec3{c * p.x + s * p.z, p.y, -s * p.x + c * p.z}; };
    for (auto& p : positions) p = r(p);
    for (auto& n : normals) n = r(n);
    return *this;
}

Mesh& Mesh::rotate_z(double a) {
    const double c = std::cos(a), s = std::sin(a);
    auto r = [&](const Vec3& p) { return Vec3{c * p.x - s * p.y, s * p.x + c * p.y, p.z}; };
    for (auto& p : positions) p = r(p);
    for (auto& n : normals) n = r(n);
    return *this;
}

Mesh& Mesh::rotate(const Quat& q) {
    for (auto& p : positions) p = q.rotate(p);
    for (auto& n : normals) n = q.rotate(n);
    return *this;
}

Mesh& Mesh::scale(double x, double y, double z) {
    for (auto& p : positions) p = {p.x * x, p.y * y, p.z * z};
    // Normals transform by the inverse transpose.
    for (auto& n : normals) n = Vec3{n.x / x, n.y / y, n.z / z}.normalized();
    if (x * y * z < 0) {
        for (size_t t = 0; t + 2 < indices.size(); t += 3) std::swap(indices[t + 1], indices[t + 2]);
    }
    return *this;
}

Mesh& Mesh::drop_degenerate() {
    std::vector<uint32_t> kept;
    kept.reserve(indices.size());
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        const Vec3& a = positions[indices[t]];
        const Vec3& b = positions[indices[t + 1]];
        const Vec3& c = positions[indices[t + 2]];
        if ((b - a).cross(c - a).length_sq() > 1e-18) kept.insert(kept.end(), {indices[t], indices[t + 1], indices[t + 2]});
    }
    indices = std::move(kept);
    return *this;
}

Mesh& Mesh::flip() {
    for (size_t t = 0; t + 2 < indices.size(); t += 3) std::swap(indices[t + 1], indices[t + 2]);
    for (auto& n : normals) n = n * -1;
    return *this;
}

Mesh& Mesh::smooth(double creaseDeg) {
    drop_degenerate();
    const double cosCrease = std::cos(creaseDeg * PI / 180.0);
    const size_t triCount = indices.size() / 3;
    std::vector<Vec3> faceN(triCount);
    for (size_t t = 0; t < triCount; t++) {
        const Vec3& a = positions[indices[t * 3]];
        const Vec3& b = positions[indices[t * 3 + 1]];
        const Vec3& c = positions[indices[t * 3 + 2]];
        faceN[t] = (b - a).cross(c - a);  // area-weighted
    }
    // Group corners by exact quantised position.
    struct Key {
        int64_t x, y, z;
        bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return static_cast<size_t>(static_cast<uint64_t>(k.x) * 73856093ull ^ static_cast<uint64_t>(k.y) * 19349663ull ^
                                       static_cast<uint64_t>(k.z) * 83492791ull);
        }
    };
    auto key = [](const Vec3& p) {
        return Key{std::llround(p.x * 1e5), std::llround(p.y * 1e5), std::llround(p.z * 1e5)};
    };
    std::unordered_map<Key, std::vector<size_t>, KeyHash> corners;
    for (size_t c = 0; c < indices.size(); c++) corners[key(positions[indices[c]])].push_back(c);

    std::vector<Vec3> outP, outN;
    std::vector<uint32_t> outI(indices.size());
    std::unordered_map<Key, std::vector<uint32_t>, KeyHash> emitted;  // dedupe by position
    for (size_t c = 0; c < indices.size(); c++) {
        const Vec3 p = positions[indices[c]];
        const Vec3 fn = faceN[c / 3].normalized();
        Vec3 sum;
        for (size_t other : corners[key(p)]) {
            const Vec3& on = faceN[other / 3];
            if (on.normalized().dot(fn) >= cosCrease) sum += on;
        }
        Vec3 n = sum.length_sq() > 1e-20 ? sum.normalized() : fn;
        // Reuse an identical vertex if one exists.
        uint32_t idx = std::numeric_limits<uint32_t>::max();
        auto& list = emitted[key(p)];
        for (uint32_t cand : list) {
            if (outN[cand].dot(n) > 0.9999) {
                idx = cand;
                break;
            }
        }
        if (idx == std::numeric_limits<uint32_t>::max()) {
            idx = static_cast<uint32_t>(outP.size());
            outP.push_back(p);
            outN.push_back(n);
            list.push_back(idx);
        }
        outI[c] = idx;
    }
    positions = std::move(outP);
    normals = std::move(outN);
    indices = std::move(outI);
    return *this;
}

/* ========================================================= triangulate == */
// Ear clipping with hole bridging, after mapbox/earcut (ISC licence).

namespace {

struct ENode {
    uint32_t i;
    double x, y;
    ENode* prev = nullptr;
    ENode* next = nullptr;
    bool steiner = false;
};

struct Earcut {
    std::deque<ENode> pool;
    std::vector<uint32_t> tris;

    ENode* insert(uint32_t i, double x, double y, ENode* last) {
        pool.push_back({i, x, y});
        ENode* p = &pool.back();
        if (!last) {
            p->prev = p;
            p->next = p;
        } else {
            p->next = last->next;
            p->prev = last;
            last->next->prev = p;
            last->next = p;
        }
        return p;
    }
    static void remove(ENode* p) {
        p->next->prev = p->prev;
        p->prev->next = p->next;
    }
    static double area(const ENode* p, const ENode* q, const ENode* r) {
        return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y);
    }
    static bool equals(const ENode* a, const ENode* b) { return a->x == b->x && a->y == b->y; }
    static int sign(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
    static bool on_segment(const ENode* p, const ENode* q, const ENode* r) {
        return q->x <= std::max(p->x, r->x) && q->x >= std::min(p->x, r->x) && q->y <= std::max(p->y, r->y) &&
               q->y >= std::min(p->y, r->y);
    }
    static bool intersects(const ENode* p1, const ENode* q1, const ENode* p2, const ENode* q2) {
        const int o1 = sign(area(p1, q1, p2)), o2 = sign(area(p1, q1, q2));
        const int o3 = sign(area(p2, q2, p1)), o4 = sign(area(p2, q2, q1));
        if (o1 != o2 && o3 != o4) return true;
        if (o1 == 0 && on_segment(p1, p2, q1)) return true;
        if (o2 == 0 && on_segment(p1, q2, q1)) return true;
        if (o3 == 0 && on_segment(p2, p1, q2)) return true;
        if (o4 == 0 && on_segment(p2, q1, q2)) return true;
        return false;
    }
    static bool point_in_triangle(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) {
        return (cx - px) * (ay - py) >= (ax - px) * (cy - py) && (ax - px) * (by - py) >= (bx - px) * (ay - py) &&
               (bx - px) * (cy - py) >= (cx - px) * (by - py);
    }
    static bool locally_inside(const ENode* a, const ENode* b) {
        return area(a->prev, a, a->next) < 0 ? area(a, b, a->next) >= 0 && area(a, a->prev, b) >= 0
                                             : area(a, b, a->prev) < 0 || area(a, a->next, b) < 0;
    }
    static bool middle_inside(const ENode* a, const ENode* b) {
        const ENode* p = a;
        bool inside = false;
        const double px = (a->x + b->x) / 2, py = (a->y + b->y) / 2;
        do {
            if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y &&
                (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x))
                inside = !inside;
            p = p->next;
        } while (p != a);
        return inside;
    }
    static bool intersects_polygon(const ENode* a, const ENode* b) {
        const ENode* p = a;
        do {
            if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i && intersects(p, p->next, a, b))
                return true;
            p = p->next;
        } while (p != a);
        return false;
    }
    static bool valid_diagonal(const ENode* a, const ENode* b) {
        return a->next->i != b->i && a->prev->i != b->i && !intersects_polygon(a, b) &&
               ((locally_inside(a, b) && locally_inside(b, a) && middle_inside(a, b) &&
                 (area(a->prev, a, b->prev) != 0 || area(a, b->prev, b) != 0)) ||
                (equals(a, b) && area(a->prev, a, a->next) > 0 && area(b->prev, b, b->next) > 0));
    }
    ENode* split(ENode* a, ENode* b) {
        pool.push_back({a->i, a->x, a->y});
        ENode* a2 = &pool.back();
        pool.push_back({b->i, b->x, b->y});
        ENode* b2 = &pool.back();
        ENode* an = a->next;
        ENode* bp = b->prev;
        a->next = b;
        b->prev = a;
        a2->next = an;
        an->prev = a2;
        b2->next = a2;
        a2->prev = b2;
        bp->next = b2;
        b2->prev = bp;
        return b2;
    }
    ENode* filter(ENode* start, ENode* end = nullptr) {
        if (!start) return start;
        if (!end) end = start;
        ENode* p = start;
        bool again;
        do {
            again = false;
            if (!p->steiner && (equals(p, p->next) || area(p->prev, p, p->next) == 0)) {
                remove(p);
                p = end = p->prev;
                if (p == p->next) break;
                again = true;
            } else {
                p = p->next;
            }
        } while (again || p != end);
        return end;
    }
    ENode* linked(const std::vector<Vec2>& pts, uint32_t base, bool ccw) {
        double sum = 0;
        for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) sum += (pts[j].x - pts[i].x) * (pts[i].y + pts[j].y);
        ENode* last = nullptr;
        if (ccw == (sum > 0)) {
            for (size_t i = 0; i < pts.size(); i++) last = insert(base + static_cast<uint32_t>(i), pts[i].x, pts[i].y, last);
        } else {
            for (size_t i = pts.size(); i-- > 0;) last = insert(base + static_cast<uint32_t>(i), pts[i].x, pts[i].y, last);
        }
        if (last && equals(last, last->next)) {
            remove(last);
            last = last->next;
        }
        return last;
    }
    bool is_ear(const ENode* ear) {
        const ENode* a = ear->prev;
        const ENode* b = ear;
        const ENode* c = ear->next;
        if (area(a, b, c) >= 0) return false;
        const double x0 = std::min({a->x, b->x, c->x}), y0 = std::min({a->y, b->y, c->y});
        const double x1 = std::max({a->x, b->x, c->x}), y1 = std::max({a->y, b->y, c->y});
        for (const ENode* p = c->next; p != a; p = p->next) {
            if (p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1 &&
                point_in_triangle(a->x, a->y, b->x, b->y, c->x, c->y, p->x, p->y) && area(p->prev, p, p->next) >= 0)
                return false;
        }
        return true;
    }
    ENode* cure(ENode* start) {
        ENode* p = start;
        do {
            ENode* a = p->prev;
            ENode* b = p->next->next;
            if (!equals(a, b) && intersects(a, p, p->next, b) && locally_inside(a, b) && locally_inside(b, a)) {
                tris.insert(tris.end(), {a->i, p->i, b->i});
                remove(p);
                remove(p->next);
                p = start = b;
            }
            p = p->next;
        } while (p != start);
        return filter(p);
    }
    void split_earcut(ENode* start) {
        ENode* a = start;
        do {
            ENode* b = a->next->next;
            while (b != a->prev) {
                if (a->i != b->i && valid_diagonal(a, b)) {
                    ENode* c = split(a, b);
                    a = filter(a, a->next);
                    c = filter(c, c->next);
                    run(a, 0);
                    run(c, 0);
                    return;
                }
                b = b->next;
            }
            a = a->next;
        } while (a != start);
    }
    void run(ENode* ear, int pass) {
        if (!ear) return;
        ENode* stop = ear;
        while (ear->prev != ear->next) {
            ENode* prev = ear->prev;
            ENode* next = ear->next;
            if (is_ear(ear)) {
                tris.insert(tris.end(), {prev->i, ear->i, next->i});
                remove(ear);
                ear = next->next;
                stop = next->next;
                continue;
            }
            ear = next;
            if (ear == stop) {
                if (pass == 0) run(filter(ear), 1);
                else if (pass == 1) run(cure(filter(ear)), 2);
                else split_earcut(ear);
                break;
            }
        }
    }
    static ENode* leftmost(ENode* start) {
        ENode* p = start;
        ENode* best = start;
        do {
            if (p->x < best->x || (p->x == best->x && p->y < best->y)) best = p;
            p = p->next;
        } while (p != start);
        return best;
    }
    static bool sector_contains(const ENode* m, const ENode* p) {
        return area(m->prev, m, p->prev) < 0 && area(p->next, m, m->next) < 0;
    }
    static ENode* bridge(ENode* hole, ENode* outer) {
        ENode* p = outer;
        const double hx = hole->x, hy = hole->y;
        double qx = -std::numeric_limits<double>::infinity();
        ENode* m = nullptr;
        do {
            if (hy <= p->y && hy >= p->next->y && p->next->y != p->y) {
                const double x = p->x + (hy - p->y) * (p->next->x - p->x) / (p->next->y - p->y);
                if (x <= hx && x > qx) {
                    qx = x;
                    m = p->x < p->next->x ? p : p->next;
                    if (x == hx) return m;
                }
            }
            p = p->next;
        } while (p != outer);
        if (!m) return nullptr;
        const ENode* stop = m;
        const double mx = m->x, my = m->y;
        double tanMin = std::numeric_limits<double>::infinity();
        p = m;
        do {
            if (hx >= p->x && p->x >= mx && hx != p->x &&
                point_in_triangle(hy < my ? hx : qx, hy, mx, my, hy < my ? qx : hx, hy, p->x, p->y)) {
                const double tan = std::fabs(hy - p->y) / (hx - p->x);
                if (locally_inside(p, hole) &&
                    (tan < tanMin || (tan == tanMin && (p->x > m->x || (p->x == m->x && sector_contains(m, p)))))) {
                    m = p;
                    tanMin = tan;
                }
            }
            p = p->next;
        } while (p != stop);
        return m;
    }
};

}  // namespace

namespace {

/// Split triangles that have another polygon vertex lying on one of their
/// edges. Hole bridges along collinear edges leave such T-junctions, which
/// open hairline cracks against the extruded walls.
void split_t_junctions(std::vector<uint32_t>& tris, const std::vector<Vec2>& pts) {
    const double eps = 1e-9;
    bool changed = true;
    int guard = 0;
    while (changed && guard++ < 64) {
        changed = false;
        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            for (int k = 0; k < 3 && !changed; k++) {
                const uint32_t ia = tris[t + k], ib = tris[t + (k + 1) % 3], ic = tris[t + (k + 2) % 3];
                const Vec2& a = pts[ia];
                const Vec2& b = pts[ib];
                const double ex = b.x - a.x, ey = b.y - a.y, len2 = ex * ex + ey * ey;
                if (len2 < 1e-18) continue;
                for (uint32_t iv = 0; iv < pts.size(); iv++) {
                    if (iv == ia || iv == ib || iv == ic) continue;
                    const Vec2& v = pts[iv];
                    if ((v.x == a.x && v.y == a.y) || (v.x == b.x && v.y == b.y)) continue;
                    const double cross = ex * (v.y - a.y) - ey * (v.x - a.x);
                    if (std::fabs(cross) > eps * std::sqrt(len2)) continue;
                    const double u = (ex * (v.x - a.x) + ey * (v.y - a.y)) / len2;
                    if (u <= 1e-9 || u >= 1 - 1e-9) continue;
                    // (a, b, c) -> (a, v, c) + (v, b, c), same winding.
                    tris[t] = ia;
                    tris[t + 1] = iv;
                    tris[t + 2] = ic;
                    tris.insert(tris.end(), {iv, ib, ic});
                    changed = true;
                    break;
                }
            }
            if (changed) break;
        }
    }
}

}  // namespace

std::vector<uint32_t> triangulate(const std::vector<Vec2>& outer, const std::vector<std::vector<Vec2>>& holes) {
    Earcut e;
    if (outer.size() < 3) return {};
    ENode* outerNode = e.linked(outer, 0, true);
    if (!outerNode || outerNode->next == outerNode->prev) return {};
    uint32_t base = static_cast<uint32_t>(outer.size());
    std::vector<ENode*> queue;
    for (const auto& h : holes) {
        if (h.size() < 3) {
            base += static_cast<uint32_t>(h.size());
            continue;
        }
        ENode* list = e.linked(h, base, false);
        base += static_cast<uint32_t>(h.size());
        if (!list) continue;
        if (list == list->next) list->steiner = true;
        queue.push_back(Earcut::leftmost(list));
    }
    std::sort(queue.begin(), queue.end(), [](const ENode* a, const ENode* b) { return a->x < b->x; });
    for (ENode* hole : queue) {
        ENode* b = Earcut::bridge(hole, outerNode);
        if (!b) continue;
        ENode* rev = e.split(b, hole);
        e.filter(rev, rev->next);
        outerNode = e.filter(b, b->next);
    }
    e.run(outerNode, 0);
    std::vector<Vec2> all = outer;
    for (const auto& h : holes) all.insert(all.end(), h.begin(), h.end());
    split_t_junctions(e.tris, all);
    return e.tris;
}

std::vector<Vec2> rounded_rect(double w, double h, double r, int cornerSeg) {
    r = std::max(0.0, std::min({r, w / 2 - 1e-4, h / 2 - 1e-4}));
    std::vector<Vec2> out;
    const double hx = w / 2, hy = h / 2;
    const Vec2 centres[4] = {{hx - r, -hy + r}, {hx - r, hy - r}, {-hx + r, hy - r}, {-hx + r, -hy + r}};
    for (int c = 0; c < 4; c++) {
        const double a0 = -PI / 2 + c * PI / 2;
        if (r <= 0) {
            out.push_back({centres[c].x, centres[c].y});
            continue;
        }
        for (int s = 0; s <= cornerSeg; s++) {
            const double a = a0 + (PI / 2) * s / cornerSeg;
            out.push_back({centres[c].x + std::cos(a) * r, centres[c].y + std::sin(a) * r});
        }
    }
    return out;  // counter-clockwise
}

std::vector<Vec2> arc_points(double cx, double cy, double r, double a0, double a1, int steps) {
    std::vector<Vec2> out;
    for (int i = 0; i <= steps; i++) {
        const double a = a0 + (a1 - a0) * i / steps;
        out.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
    return out;
}

/* ============================================================= solids == */

namespace {

double signed_area(const std::vector<Vec2>& p) {
    double s = 0;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) s += (p[j].x + p[i].x) * (p[i].y - p[j].y);
    return s / 2;  // > 0 counter-clockwise
}

std::vector<Vec2> clean(std::vector<Vec2> p) {
    std::vector<Vec2> out;
    for (const auto& v : p) {
        if (out.empty() || std::hypot(v.x - out.back().x, v.y - out.back().y) > 1e-7) out.push_back(v);
    }
    while (out.size() > 1 && std::hypot(out.front().x - out.back().x, out.front().y - out.back().y) <= 1e-7) out.pop_back();
    return out;
}

/// Grow a ring outward (to the right of travel) by d, mitred and clamped.
std::vector<Vec2> offset_ring(const std::vector<Vec2>& p, double d) {
    const size_t n = p.size();
    std::vector<Vec2> out(n);
    for (size_t i = 0; i < n; i++) {
        const Vec2& a = p[(i + n - 1) % n];
        const Vec2& b = p[i];
        const Vec2& c = p[(i + 1) % n];
        double e1x = b.x - a.x, e1y = b.y - a.y, e2x = c.x - b.x, e2y = c.y - b.y;
        const double l1 = std::hypot(e1x, e1y), l2 = std::hypot(e2x, e2y);
        e1x /= l1; e1y /= l1; e2x /= l2; e2y /= l2;
        const double n1x = e1y, n1y = -e1x, n2x = e2y, n2y = -e2x;
        double mx = n1x + n2x, my = n1y + n2y;
        const double denom = 1 + (n1x * n2x + n1y * n2y);
        const double k = denom > 0.25 ? 1.0 / denom : 4.0;  // clamp sharp mitres
        out[i] = {b.x + mx * k * d, b.y + my * k * d};
    }
    return out;
}

}  // namespace

Mesh extrude(const std::vector<Vec2>& outerIn, const std::vector<std::vector<Vec2>>& holesIn, double depth, double bevel) {
    std::vector<Vec2> outer = clean(outerIn);
    if (signed_area(outer) < 0) std::reverse(outer.begin(), outer.end());
    std::vector<std::vector<Vec2>> holes;
    for (const auto& h : holesIn) {
        auto c = clean(h);
        if (c.size() < 3) continue;
        if (signed_area(c) > 0) std::reverse(c.begin(), c.end());  // holes clockwise
        holes.push_back(std::move(c));
    }

    Mesh m;
    std::vector<Vec2> flat = outer;
    for (const auto& h : holes) flat.insert(flat.end(), h.begin(), h.end());
    const std::vector<uint32_t> capTris = triangulate(outer, holes);

    // Caps: original outline, pushed out by the bevel thickness.
    const double zBack = -bevel, zFront = depth + bevel;
    auto addCap = [&](double z, bool front) {
        const auto base = static_cast<uint32_t>(m.positions.size());
        for (const auto& v : flat) {
            m.positions.push_back({v.x, v.y, z});
            m.normals.push_back({0, 0, front ? 1.0 : -1.0});
        }
        for (size_t t = 0; t + 2 < capTris.size(); t += 3) {
            if (front) m.indices.insert(m.indices.end(), {base + capTris[t], base + capTris[t + 1], base + capTris[t + 2]});
            else m.indices.insert(m.indices.end(), {base + capTris[t], base + capTris[t + 2], base + capTris[t + 1]});
        }
    };
    addCap(zFront, true);
    addCap(zBack, false);

    // Walls: outline -> grown ring -> grown ring -> outline.
    auto addWalls = [&](const std::vector<Vec2>& ring) {
        std::vector<std::vector<Vec2>> layers;
        std::vector<double> zs;
        if (bevel > 0) {
            const auto grown = offset_ring(ring, bevel);
            layers = {ring, grown, grown, ring};
            zs = {zBack, 0.0, depth, zFront};
        } else {
            layers = {ring, ring};
            zs = {0.0, depth};
        }
        const size_t n = ring.size();
        for (size_t k = 0; k + 1 < layers.size(); k++) {
            for (size_t i = 0; i < n; i++) {
                const size_t j = (i + 1) % n;
                const auto base = static_cast<uint32_t>(m.positions.size());
                const Vec3 a{layers[k][i].x, layers[k][i].y, zs[k]};
                const Vec3 b{layers[k][j].x, layers[k][j].y, zs[k]};
                const Vec3 c{layers[k + 1][j].x, layers[k + 1][j].y, zs[k + 1]};
                const Vec3 d{layers[k + 1][i].x, layers[k + 1][i].y, zs[k + 1]};
                Vec3 fn = (b - a).cross(c - a);
                if (fn.length_sq() < 1e-24) fn = (c - a).cross(d - a);
                fn = fn.normalized();
                for (const Vec3& v : {a, b, c, d}) {
                    m.positions.push_back(v);
                    m.normals.push_back(fn);
                }
                m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }
        }
    };
    addWalls(outer);
    for (const auto& h : holes) addWalls(h);
    m.smooth(35.0);
    return m;
}

Mesh rounded_box(double w, double h, double d, double r, double bevel, int cornerSeg) {
    const double depth = std::max(1e-4, d - bevel * 2);
    Mesh m = extrude(rounded_rect(w, h, r, cornerSeg), {}, depth, bevel);
    m.translate(0, 0, -depth / 2);
    return m;
}

Mesh cylinder(double rt, double rb, double height, int radial, bool capped) {
    Mesh m;
    const double hh = height / 2;
    const double slope = (rb - rt) / height;
    for (int y = 0; y <= 1; y++) {
        const double r = y == 0 ? rt : rb;
        const double py = y == 0 ? hh : -hh;
        for (int x = 0; x <= radial; x++) {
            const double t = static_cast<double>(x) / radial * TAU;
            const double s = std::sin(t), c = std::cos(t);
            m.positions.push_back({r * s, py, r * c});
            m.normals.push_back(Vec3{s, slope, c}.normalized());
        }
    }
    const auto row = static_cast<uint32_t>(radial + 1);
    for (uint32_t x = 0; x < static_cast<uint32_t>(radial); x++) {
        const uint32_t a = x, b = row + x, c = row + x + 1, d = x + 1;
        m.indices.insert(m.indices.end(), {a, b, d, b, c, d});
    }
    m.drop_degenerate();  // a cone's apex quads collapse to one triangle each
    if (capped) {
        for (int top = 1; top >= 0; top--) {
            const double r = top ? rt : rb;
            if (r <= 1e-9) continue;
            const double py = top ? hh : -hh;
            const auto centre = static_cast<uint32_t>(m.positions.size());
            m.positions.push_back({0, py, 0});
            m.normals.push_back({0, top ? 1.0 : -1.0, 0});
            for (int x = 0; x <= radial; x++) {
                const double t = static_cast<double>(x) / radial * TAU;
                m.positions.push_back({r * std::sin(t), py, r * std::cos(t)});
                m.normals.push_back({0, top ? 1.0 : -1.0, 0});
            }
            for (uint32_t x = 0; x < static_cast<uint32_t>(radial); x++) {
                if (top) m.indices.insert(m.indices.end(), {centre, centre + 1 + x, centre + 2 + x});
                else m.indices.insert(m.indices.end(), {centre, centre + 2 + x, centre + 1 + x});
            }
        }
    }
    return m;
}

Mesh sphere(double radius, int widthSeg, int heightSeg) {
    Mesh m;
    for (int iy = 0; iy <= heightSeg; iy++) {
        const double v = static_cast<double>(iy) / heightSeg;
        for (int ix = 0; ix <= widthSeg; ix++) {
            const double u = static_cast<double>(ix) / widthSeg;
            const Vec3 n{-std::cos(u * TAU) * std::sin(v * PI), std::cos(v * PI), std::sin(u * TAU) * std::sin(v * PI)};
            m.positions.push_back(n * radius);
            m.normals.push_back(n);
        }
    }
    const auto row = static_cast<uint32_t>(widthSeg + 1);
    for (uint32_t iy = 0; iy < static_cast<uint32_t>(heightSeg); iy++) {
        for (uint32_t ix = 0; ix < static_cast<uint32_t>(widthSeg); ix++) {
            const uint32_t a = iy * row + ix + 1, b = iy * row + ix, c = (iy + 1) * row + ix, d = (iy + 1) * row + ix + 1;
            if (iy != 0) m.indices.insert(m.indices.end(), {a, b, d});
            if (iy != static_cast<uint32_t>(heightSeg) - 1) m.indices.insert(m.indices.end(), {b, c, d});
        }
    }
    return m;
}

Mesh torus(double radius, double tubeR, int radialSeg, int tubularSeg, double arc) {
    Mesh m;
    for (int j = 0; j <= radialSeg; j++) {
        for (int i = 0; i <= tubularSeg; i++) {
            const double u = static_cast<double>(i) / tubularSeg * arc;
            const double v = static_cast<double>(j) / radialSeg * TAU;
            const Vec3 centre{radius * std::cos(u), radius * std::sin(u), 0};
            const Vec3 p{(radius + tubeR * std::cos(v)) * std::cos(u), (radius + tubeR * std::cos(v)) * std::sin(u),
                         tubeR * std::sin(v)};
            m.positions.push_back(p);
            m.normals.push_back((p - centre).normalized());
        }
    }
    const auto row = static_cast<uint32_t>(tubularSeg + 1);
    for (uint32_t j = 1; j <= static_cast<uint32_t>(radialSeg); j++) {
        for (uint32_t i = 1; i <= static_cast<uint32_t>(tubularSeg); i++) {
            const uint32_t a = row * j + i - 1, b = row * (j - 1) + i - 1, c = row * (j - 1) + i, d = row * j + i;
            m.indices.insert(m.indices.end(), {a, b, d, b, c, d});
        }
    }
    return m;
}

Mesh lathe(const std::vector<Vec2>& profile, int segments) {
    Mesh m;
    const size_t n = profile.size();
    for (int s = 0; s <= segments; s++) {
        const double t = static_cast<double>(s) / segments * TAU;
        const double sn = std::sin(t), cs = std::cos(t);
        for (const auto& p : profile) {
            const double r = std::max(0.0, p.x);  // exact 0 at poles: those triangles collapse and are dropped
            m.positions.push_back({r * sn, p.y, r * cs});
            m.normals.push_back({0, 1, 0});
        }
    }
    for (uint32_t s = 0; s < static_cast<uint32_t>(segments); s++) {
        for (uint32_t j = 0; j + 1 < n; j++) {
            const uint32_t base = s * static_cast<uint32_t>(n) + j;
            const uint32_t a = base, b = base + static_cast<uint32_t>(n), c = base + static_cast<uint32_t>(n) + 1, d = base + 1;
            m.indices.insert(m.indices.end(), {a, b, d, b, c, d});
        }
    }
    m.drop_degenerate();
    // Orient outward: the reference's profiles may run either way along Y.
    double outward = 0;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const Vec3& a = m.positions[m.indices[t]];
        const Vec3 fn = (m.positions[m.indices[t + 1]] - a).cross(m.positions[m.indices[t + 2]] - a);
        outward += fn.dot(Vec3{a.x, 0, a.z});
    }
    if (outward < 0) {
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) std::swap(m.indices[t + 1], m.indices[t + 2]);
    }
    m.smooth(50.0);
    return m;
}

namespace {

/// Centripetal Catmull-Rom sampled by arc length.
std::vector<Vec3> catmull_rom(const std::vector<Vec3>& pts, bool closed, int samples) {
    const size_t n = pts.size();
    auto P = [&](long i) -> Vec3 {
        if (closed) return pts[static_cast<size_t>((i % static_cast<long>(n) + static_cast<long>(n)) % static_cast<long>(n))];
        if (i < 0) return pts[0] * 2 - pts[1];
        if (i >= static_cast<long>(n)) return pts[n - 1] * 2 - pts[n - 2];
        return pts[static_cast<size_t>(i)];
    };
    const long segs = closed ? static_cast<long>(n) : static_cast<long>(n) - 1;
    std::vector<Vec3> dense;
    const int per = 24;
    for (long s = 0; s < segs; s++) {
        const Vec3 p0 = P(s - 1), p1 = P(s), p2 = P(s + 1), p3 = P(s + 2);
        double dt0 = std::pow((p1 - p0).length_sq(), 0.25), dt1 = std::pow((p2 - p1).length_sq(), 0.25),
               dt2 = std::pow((p3 - p2).length_sq(), 0.25);
        if (dt1 < 1e-4) dt1 = 1;
        if (dt0 < 1e-4) dt0 = dt1;
        if (dt2 < 1e-4) dt2 = dt1;
        // Non-uniform Catmull-Rom as a cubic Hermite on [0, 1].
        auto hermite = [&](double a0, double a1, double a2, double a3) {
            double t1 = (a1 - a0) / dt0 - (a2 - a0) / (dt0 + dt1) + (a2 - a1) / dt1;
            double t2 = (a2 - a1) / dt1 - (a3 - a1) / (dt1 + dt2) + (a3 - a2) / dt2;
            t1 *= dt1;
            t2 *= dt1;
            return std::array<double, 4>{a1, t1, -3 * a1 + 3 * a2 - 2 * t1 - t2, 2 * a1 - 2 * a2 + t1 + t2};
        };
        const auto cx = hermite(p0.x, p1.x, p2.x, p3.x), cy = hermite(p0.y, p1.y, p2.y, p3.y),
                   cz = hermite(p0.z, p1.z, p2.z, p3.z);
        for (int k = 0; k < per; k++) {
            const double t = static_cast<double>(k) / per;
            auto ev = [&](const std::array<double, 4>& c) { return c[0] + c[1] * t + c[2] * t * t + c[3] * t * t * t; };
            dense.push_back({ev(cx), ev(cy), ev(cz)});
        }
    }
    dense.push_back(closed ? pts[0] : pts[n - 1]);
    std::vector<double> len(dense.size(), 0);
    for (size_t i = 1; i < dense.size(); i++) len[i] = len[i - 1] + (dense[i] - dense[i - 1]).length();
    std::vector<Vec3> out;
    size_t j = 1;
    for (int i = 0; i <= samples; i++) {
        const double target = len.back() * i / samples;
        while (j + 1 < dense.size() && len[j] < target) j++;
        const double seg = len[j] - len[j - 1];
        const double t = seg > 1e-12 ? (target - len[j - 1]) / seg : 0;
        out.push_back(dense[j - 1] + (dense[j] - dense[j - 1]) * t);
    }
    return out;
}

Mesh sweep(const std::vector<Vec3>& path, double radius, int radialSeg, bool closed, bool caps) {
    Mesh m;
    const size_t n = path.size();
    std::vector<Vec3> tangents(n);
    for (size_t i = 0; i < n; i++) {
        const Vec3 a = i > 0 ? path[i - 1] : (closed ? path[n - 2] : path[0]);
        const Vec3 b = i + 1 < n ? path[i + 1] : (closed ? path[1] : path[n - 1]);
        tangents[i] = (b - a).normalized();
    }
    // Parallel-transport frames: no twist, no Frenet flips on straight runs.
    Vec3 normal = any_perpendicular(tangents[0]);
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            const Vec3 axis = tangents[i - 1].cross(tangents[i]);
            const double s = axis.length();
            if (s > 1e-9) {
                const double ang = std::atan2(s, std::clamp(tangents[i - 1].dot(tangents[i]), -1.0, 1.0));
                normal = Quat::axis_angle(axis * (1 / s), ang).rotate(normal);
            }
            normal = (normal - tangents[i] * normal.dot(tangents[i])).normalized();
        }
        const Vec3 binormal = tangents[i].cross(normal);
        for (int r = 0; r <= radialSeg; r++) {
            const double v = static_cast<double>(r) / radialSeg * TAU;
            const Vec3 dir = normal * std::cos(v) + binormal * std::sin(v);
            m.positions.push_back(path[i] + dir * radius);
            m.normals.push_back(dir);
        }
    }
    const auto row = static_cast<uint32_t>(radialSeg + 1);
    for (uint32_t i = 0; i + 1 < n; i++) {
        for (uint32_t r = 0; r < static_cast<uint32_t>(radialSeg); r++) {
            const uint32_t a = i * row + r, b = (i + 1) * row + r, c = (i + 1) * row + r + 1, d = i * row + r + 1;
            m.indices.insert(m.indices.end(), {a, d, b, b, d, c});
        }
    }
    if (caps && !closed) {
        for (int end = 0; end < 2; end++) {
            const size_t i = end ? n - 1 : 0;
            const Vec3 nrm = end ? tangents[i] : tangents[i] * -1;
            const auto centre = static_cast<uint32_t>(m.positions.size());
            m.positions.push_back(path[i]);
            m.normals.push_back(nrm);
            for (uint32_t r = 0; r <= static_cast<uint32_t>(radialSeg); r++) {
                m.positions.push_back(m.positions[i * row + r]);
                m.normals.push_back(nrm);
            }
            for (uint32_t r = 0; r < static_cast<uint32_t>(radialSeg); r++) {
                if (end) m.indices.insert(m.indices.end(), {centre, centre + 1 + r, centre + 2 + r});
                else m.indices.insert(m.indices.end(), {centre, centre + 2 + r, centre + 1 + r});
            }
        }
    }
    // Make sure the tube faces outward whichever way the ring winds.
    if (!m.indices.empty()) {
        const Vec3& a = m.positions[m.indices[0]];
        const Vec3 fn = (m.positions[m.indices[1]] - a).cross(m.positions[m.indices[2]] - a);
        if (fn.dot(m.normals[m.indices[0]]) < 0) {
            for (size_t t = 0; t + 2 < m.indices.size(); t += 3) std::swap(m.indices[t + 1], m.indices[t + 2]);
        }
    }
    return m;
}

}  // namespace

Mesh tube(const std::vector<Vec3>& points, double radius, int radialSeg, bool closed, bool caps) {
    if (points.size() < 2) return {};
    double length = 0;
    for (size_t i = 1; i < points.size(); i++) length += (points[i] - points[i - 1]).length();
    const int samples = std::clamp(static_cast<int>(std::lround(length / radius * 1.4)), 12, 220);
    std::vector<Vec3> path = points.size() == 2 ? std::vector<Vec3>{points[0], points[1]} : catmull_rom(points, closed, samples);
    return sweep(path, radius, radialSeg, closed, caps);
}

Mesh rod(const Vec3& a, const Vec3& b, double radius, int radialSeg) {
    const Vec3 d = b - a;
    const double len = d.length();
    Mesh m = cylinder(radius, radius, len, radialSeg, true);
    const Vec3 dir = d * (1 / std::max(1e-9, len));
    // Rotate +Y onto dir.
    const double r = dir.y + 1;
    Quat q = r < 1e-9 ? Quat{1, 0, 0, 0} : Quat{dir.z, 0, -dir.x, r};
    const double ql = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    q = {q.x / ql, q.y / ql, q.z / ql, q.w / ql};
    m.rotate(q);
    const Vec3 mid = (a + b) * 0.5;
    m.translate(mid.x, mid.y, mid.z);
    return m;
}

Mesh coil_spring(double radius, double length, int coils, double wire, int radialSeg) {
    std::vector<Vec3> path;
    const int steps = coils * 14;
    for (int i = 0; i <= steps; i++) {
        const double t = static_cast<double>(i) / steps;
        const double a = t * coils * TAU;
        const double ease = 0.82 + 0.18 * std::sin(PI * t);
        path.push_back({std::cos(a) * radius * ease, t * length, std::sin(a) * radius * ease});
    }
    return sweep(path, wire, radialSeg, false, false);
}

Mesh quad(double w, double h) {
    Mesh m;
    const double x = w / 2, y = h / 2;
    for (int side = 0; side < 2; side++) {
        const double nz = side ? -1 : 1;
        const auto base = static_cast<uint32_t>(m.positions.size());
        for (const Vec3& p : {Vec3{-x, -y, 0}, Vec3{x, -y, 0}, Vec3{x, y, 0}, Vec3{-x, y, 0}}) {
            m.positions.push_back(p);
            m.normals.push_back({0, 0, nz});
        }
        if (side == 0) m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        else m.indices.insert(m.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
    return m;
}

/* ========================================================= PaintedMesh == */

void PaintedMesh::add(const Mesh& m, const Look& look) {
    const auto base = static_cast<uint32_t>(positions.size());
    positions.insert(positions.end(), m.positions.begin(), m.positions.end());
    normals.insert(normals.end(), m.normals.begin(), m.normals.end());
    looks.insert(looks.end(), m.positions.size(), look);
    for (uint32_t i : m.indices) indices.push_back(base + i);
}

void PaintedMesh::add(const PaintedMesh& o) {
    const auto base = static_cast<uint32_t>(positions.size());
    positions.insert(positions.end(), o.positions.begin(), o.positions.end());
    normals.insert(normals.end(), o.normals.begin(), o.normals.end());
    looks.insert(looks.end(), o.looks.begin(), o.looks.end());
    for (uint32_t i : o.indices) indices.push_back(base + i);
}

std::array<PaintedMesh, 4> PaintedMesh::by_layer() const {
    std::array<PaintedMesh, 4> out;
    std::array<std::unordered_map<uint32_t, uint32_t>, 4> remap;
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        const uint8_t layer = std::min<uint8_t>(3, looks[indices[t]].layer);
        auto& dst = out[layer];
        for (int k = 0; k < 3; k++) {
            const uint32_t src = indices[t + k];
            auto it = remap[layer].find(src);
            uint32_t idx;
            if (it == remap[layer].end()) {
                idx = static_cast<uint32_t>(dst.positions.size());
                dst.positions.push_back(positions[src]);
                dst.normals.push_back(normals[src]);
                dst.looks.push_back(looks[src]);
                remap[layer][src] = idx;
            } else {
                idx = it->second;
            }
            dst.indices.push_back(idx);
        }
    }
    return out;
}

}  // namespace worldcore
