#!/bin/bash

# Copy the files out of the flatpak build.
cp -v ../.build-dir/files/bin/luxcomp ./
cp -v ../.build-dir/files/bin/luxconsole ./
cp -v ../.build-dir/files/bin/luxmerger ./
cp -v ../.build-dir/files/bin/luxrender ./
cp -v ../.build-dir/files/bin/pylux.so ./
cp -v ../.build-dir/files/bin/liblux.so ./

# Strip the binaries and shared objects of debug info
strip luxcomp
strip luxconsole
strip luxmerger
strip luxrender
strip pylux.so
strip liblux.so