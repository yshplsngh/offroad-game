// offroad_vehicle.hpp - worldcore::VehicleModel on a Jolt rigid body.
//
// Per physics tick, in this order (physics_process_priority is -10, so any
// script driving the vehicle at the default priority runs after it):
//   _physics_process   post_step() telemetry for the step Jolt just integrated
//   (scripts)          read telemetry, set_input(), gear/lock/range actions
//   _integrate_forces  compute_forces() from the pre-step state, apply them
//   Jolt integrates
// which is the reference's controller.step() split across the engine's step.
#pragma once

#include <memory>
#include <optional>

#include <godot_cpp/classes/physics_direct_body_state3d.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "terrain_field.hpp"
#include "worldcore/vehicle.hpp"

namespace godot {

class OffroadVehicle : public RigidBody3D {
    GDCLASS(OffroadVehicle, RigidBody3D)

public:
    OffroadVehicle();

    /// spec: one entry of data/vehicles.json; tune: data/tune.json.
    bool configure(const Dictionary& spec, const Dictionary& tune, const Ref<TerrainField>& terrain);
    /// Place the truck settled at ride height on the ground at (x, z).
    void spawn(double x, double z, double heading);

    void set_input(double throttle, double brake, double handbrake, double steer, double winch);
    bool shift_up();
    bool shift_down();
    int cycle_lock();
    bool toggle_range();
    bool flip();
    bool winch_attach();

    Dictionary telemetry() const;
    /// {steer, spin, travel, contact, load, slip, surface} for wheel 0..3 (FL FR RL RR).
    Dictionary wheel(int index) const;
    int get_steps() const { return steps_; }
    double get_ride_height() const;

    void _integrate_forces(PhysicsDirectBodyState3D* state) override;

protected:
    static void _bind_methods();
    void _notification(int what);

private:
    worldcore::BodyState body_state(PhysicsDirectBodyState3D* state) const;
    worldcore::BodyState current_body_state() const;

    std::shared_ptr<const worldcore::Field> field_;
    std::unique_ptr<worldcore::VehicleModel> model_;
    std::optional<worldcore::BodyReset> pending_reset_;
    int steps_ = 0;
    int post_stepped_ = 0;
};

}  // namespace godot
