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
#include "VideoInputPin.h"
#include "ILAVDynamicAllocator.h"

CVideoInputPin::CVideoInputPin(TCHAR* pObjectName, CLAVVideo* pFilter, HRESULT* phr, LPWSTR pName)
    : CDeCSSTransformInputPin(pObjectName, pFilter, phr, pName)
    , m_pFilter(pFilter)
{
}

STDMETHODIMP CVideoInputPin::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);

    return
        QI(IPinSegmentEx)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

STDMETHODIMP CVideoInputPin::NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly)
{
    HRESULT hr = __super::NotifyAllocator(pAllocator, bReadOnly);

    m_bDynamicAllocator = FALSE;
    if (SUCCEEDED(hr) && pAllocator) {
        ILAVDynamicAllocator* pDynamicAllocator = nullptr;
        if (SUCCEEDED(pAllocator->QueryInterface(&pDynamicAllocator))) {
            m_bDynamicAllocator = pDynamicAllocator->IsDynamicAllocator();
        }
        SafeRelease(&pDynamicAllocator);
    }

    return hr;
}

STDMETHODIMP CVideoInputPin::EndOfSegment()
{
    CAutoLock lck(&m_pFilter->m_csReceive);
    HRESULT hr = CheckStreaming();
    if (S_OK == hr) {
        hr = m_pFilter->EndOfSegment();
    }

    return hr;
}

// 由于我们没有采用DShow的注册表登记机制，因此没有将Filter以及其Pin类型描述信息注册到一个映射表中。
// 主要关键字为：IID_IFilterMapper和AMovieSetupRegisterFilter
// 没有这个机制，我们就要重载此函数来支持按照媒体主类型类匹配输入和输出Pin。
// 区分出音视和视频接口类型，防止出现将RtspSource的音频输出Pin连接到LAVVideo的情况。
HRESULT CVideoInputPin::GetMediaType(int iPosition, __inout CMediaType* pMediaType)
{
    if (iPosition < 0) {
        return E_INVALIDARG;
    }
    if (iPosition > 0) {
        return VFW_S_NO_MORE_ITEMS;
    }
    SetH265VideoType(*pMediaType, 1, 1, 25);
    return S_OK;
}

HRESULT CVideoInputPin::CheckMediaType(const CMediaType* pmtOut)
{
    return __super::CheckMediaType(pmtOut);
}
