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
#include "core/math.h"

#include <memory>

namespace lux2
{

    class PluginContext;

    // Null passes rays straight through. Modelled as a delta transmission
    // SpecularTransmission so the integrator keeps the path alive.
    class NullMaterial : public Material, public BSDF
    {
    public:
        uint32_t flags() const override
        {
            return uint32_t(BSDFType::Null) | uint32_t(BSDFType::FrontSide) |
                   uint32_t(BSDFType::BackSide);
        }

        const BSDF *GetBSDF(const DifferentialGeometryP &) const override { return this; }

        void SampleF(const SpectrumWavelengthsP &, const Vector3fP &wo,
                     const DifferentialGeometryP &dg, const FloatP &, const FloatP &,
                     const FloatP &, BSDFSampleP *s, TransportMode,
                     MaskP active) const override
        {
            // Straight through.
            enoki::masked(s->wi, active) = -wo;
            enoki::masked(s->f, active) = FloatP(1.f);
            enoki::masked(s->eta, active) = FloatP(1.f);
            enoki::masked(s->sampledType, active) =
                UInt32P(uint32_t(BSDFType::Null) | uint32_t(BSDFType::SpecularTransmission));
            enoki::masked(s->specular, active) = MaskP(true);

            // Lux SingleBSDF kills the lane when |Dot(wo, ng)| is grazing
            // so a near-tangent hit terminates.
            const FloatP cosWo = dot(wo, dg.ng);
            enoki::masked(s->pdf, active) =
                select(abs(cosWo) < EPS_DENOM, FloatP(0.f), FloatP(1.f));
        }

        FloatP Pdf(const SpectrumWavelengthsP &, const Vector3fP &wo, const Vector3fP &wi,
                   const DifferentialGeometryP &, uint32_t, TransportMode,
                   MaskP) const override
        {
            // Lux NullTransmission::Pdf is 1 for the straight through pair.
            return select(dot(wo, wi) <= FloatP(-1.f) + EPS_DENOM, FloatP(1.f),
                          FloatP(0.f));
        }

        void Eval(const SpectrumWavelengthsP &, const Vector3fP &wo, const Vector3fP &wi,
                  const DifferentialGeometryP &, TransportMode, BSDFEvalP *out,
                  MaskP active) const override
        {
            // Lux NullTransmission::F is 1 only for the straight through pair
            // but zero in NEE.
            const MaskP straight = dot(wo, wi) <= FloatP(-1.f) + EPS_DENOM;
            enoki::masked(out->f, active) =
                select(straight, SWCSpectrumP(1.f), SWCSpectrumP(0.f));
            enoki::masked(out->pdf, active) =
                select(straight, FloatP(1.f), FloatP(0.f));
            enoki::masked(out->pdfRev, active) =
                select(straight, FloatP(1.f), FloatP(0.f));
        }

        static std::shared_ptr<Material> CreateMaterial(const PluginContext &ctx);
    };

} // namespace lux2

#endif // LUX2_MATERIAL_NULL_H
