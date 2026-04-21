#!/bin/bash

######################################################################
######################################################################
#Set up the directory structure.
######################################################################
######################################################################
cd "$PWD"
MAKELUX_DIR=$PWD

mkdir -p $MAKELUX_DIR/ext
mkdir -p $MAKELUX_DIR/ext/build
mkdir -p $MAKELUX_DIR/ext/build/bin
mkdir -p $MAKELUX_DIR/ext/build/lib
mkdir -p $MAKELUX_DIR/ext/build/include

SRC_DIR=$MAKELUX_DIR/src
EXT_DIR=$MAKELUX_DIR/ext
BLD_DIR=$EXT_DIR/build
BIN_DIR=$BLD_DIR/bin
LIB_DIR=$BLD_DIR/lib
INC_DIR=$BLD_DIR/include

PATCH_DIR=$PWD/patches

######################################################################
######################################################################
#Enumerate OpenImageIO deps.
######################################################################
######################################################################

#For zlib-ng.
ZLIB_DIR="$EXT_DIR/zlib-ng"
ZLIB_URL="https://github.com/zlib-ng/zlib-ng.git"
ZLIB_TAG="2.3.3"

#For robin-map.
ROBIN_DIR="$EXT_DIR/robin-map"
ROBIN_URL="https://github.com/Tessil/robin-map.git"
ROBIN_TAG="v1.4.1"

#For yaml-cpp.
YAML_DIR="$EXT_DIR/yaml-cpp"
YAML_URL="https://github.com/jbeder/yaml-cpp.git"
YAML_TAG="yaml-cpp-0.9.0"

#For pybind11.
PYBIND_DIR="$EXT_DIR/pybind11"
PYBIND_URL="https://github.com/pybind/pybind11.git"
PYBIND_TAG="v3.0.3"

#For pystring.
PYSTRING_DIR="$EXT_DIR/pystring"
PYSTRING_URL="https://github.com/imageworks/pystring.git"
PYSTRING_TAG="v1.1.5"

#For minizip.
MINIZIP_DIR="$EXT_DIR/minizip-ng"
MINIZIP_URL="https://github.com/zlib-ng/minizip-ng.git"
MINIZIP_TAG="4.1.0"

#For libpng.
PNG_DIR="libpng16"
PNG_URL="https://github.com/pnggroup/libpng.git"
PNG_TAG="v1.6.58"

#For openjph.
JPH_DIR="OpenJPH"
JPH_URL="https://github.com/aous72/OpenJPH.git"
JPH_TAG="0.27.0"


#For libjpeg-turbo.
JPEG_DIR="libjpeg-turbo"
JPEG_URL="https://github.com/libjpeg-turbo/libjpeg-turbo.git"
JPEG_TAG="3.1.4.1"

#For libtiff.
TIFF_DIR="libtiff"
TIFF_URL="https://gitlab.com/libtiff/libtiff.git"
TIFF_TAG="v4.7.1"

#For Imath.
IMATH_DIR="$EXT_DIR/Imath"
IMATH_URL="https://github.com/AcademySoftwareFoundation/Imath.git"
IMATH_TAG="v3.2.2"

#For OpenEXR.
EXR_DIR="$EXT_DIR/openexr"
EXR_URL="https://github.com/AcademySoftwareFoundation/openexr.git"
EXR_TAG="v3.4.9"

#For OpenColorIO.
OCIO_DIR="$EXT_DIR/OpenColorIO"
OCIO_URL="https://github.com/AcademySoftwareFoundation/OpenColorIO.git"
OCIO_TAG="v2.5.1"

#For OpenImageIO.
OIIO_DIR="$EXT_DIR/OpenImageIO"
OIIO_URL="https://github.com/AcademySoftwareFoundation/OpenImageIO.git"
OIIO_TAG="v3.1.12.0"

