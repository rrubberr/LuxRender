/***************************************************************************
 *   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   Lux Renderer is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   Lux Renderer is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 *                                                                         *
 *   This project is based on PBRT ; see http://www.pbrt.org               *
 *   Lux Renderer website : http://www.luxrender.net                       *
 ***************************************************************************/

// exrio.cpp*
#include "lux.h"
#include "error.h"
#include "luxrays/core/color/color.h"
#include "luxrays/core/color/swcspectrum.h"
#include "imagereader.h"
#include "texturecolor.h"
#include <algorithm>

#include <numeric>
#include <memory>
#include <cmath>

//undef to fix breakage caused by luxrays utils.h
#undef isnan
#undef isinf

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/dassert.h>
#include <OpenImageIO/span.h>

#include <boost/filesystem.hpp>

#define cimg_display_type  0

//ifdef LUX_USE_CONFIG start
#ifdef LUX_USE_CONFIG_H
#include "config.h"

#ifdef PNG_FOUND
#define cimg_use_png 1
#endif

#ifdef JPEG_FOUND
#define cimg_use_jpeg 1
#endif

#ifdef TIFF_FOUND
#define cimg_use_tiff 1
#endif

#else //LUX_USE_CONFIG_H
#define cimg_use_png 1
#define cimg_use_tiff 1
#define cimg_use_jpeg 1
#endif //LUX_USE_CONFIG_H

//ifdef LUX_USE_CONFIG end


#define cimg_debug 0     // Disable modal window in CImg exceptions.
// Include the CImg Library, with the GREYCstoration plugin included
#define cimg_plugin "greycstoration.h"
#include "cimg.h"
using namespace cimg_library;

#if defined(WIN32) && !defined(__CYGWIN__)
#define hypotf hypot // For the OpenEXR headers
#endif

// OpenEXR 3.x includes - updated header paths
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfHeader.h>

// Imath is now a separate library in OpenEXR 3.x
#include <Imath/ImathVec.h>
#include <Imath/ImathBox.h>

// Half library
#include <Imath/half.h>

using namespace lux;

// OIIO generates this based on package version.
OIIO_NAMESPACE_USING;

using namespace OPENEXR_IMF_NAMESPACE;  // Updated for OpenEXR 3.x
using namespace IMATH_NAMESPACE;         // Updated for Imath 3.x

namespace lux {

	template <typename T, int C> void* setImageData(const ImageSpec &spec) {

		const u_int width = spec.width;
		const u_int height = spec.height;

		void* ret = new TextureColor<T, C>[width * height];

		return ret;
	}
	template <typename T> void* setImageData(const ImageSpec &spec) {
		const u_int channelCount = spec.nchannels;
		switch (channelCount) {
		case 1:
			return setImageData<T, 1>(spec);
		case 3:
			return setImageData<T, 3>(spec);
		case 4:
			return setImageData<T, 4>(spec);
		default:
			return NULL;
		}
	}

	ImageData *ReadImage(const string &name)
	{
		try {
			boost::filesystem::path imagePath(AdjustFilename(name));
			// boost::filesystem::exists() can throw an exception under Windows
			// if the drive in imagePath doesn't exist
			if (!boost::filesystem::exists(imagePath)) {
				LOG(LUX_ERROR, LUX_NOFILE) <<
					"Unable to open image file '" <<
					imagePath.string() << "'";
				return NULL;
			}

			ImageSpec config;
			config.attribute("oiio:UnassociatedAlpha", 1);
			//std::auto_ptr<ImageInput> in(ImageInput::open(name, &config));
			std::unique_ptr<ImageInput> in = ImageInput::open(name, &config);
			if (in.get()) {
				const ImageSpec &spec = in->spec();

				const u_int width = spec.width;
				const u_int height = spec.height;
				const u_int channelCount = spec.nchannels;

				if ((channelCount != 1) && (channelCount != 3) && (channelCount != 4)) {
					LOG(LUX_ERROR, LUX_BADFILE) << "Unsupported number of channels in an image file:" << channelCount;
					return NULL;
				}

				void* data;
				TypeDesc format;
				ImageData::PixelDataType type;
				u_int bytesPerChannel = spec.channel_bytes();
				switch (bytesPerChannel) {
				case 1:
					type = ImageData::UNSIGNED_CHAR_TYPE;
					data = setImageData<unsigned char>(spec);
					format = TypeDesc::UINT8;
					break;
				case 2:
					type = ImageData::UNSIGNED_SHORT_TYPE;
					data = setImageData<unsigned short>(spec);
					format = TypeDesc::UINT16;
					break;
				case 4:
					type = ImageData::FLOAT_TYPE;
					data = setImageData<float>(spec);
					format = TypeDesc::FLOAT;
					break;
				default:
					LOG(LUX_ERROR, LUX_SYSTEM) <<
						"Unsupported pixel type (size=" << bytesPerChannel << ")";
					return NULL;
				}
				
				// declared at line 151
				// const ImageSpec& spec = in->spec();

					in->read_image(
						0, 0,
						0, spec.nchannels,
						format,
						data
					);

				in->close();
				in.reset();

				return new ImageData(width, height, type, channelCount, data);
			}
			LOG(LUX_ERROR, LUX_BADFILE) <<
				"Cannot recognise file format for image '" <<
				name << "'";
			return NULL;
		}
		catch (const std::exception &e) {
			LOG(LUX_ERROR, LUX_BUG) << "Unable to read image file '" <<
				name << "': " << e.what();
			return NULL;
		}
	}

