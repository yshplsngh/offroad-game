// vehicle_spec_json.hpp - VehicleSpec from native/godot/data/vehicles.json.
#pragma once

#include <stdexcept>
#include <string>

#include "json.hpp"
#include "worldcore/vehicle.hpp"

inline worldcore::VehicleSpec vehicle_spec_from_json(const json::Value& vehicles, const std::string& id) {
    for (const auto& v : vehicles["vehicles"].a) {
        if (v["id"].s != id) continue;
        worldcore::VehicleSpec s;
        s.id = id;
        const auto& p = v["perf"];
        s.perf.mass = p["mass"].n;
        s.perf.power = p["power"].n;
        s.perf.torque = p["torque"].n;
        s.perf.topSpeed = p["topSpeed"].n;
        s.perf.lowRangeRatio = p["lowRangeRatio"].n;
        s.perf.forwardGears = static_cast<int>(p["gearRatios"].size());
        for (size_t i = 0; i < p["gearRatios"].size(); i++) s.perf.gearRatios.at(i) = p["gearRatios"][i].n;
        s.perf.finalDrive = p["finalDrive"].n;
        s.perf.brakeTorque = p["brakeTorque"].n;
        s.wheelbase = v["frame"]["wheelbase"].n;
        s.track = v["axle"]["track"].n;
        s.suspensionTravel = v["suspension"]["travel"].n;
        const auto& ph = v["physics"];
        s.wheelRadius = ph["wheelRadius"].n;
        s.sillY = ph["sillY"].n;
        s.zFront = ph["zFront"].n;
        const auto& box = ph["chassisBox"];
        s.boxHalf = {box["halfExtents"][0].n, box["halfExtents"][1].n, box["halfExtents"][2].n};
        s.boxCentre = {box["centre"][0].n, box["centre"][1].n, box["centre"][2].n};
        return s;
    }
    throw std::runtime_error("vehicles.json: no vehicle " + id);
}
