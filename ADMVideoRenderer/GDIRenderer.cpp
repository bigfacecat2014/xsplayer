#include "stdafx.h"
#include "global.h"

#include <initguid.h>
DEFINE_GUID(CLSID_SampleRenderer,
0x4d4b1600, 0x33ac, 0x11cf, 0xbf, 0x30, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a);

namespace VideoRenderer {

    //-------------------------------------------------------------------------------------------------
    // CGDIVideoInputPin implementation
    //-------------------------------------------------------------------------------------------------
    CGDIVideoInputPin::CGDIVideoInputPin(int channel, TCHAR* pObjectName, CGDIVideoRenderer* pRenderer,
        CCritSec* pInterfaceLock, HRESULT* phr, LPCWSTR pPinName)
        : CRendererInputPin(channel, pRenderer, phr, pPinName),
        m_pRenderer(pRenderer),
        m_pInterfaceLock(pInterfaceLock)
    {
        ASSERT(m_pRenderer);
        ASSERT(pInterfaceLock);
    }

    STDMETHODIMP CGDIVideoInputPin::GetAllocator(IMemAllocator** ppAllocator)
    {
        CheckPointer(ppAllocator, E_POINTER);
        CAutoLock cInterfaceLock(m_pInterfaceLock);

        if (m_pAllocator == NULL) {
            m_pAllocator = (IMemAllocator*)(m_pRenderer->_imageAllocator[m_channel]);
            m_pAllocator->AddRef();
        }

        m_pAllocator->AddRef();
        *ppAllocator = m_pAllocator;

        return S_OK;
    }

