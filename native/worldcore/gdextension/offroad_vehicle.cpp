#include "offroad_vehicle.hpp"

#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/physics_material.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

using worldcore::BodyState;
using worldcore::Quat;
using worldcore::Vec3;

namespace {

Vec3 to_vec(const Vector3& v) { return {v.x, v.y, v.z}; }
Vector3 to_godot(const Vec3& v) { return Vector3(v.x, v.y, v.z); }

double num(const Dictionary& d, const char* key, double fallback) {
    return d.has(key) ? static_cast<double>(d[key]) : fallback;
}

worldcore::Tune tune_from(const Dictionary& d) {
    worldcore::Tune t;
#define X(name) t.name = num(d, #name, t.name);
    WORLDCORE_TUNE_FIELDS(X)
#undef X
    return t;
}

bool spec_from(const Dictionary& v, worldcore::VehicleSpec& s) {
    if (!v.has("perf") || !v.has("physics")) return false;
    const Dictionary perf = v["perf"];
    const Dictionary frame = v["frame"];
    const Dictionary axle = v["axle"];
    const Dictionary susp = v["suspension"];
    const Dictionary phys = v["physics"];
    const Dictionary box = phys["chassisBox"];
    const Array half = box["halfExtents"];
    const Array centre = box["centre"];
    const Array gears = perf["gearRatios"];

    s.id = String(v["id"]).utf8().get_data();
    s.perf.mass = num(perf, "mass", s.perf.mass);
    s.perf.power = num(perf, "power", s.perf.power);
    s.perf.torque = num(perf, "torque", s.perf.torque);
    s.perf.topSpeed = num(perf, "topSpeed", s.perf.topSpeed);
    s.perf.lowRangeRatio = num(perf, "lowRangeRatio", s.perf.lowRangeRatio);
    s.perf.finalDrive = num(perf, "finalDrive", s.perf.finalDrive);
    s.perf.brakeTorque = num(perf, "brakeTorque", s.perf.brakeTorque);
    s.perf.tireGrip = num(perf, "tireGrip", s.perf.tireGrip);
    s.perf.tractionControl = num(perf, "tractionControl", s.perf.tractionControl);
    s.perf.forwardGears = static_cast<int>(std::min<int64_t>(gears.size(), s.perf.gearRatios.size()));
    for (int i = 0; i < s.perf.forwardGears; i++) s.perf.gearRatios[static_cast<size_t>(i)] = gears[i];
    s.wheelbase = num(frame, "wheelbase", s.wheelbase);
    s.track = num(axle, "track", s.track);
    s.suspensionTravel = num(susp, "travel", s.suspensionTravel);
    s.wheelRadius = num(phys, "wheelRadius", s.wheelRadius);
    s.sillY = num(phys, "sillY", s.sillY);
    s.zFront = num(phys, "zFront", s.zFront);
    s.boxHalf = {half[0], half[1], half[2]};
    s.boxCentre = {centre[0], centre[1], centre[2]};
    return true;
}

}  // namespace

OffroadVehicle::OffroadVehicle() {
    set_physics_process_priority(-10);
}

bool OffroadVehicle::configure(const Dictionary& spec, const Dictionary& tune, const Ref<TerrainField>& terrain) {
    worldcore::VehicleSpec s;
    if (!spec_from(spec, s)) {
        UtilityFunctions::push_error("OffroadVehicle.configure: spec is missing perf/physics");
        return false;
    }
    field_ = terrain.is_valid() ? terrain->core() : nullptr;
    model_ = std::make_unique<worldcore::VehicleModel>(s, tune_from(tune), field_.get());

    // Body: the model's mass properties are the truth; the box only collides.
    const worldcore::BodySetup b = model_->body_setup();
    set_mass(b.mass);
    set_center_of_mass_mode(CENTER_OF_MASS_MODE_CUSTOM);
    set_center_of_mass(to_godot(b.comLocal));
    set_inertia(to_godot(b.inertia));
    set_linear_damp_mode(DAMP_MODE_REPLACE);
    set_linear_damp(b.linearDamping);
    set_angular_damp_mode(DAMP_MODE_REPLACE);
    set_angular_damp(b.angularDamping);
    set_can_sleep(false);
    set_use_custom_integrator(false);

    Ref<PhysicsMaterial> mat;
    mat.instantiate();
    mat->set_friction(b.friction);
    mat->set_bounce(b.restitution);
    set_physics_material_override(mat);

    for (int i = get_child_count() - 1; i >= 0; i--) {
        if (Object::cast_to<CollisionShape3D>(get_child(i))) get_child(i)->queue_free();
    }
    Ref<BoxShape3D> shape;
    shape.instantiate();
    shape->set_size(to_godot(b.boxHalf) * 2.0);
    auto* col = memnew(CollisionShape3D);
    col->set_name("Chassis");
    col->set_shape(shape);
    col->set_position(to_godot(b.boxCentre));
    add_child(col);

    steps_ = 0;
    post_stepped_ = 0;
    return true;
}

