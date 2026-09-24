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

#ifndef LUX2_FRESNEL_H
#define LUX2_FRESNEL_H

#include "core/spectrum.h"

namespace lux2
{

    // Dielectric interface reflectance.
    SWCSpectrumP FrDiel(const FloatP &cosi, const FloatP &cost,
                        const SWCSpectrumP &etai, const SWCSpectrumP &etat);

    // Dielectric reflectance with a precomputed relative IOR eta = etat / etai.
    SWCSpectrumP FrDiel2(const FloatP &cosi, const SWCSpectrumP &cost,
                         const SWCSpectrumP &eta);

    // Complex IOR reflectance at cos(theta_i) = cosi.
    SWCSpectrumP FrCond(const FloatP &cosi, const SWCSpectrumP &eta,
                        const SWCSpectrumP &k);

    // Full conductor reflectance with both cos(theta_i) and cos(theta_t).
    SWCSpectrumP FrFull(const FloatP &cosi, const SWCSpectrumP &cost,
                        const SWCSpectrumP &eta, const SWCSpectrumP &k);

    // Approximate real IOR from a reflectance Fr in [0, 1]:
    //   (1 + sqrt(Fr)) / (1 - sqrt(Fr)), Fr clamped to [0, .999].
    SWCSpectrumP FresnelApproxEta(const SWCSpectrumP &Fr);

    // Approximate imaginary IOR from a reflectance Fr in [0, 1]:
    //   2 * sqrt(Fr / (1 - Fr)), Fr clamped to [0, .999].
    SWCSpectrumP FresnelApproxK(const SWCSpectrumP &Fr);
    // Evaluate a FresnelGeneralP at cos(theta_i).
    struct FresnelGeneralP;
    SWCSpectrumP FresnelGeneralEvaluate(const FresnelGeneralP &fg, const FloatP &cosi);
} // namespace lux2

#endif // LUX2_FRESNEL_H
