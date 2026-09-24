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

#include "core/api.h"

#include "core/context2.h"
#include "core/error.h"
#include "core/paramset.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <string>
#include <vector>

#ifndef LUX2_VERSION_STRING
#define LUX2_VERSION_STRING "0.1.0"
#endif

using namespace lux2;

// Parser entry points.
extern int yyparse(void);
extern void yyrestart(FILE *new_file);
// Defined by the lexer; clears the include stack between parses.
void include_clear();

// Defined at global scope by the parser and shared with the lexer.
extern std::string currentFile;
extern std::uint32_t lineNum;

namespace
{

    // Extract a parameter list.
    unsigned int BuildParameterList(va_list args,
                                    std::vector<LuxToken> &tokens,
                                    std::vector<LuxPointer> &values)
    {
        unsigned int count = 0;
        tokens.clear();
        values.clear();
        LuxToken token = va_arg(args, LuxToken);
        while (token != nullptr && token != LUX_NULL)
        {
            tokens.push_back(token);
            values.push_back(va_arg(args, LuxPointer));
            token = va_arg(args, LuxToken);
            ++count;
        }
        return count;
    }

#define EXTRACT_PARAMETERS(_start)   \
    va_list pArgs;                   \
    va_start(pArgs, _start);         \
    std::vector<LuxToken> aTokens;   \
    std::vector<LuxPointer> aValues; \
    unsigned int count = BuildParameterList(pArgs, aTokens, aValues);

#define PASS_PARAMETERS \
    count, aTokens.size() > 0 ? &aTokens[0] : 0, aValues.size() > 0 ? &aValues[0] : 0

    bool g_initialized = false;

    // Parse a single file.
    bool ParseFile(const char *filename)
    {
        bool parse_success = false;
        FILE *yyin;

        if (strcmp(filename, "-") == 0)
            yyin = stdin;
        else
            yyin = fopen(filename, "r");

        if (yyin != nullptr)
        {
            currentFile = filename;
            if (yyin == stdin)
                currentFile = "<standard input>";
            lineNum = 1;
            include_clear();
            yyrestart(yyin);
            try
            {
                parse_success = (yyparse() == 0);
            }
            catch (std::runtime_error &e)
            {
                LOG(LUX_SEVERE, LUX_SYSTEM)
                    << "Exception during parsing (file '" << currentFile
                    << "', line: " << lineNum << "): " << e.what();
            }
            if (yyin != stdin)
                fclose(yyin);
        }
        else
        {
            LOG(LUX_SEVERE, LUX_NOFILE) << "Unable to read scenefile '" << filename << "'";
        }

        currentFile = "";
        lineNum = 0;
        return (yyin != nullptr) && parse_success;
    }

} // anonymous namespace

// ---------------------------------------------------------------------------
// Error handler state
// ---------------------------------------------------------------------------
int luxLastError = 0;

static LuxErrorHandler g_errorHandler = nullptr;

void luxErrorFilter(int severity) { LuxLogFilter() = severity; }
int luxGetErrorFilter() { return LuxLogFilter(); }
void luxErrorHandler(LuxErrorHandler handler) { g_errorHandler = handler; }
void luxErrorAbort(int, int, const char *) {}
void luxErrorIgnore(int, int, const char *) {}
void luxErrorPrint(int, int, const char *) {}
LuxErrorHandler luxError = nullptr;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
extern "C" const char *luxVersion()
{
    static const char version[] = LUX2_VERSION_STRING;
    return version;
}

extern "C" void luxInit()
{
    if (g_initialized)
    {
        LOG(LUX_ERROR, LUX_ILLSTATE) << "luxInit() has already been called.";
    }
    else
    {
        Context2::SetActive(new Context2());
        Context2::GetActive()->Init();
    }
    g_initialized = true;
}

extern "C" void luxCleanup()
{
    if (g_initialized)
    {
        Context2::GetActive()->Cleanup();
    }
    else
    {
        LOG(LUX_ERROR, LUX_NOTSTARTED) << "luxCleanup() called without luxInit().";
    }
}

extern "C" int luxParse(const char *filename)
{
    bool parse_success = ParseFile(filename);

    if (!parse_success)
    {
        Context2::GetActive()->Free();
        Context2::GetActive()->Init();
        Context2::GetActive()->MarkParseFail();
    }
    else if (Context2::GetActive()->State() == LUX2_STATE_WORLD_BLOCK)
    {
        LOG(LUX_SEVERE, LUX_BADFILE) << "Missing WorldEnd in scenefile '" << filename << "'";
        Context2::GetActive()->Free();
        Context2::GetActive()->Init();
        Context2::GetActive()->MarkParseFail();
        parse_success = false;
    }
    return parse_success ? 1 : 0;
}

