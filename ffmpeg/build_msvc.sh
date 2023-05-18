#!/bin/sh

arch=x86

make_dirs() (
  mkdir -p bin_win32d/lib
)

clean() (
  make distclean > /dev/null 2>&1
)

#add custom fake <unistd.h> to:
#C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\include
#or 
#C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\ucrt\sys
# You can also fix zconf.h: #define HAVE_UNISTD_H 0

config() (
  echo "enter config()"
  OPTIONS="
    --prefix=/d/ffmpeg/bin_win32d   \
    --toolchain=msvc                \
    --target_os=win32               \
    --enable-debug                  \
    --disable-optimizations         \
    --enable-shared                 \
    --disable-static                \
    --enable-gpl                    \
    --enable-version3               \
    --enable-nonfree                \
    --enable-autodetect             \
    --disable-mediafoundation       \
    --disable-devices               \
    --disable-filters               \
    --enable-filter=aresample,scale,yadif,w3fdif,bwdif \
    --disable-protocol=async,cache,concat,httpproxy,icecast,md5,subfile \
    --disable-muxers                \
    --enable-muxer=spdif            \
    --disable-bsfs                  \
    --enable-bsf=extract_extradata  \
    --enable-postproc               \
    --enable-avdevice               \
    --disable-encoders              \
    --enable-encoder=libx264,libfdk_aac,libmp3lame \
    --enable-libx264                \
    --enable-libfdk-aac             \
    --enable-libmp3lame             \
    --enable-doc                    \
    --disable-avisynth              \
    --enable-hwaccels               \
    --enable-schannel               \
    --disable-programs              \
    --enable-ffmpeg                 \
    --enable-ffplay                 \
    --enable-ffprobe                \
    --enable-zlib                   \
    --enable-sdl2                   \
    --arch=${arch}"

  export PKG_CONFIG_PATH=/mingw32/i686-AMD-win32/lib/pkgconfig
  EXTRA_CFLAGS="-D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -MDd -Zo -GS -I/mingw32/i686-AMD-win32/include -I/mingw32/i686-AMD-win32/include/SDL2 -I compat/atomics/win32"
  EXTRA_LDFLAGS="-LIBPATH:/mingw32/i686-AMD-win32/lib"
  sh ./configure ${OPTIONS} --extra-cflags="${EXTRA_CFLAGS}" --extra-ldflags="${EXTRA_LDFLAGS}" 
)

build() (
  make -j$NUMBER_OF_PROCESSORS
)


echo Building ffmpeg with msvc ...

make_dirs

clean

config

build

sh ./cc.sh

