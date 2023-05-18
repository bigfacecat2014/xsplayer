/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2014, 2016 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "CoordGeom.h"
#include "DSUtil/DSUtil.h"

static bool IsZero(float d)
{
    return IsEqual(d, 0.0f);
}

//
// Vector3d
//

Vector3d::Vector3d(float _x, float _y, float _z)
    : x(_x), y(_y), z(_z)
{
}

void Vector3d::Set(float _x, float _y, float _z)
{
    x = _x;
    y = _y;
    z = _z;
}

float Vector3d::Length() const
{
    return sqrt(x * x + y * y + z * z);
}

float Vector3d::Sum() const
{
    return (x + y + z);
}

float Vector3d::CrossSum() const
{
    return (x * y + x * z + y * z);
}

Vector3d Vector3d::Cross() const
{
    return Vector3d(x * y, x * z, y * z);
}

Vector3d Vector3d::Pow(float exp) const
{
    return (exp == 0.0f ? Vector3d(1.0f, 1.0f, 1.0f) : exp == 1.0f ? *this : Vector3d(pow(x, exp), pow(y, exp), pow(z, exp)));
}

Vector3d Vector3d::Unit() const
{
    float l = Length();
    if (!l || l == 1.0f) {
        return *this;
    }
    return (*this * (1.0f / l));
}

Vector3d& Vector3d::Unitalize()
{
    return (*this = Unit());
}

Vector3d Vector3d::Normal(const Vector3d& a, const Vector3d& b) const
{
    return ((a - *this) % (b - a));
}

float Vector3d::Angle(const Vector3d& a, const Vector3d& b) const
{
    return (((a - *this).Unit()).Angle((b - *this).Unit()));
}

float Vector3d::Angle(const Vector3d& a) const
{
    float angle = *this | a;
    return ((angle > 1.0f) ? 0.0f : (angle < -1.0f) ? (float)M_PI : acos(angle));
}

void Vector3d::Angle(float& u, float& v) const
{
    Vector3d n = Unit();

    u = asin(n.y);

    if (IsZero(n.z)) {
        v = (float)M_PI_2 * SGN(n.x);
    } else if (n.z > 0) {
        v = atan(n.x / n.z);
    } else if (n.z < 0) {
        v = IsZero(n.x) ? (float)M_PI : ((float)M_PI * SGN(n.x) + atan(n.x / n.z));
    }
}

Vector3d Vector3d::Angle() const
{
    Vector3d ret;
    Angle(ret.x, ret.y);
    ret.z = 0;
    return ret;
}

Vector3d& Vector3d::Min(const Vector3d& a)
{
    x = (x < a.x) ? x : a.x;
    y = (y < a.y) ? y : a.y;
    z = (z < a.z) ? z : a.z;
    return *this;
}

Vector3d& Vector3d::Max(const Vector3d& a)
{
    x = (x > a.x) ? x : a.x;
    y = (y > a.y) ? y : a.y;
    z = (z > a.z) ? z : a.z;
    return *this;
}

Vector3d Vector3d::Abs() const
{
    return Vector3d(fabs(x), fabs(y), fabs(z));
}

Vector3d Vector3d::Reflect(const Vector3d& n) const
{
    return (n * ((-*this) | n) * 2 - (-*this));
}

Vector3d Vector3d::Refract(const Vector3d& N, float nFront, float nBack, float* nOut /*= nullptr*/) const
{
    Vector3d D = -*this;

    float N_dot_D = (N | D);
    float n = N_dot_D >= 0.0f ? (nFront / nBack) : (nBack / nFront);

    Vector3d cos_D = N * N_dot_D;
    Vector3d sin_T = (cos_D - D) * n;

    float len_sin_T = sin_T | sin_T;

    if (len_sin_T > 1.0f) {
        if (nOut) {
            *nOut = N_dot_D >= 0.0f ? nFront : nBack;
        }
        return this->Reflect(N);
    }

    float N_dot_T = (float)sqrt(1.0f - len_sin_T);
    if (N_dot_D < 0.0f) {
        N_dot_T = -N_dot_T;
    }

    if (nOut) {
        *nOut = N_dot_D >= 0.0f ? nBack : nFront;
    }

    return (sin_T - (N * N_dot_T));
}