extern "C" int luxParsePartial(const char *filename)
{
    // Caller does error handling.
    return ParseFile(filename) ? 1 : 0;
}

extern "C" void luxStartRenderingAfterParse(const bool start)
{
    Context2::GetActive()->StartRenderingAfterParse(start);
}

extern "C" void luxParseEnd()
{
    Context2::GetActive()->ParseEnd();
}

extern "C" void resetFlm()
{
    LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " resetFlm";
}

// ---------------------------------------------------------------------------
// Transform / state free functions
// ---------------------------------------------------------------------------
extern "C" void luxIdentity() { Context2::GetActive()->Identity(); }
extern "C" void luxTranslate(float dx, float dy, float dz) { Context2::GetActive()->Translate(dx, dy, dz); }
extern "C" void luxTransform(float tr[16]) { Context2::GetActive()->Transform(tr); }
extern "C" void luxConcatTransform(float tr[16]) { Context2::GetActive()->ConcatTransform(tr); }
extern "C" void luxRotate(float angle, float dx, float dy, float dz) { Context2::GetActive()->Rotate(angle, dx, dy, dz); }
extern "C" void luxScale(float sx, float sy, float sz) { Context2::GetActive()->Scale(sx, sy, sz); }
extern "C" void luxLookAt(float ex, float ey, float ez, float lx, float ly, float lz, float ux, float uy, float uz)
{
    Context2::GetActive()->LookAt(ex, ey, ez, lx, ly, lz, ux, uy, uz);
}
extern "C" void luxCoordinateSystem(const char *name) { Context2::GetActive()->CoordinateSystem(std::string(name)); }
extern "C" void luxCoordSysTransform(const char *name) { Context2::GetActive()->CoordSysTransform(std::string(name)); }

