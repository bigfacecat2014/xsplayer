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

#include "VideoInputPin.h"
#include "VideoOutputPin.h"

#include "IMediaSample3D.h"
#include "IMediaSideDataFFmpeg.h"

#pragma warning(disable: 4355)

CLAVVideo::CLAVVideo(LPUNKNOWN pUnk, HRESULT* phr)
    : CTransformFilter(TEXT("LAV Video Decoder"), 0, __uuidof(CLAVVideo))
    , m_Decoder(this)
{
    *phr = S_OK;
    m_pInput = new CVideoInputPin(TEXT("CVideoInputPin"), this, phr, L"Input");
    ASSERT(SUCCEEDED(*phr));

    m_pOutput = new CVideoOutputPin(TEXT("CVideoOutputPin"), this, phr, L"Output");
    ASSERT(SUCCEEDED(*phr));

    memset(&m_LAVPinInfo, 0, sizeof(m_LAVPinInfo));
    memset(&m_FilterPrevFrame, 0, sizeof(m_FilterPrevFrame));
    memset(&m_SideData, 0, sizeof(m_SideData));

    m_PixFmtConverter.SetSettings(this);

#ifdef _DEBUG
    DbgSetModuleLevel(LOG_TRACE, DWORD_MAX);
    DbgSetModuleLevel(LOG_ERROR, DWORD_MAX);
    DbgSetModuleLevel(LOG_CUSTOM1, DWORD_MAX); // FFMPEG messages use custom1
    //DbgSetLogFileDesktop(LAVC_VIDEO_LOG_FILE);
#endif
}

CLAVVideo::~CLAVVideo()
{
    ReleaseLastSequenceFrame();
    m_Decoder.Close();

#if defined(_DEBUG)
    DbgCloseLogFile();
#endif
}

STDMETHODIMP CLAVVideo::NonDelegatingQueryInterface(REFIID riid, void** ppv)
{
    CheckPointer(ppv, E_POINTER);
    *ppv = nullptr;
    return QI2(ILAVVideoConfig)
        QI2(ILAVVideoStatus)
        __super::NonDelegatingQueryInterface(riid, ppv);
}

int CLAVVideo::GetPinCount()
{
    return 2;
}

CBasePin* CLAVVideo::GetPin(int n)
{
    ASSERT(n < 2);
    return __super::GetPin(n);
}

HRESULT CLAVVideo::CheckInputType(const CMediaType* mtIn)
{
    if (mtIn->formattype == FORMAT_VideoInfo2) // 格式可能仅被初始化为空，尚未确定格式类型。
    {
        for (UINT i = 0; i < sudPinTypesInCount; i++) {
            if (*sudPinTypesIn[i].clsMajorType == mtIn->majortype
                && *sudPinTypesIn[i].clsMinorType == mtIn->subtype)
                return S_OK;
        }
    }
    return VFW_E_TYPE_NOT_ACCEPTED;
}

// Check if the types are compatible
// 检查此filter是否有能力将上游的输入媒体类型转换成当前下游filter（一般是渲染器）的支持的输入类型。
// 当下游渲染器正在连接中的时候，如果可以直接转换，意味着，不需要重新断开连接重建Graph.
// 如何运行时动态改变（不重置渲染器）上游输入的媒体类型，这里面的逻辑比较复杂。
// 如果能做到动态改变，意味着，可以做到无缝切换不同编码格式的输入码流，而不仅仅是清晰度切换了。
HRESULT CLAVVideo::CheckTransform(const CMediaType* mtIn, const CMediaType* mtOut)
{
    DbgLog((LOG_TRACE, 10, L"::CheckTransform()"));
    return S_OK; // 下游渲染器提议的一切媒体类型都无条件接受。
}

HRESULT CLAVVideo::DecideBufferSize(IMemAllocator* pAllocator, ALLOCATOR_PROPERTIES* pProperties)
{
    DbgLog((LOG_TRACE, 10, L"::DecideBufferSize()"));
    if (m_pInput->IsConnected() == FALSE) {
        return E_UNEXPECTED;
    }

    BITMAPINFOHEADER* bih = nullptr;
    CMediaType& mtOut = m_pOutput->CurrentMediaType();
    videoFormatTypeHandler(mtOut, &bih);

    pProperties->cBuffers = GetOutputBufferCount();
    pProperties->cbBuffer = (bih != nullptr) ? bih->biSizeImage : 4096;
    pProperties->cbAlign = 1;
    pProperties->cbPrefix = 0;

    HRESULT hr;
    ALLOCATOR_PROPERTIES Actual;
    if (FAILED(hr = pAllocator->SetProperties(pProperties, &Actual))) {
        return hr;
    }

    return pProperties->cBuffers > Actual.cBuffers
        || pProperties->cbBuffer > Actual.cbBuffer ? E_FAIL : S_OK;
}

HRESULT CLAVVideo::GetMediaType(int iPosition, CMediaType* pMediaType)
{
    DbgLog((LOG_TRACE, 10, L"::GetMediaType(): position: %d", iPosition));
    if (m_pInput->IsConnected() == FALSE) {
        return E_UNEXPECTED;
    }
    if (iPosition < 0) {
        return E_INVALIDARG;
    }

    if (iPosition >= (m_PixFmtConverter.GetNumMediaTypes() * 2)) {
        return VFW_S_NO_MORE_ITEMS;
    }

    int index = iPosition / 2;
    BOOL bVIH1 = iPosition % 2;

    CMediaType& mtIn = m_pInput->CurrentMediaType();

    BITMAPINFOHEADER* pBIH = nullptr;
    REFERENCE_TIME rtAvgTime = 0;
    DWORD dwAspectX = 0, dwAspectY = 0;
    videoFormatTypeHandler(mtIn.Format(), mtIn.FormatType(), &pBIH, &rtAvgTime, &dwAspectX, &dwAspectY);

    m_PixFmtConverter.GetMediaType(pMediaType, index, pBIH->biWidth, pBIH->biHeight,
        dwAspectX, dwAspectY, rtAvgTime);

    return S_OK;
}

