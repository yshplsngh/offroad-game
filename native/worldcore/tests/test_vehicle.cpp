// test_vehicle.cpp - tire model and drivetrain against the browser oracle
// (tests/golden/physics.json), plus a drift check that the C++ feel tables
// still equal native/godot/data/*.json (exported once from the removed JS
// reference, git a59773d; now the source of truth).
#include <cmath>
#include <cstdio>
#include <string>

#include "json.hpp"
#include "worldcore/drivetrain.hpp"
#include "worldcore/tire.hpp"
#include "worldcore/tune.hpp"

using namespace worldcore;

namespace {

int g_fail = 0;
int g_checks = 0;
double g_worst_rel = 0;

void check_eq(bool ok, const std::string& what) {
    g_checks++;
    if (!ok && g_fail++ < 25) std::fprintf(stderr, "FAIL %s\n", what.c_str());
}

/// Relative tolerance with an absolute floor: torques run to 1e4, slips to 1e-3.
void check_near(double got, double want, const std::string& what, double rel = 1e-9, double abs = 1e-9) {
    g_checks++;
    const double err = std::fabs(got - want);
    const double scale = std::max(1.0, std::fabs(want));
    if (err / scale > g_worst_rel) g_worst_rel = err / scale;
    if (!(err <= abs || err <= rel * scale) && g_fail++ < 25) {
        std::fprintf(stderr, "FAIL %s: got %.17g want %.17g\n", what.c_str(), got, want);
    }
}

VehiclePerf perf_from(const json::Value& v) {
    const auto& p = v["perf"];
    VehiclePerf perf;
    perf.mass = p["mass"].n;
    perf.power = p["power"].n;
    perf.torque = p["torque"].n;
    perf.topSpeed = p["topSpeed"].n;
    perf.lowRangeRatio = p["lowRangeRatio"].n;
    perf.forwardGears = static_cast<int>(p["gearRatios"].size());
    for (size_t i = 0; i < p["gearRatios"].size(); i++) perf.gearRatios.at(i) = p["gearRatios"][i].n;
    perf.finalDrive = p["finalDrive"].n;
    perf.brakeTorque = p["brakeTorque"].n;
    return perf;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string golden = argc > 1 ? argv[1] : "tests/golden/physics.json";
    const std::string data = argc > 2 ? argv[2] : "../godot/data";
    const json::Value g = json::load(golden);
    const json::Value tuneJson = json::load(data + "/tune.json");
    const json::Value worldJson = json::load(data + "/world.json");
    const json::Value vehiclesJson = json::load(data + "/vehicles.json");
    const Tune tune = reference_tune();
    const Tune defaults;

    /* ---- data drift: C++ defaults must equal the data tables ---- */
    size_t tuneFields = 0;
#define X(name) \
    check_near(defaults.name, tuneJson[#name].n, "tune." #name, 0, 1e-12); \
    tuneFields++;
    WORLDCORE_TUNE_FIELDS(X)
#undef X
    // Every key in tune.json except the _source note must be a known field.
    check_eq(tuneFields + 1 == tuneJson.size(), "tune.json has " + std::to_string(tuneJson.size() - 1) +
             " fields, C++ Tune has " + std::to_string(tuneFields));

    check_eq(worldJson["surfaces"].size() == SURFACE_COUNT, "surface count");
    for (const auto& s : worldJson["surfaces"].a) {
        const SurfaceInfo& c = surface_info(static_cast<int>(s["id"].n));
        const std::string at = "surface " + s["name"].s + " ";
        check_eq(c.name == s["name"].s, at + "name");
        check_near(c.friction, s["friction"].n, at + "friction", 0, 1e-12);
        check_near(c.drag, s["drag"].n, at + "drag", 0, 1e-12);
        check_near(c.sink, s["sink"].n, at + "sink", 0, 1e-12);
        check_near(c.dust, s["dust"].n, at + "dust", 0, 1e-12);
        check_eq(c.deform == s["deform"].b, at + "deform");
    }
    check_eq(worldJson["chunk"].n == kChunk && worldJson["fixedDt"].n == kFixedDt, "chunk/fixedDt");

    /* ---- tire ---- */
    for (const auto& row : g["slipCurve"].a) {
        check_near(slip_curve(row[0].n, row[1].n), row[2].n, "slip_curve");
    }
    for (const auto& t : g["tire"].a) {
        const auto& in = t["in"];
        const SurfaceInfo& surf = surface_info(static_cast<int>(in["surfaceId"].n));
        const double mu = peak_friction(surf, in["wetness"].n, in["load"].n, in["nominal"].n, tune);
        check_near(mu, t["mu"].n, "peak_friction");
        const TireResult r = tire_force(in["load"].n, t["mu"].n, in["vLong"].n, in["vLat"].n, in["wheelSpeed"].n, tune);
        check_near(r.fx, t["fx"].n, "tire fx", 1e-9, 1e-7);
        check_near(r.fy, t["fy"].n, "tire fy", 1e-9, 1e-7);
        check_near(r.slipRatio, t["slipRatio"].n, "tire slipRatio");
        check_near(r.slipAngle, t["slipAngle"].n, "tire slipAngle");
        check_near(r.combined, t["combined"].n, "tire combined");
        check_near(rolling_resistance(surf, in["wetness"].n, in["load"].n, in["radius"].n, tune), t["rolling"].n,
                   "rolling_resistance");
    }

    /* ---- drivetrain: replay the recorded session step by step ---- */
    for (const auto& session : g["drivetrain"].a) {
        const json::Value* spec = nullptr;
        for (const auto& v : vehiclesJson["vehicles"].a) {
            if (v["id"].s == session["vehicle"].s) spec = &v;
        }
        check_eq(spec != nullptr, "vehicle " + session["vehicle"].s + " in vehicles.json");
        if (!spec) continue;

        // A session can pin the reference perf when the game has since retuned that vehicle.
        const json::Value& source = session.has("reference") ? session["reference"] : *spec;
        const VehiclePerf perf = perf_from(source);
        const double r = source["physics"]["wheelRadius"].n;
        const double wheelInertia = tune.wheelInertiaFactor * perf.mass * tune.wheelMassFraction * r * r;
        check_near(wheelInertia, session["wheelInertia"].n, "wheelInertia");

        Drivetrain dt(perf, tune, session["wheelInertia"].n);
        const double h = 1.0 / 60.0;
        int i = 0;
        for (const auto& s : session["steps"].a) {
            const std::string at = session["vehicle"].s + " step " + std::to_string(i) + " ";
            for (const auto& e : s["events"].a) {
                const std::string name = e[0].s;
                const json::Value& want = e[1];
                if (name == "shiftUp") check_eq(dt.shift_up() == want.b, at + name);
                else if (name == "shiftDown") check_eq(dt.shift_down() == want.b, at + name);
                else if (name == "cycleLock") check_eq(dt.cycle_lock() == static_cast<int>(want.n), at + name);
                else if (name == "toggleRange") check_eq(dt.toggle_range(s["omegaIn"][0].n * r) == want.b, at + name);
                else if (name == "setLock") dt.set_lock(2);
                else if (name == "shift") check_eq(dt.shift(i == 600 ? -1 : 2) == want.b, at + name);
                else if (name == "reset") dt.reset();
                else check_eq(false, at + "unknown event " + name);
            }

            WheelArray omegaIn{};
            for (size_t k = 0; k < 4; k++) omegaIn[k] = s["omegaIn"][k].n;
            const WheelArray drive = dt.update(h, s["throttle"].n, omegaIn);
            const WheelArray brakes = dt.brake_torques(s["brake"].n, s["handbrake"].n);
            WheelArray omega{};
            for (size_t k = 0; k < 4; k++) {
                check_near(drive[k], s["drive"][k].n, at + "drive", 1e-9, 1e-7);
                check_near(brakes[k], s["brakes"][k].n, at + "brakes");
                omega[k] = s["omegaPreDiff"][k].n;
            }
            dt.apply_diffs(omega, h);
            for (size_t k = 0; k < 4; k++) check_near(omega[k], s["omegaOut"][k].n, at + "diffs");

            const auto& st = dt.state();
            check_near(dt.ratio(), s["ratio"].n, at + "ratio");
            check_eq(st.gear == static_cast<int>(s["gear"].n), at + "gear");
            check_eq(st.lowRange == s["lowRange"].b, at + "lowRange");
            check_eq(st.diffLock == static_cast<int>(s["lock"].n), at + "lock");
            check_near(st.omegaE, s["omegaE"].n, at + "omegaE", 1e-9, 1e-7);
            check_near(st.engineTorque, s["engineTorque"].n, at + "engineTorque", 1e-9, 1e-7);
            check_near(st.clutchTorque, s["clutchTorque"].n, at + "clutchTorque", 1e-9, 1e-7);
            check_near(st.shiftTimer, s["shiftTimer"].n, at + "shiftTimer");
            i++;
        }
    }

    std::printf("%d checks, %d failed, worst relative error %.3g\n", g_checks, g_fail, g_worst_rel);
    return g_fail == 0 ? 0 : 1;
}
