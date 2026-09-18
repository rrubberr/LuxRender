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

#include "core/paramset.h"

#include <cctype>
#include <cstring>
#include <sstream>

namespace lux2 {

// ---------------------------------------------------------------------------
// LookupType
// ---------------------------------------------------------------------------
bool LookupType(const char *token, ParamType *type, std::string &name) {
    *type = PARAM_TYPE_INT;  // dummy init
    const char *strp = token;
    while (*strp && std::isspace(static_cast<unsigned char>(*strp)))
        ++strp;
    if (!*strp) {
        LOG(LUX_ERROR, LUX_SYNTAX) << "Parameter '" << token << "' doesn't have a type declaration?!";
        name = token;
        return false;
    }

#define TRY_DECODING_TYPE(keyword, mask) \
    if (strncmp(keyword, strp, strlen(keyword)) == 0) { \
        *type = mask; strp += strlen(keyword); \
    }
    TRY_DECODING_TYPE("float", PARAM_TYPE_FLOAT)
    else TRY_DECODING_TYPE("integer", PARAM_TYPE_INT)
    else TRY_DECODING_TYPE("bool", PARAM_TYPE_BOOL)
    else TRY_DECODING_TYPE("point", PARAM_TYPE_POINT)
    else TRY_DECODING_TYPE("vector", PARAM_TYPE_VECTOR)
    else TRY_DECODING_TYPE("normal", PARAM_TYPE_NORMAL)
    else TRY_DECODING_TYPE("string", PARAM_TYPE_STRING)
    else TRY_DECODING_TYPE("texture", PARAM_TYPE_TEXTURE)
    else TRY_DECODING_TYPE("color", PARAM_TYPE_COLOR)
    else {
        LOG(LUX_ERROR, LUX_SYNTAX) << "Unable to decode type for token '" << token << "'";
        name = token;
        return false;
    }
#undef TRY_DECODING_TYPE

    while (*strp && std::isspace(static_cast<unsigned char>(*strp)))
        ++strp;
    name = strp;
    return true;
}

// ---------------------------------------------------------------------------
// Helpers to avoid duplication
// ---------------------------------------------------------------------------
template <class T>
static bool EraseItem(std::vector<ParamSetItem<T> *> &v, const std::string &name) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i]->name == name) {
            delete v[i];
            v.erase(v.begin() + i);
            return true;
        }
    return false;
}

template <class T>
static ParamSetItem<T> *FindItem(std::vector<ParamSetItem<T> *> &v, const std::string &name) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i]->name == name)
            return v[i];
    return nullptr;
}

template <class T>
static const ParamSetItem<T> *FindItemConst(const std::vector<ParamSetItem<T> *> &v,
                                             const std::string &name) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i]->name == name)
            return v[i];
    return nullptr;
}

// ---------------------------------------------------------------------------
// ParamSet methods
// ---------------------------------------------------------------------------

// Add methods.
void ParamSet::AddFloat(const std::string &name, const float *v, std::uint32_t n) {
    floats.push_back(new ParamSetItem<float>(name, v, n));
}
void ParamSet::AddInt(const std::string &name, const int *v, std::uint32_t n) {
    ints.push_back(new ParamSetItem<int>(name, v, n));
}
void ParamSet::AddBool(const std::string &name, const bool *v, std::uint32_t n) {
    bools.push_back(new ParamSetItem<bool>(name, v, n));
}
void ParamSet::AddPoint(const std::string &name, const Point3f *v, std::uint32_t n) {
    points.push_back(new ParamSetItem<Point3f>(name, v, n));
}
void ParamSet::AddVector(const std::string &name, const Vector3f *v, std::uint32_t n) {
    vectors.push_back(new ParamSetItem<Vector3f>(name, v, n));
}
void ParamSet::AddNormal(const std::string &name, const Normal3f *v, std::uint32_t n) {
    normals.push_back(new ParamSetItem<Normal3f>(name, v, n));
}
void ParamSet::AddRGBColor(const std::string &name, const RGBColor *v, std::uint32_t n) {
    spectra.push_back(new ParamSetItem<RGBColor>(name, v, n));
}
void ParamSet::AddString(const std::string &name, const std::string *v, std::uint32_t n) {
    strings.push_back(new ParamSetItem<std::string>(name, v, n));
}
void ParamSet::AddTexture(const std::string &name, const std::string &value) {
    textures.push_back(new ParamSetItem<std::string>(name, &value, 1));
}

