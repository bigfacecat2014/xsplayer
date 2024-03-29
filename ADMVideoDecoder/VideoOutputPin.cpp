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
#include "VideoOutputPin.h"

CVideoOutputPin::CVideoOutputPin(LPCTSTR pObjectName, CLAVVideo* pFilter, HRESULT* phr, LPCWSTR pName)
    : CTransformOutputPin(pObjectName, (CTransformFilter*)pFilter, phr, pName)
    , m_pFilter(pFilter)
{
}

CVideoOutputPin::~CVideoOutputPin()
{
}

HRESULT CVideoOutputPin::InitAllocator(IMemAllocator** ppAlloc)
{
    HRESULT hr = S_FALSE;

    hr = m_pFilter->m_Decoder.InitAllocator(ppAlloc);
    if (hr != S_OK)
        hr = __super::InitAllocator(ppAlloc);

    return hr;
}

HRESULT CVideoOutputPin::GetMediaType(int iPosition, __inout CMediaType* pMediaType)
{
    if (iPosition < 0) {
        return E_INVALIDARG;
    }
    if (iPosition > 1) {
        return VFW_S_NO_MORE_ITEMS;
    }
    return __super::GetMediaType(iPosition, pMediaType);
}

HRESULT CVideoOutputPin::CheckMediaType(const CMediaType* pmtOut)
{
    return __super::CheckMediaType(pmtOut);
}
