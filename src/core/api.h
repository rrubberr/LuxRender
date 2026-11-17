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

#ifndef LUX2_API_H
#define LUX2_API_H 1

#include "core/error.h"

#ifdef __cplusplus
extern "C"
{
#endif

	typedef const char *LuxToken;
	typedef const char *LuxPointer;
#define LUX_NULL NULL

	LUX_EXPORT const char *luxVersion();
	LUX_EXPORT void luxInit();
	LUX_EXPORT int luxParse(const char *filename);
	// Allows for parsing of partial files.
	LUX_EXPORT int luxParsePartial(const char *filename);
	// Set if to start the rendering after the end of parsing phase (default is true).
	LUX_EXPORT void luxStartRenderingAfterParse(const bool start);
	// Used to end the parse phase.
	LUX_EXPORT void luxParseEnd();
	LUX_EXPORT void luxCleanup();
	LUX_EXPORT void resetFlm();

	// Basic control flow, scoping, stacks.
	LUX_EXPORT void luxIdentity();
	LUX_EXPORT void luxTranslate(float dx, float dy, float dz);
	LUX_EXPORT void luxRotate(float angle, float ax, float ay, float az);
	LUX_EXPORT void luxScale(float sx, float sy, float sz);
	LUX_EXPORT void luxLookAt(float ex, float ey, float ez, float lx, float ly, float lz, float ux, float uy, float uz);
	LUX_EXPORT void luxConcatTransform(float transform[16]);
	LUX_EXPORT void luxTransform(float transform[16]);
	LUX_EXPORT void luxCoordinateSystem(const char *);
	LUX_EXPORT void luxCoordSysTransform(const char *);
	LUX_EXPORT void luxPixelFilter(const char *name, ...);
	LUX_EXPORT void luxPixelFilterV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxFilm(const char *name, ...);
	LUX_EXPORT void luxFilmV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxSampler(const char *name, ...);
	LUX_EXPORT void luxSamplerV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxAccelerator(const char *name, ...);
	LUX_EXPORT void luxAcceleratorV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxSurfaceIntegrator(const char *name, ...);
	LUX_EXPORT void luxSurfaceIntegratorV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxVolumeIntegrator(const char *name, ...);
	LUX_EXPORT void luxVolumeIntegratorV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxCamera(const char *name, ...);
	LUX_EXPORT void luxCameraV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxWorldBegin();
	LUX_EXPORT void luxAttributeBegin();
	LUX_EXPORT void luxAttributeEnd();
	LUX_EXPORT void luxTransformBegin();
	LUX_EXPORT void luxTransformEnd();
	LUX_EXPORT void luxMotionBegin(unsigned int n, float *t);
	LUX_EXPORT void luxMotionEnd();
	LUX_EXPORT void luxTexture(const char *name, const char *type, const char *texname, ...);
	LUX_EXPORT void luxTextureV(const char *name, const char *type, const char *texname, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxMaterial(const char *name, ...);
	LUX_EXPORT void luxMaterialV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxMakeNamedMaterial(const char *name, ...);
	LUX_EXPORT void luxMakeNamedMaterialV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxNamedMaterial(const char *name);
	LUX_EXPORT void luxLightSource(const char *name, ...);
	LUX_EXPORT void luxLightSourceV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxAreaLightSource(const char *name, ...);
	LUX_EXPORT void luxAreaLightSourceV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxPortalShape(const char *name, ...);
	LUX_EXPORT void luxPortalShapeV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxShape(const char *name, ...);
	LUX_EXPORT void luxShapeV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxRenderer(const char *name, ...);
	LUX_EXPORT void luxRendererV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxReverseOrientation();
	LUX_EXPORT void luxMakeNamedVolume(const char *id, const char *name, ...);
	LUX_EXPORT void luxMakeNamedVolumeV(const char *id, const char *name, unsigned int n,
										const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxVolume(const char *name, ...);
	LUX_EXPORT void luxVolumeV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[]);
	LUX_EXPORT void luxExterior(const char *name);
	LUX_EXPORT void luxInterior(const char *name);
	LUX_EXPORT void luxObjectBegin(const char *name);
	LUX_EXPORT void luxObjectEnd();
	LUX_EXPORT void luxObjectInstance(const char *name);
	LUX_EXPORT void luxPortalInstance(const char *name);
	LUX_EXPORT void luxMotionInstance(const char *name, float startTime, float endTime, const char *toTransform);
	LUX_EXPORT void luxWorldEnd();

	// Load/Save FLM file.
	LUX_EXPORT void luxLoadFLM(const char *name);
	LUX_EXPORT void luxSaveFLM(const char *name);

	LUX_EXPORT unsigned char *luxSaveFLMToStream(unsigned int &size);
	LUX_EXPORT void luxDeleteFLMBuffer(unsigned char *buffer);
	LUX_EXPORT void luxLoadFLMFromStream(char *buffer, unsigned int bufSize, const char *name);
	LUX_EXPORT double luxUpdateFLMFromStream(char *buffer, unsigned int bufSize);

	// Overrides the resume settings of the Film in the next scene to resume from the given FLM file, or use filename from scene if empty.
	LUX_EXPORT void luxOverrideResumeFLM(const char *name);
	// Overrides the Film output filename.
	LUX_EXPORT void luxOverrideFilename(const char *name);

	// Write film to a floating point OpenEXR image.
	LUX_EXPORT int luxSaveEXR(const char *name, bool useHalfFloat, bool includeZBuffer, int compressionType, bool tonemapped);

	// User interactive thread functions.
	LUX_EXPORT void luxStart();
	LUX_EXPORT void luxPause();
	LUX_EXPORT void luxExit();
	LUX_EXPORT void luxAbort();
	LUX_EXPORT void luxWait();

	LUX_EXPORT void luxSetHaltSamplesPerPixel(int haltspp, bool haveEnoughSamplesPerPixel, bool suspendThreadsWhenDone);

	// Controlling number of threads (effective at the next render start).
	LUX_EXPORT void luxSetThreadCount(unsigned int n);
	LUX_EXPORT unsigned int luxGetThreadCount();

	// Set the minimum and maximum value used for epsilon.
	LUX_EXPORT void luxSetEpsilon(const float minValue, const float maxValue);

	// Framebuffer access.
	LUX_EXPORT void luxUpdateFramebuffer();
	LUX_EXPORT unsigned char *luxFramebuffer();
	LUX_EXPORT float *luxFloatFramebuffer();
	LUX_EXPORT float *luxAlphaBuffer();

	// User defined sampling.
	LUX_EXPORT void luxSetUserSamplingMap(const float *map);
	// NOTE: returns a copy of the map, it is up to the caller to free the allocated memory!
	LUX_EXPORT float *luxGetUserSamplingMap();

	// Histogram access.
	LUX_EXPORT void luxGetHistogramImage(unsigned char *outPixels, unsigned int width, unsigned int height, int options);

	// Histogram drawing options.
	#define LUX_HISTOGRAM_RGB 1
	#define LUX_HISTOGRAM_RGB_ADD 2
	#define LUX_HISTOGRAM_RED 4
	#define LUX_HISTOGRAM_GREEN 8
	#define LUX_HISTOGRAM_BLUE 16
	#define LUX_HISTOGRAM_VALUE 32
	#define LUX_HISTOGRAM_LOG 64

	// Exposed Parameters

	enum luxComponent
	{
		LUX_FILM
	};

	enum luxComponentParameters
	{
		LUX_FILM_TM_TONEMAPKERNEL,
		LUX_FILM_TM_REINHARD_PRESCALE,
		LUX_FILM_TM_REINHARD_POSTSCALE,
		LUX_FILM_TM_REINHARD_BURN,
		LUX_FILM_TM_LINEAR_SENSITIVITY,
		LUX_FILM_TM_LINEAR_EXPOSURE,
		LUX_FILM_TM_LINEAR_FSTOP,
		LUX_FILM_TM_LINEAR_GAMMA,
		LUX_FILM_TORGB_X_WHITE,
		LUX_FILM_TORGB_Y_WHITE,
		LUX_FILM_TORGB_X_RED,
		LUX_FILM_TORGB_Y_RED,
		LUX_FILM_TORGB_X_GREEN,
		LUX_FILM_TORGB_Y_GREEN,
		LUX_FILM_TORGB_X_BLUE,
		LUX_FILM_TORGB_Y_BLUE,
		LUX_FILM_TORGB_GAMMA,

		// Light-group control.
		LUX_FILM_LG_COUNT,
		LUX_FILM_LG_ENABLE,
		LUX_FILM_LG_NAME,
		LUX_FILM_LG_SCALE,
		LUX_FILM_LG_SCALE_RED,
		LUX_FILM_LG_SCALE_BLUE,
		LUX_FILM_LG_SCALE_GREEN,
		LUX_FILM_LG_TEMPERATURE,
		LUX_FILM_LG_SCALE_X,
		LUX_FILM_LG_SCALE_Y,
		LUX_FILM_LG_SCALE_Z
	};

	// Parameter Access functions.
	LUX_EXPORT void luxSetParameterValue(luxComponent comp, luxComponentParameters param, double value, unsigned int index = 0);
	LUX_EXPORT double luxGetParameterValue(luxComponent comp, luxComponentParameters param, unsigned int index = 0);
	LUX_EXPORT double luxGetDefaultParameterValue(luxComponent comp, luxComponentParameters param, unsigned int index = 0);
	LUX_EXPORT void luxSetStringParameterValue(luxComponent comp, luxComponentParameters param, const char *value, unsigned int index = 0);
	LUX_EXPORT unsigned int luxGetStringParameterValue(luxComponent comp, luxComponentParameters param, char *dst, unsigned int dstlen, unsigned int index = 0);
	LUX_EXPORT unsigned int luxGetDefaultStringParameterValue(luxComponent comp, luxComponentParameters param, char *dst, unsigned int dstlen, unsigned int index = 0);

	// Queryable objects.
	enum luxAttributeType
	{ // This should stay in sync with AttributeType::DataType.
		LUX_ATTRIBUTETYPE_NONE,
		LUX_ATTRIBUTETYPE_BOOL,
		LUX_ATTRIBUTETYPE_INT,
		LUX_ATTRIBUTETYPE_FLOAT,
		LUX_ATTRIBUTETYPE_DOUBLE,
		LUX_ATTRIBUTETYPE_STRING
	};

	LUX_EXPORT const char *luxGetAttributes();																							   // Returns an XML string containing all queryable data of the current context.
	LUX_EXPORT bool luxHasObject(const char *objectName);																				   // Returns true if the given object exists in the registry.
	LUX_EXPORT bool luxHasAttribute(const char *objectName, const char *attributeName);													   // Returns true if object has the given attribute.
	LUX_EXPORT luxAttributeType luxGetAttributeType(const char *objectName, const char *attributeName);									   // Returns the type of the attribute.
	LUX_EXPORT unsigned int luxGetAttributeDescription(const char *objectName, const char *attributeName, char *dst, unsigned int dstlen); // Gets the description of the attribute.
	LUX_EXPORT bool luxHasAttributeDefaultValue(const char *objectName, const char *attributeName);										   // Returns true if attribute has a default value.

	LUX_EXPORT unsigned int luxGetStringAttribute(const char *objectName, const char *attributeName, char *dst, unsigned int dstlen);
	LUX_EXPORT unsigned int luxGetStringAttributeDefault(const char *objectName, const char *attributeName, char *dst, unsigned int dstlen);
	LUX_EXPORT void luxSetStringAttribute(const char *objectName, const char *attributeName, const char *value);
	LUX_EXPORT float luxGetFloatAttribute(const char *objectName, const char *attributeName);				 // Returns the value of a float attribute.
	LUX_EXPORT float luxGetFloatAttributeDefault(const char *objectName, const char *attributeName);		 // Returns the default value of a float attribute.
	LUX_EXPORT void luxSetFloatAttribute(const char *objectName, const char *attributeName, float value);	 // Sets an float attribute value.
	LUX_EXPORT double luxGetDoubleAttribute(const char *objectName, const char *attributeName);				 // Returns the value of a double attribute.
	LUX_EXPORT double luxGetDoubleAttributeDefault(const char *objectName, const char *attributeName);		 // Returns the default value of a double attribute.
	LUX_EXPORT void luxSetDoubleAttribute(const char *objectName, const char *attributeName, double value);	 // Sets an double attribute value.
	LUX_EXPORT int luxGetIntAttribute(const char *objectName, const char *attributeName);					 // Returns the value of an int attribute.
	LUX_EXPORT int luxGetIntAttributeDefault(const char *objectName, const char *attributeName);			 // Returns the default value of an int attribute.
	LUX_EXPORT void luxSetIntAttribute(const char *objectName, const char *attributeName, int value);		 // Sets an int attribute value.
	LUX_EXPORT bool luxGetBoolAttribute(const char *objectName, const char *attributeName);					 // Returns the value of a bool attribute.
	LUX_EXPORT bool luxGetBoolAttributeDefault(const char *objectName, const char *attributeName);			 // Returns the default value of a bool attribute.
	LUX_EXPORT void luxSetBoolAttribute(const char *objectName, const char *attributeName, bool value);		 // Sets a bool attribute value.
	LUX_EXPORT void luxSetAttribute(const char *objectName, const char *attributeName, int n, void *values); // Sets an attribute value.

	// Informations and statistics.
	LUX_EXPORT double luxStatistics(const char *statName);

	// Dade - enable debug mode.
	LUX_EXPORT void luxEnableDebugMode();
	LUX_EXPORT void luxDisableRandomMode();

	// Error Handlers.
	LUX_EXPORT extern int luxLastError;					 // keeps track of the last error code
	LUX_EXPORT extern void luxErrorFilter(int severity); // sets the minimal level of severity to report
	LUX_EXPORT extern int luxGetErrorFilter();			 // gets the minimal level of severity to report
	typedef void (*LuxErrorHandler)(int code, int severity, const char *msg);
	LUX_EXPORT extern void luxErrorHandler(LuxErrorHandler handler);
	LUX_EXPORT extern void luxErrorAbort(int code, int severity, const char *message);
	LUX_EXPORT extern void luxErrorIgnore(int code, int severity, const char *message);
	LUX_EXPORT extern void luxErrorPrint(int code, int severity, const char *message);
	LUX_EXPORT extern LuxErrorHandler luxError;

	// Reduce the magnitude on the input number by dividing into units.
	LUX_EXPORT double luxMagnitudeReduce(double number);

	// Return the magnitude prefix char for units.
	LUX_EXPORT const char *luxMagnitudePrefix(double number);

#ifdef __cplusplus
}
#endif

#endif // LUX2_API_H