#Compile into strings.
OIIO_DEP_DIRS="$ZLIB_DIR $ROBIN_DIR $YAML_DIR $PYBIND_DIR $PYSTRING_DIR $MINIZIP_DIR $PNG_DIR $JPH_DIR $JPEG_DIR $TIFF_DIR $IMATH_DIR $EXR_DIR $OCIO_DIR $OIIO_DIR"
OIIO_DEP_URLS="$ZLIB_URL $ROBIN_URL $YAML_URL $PYBIND_URL $PYSTRING_URL $MINIZIP_URL $PNG_URL $JPH_URL $JPEG_URL $TIFF_URL $IMATH_URL $EXR_URL $OCIO_URL $OIIO_URL"
OIIO_DEP_TAGS="$ZLIB_TAG $ROBIN_TAG $YAML_TAG $PYBIND_TAG $PYSTRING_TAG $MINIZIP_TAG $PNG_TAG $JPH_TAG $JPEG_TAG $TIFF_TAG $IMATH_TAG $EXR_TAG $OCIO_TAG $OIIO_TAG"

######################################################################
######################################################################
#Build OpenImageIO.
######################################################################
######################################################################

cd $EXT_DIR

#Turn strings into arrays.
read -r -a URLS <<< "$OIIO_DEP_URLS"
read -r -a DIRS <<< "$OIIO_DEP_DIRS"
read -r -a TAGS <<< "$OIIO_DEP_TAGS"

for i in "${!URLS[@]}"; do
    URL="${URLS[$i]}"
    DIR="${DIRS[$i]}"
    TAG="${TAGS[$i]}"
    NAME=$(basename "$DIR")

    echo "Processing $NAME..."

