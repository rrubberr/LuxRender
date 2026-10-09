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
#include "core/colorsystem.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lux2
{

    struct PluginContext;
    class ToneMap;

    // Fleximage accumulates XYZ with filter weights.
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

        // Zero the accumulation buffers and sample count.
        void Clear() override;

        // Crop window in pixels.
        int XStart() const override { return m_xStart; }
        int YStart() const override { return m_yStart; }
        int XCount() const override { return m_xCount; }
        int YCount() const override { return m_yCount; }

        void Splat(const FloatP &x, const FloatP &y, const SWCSpectrumP &L,
                   const SpectrumWavelengthsP &sw, const FloatP &alpha,
                   const FloatP &weight, int bufferId) override;

        void Merge(Film *other) override;

        // Scheduler interface
        int SetupTiles(int tileSize, int haloX, int haloY) override;
        int TileCount() const override { return int(m_tiles.size()); }
        FilmTile GetTile(uint32_t index) const override;
        std::unique_ptr<Film> MakeWorkerScratch() const override;
        void BindScratch(uint32_t tile, Film &scratch) const override;
        void MergeScratch(uint32_t tile, const Film &scratch) override;
        void AddTileSampleCount(uint32_t tile, double n) override;
        // Global SPP = min across tiles once a partition exists, else a
        // flat counter.
        double SampleCount() const override;

        void AddSampleCount(double n) override { m_sampleCount += n; }

        bool WriteImage(ImageType type) override;

        // Live parameter access.
        void SetParameterValue(luxComponentParameters param, double value,
                               unsigned int index) override;
        double GetParameterValue(luxComponentParameters param,
                                 unsigned int index) const override;
        double GetDefaultParameterValue(luxComponentParameters param,
                                        unsigned int index) const override;
        void SetStringParameterValue(luxComponentParameters param,
                                     const std::string &value,
                                     unsigned int index) override;
        std::string GetStringParameterValue(
            luxComponentParameters param, unsigned int index) const override;
        std::string GetDefaultStringParameterValue(
            luxComponentParameters param, unsigned int index) const override;

        // Normalized pixel access.
        void GetPixelNormalized(int x, int y, float xyz[3],
                                float *alpha) const;

        // Display framebuffer.
        void UpdateFrameBuffer() override;
        unsigned char *GetFrameBuffer() override;
        float *GetFloatFrameBuffer() override;
        float *GetAlphaBuffer() override;

        // Accumulation buffers of the active group, crop window indexed.
        // For single light group this is the whole frame.
        const std::vector<float> &BufX() const { return Active().bX; }
        const std::vector<float> &BufY() const { return Active().bY; }
        const std::vector<float> &BufZ() const { return Active().bZ; }
        const std::vector<float> &BufAlpha() const { return Active().bAlpha; }
        const std::vector<float> &BufWeight() const { return Active().bW; }

        // Lightgroup accumulation.
        void SetLightGroupCount(int n) override;
        int GroupCount() const { return int(m_groups.size()); }

        // Active lightgroup.
        void SetActiveGroup(int g) override
        {
            m_activeGroup = (g >= 0 && g < int(m_groups.size())) ? g : 0;
        }
        int ActiveGroup() const { return m_activeGroup; }

        // Composite modifiers.
        void SetGroupEnable(int g, bool on)
        {
            if (g >= 0 && g < int(m_gmod.size()))
                m_gmod[g].enable = on;
        }
        void SetGroupGlobalScale(int g, float s)
        {
            if (g >= 0 && g < int(m_gmod.size()))
                m_gmod[g].globalScale = s;
        }
        void SetGroupTemperature(int g, float t)
        {
            if (g >= 0 && g < int(m_gmod.size()))
                m_gmod[g].temperature = t;
        }
        void SetGroupRGBScale(int g, const RGBColor &s)
        {
            if (g >= 0 && g < int(m_gmod.size()))
                m_gmod[g].rgbScale = s;
        }

        // Group modifiers.
        bool GetGroupEnable(int g) const
        {
            return (g >= 0 && g < int(m_gmod.size())) ? m_gmod[g].enable
                                                      : true;
        }
        float GetGroupScale(int g) const
        {
            return (g >= 0 && g < int(m_gmod.size())) ? m_gmod[g].globalScale
                                                      : 1.f;
        }
        RGBColor GetGroupRGBScale(int g) const
        {
            return (g >= 0 && g < int(m_gmod.size())) ? m_gmod[g].rgbScale
                                                      : RGBColor(1.f);
        }
        float GetGroupTemperature(int g) const
        {
            return (g >= 0 && g < int(m_gmod.size())) ? m_gmod[g].temperature
                                                      : 0.f;
        }
        std::string GetGroupName(int g) const
        {
            return (g >= 0 && g < int(m_groupNames.size()))
                       ? m_groupNames[g]
                       : std::string();
        }
        void SetGroupName(int g, const std::string &name)
        {
            if (g >= int(m_groupNames.size()))
                m_groupNames.resize(size_t(g) + 1);
            if (g >= 0)
                m_groupNames[size_t(g)] = name;
        }

        // True when group g is enabled with unit scale/temperature/rgbScale.
        bool GroupConvertIsIdentity(int g) const override
        {
            if (g < 0 || g >= int(m_gmod.size()))
                return true;
            const GroupModifier &m = m_gmod[g];
            return m.enable && m.globalScale == 1.f && m.temperature == 0.f &&
                   m.rgbScale[0] == 1.f && m.rgbScale[1] == 1.f &&
                   m.rgbScale[2] == 1.f;
        }

        // Group names.
        void SetLightGroupNames(const std::vector<std::string> &names) override
        {
            m_groupNames = names;
        }
        void SetBakeGroupState(bool on) override { m_bakeGroupState = on; }

        // Output configuration.
        const std::string &FileName() const { return m_filename; }
        void SetFileName(const std::string &f) { m_filename = f; }
        bool WriteEXR() const { return m_writeEXR; }
        bool WriteEXRHalf() const { return m_writeEXRHalf; }
        bool WriteEXRApplyImaging() const { return m_writeEXRApplyImaging; }
        bool WritePNG() const { return m_writePNG; }
        bool WritePNG16() const { return m_writePNG16; }
        bool PremultiplyAlpha() const { return m_premultiplyAlpha; }
        int WriteInterval() const override { return m_writeInterval; }
        int FlmWriteInterval() const { return m_flmWriteInterval; }
        int DisplayInterval() const override { return m_displayInterval; }
        int HaltSpp() const override { return m_haltspp; }
        int HaltTime() const override { return m_halttime; }

        // Tonemap/colorspace state.
        int TonemapKernelValue() const { return m_tonemapKernel; }
        float Gamma() const { return m_gamma; }
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
        FlexImageFilm(int xres, int yres, const Filter *filter,
                      int xStart, int xCount, int yStart, int yCount,
                      bool premultiplyAlpha);

        // A private accumulation buffer.
        struct ScratchTag
        {
        };
        FlexImageFilm(int xres, int yres, const Filter *filter,
                      int xStart, int xCount, int yStart, int yCount,
                      bool premultiplyAlpha, ScratchTag);

        // Allocate display buffers.
        void CreateFrameBuffer();

        // ToneMap built from current parameters.
        std::unique_ptr<ToneMap> BuildToneMap() const;

        // Add src into this over [x0,x1)x[y0,y1).
        void AddRegion(const FlexImageFilm &src, int x0, int y0, int x1, int y1);

        // One accumulation set per lightgroup.
        struct BufferSet
        {
            std::vector<float> bX, bY, bZ, bAlpha, bW;
        };

        // Composite modifiers.
        struct GroupModifier
        {
            float globalScale = 1.f;
            float temperature = 0.f; // 0 disables blackbody adaptation
            RGBColor rgbScale = RGBColor(1.f);
            bool enable = true;
        };

        BufferSet &Active() { return m_groups[m_activeGroup]; }
        const BufferSet &Active() const { return m_groups[m_activeGroup]; }

        // Size m_groups/m_gmod to groupCount (>=1) and zero every set to the
        // current crop area.
        void AllocateSets(int groupCount);

        // Bradford convert for a group:
        // Adapt(white -> ToXYZ(rgbScale)) * (temp>0 ? Adapt(white -> bb/Y) : I),
        // then *= globalScale.
        ColorAdaptator ComputeConvert(const ColorSystem &cs,
                                      const XYZColor &white,
                                      const GroupModifier &m) const;

        // Color setup shared.
        struct SnapshotColor
        {
            ColorSystem cs;
            XYZColor white;
            // Group convert.
            std::vector<ColorAdaptator> converts;
        };
        SnapshotColor MakeSnapshotColor() const;

        // Composite one pixel.
        XYZColor CompositePixel(const std::vector<BufferSet> &sets,
                                const SnapshotColor &sc, size_t idx,
                                float &alpha) const;

        // Copy the buffers of every set.
        void SnapshotAccum(std::vector<BufferSet> &out) const;

        // Normalize, tonemap, and convert to display RGB.
        bool BuildDisplayImage(std::vector<RGBColor> &rgb,
                               std::vector<float> &alpha, bool applyTonemap,
                               const std::vector<BufferSet> &sets) const;

        // Multichannel linear EXR with beauty.
        bool WriteGroupEXR(const std::vector<BufferSet> &sets) const;

        // TODO: add convergence test.
        void ResetConvTest()
        {
            // TODO: reset the convergence test once implemented.
        }

        int m_xres, m_yres;
        int m_xStart, m_xCount, m_yStart, m_yCount;

        // Crop window sized accumulation buffer per lightgroup.
        std::vector<BufferSet> m_groups;
        std::vector<GroupModifier> m_gmod;
        // Written by GroupRenderPass between joined passes.
        int m_activeGroup = 0;
        // Group names for multichannel EXR AOV prefixes.
        std::vector<std::string> m_groupNames;
        // Bake GUI convert into exported AOVs.
        bool m_bakeGroupState = false;

        double m_sampleCount = 0.0;

        // Mutex Merge() into a shared master film.
        mutable std::mutex m_mergeMutex;

        // Tile mutex over tile's pixels in shared buffers.
        std::vector<FilmTile> m_tiles;
        int m_tileCols = 0;
        int m_tileRows = 0;
        std::vector<std::unique_ptr<std::mutex>> m_tileMutexes;
        // Tile sample count.
        std::vector<std::atomic<double>> m_tileSampleCount;

        // Merge directory where each tile's FilmTile.ovBegin/ovCount
        // indexes a slice of the list.
        std::vector<TileOverlap> m_overlaps;
        size_t m_maxScratchArea = 0;
        // Increment on each SetupTiles so an old directory
        // is caught by the MergeScratch assert.
        uint64_t m_epoch = 0;

        // Scratch binding identity holds the tile and directory
        // epoch the scratch was last bound to.
        mutable uint32_t m_boundTile = 0;
        mutable uint64_t m_boundEpoch = 0;

        // Serialize WriteImage() for display timer and render thread.
        mutable std::mutex m_writeMutex;

        // Display buffers.
        std::vector<unsigned char> m_frameBuffer;
        std::vector<float> m_floatFrameBuffer;
        std::vector<float> m_alphaBuffer;

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

        // Tonemap/colorspace state and defaults.
        int m_tonemapKernel = TMK_AUTOLINEAR;
        int m_dTonemapKernel = TMK_AUTOLINEAR;
        float m_reinhardPreScale = 1.f, m_dReinhardPreScale = 1.f;
        float m_reinhardPostScale = 1.f, m_dReinhardPostScale = 1.f;
        float m_reinhardBurn = 6.f, m_dReinhardBurn = 6.f;
        float m_linearSensitivity = 50.f, m_dLinearSensitivity = 50.f;
        float m_linearExposure = 1.f, m_dLinearExposure = 1.f;
        float m_linearFStop = 2.8f, m_dLinearFStop = 2.8f;
        float m_linearGamma = 1.f, m_dLinearGamma = 1.f;
        // Display gamma for the viewport and PNG.
        float m_gamma = 2.2f, m_dGamma = 2.2f;
        mutable bool m_tonemapWarned = false;
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