Vector3d Vector3d::Refract2(const Vector3d& N, float nFrom, float nTo, float* nOut /*= nullptr*/) const
{
    Vector3d D = -*this;

    float N_dot_D = (N | D);
    float n = nFrom / nTo;

    Vector3d cos_D = N * N_dot_D;
    Vector3d sin_T = (cos_D - D) * n;

    float len_sin_T = sin_T | sin_T;

    if (len_sin_T > 1.0f) {
        if (nOut) {
            *nOut = nFrom;
        }
        return this->Reflect(N);
    }

    float N_dot_T = (float)sqrt(1.0f - len_sin_T);
    if (N_dot_D < 0.0f) {
        N_dot_T = -N_dot_T;
    }

    if (nOut) {
        *nOut = nTo;
    }

    return (sin_T - (N * N_dot_T));
}

float Vector3d::operator | (const Vector3d& v) const
{
    return (x * v.x + y * v.y + z * v.z);
}

Vector3d Vector3d::operator % (const Vector3d& v) const
{
    return Vector3d(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
}

float& Vector3d::operator [](size_t i)
{
    return (!i ? x : (i == 1) ? y : z);
}

Vector3d Vector3d::operator - () const
{
    return Vector3d(-x, -y, -z);
}

bool Vector3d::operator == (const Vector3d& v) const
{
    return (IsZero(x - v.x) && IsZero(y - v.y) && IsZero(z - v.z));
}

bool Vector3d::operator != (const Vector3d& v) const
{
    return !(*this == v);
}

Vector3d Vector3d::operator + (float d) const
{
    return Vector3d(x + d, y + d, z + d);
}

Vector3d Vector3d::operator + (const Vector3d& v) const
{
    return Vector3d(x + v.x, y + v.y, z + v.z);
}

Vector3d Vector3d::operator - (float d) const
{
    return Vector3d(x - d, y - d, z - d);
}

Vector3d Vector3d::operator - (const Vector3d& v) const
{
    return Vector3d(x - v.x, y - v.y, z - v.z);
}

Vector3d Vector3d::operator * (float d) const
{
    return Vector3d(x * d, y * d, z * d);
}

Vector3d Vector3d::operator * (const Vector3d& v) const
{
    return Vector3d(x * v.x, y * v.y, z * v.z);
}

Vector3d Vector3d::operator / (float d) const
{
    return Vector3d(x / d, y / d, z / d);
}

Vector3d Vector3d::operator / (const Vector3d& v) const
{
    return Vector3d(x / v.x, y / v.y, z / v.z);
}

Vector3d& Vector3d::operator += (float d)
{
    x += d;
    y += d;
    z += d;
    return *this;
}

Vector3d& Vector3d::operator += (const Vector3d& v)
{
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
}

Vector3d& Vector3d::operator -= (float d)
{
    x -= d;
    y -= d;
    z -= d;
    return *this;
}

Vector3d& Vector3d::operator -= (Vector3d& v)
{
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
}

Vector3d& Vector3d::operator *= (float d)
{
    x *= d;
    y *= d;
    z *= d;
    return *this;
}

Vector3d& Vector3d::operator *= (const Vector3d& v)
{
    x *= v.x;
    y *= v.y;
    z *= v.z;
    return *this;
}

Vector3d& Vector3d::operator /= (float d)
{
    x /= d;
    y /= d;
    z /= d;
    return *this;
}

Vector3d& Vector3d::operator /= (const Vector3d& v)
{
    x /= v.x;
    y /= v.y;
    z /= v.z;
    return *this;
}

//
// Ray3d
//

Ray3d::Ray3d(const Vector3d& _p, const Vector3d& _d)
    : p(_p)
    , d(_d)
{
}

void Ray3d::Set(const Vector3d& _p, const Vector3d& _d)
{
    p = _p;
    d = _d;
}

float Ray3d::GetDistanceFrom(const Ray3d& r) const
{
    float t = (d | r.d);
    if (IsZero(t)) {
        return -std::numeric_limits<float>::infinity();    // plane is parallel to the ray, return -infinite
    }
    return (((r.p - p) | r.d) / t);
}

float Ray3d::GetDistanceFrom(const Vector3d& v) const
{
    float t = ((v - p) | d) / (d | d);
    return ((p + d * t) - v).Length();
}

Vector3d Ray3d::operator [](float t) const
{
    return (p + d * t);
}
