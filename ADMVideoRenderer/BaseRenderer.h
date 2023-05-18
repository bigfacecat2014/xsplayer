#pragma once

namespace VideoRenderer {

    // Forward class declarations
    class CBaseRenderer;
    class CRendererInputPin;

    //-----------------------------------------------------------------------------
    // Uncompressed video samples input pin 
    //-----------------------------------------------------------------------------
    class CRendererInputPin : public CBaseInputPin
    {
    protected:
        int m_channel = -1;
        CBaseRenderer* m_pRenderer;

    public:

        CRendererInputPin(__in int channel,
            __inout CBaseRenderer* pRenderer,
            __inout HRESULT* phr,
            __in_opt LPCWSTR Name);

        // Overriden from the base pin classes
        HRESULT CheckStreaming() { return S_OK; }
        HRESULT BreakConnect();
        HRESULT CompleteConnect(IPin* pReceivePin);
        HRESULT SetMediaType(const CMediaType* pmt);
        HRESULT CheckMediaType(const CMediaType* pmt);
        HRESULT Active();
        HRESULT Inactive();

        // Check if our filter is currently stopped
        virtual BOOL IsStopped();

        // Add rendering behaviour to interface functions

        STDMETHODIMP QueryId(__deref_out LPWSTR* Id);
        STDMETHODIMP EndOfStream();
        STDMETHODIMP BeginFlush();
        STDMETHODIMP EndFlush();
        STDMETHODIMP Receive(IMediaSample* pMediaSample);

        // Helper
        IMemAllocator inline* Allocator() const
        {
            return m_pAllocator;
        }
    };

    //-----------------------------------------------------------------------------
    // Multi input pin video renderer base class
    //-----------------------------------------------------------------------------
    class CBaseRenderer : public CBaseFilter
    {
    public:
        // 更多用户自定义布局，采用字符串作为字典索引保存。
        // 系统内置16个用整数索引的布局，可应对90%以上的需求。
        // 如果用户需要超过16个自定义布局，则需要另存为xxx视图模式，系统将其保存在std::map中。
        // 这与WIN32的消息注册机制如出一辙。
        enum VIEW_MODE {
            VIEW_MODE_1x1,
            VIEW_MODE_2x2,
            VIEW_MODE_3x3,
            VIEW_MODE_4x4,
            VIEW_MODE_COUNT
        };

        // 变速播放参数
        enum BOOST_PARAM {
            BP_IDEA_QUEUE_LEN = 2, // 预期的待呈现样本队列长度。
        };

        // 变速播放状态
        enum BOOST_STATE {
            BS_NORMAL,
            BS_FASTER,
            BS_SLOWER,
        };

        // 用于平滑抖动的样本缓冲区容量（单位：样本个数)。
        enum { JITTER_BUFFER = 8 }; // 最大容忍10帧的传输+解码复合原因的延迟抖动（延迟+卡顿）。

    protected:
        friend class CRendererInputPin;

        friend void CALLBACK EndOfStreamTimer(UINT uID,      // Timer identifier
            UINT uMsg,     // Not currently used
            DWORD_PTR dwUser,  // User information
            DWORD_PTR dw1,     // Windows reserved
            DWORD_PTR dw2);    // Is also reserved

        CCritSec m_PresenterLock;

        // 显示设备描述
        HWND _hwndHost = 0; // 宿主窗口句柄。
        RECT _posRect = { 0 }; // 物理设备坐标系：控件位置矩形
        RECT _clipRect = { 0 }; // 物理设备坐标系：窗口的逻辑画布的剪裁矩形（视口）。
        VIEW_MODE _viewMode = VIEW_MODE_1x1; // 默认为单视口模式
        ViewportDesc_t _viewportDesc[VIEW_MODE_COUNT][INPUT_PIN_COUNT]; // 每种视图模式下的每个通道对应的视口描述（相对坐标）
        RECT _viewportRect[VIEW_MODE_COUNT][INPUT_PIN_COUNT]; // 每种视图模式下的每个通道的边界矩形（视图变化时动态计算出来的像素坐标）

        CRefTime m_ChannelStart[INPUT_PIN_COUNT]; // offset from stream time to reference time
        FILTER_STATE m_ChannelState[INPUT_PIN_COUNT]; // channel current state: running, paused, m_State作为Presenter的状态。