HRESULT CLAVVideo::CreateDecoder(const CMediaType* pmt)
{
    DbgLog((LOG_TRACE, 10, L"::CreateDecoder(): Creating new decoder..."));
    HRESULT hr = S_OK;

    AVCodecID codec = FindCodecId(pmt);
    if (codec == AV_CODEC_ID_NONE) {
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    ASSERT(codec == AV_CODEC_ID_HEVC);

    ILAVPinInfo* pPinInfo = nullptr;
    hr = FindPinIntefaceInGraph(m_pInput, IID_ILAVPinInfo, (void**)&pPinInfo);
    if (SUCCEEDED(hr)) {
        memset(&m_LAVPinInfo, 0, sizeof(m_LAVPinInfo));

        m_LAVPinInfoValid = TRUE;
        m_LAVPinInfo.flags = pPinInfo->GetStreamFlags();
        m_LAVPinInfo.pix_fmt = (AVPixelFormat)pPinInfo->GetPixelFormat();
        m_LAVPinInfo.has_b_frames = pPinInfo->GetHasBFrames();

        SafeRelease(&pPinInfo);
    }
    else {
        m_LAVPinInfoValid = FALSE;
    }

    // Clear old sidedata
    memset(&m_SideData, 0, sizeof(m_SideData));

    // Read and store stream-level sidedata
    IMediaSideData* pPinSideData = nullptr;
    hr = FindPinIntefaceInGraph(m_pInput, __uuidof(IMediaSideData), (void**)&pPinSideData);
    if (SUCCEEDED(hr)) {
        MediaSideDataFFMpeg* pSideData = nullptr;
        size_t size = 0;
        hr = pPinSideData->GetSideData(IID_MediaSideDataFFMpeg, (const BYTE**)&pSideData, &size);
        if (SUCCEEDED(hr) && size == sizeof(MediaSideDataFFMpeg)) {
            for (int i = 0; i < pSideData->side_data_elems; i++) {
                AVPacketSideData* sd = &pSideData->side_data[i];

                // Display Mastering metadata, including color info
                if (sd->type == AV_PKT_DATA_MASTERING_DISPLAY_METADATA && sd->size == sizeof(AVMasteringDisplayMetadata))
                {
                    m_SideData.Mastering = *(AVMasteringDisplayMetadata*)sd->data;
                }
                else if (sd->type == AV_PKT_DATA_CONTENT_LIGHT_LEVEL && sd->size == sizeof(AVContentLightMetadata))
                {
                    m_SideData.ContentLight = *(AVContentLightMetadata*)sd->data;
                }
            }
        }

        SafeRelease(&pPinSideData);
    }

    m_dwDecodeFlags = 0;

    if (m_LAVPinInfoValid && (m_LAVPinInfo.flags & LAV_STREAM_FLAG_ONLY_DTS))
        m_dwDecodeFlags |= LAV_VIDEO_DEC_FLAG_ONLY_DTS;
    if (m_LAVPinInfoValid && (m_LAVPinInfo.flags & LAV_STREAM_FLAG_LIVE))
        m_dwDecodeFlags |= LAV_VIDEO_DEC_FLAG_LIVE;

    hr = m_Decoder.CreateDecoder(pmt, codec);
    if (FAILED(hr)) {
        DbgLog((LOG_TRACE, 10, L"-> Decoder creation failed"));
        goto done;
    }

    // Get avg time per frame
    videoFormatTypeHandler(pmt->Format(), pmt->FormatType(), nullptr, &m_rtAvgTimePerFrame);

    LAVPixelFormat pix;
    int bpp;
    m_Decoder.GetPixelFormat(&pix, &bpp);

    // Set input on the converter, and force negotiation if needed
    if (m_PixFmtConverter.SetInputFmt(pix, bpp) && m_pOutput->IsConnected())
        m_bForceFormatNegotiation = TRUE;

    if (pix == LAVPixFmt_YUV420)
        m_filterPixFmt = pix;

done:
    return SUCCEEDED(hr) ? S_OK : VFW_E_TYPE_NOT_ACCEPTED;
}

// 记住与上游SourceFilter辛苦协商确定下来的媒体类型。
// 此时已经知道上游的输出媒体类型是HEVC了。
// 显然，我们在CheckMediaType()那一步就告知此filter支持HEVC了。
// 尽管如此，此时尝试创建HEVC解码器，仍然有可能出现与系统环境有关的失败的情况！
HRESULT CLAVVideo::SetMediaType(PIN_DIRECTION dir, const CMediaType* pmt)
{
    HRESULT hr = S_OK;
    DbgLog((LOG_TRACE, 5, L"SetMediaType -- %S", dir == PINDIR_INPUT ? "in" : "out"));
    if (dir == PINDIR_INPUT) {
        hr = CreateDecoder(pmt); // 尝试创建HEVC解码器
        if (FAILED(hr)) {
            return hr;
        }
        m_bForceInputAR = TRUE;
    }
    else if (dir == PINDIR_OUTPUT) {
        LAVOutPixFmts outPixFmt = m_PixFmtConverter.GetOutPixFmtBySubtype(pmt->Subtype());
        m_PixFmtConverter.SetOutputPixFmt(outPixFmt);
    }
    return __super::SetMediaType(dir, pmt);
}

HRESULT CLAVVideo::EndOfStream()
{
    DbgLog((LOG_TRACE, 1, L"EndOfStream, flushing decoder"));
    CAutoLock cAutoLock(&m_csReceive);

    m_Decoder.EndOfStream();
    Deliver(GetFlushFrame());

    DbgLog((LOG_TRACE, 1, L"EndOfStream finished, decoder flushed"));
    return __super::EndOfStream();
}

HRESULT CLAVVideo::EndOfSegment()
{
    DbgLog((LOG_TRACE, 1, L"EndOfSegment, flushing decoder"));
    CAutoLock cAutoLock(&m_csReceive);

    m_Decoder.EndOfStream();
    Deliver(GetFlushFrame());

    // Forward the EndOfSegment call downstream
    if (m_pOutput != NULL && m_pOutput->IsConnected())
    {
        IPin* pConnected = m_pOutput->GetConnected();

        IPinSegmentEx* pPinSegmentEx = NULL;
        if (pConnected->QueryInterface(&pPinSegmentEx) == S_OK)
        {
            pPinSegmentEx->EndOfSegment();
        }
        SafeRelease(&pPinSegmentEx);
    }

    return S_OK;
}

HRESULT CLAVVideo::BeginFlush()
{
    DbgLog((LOG_TRACE, 1, L"::BeginFlush"));
    m_bFlushing = TRUE;
    return __super::BeginFlush();
}

HRESULT CLAVVideo::EndFlush()
{
    DbgLog((LOG_TRACE, 1, L"::EndFlush"));
    CAutoLock cAutoLock(&m_csReceive);

    ReleaseLastSequenceFrame();

    if (m_dwDecodeFlags & LAV_VIDEO_DEC_FLAG_DVD) {
        PerformFlush();
    }

    HRESULT hr = __super::EndFlush();
    m_bFlushing = FALSE;

    return hr;
}

HRESULT CLAVVideo::ReleaseLastSequenceFrame()
{
    ReleaseFrame(&m_pLastSequenceFrame);
    return S_OK;
}

HRESULT CLAVVideo::PerformFlush()
{
    CAutoLock cAutoLock(&m_csReceive);

    ReleaseLastSequenceFrame();
    m_Decoder.Flush();

    m_rtPrevStart = m_rtPrevStop = 0;
    memset(&m_FilterPrevFrame, 0, sizeof(m_FilterPrevFrame));

    return S_OK;
}

HRESULT CLAVVideo::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
    DbgLog((LOG_TRACE, 1, L"::NewSegment - %I64d / %I64d", tStart, tStop));
    PerformFlush();
    return __super::NewSegment(tStart, tStop, dRate);
}

