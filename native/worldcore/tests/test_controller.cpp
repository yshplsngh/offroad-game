// test_controller.cpp - VehicleModel against the reference controller at the
// body boundary (tests/golden/controller.json, frozen from the removed browser reference, git a59773d).
//
// Each step feeds the Rapier body state the JS controller read, then compares
// every force it applied and the telemetry it derived from the post-step state.
// The rigid-body integrator is not under test here - only the driving model.
//
// The reference amplifies round-off exponentially through wheel spin (a 5e-13
// libm difference at step 1 is 1e-5 by step 37), so only the first kFreeSteps
// run free. After that the model's integrated state is resynchronised from the
// oracle before every step and each step's outputs are held to a tight tolerance.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "json.hpp"
#include "vehicle_spec_json.hpp"
#include "worldcore/field.hpp"
#include "worldcore/vehicle.hpp"

using namespace worldcore;

namespace {

int g_fail = 0;
int g_checks = 0;
double g_worst_rel = 0;
std::string g_worst_what;
double g_step_worst = 0;
std::string g_step_what;

void check_eq(bool ok, const std::string& what) {
    g_checks++;
    if (!ok && g_fail++ < 25) std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

void check_near(double got, double want, const std::string& what, double rel = 1e-9, double abs = 1e-9) {
    g_checks++;
    const double err = std::fabs(got - want);
    const double scale = std::max(1.0, std::fabs(want));
    if (err / scale > g_step_worst) {
        g_step_worst = err / scale;
        g_step_what = what;
    }
    if (err / scale > g_worst_rel) {
        g_worst_rel = err / scale;
        g_worst_what = what;
    }
    if (!(err <= abs || err <= rel * scale) && g_fail++ < 25) {
        std::fprintf(stderr, "FAIL %s: got %.17g want %.17g\n", what.c_str(), got, want);
    }
}

BodyState body_from(const json::Value& a) {
    BodyState b;
    b.pos = {a[0].n, a[1].n, a[2].n};
    b.rot = {a[3].n, a[4].n, a[5].n, a[6].n};
    b.com = {a[7].n, a[8].n, a[9].n};
    b.linvel = {a[10].n, a[11].n, a[12].n};
    b.angvel = {a[13].n, a[14].n, a[15].n};
    return b;
}

constexpr int kFreeSteps = 12;

IntegratedState integrated_from(const json::Value& v) {
    IntegratedState s;
    s.steerAngle = v["steerAngle"].n;
    for (size_t k = 0; k < 4; k++) {
        s.omega[k] = v["omega"][k].n;
        s.spin[k] = v["spin"][k].n;
        s.sink[k] = v["sink"][k].n;
    }
    const auto& d = v["drivetrain"];
    s.drivetrain.omegaE = d[0].n;
    s.drivetrain.rpm = d[1].n;
    s.drivetrain.gear = static_cast<int>(d[2].n);
    s.drivetrain.lowRange = d[3].n != 0;
    s.drivetrain.diffLock = static_cast<int>(d[4].n);
    s.drivetrain.shiftTimer = d[5].n;
    s.drivetrain.engineTorque = d[6].n;
    s.drivetrain.clutchTorque = d[7].n;
    s.drivetrain.propTorque = d[8].n;
    s.drivetrain.running = d[9].n != 0;
    s.speed = v["speed"].n;
    s.forwardSpeed = v["forwardSpeed"].n;
    s.stuck = v["stuck"].n;
    s.mud = v["mud"].n;
    s.odometer = v["odometer"].n;
    s.flipTimer = v["flipTimer"].n;
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string golden = argc > 1 ? argv[1] : "tests/golden/controller.json";
    const std::string data = argc > 2 ? argv[2] : "../godot/data";
    const json::Value g = json::load(golden);
    const json::Value vehicles = json::load(data + "/vehicles.json");
    const json::Value replays = json::load(data + "/replays.json");
    const Tune tune = reference_tune();

    for (const auto& session : g["sessions"].a) {
        const std::string rid = session["replay"].s;
        const auto& replay = replays["replays"][rid];
        const VehicleSpec spec = vehicle_spec_from_json(vehicles, session["vehicle"].s);
        Field field(static_cast<int32_t>(replay["seed"].n));
        VehicleModel model(spec, tune, &field);

        // Spawn: the JS body starts at ground + 1 and settle() drops it to ride height.
        const auto& sp = session["spawn"];
        const BodyReset pose = model.spawn_pose(sp[0].n, sp[2].n, session["heading"].n);
        const BodyState initial = body_from(session["initial"]);
        check_near(pose.pos.y, initial.pos.y, rid + " spawn height", 1e-6, 1e-5);
        check_near(pose.rot.y, initial.rot.y, rid + " spawn heading", 1e-6, 1e-6);
        model.settle();

        const double dt = kFixedDt;
        int i = 0;
        for (const auto& s : session["steps"].a) {
            const std::string at = rid + " step " + std::to_string(i) + " ";
            const bool free = i < kFreeSteps;
            // Free-running steps apply actions like the game; resynced steps take
            // the post-action drivetrain state straight from the oracle.
            if (!free) model.restore(integrated_from(s["integrated"]));
            for (const auto& act : free ? s["actions"].a : std::vector<json::Value>{}) {
                if (act.s == "gearUp") model.drivetrain().shift_up();
                else if (act.s == "gearDown") model.drivetrain().shift_down();
                else if (act.s == "lock") model.drivetrain().cycle_lock();
                else if (act.s == "range") model.drivetrain().toggle_range(model.state().speed);
                else check_eq(false, at + "unknown action " + act.s);
            }
            const auto& ax = s["axes"];
            model.set_input({ax["throttle"].n, ax["brake"].n, ax["handbrake"].n, ax["steer"].n, ax["winch"].n});

            const auto& forces = model.compute_forces(dt, body_from(s["pre"]));
            const auto& want = s["forces"];
            check_eq(forces.size() == want.size(),
                     at + "force count " + std::to_string(forces.size()) + " vs " + std::to_string(want.size()));
            for (size_t k = 0; k < std::min(forces.size(), want.size()); k++) {
                const auto& f = forces[k];
                const auto& w = want[k];
                // Forces run to ~1e5 N: relative tolerance with an absolute floor of 1 uN.
                check_near(f.force.x, w[0].n, at + "force.x", 1e-9, 1e-6);
                check_near(f.force.y, w[1].n, at + "force.y", 1e-9, 1e-6);
                check_near(f.force.z, w[2].n, at + "force.z", 1e-9, 1e-6);
                check_near(f.point.x, w[3].n, at + "point.x", 1e-9, 1e-6);
                check_near(f.point.y, w[4].n, at + "point.y", 1e-9, 1e-6);
                check_near(f.point.z, w[5].n, at + "point.z", 1e-9, 1e-6);
            }

            model.post_step(dt, body_from(s["post"]));
            check_near(model.steer_angle(), s["steerAngle"].n, at + "steerAngle");

            const auto& tel = s["tel"];
            const VehicleState& st = model.state();
            check_near(st.speed, tel["speed"].n, at + "speed");
            check_near(st.forwardSpeed, tel["forwardSpeed"].n, at + "forwardSpeed");
            check_near(st.heading, tel["heading"].n, at + "heading", 1e-6, 1e-6);
            check_near(st.pitch, tel["pitch"].n, at + "pitch", 1e-6, 1e-6);
            check_near(st.roll, tel["roll"].n, at + "roll", 1e-6, 1e-6);
            check_near(model.drivetrain().state().rpm, tel["rpm"].n, at + "rpm", 1e-6, 1e-4);
            check_eq(model.drivetrain().state().gear == static_cast<int>(tel["gear"].n), at + "gear");
            check_eq(model.drivetrain().state().lowRange == tel["lowRange"].b, at + "lowRange");
            check_eq(std::string(model.drivetrain().lock_name()) == tel["lock"].s, at + "lock");
            check_eq(st.airborne == static_cast<int>(tel["airborne"].n), at + "airborne");
            check_near(st.stuck, tel["stuck"].n, at + "stuck");
            check_near(st.mud, tel["mud"].n, at + "mud");
            check_near(st.slipMax, tel["slip"].n, at + "slip", 1e-6, 1e-6);
            check_near(st.odometer, tel["odometer"].n, at + "odometer", 1e-6, 1e-6);
            check_near(st.impact, tel["impact"].n, at + "impact", 1e-6, 1e-6);
            check_near(st.wetness, tel["wetness"].n, at + "wetness");
            check_eq(st.surface == static_cast<int>(tel["surface"].n), at + "surface");

            for (size_t k = 0; k < 4; k++) {
                const Wheel& w = model.wheels()[k];
                const auto& ww = s["wheels"][k];
                const std::string wk = at + "wheel " + std::to_string(k) + " ";
                check_near(w.steer, ww[0].n, wk + "steer");
                check_near(w.omega, ww[1].n, wk + "omega", 1e-6, 1e-5);
                check_near(w.spin, ww[2].n, wk + "spin", 1e-6, 1e-5);
                check_near(w.travel, ww[3].n, wk + "travel");
                check_near(w.u, ww[4].n, wk + "u");
                check_near(w.load, ww[5].n, wk + "load", 1e-6, 1e-3);
                check_near(w.sink, ww[6].n, wk + "sink");
                check_near(w.slip, ww[7].n, wk + "slip", 1e-6, 1e-6);
                check_near(w.slipRatio, ww[8].n, wk + "slipRatio", 1e-6, 1e-6);
                check_eq(w.contact == (ww[9].n != 0), wk + "contact");
                check_eq(w.surface == static_cast<int>(ww[10].n), wk + "surface");
                check_near(w.wetness, ww[11].n, wk + "wetness");
                check_near(w.springForce, ww[12].n, wk + "springForce", 1e-6, 1e-3);
                check_near(w.lng, ww[13].n, wk + "long", 1e-6, 1e-3);
                check_near(w.lat, ww[14].n, wk + "lat", 1e-6, 1e-3);
            }
            if (std::getenv("TRACE_STEPS") && i < std::atoi(std::getenv("TRACE_STEPS"))) {
                std::fprintf(stderr, "trace %s %.3g %s\n", at.c_str(), g_step_worst, g_step_what.c_str());
            }
            g_step_worst = 0;
            i++;
        }
    }

    std::printf("%d checks, %d failed, worst relative error %.3g (%s)\n", g_checks, g_fail, g_worst_rel,
                g_worst_what.c_str());
    return g_fail == 0 ? 0 : 1;
}
