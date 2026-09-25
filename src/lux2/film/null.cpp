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

#include "film/null.h"

#include "core/color.h"
#include "core/dynload.h"
#include "core/paramset.h"
#include "core/register.h"

namespace lux2
{

    void NullFilm::Splat(const FloatP &, const FloatP &,
                         const SWCSpectrumP &L, const SpectrumWavelengthsP &sw,
                         const FloatP &alpha, const FloatP &weight,
                         int)
    {
        // Luminance of each lane at its own wavelength weighted by the
        // filter contribution.
        const FloatP lum = SWCY(L, sw) * weight;
        const MaskP active = alpha > FloatP(0.f);

        // Reduce the active lanes into the running scalar accumulators.
        float lumArr[PACKET_WIDTH];
        bool actArr[PACKET_WIDTH];
        enoki::store_unaligned(lumArr, lum);
        enoki::store_unaligned(actArr, active);

        for (size_t i = 0; i < PACKET_WIDTH; ++i) {
            if (actArr[i] && enoki::isfinite(lumArr[i])) {
                m_sumLuminance += double(lumArr[i]);
                m_count += 1.0;
            }
        }
    }

    void NullFilm::Merge(Film *other)
    {
        NullFilm *o = dynamic_cast<NullFilm *>(other);
        if (!o)
            return;
        m_sumLuminance += o->m_sumLuminance;
        m_count += o->m_count;
    }

    std::shared_ptr<Film> NullFilm::CreateFilm(const PluginContext &ctx)
    {
        const int xres = ctx.params ? ctx.params->FindOneInt("xresolution", 800) : 800;
        const int yres = ctx.params ? ctx.params->FindOneInt("yresolution", 600) : 600;
        return std::make_shared<NullFilm>(xres, yres);
    }

    LUX2_REGISTER_FILM(NullFilm, "null");

} // namespace lux2
