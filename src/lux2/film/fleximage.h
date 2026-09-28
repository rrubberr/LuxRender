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

#ifndef LUX2_FILM_FLEXIMAGE_H
#define LUX2_FILM_FLEXIMAGE_H

#include "core/film.h"
#include "core/filter.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lux2
{

    struct PluginContext;

    // Fleximage accumulates per-pixel XYZ with filter weights.
    class FlexImageFilm : public Film
    {
    public:
        // Tonemap kernel ids.
        enum TonemapKernel
        {
            TMK_REINHARD = 0,
            TMK_LINEAR = 1,
            TMK_CONTRAST = 2,
            TMK_MAXWHITE = 3,
            TMK_AUTOLINEAR = 4,
            TMK_FALSECOLORS = 5
        };

        FlexImageFilm(int xres, int yres, const Filter *filter,
                      const float crop[4], const std::string &filename,
                      bool premultiplyAlpha);

        int XRes() const override { return m_xres; }
        int YRes() const override { return m_yres; }

        // Private region.
        std::unique_ptr<Film> MakePrivateBlock(int x0, int y0,
                                               int x1, int y1) const override;

        // Zero the accumulation buffers and sample count.
        void Clear() override;

        // Crop window in pixels.
        int XStart() const { return m_xStart; }
        int YStart() const { return m_yStart; }
        int XCount() const { return m_xCount; }
        int YCount() const { return m_yCount; }

        void Splat(const FloatP &x, const FloatP &y, const SWCSpectrumP &L,
                   const SpectrumWavelengthsP &sw, const FloatP &alpha,
                   const FloatP &weight, int bufferId) override;

        void Merge(Film *other) override;
        void MergeRegion(Film *other, int x0, int y0, int x1, int y1) override;

        void AddSampleCount(double n) override { m_sampleCount += n; }
        double SampleCount() const override { return m_sampleCount; }

        bool WriteImage(ImageType type) override;

        // Live parameter access.
        void SetParameterValue(luxComponentParameters param, double value,
                               unsigned int index) override;
        double GetParameterValue(luxComponentParameters param,
                                 unsigned int index) const override;
        double GetDefaultParameterValue(luxComponentParameters param,
                                        unsigned int index) const override;

        // Normalized pixel access.
        void GetPixelNormalized(int x, int y, float xyz[3],
                                float *alpha) const;

        // Raw accumulation buffers, crop-window indexed.
        const std::vector<float> &BufX() const { return m_bX; }
        const std::vector<float> &BufY() const { return m_bY; }
        const std::vector<float> &BufZ() const { return m_bZ; }
        const std::vector<float> &BufAlpha() const { return m_bAlpha; }
        const std::vector<float> &BufWeight() const { return m_bW; }

        // Output configuration.
        const std::string &FileName() const { return m_filename; }
        void SetFileName(const std::string &f) { m_filename = f; }
        bool WriteEXR() const { return m_writeEXR; }
        bool WriteEXRHalf() const { return m_writeEXRHalf; }
        bool WriteEXRApplyImaging() const { return m_writeEXRApplyImaging; }
        bool WritePNG() const { return m_writePNG; }
        bool WritePNG16() const { return m_writePNG16; }
        bool PremultiplyAlpha() const { return m_premultiplyAlpha; }
        int WriteInterval() const { return m_writeInterval; }
        int FlmWriteInterval() const { return m_flmWriteInterval; }
        int DisplayInterval() const { return m_displayInterval; }
        int HaltSpp() const { return m_haltspp; }
        int HaltTime() const { return m_halttime; }

        // Tonemap/colorspace state.
        int TonemapKernelValue() const { return m_tonemapKernel; }
        float LinearSensitivity() const { return m_linearSensitivity; }
        float LinearExposure() const { return m_linearExposure; }
        float LinearFStop() const { return m_linearFStop; }
        float LinearGamma() const { return m_linearGamma; }
        float ReinhardPreScale() const { return m_reinhardPreScale; }
        float ReinhardPostScale() const { return m_reinhardPostScale; }
        float ReinhardBurn() const { return m_reinhardBurn; }
        const float *ColorspaceRed() const { return m_csRed; }
        const float *ColorspaceGreen() const { return m_csGreen; }
        const float *ColorspaceBlue() const { return m_csBlue; }
        const float *ColorspaceWhite() const { return m_csWhite; }

        static std::shared_ptr<Film> CreateFilm(const PluginContext &ctx);

    private:
        // Private-block constructor: explicit geometry + filter, zeroed
        // buffers, default output config, no filename. Used by
        // MakePrivateBlock().
        FlexImageFilm(int xres, int yres, const Filter *filter,
                      int xStart, int xCount, int yStart, int yCount,
                      bool premultiplyAlpha);

        int m_xres, m_yres;
        int m_xStart, m_xCount, m_yStart, m_yCount;

        // Crop window sized accumulation buffers.
        std::vector<float> m_bX, m_bY, m_bZ, m_bAlpha, m_bW;

        double m_sampleCount = 0.0;

        // Guards Merge() into a shared master film (one lock per tile merge).
        mutable std::mutex m_mergeMutex;

        const Filter *m_filter; // owned by the Scene
        bool m_premultiplyAlpha;
        std::string m_filename;

        // Output configuration.
        bool m_writeEXR = false;
        bool m_writeEXRHalf = true;
        bool m_writeEXRApplyImaging = true;
        bool m_writePNG = true;
        bool m_writePNG16 = false;
        int m_writeInterval = 60;
        int m_flmWriteInterval = 60;
        int m_displayInterval = 12;
        int m_haltspp = -1;
        int m_halttime = -1;

        // Live tonemap/colorspace state and defaults.
        int m_tonemapKernel = TMK_AUTOLINEAR;
        int m_dTonemapKernel = TMK_AUTOLINEAR;
        float m_reinhardPreScale = 1.f, m_dReinhardPreScale = 1.f;
        float m_reinhardPostScale = 1.f, m_dReinhardPostScale = 1.f;
        float m_reinhardBurn = 6.f, m_dReinhardBurn = 6.f;
        float m_linearSensitivity = 50.f, m_dLinearSensitivity = 50.f;
        float m_linearExposure = 1.f, m_dLinearExposure = 1.f;
        float m_linearFStop = 2.8f, m_dLinearFStop = 2.8f;
        float m_linearGamma = 1.f, m_dLinearGamma = 1.f;
        // SMPTE primaries + white point.
        float m_csRed[2] = {0.63f, 0.34f};
        float m_csGreen[2] = {0.31f, 0.595f};
        float m_csBlue[2] = {0.155f, 0.07f};
        float m_csWhite[2] = {0.314275f, 0.329411f};
        float m_dCsRed[2] = {0.63f, 0.34f};
        float m_dCsGreen[2] = {0.31f, 0.595f};
        float m_dCsBlue[2] = {0.155f, 0.07f};
        float m_dCsWhite[2] = {0.314275f, 0.329411f};
    };

} // namespace lux2

#endif // LUX2_FILM_FLEXIMAGE_H