        CAMEvent* m_evComplete[INPUT_PIN_COUNT]; // 通知状态转换过程已完成。（转到Pause状态可能要等）
        BOOL m_bAbort[INPUT_PIN_COUNT]; // Stop us from rendering more data
        BOOL m_bStreaming[INPUT_PIN_COUNT]; // Are we currently streaming

        // -----------------------------------------------------------------------------------------------
        // 每个通道已借给混合呈现器进行多视口混合输出的缓冲区指针。
        // 混合呈现器混合完成后，会主动归还释放，被ImageAllocator的空闲缓冲区池回收。
        // 为以下变量定义专门的锁，只可用于解码线程和窗口线程之间的数据单向交换。
        //
        // 解码线程持有的，已解码的，待窗口线程打包带走的样本队列。
        // 解码线程和窗口线程并发访问，需要加锁。
        CCritSec m_pendingSampleQueueLock[INPUT_PIN_COUNT];
        IMediaSample* m_pendingSampleQueue[INPUT_PIN_COUNT][JITTER_BUFFER]; // 数据推送和解码线程的抖动缓冲区。
        int m_pendingSampleCount[INPUT_PIN_COUNT] = { 0 }; // 解码线程已解码的，等待窗口线程打包带走的样本个数。
        int m_receivedSampleCount[INPUT_PIN_COUNT] = { 0 }; // 用于统计解码线程每5秒内累计输出的样本数，定时重置。
        DWORD m_lastPrintTime[INPUT_PIN_COUNT] = { 0 }; // 用于每隔5秒打印一次解码线程的样本输出帧率。
        volatile bool m_isFirstSampleReceived[INPUT_PIN_COUNT] = { false }; // 从Pause|Run开始，是否收到过第一个样本,Stop时重置。
        //
        // -----------------------------------------------------------------------------------------------

        // 解码线程写入，窗口线程读取。
        volatile DWORD m_sourceFrameInterval[INPUT_PIN_COUNT] = { 0 }; // 媒体源提供的平均帧间隔。

        //------------------------------------------------------------------------------------------------
        // 窗口线程呈现时使用的字段
        // 仅窗口线程访问
        IMediaSample* m_presentSampleQueue[INPUT_PIN_COUNT][JITTER_BUFFER]; // 窗口线程的待呈现样本队列。
        IMediaSample* m_lastPresentSample[INPUT_PIN_COUNT]; // 最后一次呈现的样本指针。
        int m_presentSampleCount[INPUT_PIN_COUNT] = { 0 }; // 窗口线程已重新排程的，等待呈现的样本个数。
        DWORD m_presentIntervalAdjust[INPUT_PIN_COUNT] = { 0 }; // 帧呈现间隔调整百分比的分子，定义域[0, 100]。
        DWORD m_presentInterval[INPUT_PIN_COUNT] = { 0 }; // 动态计算出来的两帧之间的呈现间隔，仅用于诊断。
        DWORD m_debugLastPTS[INPUT_PIN_COUNT] = { 0 }; // 最后一次已呈现的样本的PTS。（窗口线程填写）
        volatile bool m_isFirstSample[INPUT_PIN_COUNT] = { true }; // 是否为通道的第一个待呈现样本。
        DWORD m_presentTimeBaseLine[INPUT_PIN_COUNT] = { 0 }; // 呈现时间基线。
        DWORD m_lastPTS[INPUT_PIN_COUNT] = { 0 }; // 上一个呈现时间线。
        DWORD m_nextPTS[INPUT_PIN_COUNT] = { 0 }; // 呈现时间当前线。
        BOOST_STATE m_boostState[INPUT_PIN_COUNT] = { BS_NORMAL };
        //
        //------------------------------------------------------------------------------------------------

        BOOL m_bEOS[INPUT_PIN_COUNT]; // Any more samples in the stream
        BOOL m_bEOSDelivered[INPUT_PIN_COUNT]; // Have we delivered an EC_COMPLETE
        CRendererInputPin* m_pInputPin[INPUT_PIN_COUNT]; // Our renderer input pin object
        CCritSec m_InterfaceLock[INPUT_PIN_COUNT]; // Critical section for interfaces
        BOOL m_bRepaintStatus[INPUT_PIN_COUNT]; // Can we signal an EC_REPAINT
        //  Avoid some deadlocks by tracking filter during stop
        volatile BOOL m_bInReceive[INPUT_PIN_COUNT]; // 在PrepareReceive()和DoRenderSample()之间。
        CCritSec m_ObjectCreationLock[INPUT_PIN_COUNT]; // 防止两个线程并发GetPin(n)获取m_pInputPin[n]时发生读写冲突。

