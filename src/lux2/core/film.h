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

#ifndef LUX2_FILM_H
#define LUX2_FILM_H

#include "core/vecp.h"
#include "core/spectrum.h"

namespace lux2
{

    // Accumulates radiance contributions into pixels and writes the image.
    class Film
    {
    public:
        virtual ~Film() = default;

        // Film resolution in pixels.
        virtual int XRes() const = 0;
        virtual int YRes() const = 0;

        // Splat a packet of filter-weighted contributions.
        virtual void Splat(const FloatP &x, const FloatP &y,
                           const SWCSpectrumP &rgb, const FloatP &alpha,
                           const FloatP &weight, int bufferId) = 0;

        // Merge another film's accumulated buffer into this one.
        virtual void Merge(Film *other) = 0;

        // Write the current accumulated image to disk.
        virtual void WriteImage() = 0;

        // Write a resume FLM file.
        virtual void WriteFLM() {}
    };

} // namespace lux2

#endif // LUX2_FILM_H
