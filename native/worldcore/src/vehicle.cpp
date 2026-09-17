// vehicle.cpp - see vehicle.hpp. Follows src/physics/controller.js step by
// step (browser reference, git a59773d); its comments explain each term.
#include "worldcore/vehicle.hpp"

#include <algorithm>
#include <cmath>

#include "worldcore/tire.hpp"

namespace worldcore {

namespace {

constexpr double G = 9.81;

double clamp(double v, double a, double b) { return std::min(b, std::max(a, v)); }
double clamp01(double v) { return clamp(v, 0, 1); }

double approach(double cur, double target, double rate, double dt) {
    const double d = target - cur;
    const double step = rate * dt;
    return std::fabs(d) <= step ? target : cur + js_sign(d) * step;
}

}  // namespace

VehicleModel::VehicleModel(const VehicleSpec& spec, const Tune& tune, const Field* field)
    : spec_(spec),
      tune_(tune),
      field_(field),
      drivetrain_(spec.perf, tune,
                  tune.wheelInertiaFactor * (spec.perf.mass * tune.wheelMassFraction) * spec.wheelRadius *
                      spec.wheelRadius) {
    mass_ = spec.perf.mass;
    tireR_ = spec.wheelRadius;
    travel_ = spec.suspensionTravel;
    track_ = spec.track;
    wheelbase_ = spec.wheelbase;

    const Vec3& half = spec.boxHalf;
    comLocal_ = {0, spec.sillY + tune.comAboveSill, -tune.comRearBias * wheelbase_};
    inertia_ = {
        (mass_ / 12) * (4 * half.y * half.y + 4 * half.z * half.z) * tune.inertiaPitch,
        (mass_ / 12) * (4 * half.x * half.x + 4 * half.z * half.z) * tune.inertiaYaw,
        (mass_ / 12) * (4 * half.x * half.x + 4 * half.y * half.y) * tune.inertiaRoll,
    };

    staticLoad_ = (mass_ * G) / 4;
    const double wheelMass = mass_ * tune.wheelMassFraction;
    wheelInertia_ = tune.wheelInertiaFactor * wheelMass * tireR_ * tireR_;

    for (int i = 0; i < 4; i++) {
        Wheel& w = wheels_[static_cast<size_t>(i)];
        w.index = i;
        w.front = i < 2;
        w.side = i % 2 == 0 ? -1 : 1;
        w.hub = {w.side * (track_ / 2), tireR_, w.front ? wheelbase_ / 2 : -wheelbase_ / 2};
    }

    const double totalTravel = travel_ * 2;
    sagU_ = std::max(0.01, tune.staticSag * totalTravel);
    springK_ = staticLoad_ / sagU_;
    const double critical = 2 * std::sqrt(springK_ * (mass_ / 4));
    dampBump_ = critical * tune.dampBump * tune.handlingDamping;
    dampRebound_ = critical * tune.dampRebound * tune.handlingDamping;
    maxSpring_ = staticLoad_ * tune.maxSpringForceG;
    rayLen_ = totalTravel + tireR_ + 0.6;
    bumpStopU_ = tune.bumpStopStart * totalTravel;
    restTravel_ = sagU_ - travel_;
    rideHeight_ = -restTravel_;
}

BodySetup VehicleModel::body_setup() const {
    return {mass_, comLocal_, inertia_, tune_.bodyLinearDamping, tune_.bodyAngularDamping,
            spec_.boxHalf, spec_.boxCentre};
}

BodyReset VehicleModel::spawn_pose(double x, double z, double heading) const {
    return {{x, ground_height(x, z) + rideHeight_, z}, Quat::axis_angle({0, 1, 0}, heading)};
}

void VehicleModel::set_input(const VehicleInput& in) {
    input_.throttle = clamp01(in.throttle);
    input_.brake = clamp01(in.brake);
    input_.handbrake = clamp01(in.handbrake);
    input_.steer = clamp(in.steer, -1, 1);
    input_.winch = clamp01(in.winch);
}

double VehicleModel::ground_height(double x, double z) const { return field_ ? field_->height(x, z) : 0; }

/* ---------------------------------------------------------------- ground -- */

GroundHit VehicleModel::probe(const Vec3& origin, const Vec3& dir, double maxDist) const {
    GroundHit out;
    const double down = -dir.y;
    if (down > 0.15) {
        double t = clamp((origin.y - ground_height(origin.x, origin.z)) / down, 0, maxDist * 2);
        for (int i = 0; i < 3; i++) {
            const double x = origin.x + dir.x * t;
            const double z = origin.z + dir.z * t;
            const double y = origin.y + dir.y * t;
            t = clamp(t + (y - ground_height(x, z)) / down, 0, maxDist * 2);
        }
        if (t <= maxDist) {
            const double x = origin.x + dir.x * t;
            const double z = origin.z + dir.z * t;
            out.hit = true;
            out.dist = t;
            if (field_) {
                const GroundSample s = field_->sample(x, z);
                out.p = {x, s.height, z};
                out.n = {s.nx, s.ny, s.nz};
                out.surface = s.surfaceId;
                out.wetness = s.wetness;
            } else {
                out.p = {x, 0, z};
            }
        }
    }

    if (solid_) {
        const double limit = std::min(maxDist, out.hit ? out.dist : maxDist);
        GroundHit s;
        if (solid_(origin, dir, limit, s) && s.dist < limit) {
            s.hit = true;
            s.solid = true;
            if (s.n.y < 0) s.n = s.n * -1;
            s.surface = SURFACE_ROCK;
            s.wetness = 0;
            out = s;
        }
    }
    return out;
}

/* ------------------------------------------------------------------ body -- */

void VehicleModel::read_frame(const BodyState& b) {
    pos_ = b.pos;
    q_ = b.rot;
    com_ = b.com;
    linvel_ = b.linvel;
    angvel_ = b.angvel;
    up_ = q_.rotate({0, 1, 0});
    fwd_ = q_.rotate({0, 0, 1});
    right_ = q_.rotate({1, 0, 0});
}

Vec3 VehicleModel::point_velocity(const Vec3& p) const { return angvel_.cross(p - com_) + linvel_; }

/* ------------------------------------------------------------ suspension -- */

void VehicleModel::solve_suspension(double dt) {
    for (Wheel& w : wheels_) {
        const Vec3 anchorLocal{w.hub.x, tireR_ + travel_, w.hub.z};
        const Vec3 anchor = q_.rotate(anchorLocal) + pos_;
        const Vec3 down = up_ * -1;

        const GroundHit hit = probe(anchor, down, rayLen_);
        if (!hit.hit) {
            w.contact = false;
            w.travel = -travel_;
            w.u = 0;
            w.load = 0;
            w.springForce = 0;
            w.sink = std::max(0.0, w.sink - dt * tune_.sinkRate * 0.5);
            continue;
        }

        w.surface = hit.surface;
        w.wetness = hit.wetness;
        w.solid = hit.solid;

        const double sinkTarget = surface_info(hit.surface).sink * tune_.sinkScale;
        w.sink += (sinkTarget - w.sink) * clamp01(dt * tune_.sinkRate);

        const double c = clamp(travel_ - (hit.dist - tireR_ - w.sink), -travel_, travel_);
        w.travel = c;
        w.u = c + travel_;
        w.contact = c > -travel_ + 1e-4;
        if (!w.contact) {
            w.load = 0;
            w.springForce = 0;
            continue;
        }

        const Vec3 pv = point_velocity(hit.p);
        const double vSusp = -pv.dot(up_);

        double f = springK_ * w.u;
        if (w.u > bumpStopU_) {
            const double over = w.u - bumpStopU_;
            f += springK_ * tune_.bumpStopRate * over;
            if (vSusp > 0) f += dampBump_ * tune_.bumpStopRate * 0.08 * over * vSusp;
        }
        f += (vSusp > 0 ? dampBump_ : dampRebound_) * vSusp;

        w.springForce = clamp(f, 0, maxSpring_);
        w.contactPoint = hit.p;
        w.normal = hit.n;
    }

    const std::array<std::array<double, 3>, 2> bars{{{0, 1, tune_.antiRollFront * tune_.handlingAntiRoll}, {2, 3, tune_.antiRollRear * tune_.handlingAntiRoll}}};
    for (const auto& bar : bars) {
        Wheel& wa = wheels_[static_cast<size_t>(bar[0])];
        Wheel& wb = wheels_[static_cast<size_t>(bar[1])];
        if (!wa.contact && !wb.contact) continue;
        const double transfer = springK_ * bar[2] * (wa.u - wb.u);
        wa.springForce = clamp(wa.springForce + transfer, 0, maxSpring_);
        wb.springForce = clamp(wb.springForce - transfer, 0, maxSpring_);
    }

    for (Wheel& w : wheels_) {
        if (!w.contact) {
            w.load = 0;
            continue;
        }
        add_force(up_ * w.springForce, w.contactPoint);
        w.load = w.springForce * std::max(0.25, w.normal.dot(up_));
    }
}

/* ----------------------------------------------------------------- tires -- */

double VehicleModel::solve_tires(double dt) {
    double slipMax = 0;
    double bogged = 0;

    for (Wheel& w : wheels_) {
        const size_t i = static_cast<size_t>(w.index);
        double torque = driveTorque_[i];

        if (w.contact && w.load > 0) {
            Vec3 wFwd = apply_axis_angle(fwd_, up_, w.steer);
            wFwd += w.normal * -wFwd.dot(w.normal);
            if (wFwd.length_sq() < 1e-6) wFwd = fwd_;
            wFwd = wFwd.normalized();
            const Vec3 wRight = w.normal.cross(wFwd).normalized();

            const Vec3 pv = point_velocity(w.contactPoint);
            const double vLong = pv.dot(wFwd);
            const double vLat = pv.dot(wRight);
            const double wheelSpeed = w.omega * tireR_;

            const SurfaceInfo& surf = surface_info(w.surface);
            const double mu = peak_friction(surf, w.wetness, w.load, staticLoad_, tune_) * spec_.perf.tireGrip;
            const TireResult tire = tire_force(w.load, mu, vLong, vLat, wheelSpeed, tune_);

            double fx = tire.fx;
            double fy = tire.fy;
            if (tune_.tireRelax > 0) {
                // R4: force relaxation - the carcass winds up over rolled
                // distance, which kills the crawl-speed slip oscillation the
                // point model suffers near v = 0. tireRelax = 0 is the exact
                // reference (fx/fy pass through unfiltered).
                const double rolled = std::max(std::fabs(vLong), std::fabs(wheelSpeed)) * dt;
                const double a = std::min(1.0, (rolled + 0.002) / tune_.tireRelax);
                fx = w.lng + (tire.fx - w.lng) * a;
                fy = w.lat + (tire.fy - w.lat) * a;
            }
            w.lng = fx;
            w.lat = fy;
            w.slip = tire.combined;
            w.slipRatio = tire.slipRatio;
            if (tire.combined > slipMax) slipMax = tire.combined;

            add_force(wFwd * fx + wRight * fy, w.contactPoint + w.normal * (tune_.tireForceHeight * tireR_));

            torque -= fx * tireR_;
            double rr = rolling_resistance(surf, w.wetness, w.load, tireR_, tune_);
            if (tune_.rollSpread > 0) {
                // R4: surfaces differ in how hard they are to roll over - the
                // reference used one flat scale. Deviation from 1 scales with
                // rollSpread (0 = reference).
                static constexpr double kRollSpread[8] = {0.90, 1.15, 1.00, 1.05,
                                                          1.35, 1.60, 1.50, 1.50};
                const int s = std::clamp(w.surface, 0, 7);
                rr *= 1.0 + tune_.rollSpread * (kRollSpread[s] - 1.0);
            }
            torque -= js_sign(w.omega) * rr;

            if (w.sink > 0.005) {
                bogged += w.sink;
                const double drag = tune_.bogDrag * w.sink;
                const Vec3 flat{pv.x, 0, pv.z};
                const double sp = flat.length();
                if (sp > 0.02) add_force(flat * (-drag * std::min(sp, 6.0) / sp), w.contactPoint);
            }
        } else {
            w.lng = 0;
            w.lat = 0;
            w.slip = 0;
            w.slipRatio = 0;
            torque -= w.omega * 0.6;
        }

        w.omega += (torque * dt) / wheelInertia_;

        const double bt = brakes_[i];
        if (bt > 0) {
            const double dOmega = (bt * dt) / wheelInertia_;
            w.omega = std::fabs(w.omega) <= dOmega ? 0 : w.omega - js_sign(w.omega) * dOmega;
        }
        omega_[i] = w.omega;
    }

    drivetrain_.apply_diffs(omega_, dt);
    for (Wheel& w : wheels_) {
        const size_t i = static_cast<size_t>(w.index);
        w.omega = omega_[i];
        // R4: the diffs above re-inject engine-side spin into wheels the brake
        // clamp just stopped - the engine of the brake-held creep (a braked
        // truck drove itself at up to 0.5 m/s). brakeHold re-applies the brake
        // capacity after the diffs; 0 = reference behaviour.
        if (tune_.brakeHold > 0 && brakes_[i] > 0) {
            const double dOmega = (brakes_[i] * dt / wheelInertia_) * tune_.brakeHold;
            w.omega = std::fabs(w.omega) <= dOmega ? 0 : w.omega - js_sign(w.omega) * dOmega;
            omega_[i] = w.omega;
        }
        w.spin += w.omega * dt;
    }

    state_.slipMax = slipMax;
    return bogged;
}

/* ------------------------------------------------------------------ misc -- */

void VehicleModel::apply_aero() {
    const double v2 = linvel_.length_sq();
    if (v2 < 0.25) return;
    const double v = std::sqrt(v2);
    add_force(linvel_ * (-tune_.aeroDrag * v), com_);
}

void VehicleModel::apply_winch(double dt) {
    state_.winchTension = 0;
    if (!state_.winchAnchor) return;
    const Vec3 toAnchor = *state_.winchAnchor - pos_;
    const double dist = toAnchor.length();
    if (dist > tune_.winchRange * 1.3) {
        state_.winchAnchor.reset();
        return;
    }
    if (input_.winch > 0) state_.winchLength = std::max(1.0, state_.winchLength - tune_.winchSpeed * dt);
    const double over = dist - state_.winchLength;
    if (over <= 0) return;
    const double pull = std::min(tune_.winchForce, over * tune_.winchStiffness);
    state_.winchTension = pull / tune_.winchForce;
    const Vec3 point = q_.rotate({0, spec_.sillY * 0.4, spec_.zFront}) + pos_;
    add_force(toAnchor * (pull / std::max(0.001, dist)), point);
}

/* ------------------------------------------------------------------ step -- */

const std::vector<AppliedForce>& VehicleModel::compute_forces(double dt, const BodyState& body) {
    forces_.clear();
    read_frame(body);

    const double speedFrac = clamp01(std::fabs(state_.forwardSpeed) / tune_.steerSpeedFalloff);
    const double authority = 1 - (1 - tune_.steerMinFraction) * speedFrac;
    const double target = input_.steer * tune_.maxSteerAngle * authority;
    const double rate = std::fabs(input_.steer) > 0.02 ? tune_.steerRate : tune_.steerReturnRate;
    steerAngle_ = approach(steerAngle_, target, rate, dt);

    const double mag = std::fabs(steerAngle_);
    double innerA = mag, outerA = mag;
    if (mag > 1e-4) {
        const double radius = wheelbase_ / std::tan(mag);
        innerA = std::atan(wheelbase_ / std::max(0.35, radius - track_ / 2));
        outerA = std::atan(wheelbase_ / (radius + track_ / 2));
    }
    double sgn = js_sign(steerAngle_);
    if (sgn == 0) sgn = 1;
    const double leftA = sgn * (steerAngle_ > 0 ? outerA : innerA);
    const double rightA = sgn * (steerAngle_ > 0 ? innerA : outerA);
    wheels_[0].steer = steerAngle_ + (leftA - steerAngle_) * tune_.ackermann;
    wheels_[1].steer = steerAngle_ + (rightA - steerAngle_) * tune_.ackermann;

    solve_suspension(dt);

    driveTorque_ = drivetrain_.update(dt, input_.throttle, omega_);
    if (spec_.perf.tractionControl > 0) {
        // Traction control: cap each wheel's drive torque at a fraction of what
        // its tyre can transmit this step. Beyond the tyre's peak a wheel runs
        // away within one 60 Hz step and loses nearly all force, which no
        // throttle-side controller can catch in time.
        for (const Wheel& w : wheels_) {
            if (!w.contact || w.load <= 0) continue;
            const double mu = peak_friction(surface_info(w.surface), w.wetness, w.load, staticLoad_, tune_) *
                              spec_.perf.tireGrip;
            double limit = spec_.perf.tractionControl * mu * w.load * tireR_;
            double& t = driveTorque_[static_cast<size_t>(w.index)];
            // Already past the peak in the driven direction: back off so the wheel falls back into grip.
            if (w.slipRatio * t > 0 && std::fabs(w.slipRatio) > tune_.peakSlipRatio * 2) limit *= 0.4;
            t = clamp(t, -limit, limit);
        }
    }
    brakes_ = drivetrain_.brake_torques(input_.brake, input_.handbrake);

    bogged_ = solve_tires(dt);

    apply_aero();
    apply_winch(dt);
    return forces_;
}

void VehicleModel::post_step(double dt, const BodyState& body) {
    read_frame(body);
    state_.forwardSpeed = linvel_.dot(fwd_);
    state_.speed = linvel_.length();
    state_.reverse = state_.forwardSpeed < -0.4;
    state_.altitude = pos_.y;
    const Vec3 e = euler_yxz(q_);
    state_.heading = e.y;
    state_.pitch = e.x;
    state_.roll = e.z;

    int contacts = 0;
    double wet = 0;
    int surf = SURFACE_DIRT;
    double best = -1;
    for (const Wheel& w : wheels_) {
        if (!w.contact) continue;
        contacts++;
        wet += w.wetness;
        if (w.load > best) {
            best = w.load;
            surf = w.surface;
        }
    }
    state_.airborne = 4 - contacts;
    state_.wetness = contacts ? wet / contacts : 0;
    state_.surface = surf;

    const double dv = std::fabs(state_.speed - lastSpeed_);
    state_.impact = dv > 1.6 ? std::min(1.5, (dv - 1.6) * 0.5) : 0;
    lastSpeed_ = state_.speed;
    state_.odometer += std::fabs(state_.forwardSpeed) * dt;

    const double spinning = std::fabs(wheels_[2].omega + wheels_[3].omega) * 0.5 * tireR_;
    const bool trying = input_.throttle > 0.35 && spinning > 1.2;
    if (trying && state_.speed < tune_.stuckSpeed) {
        state_.stuck = clamp01(state_.stuck + dt * tune_.stuckRate);
    } else {
        state_.stuck = clamp01(state_.stuck - dt * tune_.stuckRate * 0.8);
    }

    const double soft = clamp01(bogged_ * 3);
    const double filth = soft * clamp01(0.22 + state_.wetness * 1.4) * (0.4 + 0.6 * state_.slipMax);
    state_.mud = clamp01(state_.mud + (filth * tune_.mudCakeRate - tune_.mudCleanRate * (1 - filth)) * dt);

    if (state_.flipTimer > 0) state_.flipTimer = std::max(0.0, state_.flipTimer - dt);
}

/* ----------------------------------------------------------------- state -- */

IntegratedState VehicleModel::integrated_state() const {
    IntegratedState s;
    s.steerAngle = steerAngle_;
    for (size_t i = 0; i < 4; i++) {
        s.omega[i] = wheels_[i].omega;
        s.spin[i] = wheels_[i].spin;
        s.sink[i] = wheels_[i].sink;
    }
    s.drivetrain = drivetrain_.state();
    s.speed = state_.speed;
    s.forwardSpeed = state_.forwardSpeed;
    s.stuck = state_.stuck;
    s.mud = state_.mud;
    s.odometer = state_.odometer;
    s.flipTimer = state_.flipTimer;
    s.winchAnchor = state_.winchAnchor;
    s.winchLength = state_.winchLength;
    return s;
}

void VehicleModel::restore(const IntegratedState& s) {
    steerAngle_ = s.steerAngle;
    for (size_t i = 0; i < 4; i++) {
        wheels_[i].omega = s.omega[i];
        wheels_[i].spin = s.spin[i];
        wheels_[i].sink = s.sink[i];
        omega_[i] = s.omega[i];  // the drivetrain reads last step's post-diff speeds
    }
    drivetrain_.restore(s.drivetrain);
    state_.speed = s.speed;
    lastSpeed_ = s.speed;
    state_.forwardSpeed = s.forwardSpeed;
    state_.stuck = s.stuck;
    state_.mud = s.mud;
    state_.odometer = s.odometer;
    state_.flipTimer = s.flipTimer;
    state_.winchAnchor = s.winchAnchor;
    state_.winchLength = s.winchLength;
}

/* ----------------------------------------------------------------- verbs -- */

void VehicleModel::settle() {
    for (Wheel& w : wheels_) {
        w.travel = restTravel_;
        w.u = sagU_;
        w.omega = 0;
        w.sink = 0;
        w.spin = 0;
    }
}

std::optional<BodyReset> VehicleModel::flip(const BodyState& body) {
    if (state_.flipTimer > 0) return std::nullopt;
    read_frame(body);
    state_.flipTimer = tune_.flipCooldown;
    const double yaw = std::atan2(fwd_.x, fwd_.z);
    const double h = ground_height(pos_.x, pos_.z);
    for (Wheel& w : wheels_) w.omega = 0;
    state_.stuck = 0;
    return BodyReset{{pos_.x, h + tune_.flipLift + travel_, pos_.z}, Quat::axis_angle({0, 1, 0}, yaw)};
}

bool VehicleModel::winch_attach(const BodyState& body) {
    read_frame(body);
    if (state_.winchAnchor) {
        state_.winchAnchor.reset();
        return false;
    }
    for (double d = 6; d <= tune_.winchRange; d += 2) {
        const double x = pos_.x + fwd_.x * d;
        const double z = pos_.z + fwd_.z * d;
        const double y = ground_height(x, z);
        if (y > pos_.y - 1.5) {
            state_.winchAnchor = Vec3{x, y + 0.4, z};
            state_.winchLength = (*state_.winchAnchor - pos_).length();
            return true;
        }
    }
    const double x = pos_.x + fwd_.x * tune_.winchRange * 0.9;
    const double z = pos_.z + fwd_.z * tune_.winchRange * 0.9;
    const double y = ground_height(x, z);
    state_.winchAnchor = Vec3{x, y + 0.6, z};
    state_.winchLength = (*state_.winchAnchor - pos_).length();
    return true;
}

}  // namespace worldcore
