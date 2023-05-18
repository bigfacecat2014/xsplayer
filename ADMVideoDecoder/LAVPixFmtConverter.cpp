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
#include "LAVPixFmtConverter.h"
#include "Media.h"

#include "decoders/LAVDecoder.h"

#include <MMReg.h>
#include "moreuuids.h"

#include <time.h>
#include "rand_sse.h"

//
//struct LAVOutPixFmtDesc {
//    GUID subtype;
//    LAVOutPixFmts outPixFmt; // 映射到像素格式枚举值
//    int bpp;
//    int codedbytes;
//    int planes;
//    int planeHeight[4]; // 高度的分母
//    int planeWidth[4];  // 宽度的分母
//};
//
LAVOutPixFmtDesc g_lav_out_pixfmt_desc[] = {
  { MEDIASUBTYPE_YV12, LAVOutPixFmt_YV12, 12, 1, 3, { 1, 2, 2 }, { 1, 2, 2 } },
  { MEDIASUBTYPE_RGB32, LAVOutPixFmt_RGB32, 32, 4, 1, { 1, 1, 1 }, { 1, 1, 1 } },
};

struct LAV_INOUT_PIXFMT_MAP {
    LAVPixelFormat in_pix_fmt;
    LAVOutPixFmts out_pix_fmt_list[LAVOutPixFmt_NB + 1];
};

// 输入格式对应的可用的输出格式优先级列表映射表
static LAV_INOUT_PIXFMT_MAP s_lav_in_out_pixfmt_map[] = {
    // 为任意输入格式定义的首选输出格式，通过固定[0].out_pix_fmt_list[index]来访问
    { LAVPixFmt_None, { LAVOutPixFmt_YV12 } },

    // 4:2:0
    { LAVPixFmt_YUV420, { LAVOutPixFmt_YV12, LAVOutPixFmt_RGB32 } },
};

static LAV_INOUT_PIXFMT_MAP* lookupInOutFormatMap(LAVPixelFormat informat)
{
    // 精确查找为输入格式informat设定的优化输出格式优先级列表。
    for (int i = 0; i < countof(s_lav_in_out_pixfmt_map); ++i) {
        if (s_lav_in_out_pixfmt_map[i].in_pix_fmt == informat) {
            return &s_lav_in_out_pixfmt_map[i]; // 为informat格式精确定义的转换输出格式列表
        }
    }
    return nullptr;
}

CLAVPixFmtConverter::CLAVPixFmtConverter()
{
    ZeroMemory(&m_ColorProps, sizeof(m_ColorProps));
}

CLAVPixFmtConverter::~CLAVPixFmtConverter()
{
    av_freep(&m_pAlignedBuffer);
}

LAVOutPixFmts CLAVPixFmtConverter::GetOutPixFmtBySubtype(const GUID* guid)
{
    for (int i = 0; i < countof(g_lav_out_pixfmt_desc); ++i) {
        if (g_lav_out_pixfmt_desc[i].subtype == *guid) {
            return g_lav_out_pixfmt_desc[i].outPixFmt;
        }
    }
    return LAVOutPixFmt_None;
}

LAVOutPixFmts CLAVPixFmtConverter::GetFilteredFormat(int index)
{
    return s_lav_in_out_pixfmt_map[0].out_pix_fmt_list[index];
}

