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

#ifndef LUX2_FILM_H
#define LUX2_FILM_H

#include "core/vecp.h"
#include "core/spectrum.h"
#include "core/color.h"
#include "core/api.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace lux2
{

    // The intersection of a tile's scratch window with a destination tile's owned domain.
    // Every domain lies inside both the scratch window and the destination by construction.
    struct TileOverlap
    {
        uint32_t tile;      // destination tile (may be the source tile itself)
        int x0, y0, x1, y1; // crop-window-absolute scratch destination
    };

    // A rectangular pixel region used to partition the film into lock and
    // ownership domains. The owned domain is the tile's pixels and the scratch
    // window adds a filter halo. ovBegin/ovCount index the overlap list.
    struct FilmTile
    {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;     // owned pixels (crop absolute)
        int sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0; // scratch window of owned pixels + halo
        uint32_t ovBegin = 0, ovCount = 0;      // slice of the overlap list
    };

    // Which outputs to write.
    enum ImageType
    {
        IMAGE_NONE = 0,
        IMAGE_FILEOUTPUT = 1 << 0,  // write image file(s)
        IMAGE_FLMOUTPUT = 1 << 1,   // write resume FLM file
        IMAGE_FRAMEBUFFER = 1 << 2, // refresh the display framebuffer
        IMAGE_FINAL = 1 << 3,       // final output before ending
        IMAGE_FILE_ALL = IMAGE_FILEOUTPUT | IMAGE_FLMOUTPUT
    };

    // Accumulates radiance contributions into pixels and writes the image.
    class Film
    {
    public:
        virtual ~Film() = default;

        // Film resolution in pixels.
        virtual int XRes() const = 0;
        virtual int YRes() const = 0;

        // Crop window in pixels (defaults to full frame).
        virtual int XStart() const { return 0; }
        virtual int YStart() const { return 0; }
        virtual int XCount() const { return XRes(); }
        virtual int YCount() const { return YRes(); }

        // Halt parameters in SPP and seconds. 0 disables.
        virtual int HaltSpp() const { return 0; }
        virtual int HaltTime() const { return 0; }

        // DisplayTimer cadence in seconds. 0 disables.
        virtual int DisplayInterval() const { return 0; }
        virtual int WriteInterval() const { return 0; }

        // Splat contributions.
        virtual void Splat(const FloatP &x, const FloatP &y,
                           const SWCSpectrumP &L, const SpectrumWavelengthsP &sw,
                           const FloatP &alpha, const FloatP &weight,
                           int bufferId) = 0;

        // Merge another film's buffer into this one.
        virtual void Merge(Film *other) = 0;

        // The film is partitioned into tiled lock/ownership domains. A tile index
        // determines the scratch window, the owned domain, and the merge targets.
        // Workers allocate one scratch at max size and retarget it via BindScratch.
        // For Enoki I need a PACKET_WIDTH aligned tile grid.
        virtual int SetupTiles(int tileSize, int haloX, int haloY)
        {
            (void)tileSize;
            (void)haloX;
            (void)haloY;
            return 0;
        }
        virtual int TileCount() const { return 0; }
        virtual FilmTile GetTile(uint32_t index) const
        {
            (void)index;
            return FilmTile{};
        }

        // A private accumulation buffer sized for the largest scratch window in
        // the current directory.
        virtual std::unique_ptr<Film> MakeWorkerScratch() const
        {
            return nullptr;
        }

        // Point a worker scratch at a tile scratch window and zero it.
        virtual void BindScratch(uint32_t tile, Film &scratch) const {}

        // Merge a worker's scratch into the shared frame with each precomputed
        // overlap region under that destination tile's lock.
        virtual void MergeScratch(uint32_t tile, const Film &scratch)
        {
            (void)tile;
            (void)scratch;
        }

        // Global SampleCount() is the min across tiles.
        virtual void AddTileSampleCount(uint32_t tile, double n)
        {
            (void)tile;
            (void)n;
        }

        // Zero the accumulation buffers.
        virtual void Clear() {}

        // Accumulated sample count.
        virtual void AddSampleCount(double n) = 0;
        virtual double SampleCount() const = 0;

        // Write the requested image format.
        virtual bool WriteImage(ImageType type) = 0;

        // Resume film.
        virtual bool WriteFilmToFile(const std::string &filename)
        {
            (void)filename;
            return false;
        }
        virtual bool LoadResumeFilm(const std::string &filename)
        {
            (void)filename;
            return false;
        }
        virtual double MergeFilmFromStream(std::istream &stream)
        {
            (void)stream;
            return 0.0;
        }

        // Display framebuffer access for the C-API.
        virtual void UpdateFrameBuffer() {}
        virtual unsigned char *GetFrameBuffer() { return nullptr; }
        virtual float *GetFloatFrameBuffer() { return nullptr; }
        virtual float *GetAlphaBuffer() { return nullptr; }

        // Parameter access.
        virtual void SetParameterValue(luxComponentParameters param,
                                       double value, unsigned int index)
        {
            (void)param;
            (void)value;
            (void)index;
        }
        virtual double GetParameterValue(luxComponentParameters param,
                                         unsigned int index) const
        {
            (void)param;
            (void)index;
            return 0.0;
        }
        virtual double GetDefaultParameterValue(luxComponentParameters param,
                                                unsigned int index) const
        {
            (void)param;
            (void)index;
            return 0.0;
        }
    };

} // namespace lux2

#endif // LUX2_FILM_H
