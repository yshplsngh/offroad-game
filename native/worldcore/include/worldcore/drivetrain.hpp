// drivetrain.hpp - engine, clutch, gearbox, transfer case, differentials.
// Port of src/physics/drivetrain.js (browser reference, removed; read it with
// `git show a59773d:src/physics/drivetrain.js`). Its header explains the impulse
// clutch and the torque-side/speed-side diff split.
#pragma once

#include <array>

#include "worldcore/tune.hpp"

namespace worldcore {

inline constexpr int kReverse = -1;
inline constexpr int kNeutral = 0;

enum DiffLock : int { LOCK_OPEN = 0, LOCK_CENTRE = 1, LOCK_FULL = 2 };

using WheelArray = std::array<double, 4>;  // FL, FR, RL, RR

class Drivetrain {
public:
    struct State {
        double omegaE = 0;
        double rpm = 0;
        int gear = 1;
        bool lowRange = true;  // a crawler starts in low
        int diffLock = LOCK_OPEN;
        double shiftTimer = 0;
        double engineTorque = 0;
        double clutchTorque = 0;
        double propTorque = 0;
        bool running = true;
    };

    Drivetrain(const VehiclePerf& perf, const Tune& tune, double wheelInertia);

    const State& state() const { return state_; }

    /// Signed overall ratio from engine to wheel. 0 means no drive path.
    double ratio() const;
    bool shift(int to);
    bool shift_up() { return shift(state_.gear + 1); }
    bool shift_down() { return shift(state_.gear - 1); }
    int cycle_lock();
    void set_lock(int lock);
    /// Range changes need the driveline near-stationary, same as the real lever.
    bool toggle_range(double speed);
    const char* gear_name() const;
    const char* lock_name() const;

    /// Advance engine and clutch one step; returns per-wheel drive torque.
    WheelArray update(double dt, double throttle, const WheelArray& wheelOmega);
    /// Speed side of the diffs, after the wheels have integrated their spin.
    void apply_diffs(WheelArray& wheelOmega, double dt) const;
    WheelArray brake_torques(double brake, double handbrake) const;
    void reset();
    /// Restore everything that is not recomputed from inputs (save/load, resync).
    void restore(const State& s) { state_ = s; }

private:
    VehiclePerf perf_;
    Tune tune_;
    double wheelInertia_;
    double peakPower_, idleW_, stallW_, limitW_, clutchCap_;
    State state_;
};

}  // namespace worldcore
