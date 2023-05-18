#include "stdafx.h"
#include "global.h"

namespace VideoRenderer {

    //-----------------------------------------------------------------------------
    // Uncompressed video samples input pin implementation
    //-----------------------------------------------------------------------------
    CRendererInputPin::CRendererInputPin(int channel, __inout CBaseRenderer* pRenderer,
        __inout HRESULT* phr, __in_opt LPCWSTR pPinName) :
        CBaseInputPin(TEXT(""), pRenderer, &pRenderer->m_InterfaceLock[channel], (HRESULT*)phr, pPinName)
    {
        m_channel = channel;
        m_pRenderer = pRenderer;
        ASSERT(m_pRenderer);
    }

    // Signals end of data stream on the input pin
    STDMETHODIMP CRendererInputPin::EndOfStream()
    {
        CAutoLock channelLock(&m_pRenderer->m_InterfaceLock[m_channel]);

        // Make sure we're streaming ok
        HRESULT hr = CheckStreaming();
        if (hr != S_OK) {
            return hr;
        }

        // Pass it onto the renderer
        hr = m_pRenderer->EndOfStream(m_channel);
        if (SUCCEEDED(hr)) {
            hr = CBaseInputPin::EndOfStream();
        }
        return hr;
    }

    STDMETHODIMP CRendererInputPin::BeginFlush()
    {
        CAutoLock channelLock(&m_pRenderer->m_InterfaceLock[m_channel]);
        {
            CBaseInputPin::BeginFlush();
            m_pRenderer->BeginFlush(m_channel);
        }
        return m_pRenderer->ResetEndOfStream(m_channel);
    }

    STDMETHODIMP CRendererInputPin::EndFlush()
    {
        CAutoLock channelLock(&m_pRenderer->m_InterfaceLock[m_channel]);

        HRESULT hr = m_pRenderer->EndFlush(m_channel);
        if (SUCCEEDED(hr)) {
            hr = CBaseInputPin::EndFlush();
        }
        return hr;
    }

    STDMETHODIMP CRendererInputPin::Receive(IMediaSample* pSample)
    {
        HRESULT hr = m_pRenderer->Receive(m_channel, pSample);
        if (FAILED(hr)) {
            // 失败了就要报错，退出，通知渲染器EOS？
            CAutoLock channelLock(&m_pRenderer->m_InterfaceLock[m_channel]);
            if (!IsStopped() && !IsFlushing() && !m_pRenderer->m_bAbort && !m_bRunTimeError) {
                m_pRenderer->NotifyEvent(EC_ERRORABORT, hr, 0);
                {
                    if (m_pRenderer->IsStreaming(m_channel) 
                        && !m_pRenderer->IsEndOfStreamDelivered(m_channel)) {
                        m_pRenderer->NotifyEndOfStream(m_channel);
                    }
                }
                m_bRunTimeError = TRUE; // 最好别发生运行时错误。
            }
        }

        return hr;
    }

    HRESULT CRendererInputPin::BreakConnect()
    {
        HRESULT hr = m_pRenderer->BreakConnect(m_channel);
        if (FAILED(hr)) {
            return hr;
        }
        return CBaseInputPin::BreakConnect();
    }

    HRESULT CRendererInputPin::CompleteConnect(IPin* pReceivePin)
    {
        HRESULT hr = m_pRenderer->CompleteConnect(m_channel, pReceivePin);
        if (FAILED(hr)) {
            return hr;
        }
        return CBaseInputPin::CompleteConnect(pReceivePin);
    }

    STDMETHODIMP CRendererInputPin::QueryId(__deref_out LPWSTR* Id)
    {
        CheckPointer(Id, E_POINTER);

        const WCHAR szIn[] = L"In";
        *Id = (LPWSTR)CoTaskMemAlloc(sizeof(szIn));
        if (*Id == NULL) {
            return E_OUTOFMEMORY;
        }
        CopyMemory(*Id, szIn, sizeof(szIn));
        return S_OK;
    }

    HRESULT CRendererInputPin::CheckMediaType(const CMediaType* pmt)
    {
        return m_pRenderer->CheckMediaType(m_channel, pmt);
    }

    // Called when we go paused or running
    HRESULT CRendererInputPin::Active()
    {
        return m_pRenderer->Active(m_channel);
    }

