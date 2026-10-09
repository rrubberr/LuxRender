/***************************************************************************
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   To the extent possible under law, the author(s) have dedicated all    *
 *   copyright and related neighboring rights to the belowe code to the    *
 *   public domain worldwide. The below code is distributed without any    *
 *   warranty.                                                             *
 *                                                                         *
 *   See: <https://creativecommons.org/publicdomain/zero/1.0/>             *
 *                                                                         *
 ***************************************************************************/
// Tests are machine generated. Proceed with caution!

// Verifies the EXR and PNG writers/readers: round-trips, crop layout,
// channel modes, and error paths.

#include "core/colorsystem.h"
#include "core/exrio.h"
#include "core/jpegio.h"
#include "core/pngio.h"
#include "core/tiffio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace lux2;

namespace
{

    int g_failures = 0;

    void Check(bool cond, const char *what)
    {
        if (!cond)
        {
            std::cerr << "  FAIL: " << what << std::endl;
            ++g_failures;
        }
        else
        {
            std::cout << "  ok:   " << what << std::endl;
        }
    }

    bool RelClose(float a, float b, float tol)
    {
        return std::fabs(a - b) <= tol * (1.f + std::fabs(a) + std::fabs(b));
    }

    // ---------------------------------------------------------------------------
    // PNG
    // ---------------------------------------------------------------------------

    void CheckPngRoundTrip8()
    {
        const int w = 7, h = 5;
        std::vector<RGBColor> pixels(w * h);
        std::vector<float> alpha(w * h, 1.f);
        for (int i = 0; i < w * h; ++i)
        {
            const float t = float(i) / float(w * h - 1);
            pixels[i] = RGBColor(t, 1.f - t, 0.25f);
            alpha[i] = t;
        }

        // RGB
        Check(WritePngImage(2, false, "rt_rgb8.png", pixels, alpha, w, h, w, h,
                            0, 0, ColorSystem::DefaultColorSystem, 2.2f),
              "PNG write 8-bit RGB");
        ImageData img;
        Check(ReadPngImage("rt_rgb8.png", &img), "PNG read 8-bit RGB");
        bool ok = img.Width() == w && img.Height() == h && img.Channels() == 3 &&
                  img.Type() == ImageData::UNSIGNED_CHAR_TYPE;
        Check(ok, "PNG 8-bit RGB header (size/channels/type)");
        if (ok)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
                for (int c = 0; c < 3; ++c)
                {
                    const float q = std::round(pixels[i][c] * 255.f) / 255.f;
                    px = px && std::fabs(img.GetUChar(i % w, i / w, c) - q * 255.f) <=
                                   1.5f;
                }
            Check(px, "PNG 8-bit RGB pixels within quantization tolerance");
        }

