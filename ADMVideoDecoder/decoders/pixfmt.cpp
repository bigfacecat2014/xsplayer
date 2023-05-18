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
#include "LAVDecoder.h"

LAVPixFmtDesc getPixelFormatDesc(LAVPixelFormat pixFmt)
{
    //
    //struct LAVPixFmtDesc {
    //    int codedbytes;        ///< coded byte per pixel in one plane (for packed and multibyte formats)
    //    int planes;            ///< number of planes
    //    int planeWidth[4];     ///< log2 width factor
    //    int planeHeight[4];    ///< log2 height factor
    //};
    //
    static LAVPixFmtDesc s_lav_pixfmt_desc[] = {
        { 1, 3, { 1, 2, 2 }, { 1, 2, 2 } },       ///< LAVPixFmt_YUV420
        { 4, 1, { 1 },       { 1 }       },       ///< LAVPixFmt_RGB32
    };
    return s_lav_pixfmt_desc[pixFmt];
}

static struct {
    LAVPixelFormat pixfmt;
    AVPixelFormat  ffpixfmt;
} lav_ff_pixfmt_map[] = {
  { LAVPixFmt_YUV420, AV_PIX_FMT_YUV420P },
  { LAVPixFmt_RGB32,  AV_PIX_FMT_BGRA    },
};

AVPixelFormat getFFPixelFormatFromLAV(LAVPixelFormat pixFmt, int bpp)
{
    AVPixelFormat fmt = AV_PIX_FMT_NONE;
    for (int i = 0; i < countof(lav_ff_pixfmt_map); i++) {
        if (lav_ff_pixfmt_map[i].pixfmt == pixFmt) {
            fmt = lav_ff_pixfmt_map[i].ffpixfmt;
            break;
        }
    }
    return fmt;
}

static void free_buffers(struct LAVFrame* pFrame)
{
    _aligned_free(pFrame->data[0]);
    _aligned_free(pFrame->data[1]);
    _aligned_free(pFrame->data[2]);
    _aligned_free(pFrame->data[3]);
    memset(pFrame->data, 0, sizeof(pFrame->data));

    _aligned_free(pFrame->stereo[0]);
    _aligned_free(pFrame->stereo[1]);
    _aligned_free(pFrame->stereo[2]);
    _aligned_free(pFrame->stereo[3]);
    memset(pFrame->stereo, 0, sizeof(pFrame->stereo));
}

HRESULT AllocLAVFrameBuffers(LAVFrame* pFrame, ptrdiff_t stride)
{
    LAVPixFmtDesc desc = getPixelFormatDesc(pFrame->format);

    if (stride < pFrame->width) {
        // Ensure alignment of at least 32 on all planes
        stride = FFALIGN(pFrame->width, 64);
    }

    stride *= desc.codedbytes;

    int alignedHeight = FFALIGN(pFrame->height, 2);

    memset(pFrame->data, 0, sizeof(pFrame->data));
    memset(pFrame->stereo, 0, sizeof(pFrame->stereo));
    memset(pFrame->stride, 0, sizeof(pFrame->stride));
    for (int plane = 0; plane < desc.planes; plane++) {
        ptrdiff_t planeStride = stride / desc.planeWidth[plane];
        size_t size = planeStride * (alignedHeight / desc.planeHeight[plane]);
        pFrame->data[plane] = (BYTE*)_aligned_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE, 64);
        if (pFrame->data[plane] == nullptr) {
            free_buffers(pFrame);
            return E_OUTOFMEMORY;
        }
        pFrame->stride[plane] = planeStride;
    }

    if (pFrame->flags & LAV_FRAME_FLAG_MVC) {
        for (int plane = 0; plane < desc.planes; plane++) {
            size_t size = pFrame->stride[plane] * (alignedHeight / desc.planeHeight[plane]);
            pFrame->stereo[plane] = (BYTE*)_aligned_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE, 64);
            if (pFrame->stereo[plane] == nullptr) {
                free_buffers(pFrame);
                return E_OUTOFMEMORY;
            }
        }
    }

    pFrame->destruct = &free_buffers;
    pFrame->flags |= LAV_FRAME_FLAG_BUFFER_MODIFY;

    return S_OK;
}