ParamSet::ParamSet(std::uint32_t n, const char * pluginName, const char * const tokens[], const char * const params[])
{
    //TODO - jromang : implement this using a std::map or std::string hashing

    // NOTE - radiance - THIS NEEDS TO BE UPDATED! :)

    std::string pn(pluginName);

    for(std::uint32_t i = 0; i < n; ++i)
    {
        ParamType type;
        std::string s;
        bool typed = LookupType(tokens[i], &type, s);
        if (typed) {
            switch (type) {
            case PARAM_TYPE_INT: {
                std::uint32_t np = 1;
                if (s == "indices")
                    np = FindOneInt("ntris", 1);  // [add this special 'ntris' parameter when using the API]
                else if (s == "quadindices")
                    np = FindOneInt("nquads", 1);  // [add this special 'nquads' parameter when using the API]
                else if (s == "triindices")
                    np = FindOneInt("ntris", 1);  // [add this special 'ntris' parameter when using the API]
                AddInt(s, (int*)(params[i]), np);
                break;
            }
            case PARAM_TYPE_BOOL: {
                std::uint32_t np = 1;
                AddBool(s, (bool*)(params[i]), np);
                break;
            }
            case PARAM_TYPE_FLOAT: {
                std::uint32_t np = 1;
                if (s == "Pw")
                    np = 4 * FindOneInt("nu", 1) *
                        FindOneInt("nv", 1);
                else if (s == "Pz")
                    np = FindOneInt("nu", 1) *
                        FindOneInt("nv", 1);
                else if (s == "data" || s == "wavelengths")
                    np = FindOneInt("nvalues", 1);  // [add this special 'nvalues' parameter when using the API]
                else if (s == "density")
                    np = FindOneInt("nx", 1) *
                        FindOneInt("ny", 1) *
                        FindOneInt("nz", 1);
                else if (s == "screenwindow")
                    np = 4;
                else if (s == "st" || s == "uv")
                    np = 2 * FindOneInt("nvertices", 1); // [add this special 'nvertices' parameter when using the API]
                else if (s == "uknots")
                    np = FindOneInt("nu", 1) +
                        FindOneInt("uorder", 1);
                else if (s == "vknots")
                    np = FindOneInt("nv", 1) +
                        FindOneInt("vorder", 1);
                else if (s == "offsets")
                    np = FindOneInt("noffsets", 1);
                else if (s == "weights")
                    np = FindOneInt("nweights", 1);

                AddFloat(s, (float*)(params[i]), np);
                break;
            }
            case PARAM_TYPE_POINT: {
                std::uint32_t np = 1;
                if (s == "P") {
                    if (pn == "nurbs")
                        np = FindOneInt("nu", 1) *
                            FindOneInt("nv", 1);
                    else
                        np = FindOneInt("nvertices", 1);  // [add this special 'nvertices' parameter when using the API]
                }
                AddPoint(s, (Point3f*)(params[i]), np);
                break;
            }
            case PARAM_TYPE_VECTOR: {
                std::uint32_t np = 1;
                AddVector(s, (Vector3f*)(params[i]), np);
                break;
            }
            case PARAM_TYPE_NORMAL: {
                std::uint32_t np = 1;
                if (s == "N")
                    np = FindOneInt("nvertices", 1);  // [add this special 'nvertices' parameter when using the API]
                AddNormal(s, (Normal3f*)(params[i]), np);
                break;
            }
            case PARAM_TYPE_COLOR: {
                { const RGBColor c((const float*)(params[i])); AddRGBColor(s, &c, 1); }
                break;
            }
            case PARAM_TYPE_STRING: {
                // Special code for handling SLGRenderer config parameter
                if (s == "config" && pn == "slg") {
                    const char *first = params[i];
                    std::vector<std::string> opts;
                    for (;;) {
                        // Look for the first quote
                        while (*first != '\"' && *first != '\0')
                            ++first;
                        if (*first == '\0')
                            break;

                        // Look for the second quote
                        const char *second = first + 1;
                        while (*second != '\"' && *second != '\0')
                            ++second;
                        if (*second == '\0')
                            break;

                        const std::string opt(first + 1, second - first - 1);
                        opts.push_back(opt);

                        first = second + 1;
                    }

                    AddString(s, &opts[0], opts.size());
                } else
                    { const std::string v((char*)(params[i])); AddString(s, &v, 1); }
                break;
            }
            case PARAM_TYPE_TEXTURE: {
                AddTexture(s, std::string((char*)(params[i])));
                break;
            }
            default:
                break;
            }
            continue;
        }
        //float parameters
        if (s == "B")
            AddFloat(s,(float*)(params[i]));
        if (s == "C")
            AddFloat(s, (float*)(params[i]));
        if (s == "Pw")
            AddFloat(s, (float*)(params[i]),
                4 * FindOneInt("nu", i) * FindOneInt("nv", i));
        if (s == "Pz")
            AddFloat(s, (float*)(params[i]),
                FindOneInt("nu", i) * FindOneInt("nv", i));
        if (s == "a")
            AddFloat(s, (float*)(params[i]));
        if (s == "aconst")
            AddFloat(s, (float*)(params[i]));
        if (s == "alpha")
            AddFloat(s, (float*)(params[i]));
        if (s == "aperture")
            AddFloat(s, (float*)(params[i]));
        if (s == "aperture_diameter")
            AddFloat(s, (float*)(params[i]));
        if (s == "b")
            AddFloat(s, (float*)(params[i]));
        if (s == "baseflatness")
            AddFloat(s, (float*)(params[i]));
        if (s == "bconst")
            AddFloat(s, (float*)(params[i]));
        if (s == "brickdepth")
            AddFloat(s, (float*)(params[i]));
        if (s == "brickheight")
            AddFloat(s, (float*)(params[i]));
        if (s == "brickwidth")
            AddFloat(s, (float*)(params[i]));
        if (s == "bright")
            AddFloat(s, (float*)(params[i]));
        if (s == "bumpmapsampledistance")
            AddFloat(s, (float*)(params[i]));
        if (s == "burn")
            AddFloat(s, (float*)(params[i]));
        if (s == "cconst")
            AddFloat(s, (float*)(params[i]));
        if (s == "compo_override_alpha_value")
            AddFloat(s, (float*)(params[i]));
        if (s == "coneangle")
            AddFloat(s, (float*)(params[i]));
        if (s == "conedeltaangle")
            AddFloat(s, (float*)(params[i]));
        if (s == "contrast")
            AddFloat(s, (float*)(params[i]));
        if (s == "contrast_ywa")
            AddFloat(s, (float*)(params[i]));
        if (s == "cropwindow")
            AddFloat(s, (float*)(params[i]), 4);
/*      if (s == "data")
            AddFloat(s, (float*)(params[i]));*/ //FIXME - there's currently no way of getting the array length for regular and irregular spectrum data
        if (s == "dconst")
            AddFloat(s, (float*)(params[i]));
        if (s == "density") AddFloat(s, (float*)(params[i]), FindOneInt("nx", i) * FindOneInt("ny", i) * FindOneInt("nz", i));
        if (s == "diffusereflectreject_threshold")
            AddFloat(s, (float*)(params[i]));
        if (s == "diffuserefractreject_threshold")
            AddFloat(s, (float*)(params[i]));
        if (s == "distamount")
            AddFloat(s, (float*)(params[i]));
        if (s == "distancethreshold")
            AddFloat(s, (float*)(params[i]));
        if (s == "dmoffset")
            AddFloat(s, (float*)(params[i]));
        if (s == "dmscale")
            AddFloat(s, (float*)(params[i]));
        if (s == "econst")
            AddFloat(s, (float*)(params[i]));
        if (s == "efficacy")
            AddFloat(s, (float*)(params[i]));
        if (s == "emptybonus")
            AddFloat(s, (float*)(params[i]));
        if (s == "end")
            AddFloat(s, (float*)(params[i]));
        if (s == "energy")
            AddFloat(s, (float*)(params[i]));
        if (s == "exposure")
            AddFloat(s, (float*)(params[i]));
        if (s == "eyerrthreshold")
            AddFloat(s, (float*)(params[i]));
        if (s == "filmdiag")
            AddFloat(s, (float*)(params[i]));
        if (s == "filmdistance")
            AddFloat(s, (float*)(params[i]));
        if (s == "filterquality")
            AddFloat(s, (float*)(params[i]));
        if (s == "focaldistance")
            AddFloat(s, (float*)(params[i]));
        if (s == "fov")
            AddFloat(s, (float*)(params[i]));
        if (s == "frameaspectratio")
            AddFloat(s, (float*)(params[i]));
        if (s == "freq")
            AddFloat(s, (float*)(params[i]));
        if (s == "fstop")
            AddFloat(s, (float*)(params[i]));
        if (s == "fullsweepthreshold")
            AddFloat(s, (float*)(params[i]));
        if (s == "g")
            AddFloat(s, (float*)(params[i]));
        if (s == "gain")
            AddFloat(s, (float*)(params[i]));
        if (s == "gamma")
            AddFloat(s, (float*)(params[i]));
        if (s == "gatherangle")
            AddFloat(s, (float*)(params[i]));
        if (s == "glossyreflectreject_threshold")
            AddFloat(s, (float*)(params[i]));
        if (s == "glossyrefractreject_threshold")
            AddFloat(s, (float*)(params[i]));
        if (s == "h")
            AddFloat(s, (float*)(params[i]));
        if (s == "height")
            AddFloat(s, (float*)(params[i]));
        if (s == "hither")
            AddFloat(s, (float*)(params[i]));
        if (s == "innerradius")
            AddFloat(s, (float*)(params[i]));
        if (s == "lacu")
            AddFloat(s, (float*)(params[i]));
        if (s == "largemutationprob")
            AddFloat(s, (float*)(params[i]));
        if (s == "lensradius")
            AddFloat(s, (float*)(params[i]));
        if (s == "lightrrthreshold")
            AddFloat(s, (float*)(params[i]));
        if (s == "linear_exposure")
            AddFloat(s, (float*)(params[i]));
        if (s == "linear_fstop")
            AddFloat(s, (float*)(params[i]));
        if (s == "linear_gamma")
            AddFloat(s, (float*)(params[i]));
        if (s == "linear_sensitivity")
            AddFloat(s, (float*)(params[i]));
        if (s == "majorradius")
            AddFloat(s, (float*)(params[i]));
        if (s == "maxY")
            AddFloat(s, (float*)(params[i]));
        if (s == "maxanisotropy")
            AddFloat(s, (float*)(params[i]));
        if (s == "maxphotondist")
            AddFloat(s, (float*)(params[i]));
        if (s == "micromutationprob")
            AddFloat(s, (float*)(params[i]));
        if (s == "mindist")
            AddFloat(s, (float*)(params[i]));
        if (s == "minkovsky_exp")
            AddFloat(s, (float*)(params[i]));
        if (s == "minorradius")
            AddFloat(s, (float*)(params[i]));
        if (s == "mortarsize")
            AddFloat(s, (float*)(params[i]));
        if (s == "mutationrange")
            AddFloat(s, (float*)(params[i]));
        if (s == "nabla")
            AddFloat(s, (float*)(params[i]));
        if (s == "noiseoffset")
            AddFloat(s, (float*)(params[i]));
        if (s == "noisescale")
            AddFloat(s, (float*)(params[i]));
        if (s == "noisesize")
            AddFloat(s, (float*)(params[i]));
        if (s == "octs")
            AddFloat(s, (float*)(params[i]));
        if (s == "offset")
            AddFloat(s, (float*)(params[i]));
        if (s == "omega")
            AddFloat(s, (float*)(params[i]));
        if (s == "outscale")
            AddFloat(s, (float*)(params[i]));
        if (s == "phimax")
            AddFloat(s, (float*)(params[i]));
        if (s == "photonpowerclamp")
            AddFloat(s, (float*)(params[i]));
        if (s == "photonemissionclamp")
            AddFloat(s, (float*)(params[i]));
        if (s == "postscale")
            AddFloat(s, (float*)(params[i]));
        if (s == "power" && pn == "area")
            AddFloat(s, (float*)(params[i]));
        if (s == "prescale")
            AddFloat(s, (float*)(params[i]));
        if (s == "radius")
            AddFloat(s, (float*)(params[i]));
        if (s == "reinhard_burn")
            AddFloat(s, (float*)(params[i]));
        if (s == "reinhard_postscale")
            AddFloat(s, (float*)(params[i]));
        if (s == "reinhard_prescale")
            AddFloat(s, (float*)(params[i]));
        if (s == "relsize")
            AddFloat(s, (float*)(params[i]));
        if (s == "roughness")
            AddFloat(s, (float*)(params[i]));
        if (s == "rrcontinueprob")
            AddFloat(s, (float*)(params[i]));
        if (s == "scale")
            AddFloat(s, (float*)(params[i]));
        if (s == "screenwindow")
            AddFloat(s, (float*)(params[i]), 4);
        if (s == "sensitivity")
            AddFloat(s, (float*)(params[i]));
        if (s == "sharpness")
            AddFloat(s, (float*)(params[i]));
        if (s == "shutterclose")
            AddFloat(s, (float*)(params[i]));
        if (s == "shutteropen")
            AddFloat(s, (float*)(params[i]));
        if (s == "spheresize")
            AddFloat(s, (float*)(params[i]));
        if (s == "st")
            AddFloat(s, (float*)(params[i]),
                2 * FindOneInt("nvertices", i));  // jromang - p.926 find 'n' ? - [a ajouter dans le vecteur lors de l'appel de la fonction, avec un parametre 'nvertices supplementaire dans l API]
        if (s == "start")
            AddFloat(s, (float*)(params[i]));
        if (s == "stepsize")
            AddFloat(s, (float*)(params[i]));
        if (s == "tau")
            AddFloat(s, (float*)(params[i]));
        if (s == "temperature")
            AddFloat(s, (float*)(params[i]));
        if (s == "thetamax")
            AddFloat(s, (float*)(params[i]));
        if (s == "thetamin")
            AddFloat(s, (float*)(params[i]));
        if (s == "turbidity")
            AddFloat(s, (float*)(params[i]));
        if (s == "turbulance")
            AddFloat(s, (float*)(params[i]));
        if (s == "turbulence")
            AddFloat(s, (float*)(params[i]));
        if (s == "u0")
            AddFloat(s, (float*)(params[i]));
        if (s == "u1")
            AddFloat(s, (float*)(params[i]));
        if (s == "udelta")
            AddFloat(s, (float*)(params[i]));
        if (s == "uknots")
            AddFloat(s, (float*)(params[i]),
                FindOneInt("nu", i) + FindOneInt("uorder", i));
        if (s == "uscale")
            AddFloat(s, (float*)(params[i]));
        if (s == "uv")
            AddFloat(s, (float*)(params[i]),
                2 * FindOneInt("nvertices", i));  // jromang - p.926 find 'n' ? - [a ajouter dans le vecteur lors de l'appel de la fonction, avec un parametre 'nvertices supplementaire dans l API]
        if (s == "v0")
            AddFloat(s, (float*)(params[i]));
        if (s == "v00")
            AddFloat(s, (float*)(params[i]));
        if (s == "v01")
            AddFloat(s, (float*)(params[i]));
        if (s == "v1")
            AddFloat(s, (float*)(params[i]));
        if (s == "v10")
            AddFloat(s, (float*)(params[i]));
        if (s == "v11")
            AddFloat(s, (float*)(params[i]));
        if (s == "value")
            AddFloat(s, (float*)(params[i]));
        if (s == "variability")
            AddFloat(s, (float*)(params[i]));
        if (s == "variation")
            AddFloat(s, (float*)(params[i]));
        if (s == "vdelta")
            AddFloat(s,(float*)(params[i]));
        if (s == "vknots")
            AddFloat(s, (float*)(params[i]),
                FindOneInt("nv", i) + FindOneInt("vorder", i));
        if (s == "vscale")
            AddFloat(s, (float*)(params[i]));
        if (s == "w1")
            AddFloat(s, (float*)(params[i]));
        if (s == "w2")
            AddFloat(s, (float*)(params[i]));
        if (s == "w3")
            AddFloat(s, (float*)(params[i]));
        if (s == "w4")
            AddFloat(s, (float*)(params[i]));
        if (s == "wavelength")
            AddFloat(s, (float*)(params[i]));
/*      if (s == "wavelengths")
            AddFloat(s, (float*)(params[i]));*/ //FIXME - there's currently no way of getting the array length for irregular spectrum data
        if (s == "width")
            AddFloat(s, (float*)(params[i]));
        if (s == "xwidth")
            AddFloat(s, (float*)(params[i]));
        if (s == "yon")
            AddFloat(s, (float*)(params[i]));
        if (s == "ywa")
            AddFloat(s, (float*)(params[i]));
        if (s == "ywidth")
            AddFloat(s, (float*)(params[i]));
        if (s == "zmax")
            AddFloat(s, (float*)(params[i]));
        if (s == "zmin")
            AddFloat(s, (float*)(params[i]));

        //int parameters
        if (s == "blades")
            AddInt(s, (int*)(params[i]));
        if (s == "causticphotons")
            AddInt(s, (int*)(params[i]));
        if (s == "chainlength")
            AddInt(s, (int*)(params[i]));
        if (s == "coltype")
            AddInt(s, (int*)(params[i]));
        if (s == "diffusereflectdepth")
            AddInt(s, (int*)(params[i]));
        if (s == "diffusereflectsamples")
            AddInt(s, (int*)(params[i]));
        if (s == "diffuserefractdepth")
            AddInt(s, (int*)(params[i]));
        if (s == "diffuserefractsamples")
            AddInt(s, (int*)(params[i]));
        if (s == "dimension")
            AddInt(s, (int*)(params[i]));
        if (s == "directphotons")
            AddInt(s, (int*)(params[i]));
        if (s == "directsamples")
            AddInt(s, (int*)(params[i]));
        if (s == "discardmipmaps")
            AddInt(s, (int*)(params[i]));
        if (s == "displayinterval")
            AddInt(s, (int*)(params[i]));
        if (s == "eyedepth")
            AddInt(s, (int*)(params[i]));
        if (s == "finalgathersamples")
            AddInt(s, (int*)(params[i]));
        if (s == "flmwriteinterval")
            AddInt(s, (int*)(params[i]));
        if (s == "glossyreflectdepth")
            AddInt(s, (int*)(params[i]));
        if (s == "glossyreflectsamples")
            AddInt(s, (int*)(params[i]));
        if (s == "glossyrefractdepth")
            AddInt(s, (int*)(params[i]));
        if (s == "glossyrefractsamples")
            AddInt(s, (int*)(params[i]));
        if (s == "haltspp")
            AddInt(s, (int*)(params[i]));
        if( s == "indices")
            AddInt(s, (int*)(params[i]),
                FindOneInt("nvertices", i));  // jromang - p.926 find 'n' ? - [a ajouter dans le vecteur lors de l'appel de la fonction, avec un parametre 'nvertices supplementaire dans l API]
        if (s == "indirectphotons")
            AddInt(s, (int*)(params[i]));
        if (s == "indirectsamples")
            AddInt(s, (int*)(params[i]));
        if (s == "intersectcost")
            AddInt(s, (int*)(params[i]));
        if (s == "lightdepth")
            AddInt(s, (int*)(params[i]));
        if (s == "maxconsecrejects")
            AddInt(s, (int*)(params[i]));
        if (s == "maxdepth")
            AddInt(s, (int*)(params[i]));
        if (s == "maxphotondepth")
            AddInt(s, (int*)(params[i]));
        if (s == "maxprims")
            AddInt(s, (int*)(params[i]));
        if (s == "maxprimsperleaf")
            AddInt(s, (int*)(params[i]));
        if (s == "nlevels")
            AddInt(s, (int*)(params[i]));
        if (s == "nlights")
            AddInt(s, (int*)(params[i]));
        if (s == "noisedepth")
            AddInt(s, (int*)(params[i]));
        if (s == "nphotonused")
            AddInt(s, (int*)(params[i]));
        if (s == "nsamples")
            AddInt(s, (int*)(params[i]));
        if (s == "nsets")
            AddInt(s, (int*)(params[i]));
        if (s == "nsubdivlevels")
            AddInt(s, (int*)(params[i]));
        if (s == "nu")
            AddInt(s, (int*)(params[i]));
        if (s == "nv")
            AddInt(s, (int*)(params[i]));
        if (s == "nx")
            AddInt(s, (int*)(params[i]));
        if (s == "ny")
            AddInt(s, (int*)(params[i]));
        if (s == "nz")
            AddInt(s, (int*)(params[i]));
        if (s == "octaves")
            AddInt(s, (int*)(params[i]));
        if (s == "outlierrejection_k")
            AddInt(s, (int*)(params[i]));
        if (s == "variancerejection_k")
            AddInt(s, (int*)(params[i]));
        if (s == "variancerejectionwarmup")
            AddInt(s, (int*)(params[i]));
        if (s == "pixelsamples")
            AddInt(s, (int*)(params[i]));
        if (s == "power" && pn == "perspective")
            AddInt(s, (int*)(params[i]));
/*      if (s == "quadindices")
            AddInt(s, (int*)(params[i]));*/ //FIXME - there's no way of knowinf the number of elements
        if (s == "radiancephotons")
            AddInt(s, (int*)(params[i]));
        if (s == "reject_warmup")
            AddInt(s, (int*)(params[i]));
        if (s == "skipfactor")
            AddInt(s, (int*)(params[i]));
        if (s == "specularreflectdepth")
            AddInt(s, (int*)(params[i]));
        if (s == "specularrefractdepth")
            AddInt(s, (int*)(params[i]));
        if (s == "spheres")
            AddInt(s, (int*)(params[i]));
        if (s == "traversalcost")
            AddInt(s, (int*)(params[i]));
        if (s == "treetype")
            AddInt(s, (int*)(params[i]));
/*      if( s == "triindices")
            AddInt(s, (int*)(params[i]));*/ //FIXME - there's no way of knowing the number of elements
        if (s == "uorder")
            AddInt(s, (int*)(params[i]));
        if (s == "vorder")
            AddInt(s, (int*)(params[i]));
        if (s == "writeinterval")
            AddInt(s, (int*)(params[i]));
        if (s == "xresolution")
            AddInt(s, (int*)(params[i]));
        if (s == "xsamples")
            AddInt(s, (int*)(params[i]));
        if (s == "yresolution")
            AddInt(s, (int*)(params[i]));
        if (s == "ysamples")
            AddInt(s, (int*)(params[i]));

        //bool parameters
        if (s == "architectural")
            AddBool(s, (bool*)(params[i]));
        if (s == "autofocus")
            AddBool(s, (bool*)(params[i]));
        if (s == "compo_override_alpha")
            AddBool(s, (bool*)(params[i]));
        if (s == "compo_use_key")
            AddBool(s, (bool*)(params[i]));
        if (s == "compo_visible_emission")
            AddBool(s, (bool*)(params[i]));
        if (s == "compo_visible_indirect_emission")
            AddBool(s, (bool*)(params[i]));
        if (s == "compo_visible_indirect_material")
            AddBool(s, (bool*)(params[i]));
        if (s == "compo_visible_material")
            AddBool(s, (bool*)(params[i]));
        if (s == "dbg_enabledirect")
            AddBool(s, (bool*)(params[i]));
        if (s == "dbg_enableindircaustic")
            AddBool(s, (bool*)(params[i]));
        if (s == "dbg_enableindirdiffuse")
            AddBool(s, (bool*)(params[i]));
        if (s == "dbg_enableindirspecular")
            AddBool(s, (bool*)(params[i]));
        if (s == "dbg_enableradiancemap")
            AddBool(s, (bool*)(params[i]));
        if (s == "debug")
            AddBool(s, (bool*)(params[i]));
        if (s == "diffusereflectreject")
            AddBool(s, (bool*)(params[i]));
        if (s == "diffuserefractreject")
            AddBool(s, (bool*)(params[i]));
        if (s == "directdiffuse")
            AddBool(s, (bool*)(params[i]));
        if (s == "directglossy")
            AddBool(s, (bool*)(params[i]));
        if (s == "directsampleall")
            AddBool(s, (bool*)(params[i]));
        if (s == "dmnormalsmooth")
            AddBool(s, (bool*)(params[i]));
        if (s == "dmsharpboundary")
            AddBool(s, (bool*)(params[i]));
        if (s == "finalgather")
            AddBool(s, (bool*)(params[i]));
        if (s == "flipxy")
            AddBool(s, (bool*)(params[i]));
        if (s == "flipz")
            AddBool(s, (bool*)(params[i]));
        if (s == "glossyreflectreject")
            AddBool(s, (bool*)(params[i]));
        if (s == "glossyrefractreject")
            AddBool(s, (bool*)(params[i]));
        if (s == "includeenvironment")
            AddBool(s, (bool*)(params[i]));
        if (s == "indirectdiffuse")
            AddBool(s, (bool*)(params[i]));
        if (s == "indirectglossy")
            AddBool(s, (bool*)(params[i]));
        if (s == "indirectsampleall")
            AddBool(s, (bool*)(params[i]));
        if (s == "premultiplyalpha")
            AddBool(s, (bool*)(params[i]));
        if (s == "refineimmediately")
            AddBool(s, (bool*)(params[i]));
        if (s == "restart_resume_flm")
            AddBool(s, (bool*)(params[i]));
        if (s == "smooth")
            AddBool(s, (bool*)(params[i]));
        if (s == "usevariance")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_exr")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_exr_ZBuf")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_exr_applyimaging")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_exr_gamutclamp")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_exr_halftype")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_exr_straightcolors")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_png")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_png_16bit")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_png_ZBuf")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_png_gamutclamp")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_resume_flm")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_tga")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_tga_ZBuf")
            AddBool(s, (bool*)(params[i]));
        if (s == "write_tga_gamutclamp")
            AddBool(s, (bool*)(params[i]));
        if (s == "recenter_mesh")
            AddBool(s, (bool*)(params[i]));     

        //std::string parameters
        if (s == "aamode")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "acceltype")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "basesampler")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "cameraresponse")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "configfile")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "displacementmap")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "distmetric")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "distribution")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "endtransform")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "filename")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "filtertype")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "glarelashesfilename")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "glarepupilfilename")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "iesname")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "ldr_clamp_method")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "mapname")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "mapping")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "name")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "namedmaterial1")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "namedmaterial2")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "noisebasis")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "noisebasis2")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "noisetype")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "photonmapsfile")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "pixelsampler")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "quadtype")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "renderingmode")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "rrstrategy")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "scheme")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "shutterdistribution")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "specfile")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "strategy")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "subdivscheme")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "tonemapkernel")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "tritype")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "type")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "wrap")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "write_exr_channels")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "write_exr_compressiontype")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "write_exr_zbuf_normalization")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "write_png_channels")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "write_pxr_zbuf_normalization")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "write_tga_channels")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "write_tga_zbuf_normalization")
            { const std::string v(params[i]); AddString(s, &v, 1); }
        if (s == "coordinates")
            { const std::string v(params[i]); AddString(s, &v, 1); }

        //point parameters
        if (s == "P") {
            if (pn == "nurbs")
                AddPoint(s, (Point3f*)(params[i]),
                    FindOneInt("nu", i) * FindOneInt("nv", i));
            else
                AddPoint(s, (Point3f*)(params[i]),
                    FindOneInt("nvertices", i)); // jromang - p.926 find 'n' ? - [a ajouter dans le vecteur lors de l'appel de la fonction, avec un parametre 'nvertices supplementaire dans l API]
        }
        if (s == "from")
            AddPoint(s, (Point3f*)(params[i]));
        if (s == "p0")
            AddPoint(s, (Point3f*)(params[i]));
        if (s == "p1")
            AddPoint(s, (Point3f*)(params[i]));
        if (s == "p2")
            AddPoint(s, (Point3f*)(params[i]));
        if (s == "to")
            AddPoint(s,(Point3f*)(params[i]));

        //normal parameters
        if (s == "N")
            AddNormal(s, (Normal3f*)(params[i]),
                FindOneInt("nvertices", i)); // jromang - p.926 find 'n' ? - [a ajouter dans le vecteur lors de l'appel de la fonction, avec un parametre 'nvertices supplementaire dans l API]
        
        //std::vector parameters
        if (s == "rotate")
            AddVector(s, (Vector3f*)(params[i]));
        if (s == "scale")
            AddVector(s, (Vector3f*)(params[i]));
        if (s == "sundir")
            AddVector(s, (Vector3f*)(params[i]));
        if (s == "translate")
            AddVector(s, (Vector3f*)(params[i]));
        if (s == "updir")
            AddVector(s, (Vector3f*)(params[i]));
        if (s == "v1")
            AddVector(s, (Vector3f*)(params[i]));
        if (s == "v2")
            AddVector(s, (Vector3f*)(params[i]));
        
        //texture parameters
        if (s == "Ka")
            AddTexture(s, std::string(params[i]));
        if (s == "Kd")
            AddTexture(s, std::string(params[i]));
        if (s == "Kr")
            AddTexture(s, std::string(params[i]));
        if (s == "Ks")
            AddTexture(s, std::string(params[i]));
        if (s == "Ks1")
            AddTexture(s, std::string(params[i]));
        if (s == "Ks2")
            AddTexture(s, std::string(params[i]));
        if (s == "Ks3")
            AddTexture(s, std::string(params[i]));
        if (s == "Kt")
            AddTexture(s, std::string(params[i]));
        if (s == "L")
            AddTexture(s, std::string(params[i]));
        if (s == "M1")
            AddTexture(s, std::string(params[i]));
        if (s == "M2")
            AddTexture(s, std::string(params[i]));
        if (s == "M3")
            AddTexture(s, std::string(params[i]));
        if (s == "R1")
            AddTexture(s, std::string(params[i]));
        if (s == "R2")
            AddTexture(s, std::string(params[i]));
        if (s == "R3")
            AddTexture(s, std::string(params[i]));
        if (s == "amount")
            AddTexture(s, std::string(params[i]));
        if (s == "bricktex")
            AddTexture(s, std::string(params[i]));
        if (s == "bumpmap")
            AddTexture(s, std::string(params[i]));
        if (s == "cauchyb")
            AddTexture(s, std::string(params[i]));
        if (s == "d")
            AddTexture(s, std::string(params[i]));
        if (s == "film")
            AddTexture(s, std::string(params[i]));
        if (s == "filmindex")
            AddTexture(s, std::string(params[i]));
        if (s == "index")
            AddTexture(s, std::string(params[i]));
        if (s == "inside")
            AddTexture(s, std::string(params[i]));
        if (s == "mortartex")
            AddTexture(s, std::string(params[i]));
        if (s == "outside")
            AddTexture(s, std::string(params[i]));
        if (s == "sigma")
            AddTexture(s, std::string(params[i]));
        if (s == "tex1")
            AddTexture(s, std::string(params[i]));
        if (s == "tex2")
            AddTexture(s, std::string(params[i]));
        if (s == "uroughness")
            AddTexture(s, std::string(params[i]));
        if (s == "vroughness")
            AddTexture(s, std::string(params[i]));
        
        //color (RGBColor) parameters
        if (s == "L")
            { const RGBColor c((const float *)params[i]); AddRGBColor(s, &c, 1); }
        if (s == "Le")
            { const RGBColor c((const float *)params[i]); AddRGBColor(s, &c, 1); }
        if (s == "compo_key_color")
            { const RGBColor c((const float *)params[i]); AddRGBColor(s, &c, 1); }
        if (s == "sigma_a")
            { const RGBColor c((const float *)params[i]); AddRGBColor(s, &c, 1); }
        if (s == "sigma_s")
            { const RGBColor c((const float *)params[i]); AddRGBColor(s, &c, 1); }
        if (s == "v00")
            { const RGBColor c((const float *)params[i]); AddRGBColor(s, &c, 1); }
        if (s == "v01")
            { const RGBColor c((const float *)params[i]); AddRGBColor(s, &c, 1); }
        if (s == "v10")
            { const RGBColor c((const float *)params[i]); AddRGBColor(s, &c, 1); }
        if (s == "v11")
            { const RGBColor c((const float *)params[i]); AddRGBColor(s, &c, 1); }
        if (s == "value")
            { const RGBColor c((const float *)params[i]); AddRGBColor(s, &c, 1); }  

        //unknown parameter
        /*
        else
        {
            LOG(LUX_ERROR,LUX_SYNTAX)<<"Unknown parameter '"<<p<<":"<<s<<"', Ignoring.";
        }*/
    }
}

