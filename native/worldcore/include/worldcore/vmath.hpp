// vmath.hpp - the few vector/quaternion operations the vehicle model needs,
// with Three.js semantics (the reference implementation's maths library).
#pragma once

#include <algorithm>
#include <cmath>

namespace worldcore {

struct Vec3 {
    double x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length_sq() const { return dot(*this); }
    double length() const { return std::sqrt(length_sq()); }
    Vec3 normalized() const {
        const double l = length();
        return l > 0 ? Vec3{x / l, y / l, z / l} : Vec3{};
    }
};

struct Quat {
    double x = 0, y = 0, z = 0, w = 1;

    static Quat axis_angle(const Vec3& axis, double angle) {
        const double h = angle / 2, s = std::sin(h);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(h)};
    }

    /// Vector3.applyQuaternion
    Vec3 rotate(const Vec3& v) const {
        const double tx = 2 * (y * v.z - z * v.y);
        const double ty = 2 * (z * v.x - x * v.z);
        const double tz = 2 * (x * v.y - y * v.x);
        return {v.x + w * tx + y * tz - z * ty, v.y + w * ty + z * tx - x * tz, v.z + w * tz + x * ty - y * tx};
    }
};

/// Vector3.applyAxisAngle(axis, angle)
inline Vec3 apply_axis_angle(const Vec3& v, const Vec3& axis, double angle) {
    return Quat::axis_angle(axis, angle).rotate(v);
}

/// JS Math.sign: 0 for 0.
inline double js_sign(double v) { return v > 0 ? 1.0 : v < 0 ? -1.0 : 0.0; }

/// Euler.setFromQuaternion(q, 'YXZ') -> {x: pitch, y: heading, z: roll}
inline Vec3 euler_yxz(const Quat& q) {
    const double x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    const double xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
    const double yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
    const double wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    const double m11 = 1 - (yy + zz), m13 = xz + wy;
    const double m21 = xy + wz, m22 = 1 - (xx + zz), m23 = yz - wx;
    const double m31 = xz - wy, m33 = 1 - (xx + yy);
    Vec3 e;
    e.x = std::asin(-std::clamp(m23, -1.0, 1.0));
    if (std::fabs(m23) < 0.9999999) {
        e.y = std::atan2(m13, m33);
        e.z = std::atan2(m21, m22);
    } else {
        e.y = std::atan2(-m31, m11);
        e.z = 0;
    }
    return e;
}

}  // namespace worldcore
