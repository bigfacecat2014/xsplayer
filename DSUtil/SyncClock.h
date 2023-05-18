/*
 * (C) 2010-2013, 2016 see Authors.txt
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

const IID IID_ISyncClock = { 0xa62888fb, 0x8e37, 0x44d2, {0x88, 0x50, 0xb3, 0xe3, 0xf2, 0xc1, 0x16, 0x9f} };

MIDL_INTERFACE("A62888FB-8E37-44d2-8850-B3E3F2C1169F")
ISyncClock:
public IUnknown{
public:
    STDMETHOD(AdjustClock)(double adjustment) PURE;
    STDMETHOD(SetBias)(double bias) PURE;
    STDMETHOD(GetBias)(double* bias) PURE;
    STDMETHOD(GetStartTime)(REFERENCE_TIME* startTime);
};

class CSyncClockFilter;

class CSyncClock: public CBaseReferenceClock
{
    friend class CSyncClockFilter;
public:
    CSyncClock(LPUNKNOWN pUnk, HRESULT* phr);

    REFERENCE_TIME GetPrivateTime();
    IUnknown* pUnk() { return static_cast<IUnknown*>(static_cast<IReferenceClock*>(this)); }
    double adjustment; // For adjusting speed temporarily
    double bias; // For changing speed permanently

private:
    REFERENCE_TIME m_rtPrivateTime;
    LONGLONG m_llPerfFrequency;
    REFERENCE_TIME m_rtPrevTime;
    CCritSec m_csClock;
    IReferenceClock* m_pCurrentRefClock;
    IReferenceClock* m_pPrevRefClock;
    REFERENCE_TIME GetTicks100ns();
};

class __declspec(uuid("57797fe5-ee9b-4408-98a9-20b134e7e8f0"))
    CSyncClockFilter: public ISyncClock, public CBaseFilter
{
public:
    CSyncClockFilter(LPUNKNOWN pUnk, HRESULT* phr);
    virtual ~CSyncClockFilter();

    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

    // ISyncClock
    STDMETHODIMP AdjustClock(double adjustment);
    STDMETHODIMP SetBias(double bias);
    STDMETHODIMP GetBias(double* bias);
    STDMETHODIMP GetStartTime(REFERENCE_TIME* startTime);

    // CBaseFilter methods
    int GetPinCount();
    CBasePin* GetPin(int iPin);

private:
    CSyncClock m_Clock;
    CCritSec m_Lock;
};
