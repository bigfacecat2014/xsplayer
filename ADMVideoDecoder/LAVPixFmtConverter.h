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

#include "ILAVVideo.h"
#include "decoders/LAVDecoder.h"

extern DECLARE_ALIGNED(16, const uint16_t, dither_8x8_256)[8][8];

// Important, when adding new pixel formats, they need to be added in 
// LAVPixFmtConverter.cpp as well to the format descriptors
struct LAVOutPixFmtDesc {
    GUID subtype;
    LAVOutPixFmts outPixFmt; // 映射到像素格式枚举值
    int bpp;
    int codedbytes;
    int planes;
    int planeHeight[4];
    int planeWidth[4];
};

struct RGBCoeffs {
    __m128i Ysub;
    __m128i CbCr_center;
    __m128i rgb_add;
    __m128i cy;
    __m128i cR_Cr;
    __m128i cG_Cb_cG_Cr;
    __m128i cB_Cb;
};

typedef int(__stdcall* YUVRGBConversionFunc)(const uint8_t* srcY, const uint8_t* srcU, const uint8_t* srcV, uint8_t* dst, int width, int height, ptrdiff_t srcStrideY, ptrdiff_t srcStrideUV, ptrdiff_t dstStride, ptrdiff_t sliceYStart, ptrdiff_t sliceYEnd, const RGBCoeffs* coeffs, const uint16_t* dithers);

extern LAVOutPixFmtDesc g_lav_out_pixfmt_desc[];

class CLAVPixFmtConverter
{
public:
    CLAVPixFmtConverter();
    ~CLAVPixFmtConverter();

    void SetSettings(ILAVVideoConfig* pSettings) { m_pConfig = pSettings; }

    BOOL SetInputFmt(enum LAVPixelFormat pixfmt, int bpp) {
        if (m_InputPixFmt != pixfmt || m_InBpp != bpp) {
            m_InputPixFmt = pixfmt;
            m_InBpp = bpp;
            SelectConvertFunction();
            return TRUE;
        }
        return FALSE;
    }
    HRESULT SetOutputPixFmt(enum LAVOutPixFmts pix_fmt) {
        m_OutputPixFmt = pix_fmt;
        SelectConvertFunction();
        return S_OK; 
    }

    LAVOutPixFmts GetOutputPixFmt() {
        return m_OutputPixFmt;
    }

    LAVOutPixFmts GetOutPixFmtBySubtype(const GUID* guid);

    // 首选输出格式，放在输入格式的输出格式列表的第一个下标处。
    LAVOutPixFmts GetPreferredOutPixFmt() {
        return GetFilteredFormat(0);
    }

    void SetColorProps(DXVA2_ExtendedFormat props, int RGBOutputRange) {
        if (props.value != m_ColorProps.value || swsOutputRange != RGBOutputRange) {
            m_ColorProps = props;
            swsOutputRange = RGBOutputRange;
        }
    }

    int GetNumMediaTypes() {
        return GetFilteredFormatCount();
    }

    void GetMediaType(CMediaType* mt, int index, LONG width, LONG height,
        DWORD dwAspectX, DWORD dwAspectY, REFERENCE_TIME rtAvgTime);
    BOOL IsAllowedSubtype(const GUID* guid);

    HRESULT Convert(const uint8_t* const src[4], const ptrdiff_t srcStride[4],
        uint8_t* dst, int width, int height, ptrdiff_t dstStride, int planeHeight);

    BOOL IsRGBConverterActive() { return m_bRGBConverter; }
    DWORD GetImageSize(int width, int height, LAVOutPixFmts pixFmt = LAVOutPixFmt_None);

private:
    AVPixelFormat GetFFInput() {
        return getFFPixelFormatFromLAV(m_InputPixFmt, m_InBpp);
    }

    int GetFilteredFormatCount() {
        return LAVOutPixFmt_NB;
    }

    LAVOutPixFmts GetFilteredFormat(int index);

    void SelectConvertFunction();
    void ChangeStride(const uint8_t* src, ptrdiff_t srcStride, uint8_t* dst, ptrdiff_t dstStride, int width, int height, int planeHeight, LAVOutPixFmts format);

    // 一堆像素转换函数，吾仅需一种。
    HRESULT convert_yuv_to_rgb(const uint8_t* const src[4], const ptrdiff_t srcStride[4], uint8_t* dst[4], const ptrdiff_t dstStride[4], int width, int height, LAVPixelFormat inputFormat, int bpp, LAVOutPixFmts outputFormat);

    const RGBCoeffs* getRGBCoeffs(int width, int height);
    const uint16_t* GetRandomDitherCoeffs(int height, int coeffs, int bits, int line);

private:
    LAVPixelFormat m_InputPixFmt = LAVPixFmt_None;
    LAVOutPixFmts m_OutputPixFmt = LAVOutPixFmt_YV12;
    int m_InBpp = 0;
    int swsWidth = 0;
    int swsHeight = 0;
    int swsOutputRange = 0;
    DXVA2_ExtendedFormat m_ColorProps;
    ptrdiff_t m_RequiredAlignment = 0;
    size_t  m_nAlignedBufferSize = 0;
    uint8_t* m_pAlignedBuffer = nullptr;

    ILAVVideoConfig* m_pConfig = nullptr;
    RGBCoeffs* m_rgbCoeffs = nullptr;
    BOOL m_bRGBConverter = FALSE;

    uint16_t* m_pRandomDithers = nullptr;
    int m_ditherWidth = 0;
    int m_ditherHeight = 0;
    int m_ditherBits = 0;
};
