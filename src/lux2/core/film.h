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

#include <iosfwd>
#include <memory>
#include <string>

namespace lux2
{

    // Which outputs to produce on a WriteImage call.
    enum ImageType
    {
        IMAGE_NONE = 0,
        IMAGE_FILEOUTPUT = 1 << 0,   // Write image file(s) (PNG/EXR)
        IMAGE_FLMOUTPUT = 1 << 1,    // Write resume FLM file
        IMAGE_FRAMEBUFFER = 1 << 2,  // Refresh the display framebuffer
        IMAGE_FINAL = 1 << 3,        // Final output before ending
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

        // Splat a packet of filter-weighted contributions.
        // Each splat carries its wavelength set `sw`.
        virtual void Splat(const FloatP &x, const FloatP &y,
                           const SWCSpectrumP &L, const SpectrumWavelengthsP &sw,
                           const FloatP &alpha, const FloatP &weight,
                           int bufferId) = 0;

        // Merge another film's accumulated buffer into this one.
        virtual void Merge(Film *other) = 0;

        // Merge the `other` rectangle into this film, zero, and transfer its
        // pending samples.
        virtual void MergeRegion(Film *other, int x0, int y0, int x1, int y1) = 0;

        // Create a private accumulation block clamped to this film's crop window.
        virtual std::unique_ptr<Film> MakePrivateBlock(int x0, int y0,
                                                       int x1, int y1) const
        {
            (void)x0; (void)y0; (void)x1; (void)y1;
            return nullptr;
        }

        // Zero the accumulation buffers.
        virtual void Clear() {}

        // Accumulated sample count (drives haltspp and FLM bookkeeping).
        virtual void AddSampleCount(double n) = 0;
        virtual double SampleCount() const = 0;

        // Produce the requested outputs. Returns true on success.
        virtual bool WriteImage(ImageType type) = 0;

        // Resume-file support. Films without FLM support keep the defaults.
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

        // Display framebuffer access for the C-API. nullptr when unsupported.
        virtual void UpdateFrameBuffer() {}
        virtual unsigned char *GetFrameBuffer() { return nullptr; }
        virtual float *GetFloatFrameBuffer() { return nullptr; }
        virtual float *GetAlphaBuffer() { return nullptr; }

        // Live parameter access (legacy luxComponentParameters subset).
        // Unimplemented ids are ignored on set and read as 0.
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