HRESULT CLAVVideo::CheckConnect(PIN_DIRECTION dir, IPin* pPin)
{
    if (dir == PINDIR_INPUT) {
        if (FilterInGraphSafe(pPin, CLSID_LAVVideo, TRUE)) {
            DbgLog((LOG_TRACE, 10, L"CLAVVideo::CheckConnect(): LAVVideo is already in this graph branch, aborting."));
            return E_FAIL;
        }
    }
    else if (dir == PINDIR_OUTPUT) {
        // Get the filter we're connecting to
        IBaseFilter* pFilter = GetFilterFromPin(pPin);
        CLSID guidFilter = GUID_NULL;
        if (pFilter != nullptr) {
            if (FAILED(pFilter->GetClassID(&guidFilter)))
                guidFilter = GUID_NULL;

            SafeRelease(&pFilter);
        }

        // Don't allow connection to the AVI Decompressor, it doesn't support a variety of our output formats properly
        if (guidFilter == CLSID_AVIDec)
            return VFW_E_TYPE_NOT_ACCEPTED;
    }
    return __super::CheckConnect(dir, pPin);
}

HRESULT CLAVVideo::BreakConnect(PIN_DIRECTION dir)
{
    DbgLog((LOG_TRACE, 10, L"::BreakConnect"));
    if (dir == PINDIR_INPUT) {
        m_Decoder.Close();
    }
    else if (dir == PINDIR_OUTPUT)
    {
        m_Decoder.BreakConnect();
    }
    return __super::BreakConnect(dir);
}

HRESULT CLAVVideo::CompleteConnect(PIN_DIRECTION dir, IPin* pReceivePin)
{
    DbgLog((LOG_TRACE, 10, L"::CompleteConnect"));
    HRESULT hr = S_OK;
    if (dir == PINDIR_OUTPUT) {
        // Fail P010 software connections before Windows 10 Creators Update.
        // 要支持P010位颜色格式，就必须要求Win10操作系统的Creators资料片更新。
        hr = m_Decoder.PostConnect(pReceivePin);
    }
    else if (dir == PINDIR_INPUT) {
    }
    return hr;
}

HRESULT CLAVVideo::StartStreaming()
{
    CAutoLock cAutoLock(&m_csReceive);
    // delete by yxs
    return S_OK;
}

STDMETHODIMP CLAVVideo::Stop()
{
    // Get the receiver lock and prevent frame delivery
    {
        CAutoLock lck3(&m_csReceive);
        m_bFlushing = TRUE;
    }

    // actually perform the stop
    HRESULT hr = __super::Stop();

    // unblock delivery again, if we continue receiving frames
    m_bFlushing = FALSE;
    return hr;
}

