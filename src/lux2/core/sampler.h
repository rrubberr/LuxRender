/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   LuxRender is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   LuxRender is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program. If not, see <http://www.gnu.org/licenses/>   *
 *                                                                         *
 *   This project is based on PBRT; see <http://www.pbrt.org>              *
 ***************************************************************************/

#ifndef LUX2_SAMPLER_H
#define LUX2_SAMPLER_H

// Abstract sampler interface.

#include "core/vecp.h"
#include "core/geometry.h"

namespace lux2 {

// Integer pixel coordinate.
struct Point2i { int x; int y; };

// Produces random streams consumed by the integrator.
class Sampler {
public:
    virtual ~Sampler() = default;

    // Declare that one more random stream is needed per sample; returns its offset.
    virtual int AddSample() = 0;

    // Begin a new pixel.
    virtual void StartPixel(const Point2i& p) = 0;

    // Begin a new sample within the current pixel.
    virtual void StartSample() = 0;

    // Fetch the value of stream `offset` for the current sample.
    // Implementations return a broadcast or per-lane jittered
    // FloatP as appropriate.
    virtual FloatP Get(int offset) = 0;

    // Total number of samples per pixel.
    virtual int SampleCount() const = 0;

    // Number of streams declared via AddSample().
    virtual int StreamCount() const = 0;
};

} // namespace lux2

#endif // LUX2_SAMPLER_H