void OffroadVehicle::spawn(double x, double z, double heading) {
    ERR_FAIL_COND_MSG(!model_, "OffroadVehicle.spawn before configure");
    const worldcore::BodyReset pose = model_->spawn_pose(x, z, heading);
    const Transform3D t(Basis(Quaternion(pose.rot.x, pose.rot.y, pose.rot.z, pose.rot.w)), to_godot(pose.pos));
    set_global_transform(t);
    PhysicsServer3D::get_singleton()->body_set_state(get_rid(), PhysicsServer3D::BODY_STATE_TRANSFORM, t);
    set_linear_velocity(Vector3());
    set_angular_velocity(Vector3());
    model_->settle();
    pending_reset_.reset();
}

void OffroadVehicle::set_input(double throttle, double brake, double handbrake, double steer, double winch) {
    if (model_) model_->set_input({throttle, brake, handbrake, steer, winch});
}

bool OffroadVehicle::shift_up() { return model_ && model_->drivetrain().shift_up(); }
bool OffroadVehicle::shift_down() { return model_ && model_->drivetrain().shift_down(); }
int OffroadVehicle::cycle_lock() { return model_ ? model_->drivetrain().cycle_lock() : 0; }
bool OffroadVehicle::toggle_range() { return model_ && model_->drivetrain().toggle_range(model_->state().speed); }

bool OffroadVehicle::flip() {
    if (!model_) return false;
    pending_reset_ = model_->flip(current_body_state());
    return pending_reset_.has_value();
}

bool OffroadVehicle::winch_attach() { return model_ && model_->winch_attach(current_body_state()); }

double OffroadVehicle::get_ride_height() const { return model_ ? model_->ride_height() : 0.0; }

BodyState OffroadVehicle::body_state(PhysicsDirectBodyState3D* state) const {
    const Transform3D t = state->get_transform();
    const Quaternion q = t.basis.get_rotation_quaternion();
    BodyState b;
    b.pos = to_vec(t.origin);
    b.rot = {q.x, q.y, q.z, q.w};
    b.com = to_vec(t.origin + state->get_center_of_mass());
    b.linvel = to_vec(state->get_linear_velocity());
    b.angvel = to_vec(state->get_angular_velocity());
    return b;
}

BodyState OffroadVehicle::current_body_state() const {
    PhysicsDirectBodyState3D* state = PhysicsServer3D::get_singleton()->body_get_direct_state(get_rid());
    return body_state(state);
}

void OffroadVehicle::_notification(int what) {
    if (what == NOTIFICATION_READY) {
        set_physics_process(true);
        return;
    }
    if (what != NOTIFICATION_PHYSICS_PROCESS || !model_ || Engine::get_singleton()->is_editor_hint()) return;
    // Telemetry for the step Jolt integrated since the last tick.
    if (steps_ > post_stepped_) {
        model_->post_step(worldcore::kFixedDt, current_body_state());
        post_stepped_ = steps_;
    }
}