HRESULT CLAVVideo::GetDeliveryBuffer(IMediaSample** ppOut, int width, int height, AVRational ar,
    DXVA2_ExtendedFormat dxvaExtFlags, REFERENCE_TIME avgFrameDuration)
{
    HRESULT hr = S_OK;

    CheckPointer(ppOut, E_POINTER);

    // 吓老子，一开始就要检测是否需要重新连接渲染器？会修改m_bSendMediaType的值。
    if (FAILED(hr = ReconnectOutput(width, height, ar, dxvaExtFlags, avgFrameDuration))) {
        return hr;
    }

    // 经过重连检测，我们已经得到当前渲染器最准确的媒体样本的格式信息，分配出来的样本也是最新格式。
    if (FAILED(hr = m_pOutput->GetDeliveryBuffer(ppOut, nullptr, nullptr, 0))) {
        return hr;
    }

    CheckPointer(*ppOut, E_UNEXPECTED);

    AM_MEDIA_TYPE* pmt = nullptr;
    if (SUCCEEDED((*ppOut)->GetMediaType(&pmt)) && pmt) {
        // 获取渲染器的最新媒体类型？从RGB32变成YV12了？还是桌面颜色模式发生了改变？
        // 这允许我们在渲染器检测到颜色模式变化时，可以直接修改连接的上游解码器的输出pin的媒体类型。
        CMediaType& outMt = m_pOutput->CurrentMediaType();
        BITMAPINFOHEADER* pBMINew = nullptr;
        videoFormatTypeHandler(pmt->pbFormat, &pmt->formattype, &pBMINew);

#ifdef _DEBUG
        BITMAPINFOHEADER* pBMIOld = nullptr;
        videoFormatTypeHandler(outMt.pbFormat, &outMt.formattype, &pBMIOld);

        RECT rcTarget = { 0 };
        ASSERT(pmt->formattype == FORMAT_VideoInfo2);
        rcTarget = ((VIDEOINFOHEADER2*)pmt->pbFormat)->rcTarget;
        DbgLog((LOG_TRACE, 10, L"::GetDeliveryBuffer(): Sample contains new media type from downstream filter.."));
        DbgLog((LOG_TRACE, 10, L"-> Width changed from %d to %d (target: %d)", pBMIOld->biWidth, pBMINew->biWidth, rcTarget.right));
#endif

        if (pBMINew->biWidth < width) {
            DbgLog((LOG_TRACE, 10, L" -> Renderer is trying to shrink the output window, failing!"));
            (*ppOut)->Release();
            (*ppOut) = nullptr;
            DeleteMediaType(pmt);
            return E_FAIL;
        }

        // Check image size，这是为了检测意外情况？
        DWORD lSampleSize = m_PixFmtConverter.GetImageSize(pBMINew->biWidth, abs(pBMINew->biHeight));
        if (lSampleSize != pBMINew->biSizeImage) {
            DbgLog((LOG_TRACE, 10, L"-> biSizeImage does not match our calculated sample size, correcting"));
            pBMINew->biSizeImage = lSampleSize;
            pmt->lSampleSize = lSampleSize;
            m_bSendMediaType = TRUE;
        }

        CMediaType mt = *pmt;
        m_pOutput->SetMediaType(&mt);
        DeleteMediaType(pmt);
        pmt = nullptr;
    }

    (*ppOut)->SetDiscontinuity(FALSE);
    (*ppOut)->SetSyncPoint(TRUE);

    return S_OK;
}

