// tune.hpp - the feel tables: SURFACE (what the ground is like) and TUNE (what
// the truck is like), plus the physics-facing part of a vehicle spec.
//
// Defaults were ported from the browser reference's SURFACE and TUNE tables
// (git a59773d). native/godot/data/*.json is now the source of truth: the test
// suite compares every field against it, so a retune must change both.
#pragma once

#include <array>
#include <string>

#include "worldcore/world.hpp"

namespace worldcore {

struct SurfaceInfo {
    int id;
    const char* name;
    double friction, drag, sink;
    bool deform;
    double dust;
};

inline constexpr std::array<SurfaceInfo, SURFACE_COUNT> kSurfaces{{
    {SURFACE_ROCK, "rock", 1.05, 0.012, 0.00, false, 0.15},
    {SURFACE_GRAVEL, "gravel", 0.82, 0.030, 0.02, true, 0.75},
    {SURFACE_DIRT, "dirt", 0.88, 0.026, 0.02, true, 0.85},
    {SURFACE_GRASS, "grass", 0.76, 0.034, 0.03, true, 0.20},
    {SURFACE_LOAM, "loam", 0.70, 0.055, 0.06, true, 0.30},
    {SURFACE_MUD, "mud", 0.38, 0.145, 0.16, true, 0.00},
    {SURFACE_WATER, "water", 0.30, 0.190, 0.10, false, 0.00},
    {SURFACE_SNOW, "snow", 0.45, 0.090, 0.10, true, 0.00},
}};

inline const SurfaceInfo& surface_info(int id) {
    return kSurfaces[static_cast<size_t>(id >= 0 && id < SURFACE_COUNT ? id : SURFACE_DIRT)];
}

struct Tune {
    // mass
    double comAboveSill = 0.05;
    double comRearBias = 0.05;
    double inertiaRoll = 0.92;
    double inertiaPitch = 1.18;
    double inertiaYaw = 1.22;
    double bodyLinearDamping = 0.02;
    double bodyAngularDamping = 0.10;
    double aeroDrag = 0.62 * 3.4 * 0.5 * 1.225;
    // suspension
    double staticSag = 0.32;
    double dampBump = 0.30;
    double dampRebound = 0.52;
    double bumpStopStart = 0.86;
    double bumpStopRate = 26;
    double maxSpringForceG = 6.0;
    double antiRollFront = 0.10;
    double antiRollRear = 0.06;
    // tire
    double peakSlipRatio = 0.16;
    double peakSlipAngle = 0.17;
    double slipFalloff = 0.030;
    double loadSensitivity = 0.12;
    double wetnessGripLoss = 0.35;
    double gripScale = 1.0;
    double slipRefSpeed = 1.6;
    double tireForceHeight = 0.25;
    double rollingResistScale = 1.0;
    double wheelInertiaFactor = 0.62;
    double wheelMassFraction = 0.028;
    // mud
    double sinkScale = 1.0;
    double sinkRate = 3.2;
    double bogDrag = 5200;
    double mudCakeRate = 0.55;
    double mudCleanRate = 0.05;
    // drivetrain
    double idleRpm = 780;
    double stallRpm = 380;
    double redlineRpm = 5000;
    double peakTorqueRpm = 2700;
    double engineInertia = 0.42;
    double engineFrictionA = 12;
    double engineFrictionB = 0.035;
    double clutchCapacity = 1.45;
    double clutchCreep = 0.10;
    double drivelineEff = 0.86;
    double reverseRatio = 1.05;
    double shiftTime = 0.28;
    double brakeBiasFront = 0.62;
    double brakeScale = 2.4;
    double handbrakeFraction = 1.3;
    double lockerStrength = 1.0;
    double openDiffPreload = 3.0;
    // steering
    double maxSteerAngle = 0.62;
    double steerSpeedFalloff = 18;
    double steerMinFraction = 0.34;
    double steerRate = 2.0;
    double steerReturnRate = 2.6;
    double ackermann = 0.85;
    // recovery
    double winchForce = 42000;
    double winchSpeed = 0.55;
    double winchRange = 28;
    double winchStiffness = 90000;
    double flipCooldown = 4.0;
    double flipLift = 0.45;
    // stuckness
    double stuckSpeed = 0.7;
    double stuckRate = 0.55;
};

/// X-macro over every Tune field, for loaders and the data-drift test.
#define WORLDCORE_TUNE_FIELDS(X)                                                              \
    X(comAboveSill) X(comRearBias) X(inertiaRoll) X(inertiaPitch) X(inertiaYaw)               \
    X(bodyLinearDamping) X(bodyAngularDamping) X(aeroDrag) X(staticSag) X(dampBump)           \
    X(dampRebound) X(bumpStopStart) X(bumpStopRate) X(maxSpringForceG) X(antiRollFront)       \
    X(antiRollRear) X(peakSlipRatio) X(peakSlipAngle) X(slipFalloff) X(loadSensitivity)       \
    X(wetnessGripLoss) X(gripScale) X(slipRefSpeed) X(tireForceHeight) X(rollingResistScale)  \
    X(wheelInertiaFactor) X(wheelMassFraction) X(sinkScale) X(sinkRate) X(bogDrag)            \
    X(mudCakeRate) X(mudCleanRate) X(idleRpm) X(stallRpm) X(redlineRpm) X(peakTorqueRpm)      \
    X(engineInertia) X(engineFrictionA) X(engineFrictionB) X(clutchCapacity) X(clutchCreep)   \
    X(drivelineEff) X(reverseRatio) X(shiftTime) X(brakeBiasFront) X(brakeScale)              \
    X(handbrakeFraction) X(lockerStrength) X(openDiffPreload) X(maxSteerAngle)                \
    X(steerSpeedFalloff) X(steerMinFraction) X(steerRate) X(steerReturnRate) X(ackermann)     \
    X(winchForce) X(winchSpeed) X(winchRange) X(winchStiffness) X(flipCooldown) X(flipLift)   \
    X(stuckSpeed) X(stuckRate)

/// spec.perf from catalog.js - what the drivetrain reads.
struct VehiclePerf {
    double mass = 2100;
    double power = 230;  // kW
    double torque = 420;  // N m
    double topSpeed = 38;
    double lowRangeRatio = 2.72;
    std::array<double, 5> gearRatios{3.6, 2.1, 1.4, 1.0, 0.78};
    int forwardGears = 5;
    double finalDrive = 4.56;
    double brakeTorque = 3600;
};

}  // namespace worldcore
