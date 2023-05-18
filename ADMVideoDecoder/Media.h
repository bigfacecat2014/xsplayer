/*
 *      Copyright (C) 2010-2019 Hendrik Leppkes
 *      http://www.1f0.de
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include "IMediaSideData.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/hdr_dynamic_metadata.h"

AVCodecID FindCodecId(const CMediaType* mt);
int getThreadFlags(AVCodecID codecId);

#define MAX_NUM_CC_CODECS 3

struct codec_config_t {
    int nCodecs;
    AVCodecID codecs[MAX_NUM_CC_CODECS];
    const char* name;
    const char* description;
};
const codec_config_t* get_codec_config(LAVVideoCodec codec);

int flip_plane(BYTE* buffer, int stride, int height);
void fillDXVAExtFormat(DXVA2_ExtendedFormat& fmt, int range, int primaries, int matrix,
    int transfer, int chroma_sample_location, bool bClear);

#define STATE_NOT_FOUND 0
#define STATE_EOS_FOUND 1
#define STATE_GOP_FOUND 2
