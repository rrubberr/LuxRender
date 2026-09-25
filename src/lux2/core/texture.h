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

#ifndef LUX2_TEXTURE_H
#define LUX2_TEXTURE_H

#include "core/vecp.h"
#include "core/spectrum.h"
#include "core/color.h"
#include "core/bsdf.h"

namespace lux2
{

    // ---------------------------------------------------------------------------
    // FresnelModel / FresnelGeneralP
    // ---------------------------------------------------------------------------

    // Fresnel evaluation model.
    enum class FresnelModel
    {
        Auto,
        Dielectric,
        Conductor,
        Full
    };

    // Complex IOR Fresnel data.
    struct FresnelGeneralP
    {
        FloatP eta; // real IOR
        FloatP k;   // imaginary IOR
        FresnelModel model;
    };

    // ---------------------------------------------------------------------------
    // Abstract texture interface
    // ---------------------------------------------------------------------------

    // Evaluated at a shading point.
    template <typename T>
    class Texture
    {
    public:
        virtual ~Texture() = default;

        // Evaluate at a packet of shading points.
        virtual T Evaluate(const DifferentialGeometryP &dg,
                           const SpectrumWavelengthsP &sw,
                           MaskP active = MaskP(true)) const = 0;

        // Constant textures let the integrator skip eval.
        virtual bool IsConstant() const { return false; }
    };

    // ---------------------------------------------------------------------------
    // Texture type aliases
    // ---------------------------------------------------------------------------

    // Float texture.
    using FloatTexture = Texture<FloatP>;

   // Spectral color texture.
    using ColorTexture = Texture<FloatP>;

    // Fresnel texture.
    using FresnelTexture = Texture<FresnelGeneralP>;

    // ---------------------------------------------------------------------------
    // Constant texture
    // ---------------------------------------------------------------------------

    // A texture returning a single value for all points.
    template <typename T>
    class ConstantTexture : public Texture<T>
    {
    public:
        // Construct with a single value broadcast.
        explicit ConstantTexture(const T &value) : m_value(value) {}

        T Evaluate(const DifferentialGeometryP &, const SpectrumWavelengthsP &,
                   MaskP = MaskP(true)) const override
        {
            return m_value;
        }

        bool IsConstant() const override { return true; }

    private:
        T m_value;
    };

    // ---------------------------------------------------------------------------
    // Constant color texture
    // ---------------------------------------------------------------------------

    // A constant RGB color reconstructed to a spectrum at each shading point.
    class ConstantColorTexture : public Texture<SWCSpectrumP>
    {
    public:
        explicit ConstantColorTexture(const RGBColorP &color,
                                      bool illuminant = false)
            : m_color(color), m_illuminant(illuminant) {}

        SWCSpectrumP Evaluate(const DifferentialGeometryP &,
                              const SpectrumWavelengthsP &sw,
                              MaskP = MaskP(true)) const override
        {
            return RGBToSmitsSPD(m_color, sw, m_illuminant);
        }

        // Switch between the reflectant (E) and illuminant (D65) bases.
        void SetIlluminant() { m_illuminant = true; }

        bool IsConstant() const override { return true; }

    private:
        RGBColorP m_color;
        bool m_illuminant;
    };

} // namespace lux2

#endif // LUX2_TEXTURE_H
