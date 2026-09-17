// ground_texture.hpp - procedural, tileable grass detail for the terrain shader.
//
// No downloaded or hand-painted textures: the baker generates these once at
// build time (worldcore_bake -> generated/ground.bin) from anti-aliased blade
// strokes over wrapped value noise.
//
//   albedo: RGB detail MULTIPLIER around 1.0 (byte 128 = x1.0), so the
//           terrain classifier's colour stays in charge of hue per biome
//   normal: tangent-space normal map from the blade height field (+Z up)
//
// Both tile seamlessly in X and Y.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace worldcore {

struct GroundTextures {
    int size = 0;
    std::vector<uint8_t> albedo;  // RGBA8, size * size
    std::vector<uint8_t> normal;  // RGBA8, size * size
};

GroundTextures build_ground_textures(int size = 512, uint32_t seed = 1337);

std::vector<uint8_t> serialize_ground(const GroundTextures& t);
bool deserialize_ground(const std::vector<uint8_t>& bytes, GroundTextures& out, std::string* error);

}  // namespace worldcore
