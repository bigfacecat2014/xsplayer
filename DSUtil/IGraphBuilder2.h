/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2014, 2016-2017 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

class CMediaType;

interface __declspec(uuid("165BE9D6-0929-4363-9BA3-580D735AA0F6"))
    IGraphBuilder2 :
    public IFilterGraph2
{
    STDMETHOD(IsPinDirection)(IPin* pPin, PIN_DIRECTION dir) PURE;
    STDMETHOD(IsPinConnected)(IPin* pPin) PURE;
    STDMETHOD(ConnectFilter)(IBaseFilter* pBF, IPin* pPinIn) PURE;
    STDMETHOD(ConnectFilter)(IPin* pPinOut, IBaseFilter* pBF) PURE;
    STDMETHOD(ConnectFilterDirect)(IPin* pPinOut, IBaseFilter* pBF, const AM_MEDIA_TYPE* pmt) PURE;
    STDMETHOD(NukeDownstream)(IUnknown* pUnk) PURE;
    STDMETHOD(FindInterface)(REFIID iid, void** ppv, BOOL bRemove) PURE;
    STDMETHOD(AddToROT)() PURE;
    STDMETHOD(RemoveFromROT)() PURE;
};