HRESULT FreeLAVFrameBuffers(LAVFrame* pFrame)
{
    CheckPointer(pFrame, E_POINTER);

    if (pFrame->destruct) {
        pFrame->destruct(pFrame);
        pFrame->destruct = nullptr;
        pFrame->priv_data = nullptr;
    }
    memset(pFrame->data, 0, sizeof(pFrame->data));
    memset(pFrame->stereo, 0, sizeof(pFrame->stereo));
    memset(pFrame->stride, 0, sizeof(pFrame->stride));

    for (int i = 0; i < pFrame->side_data_count; i++)
    {
        SAFE_CO_FREE(pFrame->side_data[i].data);
    }
    SAFE_CO_FREE(pFrame->side_data);
    pFrame->side_data_count = 0;

    return S_OK;
}

HRESULT CopyLAVFrame(LAVFrame* pSrc, LAVFrame** ppDst)
{
    *ppDst = (LAVFrame*)CoTaskMemAlloc(sizeof(LAVFrame));
    if (!*ppDst) return E_OUTOFMEMORY;
    **ppDst = *pSrc;

    (*ppDst)->destruct = nullptr;
    (*ppDst)->priv_data = nullptr;

    HRESULT hr = AllocLAVFrameBuffers(*ppDst);
    if (FAILED(hr))
        return hr;

    LAVPixFmtDesc desc = getPixelFormatDesc(pSrc->format);
    for (int plane = 0; plane < desc.planes; plane++) {
        size_t linesize = (pSrc->width / desc.planeWidth[plane]) * desc.codedbytes;
        BYTE* dst = (*ppDst)->data[plane];
        BYTE* src = pSrc->data[plane];
        if (!dst || !src)
            return E_FAIL;
        for (int i = 0; i < (pSrc->height / desc.planeHeight[plane]); i++) {
            memcpy(dst, src, linesize);
            dst += (*ppDst)->stride[plane];
            src += pSrc->stride[plane];
        }

        if (pSrc->flags & LAV_FRAME_FLAG_MVC) {
            dst = (*ppDst)->stereo[plane];
            src = pSrc->stereo[plane];
            if (!dst || !src)
                return E_FAIL;
            for (int i = 0; i < (pSrc->height / desc.planeHeight[plane]); i++) {
                memcpy(dst, src, linesize);
                dst += (*ppDst)->stride[plane];
                src += pSrc->stride[plane];
            }
        }
    }

    (*ppDst)->side_data = nullptr;
    (*ppDst)->side_data_count = 0;
    for (int i = 0; i < pSrc->side_data_count; i++)
    {
        BYTE* p = AddLAVFrameSideData(*ppDst, pSrc->side_data[i].guidType, pSrc->side_data[i].size);
        if (p)
            memcpy(p, pSrc->side_data[i].data, pSrc->side_data[i].size);
    }

    return S_OK;
}

HRESULT CopyLAVFrameInPlace(LAVFrame* pFrame)
{
    LAVFrame* tmpFrame = nullptr;
    CopyLAVFrame(pFrame, &tmpFrame);
    FreeLAVFrameBuffers(pFrame);
    *pFrame = *tmpFrame;
    SAFE_CO_FREE(tmpFrame);
    return S_OK;
}

BYTE* AddLAVFrameSideData(LAVFrame* pFrame, GUID guidType, size_t size)
{
    BYTE* ptr = (BYTE*)CoTaskMemRealloc(pFrame->side_data, sizeof(LAVFrameSideData) * (pFrame->side_data_count + 1));
    if (!ptr)
        return NULL;

    pFrame->side_data = (LAVFrameSideData*)ptr;
    pFrame->side_data[pFrame->side_data_count].guidType = guidType;
    pFrame->side_data[pFrame->side_data_count].data = (BYTE*)CoTaskMemAlloc(size);
    pFrame->side_data[pFrame->side_data_count].size = size;

    if (!pFrame->side_data[pFrame->side_data_count].data)
        return NULL;

    memset(pFrame->side_data[pFrame->side_data_count].data, 0, size);

    pFrame->side_data_count++;

    return pFrame->side_data[pFrame->side_data_count - 1].data;
}

BYTE* GetLAVFrameSideData(LAVFrame* pFrame, GUID guidType, size_t* pSize)
{
    if (!pFrame || pFrame->side_data_count == 0)
        return NULL;

    ASSERT(pSize);

    for (int i = 0; i < pFrame->side_data_count; i++)
    {
        if (pFrame->side_data[i].guidType == guidType) {
            *pSize = pFrame->side_data[i].size;
            return pFrame->side_data[i].data;
        }
    }

    return NULL;
}
