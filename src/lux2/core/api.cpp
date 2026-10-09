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
#include "core/threadpool.h"

#include <algorithm>
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

    // Worker count for the scheduler.
    unsigned int g_threadCount = 0;

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
        // Initialize a TBB scheduler cap.
        RenderThreadPool::Get().Init(g_threadCount);

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
extern "C" void luxStart()
{
    Context2::GetActive()->Resume();
}
extern "C" void luxPause() { Context2::GetActive()->Pause(); }
extern "C" void luxExit() { Context2::GetActive()->Exit(); }
extern "C" void luxAbort() { Context2::GetActive()->Abort(); }
extern "C" void luxWait() { Context2::GetActive()->Wait(); }
extern "C" void luxSetHaltSamplesPerPixel(int, bool, bool) { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSetHaltSamplesPerPixel"; }
extern "C" void luxSetThreadCount(unsigned int n)
{
    // Effective at the next render start.
    g_threadCount = n;
    if (g_initialized)
        RenderThreadPool::Get().Init(n);
}
extern "C" unsigned int luxGetThreadCount()
{
    return RenderThreadPool::Get().Count();
}
extern "C" void luxSetEpsilon(const float, const float) { LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSetEpsilon"; }

// ---------------------------------------------------------------------------
// Framebuffer
// ---------------------------------------------------------------------------
extern "C" void luxUpdateFramebuffer() { Context2::GetActive()->UpdateFramebuffer(); }
extern "C" unsigned char *luxFramebuffer() { return Context2::GetActive()->Framebuffer(); }
extern "C" float *luxFloatFramebuffer() { return Context2::GetActive()->FloatFramebuffer(); }
extern "C" float *luxAlphaBuffer() { return Context2::GetActive()->AlphaBuffer(); }

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
namespace
{
    // The committed scene's film.
    Film *ActiveFilm()
    {
        Scene *s = Context2::GetCurrentScene();
        return (s && s->IsCommitted()) ? &s->GetFilm() : nullptr;
    }

    // Copy a film string parameter into the caller's buffer.
    unsigned int CopyFilmString(Film *film, luxComponentParameters param,
                                bool def, char *dst, unsigned int dstlen,
                                unsigned int index)
    {
        if (dst && dstlen)
            dst[0] = '\0';
        if (!film)
            return 0;
        const std::string s =
            def ? film->GetDefaultStringParameterValue(param, index)
                : film->GetStringParameterValue(param, index);
        const unsigned int n =
            std::min<unsigned int>(unsigned(s.size()), dstlen ? dstlen - 1 : 0);
        if (dst && n)
            std::memcpy(dst, s.data(), n);
        if (dst && dstlen)
            dst[n] = '\0';
        return n;
    }
} // namespace

extern "C" void luxSetParameterValue(luxComponent comp, luxComponentParameters param, double value, unsigned int index)
{
    Film *film = (comp == LUX_FILM) ? ActiveFilm() : nullptr;
    if (film)
        film->SetParameterValue(param, value, index);
    else
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSetParameterValue";
}
extern "C" double luxGetParameterValue(luxComponent comp, luxComponentParameters param, unsigned int index)
{
    Film *film = (comp == LUX_FILM) ? ActiveFilm() : nullptr;
    return film ? film->GetParameterValue(param, index) : 0.0;
}
extern "C" double luxGetDefaultParameterValue(luxComponent comp, luxComponentParameters param, unsigned int index)
{
    Film *film = (comp == LUX_FILM) ? ActiveFilm() : nullptr;
    return film ? film->GetDefaultParameterValue(param, index) : 0.0;
}
extern "C" void luxSetStringParameterValue(luxComponent comp, luxComponentParameters param, const char *value, unsigned int index)
{
    Film *film = (comp == LUX_FILM) ? ActiveFilm() : nullptr;
    if (film && value)
        film->SetStringParameterValue(param, std::string(value), index);
    else
        LOG(LUX_WARNING, LUX_UNIMPLEMENT) << LUX2_UNSUPPORTED_TAG << " luxSetStringParameterValue";
}
extern "C" unsigned int luxGetStringParameterValue(luxComponent comp, luxComponentParameters param, char *dst, unsigned int dstlen, unsigned int index)
{
    Film *film = (comp == LUX_FILM) ? ActiveFilm() : nullptr;
    return CopyFilmString(film, param, false, dst, dstlen, index);
}
extern "C" unsigned int luxGetDefaultStringParameterValue(luxComponent comp, luxComponentParameters param, char *dst, unsigned int dstlen, unsigned int index)
{
    Film *film = (comp == LUX_FILM) ? ActiveFilm() : nullptr;
    return CopyFilmString(film, param, true, dst, dstlen, index);
}

// ---------------------------------------------------------------------------
// Attribute access
// ---------------------------------------------------------------------------
namespace
{
    // Resolve an attribute through the active registry.
    const QueryableAttribute *FindAttribute(const char *objectName,
                                            const char *attributeName)
    {
        if (!objectName || !attributeName)
            return nullptr;
        Context2 *ctx = Context2::GetActive();
        if (!ctx)
            return nullptr;
        Queryable *obj = ctx->Registry()[objectName];
        if (!obj || !obj->HasAttribute(attributeName))
            return nullptr;
        return &(*obj)[attributeName];
    }

    // Copy a string into the caller buffer.
    unsigned int CopyString(const std::string &s, char *dst, unsigned int dstlen)
    {
        if (dst && dstlen)
        {
            const unsigned int n =
                static_cast<unsigned int>(std::min<size_t>(s.size(), dstlen - 1));
            std::memcpy(dst, s.data(), n);
            dst[n] = '\0';
            return n;
        }
        return 0;
    }
} // namespace

extern "C" const char *luxGetAttributes()
{
    Context2 *ctx = Context2::GetActive();
    return ctx ? ctx->Registry().GetContent() : "";
}
extern "C" bool luxHasObject(const char *objectName)
{
    Context2 *ctx = Context2::GetActive();
    return ctx && objectName && ctx->Registry().Has(objectName);
}
extern "C" bool luxHasAttribute(const char *objectName, const char *attributeName)
{
    return FindAttribute(objectName, attributeName) != nullptr;
}
extern "C" luxAttributeType luxGetAttributeType(const char *objectName,
                                                const char *attributeName)
{
    const QueryableAttribute *a = FindAttribute(objectName, attributeName);
    return a ? static_cast<luxAttributeType>(a->Type()) : LUX_ATTRIBUTETYPE_NONE;
}
extern "C" unsigned int luxGetAttributeDescription(const char *objectName,
                                                   const char *attributeName,
                                                   char *dst, unsigned int dstlen)
{
    const QueryableAttribute *a = FindAttribute(objectName, attributeName);
    return CopyString(a ? a->Description() : std::string(), dst, dstlen);
}
extern "C" bool luxHasAttributeDefaultValue(const char *objectName,
                                            const char *attributeName)
{
    const QueryableAttribute *a = FindAttribute(objectName, attributeName);
    return a && a->HasDefaultValue();
}
extern "C" unsigned int luxGetStringAttribute(const char *objectName,
                                              const char *attributeName,
                                              char *dst, unsigned int dstlen)
{
    const QueryableAttribute *a = FindAttribute(objectName, attributeName);
    return CopyString(a ? a->StringValue() : std::string(), dst, dstlen);
}
extern "C" unsigned int luxGetStringAttributeDefault(const char *objectName,
                                                     const char *attributeName,
                                                     char *dst, unsigned int dstlen)
{
    const QueryableAttribute *a = FindAttribute(objectName, attributeName);
    return CopyString(a ? a->DefaultValue() : std::string(), dst, dstlen);
}
extern "C" void luxSetStringAttribute(const char *objectName,
                                      const char *attributeName, const char *value)
{
    Context2 *ctx = Context2::GetActive();
    if (!ctx || !objectName || !attributeName || !value)
        return;
    Queryable *obj = ctx->Registry()[objectName];
    if (obj && obj->HasAttribute(attributeName))
    {
        try
        {
            (*obj)[attributeName].Set(std::string(value));
        }
        catch (const std::exception &e)
        {
            LOG(LUX_ERROR, LUX_CONSISTENCY) << "luxSetStringAttribute: " << e.what();
        }
    }
}
extern "C" float luxGetFloatAttribute(const char *objectName, const char *attributeName)
{
    const QueryableAttribute *a = FindAttribute(objectName, attributeName);
    return a ? a->FloatValue() : 0.f;
}
extern "C" float luxGetFloatAttributeDefault(const char *, const char *) { return 0.f; }
extern "C" void luxSetFloatAttribute(const char *objectName, const char *attributeName,
                                     float value)
{
    Context2 *ctx = Context2::GetActive();
    if (!ctx || !objectName || !attributeName)
        return;
    Queryable *obj = ctx->Registry()[objectName];
    if (obj && obj->HasAttribute(attributeName))
    {
        try
        {
            (*obj)[attributeName].Set(value);
        }
        catch (const std::exception &e)
        {
            LOG(LUX_ERROR, LUX_CONSISTENCY) << "luxSetFloatAttribute: " << e.what();
        }
    }
}
extern "C" double luxGetDoubleAttribute(const char *objectName, const char *attributeName)
{
    const QueryableAttribute *a = FindAttribute(objectName, attributeName);
    return a ? a->DoubleValue() : 0.0;
}
extern "C" double luxGetDoubleAttributeDefault(const char *, const char *) { return 0.0; }
extern "C" void luxSetDoubleAttribute(const char *objectName, const char *attributeName,
                                      double value)
{
    Context2 *ctx = Context2::GetActive();
    if (!ctx || !objectName || !attributeName)
        return;
    Queryable *obj = ctx->Registry()[objectName];
    if (obj && obj->HasAttribute(attributeName))
    {
        try
        {
            (*obj)[attributeName].Set(value);
        }
        catch (const std::exception &e)
        {
            LOG(LUX_ERROR, LUX_CONSISTENCY) << "luxSetDoubleAttribute: " << e.what();
        }
    }
}
extern "C" int luxGetIntAttribute(const char *objectName, const char *attributeName)
{
    const QueryableAttribute *a = FindAttribute(objectName, attributeName);
    return a ? a->IntValue() : 0;
}
extern "C" int luxGetIntAttributeDefault(const char *, const char *) { return 0; }
extern "C" void luxSetIntAttribute(const char *objectName, const char *attributeName,
                                   int value)
{
    Context2 *ctx = Context2::GetActive();
    if (!ctx || !objectName || !attributeName)
        return;
    Queryable *obj = ctx->Registry()[objectName];
    if (obj && obj->HasAttribute(attributeName))
    {
        try
        {
            (*obj)[attributeName].Set(value);
        }
        catch (const std::exception &e)
        {
            LOG(LUX_ERROR, LUX_CONSISTENCY) << "luxSetIntAttribute: " << e.what();
        }
    }
}
extern "C" bool luxGetBoolAttribute(const char *objectName, const char *attributeName)
{
    const QueryableAttribute *a = FindAttribute(objectName, attributeName);
    return a ? a->BoolValue() : false;
}
extern "C" bool luxGetBoolAttributeDefault(const char *, const char *) { return false; }
extern "C" void luxSetBoolAttribute(const char *objectName, const char *attributeName,
                                    bool value)
{
    Context2 *ctx = Context2::GetActive();
    if (!ctx || !objectName || !attributeName)
        return;
    Queryable *obj = ctx->Registry()[objectName];
    if (obj && obj->HasAttribute(attributeName))
    {
        try
        {
            (*obj)[attributeName].Set(value);
        }
        catch (const std::exception &e)
        {
            LOG(LUX_ERROR, LUX_CONSISTENCY) << "luxSetBoolAttribute: " << e.what();
        }
    }
}
extern "C" void luxSetAttribute(const char *, const char *, int, void *) {}

// ---------------------------------------------------------------------------
// Statistics / misc
// ---------------------------------------------------------------------------
extern "C" double luxStatistics(const char *statName)
{
    Context2 *ctx = Context2::GetActive();
    if (!ctx || !statName)
        return 0.0;
    const std::string name(statName);
    // Context level state queries.
    if (name == "sceneIsReady")
        return ctx->IsCommitted() ? 1.0 : 0.0;
    if (name == "terminated")
        return ctx->IsAborted() ? 1.0 : 0.0;
    RenderStatistics *stats = ctx->Stats();
    if (!stats)
        return 0.0;
    if (name == "secElapsed")
        return stats->ElapsedTime();
    if (name == "percentComplete")
        return stats->PercentComplete();
    if (name == "samplesPerPixel")
        return stats->SamplesPerPixel();
    if (name == "samplesPerSecond")
        return stats->SamplesPerSecond();
    return 0.0;
}
extern "C" void luxEnableDebugMode()
{
    if (Context2 *c = Context2::GetActive())
        c->EnableDebugMode();
}
extern "C" void luxDisableRandomMode()
{
    if (Context2 *c = Context2::GetActive())
        c->DisableRandomMode();
}

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