void CLAVPixFmtConverter::GetMediaType(CMediaType* mt, int index, LONG width, LONG height,
    DWORD dwAspectX, DWORD dwAspectY, REFERENCE_TIME rtAvgTime)
{
    if (index < 0 || index >= GetFilteredFormatCount())
      index = 0;

    LAVOutPixFmts pixFmt = LAVOutPixFmt_RGB32;// GetFilteredFormat(index);

    GUID subType = MEDIASUBTYPE_RGB32;// g_lav_out_pixfmt_desc[pixFmt].subtype;

    mt->SetType(&MEDIATYPE_Video);
    mt->SetSubtype(&subType);
    mt->SetFormatType(&FORMAT_VideoInfo2);
    mt->SetTemporalCompression(0);

    VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)mt->ReallocFormatBuffer(sizeof(VIDEOINFOHEADER2));
    if (vih2 != nullptr)
    {
        memset(vih2, 0, sizeof(VIDEOINFOHEADER2));

        // Validate the Aspect Ratio - an AR of 0 crashes VMR-9
        if (dwAspectX == 0 || dwAspectY == 0) {
            dwAspectX = width;
            dwAspectY = abs(height);
        }

        // Always reduce the AR to the smalles fraction
        int dwX = 0, dwY = 0;
        av_reduce(&dwX, &dwY, dwAspectX, dwAspectY, max(dwAspectX, dwAspectY));

        vih2->rcSource.right = vih2->rcTarget.right = width;
        vih2->rcSource.bottom = vih2->rcTarget.bottom = abs(height);
        vih2->AvgTimePerFrame = rtAvgTime;
        vih2->dwPictAspectRatioX = dwX;
        vih2->dwPictAspectRatioY = dwY;

        BITMAPINFOHEADER& bih = vih2->bmiHeader;
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = width;
        bih.biHeight = height;
        bih.biBitCount = g_lav_out_pixfmt_desc[pixFmt].bpp;
        bih.biPlanes = 1;
        bih.biSizeImage = GetImageSize(width, abs(height), pixFmt);
        bih.biCompression = subType.Data1;
        if (subType == MEDIASUBTYPE_RGB32) {
            bih.biCompression = BI_RGB;
        }
        mt->SetSampleSize(bih.biSizeImage);
    }
}

DWORD CLAVPixFmtConverter::GetImageSize(int width, int height, LAVOutPixFmts pixFmt)
{
    if (pixFmt == LAVOutPixFmt_None)
        pixFmt = m_OutputPixFmt;
    return (width * height * g_lav_out_pixfmt_desc[pixFmt].bpp) >> 3;
}

BOOL CLAVPixFmtConverter::IsAllowedSubtype(const GUID* guid)
{
    for (int i = 0; i < GetFilteredFormatCount(); ++i) {
        if (g_lav_out_pixfmt_desc[GetFilteredFormat(i)].subtype == *guid)
            return TRUE;
    }

    return FALSE;
}

#define OUTPUT_RGB (m_OutputPixFmt == LAVOutPixFmt_RGB32 || m_OutputPixFmt == LAVOutPixFmt_RGB24)

void CLAVPixFmtConverter::SelectConvertFunction()
{
    m_RequiredAlignment = 16;
    m_bRGBConverter = FALSE;

    int cpu = av_get_cpu_flags();
    // input format may come from Capture device source / RTSP source / Multi media stream file source.
    if (m_InputPixFmt == LAVPixFmt_YUV420 && m_OutputPixFmt == LAVOutPixFmt_RGB32) {
        m_RequiredAlignment = 4;
        m_bRGBConverter = TRUE;
    }
    else {
        // 不需要转换
    }
}

HRESULT CLAVPixFmtConverter::Convert(const BYTE* const src[4], const ptrdiff_t srcStride[4],
    uint8_t* dst, int width, int height, ptrdiff_t dstStride, int planeHeight)
{
    HRESULT hr = S_OK;

    uint8_t* out = dst;
    ptrdiff_t outStride = dstStride, i;
    planeHeight = max(height, planeHeight);
    LAVOutPixFmtDesc& desc = g_lav_out_pixfmt_desc[m_OutputPixFmt];

    // Check if we have proper pixel alignment and the dst memory is actually aligned
    if (m_RequiredAlignment && (FFALIGN(dstStride, m_RequiredAlignment) != dstStride || ((uintptr_t)dst % 16u))) {
        outStride = FFALIGN(dstStride, m_RequiredAlignment);
        size_t requiredSize = (outStride * planeHeight * desc.bpp) >> 3;
        if (requiredSize > m_nAlignedBufferSize || !m_pAlignedBuffer) {
            DbgLog((LOG_TRACE, 10, L"::Convert(): Conversion requires a bigger stride (need: %d, have: %d), allocating buffer...", outStride, dstStride));
            av_freep(&m_pAlignedBuffer);
            m_pAlignedBuffer = (uint8_t*)av_malloc(requiredSize + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!m_pAlignedBuffer) {
                return E_FAIL;
            }
            m_nAlignedBufferSize = requiredSize;
        }
        out = m_pAlignedBuffer;
    }

    uint8_t* dstArray[4] = { 0 };
    ptrdiff_t dstStrideArray[4] = { 0 };
    ptrdiff_t byteStride = outStride * desc.codedbytes;

    // 0号平面
    dstArray[0] = out; 
    dstStrideArray[0] = byteStride;
    // {1, 2}号平面
    for (i = 1; i < desc.planes; ++i) {
        dstArray[i] = dstArray[i - 1] + dstStrideArray[i - 1] * (planeHeight / desc.planeHeight[i - 1]);
        dstStrideArray[i] = byteStride / desc.planeWidth[i];
    }

    if (m_bRGBConverter) {
        hr = convert_yuv_to_rgb(src, srcStride, dstArray, dstStrideArray, width, height, m_InputPixFmt, m_InBpp, m_OutputPixFmt);
    }

    if (out != dst) {
        ChangeStride(out, outStride, dst, dstStride, width, height, planeHeight, m_OutputPixFmt);
    }

    return hr;
}

