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

// Spectral -> XYZ for ERPT deposits.

#ifndef LUX2_INTEGRATORS_ERPT_DEPOSIT_H
#define LUX2_INTEGRATORS_ERPT_DEPOSIT_H

#include "integrators/lane/math.h"
#include "core/color/color.h"

namespace lux2
{
    namespace erpt
    {
        // wl/amt: PACKET_WIDTH floats in; X/Y/Z: PACKET_WIDTH floats out.
        // Lanes with amt == 0 must produce 0.
        inline void XYZOfBatch(const float *wl, const float *amt, float *X,
                               float *Y, float *Z)
        {
            SpectrumWavelengthsP sw;
            sw.FromWavelength(enoki::load_unaligned<FloatP>(wl));
            const SWCSpectrumP L(enoki::load_unaligned<FloatP>(amt));
            const XYZColorP xyz = SWCToXYZ(L, sw);
            FloatP x = xyz.X(), y = xyz.Y(), z = xyz.Z();
            enoki::store_unaligned(X, x);
            enoki::store_unaligned(Y, y);
            enoki::store_unaligned(Z, z);
        }
    } // namespace erpt
} // namespace lux2

#endif // LUX2_INTEGRATORS_ERPT_DEPOSIT_H
