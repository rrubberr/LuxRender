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

#ifndef LUX2_MATERIAL_NULL_H
#define LUX2_MATERIAL_NULL_H

#include "core/material.h"
#include "core/bsdf.h"
#include "core/bsdf_type.h"

#include <memory>

namespace lux2
{

    class PluginContext;

    class NullMaterial : public Material, public BSDF
    {
    public:
        uint32_t flags() const override
        {
            return uint32_t(BSDFType::Null) | uint32_t(BSDFType::FrontSide);
        }

        const BSDF *GetBSDF(const DifferentialGeometryP &) const override { return this; }

        void SampleF(const SpectrumWavelengthsP &, const Vector3fP &,
                     const DifferentialGeometryP &, const FloatP &, const FloatP &,
                     const FloatP &, BSDFSampleP *s, TransportMode,
                     MaskP active) const override
        {
            enoki::masked(s->pdf, active) = FloatP(0.f);
            enoki::masked(s->f, active) = FloatP(0.f);
            enoki::masked(s->sampledType, active) = UInt32P(uint32_t(BSDFType::Null));
            enoki::masked(s->specular, active) = MaskP(true);
        }

        FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
                   const DifferentialGeometryP &, uint32_t, TransportMode,
                   MaskP) const override
        {
            return FloatP(0.f);
        }

        void Eval(const SpectrumWavelengthsP &, const Vector3fP &, const Vector3fP &,
                  const DifferentialGeometryP &, TransportMode, BSDFEvalP *out,
                  MaskP active) const override
        {
            enoki::masked(out->f, active) = SWCSpectrumP(0.f);
            enoki::masked(out->pdf, active) = FloatP(0.f);
            enoki::masked(out->pdfRev, active) = FloatP(0.f);
        }

        static std::shared_ptr<Material> CreateMaterial(const PluginContext &ctx);
    };

} // namespace lux2

#endif // LUX2_MATERIAL_NULL_H