#Clone if missing.

    cd $EXT_DIR

    if [ ! -d "$DIR" ]; then
        echo "$NAME sources not found, cloning..."
        git clone --recursive -b "$TAG" "$URL" "$DIR" || exit 1
    else
        echo "$NAME sources already exist, skipping clone."
    fi

    #Build if missing.
    if [ "$NAME" != "libtiff" ] && [ "$NAME" != "OpenJPH" ] && [ ! -d "$DIR/build" ]; then
        echo "$NAME build directory not found, building..."

        mkdir -p "$DIR/build"
        cd "$DIR/build" || exit 1

        cmake ../ \
            -DCMAKE_INSTALL_PREFIX="$BLD_DIR" \
            -DCMAKE_INSTALL_LIBDIR="$LIB_DIR" \
            -D_GLIBCXX_USE_CXX11_ABI="11" \
            -DCMAKE_PREFIX_PATH="$BLD_DIR" \
            -DCMAKE_CXX_FLAGS="-w -fPIC" \
            -DCMAKE_POSITION_INDEPENDENT_CODE="ON" \
            -DCMAKE_CXX_STANDARD="17" \
            -DZLIB_COMPAT="ON" \
            -DZLIB_ENABLE_TESTS="OFF" \
            -DBUILD_SHARED_LIBS="OFF" \
            -DOCIO_BUILD_APPS="OFF" \
            -DOCIO_BUILD_TESTS="OFF" \
            -DOCIO_BUILD_PYTHON="OFF" \
            -DOIIO_USE_PYTHON="0" \
            -DOIIO_BUILD_TESTS="0" \
            -DOIIO_BUILD_TOOLS="0" \
            -DSTOP_ON_WARNING="0" \
            -DUSE_PYTHON="0" \
            -DUSE_FFMPEG="0" \
            -DUSE_FIELD3D="0" \
            -DUSE_FREETYPE="0" \
            -DUSE_LIBUHDR="0" \
            -DUSE_LIBRAW="0" \
            -DUSE_NUKE="0" \
            -DUSE_QT="0" \
            -DUSE_JXL="0" \
            -DUSE_OPENCV="0" \
            -DUSE_DCMTK="0" \
            -DUSE_FFmpeg="0" \
            -DUSE_LibRaw="0" \
            -DUSE_OpenVDB="0" \
            -DUSE_Ptex="0" \
            -DUSE_WebP="0" \
            -DUSE_OpenVDB="0" \
            -DUSE_openjph="0" \
            -DUSE_Libheif="0" \
            -DUSE_OpenJPEG="0" \
            -DUSE_GIF="0" \
            -DZLIB_ROOT="$BLD_DIR" \
            -DMZ_ZLIB_FLAVOR="zlib" \
            -DWITH_TURBOJPEG="0" \
            -DMZ_PKCRYPT="OFF" \
            -DMZ_WZAES="OFF" \
            -DMZ_OPENSSL="OFF" \
            -DPNG_SHARED="OFF" \
            -DPNG_STATIC="ON" \
            -DOPENEXR_FORCE_INTERNAL_OPENJPH="OFF" \
            -DBUILD_TESTING="OFF" \
            -DUSE_TBB="0"
        
        make install -j"$(nproc)"
    else
        echo "$NAME already built, skipping."
    fi

    if [ "$NAME" = "libtiff" ] && [ ! -f "$LIB_DIR/libtiff.a" ]; then

        cd $EXT_DIR

        rm -rf $TIFF_DIR
        git clone --recursive -b "$TAG" "$URL" "$DIR"

        echo "$NAME build directory not found, building..."

        mkdir -p "$DIR/build"
        cd "$DIR/build" || exit 1

        cmake ../ \
            -DCMAKE_INSTALL_PREFIX="$BLD_DIR" \
            -DCMAKE_INSTALL_LIBDIR="$LIB_DIR" \
            -D_GLIBCXX_USE_CXX11_ABI="11" \
            -DCMAKE_PREFIX_PATH="$BLD_DIR" \
            -DCMAKE_CXX_FLAGS="-w -fPIC" \
            -DCMAKE_POSITION_INDEPENDENT_CODE="ON" \
            -DCMAKE_CXX_STANDARD="17" \
            -DBUILD_SHARED_LIBS="OFF" \
            -Dlibdeflate="OFF" \
            -Dlzma="OFF" \
            -Dzstd="OFF" \
            -Dwebp="OFF" \
            -Djbig="OFF" \
            -Dtiff-tools="OFF" \
            -Dtiff-tests="OFF" \
            -Dtiff-cxx="OFF"
        
        make install -j"$(nproc)"

    fi


    if [ "$NAME" = "OpenJPH" ] && [ ! -f "$LIB_DIR/libopenjph.a" ]; then

        cd $EXT_DIR

        rm -rf $JPH_DIR
        git clone --recursive -b "$TAG" "$URL" "$DIR"

        echo "$NAME build directory not found, building..."

        rm -rf "$JPH_DIR/build"

        mkdir -p "$DIR/build"
        cd "$DIR/build" || exit 1

        cmake ../ \
            -DCMAKE_INSTALL_PREFIX="$BLD_DIR" \
            -DCMAKE_INSTALL_LIBDIR="$LIB_DIR" \
            -D_GLIBCXX_USE_CXX11_ABI="11" \
            -DCMAKE_PREFIX_PATH="$BLD_DIR" \
            -DCMAKE_CXX_FLAGS="-w -fPIC" \
            -DCMAKE_POSITION_INDEPENDENT_CODE="ON" \
            -DCMAKE_CXX_STANDARD="17" \
            -DBUILD_SHARED_LIBS="OFF" \
            -DOJPH_BUILD_EXECUTABLES="OFF" \
            -DOJPH_ENABLE_TIFF_SUPPORT="OFF"
        
        make install -j"$(nproc)"

    fi

done

######################################################################
######################################################################
#Enumerate dependencies.
######################################################################
######################################################################

#For OpenSSL.
OPENSSL_HASH="ecd0c6ffb493dd06707d38b14bb4d8c2288bb7033735606569d8f90f89669d16"
OPENSSL_FILENAME="openssl-1.0.2u.tar.gz"
OPENSSL_DIR="$EXT_DIR/openssl-1.0.2u"
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/OpenSSL_1_0_2u/openssl-1.0.2u.tar.gz"
OPENSSL_INC_DIR="$INC_DIR/openssl"