HRESULT CLAVVideo::ReconnectOutput(int width, int height, AVRational ar, DXVA2_ExtendedFormat dxvaExtFlags, REFERENCE_TIME avgFrameDuration)
{
    CMediaType mt = m_pOutput->CurrentMediaType();
    ASSERT(mt.formattype == FORMAT_VideoInfo2);

    HRESULT hr = S_FALSE;
    BOOL bNeedReconnect = FALSE;
    int timeout = 100;

    DWORD dwAspectX = 0, dwAspectY = 0;
    RECT rcTargetOld = { 0 };
    LONG biWidthOld = 0;

    // 检测输入Pin的当前媒体格式是否发生了变化？
    {
        VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)mt.Format();
        if (avgFrameDuration == AV_NOPTS_VALUE)
            avgFrameDuration = vih2->AvgTimePerFrame;

        DWORD dwARX, dwARY;
        videoFormatTypeHandler(m_pInput->CurrentMediaType().Format(), m_pInput->CurrentMediaType().FormatType(), nullptr, nullptr, &dwARX, &dwARY);

        int num = ar.num, den = ar.den;
        BOOL bStreamAR = (m_config.StreamAR == 1) || (m_config.StreamAR == 2 && (!(m_dwDecodeFlags & LAV_VIDEO_DEC_FLAG_STREAMAR_BLACKLIST) || !(dwARX && dwARY)));
        if (!bStreamAR || num == 0 || den == 0) {
            if (m_bForceInputAR && dwARX && dwARY) {
                num = dwARX;
                den = dwARY;
            }
            else {
                num = vih2->dwPictAspectRatioX;
                den = vih2->dwPictAspectRatioY;
            }
            m_bForceInputAR = FALSE;
        }
        dwAspectX = num;
        dwAspectY = den;

        bNeedReconnect = (vih2->rcTarget.right != width
            || vih2->rcTarget.bottom != height
            || vih2->dwPictAspectRatioX != num
            || vih2->dwPictAspectRatioY != den
            || abs(vih2->AvgTimePerFrame - avgFrameDuration) > 10);
    }

    // 如果发生了，那么就要重新设置输出Pin的媒体类型。
    if (bNeedReconnect) {
        DbgLog((LOG_TRACE, 10, L"::ReconnectOutput(): Performing reconnect"));
        BITMAPINFOHEADER* bih = nullptr;
        ASSERT(mt.formattype == FORMAT_VideoInfo2);
        {
            VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)mt.Format();
            rcTargetOld = vih2->rcTarget;

            DbgLog((LOG_TRACE, 10, L"Using VIH2, Format dump:"));
            DbgLog((LOG_TRACE, 10, L"-> width: %d -> %d", vih2->rcTarget.right, width));
            DbgLog((LOG_TRACE, 10, L"-> height: %d -> %d", vih2->rcTarget.bottom, height));
            DbgLog((LOG_TRACE, 10, L"-> AR: %d:%d -> %d:%d", vih2->dwPictAspectRatioX, vih2->dwPictAspectRatioY, dwAspectX, dwAspectY));
            DbgLog((LOG_TRACE, 10, L"-> FPS: %I64d -> %I64d", vih2->AvgTimePerFrame, avgFrameDuration));

            vih2->dwPictAspectRatioX = dwAspectX;
            vih2->dwPictAspectRatioY = dwAspectY;

            SetRect(&vih2->rcSource, 0, 0, width, height);
            SetRect(&vih2->rcTarget, 0, 0, width, height);

            vih2->AvgTimePerFrame = avgFrameDuration;
            vih2->dwInterlaceFlags = 0;

            bih = &vih2->bmiHeader;
        }

        DWORD oldSizeImage = bih->biSizeImage;

        biWidthOld = bih->biWidth;
        bih->biWidth = width;
        bih->biHeight = bih->biHeight < 0 ? -height : height;
        bih->biSizeImage = m_PixFmtConverter.GetImageSize(width, height);
        HRESULT hrQA = m_pOutput->GetConnected()->QueryAccept(&mt); // 询问渲染器是否接受新的媒体类型。
        // 搞一波动态重连操作。
        {
receiveconnection:
            hr = m_pOutput->GetConnected()->ReceiveConnection(m_pOutput, &mt); // 强制让渲染器接受新的媒体类。
            if (SUCCEEDED(hr)) {
                // 让输出Pin重新分配媒体样本缓冲对象。内存分配器实际上是GDI渲染器提供的DIB分配器。
                // 注意，临时分配一个IMediaSample*，仅用于获取下游渲染器的媒体类型（SwapChain样本的尺寸和像素格式）信息。
                IMediaSample* pOut = nullptr;
                if (SUCCEEDED(hr = m_pOutput->GetDeliveryBuffer(&pOut, nullptr, nullptr, 0)) && pOut) {
                    AM_MEDIA_TYPE* pmt = nullptr;
                    if (SUCCEEDED(pOut->GetMediaType(&pmt)) && pmt) {
                        CMediaType newmt = *pmt;
                        videoFormatTypeHandler(newmt.Format(), newmt.FormatType(), &bih);
                        DbgLog((LOG_TRACE, 10, L"-> New MediaType negotiated; actual width: %d - renderer requests: %ld", width, bih->biWidth));
                        if (bih->biWidth < width) {
                            DbgLog((LOG_ERROR, 10, L"-> Renderer requests width smaller than image width, failing.."));
                            DeleteMediaType(pmt);
                            pOut->Release();
                            return E_FAIL;
                        }
                        // Check image size
                        DWORD lSampleSize = m_PixFmtConverter.GetImageSize(bih->biWidth, abs(bih->biHeight));
                        if (lSampleSize != bih->biSizeImage) {
                            DbgLog((LOG_TRACE, 10, L"-> biSizeImage does not match our calculated sample size, correcting"));
                            bih->biSizeImage = lSampleSize;
                            newmt.SetSampleSize(lSampleSize);
                            m_bSendMediaType = TRUE;
                        }
                        // Store media type
                        m_pOutput->SetMediaType(&newmt);
                        m_bSendMediaType = TRUE;
                        DeleteMediaType(pmt);
                    }
                    else {
                        // No Stride Request? We're ok with that, too!
                        // The overlay mixer doesn't ask for a stride, but it needs one anyway
                        // It'll provide a buffer just in the right size, so we can calculate this here.
                        DbgLog((LOG_TRACE, 10, L"-> We did not get a stride request, using width %d for stride", bih->biWidth));
                        m_bSendMediaType = TRUE;
                        m_pOutput->SetMediaType(&mt);
                    }
                    // 此IMediaSample仅仅是为了了解尺寸信息的。比如EVR渲染器发生了全屏？
                    // 用完了就要立即释放掉。
                    pOut->Release(); 
                }
            }
            else if (hr == VFW_E_BUFFERS_OUTSTANDING && timeout != -1) {
                // 缺少内存，等待，重试，累计等待超过100ms后，就放弃，直接发送EndFlush()通知给下游渲染器。
                if (timeout > 0) {
                    DbgLog((LOG_TRACE, 10, L"-> Buffers outstanding, retrying in 10ms.."));
                    Sleep(10);
                    timeout -= 10;
                }
                else {
                    DbgLog((LOG_TRACE, 10, L"-> Buffers outstanding, timeout reached, flushing.."));
                    m_pOutput->DeliverBeginFlush();
                    m_pOutput->DeliverEndFlush();
                    timeout = -1;
                }
                goto receiveconnection;
            }
            else if (hrQA == S_OK) {
                DbgLog((LOG_TRACE, 10, L"-> Downstream accepts new format, but cannot reconnect dynamically..."));
                if (bih->biSizeImage > oldSizeImage) {
                    DbgLog((LOG_TRACE, 10, L"-> But, we need a bigger buffer, try to adapt allocator manually"));
                    IMemInputPin* pMemPin = nullptr;
                    if (SUCCEEDED(hr = m_pOutput->GetConnected()->QueryInterface<IMemInputPin>(&pMemPin)) && pMemPin) {
                        IMemAllocator* pMemAllocator = nullptr;
                        if (SUCCEEDED(hr = pMemPin->GetAllocator(&pMemAllocator)) && pMemAllocator) {
                            ALLOCATOR_PROPERTIES props, actual;
                            hr = pMemAllocator->GetProperties(&props);
                            hr = pMemAllocator->Decommit();
                            props.cbBuffer = bih->biSizeImage;
                            hr = pMemAllocator->SetProperties(&props, &actual);
                            hr = pMemAllocator->Commit();
                            SafeRelease(&pMemAllocator);
                        }
                    }
                    SafeRelease(&pMemPin);
                }
                else {
                    // Check if there was a stride before..
                    if (rcTargetOld.right && biWidthOld > rcTargetOld.right && biWidthOld > bih->biWidth) {
                        // If we had a stride before, the filter is apparently stride aware
                        // Try to make it easier by keeping the old stride around
                        bih->biWidth = biWidthOld;
                    }
                }
                m_pOutput->SetMediaType(&mt);
                m_bSendMediaType = TRUE;
            }
            else {
                DbgLog((LOG_TRACE, 10, L"-> Receive Connection failed (hr: %x); QueryAccept: %x", hr, hrQA));
            }
        }
        if (bNeedReconnect)
            NotifyEvent(EC_VIDEO_SIZE_CHANGED, MAKELPARAM(width, height), 0);

        hr = S_OK;
    }

    return hr;
}

