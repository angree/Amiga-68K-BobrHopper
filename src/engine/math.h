// Math with three.js conventions, so constants and animations from the TypeScript port 1:1:
// column-major Matrix4 (same element order as three's .elements), Euler order XYZ,
// Object3D matrix = compose(position, quaternion(euler), scale), Matrix4.lookAt, OrthographicCamera.
//
// Numbers are `real` / `mreal` (real.h): double / float on PC and R36S, 16.16 fixed point on the SF2000.
#pragma once

#include <cmath>

#include "real.h"

namespace cr {

constexpr float PI = 3.14159265358979323846f;

// Vec3 and Quat are `real` (double on PC/R36S, like JavaScript numbers in three.js: game logic accumulates positions
// over hundreds of ticks and compares them against thresholds - a train wraps at x > 110 after 275 x 0.8 steps - so
// float storage changes the tick things happen on). Only Mat4 (what goes to the GPU) is `mreal`.
struct Vec3 {
    real x = 0, y = 0, z = 0;
    Vec3() = default;
    constexpr Vec3(real x_, real y_, real z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(real s) const { return {x * s, y * s, z * s}; }
    Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
    void set(real x_, real y_, real z_) { x = x_; y = y_; z = z_; }
};

inline real dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline real length(const Vec3 &v) { return rsqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3 &v)
{
    real l = length(v);
    return l > 0 ? v * (real(1) / l) : v;
}

struct Quat {
    real x = 0, y = 0, z = 0, w = 1;
};

// THREE.Quaternion.setFromEuler with order 'XYZ'
inline Quat quatFromEulerXYZ(const Vec3 &e)
{
    // most scene nodes are not rotated: cos(0) = 1 and sin(0) = 0 exactly, so this is the same quaternion
    if (e.x == 0 && e.y == 0 && e.z == 0) return Quat();
    const real hx = rhalf(e.x), hy = rhalf(e.y), hz = rhalf(e.z); // e / 2 (without a 64-bit division on the SF2000)
    real c1 = rcos(hx), c2 = rcos(hy), c3 = rcos(hz);
    real s1 = rsin(hx), s2 = rsin(hy), s3 = rsin(hz);
    Quat q;
    q.x = s1 * c2 * c3 + c1 * s2 * s3;
    q.y = c1 * s2 * c3 - s1 * c2 * s3;
    q.z = c1 * c2 * s3 + s1 * s2 * c3;
    q.w = c1 * c2 * c3 - s1 * s2 * s3;
    return q;
}

struct Mat4 {
    mreal e[16]; // column-major, identical to THREE.Matrix4.elements

    static Mat4 identity()
    {
        Mat4 m;
        for (int i = 0; i < 16; i++) m.e[i] = (i % 5 == 0) ? mreal(1.0f) : mreal(0.0f);
        return m;
    }

    // THREE.Matrix4.multiplyMatrices(a, b) -> a * b
    friend Mat4 operator*(const Mat4 &a, const Mat4 &b)
    {
        Mat4 r;
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++) {
                mreal s = 0;
                for (int k = 0; k < 4; k++) s += a.e[k * 4 + row] * b.e[c * 4 + k];
                r.e[c * 4 + row] = s;
            }
        return r;
    }

    Vec3 transformPoint(const Vec3 &v) const
    {
#ifdef CR_FIXED
        // O7.2: every scene, view and orthographic matrix is affine: w = 1/1 = 1 and x * 1 = x exactly in 16.16, so the
        // same result without a 64-bit division (a library call on the SF2000's MIPS32)
        if (e[3] == mreal(0) && e[7] == mreal(0) && e[11] == mreal(0) && e[15] == mreal(1))
            return {e[0] * v.x + e[4] * v.y + e[8] * v.z + e[12], e[1] * v.x + e[5] * v.y + e[9] * v.z + e[13],
                    e[2] * v.x + e[6] * v.y + e[10] * v.z + e[14]};
#endif
        mreal w = mreal(1.0f) / (e[3] * v.x + e[7] * v.y + e[11] * v.z + e[15]);
        return {(e[0] * v.x + e[4] * v.y + e[8] * v.z + e[12]) * w, (e[1] * v.x + e[5] * v.y + e[9] * v.z + e[13]) * w,
                (e[2] * v.x + e[6] * v.y + e[10] * v.z + e[14]) * w};
    }