    STDMETHODIMP CGDIVideoInputPin::NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly)
    {
        CAutoLock cInterfaceLock(m_pInterfaceLock);

        HRESULT hr = CBaseInputPin::NotifyAllocator(pAllocator, bReadOnly);
        ASSERT(SUCCEEDED(hr));

        return S_OK;
    }

    HRESULT CGDIVideoInputPin::GetMediaType(int iPosition, __inout CMediaType* pMediaType)
    {
        if (iPosition < 0) {
            return E_INVALIDARG;
        }
        if (iPosition > 0) {
            return VFW_S_NO_MORE_ITEMS;
        }
        pMediaType->majortype = MEDIATYPE_Video;
        pMediaType->subtype = MEDIASUBTYPE_RGB32;
        pMediaType->formattype = FORMAT_VideoInfo2;

        return S_OK;
    }

    HRESULT CGDIVideoInputPin::CheckMediaType(const CMediaType* pmt)
    {
        return pmt->subtype == MEDIASUBTYPE_RGB32 ? S_OK : E_FAIL;
    }


    //-------------------------------------------------------------------------------------------------
    // CVideoRenderer implementation
    //-------------------------------------------------------------------------------------------------
    CGDIVideoRenderer::CGDIVideoRenderer(HWND hwndHost, TCHAR* pName, LPUNKNOWN pUnk, HRESULT* phr)
        : CBaseRenderer(CLSID_SampleRenderer, pName, pUnk, phr)
    {
        _hwndHost = hwndHost;

        // TODO：从输入Pin的媒体类型字段中获取视频源尺寸。
        // 如果有所变化，那么就要重新设置SourceRect的尺寸。如果在Zoom操作，那么应当采用独立的Zoom过的源矩形。
        // 这里是不能直接使用的初始值。
        RECT sr = { 0, 0, 640, 360 };
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            _imageAllocator[i] = new CImageAllocator(this, TEXT(""), phr),

            _inputPin[i] = new CGDIVideoInputPin(i, TEXT(""), this, &m_InterfaceLock[i], phr, L"");
            m_pInputPin[i] = _inputPin[i]; // weak ptr.

            _drawImage[i] = new CDrawImage();
            _drawImage[i]->InitHostWnd(_hwndHost);
            _drawImage[i]->SetSourceRect(&sr);
        }
        // 默认位置矩形和剪裁矩形
        {
            RECT rc;
            ::GetClientRect(_hwndHost, &rc);
            _posRect = rc;
            _clipRect = rc;
            CalcLayout();
        }
    }

    CGDIVideoRenderer::~CGDIVideoRenderer()
    {
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            SAFE_DELETE(_imageAllocator[i]);
            SAFE_DELETE(_inputPin[i]);
            m_pInputPin[i] = nullptr;
            SAFE_DELETE(_drawImage[i]);
        }
    }

    HRESULT CGDIVideoRenderer::CheckMediaType(int channel, const CMediaType* pmtIn)
    {
        return _display.CheckMediaType(pmtIn);
    }

    CBasePin* CGDIVideoRenderer::GetPin(int n)
    {
        ASSERT(n >= 0 && n < INPUT_PIN_COUNT);
        if (n < 0 || n >= INPUT_PIN_COUNT) {
            return NULL;
        }

        if (m_pInputPin[n] == NULL) {
            m_pInputPin[n] = _inputPin[n];
        }

        return m_pInputPin[n];
    }

    STDMETHODIMP CGDIVideoRenderer::NonDelegatingQueryInterface(REFIID riid, void** ppv)
    {
        CheckPointer(ppv, E_POINTER);

        if (riid == IID_IVideoRendererCommand) {
            AddRef();
            *ppv = static_cast<ICommand*>(this);
            return S_OK;
        }
        return CBaseRenderer::NonDelegatingQueryInterface(riid, ppv);

    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::SetObjectRects(LPCRECT posRect, LPCRECT clipRect)
    {
        _posRect = *posRect;
        _clipRect = *clipRect;
        CalcLayout();
        return S_OK;
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::SetViewMode(int mode)
    {
        HRESULT hr = S_OK;

        if (mode < VIEW_MODE_1x1 || mode >= VIEW_MODE_COUNT) {
            return E_NOTIMPL;
        }

        _viewMode = (VIEW_MODE)mode;
        CalcLayout();

        return hr;
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::SetLayout(int channel, const ViewportDesc_t* desc)
    {
        HRESULT hr = S_OK;

        _viewportDesc[(int)_viewMode][channel] = *desc;

        return hr;
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::SetSourceFrameInterval(int channel, DWORD frameInterval)
    {
        HRESULT hr = S_OK;

        m_sourceFrameInterval[channel] = frameInterval;

        return hr;
    }
        
    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::SetNotifyReceiver(INotify* receiver)
    {
        return E_NOTIMPL;
    }

    // 网络源打的时间戳，由于解码器解码时间的剧烈抖动，已经变得没有任何呈现参考价值了。
    // 此时我们要根据收到样本的当前时刻，重新估算，得到更加平滑有效的样本呈现时间。
    // 一波神操作，为解码后的样本重新设置了的更加平滑的呈现时间戳。
    // 这也是为啥，DirectShow对LiveSource不打时间戳的原因，打了也没有意义，总是晚了。
    // 必须根据解码后，重新打时间戳，且要在窗口线程每次来取样呈现的时候统计样本是否
    // 总是晚了，且待呈现队列出现了库存堆积，则要增加帧间隔。
    // 如果呈现器的待呈现队列总是空的状态，那么应该增加帧间隔。？？？？
    // TODO:在UI线程才会计算时间戳，解码线程仅入列，不做任何时间间隔相关计算。
    STDMETHODIMP_(BOOL __stdcall) CGDIVideoRenderer::Update(TimeContext* tc)
    {
        BOOL isChanged = FALSE;
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            if (Update(i, tc))
                isChanged = TRUE;
        }
        return isChanged;
    }

    BOOL CGDIVideoRenderer::Update(int channel, TimeContext* tc)
    {
        BOOL needPresent = FALSE;
        DWORD curTime = (DWORD)tc->cur_update_time;
        DWORD dt = 0; // 当前的帧持续时间。

        // 如果没有收到任何样本直接返回
        if (!m_isFirstSampleReceived[channel])
            return FALSE;

        // 一次性将已解码样本队列中的样本全部复制到待呈现队列。
        int count = m_presentSampleCount[channel];
        int newCount = 0;
        IMediaSample* tempQueue[JITTER_BUFFER] = { 0 };
        {
            CAutoLock lock(&m_pendingSampleQueueLock[channel]);
            newCount = m_pendingSampleCount[channel];
            if (newCount > 0) {
                memcpy(tempQueue, m_pendingSampleQueue[channel], newCount * sizeof(void*));
                m_pendingSampleCount[channel] = 0;
            }
        }
        memcpy(&m_presentSampleQueue[channel][count], tempQueue, newCount * sizeof(void*));
        count += newCount;
        m_presentSampleCount[channel] = count;
#ifdef _DEBUG
        for (int i = count; i < JITTER_BUFFER; ++i) {
            m_presentSampleQueue[channel][i] = nullptr;
        }
#endif

        // 更新时间基线，当前时间线
        if (m_isFirstSample[channel]) {
            m_isFirstSample[channel] = false;

            dt = m_presentInterval[channel] = m_sourceFrameInterval[channel];
            // 每个通道的呈现时间轴基线不一样。因为每个通道媒体时序是不相关的，可以完全独立的。
            m_presentTimeBaseLine[channel] = curTime;
            m_nextPTS[channel] = curTime;
        }
        else {
            // 当发现PTS总是连续迟到的时候，就需要动态增加延迟了，否则抖动会越来越厉害。
            // 因为是实时源，我们不能通过质量控制接口，要求实时源加快生产样本，
            // 只能让呈现器被动选择增加延迟，缓冲一会儿再播放。
            // TODO: 改为动态调整呈现帧间隔，如果呈现时间总是晚了（样本过期很久了），那么说明需要增大呈现间隔。
            // 调整幅度与样本的平均过期程度有关，比如最近一秒内平均每帧过期20ms，那么就要将呈现间隔增加20ms。
            if (m_nextPTS[channel] == 0) {
                if (count == 0 && channel == 0)
                    printf("oh, no, no, no, .......sample pool is dry.\n");
                // TODO: 预测池子消费速度的拐点，避免闹饥荒。
                if (count >= BP_IDEA_QUEUE_LEN) {
                    if (m_boostState[channel] == BS_SLOWER) {
                        // 立即回归到期望值，然后再做加速速。
                        m_presentIntervalAdjust[channel] = 100;
                    }
                    m_boostState[channel] = BS_FASTER;
                }
                else {
                    if (m_boostState[channel] == BS_FASTER) {
                        // 立即回归到期望值，然后再做减速。
                        m_presentIntervalAdjust[channel] = 100;
                    }
                    m_boostState[channel] = BS_SLOWER;
                }
                if (m_boostState[channel] == BS_FASTER) { // 提高FPS
                    // 加速的极限是2倍速，即帧间隔为期望值的50%。
                    if (m_presentIntervalAdjust[channel] > 50) {
                        m_presentIntervalAdjust[channel] -= 5; // 每次减少10%，1秒内即可提升至2倍速。
                    }
                }
                else { // 降低FPS
                    // 减速的极限是0.5倍速，即帧间隔为期望值的200%。
                    if (m_presentIntervalAdjust[channel] < 200) {
                        m_presentIntervalAdjust[channel] += 5; // 每次增加10%，1秒内即可降低至0.5倍速。
                    }
                }
                dt = (m_presentInterval[channel] * m_presentIntervalAdjust[channel]) / 100;
                m_nextPTS[channel] = m_lastPTS[channel] + dt;
            }
            else {
                // 不要更新时间，因为队列头部的Sample还没有呈现。
            }
        }

        // 判断是否需要立即呈现
        needPresent = (count > 0 && m_nextPTS[channel] > 0 && curTime >= m_nextPTS[channel]);

        // TODO:将呈现时机过期太长的丢弃？或者加快播放？
        // 加快呈现，如果还是一直发现过期，会在网络源那里引发大量未解码样本堆积。
        // 丢帧只考虑从未解码的源那个环节丢帧。
        // 不会的，因为只有第一个帧被窗口线程打了待呈现时间戳，第一帧后续的样本根本没有打时间戳，毫无意义。

        return needPresent;
    }

    HRESULT CGDIVideoRenderer::Render(int channel, HDC hdcDraw)
    {
        DWORD curTime = timeGetTime();
        REFERENCE_TIME tsStart = 0;
        REFERENCE_TIME tsStop = 0;

        // 优先使用队列里的样本
        if (m_presentSampleCount[channel] > 0) {
            // 先归还上一个样本。
            SAFE_RELEASE(m_lastPresentSample[channel]);
            // 再取新的样本。
            IMediaSample* ms = m_presentSampleQueue[channel][0];
            assert(ms != nullptr);
            m_lastPresentSample[channel] = ms;
            DoRenderSample(channel, ms, hdcDraw);
        }
        else {
            if (m_lastPresentSample[channel] != nullptr) {
                DoRenderSample(channel, m_lastPresentSample[channel], hdcDraw);
            }
            else {
                return S_OK; // 无任何样本可供渲染。
            }
        }

        m_lastPTS[channel] = timeGetTime(); // 纠正累积误差。
        m_nextPTS[channel] = 0; // 重置，准备用于记录下一个样本的呈现时间戳。

        // PTS总是负数，总是晚了，起不到任何定时且均匀输出的作用，总是为时已晚且不得已才输出的。
        // 而不是刚刚好踩到点上，才输出的，比如晚个1-3毫秒，这都可以导致平滑。
        // 实际情况是，几乎每帧都晚了接近30ms，这样肯定不行。
        if (channel == 0) {
            if (m_debugLastPTS[channel] == 0) {
                m_debugLastPTS[channel] = curTime;
            }

            DWORD dt = curTime - m_debugLastPTS[channel];
            printf("pts[%d]=%3d, queue len=%d, adjust=%d\n", channel, dt - 40,
                m_presentSampleCount[channel], m_presentIntervalAdjust[channel]);
            m_debugLastPTS[channel] = curTime;
        }

        // 将已呈现的样本从呈现队列里消除掉
        if (m_presentSampleCount[channel] > 0) {
            --m_presentSampleCount[channel]; // 已经消费掉了一个样本。
            int count = m_presentSampleCount[channel];
            memmove(&m_presentSampleQueue[channel][0], &m_presentSampleQueue[channel][1], count * sizeof(void*));
            m_presentSampleQueue[channel][count] = nullptr;
            ++m_receivedSampleCount[channel];
        }

        return S_OK;
    }

    STDMETHODIMP_(void __stdcall) CGDIVideoRenderer::Render(DeviceContext* dc)
    {
        // TODO：移动到SetObjectRects()函数中去,根据视口相对坐标描述重新计算每一种模式每一个通道的边界矩形。
        // TODO:改为渲染合成好的大表面。
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            const RECT* r = &_viewportRect[(int)_viewMode][i];
            RECT tr = *r;
            ::OffsetRect(&tr, dc->boundRect.left, dc->boundRect.top);
            _drawImage[i]->SetTargetRect(&tr);
            // 呈现时机成熟了，才会立即呈现队列的第一个样本。
            // 16个通道中，必然存在一个通道已经出现了呈现时机成熟的样本。
            Render(i, dc->hdcDraw);
        }
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::Stop(int channel)
    {
        return StopChannel(channel);
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::Pause(int channel)
    {
        return PauseChannel(channel);
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::Run(int channel, REFERENCE_TIME startTime)
    {
        return RunChannel(channel, startTime);
    }

    HRESULT CGDIVideoRenderer::Active(int channel)
    {
        SetRepaintStatus(channel, FALSE);
        return CBaseRenderer::Active(channel);
    }

    // 此时的m_mtIn可能是我们自己提议的不完整的媒体类型。
    // 因为正在建立连接时，我们可能并不知道解码器输出的图片的宽高信息。
    // 媒体协商过程中，解码器也没有告诉我们它的输入Pin的媒体类型详情。
    // 并且还是优先让下游渲染器（就是这个对象）提议最优的媒体输出类型为RGB32。
    // 为了简单化：
    // LAVVideo会在解码第一个帧的时候，在CLAVVideo::DeliverToRenderer(LAVFrame* pFrame)方法中
    // 判断需要m_bSendMediaType时，夹带一些媒体详细信息。
    // 会在IMediaSample*上附加一些媒体类型信息，渲染器可以在收到这个样本时，
    // 决定是否重新初始化渲染设备。
    // 即使在Activate()的时候，也不能得到上游的媒体类型的详细信息。
    // 此时可以简单忽略不全面的媒体格式信息，等上游解码器Reconnect后，会再次设置更精确的媒体类型信息。
    // 可以延迟到那个时候再更新显示信息。
    HRESULT CGDIVideoRenderer::SetMediaType(int channel, const CMediaType* pmt)
    {
        HRESULT hr = S_OK;
        int i = channel;
        CAutoLock channelLock(&m_InterfaceLock[channel]);

        CMediaType oldFormat(_mtIn[i]); // old media type

        // Fill out the optional fields in the VIDEOINFOHEADER2
        _mtIn[i] = *pmt;
        VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)_mtIn[i].Format();
        _display.UpdateFormat(vih2);
        _drawImage[i]->NotifyMediaType(&_mtIn[i]);
        RECT sr = { 0, 0, vih2->bmiHeader.biWidth, vih2->bmiHeader.biHeight };
        _drawImage[i]->SetSourceRect(&sr);
        _imageAllocator[i]->NotifyMediaType(&_mtIn[i]);

        return S_OK;
    }

    HRESULT CGDIVideoRenderer::BreakConnect(int channel)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);

        HRESULT hr = CBaseRenderer::BreakConnect(channel);
        if (FAILED(hr)) {
            return hr;
        }

        // 什么都不能渲染了
        _mtIn[channel].ResetFormatBuffer();

        return S_OK;

    }
    HRESULT CGDIVideoRenderer::CompleteConnect(int channel, IPin* pReceivePin)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);
        return CBaseRenderer::CompleteConnect(channel, pReceivePin);
    }

    HRESULT CGDIVideoRenderer::DoRenderSample(int channel, IMediaSample* pMediaSample, HDC hdcDraw)
    {
        return _drawImage[channel]->DrawImage(pMediaSample, hdcDraw);
    }

} // end namespace VideoRenderer

HRESULT WINAPI GDIRenderer_CreateInstance(HWND hwndHost, IBaseFilter** ppObj)
{
    HRESULT hr = S_OK;

    auto o = new VideoRenderer::CGDIVideoRenderer(hwndHost, TEXT(""), nullptr, &hr);
    ULONG ul = o->AddRef();
    *ppObj = static_cast<IBaseFilter*>(o);

    return hr;
}