    public:
        CBaseRenderer(REFCLSID RenderClass, // CLSID for this renderer
            __in_opt LPCTSTR pName, // Debug ONLY description
            __inout_opt LPUNKNOWN pUnk, // Aggregated owner object
            __inout HRESULT* phr); // General OLE return code
        ~CBaseRenderer();

        STDMETHODIMP NonDelegatingQueryInterface(REFIID, __deref_out void**);

        virtual HRESULT CompleteStateChange(int channel, FILTER_STATE OldState);

        // Return internal information about this filter
        BOOL IsEndOfStream(int channel) { return m_bEOS[channel]; };
        BOOL IsEndOfStreamDelivered(int channel) { return m_bEOSDelivered[channel]; };
        BOOL IsStreaming(int channel) { return m_bStreaming[channel]; };
        void SetAbortSignal(int channel, BOOL bAbort) { m_bAbort[channel] = bAbort; };

        // Permit access to the transition state
        void Ready(int channel) { m_evComplete[channel]->Set(); };
        void NotReady(int channel) { m_evComplete[channel]->Reset(); };
        BOOL CheckReady(int channel) { return m_evComplete[channel]->Check(); };

        virtual int GetPinCount();
        virtual CBasePin* GetPin(int n);

        FILTER_STATE GetRealState(int channel);
        void SendRepaint(int channel);
        void SendNotifyWindow(int channel, IPin* pPin, HWND hwnd);
        BOOL OnDisplayChange();
        void SetRepaintStatus(int channel, BOOL bRepaint);

        // Override the filter and pin interface functions
        STDMETHODIMP Stop();
        STDMETHODIMP Pause();
        STDMETHODIMP Run(REFERENCE_TIME StartTime);

        STDMETHODIMP StopChannel(int channel);
        STDMETHODIMP PauseChannel(int channel);
        STDMETHODIMP RunChannel(int channel, REFERENCE_TIME StartTime);
        STDMETHODIMP GetState(int channel, DWORD dwMSecs, FILTER_STATE* State);

        virtual HRESULT GetSampleTimes(int channel, IMediaSample* pMediaSample,
            REFERENCE_TIME* pStartTime, REFERENCE_TIME* pEndTime);

        // Lots of end of stream complexities
        HRESULT NotifyEndOfStream(int channel);
        virtual HRESULT TryNotifyEndOfStream(int channel);
        virtual HRESULT ResetEndOfStream(int channel);
        virtual HRESULT EndOfStream(int channel);
        virtual HRESULT ClearPendingSample(int channel);
        void PushPendingSample(int channel, IMediaSample* ms);

        // Called when the filter changes state
        virtual HRESULT Active(int channel);
        virtual HRESULT Inactive(int channel);
        virtual HRESULT StartStreaming(int channel);
        virtual HRESULT StopStreaming(int channel);
        virtual HRESULT BeginFlush(int channel);
        virtual HRESULT EndFlush(int channel);

        // Deal with connections and type changes
        virtual HRESULT BreakConnect(int channel);
        virtual HRESULT SetMediaType(int channel, const CMediaType* pmt);
        virtual HRESULT CompleteConnect(int channel, IPin* pReceivePin);

        // These look after the handling of data samples
        virtual HRESULT PrepareReceive(int channel, IMediaSample* pMediaSample);
        virtual HRESULT Receive(int channel, IMediaSample* pMediaSample);
        virtual IMediaSample* GetCurrentSample(int channel);

        // Derived classes MUST override these
        virtual HRESULT CheckMediaType(int channel, const CMediaType* pMediaType) = 0;
        virtual HRESULT DoRenderSample(int channel, IMediaSample* pMediaSample, HDC hdcDraw) = 0;

        // Stop调用线程等待通道的Renderer线程处理完最后一帧。
        // 必须先停止源，解码器，否则渲染线程将会一直忙个不停，收不完的帧。
        void WaitForReceiveToComplete(int channel);

        virtual void SetDefaultViewportDesc();
        virtual void CalcLayout();
        virtual void CalcChannelLayout(int mode, int channel);
    };
} // end namespace VideoRenderer