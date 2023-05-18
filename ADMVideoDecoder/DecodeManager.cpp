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
#include "DecodeManager.h"
#include "LAVVideo.h"

CDecodeManager::CDecodeManager(CLAVVideo* pLAVVideo)
    : m_pLAVVideo(pLAVVideo)
{
    WCHAR fileName[1024];
    GetModuleFileName(nullptr, fileName, 1024);
    m_processName = PathFindFileName(fileName);
}

CDecodeManager::~CDecodeManager(void)
{
    Close();
}

STDMETHODIMP CDecodeManager::Close()
{
    CAutoLock decoderLock(this);
    SAFE_DELETE(m_pDecoder);

    return S_OK;
}

// TODO: 把软解和硬解的创建代码路径和数据处理代码流程全部隔离开，逻辑不可以混合出现在一个函数中。
STDMETHODIMP CDecodeManager::CreateDecoder(const CMediaType* pmt, AVCodecID codec)
{
    CAutoLock decoderLock(this);

    DbgLog((LOG_TRACE, 10, L"CDecodeThread::CreateDecoder(): Creating new decoder for codec %S", avcodec_get_name(codec)));
    HRESULT hr = S_OK;
    BOOL bWMV9 = FALSE;

    BITMAPINFOHEADER* pBMI = nullptr;
    videoFormatTypeHandler(*pmt, &pBMI);

    SAFE_DELETE(m_pDecoder);

    // Fallback for software
    DbgLog((LOG_TRACE, 10, L"-> No HW Codec, using Software"));
    ASSERT(codec == AV_CODEC_ID_HEVC); // add by yxs
    m_pDecoder = CreateDecoderAVCodec(); // create FFMPEG software codec
    DbgLog((LOG_TRACE, 10, L"-> Created Codec '%s'", m_pDecoder->GetDecoderName()));

    hr = m_pDecoder->InitInterfaces(static_cast<ILAVVideoConfig*>(m_pLAVVideo), static_cast<ILAVVideoCallback*>(m_pLAVVideo));
    if (FAILED(hr)) {
        DbgLog((LOG_TRACE, 10, L"-> Init Interfaces failed (hr: 0x%x)", hr));
        goto done;
    }

    hr = m_pDecoder->InitDecoder(codec, pmt);
    if (FAILED(hr)) {
        DbgLog((LOG_TRACE, 10, L"-> Init Decoder failed (hr: 0x%x)", hr));
        goto done;
    }

done:
    if (FAILED(hr)) {
        SAFE_DELETE(m_pDecoder);
        return hr;
    }
    m_Codec = codec;
    return hr;
}

STDMETHODIMP CDecodeManager::Decode(IMediaSample* pSample)
{
    CAutoLock decoderLock(this);
    HRESULT hr = S_OK;

    if (!m_pDecoder)
        return E_UNEXPECTED;

    hr = m_pDecoder->Decode(pSample);

    return hr;
}

STDMETHODIMP CDecodeManager::Flush()
{
    CAutoLock decoderLock(this);

    if (!m_pDecoder)
        return E_UNEXPECTED;

    return m_pDecoder->Flush();
}

STDMETHODIMP CDecodeManager::EndOfStream()
{
    CAutoLock decoderLock(this);

    if (!m_pDecoder)
        return E_UNEXPECTED;

    return m_pDecoder->EndOfStream();
}

STDMETHODIMP CDecodeManager::InitAllocator(IMemAllocator** ppAlloc)
{
    CAutoLock decoderLock(this);

    if (!m_pDecoder)
        return E_UNEXPECTED;

    return m_pDecoder->InitAllocator(ppAlloc);
}

STDMETHODIMP CDecodeManager::PostConnect(IPin* pPin)
{
    CAutoLock decoderLock(this);
    HRESULT hr = S_OK;
    if (m_pDecoder) {
        hr = m_pDecoder->PostConnect(pPin);
        if (FAILED(hr)) {
            CMediaType& mt = m_pLAVVideo->GetInputMediaType();
            hr = CreateDecoder(&mt, m_Codec);
        }
    }
    return hr;
}

STDMETHODIMP CDecodeManager::BreakConnect()
{
    CAutoLock decoderLock(this);

    if (!m_pDecoder)
        return E_UNEXPECTED;

    return m_pDecoder->BreakConnect();
}
