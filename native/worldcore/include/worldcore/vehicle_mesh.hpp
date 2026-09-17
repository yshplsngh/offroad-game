// vehicle_mesh.hpp - procedural vehicle geometry for the build-time baker.
//
// Port of the browser reference's vehicle builders (git a59773d:
// src/vehicles/{body,chassis,wheel,interior,accessories,vehicle}.js). No
// purchased models: every panel, link, lug and bolt comes from the catalog spec
// in native/godot/data/vehicles.json.
//
// For frame rate, everything static merges into ONE mesh per LOD (split only
// by material layer: opaque, paint, glass, lamp lens). Only parts that really
// move are separate: wheels (spin), brakes (steer), axle housings (travel and
// roll), suspension links and coilovers (aimed each frame), steering wheel.
//
// Coordinates: vehicle origin at ground level centred between the axles,
// +Y up, +Z forward. As in the reference, +X is the left-hand-drive driver's
// side. Wheel `side` -1 is the physics FL/RL wheel (x = -track/2).
//
// Look::emission carries the lamp kind on lens vertices (layer 3) and the
// opacity on glass vertices (layer 2).
#pragma once

#include <array>
#include <string>
#include <vector>

#include "worldcore/meshgen.hpp"

namespace worldcore {

/// Everything the builders read from a catalog entry.
struct VehicleDesign {
    std::string id, name;
    bool rhd = false;
    struct {
        double wheelbase, frontOverhang, rearOverhang, frameWidth, railY, railHeight;
    } frame{};
    struct {
        double track, tubeR, diffOffset;
    } axle{};
    struct {
        double diameter, width;
        int rimInch, spokes;
        uint32_t rimColor;
        bool beadlock;
    } tire{};
    struct {
        double travel;
        uint32_t shockColor;
    } suspension{};
    struct {
        bool pickup;
        int doors;
        double width, sideHeight, glassHeight, hoodDrop, cowlSetback, archClearance, windshieldRake, headlampR,
            glassTint, steeringR;
        bool matte;
        uint32_t color;
    } body{};
    struct {
        bool cage, winch, snorkel, lightBar, roofRack, spare, flares, sliders, rearBumper, kit;
    } features{};
};

/// body.js bodyMetrics(): every body landmark, derived from the frame and tire.
struct BodyMetrics {
    double sillY, beltY, hoodY, roofY, zFront, zRear, zCowl, zCabRear, halfW, archY, archR, archHalf, frontAxleZ,
        rearAxleZ, rake;
};
BodyMetrics body_metrics(const VehicleDesign& d);

/// Lamp kinds encoded in Look::emission for lens vertices.
enum LampKind : int { LAMP_HEAD = 1, LAMP_TAIL = 2, LAMP_INDICATOR = 3, LAMP_AUX = 4 };

struct LinkRig {
    int axle;     // 0 front, 1 rear
    int part;     // 0 lower arm, 1 upper arm, 2 panhard bar
    Vec3 pivot;   // chassis end, vehicle space
    Vec3 target;  // axle end, axle-local (relative to the axle's rest centre)
};

struct ShockRig {
    int axle;
    Vec3 mount;   // chassis top eye, vehicle space
    Vec3 anchor;  // axle eye, axle-local
};

/// How the moving parts attach and animate.
struct VehicleRig {
    double wheelRadius = 0;
    double track = 0;
    std::array<double, 2> axleZ{};  // front, rear
    double axleY = 0;               // rest height of both axle centres
    std::vector<LinkRig> links;
    std::vector<ShockRig> shocks;
    Vec3 steeringPos;               // steering wheel hub, vehicle space
    double steeringTilt = 0;        // rotation about X
    double travel = 0;
};

struct VehicleLod {
    PaintedMesh chassis;                    // all static parts, vehicle space
    std::array<PaintedMesh, 2> wheel;       // [side -1, side +1]: tire + rim, origin at hub, spins about X
    std::array<PaintedMesh, 2> brake;       // hub-mounted, steers but never spins
    std::array<PaintedMesh, 2> axle;        // [front, rear] housing, origin at axle centre
    std::array<PaintedMesh, 3> link;        // unit length along +Z from the pivot: lower, upper, panhard
    PaintedMesh shockBody;                  // hangs from the mount down -Y
    PaintedMesh shockShaft;                 // rises from the anchor up +Y
    PaintedMesh spring;                     // unit length along +Y
    PaintedMesh steeringWheel;              // origin at the hub, wheel in the XY plane
};

struct VehicleMeshes {
    std::string id;
    VehicleRig rig;
    std::array<VehicleLod, 3> lods;         // detail 0 far, 1 mid, 2 near
};

VehicleMeshes build_vehicle_meshes(const VehicleDesign& design);

std::vector<uint8_t> serialize_vehicles(const std::vector<VehicleMeshes>& set);
bool deserialize_vehicles(const std::vector<uint8_t>& bytes, std::vector<VehicleMeshes>& out, std::string* error);

}  // namespace worldcore
