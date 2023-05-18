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

#include "decoders/LAVDecoder.h"
#include "DecodeManager.h"
#include "ILAVPinInfo.h"

#include "LAVPixFmtConverter.h"
#include "ILAVVideo.h"
#include "FloatingAverage.h"

#include "ISpecifyPropertyPages2.h"

#include "IMediaSideData.h"

extern "C" {
#include "libavutil/mastering_display_metadata.h"
};

#define LAVC_VIDEO_LOG_FILE L"LAVVideo.txt"
#define DEBUG_FRAME_TIMINGS 0
#define DEBUG_PIXELCONV_TIMINGS 0

typedef struct {
    REFERENCE_TIME rtStart;
    REFERENCE_TIME rtStop;
} TimingCache;

class __declspec(uuid("CA38BA59-F95C-4AC4-B6FF-6D29C267CFCA")) CLAVVideo :
    public CTransformFilter,
    public ILAVVideoConfig,
    public ILAVVideoStatus,
    public ILAVVideoCallback
{
public:
    CLAVVideo(LPUNKNOWN pUnk, HRESULT* phr);
    ~CLAVVideo();

    // IUnknown
    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);

    // ILAVVideoConfig
    STDMETHODIMP SetFormatConfiguration(LAVVideoCodec vCodec, BOOL bEnabled);
    STDMETHODIMP_(BOOL) GetFormatConfiguration(LAVVideoCodec vCodec);
    STDMETHODIMP SetStreamAR(DWORD bStreamAR);
    STDMETHODIMP_(DWORD) GetStreamAR();
    STDMETHODIMP SetPixelFormat(LAVOutPixFmts pixFmt, BOOL bEnabled);
    STDMETHODIMP_(BOOL) GetPixelFormat(LAVOutPixFmts pixFmt);
    STDMETHODIMP SetRGBOutputRange(DWORD dwRange);
    STDMETHODIMP_(DWORD) GetRGBOutputRange();
    STDMETHODIMP SetDitherMode(LAVDitherMode ditherMode);
    STDMETHODIMP_(LAVDitherMode) GetDitherMode();
    STDMETHODIMP SetOutputBufferCount(int count);
    STDMETHODIMP_(int) GetOutputBufferCount();


    // ILAVVideoStatus
    STDMETHODIMP_(const WCHAR*) GetActiveDecoderName() { return m_Decoder.GetDecoderName(); }

    // CTransformFilter
    STDMETHODIMP Stop();

    HRESULT CheckInputType(const CMediaType* mtIn);
    HRESULT CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut);
    HRESULT DecideBufferSize(IMemAllocator* pAllocator, ALLOCATOR_PROPERTIES* pprop);
    HRESULT GetMediaType(int iPosition, CMediaType* pMediaType);

    HRESULT SetMediaType(PIN_DIRECTION dir, const CMediaType* pmt);
    HRESULT EndOfStream();
    HRESULT BeginFlush();
    HRESULT EndFlush();
    HRESULT NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate);
    HRESULT Receive(IMediaSample* pIn);

    HRESULT CheckConnect(PIN_DIRECTION dir, IPin* pPin);
    HRESULT BreakConnect(PIN_DIRECTION dir);
    HRESULT CompleteConnect(PIN_DIRECTION dir, IPin* pReceivePin);

    HRESULT StartStreaming();

    int GetPinCount();
    CBasePin* GetPin(int n);

    // IPinSegmentEx
    HRESULT EndOfSegment();

    // ILAVVideoCallback
    STDMETHODIMP AllocateFrame(LAVFrame** ppFrame);
    STDMETHODIMP ReleaseFrame(LAVFrame** ppFrame);
    STDMETHODIMP Deliver(LAVFrame* pFrame);
    STDMETHODIMP_(DWORD) GetDecodeFlags() { return m_dwDecodeFlags; }
    STDMETHODIMP_(CMediaType&) GetInputMediaType() { return m_pInput->CurrentMediaType(); }
    STDMETHODIMP GetLAVPinInfo(LAVPinInfo& info) { if (m_LAVPinInfoValid) { info = m_LAVPinInfo; return S_OK; } return E_FAIL; }
    STDMETHODIMP_(CBasePin*) GetOutputPin() { return m_pOutput; }
    STDMETHODIMP_(CMediaType&) GetOutputMediaType() { return m_pOutput->CurrentMediaType(); }
    STDMETHODIMP_(LAVFrame*) GetFlushFrame();
    STDMETHODIMP_(BOOL) HasDynamicInputAllocator();

public:
    // Pin Configuration
    const static AMOVIESETUP_MEDIATYPE sudPinTypesIn[];
    const static UINT sudPinTypesInCount;
    const static AMOVIESETUP_MEDIATYPE sudPinTypesOut[];
    const static UINT sudPinTypesOutCount;

private:
    HRESULT CreateDecoder(const CMediaType* pmt);

    HRESULT GetDeliveryBuffer(IMediaSample** ppOut, int width, int height, AVRational ar, DXVA2_ExtendedFormat dxvaExtFormat, REFERENCE_TIME avgFrameDuration);
    HRESULT ReconnectOutput(int width, int height, AVRational ar, DXVA2_ExtendedFormat dxvaExtFlags, REFERENCE_TIME avgFrameDuration);

    HRESULT SetFrameFlags(IMediaSample* pMS, LAVFrame* pFrame);

    HRESULT NegotiatePixelFormat(CMediaType& mt, int width, int height);

    HRESULT DeliverToRenderer(LAVFrame* pFrame);

    HRESULT PerformFlush();
    HRESULT ReleaseLastSequenceFrame();

private:
    friend class CVideoInputPin;
    friend class CVideoOutputPin;
    friend class CDecodeManager;

    CDecodeManager m_Decoder;

    REFERENCE_TIME m_rtPrevStart = 0;
    REFERENCE_TIME m_rtPrevStop = 0;
    REFERENCE_TIME m_rtAvgTimePerFrame = AV_NOPTS_VALUE;

    BOOL m_bForceInputAR = FALSE;
    BOOL m_bSendMediaType = FALSE; // 是否向输出IMediaSample对象中设置新的媒体尺寸信息，每次变化仅需设置一个样本。
    BOOL m_bFlushing = FALSE;
    BOOL m_bForceFormatNegotiation = FALSE;

    HRESULT m_hrDeliver = S_OK;

    CLAVPixFmtConverter m_PixFmtConverter;
    std::wstring m_strExtension;

    DWORD m_dwDecodeFlags = 0;
    // 三缓冲输出，最大容忍网络延迟抖动为120ms，可根据RtspSource模块统计情况动态调整。
    int m_outputBufferCount = 3;

    LAVPixelFormat m_filterPixFmt = LAVPixFmt_None;
    int m_filterWidth = 0;
    int m_filterHeight = 0;
    LAVFrame m_FilterPrevFrame;

    BOOL m_LAVPinInfoValid = FALSE;
    LAVPinInfo m_LAVPinInfo;

    struct {
        AVMasteringDisplayMetadata Mastering;
        AVContentLightMetadata ContentLight;
    } m_SideData;

    LAVFrame* m_pLastSequenceFrame = nullptr;

    struct VideoSettings {
        DWORD StreamAR = 2;
        DWORD RGBRange = 2;
        DWORD DitherMode = LAVDither_Random;
        int OutputBufferCount = 5;
    } m_config;
};
