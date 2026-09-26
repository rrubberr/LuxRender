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

#ifndef LUX2_FILM_NULL_H
#define LUX2_FILM_NULL_H

#include "core/film.h"

#include <memory>

namespace lux2
{

    struct PluginContext;

    // A Film that keeps no pixels.
    class NullFilm : public Film
    {
    public:
        NullFilm(int xres, int yres) : m_xres(xres), m_yres(yres) {}

        int XRes() const override { return m_xres; }
        int YRes() const override { return m_yres; }

        // Convert each lane's spectral radiance to luminance at its own
        // wavelength and fold it into the sum.
        void Splat(const FloatP &x, const FloatP &y,
                   const SWCSpectrumP &L, const SpectrumWavelengthsP &sw,
                   const FloatP &alpha, const FloatP &weight,
                   int bufferId) override;

        // Fold another NullFilm's accumulators into this one.
        void Merge(Film *other) override;

        // No image output.
        bool WriteImage(ImageType) override { return true; }

        void AddSampleCount(double n) override { m_sampleCount += n; }
        double SampleCount() const override { return m_sampleCount; }

        // Accumulated luminance statistics.
        double SumLuminance() const { return m_sumLuminance; }
        double MeanLuminance() const
        {
            return m_count > 0.0 ? m_sumLuminance / m_count : 0.0;
        }

        static std::shared_ptr<Film> CreateFilm(const PluginContext &ctx);

    private:
        int m_xres, m_yres;
        double m_sumLuminance = 0.0;
        double m_count = 0.0;
        double m_sampleCount = 0.0;
    };

} // namespace lux2

#endif // LUX2_FILM_NULL_H