void OffroadVehicle::_integrate_forces(PhysicsDirectBodyState3D* state) {
    if (!model_) return;
    if (pending_reset_) {
        const auto& r = *pending_reset_;
        state->set_transform(Transform3D(Basis(Quaternion(r.rot.x, r.rot.y, r.rot.z, r.rot.w)), to_godot(r.pos)));
        state->set_linear_velocity(Vector3());
        state->set_angular_velocity(Vector3());
        pending_reset_.reset();
    }
    const Vector3 origin = state->get_transform().origin;
    const auto& forces = model_->compute_forces(state->get_step(), body_state(state));
    for (const auto& f : forces) state->apply_force(to_godot(f.force), to_godot(f.point) - origin);
    steps_++;
}

Dictionary OffroadVehicle::telemetry() const {
    Dictionary d;
    if (!model_) return d;
    const auto& st = model_->state();
    const auto& dt = model_->drivetrain();
    d["speed"] = st.speed;
    d["forward_speed"] = st.forwardSpeed;
    d["kph"] = std::abs(st.forwardSpeed) * 3.6;
    d["reverse"] = st.reverse;
    d["rpm"] = dt.state().rpm;
    d["redline"] = 5000.0;
    d["gear"] = dt.state().gear;
    d["gear_name"] = String(dt.gear_name());
    d["low_range"] = dt.state().lowRange;
    d["lock"] = String(dt.lock_name());
    d["pitch"] = st.pitch;
    d["roll"] = st.roll;
    d["heading"] = st.heading;
    d["altitude"] = st.altitude;
    d["surface"] = st.surface;
    d["wetness"] = st.wetness;
    d["airborne"] = st.airborne;
    d["stuck"] = st.stuck;
    d["mud"] = st.mud;
    d["slip"] = st.slipMax;
    d["odometer"] = st.odometer;
    d["impact"] = st.impact;
    d["winch"] = st.winchAnchor.has_value();
    d["winch_tension"] = st.winchTension;
    d["steer_angle"] = model_->steer_angle();
    d["steps"] = steps_;
    return d;
}

Vector3 OffroadVehicle::winch_anchor() const {
    if (model_ && model_->state().winchAnchor)
        return to_godot(*model_->state().winchAnchor);
    return Vector3(0, -1e9, 0);
}

Dictionary OffroadVehicle::wheel(int index) const {
    Dictionary d;
    if (!model_ || index < 0 || index > 3) return d;
    const worldcore::Wheel& w = model_->wheels()[static_cast<size_t>(index)];
    d["steer"] = w.steer;
    d["spin"] = w.spin;
    d["omega"] = w.omega;
    d["travel"] = w.travel;
    d["sink"] = w.sink;
    d["contact"] = w.contact;
    d["load"] = w.load;
    d["slip"] = w.slip;
    d["surface"] = w.surface;
    return d;
}

void OffroadVehicle::_bind_methods() {
    ClassDB::bind_method(D_METHOD("configure", "spec", "tune", "terrain"), &OffroadVehicle::configure);
    ClassDB::bind_method(D_METHOD("spawn", "x", "z", "heading"), &OffroadVehicle::spawn);
    ClassDB::bind_method(D_METHOD("set_input", "throttle", "brake", "handbrake", "steer", "winch"),
                         &OffroadVehicle::set_input);
    ClassDB::bind_method(D_METHOD("shift_up"), &OffroadVehicle::shift_up);
    ClassDB::bind_method(D_METHOD("shift_down"), &OffroadVehicle::shift_down);
    ClassDB::bind_method(D_METHOD("cycle_lock"), &OffroadVehicle::cycle_lock);
    ClassDB::bind_method(D_METHOD("toggle_range"), &OffroadVehicle::toggle_range);
    ClassDB::bind_method(D_METHOD("flip"), &OffroadVehicle::flip);
    ClassDB::bind_method(D_METHOD("winch_attach"), &OffroadVehicle::winch_attach);
    ClassDB::bind_method(D_METHOD("telemetry"), &OffroadVehicle::telemetry);
    ClassDB::bind_method(D_METHOD("wheel", "index"), &OffroadVehicle::wheel);
    ClassDB::bind_method(D_METHOD("winch_anchor"), &OffroadVehicle::winch_anchor);
    ClassDB::bind_method(D_METHOD("get_steps"), &OffroadVehicle::get_steps);
    ClassDB::bind_method(D_METHOD("get_ride_height"), &OffroadVehicle::get_ride_height);
}

}  // namespace godot