#For Python.
PYTHON_HASH="0f0fa8685c1dc1f1dacb0b4e7779796b90aef99dc1fa4967a71b9da7b57d4a28"
PYTHON_FILENAME="Python-3.5.10.tar.xz"
PYTHON_DIR="$EXT_DIR/Python-3.5.10"
PYTHON_URL="https://www.python.org/ftp/python/3.5.10/Python-3.5.10.tar.xz"
PYTHON_PATCH="$PATCH_DIR/python/python-disable-nis.patch"
PYTHON_INC_DIR="$INC_DIR/python3.5m"

#For Boost.
BOOST_HASH="5e93d582aff26868d581a52ae78c7d8edf3f3064742c6e77901a1f18a437eea9"
BOOST_FILENAME="boost_1_90_0.tar.bz2"
BOOST_DIR="$EXT_DIR/boost_1_90_0"
BOOST_URL="https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.gz"
BOOST_INC_DIR="$INC_DIR/boost"

#Compile into strings.
EXT_URLS="$OPENSSL_URL $PYTHON_URL $BOOST_URL"
EXT_FILENAMES="$OPENSSL_FILENAME $PYTHON_FILENAME $BOOST_FILENAME"
EXT_HASHES="$OPENSSL_HASH $PYTHON_HASH $BOOST_HASH"
EXT_DIRS="$OPENSSL_DIR $PYTHON_DIR $BOOST_DIR"

######################################################################
######################################################################
#Download and hash dependencies.
######################################################################
######################################################################

cd $EXT_DIR

MAX_RETRIES=3

#Turn strings into arrays.
read -r -a URLS <<< "$EXT_URLS"
read -r -a FILENAMES <<< "$EXT_FILENAMES"
read -r -a HASHES <<< "$EXT_HASHES"

echo "URLs: ${#URLS[@]}"
echo "FILENAMES: ${#FILENAMES[@]}"
echo "HASHES: ${#HASHES[@]}"

