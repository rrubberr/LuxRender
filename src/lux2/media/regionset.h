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

#ifndef LUX2_MEDIA_REGIONSET_H
#define LUX2_MEDIA_REGIONSET_H

#include "core/tracked.h"
#include "media/homogeneous.h"

#include <memory>
#include <vector>

namespace lux2
{
    // The ambient composite at medium id 0 is the union of all region volumes.
    class RegionSetMedium : public TrackedMedium
    {
    public:
        void AddChild(const std::shared_ptr<HomogeneousMedium> &child);

        bool IsVacuum() const override { return m_children.empty(); }

        uint32_t Flags() const override;

        BBox Bounds() const override { return m_worldUnion; }

        void IntersectAABB(const RayP &ray, FloatP *t0, FloatP *t1,
                           MaskP *hit, MaskP active) const override;

        // Summed coefficients over children whose box contains p.
        SWCSpectrumP SigmaA(const SpectrumWavelengthsP &sw, const Point3fP &p,
                            MaskP active) const override;
        SWCSpectrumP SigmaS(const SpectrumWavelengthsP &sw, const Point3fP &p,
                            MaskP active) const override;
        SWCSpectrumP Lve(const SpectrumWavelengthsP &sw, const Point3fP &p,
                         MaskP active) const override;

        void Coeffs(const SpectrumWavelengthsP &sw, const Point3fP &p,
                    MaskP active, MediumCoeffs *out) const override;

        // sigma_s-weighted child draw. Weights identical to PhasePdf.
        FloatP PhaseK(const SpectrumWavelengthsP &sw, const Point3fP &p,
                      const FloatP &u, MaskP active) const override;

        // Marginal mixture density q = sum (sigma_s,i / sum sigma_s) * p_i.
        FloatP PhasePdf(const SpectrumWavelengthsP &sw, const Point3fP &p,
                        const Vector3fP &wo, const Vector3fP &wi,
                        MaskP active) const override;

        // Union slab interval over children where sigBar = sum of per-child
        // sw-independent sigma_t bounds over children with a non-empty
        // interval.
        void MajorantBegin(const RayP &ray, const FloatP &t0, const FloatP &t1,
                           MajorantState &ms, MaskP active) const override;

        // The union interval is one segment.
        MaskP MajorantNext(MajorantState &ms, MaskP active) const override;

    private:
        struct Child
        {
            std::shared_ptr<HomogeneousMedium> med;
            float sigmaTMaxRGB; // sw independent sigma_t upper bound
            float slabEps;      // local-space slack so the slab test never
                                // rejects a point Contains() accepts
            float k;            // constant Schlick k
        };

        // One pass accumulating the requested coefficients at p.
        void AccumulateChildren(const SpectrumWavelengthsP &sw,
                                const Point3fP &p, MaskP active,
                                SWCSpectrumP *sa, SWCSpectrumP *ss,
                                SWCSpectrumP *Le) const;

        // Per-child sigma_s weight at p and their sum.
        void ChildSigmaSWeights(const SpectrumWavelengthsP &sw,
                                const Point3fP &p, MaskP active,
                                std::vector<FloatP> *w, FloatP *sumW) const;

        std::vector<Child> m_children;
        BBox m_worldUnion;
        bool m_emissive = false;
    };

} // namespace lux2

#endif // LUX2_MEDIA_REGIONSET_H
