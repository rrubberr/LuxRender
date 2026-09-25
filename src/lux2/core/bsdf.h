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

    // Direction of radiance transport at a BSDF interaction.
    enum class TransportMode
    {
        Radiance,
        Importance
    };

    // ---------------------------------------------------------------------------
    // BSDFSampleP sample record
    // ---------------------------------------------------------------------------

    // Result of sampling a BSDF lobe for one packet of lanes.
    struct BSDFSampleP
    {
        Vector3fP wo;   // Sampled outgoing direction (world, normalized).
        SWCSpectrumP f; // BSDF value f(wi,wo).

        // This does NOT include the geometric |cos(theta_i)| term;
        // Integrators must multiply by |cos| explicitly!
        // HEED THIS COMMENT to avoid energy bugs.

        FloatP pdf;          // Solid-angle probability density.
        FloatP eta;          // IOR ratio.
        UInt32P sampledType; // BSDFType of the lobe chosen.
        MaskP specular;      // True where sampledType is a delta/specular lobe.
    };

    // ---------------------------------------------------------------------------
    // BSDFEvalP evaluation record
    // ---------------------------------------------------------------------------

    // Result of evaluating a BSDF at a fixed (wi, wo) pair.
    struct BSDFEvalP
    {
        SWCSpectrumP f;      // f(wi,wo), excludes geometric |cos|
        FloatP pdf;          // pdf(wi | wo) the eye-walk forward
        FloatP pdfRev;       // pdf(wo | wi) the reverse pdf
    };

    // ---------------------------------------------------------------------------
    // DifferentialGeometryP shading record
    // ---------------------------------------------------------------------------

    // The integrator fills this from the tracer's HitP. Carries
    // parametric derivatives and shading tangent for texture filtering,
    // anisotropic roughness, and normal/bump mapping.
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
        // pdf==0 is a failed sample.
        virtual void SampleF(const SpectrumWavelengthsP &sw,
                             const Vector3fP &wo,
                             const DifferentialGeometryP &dg,
                             const FloatP &u0, const FloatP &u1, const FloatP &u2,
                             BSDFSampleP *sample,
                             TransportMode mode,
                             MaskP active) const = 0;

        // Solid angle of the wi<->wo pair for lobes selected by typeMask.
        virtual FloatP Pdf(const SpectrumWavelengthsP &sw,
                           const Vector3fP &wi, const Vector3fP &wo,
                           const DifferentialGeometryP &dg,
                           uint32_t typeMask,
                           TransportMode mode,
                           MaskP active) const = 0;

        // Evaluate f(wi,wo) and both directional pdfs at a fixed pair, writing
        // into *out (masked by `active`, like SampleF). Naming is relative to
        // the eye walk: `wi` is the sampled/outgoing direction, `wo` the
        // incoming. `pdf = pdf(wi|wo)` is the forward eye-walk pdf (identical
        // to Pdf()/SampleF.pdf, used as NEE's bsdfPdf); `pdfRev = pdf(wo|wi)`
        // is the reverse. Delta/specular lobes have no finite pdf and write
        // {0,0,0} (NEE never queries them).
        virtual void Eval(const SpectrumWavelengthsP &sw,
                          const Vector3fP &wi, const Vector3fP &wo,
                          const DifferentialGeometryP &dg,
                          TransportMode mode, BSDFEvalP *out,
                          MaskP active) const = 0;

        ENOKI_CALL_SUPPORT_FRIEND()
    };

    // Used with enoki::call for per-material dispatch.
    using BSDFPtr = enoki::replace_scalar_t<FloatP, const BSDF *>;

} // namespace lux2

#endif // LUX2_BSDF_H
