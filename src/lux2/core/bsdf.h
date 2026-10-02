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

#ifndef LUX2_BSDF_H
#define LUX2_BSDF_H

#include "core/vecp.h"
#include "core/geometry.h"
#include "core/spectrum.h"
#include "core/color.h"
#include "core/bsdf_type.h"

#include <cstdint>

namespace lux2
{

    // ---------------------------------------------------------------------------
    // TransportMode
    // ---------------------------------------------------------------------------

    // Direction convention at a BSDF interaction.
    // 1. Radiance == reverse=true (wo toward the eye, eye path and NEE).
    // 2. Importance == reverse=false (wo toward the light, light path).
    enum class TransportMode
    {
        Radiance,
        Importance
    };

    // ---------------------------------------------------------------------------
    // BSDFSampleP sample record
    // ---------------------------------------------------------------------------

    // Result of sampling a BSDF lobe for one packet of lanes.
    // 1. f is the folded multiplier the integrator applies directly ( T *= f ).
    //    It matches lux MultiBSDF::SampleF under the given TransportMode:
    //    Radiance == legacy reverse=true (eye path) folds no ng Jacobian;
    //    Importance == reverse=false additionally multiplies by
    //    |Dot(ng, wi) / Dot(ng, wo)| (!= 1 under bump crossover).
    // 2. pdf is the mixture selection density, not always the solid angle pdf.
    //    For multi component BSDF a  specular bounce reports the component
    //    selection probability w, and f carries the importance sampling gain F̄*R/w.
    //    Used with integrator's FULL_MIS prevPdf, so must be sampling density.
    struct BSDFSampleP
    {
        Vector3fP wi;   // Sampled direction for the next ray (world, normalized).
        SWCSpectrumP f; // Folded multiplier; see (1) above.

        FloatP pdf;          // Mixture selection density; see (2) above.
        FloatP eta;          // IOR ratio.
        UInt32P sampledType; // BSDFType of the lobe chosen.
        MaskP specular;      // True where sampledType is delta/specular.
    };

    // ---------------------------------------------------------------------------
    // BSDFEvalP evaluation record
    // ---------------------------------------------------------------------------

    // Result of evaluating a BSDF at a (wi, wo) pair.
    struct BSDFEvalP
    {
        SWCSpectrumP f; // f(wi,wo) * |cos(0ᵢ)|; NEE divides by light pdf
        FloatP pdf;     // pdf(wi | wo) the eye-walk forward
        FloatP pdfRev;  // pdf(wo | wi) the reverse pdf
    };

    // ---------------------------------------------------------------------------
    // DifferentialGeometryP shading record
    // ---------------------------------------------------------------------------

    // The integrator fills this from the tracer's HitP.
    struct DifferentialGeometryP
    {
        Point3fP p;      // world-space hit position
        Normal3fP n;     // shading normal (cosine terms only)
        Normal3fP ng;    // geometric normal (ray offset / side tests)
        MaskP entering;  // true where the ray crosses into the surface
        FloatP uv_u;     // texture u coordinate
        FloatP uv_v;     // texture v coordinate
        Vector3fP dp_du; // d(position)/d(u) for texture mip selection
        Vector3fP dp_dv; // d(position)/d(v)
        Vector3fP dp_ds; // shading tangent for anisotropy
        Vector3fP dp_dt; // shading bitangent (unit, orthogonal to dp_ds and n)
    };

    // TODO: There is currently no Medium type on the hit or in RayP.

    // ---------------------------------------------------------------------------
    // BSDF abstract interface
    // ---------------------------------------------------------------------------

    // A Material returns one of these from GetBSDF();
    // the integrator calls SampleF/Pdf.
    class BSDF
    {
    public:
        virtual ~BSDF() = default;

        // Union of lobe types this BSDF can produce.
        virtual uint32_t flags() const = 0;

        // Sample an outgoing direction wi given wo (wo = -incidentDir).
        // 1. A lane fails when no component matches or side test is degenerate;
        //    the integrator kills those paths. On success sample->f is the folded
        //    multiplier and sample->pdf is the mixture selection density.
        //    for a specular lobe in a multi-component BSDF this is the
        //    selection probability w.
        // 2. Side rejection follows lux: use ng with an epsilon to avoid grazing NaN,
        //    then select reflection vs transmission from sign(Dot(ng,wi)/Dot(ng,wo)).
        virtual void SampleF(const SpectrumWavelengthsP &sw,
                             const Vector3fP &wo,
                             const DifferentialGeometryP &dg,
                             const FloatP &u0, const FloatP &u1, const FloatP &u2,
                             BSDFSampleP *sample,
                             TransportMode mode,
                             MaskP active) const = 0;

        // Solid angle of wi<->wo for lobes selected by typeMask.
        virtual FloatP Pdf(const SpectrumWavelengthsP &sw,
                           const Vector3fP &wi, const Vector3fP &wo,
                           const DifferentialGeometryP &dg,
                           uint32_t typeMask,
                           TransportMode mode,
                           MaskP active) const = 0;

        // Evaluate f(wi,wo)*|cos(0ᵢ)| and both directional pdfs writing into
        // *out masked by active. Naming is relative to the eye walk:
        // 1. wi is the sampled/outgoing direction.
        // 2. wo the incoming.
        // 3. pdf = pdf(wi|wo) is the forward eye-walk pdf.
        // 4. pdfRev = pdf(wo|wi) is the reverse.
        // 5. Delta/specular have no finite solid angle pdf, so write
        //    pdf = pdfRev = 0 but f MUST be evaluated. lux relies on
        //    PowerHeuristic(lightPdf, 0) == 1 to flow the term. Eval.f under
        //    Radiance uses the lux reverse=true convention (no ng
        //    Jacobian); Importance applies |sideTest|.
        virtual void Eval(const SpectrumWavelengthsP &sw,
                          const Vector3fP &wi, const Vector3fP &wo,
                          const DifferentialGeometryP &dg,
                          TransportMode mode, BSDFEvalP *out,
                          MaskP active) const = 0;

        ENOKI_CALL_SUPPORT_FRIEND()
    };

    // Used with enoki::call for material dispatch.
    using BSDFPtr = enoki::replace_scalar_t<FloatP, const BSDF *>;

} // namespace lux2

#endif // LUX2_BSDF_H
