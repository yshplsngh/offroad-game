// vehicle_design_json.hpp - VehicleDesign from native/godot/data/vehicles.json
// (shared by the baker and the tests).
#pragma once

#include <cstdlib>
#include <string>
#include <vector>

#include "json.hpp"
#include "worldcore/vehicle_mesh.hpp"

inline uint32_t design_color(const json::Value& v, uint32_t fallback) {
    if (v.kind != json::Value::String || v.s.size() != 7 || v.s[0] != '#') return fallback;
    return static_cast<uint32_t>(std::strtoul(v.s.c_str() + 1, nullptr, 16));
}

inline worldcore::VehicleDesign design_from_json(const json::Value& v) {
    worldcore::VehicleDesign d;
    auto num = [](const json::Value& o, const char* k, double f) { return o.has(k) ? o[k].num() : f; };
    auto flag = [](const json::Value& o, const char* k, bool f) { return o.has(k) ? o[k].b : f; };
    d.id = v["id"].s;
    d.name = v["name"].s;
    d.rhd = flag(v, "rhd", false);
    const auto& fr = v["frame"];
    d.frame = {num(fr, "wheelbase", 2.5), num(fr, "frontOverhang", 0.7), num(fr, "rearOverhang", 0.8),
               num(fr, "frameWidth", 0.98), num(fr, "railY", 0.5), num(fr, "railHeight", 0.16)};
    const auto& ax = v["axle"];
    d.axle = {num(ax, "track", 1.6), num(ax, "tubeR", 0.055), num(ax, "diffOffset", 0.18)};
    const auto& t = v["tire"];
    d.tire = {num(t, "diameter", 0.889), num(t, "width", 0.318), static_cast<int>(num(t, "rimInch", 17)),
              static_cast<int>(num(t, "spokes", 6)), t.has("rimColor") ? design_color(t["rimColor"], 0x2b2e33) : 0x2b2e33,
              flag(t, "beadlock", true)};
    const auto& su = v["suspension"];
    d.suspension = {num(su, "travel", 0.26), su.has("shockColor") ? design_color(su["shockColor"], 0xc23a2a) : 0xc23a2a};
    const auto& b = v["body"];
    d.body.pickup = b.has("style") && b["style"].s == "pickup";
    d.body.doors = static_cast<int>(num(b, "doors", 2));
    d.body.width = num(b, "width", 1.8);
    d.body.sideHeight = num(b, "sideHeight", 0.6);
    d.body.glassHeight = num(b, "glassHeight", 0.46);
    d.body.hoodDrop = num(b, "hoodDrop", 0.1);
    d.body.cowlSetback = num(b, "cowlSetback", 0.3);
    d.body.archClearance = num(b, "archClearance", 0.075);
    d.body.windshieldRake = num(b, "windshieldRake", 0.34);
    d.body.headlampR = num(b, "headlampR", 0.1);
    d.body.glassTint = num(b, "glassTint", 0.12);
    d.body.steeringR = num(b, "steeringR", 0.185);
    d.body.matte = flag(b, "matte", false);
    d.body.color = b.has("color") ? design_color(b["color"], 0x3f5f4a) : 0x3f5f4a;
    const auto& f = v["features"];
    d.features = {flag(f, "cage", true), flag(f, "winch", true), flag(f, "snorkel", false), flag(f, "lightBar", true),
                  flag(f, "roofRack", true), flag(f, "spare", true), flag(f, "flares", true), flag(f, "sliders", true),
                  flag(f, "rearBumper", true), flag(f, "kit", true)};
    return d;
}

inline std::vector<worldcore::VehicleDesign> designs_from_json(const json::Value& vehicles) {
    std::vector<worldcore::VehicleDesign> out;
    for (const auto& v : vehicles["vehicles"].a) out.push_back(design_from_json(v));
    return out;
}
