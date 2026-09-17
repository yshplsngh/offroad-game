// vehicle.hpp - the driving model. Port of src/physics/controller.js and the
// heightfield leg of src/physics/ground.js (browser reference, git a59773d).
//
// ENGINE BOUNDARY: the reference hands Rapier its forces; here the host rigid
// body (Jolt, via Godot) does the integrating. Each fixed step the host calls
//   read BodyState -> post_step(prev dt) -> compute_forces(dt) -> apply forces
//   -> integrate
// which is exactly controller.js step(): forces from the pre-step state,
// telemetry from the post-step state. Mass, centre of mass, inertia, damping
// and the chassis box come from body_setup() so the host cannot invent them.
//
// THE ORDER MATTERS (from controller.js): all four loads before any tire force,
// then the drivetrain, then wheel integration, then the diffs.
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "worldcore/drivetrain.hpp"
#include "worldcore/field.hpp"
#include "worldcore/tune.hpp"
#include "worldcore/vmath.hpp"

namespace worldcore {

/// Physics-facing vehicle spec: catalog perf + dimensions + body metrics.
struct VehicleSpec {
    std::string id;
    VehiclePerf perf;
    double wheelbase = 2.5;
    double track = 1.6;
    double wheelRadius = 0.44;
    double suspensionTravel = 0.26;
    double sillY = 0.7;
    double zFront = 2.0;
    Vec3 boxHalf;
    Vec3 boxCentre;
};

struct BodyState {
    Vec3 pos;
    Quat rot;
    Vec3 com;  // world-space centre of mass
    Vec3 linvel;
    Vec3 angvel;
};

struct BodySetup {
    double mass;
    Vec3 comLocal;
    Vec3 inertia;  // principal, body axes
    double linearDamping, angularDamping;
    Vec3 boxHalf, boxCentre;
    double friction = 0.35, restitution = 0.02;
};

/// A force at a world-space point, applied for one step.
struct AppliedForce {
    Vec3 force;
    Vec3 point;
};

/// The host must put the body here, with zero velocity.
struct BodyReset {
    Vec3 pos;
    Quat rot;
};

struct VehicleInput {
    double throttle = 0, brake = 0, handbrake = 0, steer = 0, winch = 0;
};

struct GroundHit {
    bool hit = false;
    double dist = 0;
    Vec3 p;
    Vec3 n{0, 1, 0};
    int surface = SURFACE_DIRT;
    double wetness = 0;
    bool solid = false;
};

/// Optional query for things that are not the heightfield (rocks, logs).
/// Returns true with dist/point/normal filled when it hits nearer than maxDist.
using SolidRaycast = std::function<bool(const Vec3& origin, const Vec3& dir, double maxDist, GroundHit& out)>;

struct Wheel {
    int index = 0;
    bool front = false;
    int side = -1;
    Vec3 hub;
    double steer = 0, omega = 0, spin = 0;
    double travel = 0, u = 0, load = 0, sink = 0;
    double slip = 0, slipRatio = 0;
    bool contact = false, solid = false;
    int surface = SURFACE_DIRT;
    double wetness = 0;
    double springForce = 0, lng = 0, lat = 0;
    Vec3 contactPoint;
    Vec3 normal{0, 1, 0};
};

struct VehicleState {
    double speed = 0, forwardSpeed = 0;
    bool reverse = false;
    double pitch = 0, roll = 0, heading = 0, altitude = 0;
    int airborne = 0;
    double stuck = 0, mud = 0, odometer = 0, impact = 0;
    int surface = SURFACE_DIRT;
    double wetness = 0, slipMax = 0, flipTimer = 0;
    std::optional<Vec3> winchAnchor;
    double winchLength = 0, winchTension = 0;
};

/// Everything VehicleModel integrates across steps. With the body transform
/// and velocities it fully determines the next step: save/load and replay
/// resynchronisation both go through this.
struct IntegratedState {
    double steerAngle = 0;
    std::array<double, 4> omega{}, spin{}, sink{};
    Drivetrain::State drivetrain;
    double speed = 0, forwardSpeed = 0;  // speed is also the impact detector's memory
    double stuck = 0, mud = 0, odometer = 0, flipTimer = 0;
    std::optional<Vec3> winchAnchor;
    double winchLength = 0;
};

class VehicleModel {
public:
    /// `field` may be null: flat ground at y = 0, dirt (ground.js fallback).
    VehicleModel(const VehicleSpec& spec, const Tune& tune, const Field* field);

    BodySetup body_setup() const;
    /// Where to put a body spawned at (x, z) with this heading, already settled.
    BodyReset spawn_pose(double x, double z, double heading) const;

    void set_input(const VehicleInput& in);
    void set_solid_raycast(SolidRaycast fn) { solid_ = std::move(fn); }

    /// Forces for this step from the pre-integration body state.
    const std::vector<AppliedForce>& compute_forces(double dt, const BodyState& body);
    /// Telemetry from the post-integration body state of the step just taken.
    void post_step(double dt, const BodyState& body);

    IntegratedState integrated_state() const;
    void restore(const IntegratedState& s);

    /// controller.settle(): wheel state for a body just placed at spawn_pose.
    void settle();
    /// Right the truck in place. nullopt while cooling down.
    std::optional<BodyReset> flip(const BodyState& body);
    /// Hook the winch to the ground ahead, or release it. Returns attached.
    bool winch_attach(const BodyState& body);

    Drivetrain& drivetrain() { return drivetrain_; }
    const Drivetrain& drivetrain() const { return drivetrain_; }
    const std::array<Wheel, 4>& wheels() const { return wheels_; }
    const VehicleState& state() const { return state_; }
    VehicleState& state() { return state_; }
    const VehicleSpec& spec() const { return spec_; }
    double steer_angle() const { return steerAngle_; }
    double static_load() const { return staticLoad_; }
    double ride_height() const { return rideHeight_; }

    GroundHit probe(const Vec3& origin, const Vec3& dir, double maxDist) const;

private:
    double ground_height(double x, double z) const;
    void read_frame(const BodyState& body);
    Vec3 point_velocity(const Vec3& p) const;
    void add_force(const Vec3& f, const Vec3& p) { forces_.push_back({f, p}); }
    void solve_suspension(double dt);
    double solve_tires(double dt);
    void apply_aero();
    void apply_winch(double dt);

    VehicleSpec spec_;
    Tune tune_;
    const Field* field_;
    SolidRaycast solid_;
    Drivetrain drivetrain_;

    double mass_, tireR_, travel_, track_, wheelbase_;
    Vec3 comLocal_, inertia_;
    double staticLoad_, wheelInertia_;
    double sagU_, springK_, dampBump_, dampRebound_, maxSpring_, rayLen_, bumpStopU_, restTravel_, rideHeight_;

    std::array<Wheel, 4> wheels_;
    WheelArray omega_{};
    WheelArray driveTorque_{};
    WheelArray brakes_{};
    VehicleInput input_;
    double steerAngle_ = 0;
    VehicleState state_;
    double lastSpeed_ = 0;
    double bogged_ = 0;

    // frame of the current body state
    Vec3 pos_, com_, linvel_, angvel_, up_, fwd_, right_;
    Quat q_;

    std::vector<AppliedForce> forces_;
};

}  // namespace worldcore
