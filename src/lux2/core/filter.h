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

#ifndef LUX2_FILTER_H
#define LUX2_FILTER_H

namespace lux2 {

// Abstract pixel reconstruction filter.
class Filter {
public:
    virtual ~Filter() = default;

    // Filter weight at an offset (dx, dy) from the pixel center.
    virtual float Evaluate(float dx, float dy) const = 0;

    // Filter footprint half-widths in pixel units.
    virtual float GetXWidth() const = 0;
    virtual float GetYWidth() const = 0;
};

}  // namespace lux2

#endif  // LUX2_FILTER_H
