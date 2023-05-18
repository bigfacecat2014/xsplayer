#pragma once

namespace VideoRenderer {

    class CGDIVideoInputPin;
    class CGDIVideoRenderer;

    //---------------------------------------------------------------------------------------------
    // GDI视频混合渲染器的输入PIN定义
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

        // 枚举必用方法
        HRESULT GetMediaType(int iPosition, __inout CMediaType* pMediaType);
        HRESULT CheckMediaType(const CMediaType* pmt);

        STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt)
        {
            return CRendererInputPin::ReceiveConnection(pConnector, pmt);
        }
    };


    //---------------------------------------------------------------------------------------------
    // GDI视频混合渲染器定义
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

        CImageDisplay _display; // 显示器的抽象代理，封装显示像素格式的细节。
        CDrawImage* _drawImage[INPUT_PIN_COUNT]; // DIB渲染工具

        // 输入PIN描述
        CGDIVideoInputPin* _inputPin[INPUT_PIN_COUNT]; // IPin based interfaces
        CMediaType _mtIn[INPUT_PIN_COUNT]; // Source connection media type
        CImageAllocator* _imageAllocator[INPUT_PIN_COUNT]; // Our DIBSECTION allocator

        // 混合器的输入缓冲区
        volatile IMediaSample* _readyChannelBuffer[INPUT_PIN_COUNT][CHANNEL_BUFFER_COUNT]; // 每个通道的已输入的尚未被合成的缓冲区列表。
        // TODO:将基类CBaseRendere::m_pMediaSample[INPUT_PIN_COUNT]改造为下面的字段。

        // 混合器的输出缓冲区。
        // 混合器混合完成后，不自我呈现，而是将需要呈现的输出缓冲区交给IE11的Tab窗口套间线程
        // 或者ADM混合现实引擎的渲染线程统一调度呈现时机和位置（时间和空间）。
        // 16个通道解码线程+一个混合渲染线程的最终输出结果会委托给一个更高级的宿主在合适的时空展现给用户。
        volatile IMediaSample* _allPresentBuffers[PRESENT_BUFFER_COUNT]; // 混合呈现器的全部输出缓冲区。
        volatile IMediaSample* _idlePresentBuffer[PRESENT_BUFFER_COUNT]; // 混合呈现器的空闲的缓冲区列表。
        volatile IMediaSample* _mixedPresentBuffer[PRESENT_BUFFER_COUNT]; // 混合呈现器的已合成好的缓冲区列表。
        volatile IMediaSample* _presentingBuffer; // 被宿主窗口线程拿走并独占，且正在进行呈现的缓冲区指针。

        // 呈现缓冲区由IE11的Tab窗口线程负责分配，并通过ICommand接口将指针设置到渲染器的。
        // 当IE11检测到显示器模式变化时，也要负责重新设置DIB缓冲区指针。
        // IE11的Tab窗口套间线程定时调用ICommand接口渲染。
        // ICommand::Draw接口方法的实现获取（带走）一个合成好的DIB缓冲区？
        // 如何贴到宿主窗口的HDC的特定区域呢？
        Statistics_t _sts; // 统计信息。
    };

} // end namespace VideoRenderer