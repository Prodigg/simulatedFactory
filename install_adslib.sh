#!/bin/bash
mkdir /tmp/buildADSLib || exit
cd /tmp/buildADSLib || exit
git clone https://github.com/beckhoff/ads
cd ./ads || exit
meson setup build
meson compile -C build
sudo meson install -C build