#include "worldcore/vegetation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace worldcore {

FloraExtents flora_extents(const std::vector<FloraPrototype>& set) {
    FloraExtents out;
    for (const auto& p : set) {
        auto& list = out[p.species][static_cast<size_t>(p.detail)];
        if (list.size() <= static_cast<size_t>(p.variant)) list.resize(static_cast<size_t>(p.variant) + 1);
        PrototypeExtent e{0, std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
        const auto& pos = p.mesh.positions;
        for (size_t i = 0; i + 2 < pos.size(); i += 3) {
            e.radiusXZ = std::max(e.radiusXZ, std::hypot(pos[i], pos[i + 2]));
            e.minY = std::min(e.minY, pos[i + 1]);
            e.maxY = std::max(e.maxY, pos[i + 1]);
        }
        list[static_cast<size_t>(p.variant)] = e;
    }
    return out;
}

size_t VegetationCell::instances() const {
    size_t n = 0;
    for (const auto& b : batches) n += b.count;
    return n;
}

int max_far_lod(Species s) {
    if (kSpecies[s].groundCover) return 0;
    if (s == SPECIES_DEADFALL) return 1;
    return 2;
}

VegetationCell build_vegetation_cell(const Field& field, int32_t seed, int32_t cx, int32_t cz, int lod,
                                     const FloraExtents& extents, double density) {
    VegetationCell cell;
    cell.cx = cx;
    cell.cz = cz;
    lod = std::clamp(lod, 0, 2);
    const int detail = 2 - lod;
    cell.detail = detail;

    const CellPlacement placed = place_cell(field, seed, cx, cz, density);
    // batch index by (species, variant) for this detail
    std::array<std::vector<int>, SPECIES_COUNT> index;

    for (const Placement& p : placed.items) {
        if (lod > max_far_lod(p.species)) continue;
        const auto& variants = extents[p.species][static_cast<size_t>(detail)];
        if (variants.empty()) continue;
        // Placement picks among the near-detail variants; coarser LODs have fewer.
        const int variant = p.variant % static_cast<int>(variants.size());

        auto& slots = index[p.species];
        if (slots.size() <= static_cast<size_t>(variant)) slots.resize(static_cast<size_t>(variant) + 1, -1);
        if (slots[static_cast<size_t>(variant)] < 0) {
            slots[static_cast<size_t>(variant)] = static_cast<int>(cell.batches.size());
            VegetationBatch b;
            b.species = p.species;
            b.detail = detail;
            b.variant = variant;
            b.aabbMin = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
            b.aabbMax = {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
            cell.batches.push_back(std::move(b));
        }
        VegetationBatch& b = cell.batches[static_cast<size_t>(slots[static_cast<size_t>(variant)])];

        const Quat& q = p.rotation;
        const double s = p.scale;
        const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        const float row[12] = {
            static_cast<float>((1 - 2 * (yy + zz)) * s), static_cast<float>(2 * (xy - wz) * s),
            static_cast<float>(2 * (xz + wy) * s), static_cast<float>(p.position.x),
            static_cast<float>(2 * (xy + wz) * s), static_cast<float>((1 - 2 * (xx + zz)) * s),
            static_cast<float>(2 * (yz - wx) * s), static_cast<float>(p.position.y),
            static_cast<float>(2 * (xz - wy) * s), static_cast<float>(2 * (yz + wx) * s),
            static_cast<float>((1 - 2 * (xx + yy)) * s), static_cast<float>(p.position.z),
        };
        b.buffer.insert(b.buffer.end(), row, row + 12);
        b.buffer.insert(b.buffer.end(), {p.tint[0], p.tint[1], p.tint[2], 1.0f});
        b.count++;

        // Exact conservative bounds: the prototype's local box (its bounding
        // cylinder squared off) through this instance's full transform.
        const PrototypeExtent& e = variants[static_cast<size_t>(variant)];
        for (int corner = 0; corner < 8; corner++) {
            const double lx = (corner & 1) ? e.radiusXZ : -e.radiusXZ;
            const double ly = (corner & 2) ? e.maxY : e.minY;
            const double lz = (corner & 4) ? e.radiusXZ : -e.radiusXZ;
            const Vec3 w{row[0] * lx + row[1] * ly + row[2] * lz + row[3], row[4] * lx + row[5] * ly + row[6] * lz + row[7],
                         row[8] * lx + row[9] * ly + row[10] * lz + row[11]};
            b.aabbMin = {std::min(b.aabbMin.x, w.x), std::min(b.aabbMin.y, w.y), std::min(b.aabbMin.z, w.z)};
            b.aabbMax = {std::max(b.aabbMax.x, w.x), std::max(b.aabbMax.y, w.y), std::max(b.aabbMax.z, w.z)};
        }
    }
    return cell;
}

}  // namespace worldcore