// Erase methods.
bool ParamSet::EraseInt(const std::string &name) { return EraseItem(ints, name); }
bool ParamSet::EraseBool(const std::string &name) { return EraseItem(bools, name); }
bool ParamSet::EraseFloat(const std::string &name) { return EraseItem(floats, name); }
bool ParamSet::ErasePoint(const std::string &name) { return EraseItem(points, name); }
bool ParamSet::EraseVector(const std::string &name) { return EraseItem(vectors, name); }
bool ParamSet::EraseNormal(const std::string &name) { return EraseItem(normals, name); }
bool ParamSet::EraseRGBColor(const std::string &name) { return EraseItem(spectra, name); }
bool ParamSet::EraseString(const std::string &name) { return EraseItem(strings, name); }
bool ParamSet::EraseTexture(const std::string &name) { return EraseItem(textures, name); }

// FindOne methods (scalar return).
float ParamSet::FindOneFloat(const std::string &name, float d) const {
    const auto *item = FindItemConst(floats, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<float> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
int ParamSet::FindOneInt(const std::string &name, int d) const {
    const auto *item = FindItemConst(ints, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<int> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
bool ParamSet::FindOneBool(const std::string &name, bool d) const {
    const auto *item = FindItemConst(bools, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<bool> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}

// FindOne methods (reference return).
const Point3f &ParamSet::FindOnePoint(const std::string &name, const Point3f &d) const {
    const auto *item = FindItemConst(points, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<Point3f> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
const Vector3f &ParamSet::FindOneVector(const std::string &name, const Vector3f &d) const {
    const auto *item = FindItemConst(vectors, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<Vector3f> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
const Normal3f &ParamSet::FindOneNormal(const std::string &name, const Normal3f &d) const {
    const auto *item = FindItemConst(normals, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<Normal3f> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
const RGBColor &ParamSet::FindOneRGBColor(const std::string &name, const RGBColor &d) const {
    const auto *item = FindItemConst(spectra, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<RGBColor> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}
const std::string &ParamSet::FindOneString(const std::string &name, const std::string &d) const {
    const auto *item = FindItemConst(strings, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<std::string> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return d;
}

// Find (array) methods.
const float *ParamSet::FindFloat(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(floats, name);
    if (item) {
        const_cast<ParamSetItem<float> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const int *ParamSet::FindInt(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(ints, name);
    if (item) {
        const_cast<ParamSetItem<int> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const bool *ParamSet::FindBool(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(bools, name);
    if (item) {
        const_cast<ParamSetItem<bool> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const Point3f *ParamSet::FindPoint(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(points, name);
    if (item) {
        const_cast<ParamSetItem<Point3f> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const Vector3f *ParamSet::FindVector(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(vectors, name);
    if (item) {
        const_cast<ParamSetItem<Vector3f> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const Normal3f *ParamSet::FindNormal(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(normals, name);
    if (item) {
        const_cast<ParamSetItem<Normal3f> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const RGBColor *ParamSet::FindRGBColor(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(spectra, name);
    if (item) {
        const_cast<ParamSetItem<RGBColor> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}
const std::string *ParamSet::FindString(const std::string &name, std::uint32_t *nItems) const {
    const auto *item = FindItemConst(strings, name);
    if (item) {
        const_cast<ParamSetItem<std::string> *>(item)->lookedUp = true;
        *nItems = item->nItems;
        return item->data;
    }
    *nItems = 0;
    return nullptr;
}

// FindTexture.
std::string ParamSet::FindTexture(const std::string &name) const {
    const auto *item = FindItemConst(textures, name);
    if (item && item->nItems >= 1) {
        const_cast<ParamSetItem<std::string> *>(item)->lookedUp = true;
        return item->data[0];
    }
    return "";
}

// Housekeeping.
void ParamSet::MarkAllUsed() const {
    auto mark = [](auto &v) {
        for (auto *item : v)
            item->lookedUp = true;
    };
    mark(ints); mark(bools); mark(floats); mark(points);
    mark(vectors); mark(normals); mark(spectra); mark(strings); mark(textures);
}

void ParamSet::ReportUnused() const {
    auto report = [this](const auto &v) {
        for (const auto *item : v)
            if (!item->lookedUp)
                LOG(LUX_WARNING, LUX_NOERROR) << "Unused parameter \"" << item->name << "\".";
    };
    report(ints); report(bools); report(floats); report(points);
    report(vectors); report(normals); report(spectra); report(strings); report(textures);
}

void ParamSet::Clear() {
    auto clear = [](auto &v) {
        for (auto *item : v) delete item;
        v.clear();
    };
    clear(ints); clear(bools); clear(floats); clear(points);
    clear(vectors); clear(normals); clear(spectra); clear(strings); clear(textures);
}

std::string ParamSet::ToString() const {
    std::ostringstream oss;
    auto dump = [&oss](const char *type, const auto &v) {
        for (const auto *item : v)
            oss << type << " " << item->name << " [" << item->nItems << " values]\n";
    };
    dump("int", ints);
    dump("bool", bools);
    dump("float", floats);
    dump("point", points);
    dump("vector", vectors);
    dump("normal", normals);
    dump("color", spectra);
    dump("string", strings);
    dump("texture", textures);
    return oss.str();
}

ParamSet::~ParamSet() {
    Clear();
}

// Clone every owned item so the copy owns independent data.
namespace {
template <class T>
std::vector<ParamSetItem<T> *> CloneItems(const std::vector<ParamSetItem<T> *> &v) {
    std::vector<ParamSetItem<T> *> out;
    out.reserve(v.size());
    for (const auto *item : v)
        out.push_back(item->Clone());
    return out;
}
} // namespace

ParamSet::ParamSet(const ParamSet &p)
    : ints(CloneItems(p.ints)),
      bools(CloneItems(p.bools)),
      floats(CloneItems(p.floats)),
      points(CloneItems(p.points)),
      vectors(CloneItems(p.vectors)),
      normals(CloneItems(p.normals)),
      spectra(CloneItems(p.spectra)),
      strings(CloneItems(p.strings)),
      textures(CloneItems(p.textures)) {
}

ParamSet &ParamSet::operator=(const ParamSet &p) {
    if (this != &p) {
        Clear();
        ints = CloneItems(p.ints);
        bools = CloneItems(p.bools);
        floats = CloneItems(p.floats);
        points = CloneItems(p.points);
        vectors = CloneItems(p.vectors);
        normals = CloneItems(p.normals);
        spectra = CloneItems(p.spectra);
        strings = CloneItems(p.strings);
        textures = CloneItems(p.textures);
    }
    return *this;
}

} // namespace lux2