HRESULT CLAVVideo::NegotiatePixelFormat(CMediaType& outMt, int width, int height)
{
    DbgLog((LOG_TRACE, 10, L"::NegotiatePixelFormat()"));

    HRESULT hr = S_OK;
    int i = 0;
    int timeout = 100;

    DWORD dwAspectX, dwAspectY;
    REFERENCE_TIME rtAvg;
    BOOL bVIH1 = (outMt.formattype == FORMAT_VideoInfo);
    videoFormatTypeHandler(outMt.Format(), outMt.FormatType(), nullptr, &rtAvg, &dwAspectX, &dwAspectY);

    CMediaType mt;
    for (i = 0; i < m_PixFmtConverter.GetNumMediaTypes(); ++i) {
        m_PixFmtConverter.GetMediaType(&mt, i, width, height, dwAspectX, dwAspectY, rtAvg);
    receiveconnection:
        hr = m_pOutput->GetConnected()->ReceiveConnection(m_pOutput, &mt);
        if (hr == S_OK) {
            DbgLog((LOG_TRACE, 10, L"::NegotiatePixelFormat(): Filter accepted format with index %d", i));
            m_pOutput->SetMediaType(&mt);
            hr = S_OK;
            goto done;
        }
        else if (hr == VFW_E_BUFFERS_OUTSTANDING && timeout != -1) {
            if (timeout > 0) {
                DbgLog((LOG_TRACE, 10, L"-> Buffers outstanding, retrying in 10ms.."));
                Sleep(10);
                timeout -= 10;
            }
            else {
                DbgLog((LOG_TRACE, 10, L"-> Buffers outstanding, timeout reached, flushing.."));
                m_pOutput->DeliverBeginFlush();
                m_pOutput->DeliverEndFlush();
                timeout = -1;
            }
            goto receiveconnection;
        }
    }

    DbgLog((LOG_ERROR, 10, L"::NegotiatePixelFormat(): Unable to agree on a pixel format"));
    hr = E_FAIL;

done:
    FreeMediaType(mt);
    return hr;
}

HRESULT CLAVVideo::Receive(IMediaSample* pIn)
{
    HRESULT hr = S_OK;
    CAutoLock cAutoLock(&m_csReceive);

    AM_SAMPLE2_PROPERTIES const* pProps = m_pInput->SampleProps();
    if (pProps->dwStreamId != AM_STREAM_MEDIA) {
        return m_pOutput->Deliver(pIn); // 透传非流媒体样本（比如：具备控制功能的特殊样本）。
    }

    // 从媒体源Receive到的样本带有媒体类型信息？
    AM_MEDIA_TYPE* pmt = nullptr;
    if (SUCCEEDED(pIn->GetMediaType(&pmt)) && pmt) {
        CMediaType mt = *pmt;
        DeleteMediaType(pmt);

        // 输入Pin的媒体样本格式发生了动态改变
        if (mt != m_pInput->CurrentMediaType()) { 
            DbgLog((LOG_TRACE, 10, L"::Receive(): Input sample contained media type, dynamic format change..."));
            m_Decoder.EndOfStream();
            hr = m_pInput->SetMediaType(&mt);
            if (FAILED(hr)) {
                DbgLog((LOG_ERROR, 10, L"::Receive(): Setting new media type failed..."));
                return hr;
            }

            // TODO:输出Pin的媒体样本格式也要改变？下游渲染器的输入Pin的媒体类型如何通知其改变?
        }
    }

    m_hrDeliver = S_OK;

    // Skip over empty packets
    if (pIn->GetActualDataLength() == 0) {
        return S_OK;
    }

    hr = m_Decoder.Decode(pIn);
    if (FAILED(hr))
        return hr;

    if (FAILED(m_hrDeliver))
        return m_hrDeliver;

    return S_OK;
}

STDMETHODIMP CLAVVideo::AllocateFrame(LAVFrame** ppFrame)
{
    CheckPointer(ppFrame, E_POINTER);

    *ppFrame = (LAVFrame*)CoTaskMemAlloc(sizeof(LAVFrame));
    if (!*ppFrame) {
        return E_OUTOFMEMORY;
    }

    // Initialize with zero
    ZeroMemory(*ppFrame, sizeof(LAVFrame));

    // Set some defaults
    (*ppFrame)->bpp = 8;
    (*ppFrame)->rtStart = AV_NOPTS_VALUE;
    (*ppFrame)->rtStop = AV_NOPTS_VALUE;
    (*ppFrame)->aspect_ratio = { 0, 1 };

    (*ppFrame)->frame_type = '?';

    return S_OK;
}

STDMETHODIMP CLAVVideo::ReleaseFrame(LAVFrame** ppFrame)
{
    CheckPointer(ppFrame, E_POINTER);

    // Allow *ppFrame to be NULL already
    if (*ppFrame) {
        FreeLAVFrameBuffers(*ppFrame);
        SAFE_CO_FREE(*ppFrame);
    }
    return S_OK;
}

STDMETHODIMP_(LAVFrame*) CLAVVideo::GetFlushFrame()
{
    LAVFrame* pFlushFrame = nullptr;
    AllocateFrame(&pFlushFrame);
    pFlushFrame->flags |= LAV_FRAME_FLAG_FLUSH;
    pFlushFrame->rtStart = INT64_MAX;
    pFlushFrame->rtStop = INT64_MAX;
    return pFlushFrame;
}