// ---------------------------------------------------------------------------
// Plugin dispatchers
// ---------------------------------------------------------------------------
extern "C" void luxPixelFilter(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxPixelFilterV(name, PASS_PARAMETERS);
}
extern "C" void luxPixelFilterV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->PixelFilter(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxFilm(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxFilmV(name, PASS_PARAMETERS);
}
extern "C" void luxFilmV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->Film(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxSampler(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxSamplerV(name, PASS_PARAMETERS);
}
extern "C" void luxSamplerV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->Sampler(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxAccelerator(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxAcceleratorV(name, PASS_PARAMETERS);
}
extern "C" void luxAcceleratorV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->Accelerator(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxSurfaceIntegrator(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxSurfaceIntegratorV(name, PASS_PARAMETERS);
}
extern "C" void luxSurfaceIntegratorV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->SurfaceIntegrator(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxVolumeIntegrator(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxVolumeIntegratorV(name, PASS_PARAMETERS);
}
extern "C" void luxVolumeIntegratorV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->VolumeIntegrator(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxCamera(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxCameraV(name, PASS_PARAMETERS);
}
extern "C" void luxCameraV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->Camera(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxWorldBegin() { Context2::GetActive()->WorldBegin(); }
extern "C" void luxAttributeBegin() { Context2::GetActive()->AttributeBegin(); }
extern "C" void luxAttributeEnd() { Context2::GetActive()->AttributeEnd(); }
extern "C" void luxTransformBegin() { Context2::GetActive()->TransformBegin(); }
extern "C" void luxTransformEnd() { Context2::GetActive()->TransformEnd(); }

extern "C" void luxMotionBegin(unsigned int n, float *t) { Context2::GetActive()->MotionBegin(n, t); }
extern "C" void luxMotionEnd() { Context2::GetActive()->MotionEnd(); }

extern "C" void luxTexture(const char *name, const char *type, const char *texname, ...)
{
    EXTRACT_PARAMETERS(texname);
    luxTextureV(name, type, texname, PASS_PARAMETERS);
}
extern "C" void luxTextureV(const char *name, const char *type, const char *texname,
                            unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->Texture(name, type, texname, ParamSet(n, name, tokens, params));
}

extern "C" void luxMaterial(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxMaterialV(name, PASS_PARAMETERS);
}
extern "C" void luxMaterialV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->Material(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxMakeNamedMaterial(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxMakeNamedMaterialV(name, PASS_PARAMETERS);
}
extern "C" void luxMakeNamedMaterialV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->MakeNamedMaterial(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxNamedMaterial(const char *name) { Context2::GetActive()->NamedMaterial(name); }

extern "C" void luxLightSource(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxLightSourceV(name, PASS_PARAMETERS);
}
extern "C" void luxLightSourceV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->LightSource(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxAreaLightSource(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxAreaLightSourceV(name, PASS_PARAMETERS);
}
extern "C" void luxAreaLightSourceV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->AreaLightSource(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxPortalShape(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxPortalShapeV(name, PASS_PARAMETERS);
}
extern "C" void luxPortalShapeV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->PortalShape(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxShape(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxShapeV(name, PASS_PARAMETERS);
}
extern "C" void luxShapeV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->Shape(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxRenderer(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxRendererV(name, PASS_PARAMETERS);
}
extern "C" void luxRendererV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->Renderer(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxReverseOrientation() { Context2::GetActive()->ReverseOrientation(); }

extern "C" void luxMakeNamedVolume(const char *id, const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxMakeNamedVolumeV(id, name, PASS_PARAMETERS);
}
extern "C" void luxMakeNamedVolumeV(const char *id, const char *name, unsigned int n,
                                    const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->MakeNamedVolume(id, name, ParamSet(n, name, tokens, params));
}

extern "C" void luxVolume(const char *name, ...)
{
    EXTRACT_PARAMETERS(name);
    luxVolumeV(name, PASS_PARAMETERS);
}
extern "C" void luxVolumeV(const char *name, unsigned int n, const LuxToken tokens[], const LuxPointer params[])
{
    Context2::GetActive()->Volume(name, ParamSet(n, name, tokens, params));
}

extern "C" void luxExterior(const char *name) { Context2::GetActive()->Exterior(name); }
extern "C" void luxInterior(const char *name) { Context2::GetActive()->Interior(name); }

extern "C" void luxObjectBegin(const char *name) { Context2::GetActive()->ObjectBegin(std::string(name)); }
extern "C" void luxObjectEnd() { Context2::GetActive()->ObjectEnd(); }
extern "C" void luxObjectInstance(const char *name) { Context2::GetActive()->ObjectInstance(std::string(name)); }

extern "C" void luxPortalInstance(const char *name)
{
    LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxPortalInstance " << name;
}
extern "C" void luxMotionInstance(const char *name, float, float, const char *)
{
    LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxMotionInstance " << name;
}

extern "C" void luxWorldEnd()
{
    srand(static_cast<unsigned int>(time(nullptr)));
    Context2::GetActive()->WorldEnd();
}

// ---------------------------------------------------------------------------
// Film
// ---------------------------------------------------------------------------
extern "C" void luxLoadFLM(const char *) { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxLoadFLM"; }
extern "C" void luxSaveFLM(const char *) { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSaveFLM"; }
extern "C" unsigned char *luxSaveFLMToStream(unsigned int &size)
{
    LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSaveFLMToStream";
    size = 0;
    return nullptr;
}
extern "C" void luxDeleteFLMBuffer(unsigned char *) {}
extern "C" void luxLoadFLMFromStream(char *, unsigned int, const char *)
{
    LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxLoadFLMFromStream";
}
extern "C" double luxUpdateFLMFromStream(char *, unsigned int)
{
    LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxUpdateFLMFromStream";
    return 0.0;
}
extern "C" void luxOverrideResumeFLM(const char *) { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxOverrideResumeFLM"; }
extern "C" void luxOverrideFilename(const char *) { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxOverrideFilename"; }
extern "C" int luxSaveEXR(const char *, bool, bool, int, bool)
{
    LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSaveEXR";
    return 0;
}

// ---------------------------------------------------------------------------
// Render control
// ---------------------------------------------------------------------------
extern "C" void luxStart() { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxStart"; }
extern "C" void luxPause() { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxPause"; }
extern "C" void luxExit() { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxExit"; }
extern "C" void luxAbort() { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxAbort"; }
extern "C" void luxWait() { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxWait"; }
extern "C" void luxSetHaltSamplesPerPixel(int, bool, bool) { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSetHaltSamplesPerPixel"; }
extern "C" void luxSetThreadCount(unsigned int) { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSetThreadCount"; }
extern "C" unsigned int luxGetThreadCount() { return 1; }
extern "C" void luxSetEpsilon(const float, const float) { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSetEpsilon"; }

// ---------------------------------------------------------------------------
// Framebuffer
// ---------------------------------------------------------------------------
extern "C" void luxUpdateFramebuffer() { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxUpdateFramebuffer"; }
extern "C" unsigned char *luxFramebuffer() { return nullptr; }
extern "C" float *luxFloatFramebuffer() { return nullptr; }
extern "C" float *luxAlphaBuffer() { return nullptr; }

// ---------------------------------------------------------------------------
// User sampling
// ---------------------------------------------------------------------------
extern "C" void luxSetUserSamplingMap(const float *) { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSetUserSamplingMap"; }
extern "C" float *luxGetUserSamplingMap() { return nullptr; }

// ---------------------------------------------------------------------------
// Histogram
// ---------------------------------------------------------------------------
extern "C" void luxGetHistogramImage(unsigned char *, unsigned int, unsigned int, int)
{
    LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxGetHistogramImage";
}

// ---------------------------------------------------------------------------
// Parameter access
// ---------------------------------------------------------------------------
extern "C" void luxSetParameterValue(luxComponent, luxComponentParameters, double, unsigned int)
{
    LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSetParameterValue";
}
extern "C" double luxGetParameterValue(luxComponent, luxComponentParameters, unsigned int) { return 0.0; }
extern "C" double luxGetDefaultParameterValue(luxComponent, luxComponentParameters, unsigned int) { return 0.0; }
extern "C" void luxSetStringParameterValue(luxComponent, luxComponentParameters, const char *, unsigned int)
{
    LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSetStringParameterValue";
}
extern "C" unsigned int luxGetStringParameterValue(luxComponent, luxComponentParameters, char *dst, unsigned int dstlen, unsigned int)
{
    if (dst && dstlen)
        dst[0] = '\0';
    return 0;
}
extern "C" unsigned int luxGetDefaultStringParameterValue(luxComponent, luxComponentParameters, char *dst, unsigned int dstlen, unsigned int)
{
    if (dst && dstlen)
        dst[0] = '\0';
    return 0;
}

// ---------------------------------------------------------------------------
// Attribute access
// ---------------------------------------------------------------------------
extern "C" const char *luxGetAttributes() { return ""; }
extern "C" bool luxHasObject(const char *) { return false; }
extern "C" bool luxHasAttribute(const char *, const char *) { return false; }
extern "C" luxAttributeType luxGetAttributeType(const char *, const char *) { return LUX_ATTRIBUTETYPE_NONE; }
extern "C" unsigned int luxGetAttributeDescription(const char *, const char *, char *dst, unsigned int dstlen)
{
    if (dst && dstlen)
        dst[0] = '\0';
    return 0;
}
extern "C" bool luxHasAttributeDefaultValue(const char *, const char *) { return false; }
extern "C" unsigned int luxGetStringAttribute(const char *, const char *, char *dst, unsigned int dstlen)
{
    if (dst && dstlen)
        dst[0] = '\0';
    return 0;
}
extern "C" unsigned int luxGetStringAttributeDefault(const char *, const char *, char *dst, unsigned int dstlen)
{
    if (dst && dstlen)
        dst[0] = '\0';
    return 0;
}
extern "C" void luxSetStringAttribute(const char *, const char *, const char *) {}
extern "C" float luxGetFloatAttribute(const char *, const char *) { return 0.f; }
extern "C" float luxGetFloatAttributeDefault(const char *, const char *) { return 0.f; }
extern "C" void luxSetFloatAttribute(const char *, const char *, float) {}
extern "C" double luxGetDoubleAttribute(const char *, const char *) { return 0.0; }
extern "C" double luxGetDoubleAttributeDefault(const char *, const char *) { return 0.0; }
extern "C" void luxSetDoubleAttribute(const char *, const char *, double) {}
extern "C" int luxGetIntAttribute(const char *, const char *) { return 0; }
extern "C" int luxGetIntAttributeDefault(const char *, const char *) { return 0; }
extern "C" void luxSetIntAttribute(const char *, const char *, int) {}
extern "C" bool luxGetBoolAttribute(const char *, const char *) { return false; }
extern "C" bool luxGetBoolAttributeDefault(const char *, const char *) { return false; }
extern "C" void luxSetBoolAttribute(const char *, const char *, bool) {}
extern "C" void luxSetAttribute(const char *, const char *, int, void *) {}

// ---------------------------------------------------------------------------
// Statistics / misc
// ---------------------------------------------------------------------------
extern "C" double luxStatistics(const char *) { return 0.0; }
extern "C" void luxEnableDebugMode() {}
extern "C" void luxDisableRandomMode() {}

extern "C" double luxMagnitudeReduce(double number)
{
    if (number >= 1e9)
        return number / 1e9;
    if (number >= 1e6)
        return number / 1e6;
    if (number >= 1e3)
        return number / 1e3;
    return number;
}
extern "C" const char *luxMagnitudePrefix(double number)
{
    if (number >= 1e9)
        return "G";
    if (number >= 1e6)
        return "M";
    if (number >= 1e3)
        return "k";
    return "";
}