    // Called when we go into a stopped state
    HRESULT CRendererInputPin::Inactive()
    {
        // The caller must hold the interface lock because 
        // this function uses m_bRunTimeError.
        ASSERT(CritCheckIn(&m_pRenderer->m_InterfaceLock[m_channel]));
        m_bRunTimeError = FALSE;
        return m_pRenderer->Inactive(m_channel);
    }

    // Check if our filter is currently stopped
    BOOL CRendererInputPin::IsStopped()
    {
        return m_pRenderer->m_ChannelState[m_channel] == State_Stopped;
    }

    // Tell derived classes about the media type agreed
    HRESULT CRendererInputPin::SetMediaType(const CMediaType* pmt)
    {
        HRESULT hr = CBaseInputPin::SetMediaType(pmt);
        if (FAILED(hr)) {
            return hr;
        }
        return m_pRenderer->SetMediaType(m_channel, pmt);
    }


    //-----------------------------------------------------------------------------
    // Multi input pin video renderer base class implementation
    //-----------------------------------------------------------------------------
    CBaseRenderer::CBaseRenderer(REFCLSID RenderClass, // CLSID for this renderer
        __in_opt LPCTSTR pName, // Debug ONLY description
        __inout_opt LPUNKNOWN pUnk, // Aggregated owner object
        __inout HRESULT* phr) : // General OLE return code
        CBaseFilter(pName, pUnk, &m_PresenterLock, RenderClass)
    {
        timeBeginPeriod(1);
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            m_evComplete[i] = new CAMEvent(TRUE, phr);
            m_bAbort[i] = FALSE;
            m_bStreaming[i] = FALSE;
            m_bEOS[i] = FALSE;
            m_bEOSDelivered[i] = FALSE;
            m_bRepaintStatus[i] = TRUE;
            m_bInReceive[i] = FALSE;
            m_pInputPin[i] = nullptr;
            m_presentIntervalAdjust[i] = 100;
            if (SUCCEEDED(*phr)) {
                Ready(i);
            }
        }
        ZeroMemory(m_pendingSampleQueue, sizeof(m_pendingSampleQueue));
        ZeroMemory(m_presentSampleQueue, sizeof(m_presentSampleQueue));
        ZeroMemory(m_lastPresentSample, sizeof(m_lastPresentSample));
        ZeroMemory(m_lastPTS, sizeof(m_lastPTS));
        ZeroMemory(m_nextPTS, sizeof(m_nextPTS));
        ZeroMemory(_viewportDesc, sizeof(_viewportDesc));
        SetDefaultViewportDesc();
        ZeroMemory(_viewportRect, sizeof(_viewportRect));
    }

    CBaseRenderer::~CBaseRenderer()
    {
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            ASSERT(!m_bStreaming[i]);
            StopStreaming(i);
            ClearPendingSample(i);
            if (m_pInputPin[i] != nullptr) {
                delete m_pInputPin[i];
                m_pInputPin[i] = nullptr;
            }
        }
        timeEndPeriod(1);
    }

    STDMETHODIMP CBaseRenderer::NonDelegatingQueryInterface(REFIID riid, __deref_out void **ppv)
    {
        return CBaseFilter::NonDelegatingQueryInterface(riid,ppv);
    }

    // 此方法会阻塞MixedGraph的每通道工作线程。
    // 我们可能根本没有使用窗口套间，因此所有对线程消息的假设都是无效的。
    void CBaseRenderer::WaitForReceiveToComplete(int channel)
    {
        for (;;) {
            {
                CAutoLock channelLock(&m_InterfaceLock[channel]);
                if (!m_bInReceive[channel]) {
                    break;
                }
            }
            Sleep(1);
        }
    }

    FILTER_STATE CBaseRenderer::GetRealState(int channel) {
        return m_ChannelState[channel];
    }

    // 收到第一个媒体样本才算完成到暂停状态的转换，暂停状态可渲染出第一帧，用于静态图片渲染。
    STDMETHODIMP CBaseRenderer::GetState(int channel, DWORD dwMSecs, FILTER_STATE* state)
    {
        CheckPointer(state, E_POINTER);

        if (WaitDispatchingMessages(m_evComplete[channel], dwMSecs) == WAIT_TIMEOUT) {
            *state = m_ChannelState[channel];
            return VFW_S_STATE_INTERMEDIATE;
        }
        *state = m_ChannelState[channel];
        return S_OK;
    }

    HRESULT CBaseRenderer::CompleteStateChange(int channel, FILTER_STATE OldState)
    {
        // 所有状态转换都可以立即完成状态转换，无需等待收到第一帧。
        // 对于实时源，暂停状态实际上是虚设的，没有什么实际意义，简化实现。
        Ready(channel);
        return S_OK;
    }

    STDMETHODIMP CBaseRenderer::Stop()
    {
        HRESULT hr = S_OK;
        ::DebugBreak();
        return hr;
    }

    STDMETHODIMP CBaseRenderer::Pause()
    {
        HRESULT hr = S_OK;
        ::DebugBreak();
        return hr;
    }

    STDMETHODIMP CBaseRenderer::Run(REFERENCE_TIME StartTime)
    {
        HRESULT hr = S_OK;
        ::DebugBreak();
        return hr;
    }

    STDMETHODIMP CBaseRenderer::StopChannel(int channel)
    {
        {
            CAutoLock channelLock(&m_InterfaceLock[channel]);

            if (m_ChannelState[channel] == State_Stopped) {
                return S_OK;
            }

            if (m_pInputPin[channel]->IsConnected()) {
                if (m_ChannelState[channel] != State_Stopped) {
                    m_pInputPin[channel]->Inactive();
                }
                m_ChannelState[channel] = State_Stopped;
            }
            else { // Disconnected
                m_ChannelState[channel] = State_Stopped;
                return S_OK;
            }

            // 通道停止后，所有内存将被系统回收，不占内存。这是我们可以静态创建16个通道的基础。
            if (m_pInputPin[channel]->Allocator()) {
                m_pInputPin[channel]->Allocator()->Decommit();
            }

            // Cancel any scheduled rendering
            SetRepaintStatus(channel, TRUE);
            StopStreaming(channel);
            ResetEndOfStream(channel);

            Ready(channel);
        }
        {
            WaitForReceiveToComplete(channel);
        }
        {
            CAutoLock channelLock(&m_InterfaceLock[channel]);
            m_bAbort[channel] = FALSE;
            ClearPendingSample(channel); // 清空正在等待混合呈现的样本。
        }
        m_sourceFrameInterval[channel] = 0; // 下次开始时，必须再次设置帧率。
        m_isFirstSampleReceived[channel] = false; // 是否收到过第一个样本。

        return S_OK;
    }

    // Pause状态等价于于只渲染一帧的运行状态。
    STDMETHODIMP CBaseRenderer::PauseChannel(int channel)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);
        FILTER_STATE OldState = m_ChannelState[channel];
        ASSERT(m_pInputPin[channel]->IsFlushing() == FALSE);

        if (m_ChannelState[channel] == State_Paused) {
            return CompleteStateChange(channel, State_Paused);
        }

        if (!m_pInputPin[channel]->IsConnected()) {
            m_ChannelState[channel] = State_Paused;
            return CompleteStateChange(channel, State_Paused);
        }

        if (m_ChannelState[channel] == State_Stopped) {
            if (m_pInputPin[channel]->IsConnected()) {
                HRESULT hr = m_pInputPin[channel]->Active();
                if (FAILED(hr)) {
                    return hr;
                }
            }
            m_ChannelState[channel] = State_Paused;
        }

        // Enable EC_REPAINT events again
        SetRepaintStatus(channel, TRUE);
        StopStreaming(channel);

        if (m_pInputPin[channel]->Allocator()) {
            m_pInputPin[channel]->Allocator()->Commit();
        }

        // There should be no outstanding advise
        ASSERT(!m_pInputPin[channel]->IsFlushing());

        if (OldState == State_Stopped) {
            m_bAbort[channel] = FALSE;
            ClearPendingSample(channel);
        }
        return CompleteStateChange(channel, OldState);
    }

    STDMETHODIMP CBaseRenderer::RunChannel(int channel, REFERENCE_TIME tStart)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);
        FILTER_STATE OldState = m_ChannelState[channel];

        // Make sure there really is a state change
        if (m_ChannelState[channel] == State_Running) {
            return S_OK;
        }
        
        // 平滑呈现（去抖动）的核心科技。
        {
            m_isFirstSample[channel] = true;
        }

        // Send EC_COMPLETE if we're not connected
        if (!m_pInputPin[channel]->IsConnected()) {
            NotifyEvent(EC_COMPLETE, S_OK, (LONG_PTR)(IBaseFilter*)this);
            m_ChannelState[channel] = State_Running;
            return S_OK;
        }

        Ready(channel);

        // Run the base filter class
        {
            // remember the stream time offset
            m_ChannelStart[channel] = tStart;

            // 从Stop状态必须先转换到Pause状态，再转换到Running状态。
            if (m_ChannelState[channel] == State_Stopped) {
                HRESULT hr = PauseChannel(channel);
                if (FAILED(hr)) {
                    return hr;
                }
            }

            if (m_ChannelState[channel] != State_Running) {
                if (m_pInputPin[channel]->IsConnected()) {
                    HRESULT hr = m_pInputPin[channel]->Run(tStart);
                    if (FAILED(hr)) {
                        return hr;
                    }
                }
            }

            m_ChannelState[channel] = State_Running;
        }

        // 初始化状态
        ASSERT(!m_pInputPin[channel]->IsFlushing());
        SetRepaintStatus(channel, FALSE);

        // 提交此通道的输入Pin的内存分配器保留的虚拟内存
        if (m_pInputPin[channel]->Allocator()) {
            m_pInputPin[channel]->Allocator()->Commit();
        }

        if (OldState == State_Stopped) {
            m_bAbort[channel] = FALSE;
            ClearPendingSample(channel);
        }
        return StartStreaming(channel);
    }

    int CBaseRenderer::GetPinCount()
    {
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            if (m_pInputPin[i] == NULL) {
                (void)GetPin(i);
            }
        }
        return INPUT_PIN_COUNT;
    }

    CBasePin* CBaseRenderer::GetPin(int n)
    {
        CAutoLock cObjectCreationLock(&m_ObjectCreationLock[n]);

        // Should only ever be called with zero
        ASSERT(n >= 0 && n < INPUT_PIN_COUNT);
        if (n < 0 || n >= INPUT_PIN_COUNT) {
            return NULL;
        }

        // Create the input pin if not already done so
        if (m_pInputPin[n] == NULL) {

            HRESULT hr = S_OK;

            m_pInputPin[n] = new CRendererInputPin(n, this, &hr, L"In");
            if (NULL == m_pInputPin[n]) {
                return NULL;
            }

            if (FAILED(hr)) {
                delete m_pInputPin[n];
                m_pInputPin[n] = NULL;
                return NULL;
            }
        }
        return m_pInputPin[n];
    }

    // Called when the input pin receives an EndOfStream notification. 
    HRESULT CBaseRenderer::EndOfStream(int channel)
    {
        if (m_ChannelState[channel] == State_Stopped) {
            return S_OK;
        }

        m_bEOS[channel] = TRUE;

        Ready(channel);

        if (m_bStreaming[channel]) {
            TryNotifyEndOfStream(channel);
        }

        return S_OK;
    }

    HRESULT CBaseRenderer::BeginFlush(int channel)
    {
        if (m_ChannelState[channel] == State_Paused) {
            NotReady(channel);
        }

        ClearPendingSample(channel);
        // 一直等到解码线程的渲染器收不到新的缓存中的帧，就说明冲洗干净了。
        WaitForReceiveToComplete(channel);

        return S_OK;
    }

    HRESULT CBaseRenderer::EndFlush(int channel)
    {
        // 可能是上游解码器动态重连时，内存分配超过100ms仍然没有成功，因此发送结束通知。
        ::DebugBreak();
        return S_OK;
    }

    HRESULT CBaseRenderer::CompleteConnect(int channel, IPin *pReceivePin)
    {
        ASSERT(CritCheckIn(&m_InterfaceLock[channel]));
        m_bAbort[channel] = FALSE;

        if (State_Running == GetRealState(channel)) {
            HRESULT hr = StartStreaming(channel);
            if (FAILED(hr)) {
                return hr;
            }
            SetRepaintStatus(channel, FALSE);
        } else {
            SetRepaintStatus(channel, TRUE);
        }

        return S_OK;
    }

    HRESULT CBaseRenderer::Active(int channel)
    {
    return S_OK;
    }

    HRESULT CBaseRenderer::Inactive(int channel)
    {
        ClearPendingSample(channel);
        return S_OK;
    }

    HRESULT CBaseRenderer::SetMediaType(int channel, const CMediaType* pmt)
    {
        return S_OK;
    }

    HRESULT CBaseRenderer::BreakConnect(int channel)
    {
        // Check we have a valid connection
        if (m_pInputPin[channel]->IsConnected() == FALSE) {
            return S_FALSE;
        }

        // Check we are stopped before disconnecting
        if (m_ChannelState[channel] != State_Stopped
            && !m_pInputPin[channel]->CanReconnectWhenActive()) {
            return VFW_E_NOT_STOPPED;
        }
        SetRepaintStatus(channel, FALSE);
        ResetEndOfStream(channel);
        ClearPendingSample(channel);
        m_bAbort[channel] = FALSE;

        if (State_Running == m_ChannelState[channel]) {
            StopStreaming(channel);
        }

        return S_OK;
    }

    // return S_OK if the sample should be rendered immediately
    HRESULT CBaseRenderer::GetSampleTimes(int channel, IMediaSample* pMediaSample,
        __out REFERENCE_TIME* pStartTime, __out REFERENCE_TIME* pEndTime)
    {
        if (SUCCEEDED(pMediaSample->GetTime(pStartTime, pEndTime))) {
            if (*pEndTime < *pStartTime) {
                return VFW_E_START_TIME_AFTER_END;
            }
        }
        else {
            return S_OK;
        }
        if (m_pClock == NULL) {
            return S_OK;
        }
        return S_OK;
    }

    IMediaSample* CBaseRenderer::GetCurrentSample(int channel)
    {
        CAutoLock channelLock(&m_pendingSampleQueueLock[channel]);
        if (m_pendingSampleQueue[channel][0]) {
            m_pendingSampleQueue[channel][0]->AddRef();
        }
        return m_pendingSampleQueue[channel][0];
    }

    // 在解码线程中检查并设置m_bInReceive[channel]。
    // 如果为FALSE，说明没有更多的样本可接收了，那么Graph控制线程的BeginFlush()就会结束。
    HRESULT CBaseRenderer::PrepareReceive(int channel, IMediaSample* pMediaSample)
    {
        // 每一帧的样本格式都可能不同，不要把格式设置到Pin上，而是放到样本中。
        // 解码和渲染过程全部根据媒体样本中的格式说明来，简单，可靠，快速，灵活。
        {
            CAutoLock cInterfaceLock(&m_pendingSampleQueueLock[channel]);

            // 基类对样本的媒体类型等进行一系列检查，并保存样本的属性信息到成员变量。
            HRESULT hr = m_pInputPin[channel]->CBaseInputPin::Receive(pMediaSample);
            if (hr != S_OK) {
                m_bInReceive[channel] = FALSE; // 格式有问题或停止流化了？
                return E_FAIL;
            }

            ASSERT(!m_pInputPin[channel]->IsFlushing());
            ASSERT(m_pInputPin[channel]->IsConnected());
            if (m_pendingSampleQueue[channel][0] != nullptr) {
                Ready(channel); // 第一帧已经有了，通过MixedGraph播放控制线程已完成到Pause状态的转换了！
            }
            if (m_bEOS[channel] || m_bAbort[channel]) {
                m_bInReceive[channel] = FALSE; // VOD节目播放完成了。
                return E_UNEXPECTED;
            }

            if (m_ChannelState[channel] != State_Running) {
                m_bInReceive[channel] = FALSE; // 暂停或者停止了。
                return S_OK;
            }
        }

        // 以前考虑一个帧的停止时间，定时等待最后一帧呈现40ms之后，再发送EOS通知。
        // 这种设计是没必要的，对实时源没有意义。对于录像播放完了，画面会停止最后一帧“The End”。
        // 如果真需要停止，则可让上层知道EOS后，再给一个用户可配置的等待时间，显示“The End”。
        // 等待40ms就EOS是不对的。用户可能根本没有发现最后一帧的状态就出现广告了。
        PushPendingSample(channel, pMediaSample);
    
        {
            CAutoLock cInterfaceLock(&m_InterfaceLock[channel]);

            if (!m_bStreaming[channel]) {
                SetRepaintStatus(channel, TRUE); // 暂停或者已停止推流了，那么就要重绘最后一帧。
            }
        }

        return S_OK;
    }

    HRESULT CBaseRenderer::Receive(int channel, IMediaSample* pSample)
    {
        HRESULT hr = S_OK;
        //
        // 安排混合前的准备工作：
        //    1.如果播放控制线程正在等待状态转换到Pause状态，我们应当激发已完成事件。
        //    2.判断一下当前局势，看是否已收到EOS通知或者要求终止渲染。
        //
        {
            CAutoLock channelLock(&m_InterfaceLock[channel]);
            m_bInReceive[channel] = TRUE;
        }
        {
            hr = PrepareReceive(channel, pSample);
            if (FAILED(hr))
                return hr;
        }
        // CBaseRenderer::WaitForReceiveToComplete()可以完成了码？
        // 如果还有帧呢？不可能，上级输出缓冲已经清空了。
        {
            CAutoLock channelLock(&m_InterfaceLock[channel]);
            m_bInReceive[channel] = FALSE; 
        }
        {
            CAutoLock channelLock(&m_InterfaceLock[channel]);
            if (m_ChannelState[channel] == State_Stopped)
                return S_OK;

            TryNotifyEndOfStream(channel);
        }

        return S_OK;
    }

    // 当在Seek动作时，需要BeginFlush，洗刷所有filter的输出缓冲区。
    HRESULT CBaseRenderer::ClearPendingSample(int channel)
    {
        CAutoLock lock(&m_pendingSampleQueueLock[channel]);

        for (int i = 0; i < m_pendingSampleCount[channel]; i++) {
            if (m_pendingSampleQueue[channel][i] != nullptr) {
                m_pendingSampleQueue[channel][i]->Release();
                m_pendingSampleQueue[channel][i] = nullptr;
            }
        }
        m_pendingSampleCount[channel] = 0;

        return S_OK;
    }

    void CBaseRenderer::PushPendingSample(int channel, IMediaSample* ms)
    {
        REFERENCE_TIME tsStart = 0;
        REFERENCE_TIME tsStop = 0;
        bool isQueueFull = false;

        m_receivedSampleCount[channel]++;

//#ifdef _DEBUG
        {
            CAutoLock lock(&m_pendingSampleQueueLock[channel]);

            DWORD curTime = timeGetTime();
            if (curTime - m_lastPrintTime[channel] >= 5000) {
                printf("Sample/s=%d, PSQL=%d\n", m_receivedSampleCount[channel] / 5, m_pendingSampleCount[channel]);
                m_lastPrintTime[channel] = curTime;
                m_receivedSampleCount[channel] = 0;
            }
        }
//#endif
        // 推到待呈现队列，窗口线程可以一次性打包带走。
        {
            CAutoLock lock(&m_pendingSampleQueueLock[channel]);
            int count = ++m_pendingSampleCount[channel];
            assert(count <= JITTER_BUFFER); // 不可溢出
            m_pendingSampleQueue[channel][count - 1] = ms;
            ms->AddRef();
            m_isFirstSampleReceived[channel] = true;
        }
    }

    HRESULT CBaseRenderer::TryNotifyEndOfStream(int channel)
    {
        ASSERT(CritCheckIn(&m_InterfaceLock[channel]));
        if (!m_bEOS[channel] || m_bEOSDelivered[channel]) {
            return S_OK;
        }

        ASSERT(m_pClock == nullptr);
        NotifyEndOfStream(channel);

        return S_OK;
    }

    // Signals EC_COMPLETE to the filtergraph manager
    HRESULT CBaseRenderer::NotifyEndOfStream(int channel)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);
        ASSERT(!m_bEOSDelivered[channel]);

        // Has the filter changed state
        if (m_bStreaming[channel] == FALSE) {
            return S_OK;
        }

        m_bEOSDelivered[channel] = TRUE;

        return NotifyEvent(EC_COMPLETE, S_OK, (LONG_PTR)(IBaseFilter*)this);
    }

    HRESULT CBaseRenderer::ResetEndOfStream(int channel)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);

        m_bEOS[channel] = FALSE;
        m_bEOSDelivered[channel] = FALSE;

        return S_OK;
    }


    HRESULT CBaseRenderer::StartStreaming(int channel)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);
        if (m_bStreaming[channel]) {
            return S_OK;
        }
        m_bStreaming[channel] = TRUE;

        // If we have an EOS and no data then deliver it now
        CAutoLock lock(&m_pendingSampleQueueLock[channel]);
        if (m_pendingSampleQueue[channel][0] == nullptr) {
            return TryNotifyEndOfStream(channel);
        }

        return S_OK;
    }

    HRESULT CBaseRenderer::StopStreaming(int channel)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);
        m_bEOSDelivered[channel] = FALSE;
        if (m_bStreaming[channel]) {
            m_bStreaming[channel] = FALSE;
        }
        return S_OK;
    }

    void CBaseRenderer::SetRepaintStatus(int channel, BOOL bRepaint)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);
        m_bRepaintStatus[channel] = bRepaint;
    }

    // Signal an EC_REPAINT to the filter graph. 
    void CBaseRenderer::SendRepaint(int channel)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);
        ASSERT(m_pInputPin[channel]);

        if (!m_bAbort[channel]) { // 尚未出错退出
            if (m_pInputPin[channel]->IsConnected()) { // 连接尚未断开
                if (!m_pInputPin[channel]->IsFlushing()) { // 非冲洗渲染管线的的状态。
                    if (!IsEndOfStream(channel)) { // 流尚未结束
                        if (m_bRepaintStatus[channel]) { // 尚未标记为需要重绘最后一帧，通常是暂停状态。
                            IPin* pPin = (IPin*)m_pInputPin[channel];
                            NotifyEvent(EC_REPAINT, (LONG_PTR)pPin, 0); // 通知窗口消息循环执行调用渲染器的重绘方法。
                            SetRepaintStatus(channel, FALSE); // 已发送了重绘通知到窗口线程。
                        }
                    }
                }
            }
        }
    }

    // 收到WM_DISPLAYCHANGE消息后，发送EC_DISPLAY_CHANGED事件给渲染器的所有输入Pin。
    BOOL CBaseRenderer::OnDisplayChange()
    {
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            CAutoLock channelLock(&m_InterfaceLock[i]);
            {
                if (!m_pInputPin[i]->IsConnected())
                    continue;

                // Pass our input pin as parameter on the event
                IPin* pPin = (IPin*)m_pInputPin[i];
                pPin->AddRef();
                {
                    NotifyEvent(EC_DISPLAY_CHANGED, (LONG_PTR)pPin, 0);
                    SetAbortSignal(i, TRUE);
                    ClearPendingSample(i);
                }
                pPin->Release();
            }
        }
        return TRUE;
    }

    void CBaseRenderer::SetDefaultViewportDesc()
    {
        auto calc = [=](int mode, int rows, int cols) {
            for (int i = 0; i < rows; ++i) {
                for (int j = 0; j < cols; ++j) {
                    int channel = i * cols + j;
                    ViewportDesc_t& vd = _viewportDesc[mode][channel];
                    vd.x = (float)j / (float)cols;
                    vd.y = (float)i / (float)rows;
                    vd.w = 1.0f / cols;
                    vd.h = 1.0f / rows;
                }
            }
        };
        calc(VIEW_MODE_1x1, 1, 1);
        calc(VIEW_MODE_2x2, 2, 2);
        calc(VIEW_MODE_3x3, 3, 3);
        calc(VIEW_MODE_4x4, 4, 4);
    }

    // 只有窗口线程读写。
    void CBaseRenderer::CalcLayout()
    {
        for (int i = VIEW_MODE_1x1; i < VIEW_MODE_COUNT; i++) {
            for (int j = 0; j < INPUT_PIN_COUNT; j++) {
                CalcChannelLayout(i, j);
            }
        }
    }

    void CBaseRenderer::CalcChannelLayout(int mode, int channel)
    {
        const ViewportDesc_t& vd = _viewportDesc[mode][channel];
        RECT& vr = _viewportRect[mode][channel];
        LONG tw = WIDTH(&_posRect);
        LONG th = HEIGHT(&_posRect);
        vr.left = (LONG)(vd.x * tw + 0.999f);
        vr.top = (LONG)(vd.y * th + 0.999f);
        vr.right = vr.left + (LONG)(vd.w * tw + 0.999f);
        vr.bottom = vr.top + (LONG)(vd.h * th + 0.999f);
    }
} // end namespace VideoRenderer