STDMETHODIMP CLAVVideo::Deliver(LAVFrame* pFrame)
{
    // Out-of-sequence flush event to get all frames delivered,
    // only triggered by decoders when they are already "empty"
    // so no need to flush the decoder here
    if (pFrame->flags & LAV_FRAME_FLAG_FLUSH) {
        DbgLog((LOG_TRACE, 10, L"Decoder triggered a flush..."));
        DeliverToRenderer(GetFlushFrame());
        ReleaseFrame(&pFrame);
        return S_FALSE;
    }

    if (m_bFlushing) {
        ReleaseFrame(&pFrame);
        return S_FALSE;
    }

    if (pFrame->rtStart == AV_NOPTS_VALUE) {
        pFrame->rtStart = m_rtPrevStop;
        pFrame->rtStop = AV_NOPTS_VALUE;
    }

    if (pFrame->rtStop == AV_NOPTS_VALUE) {
        REFERENCE_TIME duration = 0;

        CMediaType& mt = m_pOutput->CurrentMediaType();
        videoFormatTypeHandler(mt.Format(), mt.FormatType(), nullptr, &duration, nullptr, nullptr);

        REFERENCE_TIME decoderDuration = m_Decoder.GetFrameDuration();
        if (pFrame->avgFrameDuration && pFrame->avgFrameDuration != AV_NOPTS_VALUE) {
            duration = pFrame->avgFrameDuration;
        }
        else if (!duration && decoderDuration) {
            duration = decoderDuration;
        }
        else if (!duration) {
            duration = 1;
        }

        pFrame->rtStop = pFrame->rtStart + (duration * (pFrame->repeat ? 3 : 2) / 2);
    }

#if defined(DEBUG) && DEBUG_FRAME_TIMINGS
    DbgLog((LOG_TRACE, 10, L"Frame, rtStart: %I64d, dur: %I64d, diff: %I64d, key: %d, type: %C, repeat: %d, interlaced: %d, tff: %d",
        pFrame->rtStart, pFrame->rtStop - pFrame->rtStart, pFrame->rtStart - m_rtPrevStart,
        pFrame->key_frame, pFrame->frame_type, pFrame->repeat, pFrame->interlaced, pFrame->tff));
#endif

    m_rtPrevStart = pFrame->rtStart;
    m_rtPrevStop = pFrame->rtStop;

    if (!pFrame->avgFrameDuration || pFrame->avgFrameDuration == AV_NOPTS_VALUE) {
        pFrame->avgFrameDuration = m_rtAvgTimePerFrame;
    }
    else {
        m_rtAvgTimePerFrame = pFrame->avgFrameDuration;
    }

    if (pFrame->rtStart < 0) {
        ReleaseFrame(&pFrame);
        return S_OK;
    }

    return DeliverToRenderer(pFrame);
}

HRESULT CLAVVideo::DeliverToRenderer(LAVFrame* pFrame)
{
    HRESULT hr = S_OK;

    // This should never get here, but better check
    //ASSERT((pFrame->flags & LAV_FRAME_FLAG_FLUSH) == 0);
    if (pFrame->flags & LAV_FRAME_FLAG_FLUSH) {
        ReleaseFrame(&pFrame);
        return S_FALSE;
    }

    if (!(pFrame->flags & LAV_FRAME_FLAG_REDRAW)) {
        // Release the old End-of-Sequence frame, 
        // this ensures any "normal" frame will clear the stored EOS frame
        ReleaseFrame(&m_pLastSequenceFrame);
        if ((pFrame->flags & LAV_FRAME_FLAG_END_OF_SEQUENCE)) {
            assert(!pFrame->direct);
            CopyLAVFrame(pFrame, &m_pLastSequenceFrame);
        }
    }

    if (m_bFlushing) {
        ReleaseFrame(&pFrame);
        return S_FALSE;
    }

    // Collect width/height
    int width = pFrame->width;
    int height = pFrame->height;
    if (width == 1920 && height == 1088) {
        height = 1080;
    }

    m_PixFmtConverter.SetInputFmt(pFrame->format, pFrame->bpp);
    if (m_bForceFormatNegotiation) {
        DbgLog((LOG_TRACE, 10, L"::Decode(): Changed input pixel format to %d (%d bpp)", pFrame->format, pFrame->bpp));

        CMediaType& mt = m_pOutput->CurrentMediaType();

        if (m_PixFmtConverter.GetOutPixFmtBySubtype(mt.Subtype()) != m_PixFmtConverter.GetPreferredOutPixFmt()) {
            NegotiatePixelFormat(mt, width, height);
        }
        m_bForceFormatNegotiation = FALSE;
    }
    m_PixFmtConverter.SetColorProps(pFrame->ext_format, m_config.RGBRange);

    // Update flags for cases where the converter can change the nominal range
    if (m_PixFmtConverter.IsRGBConverterActive()) {
        if (m_config.RGBRange != 0) {
            pFrame->ext_format.NominalRange = m_config.RGBRange == 1 ? DXVA2_NominalRange_16_235 : DXVA2_NominalRange_0_255;
        }
        else if (pFrame->ext_format.NominalRange == DXVA2_NominalRange_Unknown) {
            pFrame->ext_format.NominalRange = DXVA2_NominalRange_16_235;
        }
    }
    else if (m_PixFmtConverter.GetOutputPixFmt() == LAVOutPixFmt_RGB32) {
        pFrame->ext_format.NominalRange = DXVA2_NominalRange_0_255;
    }

    // Check if we are doing RGB output
    BOOL bRGBOut = (m_PixFmtConverter.GetOutputPixFmt() == LAVOutPixFmt_RGB32);
    // Grab a media sample, and start assembling the data for it.
    IMediaSample* pSampleOut = nullptr;
    BYTE* pDataOut = nullptr;

    REFERENCE_TIME avgDuration = pFrame->avgFrameDuration;
    if (avgDuration == 0)
        avgDuration = AV_NOPTS_VALUE;

    // 获取指定宽高和像素格式的“样本缓冲区”
    if (FAILED(hr = GetDeliveryBuffer(&pSampleOut, width, height, pFrame->aspect_ratio, pFrame->ext_format, avgDuration))
        || FAILED(hr = pSampleOut->GetPointer(&pDataOut)) // 得到样本的缓冲区地址
        || pDataOut == nullptr) {
        SafeRelease(&pSampleOut);
        ReleaseFrame(&pFrame);
        return hr;
    }

    CMediaType& mt = m_pOutput->CurrentMediaType();
    BITMAPINFOHEADER* bih = nullptr;
    videoFormatTypeHandler(mt.Format(), mt.FormatType(), &bih);

    // 像素格式转换
    {
        long required = m_PixFmtConverter.GetImageSize(bih->biWidth, abs(bih->biHeight));
        long lSampleSize = pSampleOut->GetSize();
        if (lSampleSize < required) {
            DbgLog((LOG_ERROR, 10, L"::Decode(): Buffer is too small! Actual: %d, Required: %d", lSampleSize, required));
            SafeRelease(&pSampleOut);
            ReleaseFrame(&pFrame);
            return E_FAIL;
        }
        pSampleOut->SetActualDataLength(required);

        // 转换后的像素写入到“样本缓冲区”pDataOut中的。
        m_PixFmtConverter.Convert(pFrame->data, pFrame->stride, pDataOut, width, height, bih->biWidth, abs(bih->biHeight));

        FreeLAVFrameBuffers(pFrame);

        // 高大于0，则默认为倒置存放方式，需要上下翻转。
        if (mt.subtype == MEDIASUBTYPE_RGB32 && bih->biHeight > 0) {
            flip_plane(pDataOut, bih->biWidth * 4, height);
        }
    } // end if(软解模式)

    BOOL bSizeChanged = FALSE;
    // 将最新的媒体类型格式信息设置到IMediaSample*对象中。
    if (m_bSendMediaType) {
        AM_MEDIA_TYPE* sendmt = CreateMediaType(&mt);
        pSampleOut->SetMediaType(sendmt); // 太棒了，既然每一个媒体样本都能携带完整的视频格式信息。
        // 下游渲染器，可以通过IMediaSample2::GetProperties接口获取很多有价值的信息。
        DeleteMediaType(sendmt);
        m_bSendMediaType = FALSE;
    }

    // Set frame timings..
    pSampleOut->SetTime(&pFrame->rtStart, &pFrame->rtStop);
    pSampleOut->SetMediaTime(nullptr, nullptr);

    // And frame flags..
    SetFrameFlags(pSampleOut, pFrame);

    // Release frame before delivery, so it can be re-used by the decoder (if required)
    ReleaseFrame(&pFrame);
    
    hr = m_pOutput->Deliver(pSampleOut);
    if (FAILED(hr)) {
        DbgLog((LOG_ERROR, 10, L"::Decode(): Deliver failed with hr: %x", hr));
        m_hrDeliver = hr;
    }

    if (bSizeChanged)
        NotifyEvent(EC_VIDEO_SIZE_CHANGED, MAKELPARAM(bih->biWidth, abs(bih->biHeight)), 0);

    SafeRelease(&pSampleOut);

    return hr;
}

