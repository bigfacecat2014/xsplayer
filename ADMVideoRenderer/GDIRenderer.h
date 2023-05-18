#pragma once

namespace VideoRenderer {

    class CGDIVideoInputPin;
    class CGDIVideoRenderer;

    //---------------------------------------------------------------------------------------------
    // GDI��Ƶ�����Ⱦ��������PIN����
    //---------------------------------------------------------------------------------------------
    class CGDIVideoInputPin : public CRendererInputPin
    {
    protected:
        CGDIVideoRenderer* m_pRenderer;        // The renderer that owns us
        CCritSec* m_pInterfaceLock;         // Main filter critical section

    public:
        CGDIVideoInputPin(
            int channel,
            TCHAR* pObjectName,             // Object string description
            CGDIVideoRenderer* pRenderer,      // Used to delegate locking
            CCritSec* pInterfaceLock,       // Main critical section
            HRESULT* phr,                   // OLE failure return code
            LPCWSTR pPinName);              // This pins identification

        // Manage our DIBSECTION video allocator
        STDMETHODIMP GetAllocator(IMemAllocator** ppAllocator);
        STDMETHODIMP NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly);
        STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES* pProps) {
            return CRendererInputPin::GetAllocatorRequirements(pProps);
        }

        // ö�ٱ��÷���
        HRESULT GetMediaType(int iPosition, __inout CMediaType* pMediaType);
        HRESULT CheckMediaType(const CMediaType* pmt);

        STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt)
        {
            return CRendererInputPin::ReceiveConnection(pConnector, pmt);
        }
    };


    //---------------------------------------------------------------------------------------------
    // GDI��Ƶ�����Ⱦ������
    //---------------------------------------------------------------------------------------------
    class CGDIVideoRenderer : public CBaseRenderer, public ICommand
    {
    public:
        enum { CHANNEL_BUFFER_COUNT = 3 };
        enum { PRESENT_BUFFER_COUNT = 3 };

        CGDIVideoRenderer(HWND hwndHost, TCHAR* pName, LPUNKNOWN pUnk, HRESULT* phr);
        ~CGDIVideoRenderer();

        DECLARE_IUNKNOWN
        STDMETHODIMP NonDelegatingQueryInterface(REFIID, void**);

        // ICommand implementation
        STDMETHOD(SetObjectRects)(LPCRECT posRect, LPCRECT clipRect);
        STDMETHOD(SetViewMode)(int mode);
        STDMETHOD(SetLayout)(int channel, const ViewportDesc_t* desc);
        STDMETHOD(SetSourceFrameInterval)(int channel, DWORD frameInterval);
        STDMETHOD(SetNotifyReceiver)(INotify* receiver);

        STDMETHOD_(BOOL, Update(TimeContext* tc));
        STDMETHOD_(void, Render(DeviceContext* dc));

        STDMETHOD(Stop)(int channel);
        STDMETHOD(Pause)(int channel);
        STDMETHOD(Run)(int channel, REFERENCE_TIME startTime);

        CBasePin* GetPin(int n);

        HRESULT Active(int channel);
        HRESULT BreakConnect(int channel);
        HRESULT CompleteConnect(int channel, IPin* pReceivePin);
        HRESULT SetMediaType(int channel, const CMediaType* pmt);
        HRESULT CheckMediaType(int channel, const CMediaType* pmtIn);

        BOOL Update(int channel, TimeContext* tc);
        HRESULT Render(int channel, HDC hdcDraw);
        HRESULT DoRenderSample(int channel, IMediaSample* pMediaSample, HDC hdcDraw);

    public:

        CImageDisplay _display; // ��ʾ���ĳ��������װ��ʾ���ظ�ʽ��ϸ�ڡ�
        CDrawImage* _drawImage[INPUT_PIN_COUNT]; // DIB��Ⱦ����

        // ����PIN����
        CGDIVideoInputPin* _inputPin[INPUT_PIN_COUNT]; // IPin based interfaces
        CMediaType _mtIn[INPUT_PIN_COUNT]; // Source connection media type
        CImageAllocator* _imageAllocator[INPUT_PIN_COUNT]; // Our DIBSECTION allocator

        // ����������뻺����
        volatile IMediaSample* _readyChannelBuffer[INPUT_PIN_COUNT][CHANNEL_BUFFER_COUNT]; // ÿ��ͨ�������������δ���ϳɵĻ������б�
        // TODO:������CBaseRendere::m_pMediaSample[INPUT_PIN_COUNT]����Ϊ������ֶΡ�

        // ������������������
        // ����������ɺ󣬲����ҳ��֣����ǽ���Ҫ���ֵ��������������IE11��Tab�����׼��߳�
        // ����ADM�����ʵ�������Ⱦ�߳�ͳһ���ȳ���ʱ����λ�ã�ʱ��Ϳռ䣩��
        // 16��ͨ�������߳�+һ�������Ⱦ�̵߳�������������ί�и�һ�����߼��������ں��ʵ�ʱ��չ�ָ��û���
        volatile IMediaSample* _allPresentBuffers[PRESENT_BUFFER_COUNT]; // ��ϳ�������ȫ�������������
        volatile IMediaSample* _idlePresentBuffer[PRESENT_BUFFER_COUNT]; // ��ϳ������Ŀ��еĻ������б�
        volatile IMediaSample* _mixedPresentBuffer[PRESENT_BUFFER_COUNT]; // ��ϳ��������ѺϳɺõĻ������б�
        volatile IMediaSample* _presentingBuffer; // �����������߳����߲���ռ�������ڽ��г��ֵĻ�����ָ�롣

        // ���ֻ�������IE11��Tab�����̸߳�����䣬��ͨ��ICommand�ӿڽ�ָ�����õ���Ⱦ���ġ�
        // ��IE11��⵽��ʾ��ģʽ�仯ʱ��ҲҪ������������DIB������ָ�롣
        // IE11��Tab�����׼��̶߳�ʱ����ICommand�ӿ���Ⱦ��
        // ICommand::Draw�ӿڷ�����ʵ�ֻ�ȡ�����ߣ�һ���ϳɺõ�DIB��������
        // ��������������ڵ�HDC���ض������أ�
        Statistics_t _sts; // ͳ����Ϣ��
    };

} // end namespace VideoRenderer