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

#include "core/medium.h"
#include "core/register.h"

#include <enoki/array.h>

#include <memory>

namespace lux2
{

    class ClearMedium : public Medium
    {
    public:
        static std::shared_ptr<Medium> CreateMedium(const PluginContext &)
        {
            return std::make_shared<ClearMedium>();
        }

        uint32_t Flags() const override { return MF_Analytic; }

        SWCSpectrumP SigmaA(const SpectrumWavelengthsP &, const Point3fP &,
                            MaskP) const override
        {
            return SWCSpectrumP(0.f);
        }

        SWCSpectrumP SigmaS(const SpectrumWavelengthsP &, const Point3fP &,
                            MaskP) const override
        {
            return SWCSpectrumP(0.f);
        }

        void SampleInteraction(const SpectrumWavelengthsP &, const RayP &,
                               const FloatP &, const FloatP &t1, const FloatP &,
                               RNGP &, MediumSampleP *out,
                               MaskP active) const override
        {
            if (none(active))
                return;
            // Reach the segment end unchanged.
            enoki::masked(out->d, active) = t1;
            enoki::masked(out->scattered, active) = MaskP(false);
            enoki::masked(out->weight, active) = SWCSpectrumP(1.f);
            enoki::masked(out->emission, active) = SWCSpectrumP(0.f);
            enoki::masked(out->sigma_s, active) = SWCSpectrumP(0.f);
            enoki::masked(out->sigma_t, active) = SWCSpectrumP(0.f);
            enoki::masked(out->k, active) = FloatP(0.f);
        }

        SWCSpectrumP Transmittance(const SpectrumWavelengthsP &, const RayP &,
                                   const FloatP &, const FloatP &, RNGP &,
                                   MaskP) const override
        {
            return SWCSpectrumP(1.f);
        }

        // Zero keeps dead lanes out of NEE.
        FloatP PhasePdf(const SpectrumWavelengthsP &, const Point3fP &,
                        const Vector3fP &, const Vector3fP &,
                        MaskP) const override
        {
            return FloatP(0.f);
        }
    };

} // namespace lux2

LUX2_REGISTER_MEDIUM(lux2::ClearMedium, "clear");
