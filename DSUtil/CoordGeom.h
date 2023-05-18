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

#pragma once

class Vector3d
{
public:
    float x, y, z;

    Vector3d() { x = y = z = 0.0f; }
    Vector3d(float x, float y, float z);
    void Set(float x, float y, float z);

    Vector3d Normal(const Vector3d& a, const Vector3d& b) const;
    float Angle(const Vector3d& a, const Vector3d& b) const;
    float Angle(const Vector3d& a) const;
    void Angle(float& u, float& v) const;   // returns spherical coords in radian, -M_PI_2 <= u <= M_PI_2, -M_PI <= v <= M_PI
    Vector3d Angle() const;                   // does like prev., returns 'u' in 'ret.x', and 'v' in 'ret.y'

    Vector3d Unit() const;
    Vector3d& Unitalize();
    float Length() const;
    float Sum() const;                      // x + y + z
    float CrossSum() const;                 // xy + xz + yz
    Vector3d Cross() const;                   // xy, xz, yz
    Vector3d Pow(float exp) const;

    Vector3d& Min(const Vector3d& a);
    Vector3d& Max(const Vector3d& a);
    Vector3d Abs() const;

    Vector3d Reflect(const Vector3d& n) const;
    Vector3d Refract(const Vector3d& n, float nFront, float nBack, float* nOut = nullptr) const;
    Vector3d Refract2(const Vector3d& n, float nFrom, float nTo, float* nOut = nullptr) const;

    Vector3d operator - () const;
    float& operator [](size_t i);

    float operator | (const Vector3d& v) const;   // dot
    Vector3d operator % (const Vector3d& v) const;  // cross

    bool operator == (const Vector3d& v) const;
    bool operator != (const Vector3d& v) const;

    Vector3d operator + (float d) const;
    Vector3d operator + (const Vector3d& v) const;
    Vector3d operator - (float d) const;
    Vector3d operator - (const Vector3d& v) const;
    Vector3d operator * (float d) const;
    Vector3d operator * (const Vector3d& v) const;
    Vector3d operator / (float d) const;
    Vector3d operator / (const Vector3d& v) const;
    Vector3d& operator += (float d);
    Vector3d& operator += (const Vector3d& v);
    Vector3d& operator -= (float d);
    Vector3d& operator -= (Vector3d& v);
    Vector3d& operator *= (float d);
    Vector3d& operator *= (const Vector3d& v);
    Vector3d& operator /= (float d);
    Vector3d& operator /= (const Vector3d& v);

    template<typename T> static float DegToRad(T angle) { return (float)(angle * M_PI / 180); }
};

class Ray3d
{
public:
    Vector3d p;
    Vector3d d;

    Ray3d() {}
    Ray3d(const Vector3d& p, const Vector3d& d);
    void Set(const Vector3d& p, const Vector3d& d);

    float GetDistanceFrom(const Ray3d& r) const;      // r = plane
    float GetDistanceFrom(const Vector3d& v) const;   // v = point

    Vector3d operator [](float t) const;
};
