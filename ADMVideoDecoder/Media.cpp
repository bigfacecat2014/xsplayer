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

#include "stdafx.h"
#include "LAVVideo.h"
#include "Media.h"

typedef struct {
    const CLSID* clsMinorType;
    const enum AVCodecID  nFFCodec;
} FFMPEG_SUBTYPE_MAP;

// I only need support HEVC

// Map Media Subtype <> FFMPEG Codec Id
static const FFMPEG_SUBTYPE_MAP lavc_video_codecs[] = {
  { &MEDIASUBTYPE_HEVC, AV_CODEC_ID_HEVC },
};

// Define Input Media Types
const AMOVIESETUP_MEDIATYPE CLAVVideo::sudPinTypesIn[] = {
  { &MEDIATYPE_Video, &MEDIASUBTYPE_HEVC },
};
const UINT CLAVVideo::sudPinTypesInCount = countof(CLAVVideo::sudPinTypesIn);

// Define Output Media Types
//
// 建议保留格式：{ &MEDIATYPE_Video, & MEDIASUBTYPE_NV12 },
// 原因如下：
//  （1）这一种半平面半打包模式：Half Packed YYYY UVUV
//  （2）iOS只有这一种模式。
//  （3）USB Camere采集格式。
//  （4）安卓的模式是NV21：YYYY VUVU。
//  （5）UV丢失一部分没关系，已接收到的部分像素色彩信息可以正常解码。
//
const AMOVIESETUP_MEDIATYPE CLAVVideo::sudPinTypesOut[] = {
  { &MEDIATYPE_Video, &MEDIASUBTYPE_YV12 }, // Planar YYYY VV UU 常见于H.264的解码器，而I420格式为：YYYY UU VV，用于DVD-Video。都属于YUV420P采样格式。
  { &MEDIATYPE_Video, &MEDIASUBTYPE_RGB32 },// Packed ARGB 用于视频控制器从VRAM读取转换输出至显示器，Alpha分量被忽略。
};
const UINT CLAVVideo::sudPinTypesOutCount = countof(CLAVVideo::sudPinTypesOut);

// Crawl the lavc_video_codecs array for the proper codec
AVCodecID FindCodecId(const CMediaType* mt)
{
    for (int i = 0; i < countof(lavc_video_codecs); ++i) {
        if (mt->subtype == *lavc_video_codecs[i].clsMinorType) {
            return lavc_video_codecs[i].nFFCodec;
        }
    }
    return AV_CODEC_ID_NONE;
}

// Strings will be filled in eventually.
// AV_CODEC_ID_NONE means there is some special handling going on.
// Order is Important, has to be the same as the CC Enum
// Also, the order is used for storage in the Registry
static codec_config_t m_codec_config[] = {
  { 1, { AV_CODEC_ID_HEVC }}, // Codec_HEVC
};

const codec_config_t* get_codec_config(LAVVideoCodec codec)
{
    codec_config_t* config = &m_codec_config[codec];

    const AVCodecDescriptor* desc = avcodec_descriptor_get(config->codecs[0]);
    if (desc) {
        if (!config->name) {
            config->name = desc->name;
        }

        if (!config->description) {
            config->description = desc->long_name;
        }
    }

    return &m_codec_config[codec];
}

int flip_plane(BYTE* buffer, int stride, int height)
{
    BYTE* line_buffer = (BYTE*)av_malloc(stride);
    BYTE* cur_front = buffer;
    BYTE* cur_back = buffer + (stride * (height - 1));
    height /= 2;
    for (int i = 0; i < height; i++) {
        memcpy(line_buffer, cur_front, stride);
        memcpy(cur_front, cur_back, stride);
        memcpy(cur_back, line_buffer, stride);
        cur_front += stride;
        cur_back -= stride;
    }
    av_freep(&line_buffer);
    return 0;
}

