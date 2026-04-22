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

 - Misc. library updates include:
    - QT4 --> 6
    - Boost 1.44 --> 1.90
    - OpenImageIO 1.6.3 --> 3.1.12

What's included:
---------------
 - luxrender, luxconsole, luxmerger
   LuxRender GUI and console version, FLM merging tool.
 
 - liblux.so, pylux.so
   LuxRender shared library and Python binding modules for Blender
   2.79b integration.
 
 - examples
   Two example scenes for LuxRenderer which you can try right away:
   LuxTime (by freejack) and School Corridor (by B.Y.O.B.).

Notes:
-----
 - Please note that our official precompiled LuxRender builds are only
   compatible with official Blender builds from http://blender.org. If
   you are using a custom Blender build from your Linux distribution
   repository or a some alternative source, you should use a custom
   LuxRender build as well.

Compiling from source:
---------------------
 - To compile from source, you'll need to install Qt6 core, dbus,
   gui, imageformats, and widgets from your package manager.

 - You will also need fftw3, freetype, and common compression libraries
   like lzma and bzip2. These are likely present on your system already.

 - On Debian or Ubuntu run:
    sudo apt install git cmake build-essential python3-dev qt6-base-dev qt6-image-formats-plugins

 - On Fedora or RHEL run:
    sudo dnf groupinstall "C Development Tools and Libraries"
    sudo dnf install git cmake python3-devel qt6-qtbase-devel qt6-qtimageformats

 - Then do the standard cmake dance from the main folder:
    mkdir build
    cd build
    cmake ..
    make -j

Demo scenes:
-----------
Various LuxRender demo scenes, ranging from the most simple to highly 
complex ones: https://github.com/rrubberr/Flatpak-LuxRender-Scenes

Bugs:
----
Our bugtracker is at https://github.com/rrubberr/Flatpak-LuxRender/issues

License:
-------
LuxRender is developed and distributed under GNU GPL v3.


  LuxRender Team