#Sanity check string lengths.
if [ ${#URLS[@]} -ne ${#FILENAMES[@]} ] || [ ${#URLS[@]} -ne ${#HASHES[@]} ]; then
    echo "Mismatch between URLs, filenames, and hashes!"
    exit 1
fi

#Loop through all entries and download + verify.
for i in "${!URLS[@]}"; do
    URL="${URLS[$i]}"
    FILENAME="${FILENAMES[$i]}"
    HASH="${HASHES[$i]}"

    COUNT=0

    while [ $COUNT -lt $MAX_RETRIES ]; do
        echo "Downloading $FILENAME (Attempt $((COUNT+1)))..."

        curl -L -C - -o "$FILENAME" "$URL"

        if echo "$HASH  $FILENAME" | sha256sum -c -; then
            echo "Checksum verified for $FILENAME."
            break
        else
            echo "Checksum failed for $FILENAME!"
            ((COUNT++))
        fi

        if [ $COUNT -eq $MAX_RETRIES ]; then
            echo "Failed to download $FILENAME after $MAX_RETRIES attempts."
            exit 1
        fi

        sleep 2
    done
done

######################################################################
######################################################################
#Decompress dependencies.
######################################################################
######################################################################

cd $EXT_DIR

#Turn strings into arrays.
read -r -a FILENAMES <<< "$EXT_FILENAMES"
read -r -a DIRS <<< "$EXT_DIRS"

#Sanity check string lengths.
if [ ${#FILENAMES[@]} -ne ${#DIRS[@]} ]; then
    echo "Mismatch between filenames and directories!"
    exit 1
fi

#Loop through all entries and untar.
for i in "${!FILENAMES[@]}"; do
    file="${FILENAMES[$i]}"
    dir="${DIRS[$i]}"

    #Skip if directory already exists
    if [ -d "$dir" ]; then
        echo "Skipping $file (directory $dir already exists)"
        continue
    fi

    echo "Extracting $file into $dir..."

    mkdir -p "$dir"

    tar -xf "$file" -C "$dir" --strip-components=1
done

######################################################################
######################################################################
#Build OpenSSL.
######################################################################
######################################################################

#Do not build if OpenSSL was already installed.
if [ ! -d "$OPENSSL_INC_DIR" ]; then

    echo "OpenSSL not found. Building..."

    cd $OPENSSL_DIR
    ./config shared -fPIC -w -std=c++14 --prefix=$BLD_DIR
    make install

fi

######################################################################
######################################################################
#Build Python.
######################################################################
######################################################################

#Do not build if Python was already installed.
if [ ! -d "$PYTHON_INC_DIR" ]; then
    echo "Python not found. Building..."

    cd $PYTHON_DIR

    #Patch some broken files.
    sed -i 's@sinpi@m_sinpi@g' ./Modules/mathmodule.c
    sed -i 's@(self->win->_flags & _ISPAD)@(is_pad(self->win))@g' ./Modules/_cursesmodule.c

        if git apply --check "$PYTHON_PATCH"; then
            echo "Patch applies cleanly. Applying now..."
            git apply "$PYTHON_PATCH"
    
        else
            echo "Error: Patch cannot be applied. Check for conflicts."
            exit 1
        fi

    #The double $$ is necessary to prevent the shell from interpreting ORIGIN as a variable during compilation.
    ./configure --prefix=$BLD_DIR LDFLAGS="-Wl,-rpath,'\$\$ORIGIN'" --with-openssl=$BIN_DIR --disable-shared --with-ensurepip=no

    #Make.
    make install -j$(nproc) cc=g++ cxx=g++ cflags="-O2 -w -fPIC -std=c++14 -I $BIN_DIR -I $OPENSSL_INC_DIR" cxxflags="-O2 -w -fPIC -std=c++14 -I $BIN_DIR -I $OPENSSL_INC_DIR -fpermissive"

    #These are installed with incorrect permissions.
    chmod 644 $LIB_DIR/libpython3.5m.so.1.0
    chmod 644 $LIB_DIR/libpython3.so

    #Copy python3 binary dependency.
    cp $LIB_DIR/libpython3.5m.so.1.0 $BIN_DIR

fi

######################################################################
######################################################################
#Build Boost.
######################################################################
######################################################################

if [ ! -d "$BOOST_INC_DIR" ]; then
    echo "Boost not found. Building..."

    cd $BOOST_DIR

        #Set custom Python location.
        touch user-config.jam
        echo "using python : 3.5 : $BLD_DIR : $INC_DIR : $LIB_DIR ;" > user-config.jam

    #Configure.
    ./bootstrap.sh --prefix=$BLD_DIR

    #Make.
    ./b2 --user-config=user-config.jam stage install \
        --with-program_options \
        --with-regex \
        --with-filesystem \
        --with-system \
        --with-thread \
        --with-serialization \
        --with-iostreams \
        --with-python \
        --with-chrono \
        hardcode-dll-paths=true dll-path="'\$\$ORIGIN'" variant=release link=static runtime-link=static cflags="-fPIC" cxxflags="-fPIC -I $PYTHON_INC_DIR" -j$(nproc)

    #Make links.
    ln -s $LIB_DIR/libboost_python35.a $LIB_DIR/libboost_python.a 

fi

######################################################################
######################################################################
#Build LuxRays.
######################################################################
######################################################################

cd $SRC_DIR

#Build LuxRays.
LUXRAYS_LIB=$LIB_DIR/libluxrays.a

    if [ ! -f "$LUXRAYS_LIB" ]; then
        echo "LuxRays not found. Building..."

    if [ -d "$SRC_DIR/luxrays/build" ]; then
        rm -rf $SRC_DIR/luxrays/build
    fi

    mkdir $SRC_DIR/luxrays/build
    cd $SRC_DIR/luxrays/build

    #Configure.
    cmake .. -DCMAKE_CXX_FLAGS="-w -fPIC -std=c++14 -I $INC_DIR -DBOOST_BIND_GLOBAL_PLACEHOLDERS" \
        -DCMAKE_BUILD_TYPE="Release" \
        -DCMAKE_POLICY_VERSION_MINIMUM="3.5" \
        -D_GLIBCXX_USE_CXX11_ABI="11" \
        -DPYTHON_LIBRARY="$LIB_DIR/libpython3.5m.so" \
        -DBoost_USE_STATIC_LIBS="ON" \
        -DBoost_USE_STATIC_RUNTIME="ON" \
        -DCMAKE_PREFIX_PATH="$BLD_DIR" \
        -DCMAKE_POLICY_DEFAULT_CMP0074="NEW" \
        -DCMAKE_POLICY_DEFAULT_CMP0148="OLD" \
        -DCMAKE_POLICY_DEFAULT_CMP0167="OLD" \
        -DCMAKE_EXE_LINKER_FLAGS="-static" \
        -DCMAKE_FIND_LIBRARY_SUFFIXES=".a"

    make -j$(nproc)

    #Install libraries.
    cp -av lib/. $LIB_DIR/.
    cp -av $SRC_DIR/luxrays/include/luxrays $INC_DIR/

fi

######################################################################
######################################################################
#Build LuxRender.
######################################################################
######################################################################

cd $SRC_DIR

    if [ -d "$SRC_DIR/lux/build" ]; then
        rm -rf $SRC_DIR/lux/build
    fi

    mkdir $SRC_DIR/lux/build
    cd $SRC_DIR/lux/build

#Configure.
cmake .. -DCMAKE_CXX_FLAGS="-w -fPIC -std=c++14 -DBOOST_BIND_GLOBAL_PLACEHOLDERS" \
    -DCMAKE_BUILD_TYPE="RELWITHDEBINFO" \
    -DCMAKE_POLICY_VERSION_MINIMUM="3.5" \
    -D_GLIBCXX_USE_CXX11_ABI="11" \
    -DCMAKE_PREFIX_PATH="$BLD_DIR" \
    -DBoost_DIR="$LIB_DIR/cmake/Boost-1.90.0" \
    -DBoost_LIBRARY_DIRS="$LIB_DIR" \
    -DBoost_INCLUDE_DIRS="$INC_DIR" \
    -DBoost_USE_STATIC_RUNTIME="ON" \
    -DBoost_NO_SYSTEM_PATHS="ON" \
    -DBoost_USE_STATIC_LIBS="ON" \
    -DCMAKE_POLICY_DEFAULT_CMP0074="NEW" \
    -DCMAKE_POLICY_DEFAULT_CMP0148="OLD" \
    -DCMAKE_POLICY_DEFAULT_CMP0144="OLD" \
    -DCMAKE_POLICY_DEFAULT_CMP0167="OLD" \
    -DCMAKE_EXE_LINKER_FLAGS="-static" \
    -DCMAKE_FIND_LIBRARY_SUFFIXES=".a" \
    -DTIFF_LIBRARY="$LIB_DIR/lib64/libtiff.a" \
    -DJPEG_LIBRARY="$LIB_DIR/libjpeg.a" \
    -DPNG_LIBRARY="$LIB_DIR/libpng16.a" \
    -Dminizip-ng_LIBRARY="$LIB_DIR/libminizip.a" \
    -DJPEG_INCLUDE_DIR="$INC_DIR" \
    -DLuxRays_HOME="$BLD_DIR" \
    -DPython3_ROOT_DIR="$BLD_DIR" \
    -DPython3_FIND_STRATEGY=LOCATION \
    -DPython3_FIND_REGISTRY=NEVER \
    -DPython3_FIND_FRAMEWORK=NEVER

make -j$(nproc)

######################################################################
######################################################################
#Install LuxRender.
######################################################################
######################################################################

cd $SRC_DIR/lux/build

mkdir -p $MAKELUX_DIR/release

cp -v luxcomp $MAKELUX_DIR/release
cp -v luxconsole $MAKELUX_DIR/release
cp -v luxmerger $MAKELUX_DIR/release
cp -v luxrender $MAKELUX_DIR/release
cp -v pylux.so $MAKELUX_DIR/release
cp -v liblux.so $MAKELUX_DIR/release

echo "Success!"