void CLAVPixFmtConverter::ChangeStride(const uint8_t* src, ptrdiff_t srcStride, uint8_t* dst, ptrdiff_t dstStride, int width, int height, int planeHeight, LAVOutPixFmts format)
{
    LAVOutPixFmtDesc desc = g_lav_out_pixfmt_desc[format];

    int line = 0;

    // Copy first plane
    const size_t widthBytes = width * desc.codedbytes;
    const ptrdiff_t srcStrideBytes = srcStride * desc.codedbytes;
    const ptrdiff_t dstStrideBytes = dstStride * desc.codedbytes;
    for (line = 0; line < height; ++line) {
        memcpy(dst, src, widthBytes);
        src += srcStrideBytes;
        dst += dstStrideBytes;
    }
    dst += (planeHeight - height) * dstStrideBytes;

    // Copy other planes
    for (int plane = 1; plane < desc.planes; ++plane) {
        const size_t planeWidth = widthBytes / desc.planeWidth[plane];
        const int activePlaneHeight = height / desc.planeHeight[plane];
        const int totalPlaneHeight = planeHeight / desc.planeHeight[plane];
        const ptrdiff_t srcPlaneStride = srcStrideBytes / desc.planeWidth[plane];
        const ptrdiff_t dstPlaneStride = dstStrideBytes / desc.planeWidth[plane];
        for (line = 0; line < activePlaneHeight; ++line) {
            memcpy(dst, src, planeWidth);
            src += srcPlaneStride;
            dst += dstPlaneStride;
        }
        dst += (totalPlaneHeight - activePlaneHeight) * dstPlaneStride;
    }
}

const uint16_t* CLAVPixFmtConverter::GetRandomDitherCoeffs(int height, int coeffs, int bits, int line)
{
    int totalWidth = 8 * coeffs;
    if (!m_pRandomDithers || totalWidth > m_ditherWidth || height > m_ditherHeight || bits != m_ditherBits) {
        if (m_pRandomDithers)
            _aligned_free(m_pRandomDithers);
        m_pRandomDithers = nullptr;
        m_ditherWidth = totalWidth;
        m_ditherHeight = height;
        m_ditherBits = bits;
        m_pRandomDithers = (uint16_t*)_aligned_malloc(m_ditherWidth * m_ditherHeight * 2, 16);
        if (m_pRandomDithers == nullptr)
            return nullptr;

#ifdef _DEBUG
        LARGE_INTEGER frequency, start, end;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        DbgLog((LOG_TRACE, 10, L"Creating dither matrix"));
#endif

        // Seed random number generator
        time_t seed = time(nullptr);
        seed >>= 1;
        srand_sse((unsigned int)seed);

        bits = (1 << bits);
        for (int i = 0; i < m_ditherHeight; i++) {
            uint16_t* ditherline = m_pRandomDithers + (m_ditherWidth * i);
            for (int j = 0; j < m_ditherWidth; j += 4) {
                int rnds[4];
                rand_sse(rnds);
                ditherline[j + 0] = rnds[0] % bits;
                ditherline[j + 1] = rnds[1] % bits;
                ditherline[j + 2] = rnds[2] % bits;
                ditherline[j + 3] = rnds[3] % bits;
            }
        }
#ifdef _DEBUG
        QueryPerformanceCounter(&end);
        double diff = (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
        DbgLog((LOG_TRACE, 10, L"Finished creating dither matrix (took %2.3fms)", diff));
#endif

    }

    if (line < 0 || line >= m_ditherHeight)
        line = rand() % m_ditherHeight;

    return &m_pRandomDithers[line * m_ditherWidth];
}
