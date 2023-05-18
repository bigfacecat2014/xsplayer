#!/bin/sh

arch=x86

clean() (
  make distclean > /dev/null 2>&1
)

config() (
  echo "enter config()"
  OPTIONS="
    --prefix=/d/ffmpeg/bin_mingw32d \
    --enable-debug                  \
    --disable-optimizations         \
    --enable-shared                 \
    --disable-static                \
    --enable-gpl                    \
    --enable-version3               \
    --enable-autodetect             \
    --disable-mediafoundation       \
    --enable-avdevice               \
    --disable-devices               \
    --enable-protocols              \
    --enable-demuxers               \
    --enable-muxers                 \
    --enable-filters                \
    --enable-bsfs                   \
    --enable-postproc               \
    --enable-parsers                \
    --enable-encoders               \
    --enable-libx264                \
    --enable-decoders               \
    --disable-doc                   \
    --disable-avisynth              \
    --disable-programs              \
    --enable-ffmpeg                 \
    --enable-ffprobe                \
    --enable-sdl2                   \
    --enable-ffplay                 \
    --arch=${arch}"

  sh ./configure ${OPTIONS}
)

build() (
  make -j$NUMBER_OF_PROCESSORS
)

echo Building ffmpeg with gcc ...

clean

config

#build