void fillDXVAExtFormat(DXVA2_ExtendedFormat& fmt, int range, int primaries, int matrix, int transfer, int chroma_sample_location, bool bClear)
{
    if (bClear)
        fmt.value = 0;

    if (range != -1)
        fmt.NominalRange = range ? DXVA2_NominalRange_0_255 : DXVA2_NominalRange_16_235;

    // Color Primaries
    switch (primaries) {
    case AVCOL_PRI_BT709:
        fmt.VideoPrimaries = DXVA2_VideoPrimaries_BT709;
        break;
    case AVCOL_PRI_BT470M:
        fmt.VideoPrimaries = DXVA2_VideoPrimaries_BT470_2_SysM;
        break;
    case AVCOL_PRI_BT470BG:
        fmt.VideoPrimaries = DXVA2_VideoPrimaries_BT470_2_SysBG;
        break;
    case AVCOL_PRI_SMPTE170M:
        fmt.VideoPrimaries = DXVA2_VideoPrimaries_SMPTE170M;
        break;
    case AVCOL_PRI_SMPTE240M:
        fmt.VideoPrimaries = DXVA2_VideoPrimaries_SMPTE240M;
        break;
        // Values from newer Windows SDK (MediaFoundation)
    case AVCOL_PRI_BT2020:
        fmt.VideoPrimaries = (DXVA2_VideoPrimaries)9;
        break;
    case AVCOL_PRI_SMPTE428:
        // XYZ
        fmt.VideoPrimaries = (DXVA2_VideoPrimaries)10;
        break;
    case AVCOL_PRI_SMPTE431:
        // DCI-P3
        fmt.VideoPrimaries = (DXVA2_VideoPrimaries)11;
        break;
    }

    // Color Space / Transfer Matrix
    switch (matrix) {
    case AVCOL_SPC_BT709:
        fmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT709;
        break;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        fmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_BT601;
        break;
    case AVCOL_SPC_SMPTE240M:
        fmt.VideoTransferMatrix = DXVA2_VideoTransferMatrix_SMPTE240M;
        break;
        // Values from newer Windows SDK (MediaFoundation)
    case AVCOL_SPC_BT2020_CL:
    case AVCOL_SPC_BT2020_NCL:
        fmt.VideoTransferMatrix = (DXVA2_VideoTransferMatrix)4;
        break;
        // Custom values, not official standard, but understood by madVR
    case AVCOL_SPC_FCC:
        fmt.VideoTransferMatrix = (DXVA2_VideoTransferMatrix)6;
        break;
    case AVCOL_SPC_YCGCO:
        fmt.VideoTransferMatrix = (DXVA2_VideoTransferMatrix)7;
        break;
    }

    // Color Transfer Function
    switch (transfer) {
    case AVCOL_TRC_BT709:
    case AVCOL_TRC_SMPTE170M:
    case AVCOL_TRC_BT2020_10:
    case AVCOL_TRC_BT2020_12:
        fmt.VideoTransferFunction = DXVA2_VideoTransFunc_709;
        break;
    case AVCOL_TRC_GAMMA22:
        fmt.VideoTransferFunction = DXVA2_VideoTransFunc_22;
        break;
    case AVCOL_TRC_GAMMA28:
        fmt.VideoTransferFunction = DXVA2_VideoTransFunc_28;
        break;
    case AVCOL_TRC_SMPTE240M:
        fmt.VideoTransferFunction = DXVA2_VideoTransFunc_240M;
        break;
    case AVCOL_TRC_LOG:
        fmt.VideoTransferFunction = MFVideoTransFunc_Log_100;
        break;
    case AVCOL_TRC_LOG_SQRT:
        fmt.VideoTransferFunction = MFVideoTransFunc_Log_316;
        break;
        // Values from newer Windows SDK (MediaFoundation)
    case AVCOL_TRC_SMPTEST2084:
        fmt.VideoTransferFunction = 15;
        break;
    case AVCOL_TRC_ARIB_STD_B67: // HLG
        fmt.VideoTransferFunction = 16;
        break;
    }

    // Chroma location
    switch (chroma_sample_location) {
    case AVCHROMA_LOC_LEFT:
        fmt.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG2;
        break;
    case AVCHROMA_LOC_CENTER:
        fmt.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG1;
        break;
    case AVCHROMA_LOC_TOPLEFT:
        fmt.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_Cosited;
        break;
    }
}