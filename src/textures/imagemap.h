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

#ifndef LUX2_TEXTURE_IMAGEMAP_H
#define LUX2_TEXTURE_IMAGEMAP_H

#include "core/texture.h"
#include "core/mipmap.h"

#include <memory>

namespace lux2
{

    class PluginContext;

    // Owner of the cached MIPMap and the gain/gamma correction.
    class ImageTextureBase
    {
    public:
        ImageTextureBase(const std::shared_ptr<MIPMap> &mipmap, float gain,
                         float gamma)
            : m_mipmap(mipmap), m_gain(gain), m_gamma(gamma)
        {
        }

        const MIPMap *GetMIPMap() const { return m_mipmap.get(); }

    protected:
        std::shared_ptr<MIPMap> m_mipmap;
        float m_gain;
        float m_gamma;
    };

    // Reconstruct a spectrum from the filtered RGB texel via Smits.
    class ImageColorTexture : public ColorTexture, public ImageTextureBase
    {
    public:
        ImageColorTexture(const std::shared_ptr<MIPMap> &mipmap, float gain,
                          float gamma, bool isIlluminant)
            : ImageTextureBase(mipmap, gain, gamma), m_isIlluminant(isIlluminant)
        {
        }

        SWCSpectrumP Evaluate(const DifferentialGeometryP &dg,
                              const SpectrumWavelengthsP &sw,
                              MaskP active = MaskP(true)) const override;

        // Switch the white reference to the illuminant (D65) basis.
        void SetIlluminant() { m_isIlluminant = true; }

        static std::shared_ptr<ColorTexture> CreateColorTexture(const PluginContext &ctx);

    private:
        bool m_isIlluminant;
    };

    // Select one channel of the filtered texel.
    class ImageFloatTexture : public FloatTexture, public ImageTextureBase
    {
    public:
        ImageFloatTexture(const std::shared_ptr<MIPMap> &mipmap, float gain,
                          float gamma, TexChannel channel)
            : ImageTextureBase(mipmap, gain, gamma), m_channel(channel)
        {
        }

        FloatP Evaluate(const DifferentialGeometryP &dg,
                        const SpectrumWavelengthsP &sw,
                        MaskP active = MaskP(true)) const override;

        TexChannel GetChannel() const { return m_channel; }

        static std::shared_ptr<FloatTexture> CreateFloatTexture(const PluginContext &ctx);

    private:
        TexChannel m_channel;
    };

    // Tangent space normal map.
    class NormalMapTexture : public FloatTexture, public ImageTextureBase
    {
    public:
        NormalMapTexture(const std::shared_ptr<MIPMap> &mipmap, float gain,
                         float gamma)
            : ImageTextureBase(mipmap, gain, gamma)
        {
        }

        FloatP Evaluate(const DifferentialGeometryP &, const SpectrumWavelengthsP &,
                        MaskP = MaskP(true)) const override
        {
            return FloatP(0.f);
        }

        // Recover dh/du, dh/dv from the tangent space normal.
        void GetDuv(const DifferentialGeometryP &dg, FloatP *du, FloatP *dv) const;

        static std::shared_ptr<FloatTexture> CreateFloatTexture(const PluginContext &ctx);
    };

} // namespace lux2

#endif // LUX2_TEXTURE_IMAGEMAP_H