	/*
	 * To convert a standard EXR to Blender MultiLayer format, change the channel names:
	 * RenderLayer.Combined.R
	 * RenderLayer.Combined.G
	 * RenderLayer.Combined.B
	 * RenderLayer.Combined.A
	 * RenderLayer.Depth.Z
	 * (and force RGBA format)
	 *
	 * and set a header
	 * header.insert("BlenderMultiChannel", StringAttribute("Blender V2.43 and newer"));
	 *
	 * it may also be necessary to flip image data both horizonally and vertically
	 */

	bool WriteOpenEXRImage(int channeltype, bool halftype, bool savezbuf,
		int compressiontype, const string &name, vector<RGBColor> &pixels,
		vector<float> &alpha, u_int xRes, u_int yRes,
		u_int totalXRes, u_int totalYRes, u_int xOffset, u_int yOffset,
		vector<float> &zbuf)
	{
		Header header(totalXRes, totalYRes);

		// Set Compression
		switch (compressiontype) {
		case 0:
			header.compression() = RLE_COMPRESSION;
			break;
		case 1:
			header.compression() = PIZ_COMPRESSION;
			break;
		case 2:
			header.compression() = ZIP_COMPRESSION;
			break;
		case 3:
			header.compression() = PXR24_COMPRESSION;
			break;
		case 4:
			header.compression() = NO_COMPRESSION;
			break;
		default:
			header.compression() = RLE_COMPRESSION;
			break;
		}

		Box2i dataWindow(V2i(xOffset, yOffset),
			V2i(xOffset + xRes - 1, yOffset + yRes - 1));
		header.dataWindow() = dataWindow;

		// Set base channel type
		Imf::PixelType savetype = Imf::FLOAT;
		if (halftype)
			savetype = Imf::HALF;

		// Define channels
		if (channeltype == 0) {
			header.channels().insert("Y", Imf::Channel(savetype));
		}
		else if (channeltype == 1) {
			header.channels().insert("Y", Imf::Channel(savetype));
			header.channels().insert("A", Imf::Channel(savetype));
		}
		else if (channeltype == 2) {
			header.channels().insert("R", Imf::Channel(savetype));
			header.channels().insert("G", Imf::Channel(savetype));
			header.channels().insert("B", Imf::Channel(savetype));
		}
		else {
			header.channels().insert("R", Imf::Channel(savetype));
			header.channels().insert("G", Imf::Channel(savetype));
			header.channels().insert("B", Imf::Channel(savetype));
			header.channels().insert("A", Imf::Channel(savetype));
		}

		// Add 32bit float Zbuf channel if required
		if (savezbuf)
			header.channels().insert("Z", Imf::Channel(Imf::FLOAT));

		FrameBuffer fb;

		// Those buffers will hold image data in case a type
		// conversion is needed.
		// THEY MUST NOT BE DELETED BEFORE DATA IS WRITTEN TO FILE
		float *fy = NULL;
		half *hy = NULL;
		half *hrgb = NULL;
		half *ha = NULL;
		const u_int bufSize = xRes * yRes;
		const u_int bufOffset = xOffset + yOffset * xRes;

		if (!halftype) {
			// Write framebuffer data for 32bit FLOAT type
			if (channeltype <= 1) {
				// Save Y
				fy = new float[bufSize];
				// FIXME use the correct color space
				for (u_int i = 0; i < bufSize; ++i)
					fy[i] = (0.3f * pixels[i].c[0]) +
					(0.59f * pixels[i].c[1]) +
					(0.11f * pixels[i].c[2]);
				fb.insert("Y", Slice(Imf::FLOAT,
					(char *)(fy - bufOffset), sizeof(float),
					xRes * sizeof(float)));
			}
			else if (channeltype >= 2) {
				// Save RGB
				float *frgb = &pixels[0].c[0];
				fb.insert("R", Slice(Imf::FLOAT,
					(char *)(frgb - 3 * bufOffset),
					sizeof(RGBColor), xRes * sizeof(RGBColor)));
				fb.insert("G", Slice(Imf::FLOAT,
					(char *)(frgb - 3 * bufOffset) + sizeof(float),
					sizeof(RGBColor), xRes * sizeof(RGBColor)));
				fb.insert("B", Slice(Imf::FLOAT,
					(char *)(frgb - 3 * bufOffset) + 2 * sizeof(float),
					sizeof(RGBColor), xRes * sizeof(RGBColor)));
			}
			if (channeltype == 1 || channeltype == 3) {
				// Add alpha
				float *fa = &alpha[0];
				fb.insert("A", Slice(Imf::FLOAT,
					(char *)(fa - bufOffset), sizeof(float),
					xRes * sizeof(float)));
			}
		}
		else {
			// Write framebuffer data for 16bit HALF type
			if (channeltype <= 1) {
				// Save Y
				hy = new half[bufSize];
				//FIXME use correct color space
				for (u_int i = 0; i < bufSize; ++i)
					hy[i] = (0.3f * pixels[i].c[0]) +
					(0.59f * pixels[i].c[1]) +
					(0.11f * pixels[i].c[2]);
				fb.insert("Y", Slice(HALF, (char *)(hy - bufOffset),
					sizeof(half), xRes * sizeof(half)));
			}
			else if (channeltype >= 2) {
				// Save RGB
				hrgb = new half[3 * bufSize];
				for (u_int i = 0; i < bufSize; ++i)
					for (u_int j = 0; j < 3; j++)
						hrgb[3 * i + j] = pixels[i].c[j];
				fb.insert("R", Slice(HALF,
					(char *)(hrgb - 3 * bufOffset),
					3 * sizeof(half), xRes * (3 * sizeof(half))));
				fb.insert("G", Slice(HALF,
					(char *)(hrgb - 3 * bufOffset) + sizeof(half),
					3 * sizeof(half), xRes * (3 * sizeof(half))));
				fb.insert("B", Slice(HALF,
					(char *)(hrgb - 3 * bufOffset) + 2 * sizeof(half),
					3 * sizeof(half), xRes * (3 * sizeof(half))));
			}
			if (channeltype == 1 || channeltype == 3) {
				// Add alpha
				ha = new half[bufSize];
				for (u_int i = 0; i < bufSize; ++i)
					ha[i] = alpha[i];
				fb.insert("A", Slice(HALF, (char *)(ha - bufOffset),
					sizeof(half), xRes * sizeof(half)));
			}
		}

		if (savezbuf) {
			float *fz = &zbuf[0];
			// Add Zbuf framebuffer data (always 32bit FLOAT type)
			fb.insert("Z", Slice(Imf::FLOAT, (char *)(fz - bufOffset),
				sizeof(float), xRes * sizeof(float)));
		}

		bool result = true;
		try {
			OutputFile file(name.c_str(), header);
			file.setFrameBuffer(fb);
			file.writePixels(yRes);
		}
		catch (const std::exception &e) {
			LOG(LUX_SEVERE, LUX_BUG) << "Unable to write image file '" <<
				name << "': " << e.what();
			result = false;
		}

		// Cleanup used buffers
		// If the pointer is NULL, delete[] has no effect
		// So it is safe to avoid the NULL check of those pointers
		delete[] fy;
		delete[] hy;
		delete[] hrgb;
		delete[] ha;
		return result;
	}

	// Write a single channel float EXR
	bool WriteOpenEXRImage(const string &name, u_int xRes, u_int yRes, const float *map) {
		Header header(xRes, yRes);
		header.compression() = RLE_COMPRESSION;

		Box2i dataWindow(V2i(0, 0),
			V2i(0 + xRes - 1, 0 + yRes - 1));
		header.dataWindow() = dataWindow;

		Imf::PixelType savetype = Imf::FLOAT;
		header.channels().insert("Y", Imf::Channel(savetype));

		FrameBuffer fb;
		fb.insert("Y", Slice(Imf::FLOAT,
			(char *)map, sizeof(float),
			xRes * sizeof(float)));

		bool result = true;
		try {
			OutputFile file(name.c_str(), header);
			file.setFrameBuffer(fb);
			file.writePixels(yRes);
		}
		catch (const std::exception &e) {
			LOG(LUX_SEVERE, LUX_BUG) << "Unable to write image file '" <<
				name << "': " << e.what();
			result = false;
		}
		return result;
	}
}