HRESULT CLAVVideo::SetFrameFlags(IMediaSample* pMS, LAVFrame* pFrame)
{
    HRESULT hr = S_OK;
    IMediaSample2* pMS2 = nullptr;
    if (SUCCEEDED(hr = pMS->QueryInterface(&pMS2))) {
        AM_SAMPLE2_PROPERTIES props;
        if (SUCCEEDED(pMS2->GetProperties(sizeof(props), (BYTE*)&props))) {
            props.dwTypeSpecificFlags &= ~0x7f;

            if (!pFrame->interlaced)
                props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_WEAVE;

            if (pFrame->tff)
                props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_FIELD1FIRST;

            if (pFrame->repeat)
                props.dwTypeSpecificFlags |= AM_VIDEO_FLAG_REPEAT_FIELD;

            pMS2->SetProperties(sizeof(props), (BYTE*)&props);
        }
    }
    SafeRelease(&pMS2);
    return hr;
}

STDMETHODIMP_(BOOL) CLAVVideo::HasDynamicInputAllocator()
{
    return dynamic_cast<CVideoInputPin*>(m_pInput)->HasDynamicAllocator();
}

STDMETHODIMP CLAVVideo::SetStreamAR(DWORD bStreamAR)
{
    m_config.StreamAR = bStreamAR;
    return S_OK;
}

STDMETHODIMP_(DWORD) CLAVVideo::GetStreamAR()
{
    return m_config.StreamAR;
}

STDMETHODIMP CLAVVideo::SetRGBOutputRange(DWORD dwRange)
{
    m_config.RGBRange = dwRange;
    return S_OK;
}

STDMETHODIMP_(DWORD) CLAVVideo::GetRGBOutputRange()
{
    return m_config.RGBRange;
}

STDMETHODIMP CLAVVideo::SetDitherMode(LAVDitherMode ditherMode)
{
    m_config.DitherMode = ditherMode;
    return S_OK;
}

STDMETHODIMP_(LAVDitherMode) CLAVVideo::GetDitherMode()
{
    return (LAVDitherMode)m_config.DitherMode;
}


STDMETHODIMP CLAVVideo::SetOutputBufferCount(int count)
{
    m_config.OutputBufferCount = count;
    return S_OK;
}

STDMETHODIMP_(int) CLAVVideo::GetOutputBufferCount()
{
    return m_config.OutputBufferCount;
}

HRESULT WINAPI LAVVideo_CreateInstance(IBaseFilter** ppObj)
{
    HRESULT hr = S_OK;

    CLAVVideo* o = new CLAVVideo(nullptr, &hr);
    ULONG ul = o->AddRef();
    *ppObj = static_cast<IBaseFilter*>(o);

    return hr;
}
