// frank-war1 — the transcendental functions war1 calls, the same on every
// build (included by war1.c after <math.h> in the frank build).
//
// A recorded game replays on the board only if both builds compute exactly
// the same floats. +, -, *, /, sqrtf, floorf, ceilf and roundf are exact in
// IEEE 754 (with -ffp-contract=off: no fused multiply-adds), but every libm
// rounds sinf, cosf, atan2f and acosf its own way, and one ulp is enough for
// a unit to pick another path a minute later. These use only exact
// operations, so they give the same bits everywhere (accuracy ~1e-6, plenty
// for a game on a 16-pixel grid).
//
// Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
// SPDX-License-Identifier: Zlib
#pragma once

#include <math.h>

#define WDET_PI      3.14159265358979323846f
#define WDET_HALF_PI 1.57079632679489661923f
#define WDET_TWO_PI  6.28318530717958647692f

// sin on [-pi/2, pi/2]: odd minimax polynomial, degree 11
static inline float wdet_sinCore(float x)
{
    const float x2 = x * x;
    float p = -2.3889859e-08f;
    p = p * x2 + 2.7525562e-06f;
    p = p * x2 - 1.9840874e-04f;
    p = p * x2 + 8.3333310e-03f;
    p = p * x2 - 1.6666667e-01f;
    return x + x * x2 * p;
}

static inline float wdet_sinf(float x)
{
    // to [-pi, pi], then fold to [-pi/2, pi/2]
    x = x - WDET_TWO_PI * roundf(x / WDET_TWO_PI);
    if (x > WDET_HALF_PI) x = WDET_PI - x;
    else if (x < -WDET_HALF_PI) x = -WDET_PI - x;
    return wdet_sinCore(x);
}

static inline float wdet_cosf(float x)
{
    return wdet_sinf(x + WDET_HALF_PI);
}

// atan on [0, 1]: minimax polynomial in x^2
static inline float wdet_atanCore(float x)
{
    const float x2 = x * x;
    float p = 2.8662257e-03f;
    p = p * x2 - 1.6197170e-02f;
    p = p * x2 + 4.2909615e-02f;
    p = p * x2 - 7.5289640e-02f;
    p = p * x2 + 1.0656264e-01f;
    p = p * x2 - 1.4201230e-01f;
    p = p * x2 + 1.9993554e-01f;
    p = p * x2 - 3.3333147e-01f;
    return x + x * x2 * p;
}

static inline float wdet_atan2f(float y, float x)
{
    const float ax = fabsf(x), ay = fabsf(y);
    if (ax == 0.0f && ay == 0.0f)
        return 0.0f;
    float a = ax >= ay ? wdet_atanCore(ay / ax) : WDET_HALF_PI - wdet_atanCore(ax / ay);
    if (x < 0.0f) a = WDET_PI - a;
    return y < 0.0f ? -a : a;
}

static inline float wdet_acosf(float x)
{
    if (x >= 1.0f) return 0.0f;
    if (x <= -1.0f) return WDET_PI;
    return wdet_atan2f(sqrtf((1.0f - x) * (1.0f + x)), x);
}

#define sinf(x)     wdet_sinf(x)
#define cosf(x)     wdet_cosf(x)
#define atan2f(y,x) wdet_atan2f((y), (x))
#define acosf(x)    wdet_acosf(x)