    Vec3 transformDirection(const Vec3 &v) const
    {
        return normalize({e[0] * v.x + e[4] * v.y + e[8] * v.z, e[1] * v.x + e[5] * v.y + e[9] * v.z,
                          e[2] * v.x + e[6] * v.y + e[10] * v.z});
    }
};

// a * b for affine matrices (bottom row 0 0 0 1): 9 products per column instead of 16 (scene graph on the SF2000)
inline Mat4 mulAffine(const Mat4 &a, const Mat4 &b)
{
    Mat4 r;
    for (int c = 0; c < 4; c++) {
        const mreal *bc = b.e + c * 4;
        for (int row = 0; row < 3; row++)
            r.e[c * 4 + row] = a.e[row] * bc[0] + a.e[4 + row] * bc[1] + a.e[8 + row] * bc[2] + (c == 3 ? a.e[12 + row] : mreal(0.0f));
        r.e[c * 4 + 3] = c == 3 ? mreal(1.0f) : mreal(0.0f);
    }
    return r;
}

// THREE.Matrix4.compose
inline Mat4 compose(const Vec3 &p, const Quat &q, const Vec3 &s)
{
    Mat4 m;
    real x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    real xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
    real yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
    real wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    m.e[0] = (1 - (yy + zz)) * s.x;
    m.e[1] = (xy + wz) * s.x;
    m.e[2] = (xz - wy) * s.x;
    m.e[3] = 0;
    m.e[4] = (xy - wz) * s.y;
    m.e[5] = (1 - (xx + zz)) * s.y;
    m.e[6] = (yz + wx) * s.y;
    m.e[7] = 0;
    m.e[8] = (xz + wy) * s.z;
    m.e[9] = (yz - wx) * s.z;
    m.e[10] = (1 - (xx + yy)) * s.z;
    m.e[11] = 0;
    m.e[12] = p.x;
    m.e[13] = p.y;
    m.e[14] = p.z;
    m.e[15] = 1;
    return m;
}

inline Mat4 composeEuler(const Vec3 &p, const Vec3 &eulerXYZ, const Vec3 &s)
{
    return compose(p, quatFromEulerXYZ(eulerXYZ), s);
}

// THREE.Matrix4.lookAt(eye, target, up): rotation only (the camera's world rotation)
inline Mat4 lookAtRotation(const Vec3 &eye, const Vec3 &target, const Vec3 &up)
{
    Vec3 z = eye - target;
    if (dot(z, z) == 0) z.z = 1;
    z = normalize(z);
    Vec3 x = cross(up, z);
    if (dot(x, x) == 0) {
        if (rabs(up.z) == 1) z.x += real(0.0001f);
        else z.z += real(0.0001f);
        z = normalize(z);
        x = cross(up, z);
    }
    x = normalize(x);
    Vec3 y = cross(z, x);
    Mat4 m = Mat4::identity();
    m.e[0] = x.x; m.e[4] = y.x; m.e[8] = z.x;
    m.e[1] = x.y; m.e[5] = y.y; m.e[9] = z.y;
    m.e[2] = x.z; m.e[6] = y.z; m.e[10] = z.z;
    return m;
}

// Inverse of a rigid transform (rotation + translation), which is what a camera's matrixWorld is.
inline Mat4 inverseRigid(const Mat4 &m)
{
    Mat4 r = Mat4::identity();
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.e[j * 4 + i] = m.e[i * 4 + j];
    Vec3 t{m.e[12], m.e[13], m.e[14]};
    r.e[12] = -(r.e[0] * t.x + r.e[4] * t.y + r.e[8] * t.z);
    r.e[13] = -(r.e[1] * t.x + r.e[5] * t.y + r.e[9] * t.z);
    r.e[14] = -(r.e[2] * t.x + r.e[6] * t.y + r.e[10] * t.z);
    return r;
}

// General 4x4 inverse (THREE.Matrix4.invert); returns identity for a singular matrix.
Mat4 inverse(const Mat4 &m);

// THREE.OrthographicCamera.updateProjectionMatrix (WebGL coordinate system, no view offset)
inline Mat4 orthographic(mreal left, mreal right, mreal top, mreal bottom, mreal near, mreal far, mreal zoom)
{
    mreal dx = (right - left) / (2 * zoom), dy = (top - bottom) / (2 * zoom);
    mreal cx = (right + left) / 2, cy = (top + bottom) / 2;
    mreal l = cx - dx, r = cx + dx, t = cy + dy, b = cy - dy;
    mreal w = mreal(1.0f) / (r - l), h = mreal(1.0f) / (t - b), p = mreal(1.0f) / (far - near);
    Mat4 m = Mat4::identity();
    m.e[0] = 2 * w;
    m.e[5] = 2 * h;
    m.e[10] = -2 * p;
    m.e[12] = -(r + l) * w;
    m.e[13] = -(t + b) * h;
    m.e[14] = -(far + near) * p;
    return m;
}

#ifdef CR_FIXED
// 16.16 angles have no float drift to undo
inline real jsAngle(real a) { return a; }
#else
// Angles the original keeps as exact JavaScript doubles (0, ±PI/2, ±PI, ±(PI + PI/2), ±2PI) come back from
// float storage slightly off, which flips the sign of sin() at ±PI. Snap them back before trigonometry.
inline double jsAngle(double a)
{
    static const double PI_D = 3.141592653589793, PI_2_D = PI_D * 0.5;
    static const double candidates[] = {0,    PI_2_D, -PI_2_D, PI_D, -PI_D, PI_D + PI_2_D, -(PI_D + PI_2_D),
                                        2 * PI_D, -2 * PI_D};
    for (double c : candidates)
        if (std::fabs(a - c) < 1e-6) return c;
    return a;
}
#endif

// Math.atan2(Math.sin(a), Math.cos(a)) from the TS code, on the JavaScript double value
inline real normalizeAngle(real a)
{
    a = jsAngle(a);
    return rnormalizeAngle(a);
}

} // namespace cr
