#include "worldcore/drivetrain.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace worldcore {

namespace {

constexpr double RPM_TO_RAD = std::numbers::pi / 30;
constexpr double RAD_TO_RPM = 30 / std::numbers::pi;

double clamp(double v, double a, double b) { return std::min(b, std::max(a, v)); }
double clamp01(double v) { return clamp(v, 0, 1); }
double smooth(double a, double b, double x) {
    const double t = clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}

double torque_fraction(double rpm, const Tune& tune) {
    if (rpm <= 0) return 0;
    const double x = rpm / tune.peakTorqueRpm;
    double f = x < 1 ? 1 - 0.55 * std::pow(1 - x, 1.6) : 1 - 0.38 * std::pow(x - 1, 1.7);
    f *= 1 - smooth(tune.redlineRpm, tune.redlineRpm + 220, rpm);
    return clamp(f, 0, 1);
}

void lock_pair(WheelArray& w, size_t a, size_t b, double k) {
    const double mean = (w[a] + w[b]) * 0.5;
    w[a] += (mean - w[a]) * k;
    w[b] += (mean - w[b]) * k;
}

void preload(WheelArray& w, size_t a, size_t b, double maxTorque, double inertia, double dt) {
    const double t = clamp((w[b] - w[a]) * 25, -maxTorque, maxTorque);
    const double dw = t * dt / inertia;
    w[a] += dw;
    w[b] -= dw;
}

}  // namespace

Drivetrain::Drivetrain(const VehiclePerf& perf, const Tune& tune, double wheelInertia)
    : perf_(perf),
      tune_(tune),
      wheelInertia_(wheelInertia),
      peakPower_(perf.power * 1000),
      idleW_(tune.idleRpm * RPM_TO_RAD),
      stallW_(tune.stallRpm * RPM_TO_RAD),
      limitW_((tune.redlineRpm + 400) * RPM_TO_RAD),
      clutchCap_(perf.torque * tune.clutchCapacity) {
    state_.omegaE = idleW_;
    state_.rpm = tune.idleRpm;
}

double Drivetrain::ratio() const {
    if (state_.gear == kNeutral || !state_.running) return 0;
    const double g = state_.gear == kReverse
        ? -perf_.gearRatios[0] * tune_.reverseRatio
        : perf_.gearRatios[static_cast<size_t>(state_.gear - 1)];
    return g * perf_.finalDrive * (state_.lowRange ? perf_.lowRangeRatio : 1);
}

bool Drivetrain::shift(int to) {
    const int g = std::clamp(to, kReverse, perf_.forwardGears);
    if (g == state_.gear) return false;
    state_.gear = g;
    state_.shiftTimer = tune_.shiftTime;
    return true;
}

int Drivetrain::cycle_lock() {
    state_.diffLock = (state_.diffLock + 1) % 3;
    return state_.diffLock;
}

void Drivetrain::set_lock(int lock) { state_.diffLock = std::clamp(lock, 0, 2); }

bool Drivetrain::toggle_range(double speed) {
    if (std::fabs(speed) > 2.2) return false;
    state_.lowRange = !state_.lowRange;
    return true;
}

const char* Drivetrain::gear_name() const {
    static const char* const kNames[] = {"R", "N", "1", "2", "3", "4", "5", "6", "7", "8"};
    return kNames[std::clamp(state_.gear + 1, 0, 9)];
}

const char* Drivetrain::lock_name() const {
    static const char* const kNames[] = {"OPEN", "CENTRE", "FULL"};
    return kNames[state_.diffLock];
}

WheelArray Drivetrain::update(double dt, double throttle, const WheelArray& w) {
    if (state_.shiftTimer > 0) state_.shiftTimer = std::max(0.0, state_.shiftTimer - dt);

    const double r = ratio();
    const double wOut = (w[0] + w[1] + w[2] + w[3]) * 0.25;
    const double wIn = wOut * r;

    // engine
    const double rpm = state_.omegaE * RAD_TO_RPM;
    double te = torque_fraction(rpm, tune_) * perf_.torque * throttle;
    if (state_.omegaE > 1) te = std::min(te, peakPower_ / state_.omegaE);
    te -= tune_.engineFrictionA + tune_.engineFrictionB * state_.omegaE;
    if (state_.omegaE < idleW_) te += (idleW_ - state_.omegaE) * 3.2 * (1 - 0.6 * throttle);
    state_.engineTorque = te;

    // clutch
    double tc = 0;
    if (r != 0 && state_.shiftTimer <= 0) {
        const double engage = clamp01((std::fabs(wIn) - idleW_ * 0.30) / (idleW_ * 0.85));
        const double antiStall = smooth(stallW_, idleW_ * 1.05, state_.omegaE);
        const double cap = clutchCap_ * std::max(engage, tune_.clutchCreep + 0.6 * throttle) * antiStall;
        const double iDrive = std::max(1e-4, (wheelInertia_ * 4) / (r * r));
        const double tLock = (state_.omegaE - wIn) / (dt * (1 / tune_.engineInertia + 1 / iDrive));
        tc = clamp(tLock, -cap, cap);
    }
    state_.clutchTorque = tc;

    state_.omegaE += (te - tc) * dt / tune_.engineInertia;
    state_.omegaE = clamp(state_.omegaE, stallW_ * 0.4, limitW_);
    state_.rpm = state_.omegaE * RAD_TO_RPM;

    // open diffs: equal torque to every output
    const double prop = tc * r * tune_.drivelineEff;
    state_.propTorque = prop;
    const double quarter = prop * 0.25;
    return {quarter, quarter, quarter, quarter};
}

void Drivetrain::apply_diffs(WheelArray& w, double dt) const {
    const double k = clamp01(tune_.lockerStrength);
    const bool centreLocked = state_.lowRange || state_.diffLock >= LOCK_CENTRE;
    const bool axlesLocked = state_.diffLock >= LOCK_FULL;

    if (axlesLocked) {
        lock_pair(w, 0, 1, k);
        lock_pair(w, 2, 3, k);
    } else {
        preload(w, 0, 1, tune_.openDiffPreload, wheelInertia_, dt);
        preload(w, 2, 3, tune_.openDiffPreload, wheelInertia_, dt);
    }

    if (centreLocked) {
        const double fa = (w[0] + w[1]) * 0.5;
        const double ra = (w[2] + w[3]) * 0.5;
        const double mean = (fa + ra) * 0.5;
        const double df = (mean - fa) * k;
        const double dr = (mean - ra) * k;
        w[0] += df;
        w[1] += df;
        w[2] += dr;
        w[3] += dr;
    } else {
        preload(w, 0, 2, tune_.openDiffPreload, wheelInertia_, dt);
        preload(w, 1, 3, tune_.openDiffPreload, wheelInertia_, dt);
    }
}

WheelArray Drivetrain::brake_torques(double brake, double handbrake) const {
    const double base = perf_.brakeTorque * tune_.brakeScale;
    const double f = base * tune_.brakeBiasFront * 0.5 * brake;
    const double r = base * (1 - tune_.brakeBiasFront) * 0.5 * brake;
    const double hb = base * (1 - tune_.brakeBiasFront) * 0.5 * tune_.handbrakeFraction * handbrake;
    return {f, f, std::max(r, hb), std::max(r, hb)};
}

void Drivetrain::reset() {
    state_.omegaE = idleW_;
    state_.rpm = tune_.idleRpm;
    state_.shiftTimer = 0;
    state_.clutchTorque = 0;
    state_.propTorque = 0;
}

}  // namespace worldcore
