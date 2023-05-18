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
        // �����û��Զ��岼�֣������ַ�����Ϊ�ֵ��������档
        // ϵͳ����16�������������Ĳ��֣���Ӧ��90%���ϵ�����
        // ����û���Ҫ����16���Զ��岼�֣�����Ҫ���Ϊxxx��ͼģʽ��ϵͳ���䱣����std::map�С�
        // ����WIN32����Ϣע��������һ�ޡ�
        enum VIEW_MODE {
            VIEW_MODE_1x1,
            VIEW_MODE_2x2,
            VIEW_MODE_3x3,
            VIEW_MODE_4x4,
            VIEW_MODE_COUNT
        };

        // ���ٲ��Ų���
        enum BOOST_PARAM {
            BP_IDEA_QUEUE_LEN = 2, // Ԥ�ڵĴ������������г��ȡ�
        };

        // ���ٲ���״̬
        enum BOOST_STATE {
            BS_NORMAL,
            BS_FASTER,
            BS_SLOWER,
        };

        // ����ƽ��������������������������λ����������)��
        enum { JITTER_BUFFER = 8 }; // �������10֡�Ĵ���+���븴��ԭ����ӳٶ������ӳ�+���٣���

    protected:
        friend class CRendererInputPin;

        friend void CALLBACK EndOfStreamTimer(UINT uID,      // Timer identifier
            UINT uMsg,     // Not currently used
            DWORD_PTR dwUser,  // User information
            DWORD_PTR dw1,     // Windows reserved
            DWORD_PTR dw2);    // Is also reserved

        CCritSec m_PresenterLock;

        // ��ʾ�豸����
        HWND _hwndHost = 0; // �������ھ����
        RECT _posRect = { 0 }; // �����豸����ϵ���ؼ�λ�þ���
        RECT _clipRect = { 0 }; // �����豸����ϵ�����ڵ��߼������ļ��þ��Σ��ӿڣ���
        VIEW_MODE _viewMode = VIEW_MODE_1x1; // Ĭ��Ϊ���ӿ�ģʽ
        ViewportDesc_t _viewportDesc[VIEW_MODE_COUNT][INPUT_PIN_COUNT]; // ÿ����ͼģʽ�µ�ÿ��ͨ����Ӧ���ӿ�������������꣩
        RECT _viewportRect[VIEW_MODE_COUNT][INPUT_PIN_COUNT]; // ÿ����ͼģʽ�µ�ÿ��ͨ���ı߽���Σ���ͼ�仯ʱ��̬����������������꣩

        CRefTime m_ChannelStart[INPUT_PIN_COUNT]; // offset from stream time to reference time
        FILTER_STATE m_ChannelState[INPUT_PIN_COUNT]; // channel current state: running, paused, m_State��ΪPresenter��״̬��

        CAMEvent* m_evComplete[INPUT_PIN_COUNT]; // ֪ͨ״̬ת����������ɡ���ת��Pause״̬����Ҫ�ȣ�
        BOOL m_bAbort[INPUT_PIN_COUNT]; // Stop us from rendering more data
        BOOL m_bStreaming[INPUT_PIN_COUNT]; // Are we currently streaming

        // -----------------------------------------------------------------------------------------------
        // ÿ��ͨ���ѽ����ϳ��������ж��ӿڻ������Ļ�����ָ�롣
        // ��ϳ����������ɺ󣬻������黹�ͷţ���ImageAllocator�Ŀ��л������ػ��ա�
        // Ϊ���±�������ר�ŵ�����ֻ�����ڽ����̺߳ʹ����߳�֮������ݵ��򽻻���
        //
        // �����̳߳��еģ��ѽ���ģ��������̴߳�����ߵ��������С�
        // �����̺߳ʹ����̲߳������ʣ���Ҫ������
        CCritSec m_pendingSampleQueueLock[INPUT_PIN_COUNT];
        IMediaSample* m_pendingSampleQueue[INPUT_PIN_COUNT][JITTER_BUFFER]; // �������ͺͽ����̵߳Ķ�����������
        int m_pendingSampleCount[INPUT_PIN_COUNT] = { 0 }; // �����߳��ѽ���ģ��ȴ������̴߳�����ߵ�����������
        int m_receivedSampleCount[INPUT_PIN_COUNT] = { 0 }; // ����ͳ�ƽ����߳�ÿ5�����ۼ����������������ʱ���á�
        DWORD m_lastPrintTime[INPUT_PIN_COUNT] = { 0 }; // ����ÿ��5���ӡһ�ν����̵߳��������֡�ʡ�
        volatile bool m_isFirstSampleReceived[INPUT_PIN_COUNT] = { false }; // ��Pause|Run��ʼ���Ƿ��յ�����һ������,Stopʱ���á�
        //
        // -----------------------------------------------------------------------------------------------

        // �����߳�д�룬�����̶߳�ȡ��
        volatile DWORD m_sourceFrameInterval[INPUT_PIN_COUNT] = { 0 }; // ý��Դ�ṩ��ƽ��֡�����

        //------------------------------------------------------------------------------------------------
        // �����̳߳���ʱʹ�õ��ֶ�
        // �������̷߳���
        IMediaSample* m_presentSampleQueue[INPUT_PIN_COUNT][JITTER_BUFFER]; // �����̵߳Ĵ������������С�
        IMediaSample* m_lastPresentSample[INPUT_PIN_COUNT]; // ���һ�γ��ֵ�����ָ�롣
        int m_presentSampleCount[INPUT_PIN_COUNT] = { 0 }; // �����߳��������ų̵ģ��ȴ����ֵ�����������
        DWORD m_presentIntervalAdjust[INPUT_PIN_COUNT] = { 0 }; // ֡���ּ�������ٷֱȵķ��ӣ�������[0, 100]��
        DWORD m_presentInterval[INPUT_PIN_COUNT] = { 0 }; // ��̬�����������֮֡��ĳ��ּ������������ϡ�
        DWORD m_debugLastPTS[INPUT_PIN_COUNT] = { 0 }; // ���һ���ѳ��ֵ�������PTS���������߳���д��
        volatile bool m_isFirstSample[INPUT_PIN_COUNT] = { true }; // �Ƿ�Ϊͨ���ĵ�һ��������������
        DWORD m_presentTimeBaseLine[INPUT_PIN_COUNT] = { 0 }; // ����ʱ����ߡ�
        DWORD m_lastPTS[INPUT_PIN_COUNT] = { 0 }; // ��һ������ʱ���ߡ�
        DWORD m_nextPTS[INPUT_PIN_COUNT] = { 0 }; // ����ʱ�䵱ǰ�ߡ�
        BOOST_STATE m_boostState[INPUT_PIN_COUNT] = { BS_NORMAL };
        //
        //------------------------------------------------------------------------------------------------

        BOOL m_bEOS[INPUT_PIN_COUNT]; // Any more samples in the stream
        BOOL m_bEOSDelivered[INPUT_PIN_COUNT]; // Have we delivered an EC_COMPLETE
        CRendererInputPin* m_pInputPin[INPUT_PIN_COUNT]; // Our renderer input pin object
        CCritSec m_InterfaceLock[INPUT_PIN_COUNT]; // Critical section for interfaces
        BOOL m_bRepaintStatus[INPUT_PIN_COUNT]; // Can we signal an EC_REPAINT
        //  Avoid some deadlocks by tracking filter during stop
        volatile BOOL m_bInReceive[INPUT_PIN_COUNT]; // ��PrepareReceive()��DoRenderSample()֮�䡣
        CCritSec m_ObjectCreationLock[INPUT_PIN_COUNT]; // ��ֹ�����̲߳���GetPin(n)��ȡm_pInputPin[n]ʱ������д��ͻ��

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

        // Stop�����̵߳ȴ�ͨ����Renderer�̴߳��������һ֡��
        // ������ֹͣԴ����������������Ⱦ�߳̽���һֱæ����ͣ���ղ����֡��
        void WaitForReceiveToComplete(int channel);

        virtual void SetDefaultViewportDesc();
        virtual void CalcLayout();
        virtual void CalcChannelLayout(int mode, int channel);
    };
} // end namespace VideoRenderer