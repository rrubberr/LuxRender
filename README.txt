=================================================================
Lux-Builder is a static compilation environment for LuxRender.
=================================================================

LuxRender 1.9
=============
LuxRender is a physically based and unbiased rendering engine. Based on
state of the art algorithms, LuxRender simulates the flow of light
according to physical equations, thus producing realistic images of
photographic quality.

What's new in 1.9:
-----------------
 - The LuxRender build system has been updated to support native
   compilation on modern Linux distributions, using GCC 15+
   and CMake 4.0+.

 - A new collapsing BVH accelerator using AVX2 will be compiled
   if host CPU support is detected. Set your accelerator to "BVH"
   in Blender. (Wald et al. 2008)

 - The Bidirectional integrator now supports ray connections from
   light sources directly to the camera. (Veach & Guibas 1994)

 - Misc. library updates include:
    - QT4 --> 6
    - Boost 1.44 --> 1.90
    - OpenImageIO 1.6.3 --> 3.1.12

What's included:
---------------
 - luxrender, luxconsole, luxmerger, liblux.so and pylux.so.

 - Examples:
   Two example scenes for LuxRender which you can try right away:
   LuxTime (by freejack) and School Corridor (by B.Y.O.B.).

Notes:
-----
 - Please note that our precompiled LuxRender builds are only
   compatible with official Blender builds from http://blender.org. If
   you are using a custom Blender build from your Linux distribution
   repository or some alternative source, you should build LuxRender.

Compiling from source:
---------------------
 - To compile from source, you'll need to install several libraries.

 - On Debian or Ubuntu run:
    sudo apt install bison build-essential cmake flex git libfftw3-dev \
    patch pkg-config python3-dev qt6-base-dev qt6-image-formats-plugins

 - On Fedora or RHEL run:
    sudo dnf group install "c-development"
    sudo dnf install bison cmake expat-devel fftw-devel patch flex git \
    pkgconf-pkg-config python3-devel qt6-qtbase-devel qt6-qtimageformats 
 
 - On Arch & friends run:
    sudo pacman -S base-devel bison cmake fftw flex git pkgconf python \
    qt6-base qt6-imageformats

 - On an Intel Mac, first install XCode and Brew, then run:
    brew install bison cmake fftw flex gpatch pkgconf qt xquartz

 - Then do the standard cmake dance from the main folder:
    mkdir build
    cd build
    cmake ..
    make -j

Demo scenes:
-----------
Various LuxRender demo scenes, ranging from the most simple to highly 
complex ones: https://github.com/rrubberr/Flatpak-LuxRender-Scenes

Wiki:
----
Our wiki is at https://github.com/rrubberr/LuxRender/wiki

Bugs:
----
Our bugtracker is at https://github.com/rrubberr/LuxRender/issues

License:
-------
LuxRender is developed and distributed under GNU GPL v3.

  LuxRender Team