        // RGBA
        Check(WritePngImage(3, false, "rt_rgba8.png", pixels, alpha, w, h, w, h,
                            0, 0, ColorSystem::DefaultColorSystem, 2.2f),
              "PNG write 8-bit RGBA");
        ImageData imgA;
        Check(ReadPngImage("rt_rgba8.png", &imgA) && imgA.Channels() == 4,
              "PNG read 8-bit RGBA (4 channels)");
        if (imgA.Channels() == 4)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
                px = px && std::fabs(imgA.GetUChar(i % w, i / w, 3) -
                                     std::round(alpha[i] * 255.f)) <= 1.5f;
            Check(px, "PNG 8-bit RGBA alpha within tolerance");
        }

        // Gray
        Check(WritePngImage(0, false, "rt_gray8.png", pixels, alpha, w, h, w, h,
                            0, 0, ColorSystem::DefaultColorSystem, 2.2f),
              "PNG write 8-bit gray");
        ImageData imgG;
        Check(ReadPngImage("rt_gray8.png", &imgG) && imgG.Channels() == 1,
              "PNG read 8-bit gray (1 channel)");
        if (imgG.Channels() == 1)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
                px = px && std::fabs(imgG.GetUChar(i % w, i / w, 0) -
                                     std::round(pixels[i].Y() * 255.f)) <= 1.5f;
            Check(px, "PNG 8-bit gray uses RGB luma");
        }
    }

    void CheckPngRoundTrip16()
    {
        const int w = 4, h = 4;
        std::vector<RGBColor> pixels(w * h);
        std::vector<float> alpha(w * h, 1.f);
        for (int i = 0; i < w * h; ++i)
        {
            const float t = float(i) / float(w * h - 1);
            pixels[i] = RGBColor(t, t * 0.5f, 1.f - t);
        }

        Check(WritePngImage(2, true, "rt_rgb16.png", pixels, alpha, w, h, w, h,
                            0, 0, ColorSystem::DefaultColorSystem, 2.2f),
              "PNG write 16-bit RGB");
        ImageData img;
        Check(ReadPngImage("rt_rgb16.png", &img) &&
                  img.Type() == ImageData::UNSIGNED_SHORT_TYPE &&
                  img.Channels() == 3,
              "PNG read 16-bit RGB (ushort, 3 channels)");
        if (img.Type() == ImageData::UNSIGNED_SHORT_TYPE && img.Channels() == 3)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
                for (int c = 0; c < 3; ++c)
                {
                    const float q = std::round(pixels[i][c] * 65535.f);
                    px = px && std::fabs(float(img.GetUShort(i % w, i / w, c)) - q) <=
                                   2.f;
                }
            Check(px, "PNG 16-bit RGB pixels within quantization tolerance");
        }
    }

    void CheckPngGamma()
    {
        const int w = 2, h = 2;
        std::vector<RGBColor> pixels(w * h, RGBColor(0.5f));
        std::vector<float> alpha(w * h, 1.f);
        WritePngImage(2, false, "gamma.png", pixels, alpha, w, h, w, h, 0, 0,
                      ColorSystem::DefaultColorSystem, 2.2f);
        ImageData img;
        float gamma = -1.f;
        Check(ReadPngImage("gamma.png", &img, &gamma) &&
                  RelClose(gamma, 1.f / 2.2f, 1e-3f),
              "PNG gAMA recorded as 1/screenGamma on read");
    }

    void CheckPngCrop()
    {
        const int W = 10, H = 8;
        const int cw = 4, ch = 3, x0 = 3, y0 = 2;
        std::vector<RGBColor> pixels(cw * ch, RGBColor(1.f));
        std::vector<float> alpha(cw * ch, 1.f);
        Check(WritePngImage(2, false, "crop.png", pixels, alpha, cw, ch, W, H,
                            x0, y0, ColorSystem::DefaultColorSystem, 2.2f),
              "PNG cropped write");
        ImageData img;
        Check(ReadPngImage("crop.png", &img) && img.Width() == W && img.Height() == H,
              "PNG crop preserves full canvas size");
        if (img.Width() == W && img.Height() == H)
        {
            bool inside = true, outside = true;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const bool inCrop = x >= x0 && x < x0 + cw &&
                                        y >= y0 && y < y0 + ch;
                    const float v = img.GetUChar(x, y, 0);
                    if (inCrop)
                        inside = inside && v > 250.f;
                    else
                        outside = outside && v == 0;
                }
            Check(inside, "PNG crop band contains the written pixels");
            Check(outside, "PNG zero band outside the crop");
        }
    }

    // ---------------------------------------------------------------------------
    // EXR
    // ---------------------------------------------------------------------------

    void CheckExrRoundTripFloat()
    {
        const int w = 6, h = 5;
        std::vector<RGBColor> pixels(w * h);
        std::vector<float> alpha(w * h);
        for (int i = 0; i < w * h; ++i)
        {
            const float t = float(i) / float(w * h - 1);
            pixels[i] = RGBColor(t, 2.f * t, 0.5f - t);
            alpha[i] = t;
        }

        Check(WriteOpenEXRImage(3, false, 1, "rt_rgba.exr", pixels, alpha, w, h,
                                w, h, 0, 0),
              "EXR write float RGBA");
        ImageData img;
        Check(ReadOpenEXRImage("rt_rgba.exr", &img) && img.Channels() == 4 &&
                  img.Type() == ImageData::FLOAT_TYPE,
              "EXR read float RGBA (4 channels, float)");
        if (img.Channels() == 4)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
                for (int c = 0; c < 3; ++c)
                    px = px && RelClose(img.GetFloat(i % w, i / w, c), pixels[i][c],
                                        1e-6f);
            px = px && RelClose(img.GetFloat(0, 0, 3), alpha[0], 1e-6f);
            Check(px, "EXR float RGBA pixels exact round-trip");
        }

        // RGB (no alpha)
        Check(WriteOpenEXRImage(2, false, 2, "rt_rgb.exr", pixels, alpha, w, h,
                                w, h, 0, 0),
              "EXR write float RGB");
        ImageData img3;
        Check(ReadOpenEXRImage("rt_rgb.exr", &img3) && img3.Channels() == 3,
              "EXR read float RGB (3 channels)");
    }

    void CheckExrRoundTripHalf()
    {
        const int w = 4, h = 4;
        std::vector<RGBColor> pixels(w * h);
        std::vector<float> alpha(w * h, 0.75f);
        for (int i = 0; i < w * h; ++i)
        {
            const float t = float(i) / float(w * h - 1);
            pixels[i] = RGBColor(t, 1.f - t, 0.33f);
        }
        Check(WriteOpenEXRImage(3, true, 0, "rt_half.exr", pixels, alpha, w, h,
                                w, h, 0, 0),
              "EXR write half RGBA");
        ImageData img;
        Check(ReadOpenEXRImage("rt_half.exr", &img) && img.Channels() == 4,
              "EXR read half RGBA (converted to float)");
        if (img.Channels() == 4)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
                for (int c = 0; c < 3; ++c)
                    px = px && RelClose(img.GetFloat(i % w, i / w, c), pixels[i][c],
                                        1e-3f);
            Check(px, "EXR half RGBA within half precision tolerance");
        }
    }

    void CheckExrYModes()
    {
        const int w = 3, h = 3;
        std::vector<RGBColor> pixels(w * h, RGBColor(0.2f, 0.5f, 0.8f));
        std::vector<float> alpha(w * h, 0.5f);

        // Y only: legacy fixed weights 0.3/0.59/0.11.
        Check(WriteOpenEXRImage(0, false, 1, "y.exr", pixels, alpha, w, h, w, h, 0, 0),
              "EXR write Y");
        ImageData imgY;
        Check(ReadOpenEXRImage("y.exr", &imgY) && imgY.Channels() == 1,
              "EXR read Y (1 channel)");
        if (imgY.Channels() == 1)
        {
            const float expect = 0.3f * 0.2f + 0.59f * 0.5f + 0.11f * 0.8f;
            Check(RelClose(imgY.GetFloat(1, 1, 0), expect, 1e-6f),
                  "EXR Y uses legacy fixed luminance weights");
        }

        // Y,A
        Check(WriteOpenEXRImage(1, false, 1, "ya.exr", pixels, alpha, w, h, w, h,
                                0, 0),
              "EXR write YA");
        ImageData imgYA;
        Check(ReadOpenEXRImage("ya.exr", &imgYA) && imgYA.Channels() == 2 &&
                  RelClose(imgYA.GetFloat(0, 0, 1), 0.5f, 1e-6f),
              "EXR read YA (2 channels, alpha preserved)");
    }

    void CheckExrCropMapping()
    {
        // Full-image header, offset dataWindow: every dataWindow pixel (x, y)
        // must resolve to buffer[(x - x0) + (y - y0) * cw] on read-back
        // (plan decision 8 / row convention).
        const int W = 12, H = 9;
        const int cw = 5, ch = 4, x0 = 3, y0 = 2;
        std::vector<RGBColor> pixels(cw * ch);
        std::vector<float> alpha(cw * ch, 1.f);
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                pixels[y * cw + x] = RGBColor(float(x) / float(cw - 1),
                                              float(y) / float(ch - 1),
                                              float(x + y) / float(cw + ch - 2));

        Check(WriteOpenEXRImage(2, false, 4, "crop.exr", pixels, alpha, cw, ch,
                                W, H, x0, y0),
              "EXR cropped write (offset dataWindow)");
        ImageData img;
        Check(ReadOpenEXRImage("crop.exr", &img) && img.Width() == cw &&
                  img.Height() == ch,
              "EXR crop reads back crop-sized buffer");
        if (img.Width() == cw && img.Height() == ch)
        {
            bool px = true;
            for (int y = 0; y < ch; ++y)
                for (int x = 0; x < cw; ++x)
                {
                    // Storage index [(x - x0) + (y - y0) * cw] == y*cw + x.
                    const RGBColor &want = pixels[y * cw + x];
                    for (int c = 0; c < 3; ++c)
                        px = px && RelClose(img.GetFloat(x, y, c), want[c], 1e-6f);
                }
            Check(px, "EXR cropped pixel mapping round-trips at crop offsets");
        }
    }

    // ---------------------------------------------------------------------------
    // JPEG
    // ---------------------------------------------------------------------------

    void CheckJpegRoundTrip()
    {
        const int w = 8, h = 6;
        std::vector<RGBColor> pixels(w * h);
        std::vector<float> alpha(w * h, 1.f);
        for (int i = 0; i < w * h; ++i)
        {
            const float t = float(i) / float(w * h - 1);
            pixels[i] = RGBColor(t, 1.f - t, 0.25f);
        }

        // 4:4:4 keeps the lossy error small enough for a tight bound.
        Check(WriteJpegImage(2, 95, false, "rt_rgb.jpg", pixels, alpha, w, h,
                             w, h, 0, 0),
              "JPEG write 8-bit RGB (q95, 4:4:4)");
        ImageData img;
        Check(ReadJpegImage("rt_rgb.jpg", &img) && img.Width() == w &&
                  img.Height() == h && img.Channels() == 3 &&
                  img.Type() == ImageData::UNSIGNED_CHAR_TYPE,
              "JPEG read RGB header (size/channels/type)");
        if (img.Channels() == 3)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
                for (int c = 0; c < 3; ++c)
                    px = px && std::fabs(float(img.GetUChar(i % w, i / w, c)) -
                                         pixels[i][c] * 255.f) <= 4.f;
            Check(px, "JPEG RGB pixels within lossy tolerance (4/255)");
        }

        // Gray.
        Check(WriteJpegImage(0, 95, false, "rt_gray.jpg", pixels, alpha, w, h,
                             w, h, 0, 0),
              "JPEG write 8-bit gray");
        ImageData imgG;
        Check(ReadJpegImage("rt_gray.jpg", &imgG) && imgG.Channels() == 1,
              "JPEG read gray (1 channel)");
        if (imgG.Channels() == 1)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
                px = px && std::fabs(float(imgG.GetUChar(i % w, i / w, 0)) -
                                     pixels[i].Y() * 255.f) <= 4.f;
            Check(px, "JPEG gray uses RGB luma within tolerance");
        }

        // Alpha is dropped with a warning: RGBA request yields an RGB file.
        Check(WriteJpegImage(3, 90, true, "rgba_fallback.jpg", pixels, alpha,
                             w, h, w, h, 0, 0),
              "JPEG write RGBA request falls back");
        ImageData imgA;
        Check(ReadJpegImage("rgba_fallback.jpg", &imgA) && imgA.Channels() == 3,
              "JPEG has no alpha channel (3 channels on read-back)");
    }

    void CheckJpegCrop()
    {
        const int W = 10, H = 8;
        const int cw = 4, ch = 3, x0 = 3, y0 = 2;
        std::vector<RGBColor> pixels(cw * ch, RGBColor(1.f));
        std::vector<float> alpha(cw * ch, 1.f);
        Check(WriteJpegImage(2, 100, false, "crop.jpg", pixels, alpha, cw, ch,
                             W, H, x0, y0),
              "JPEG cropped write");
        ImageData img;
        Check(ReadJpegImage("crop.jpg", &img) && img.Width() == W &&
                  img.Height() == H,
              "JPEG crop preserves full canvas size");
        if (img.Width() == W && img.Height() == H)
        {
            // JPEG is block-based: check away from the crop boundary.
            bool inside = true, outside = true;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const bool inCrop = x >= x0 + 1 && x < x0 + cw - 1 &&
                                        y >= y0 + 1 && y < y0 + ch - 1;
                    const float v = img.GetUChar(x, y, 0);
                    if (inCrop)
                        inside = inside && v > 240.f;
                    else if (x < x0 - 1 || x >= x0 + cw + 1 || y < y0 - 1 ||
                             y >= y0 + ch + 1)
                        outside = outside && v < 8.f;
                }
            Check(inside, "JPEG crop band contains the written pixels");
            Check(outside, "JPEG black band outside the crop");
        }
    }

    // ---------------------------------------------------------------------------
    // TIFF
    // ---------------------------------------------------------------------------

    void CheckTiffRoundTrip()
    {
        const int w = 7, h = 5;
        std::vector<RGBColor> pixels(w * h);
        std::vector<float> alpha(w * h);
        for (int i = 0; i < w * h; ++i)
        {
            const float t = float(i) / float(w * h - 1);
            pixels[i] = RGBColor(t, 1.f - t, 0.25f);
            alpha[i] = t;
        }

        // 8-bit RGB, LZW: quantized but lossless after compression.
        Check(WriteTiffImage(2, 8, TIFF_COMPRESSION_LZW, "rt_rgb8.tif", pixels,
                             alpha, w, h, w, h, 0, 0,
                             ColorSystem::DefaultColorSystem, 2.2f),
              "TIFF write 8-bit RGB (LZW)");
        ImageData img;
        Check(ReadTiffImage("rt_rgb8.tif", &img) && img.Width() == w &&
                  img.Height() == h && img.Channels() == 3 &&
                  img.Type() == ImageData::UNSIGNED_CHAR_TYPE,
              "TIFF read 8-bit RGB header");
        if (img.Type() == ImageData::UNSIGNED_CHAR_TYPE && img.Channels() == 3)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
                for (int c = 0; c < 3; ++c)
                {
                    const float q = std::round(pixels[i][c] * 255.f);
                    px = px && std::fabs(float(img.GetUChar(i % w, i / w, c)) -
                                         q) <= 0.5f;
                }
            Check(px, "TIFF 8-bit RGB pixels exact after quantization");
        }

        // 16-bit RGBA, Deflate.
        Check(WriteTiffImage(3, 16, TIFF_COMPRESSION_DEFLATE, "rt_rgba16.tif",
                             pixels, alpha, w, h, w, h, 0, 0,
                             ColorSystem::DefaultColorSystem, 2.2f),
              "TIFF write 16-bit RGBA (Deflate)");
        ImageData imgA;
        Check(ReadTiffImage("rt_rgba16.tif", &imgA) && imgA.Channels() == 4 &&
                  imgA.Type() == ImageData::UNSIGNED_SHORT_TYPE,
              "TIFF read 16-bit RGBA (ushort, 4 channels)");
        if (imgA.Type() == ImageData::UNSIGNED_SHORT_TYPE && imgA.Channels() == 4)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
            {
                for (int c = 0; c < 3; ++c)
                {
                    const float q = std::round(pixels[i][c] * 65535.f);
                    px = px && std::fabs(float(imgA.GetUShort(i % w, i / w, c)) -
                                         q) <= 2.f;
                }
                const float qa = std::round(alpha[i] * 65535.f);
                px = px && std::fabs(float(imgA.GetUShort(i % w, i / w, 3)) -
                                     qa) <= 2.f;
            }
            Check(px, "TIFF 16-bit RGBA pixels and alpha within tolerance");
        }

        // 32-bit float HDR: values outside 0..1 survive exactly.
        std::vector<RGBColor> hdr(w * h);
        for (int i = 0; i < w * h; ++i)
        {
            const float t = float(i) / float(w * h - 1);
            hdr[i] = RGBColor(4.f * t, 0.5f - t, t * t * 100.f);
        }
        Check(WriteTiffImage(2, 32, TIFF_COMPRESSION_NONE, "hdr.tif", hdr,
                             alpha, w, h, w, h, 0, 0,
                             ColorSystem::DefaultColorSystem, 2.2f),
              "TIFF write 32-bit float RGB (HDR)");
        ImageData imgF;
        Check(ReadTiffImage("hdr.tif", &imgF) && imgF.Channels() == 3 &&
                  imgF.Type() == ImageData::FLOAT_TYPE,
              "TIFF read float HDR type");
        if (imgF.Type() == ImageData::FLOAT_TYPE && imgF.Channels() == 3)
        {
            bool px = true;
            for (int i = 0; i < w * h; ++i)
                for (int c = 0; c < 3; ++c)
                    px = px && RelClose(imgF.GetFloat(i % w, i / w, c),
                                        hdr[i][c], 1e-6f);
            Check(px, "TIFF float HDR pixels exact round-trip");
        }

        // Gray and YA.
        std::vector<RGBColor> flat(w * h, RGBColor(0.2f, 0.5f, 0.8f));
        std::vector<float> half(w * h, 0.5f);
        Check(WriteTiffImage(0, 8, TIFF_COMPRESSION_NONE, "y.tif", flat, half,
                             w, h, w, h, 0, 0, ColorSystem::DefaultColorSystem,
                             2.2f),
              "TIFF write gray");
        ImageData imgY;
        Check(ReadTiffImage("y.tif", &imgY) && imgY.Channels() == 1,
              "TIFF read gray (1 channel)");
        if (imgY.Channels() == 1)
            Check(std::fabs(float(imgY.GetUChar(1, 1, 0)) -
                             flat[0].Y() * 255.f) <= 1.5f,
                  "TIFF gray uses RGB luma");

        Check(WriteTiffImage(1, 8, TIFF_COMPRESSION_NONE, "ya.tif", flat, half,
                             w, h, w, h, 0, 0, ColorSystem::DefaultColorSystem,
                             2.2f),
              "TIFF write YA");
        ImageData imgYA;
        Check(ReadTiffImage("ya.tif", &imgYA) && imgYA.Channels() == 2 &&
                  std::fabs(float(imgYA.GetUChar(0, 0, 1)) - 0.5f * 255.f) <= 1.5f,
              "TIFF read YA (2 channels, alpha preserved)");
    }

    void CheckTiffCrop()
    {
        const int W = 10, H = 8;
        const int cw = 4, ch = 3, x0 = 3, y0 = 2;
        std::vector<RGBColor> pixels(cw * ch, RGBColor(1.f));
        std::vector<float> alpha(cw * ch, 1.f);
        Check(WriteTiffImage(2, 8, TIFF_COMPRESSION_LZW, "crop.tif", pixels,
                             alpha, cw, ch, W, H, x0, y0,
                             ColorSystem::DefaultColorSystem, 2.2f),
              "TIFF cropped write");
        ImageData img;
        Check(ReadTiffImage("crop.tif", &img) && img.Width() == W &&
                  img.Height() == H,
              "TIFF crop preserves full canvas size");
        if (img.Width() == W && img.Height() == H)
        {
            bool inside = true, outside = true;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const bool inCrop = x >= x0 && x < x0 + cw &&
                                        y >= y0 && y < y0 + ch;
                    const float v = img.GetUChar(x, y, 0);
                    if (inCrop)
                        inside = inside && v == 255.f;
                    else
                        outside = outside && v == 0;
                }
            Check(inside, "TIFF crop band contains the written pixels");
            Check(outside, "TIFF zero band outside the crop");
        }
    }

    void CheckErrorPaths()
    {
        ImageData img;
        Check(!ReadOpenEXRImage("no_such_file.exr", &img),
              "EXR read missing file returns false");
        Check(!ReadPngImage("no_such_file.png", &img),
              "PNG read missing file returns false");
        Check(!ReadJpegImage("no_such_file.jpg", &img),
              "JPEG read missing file returns false");
        Check(!ReadTiffImage("no_such_file.tif", &img),
              "TIFF read missing file returns false");

        // Corrupt files: wrong magic bytes.
        {
            FILE *f = fopen("corrupt.exr", "wb");
            fputs("not an exr at all", f);
            fclose(f);
            f = fopen("corrupt.png", "wb");
            fputs("not a png at all", f);
            fclose(f);
            f = fopen("corrupt.jpg", "wb");
            fputs("not a jpeg at all", f);
            fclose(f);
            f = fopen("corrupt.tif", "wb");
            fputs("not a tiff at all", f);
            fclose(f);
        }
        Check(!ReadOpenEXRImage("corrupt.exr", &img),
              "EXR read corrupt file returns false without crashing");
        Check(!ReadPngImage("corrupt.png", &img),
              "PNG read corrupt file returns false without crashing");
        Check(!ReadJpegImage("corrupt.jpg", &img),
              "JPEG read corrupt file returns false without crashing");
        Check(!ReadTiffImage("corrupt.tif", &img),
              "TIFF read corrupt file returns false without crashing");

        std::remove("corrupt.exr");
        std::remove("corrupt.png");
        std::remove("corrupt.jpg");
        std::remove("corrupt.tif");
    }

    // Multi-channel named-plane writer + general reader helpers.
    void CheckExrMultiChannel()
    {
        const int w = 4, h = 3;
        const std::size_t n = std::size_t(w) * h;
        std::vector<float> br(n), bg(n), bb(n), a(n), kr(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const float t = float(i) / float(n - 1);
            br[i] = t;
            bg[i] = 2.f * t;
            bb[i] = 0.5f - t;
            a[i] = t;
            kr[i] = 3.f + t; // distinct range so mixups are visible
        }
        std::vector<EXRChannel> chans = {
            {"beauty.R", br.data()}, {"beauty.G", bg.data()},
            {"beauty.B", bb.data()}, {"A", a.data()},
            {"KeyLight.R", kr.data()},
        };
        Check(WriteOpenEXRChannels(false, 1, "multi.exr", chans, w, h, w, h, 0, 0),
              "EXR write multi-channel (named planes)");

        std::vector<std::string> names;
        Check(ListOpenEXRChannels("multi.exr", &names),
              "EXR list channels");
        // All five names present (OpenEXR stores them alphabetically).
        auto has = [&](const char *nm)
        { return std::find(names.begin(), names.end(), nm) != names.end(); };
        Check(has("beauty.R") && has("beauty.G") && has("beauty.B") &&
                  has("A") && has("KeyLight.R"),
              "EXR channel list contains all written names");
        Check(names.size() == 5, "EXR channel count is 5");

        // Per-channel float round-trip.
        std::vector<float> got;
        Check(ReadOpenEXRChannel("multi.exr", "KeyLight.R", &got) &&
                  got.size() == n,
              "EXR read named channel");
        bool px = got.size() == n;
        for (std::size_t i = 0; i < n && px; ++i)
            px = RelClose(got[i], kr[i], 1e-6f);
        Check(px, "EXR named channel pixels exact round-trip");

        // A second channel reads back its own data (no cross-contamination).
        std::vector<float> gotB;
        bool bpx = ReadOpenEXRChannel("multi.exr", "beauty.G", &gotB) &&
                   gotB.size() == n;
        for (std::size_t i = 0; i < n && bpx; ++i)
            bpx = RelClose(gotB[i], bg[i], 1e-6f);
        Check(bpx, "EXR second named channel not contaminated");

        // Missing channel returns false.
        std::vector<float> none;
        Check(!ReadOpenEXRChannel("multi.exr", "nope.R", &none),
              "EXR read missing channel fails");
    }

    void Cleanup()
    {
        std::remove("multi.exr");
        std::remove("rt_rgb8.png");
        std::remove("rt_rgba8.png");
        std::remove("rt_gray8.png");
        std::remove("rt_rgb16.png");
        std::remove("gamma.png");
        std::remove("crop.png");
        std::remove("rt_rgba.exr");
        std::remove("rt_rgb.exr");
        std::remove("rt_half.exr");
        std::remove("y.exr");
        std::remove("ya.exr");
        std::remove("crop.exr");
        std::remove("rt_rgb.jpg");
        std::remove("rt_gray.jpg");
        std::remove("rgba_fallback.jpg");
        std::remove("crop.jpg");
        std::remove("rt_rgb8.tif");
        std::remove("rt_rgba16.tif");
        std::remove("hdr.tif");
        std::remove("y.tif");
        std::remove("ya.tif");
        std::remove("crop.tif");
    }

} // namespace

int main()
{
    std::cout << "lux2 imageio_check (Stage D)" << std::endl;

    CheckPngRoundTrip8();
    CheckPngRoundTrip16();
    CheckPngGamma();
    CheckPngCrop();
    CheckExrRoundTripFloat();
    CheckExrRoundTripHalf();
    CheckExrYModes();
    CheckExrCropMapping();
    CheckExrMultiChannel();
    CheckJpegRoundTrip();
    CheckJpegCrop();
    CheckTiffRoundTrip();
    CheckTiffCrop();
    CheckErrorPaths();

    Cleanup();

    if (g_failures == 0)
    {
        std::cout << "ALL IMAGEIO CHECKS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " imageio check(s) FAILED" << std::endl;
    return 1;
}
