//
// DemuxerGraph��PusherGraph���ܺ϶�Ϊһ����֮ΪMixedGraph��
// ��ȫ�ɹ����õĵ�·ͼ��filter��������Ծ����Ƿ���Ϊý�����á�
// Graphֻ�����Ե����ε���ÿ��Filter��Run������
//
#pragma once
#include "FixedGraph.h"

class CMixedGraph : public CFixedGraph, public RtspSource::INotify
{
public:
    DECLARE_IUNKNOWN

    // ����ͨ���޹ص�MISC�̵߳�״̬
    enum class ThreadState {
        Idle,
        OpenPending,
        Opened,
        PausePending,
        Paused,
        PlayPending,
        Playing,
        StopPending,
        Stopped,
    };

    // �����Ƚ���Paused������̬�����ٽ���Playing������̬��������OS���̵߳��Ȼ���ԭ��
    // ����״̬�л����̶����첽�ģ�������Ϊ�ǲ���������ɵġ�
    // ʵ�ʲ��Ź��̱���ʹŴ���������ʹ�ù��̶�Ӧ������
    // Paused����Playing�ıر�ǰ��״̬������ͣ������ֻ��Ҫ��ͨ������翪�ؼ��ɡ�
    // Pasued״̬���Ե���Ӧ�ã����絥�ž�̬ͼƬԴ�鿴����ֻ��ҪPaused״̬���ɡ�
    // ֻ���˶�ͼ����Ƶ������ҪPlaying״̬���������ת�������ϻ�ͼƬ����Ӱ��Ƭ����
    // �����һ��ͼƬ�ļ�Դ��ʱ�򣬾�ֻ��Ҫ����Paused״̬���ɣ�����ҪPlaying״̬��
    enum class PlayState {
        Closed, // ��ȡ���Ŵ����ѹز֣���Open
        OpenPending, // ���֣�����Ŵ���...�ز֡�
        Opened, // �ѷ���Ŵ����ѹز֣���ͷ����̧��״̬������ȫ�ڣ���Close|Pause��ͨ���Զ�ת��Paused״̬��

        PausePending, // �л�����ͣ״̬��...����ͷ������̧��״̬����˹���Ҫ����ͷ��ѹ��
        Paused, // ��ͣ���ţ����ͣת����ͷ������ѹ״̬����Play|Stop���������ʾ��ͣʱ�̵ľ�̬֡��

        PlayPending, // �л�����������״̬��...
        Playing, // �����У����ת������ͷ������ѹ״̬�����������˶��仯����Pause|Stop

        StopPending, // �л���ֹͣ״̬��...�ô�ͷ̧��
        Stopped, // ��ֹͣ���ţ���ͷ��̧�𣬻���ȫ�ڣ���Close|Pause��
        ClosePending, // ���֣�ȡ���Ŵ���...�ز֡�
    };

    enum { CHANNEL_COUNT = XSE_MAX_CHANNEL_ID + 1 };
    enum { THREAD_COUNT = CHANNEL_COUNT + 1 };
    enum { MISC_THREAD_INDEX = CHANNEL_COUNT }; // ��ͨ���ض�������������ִ���̵߳��±ꡣ

    typedef HRESULT(__thiscall CMixedGraph::* ApcFunc)(xse_arg_t*);

    // TODO��֧�����������κ����񶼿��Եݹ黮�ֳ�һϵ����һ��֡����ʱ��Ƭ(16ms)�ڿ���ɵ�������
    struct TaskItem {
        ApcFunc pFunc = nullptr;
        xse_arg_t* pArg = nullptr;

        TaskItem(ApcFunc func, xse_arg_t* arg) {
            pFunc = func;
            pArg = arg;
        }

        ~TaskItem() {
            SAFE_DELETE(pArg);
        }
    };
    typedef ConcurrentQueue<CMixedGraph::TaskItem*> TaskItemQueue;

    static CMixedGraph* CreateInstace(HWND hwnd, HRESULT& hr);

    CMixedGraph(HWND hwnd, HRESULT& hr)
        : CFixedGraph(hwnd, hr),
        _refClock(nullptr, &hr)
    {
        InterlockedIncrement(&_instanceCount);
        // ��Ϊ�˱�֤IE11���ᶯ̬ж��quarz.dll����Ĭ�ϵ��ڴ�����������ã��ɿ��ǲ�����quarz.dll?
        hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC,
            (REFIID)IID_IFilterGraph, (void**)&_graphBuilder);

        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            _playState[i] = PlayState::Closed;
            _source[i] = nullptr;
            _videoDecoder[i] = nullptr;
            _audioDecoder[i] = nullptr;
        }
        _videoRendererCmd = nullptr;
        _videoRenderer = nullptr;
        _audioRenderer = nullptr;

        for (int i = 0; i < THREAD_COUNT; ++i) {
            _threadState[i] = ThreadState::Idle;
            _taskExecuteThread[i] = std::move(std::thread(&CMixedGraph::TaskExecuteThread, this, i));
        }
    }

    virtual ~CMixedGraph()
    {
        // �ȴ��첽����ִ���߳��˳�
        {
            for (int i = 0; i < THREAD_COUNT; ++i) {
                _taskExecuteThread[i].join();
            }
        }
        // �����첽����ִ���̵߳�������кʹ����¼�
        {
            for (int i = 0; i < THREAD_COUNT; ++i) {
                TaskItem* ti = nullptr;
                while (!_pendingTaskQueue[i].empty()) {
                    _pendingTaskQueue[i].pop(ti);
                    SAFE_DELETE(ti);
                };
                while (!_doneTaskQueue[i].empty()) {
                    _doneTaskQueue[i].pop(ti);
                    SAFE_DELETE(ti);
                };
            }
        }
        // �Ͽ�����
        {
            for (int i = 0; i < CHANNEL_COUNT; ++i) {
                if (_source[i] == nullptr)
                    continue;
                DisconnectFilter(_source[i]);
                DisconnectFilter(_videoDecoder[i]);
                DisconnectFilter(_audioDecoder[i]);
            }
            DisconnectVideoRenderer();
            DisconnectAudioRenderer();
        }
        // �ͷ��˾�
        {
            for (int i = 0; i < CHANNEL_COUNT; ++i) {
                _source[i] = nullptr;
                _videoDecoder[i] = nullptr;
                _audioDecoder[i] = nullptr;
            }
            _videoRendererCmd = nullptr;
            _videoRenderer = nullptr;
            _audioRenderer = nullptr;
        }
        InterlockedDecrement(&_instanceCount);
    }

    void __stdcall OnFrameIntervalChanged(int channel, DWORD frameInterval)
    {
        if (_videoRendererCmd != nullptr) {
            _videoRendererCmd->SetSourceFrameInterval(channel, frameInterval);
        }
        return;
    }

    HRESULT DisconnectVideoRenderer()
    {
        HRESULT hr = S_OK;

        if (_videoRenderer == nullptr)
            return E_INVALIDARG;

        do {
            IPin* pIn = nullptr;
            hr = FindConnectedPin(_videoRenderer, PINDIR_INPUT, &pIn);
            if (SUCCEEDED(hr)) {
                hr = pIn->Disconnect();
                SAFE_RELEASE(pIn);
            }
        } while (SUCCEEDED(hr));


        return hr;
    }

    HRESULT DisconnectAudioRenderer() 
    {
        return S_OK;
    }

    HRESULT PostAPC(TaskItem* ti)
    {
        int i = XSE_INVALID_CHANNEL_ID;

        if (ti != nullptr && ti->pArg != nullptr
            && ti->pArg->channel > XSE_INVALID_CHANNEL_ID
            && ti->pArg->channel <= XSE_MAX_CHANNEL_ID) {
            i = ti->pArg->channel;
        }

        if (i == XSE_INVALID_CHANNEL_ID) {
            _pendingTaskQueue[MISC_THREAD_INDEX].push(ti);
        }
        else {
            assert(i >= XSE_MIN_CHANNEL_ID);
            assert(i <= XSE_MAX_CHANNEL_ID);
            _pendingTaskQueue[i].push(ti);
        }

        return S_OK;
    }

    HRESULT PostQuitMsg()
    {
        _exitThread = true;
        for (int i = 0; i < THREAD_COUNT; ++i) {
            _pendingTaskQueue[i].push(nullptr);
        }
        return S_OK;
    }

    // ÿ��ͨ��һ���̣߳����к�������ԭ�ͷ�װ��һ��lamba���ߺ��������У�ִ����ֻ��Ҫ�������޲ε�()�������ɡ�
    // ÿ������ĺ��������()����ʵ�ֻ���ȷ�Ľ������������İ󶨣���Ϊʵ�δ��ݸ�Ŀ�꺯����ַ�ġ�
    // ��ǰ��std::bind()���ڣ�ֱ����[this,a,b,c,d][&]{}�������ɸ㶨���������죬�����ı��棨�󶨣�����á�
    void TaskExecuteThread(int i);

    HRESULT DispatchCompletedAPC() 
    {
        for (int i = 0; i < THREAD_COUNT; ++i) {
            if (_source[i] == nullptr)
                continue;

            TaskItem* ti = nullptr;
            while (_doneTaskQueue[i].try_pop(ti)) {
                if (ti->pArg != nullptr && ti->pArg->cb != nullptr)
                    ti->pArg->cb(ti->pArg);
                SAFE_DELETE(ti);
            }
        }
        return S_OK;
    }

    HRESULT Init() {
        HRESULT hr = S_OK;

        ASSERT(_videoRenderer == nullptr);
        GDIRenderer_CreateInstance(_hwndHost, &_videoRenderer);
        hr = _videoRenderer->QueryInterface(IID_IVideoRendererCommand, (void**)&_videoRendererCmd);

        return hr;
    }

    HRESULT BuildRtspLiveSource(int i)
    {
        HRESULT hr = S_OK;

        ASSERT(_source[i] == nullptr);
        ASSERT(_videoDecoder[i] == nullptr);

        // RTSPֱ��Դ
        {
            RtspSource_CreateInstance(&_source[i]);
            CComQIPtr<RtspSource::ICommand, &IID_IRtspSourceCommand> cmd(_source[i]);
            cmd->SetChannelId(i);
            cmd->SetStreamingOverTcp(TRUE);
            cmd->SetTunnelingOverHttpPort(80);
            cmd->SetInitialSeekTime(0);
            cmd->SetLatency(0);
            cmd->SetAutoReconnectionPeriod(5000);
            cmd->SetNotifyReceiver(static_cast<RtspSource::INotify*>(this));
        }

        // ��Ƶ������
        {
            LAVVideo_CreateInstance(&_videoDecoder[i]);
            CComQIPtr<ILAVVideoConfig> cmd(_videoDecoder[i]);
            cmd->SetOutputBufferCount(5);
            VERIFY_HR(ConnectFilters(_source[i], _videoDecoder[i]));
        }

        VERIFY_HR(ConnectToRenderer(_videoDecoder[i], i));

        return hr;
    }


    HRESULT ConnectToRenderer(IBaseFilter* pSrc, int pinIndex)
    {
        HRESULT hr = S_OK;
        IEnumMediaTypes* pEnum = nullptr;
        IPin* pOut = nullptr;
        IPin* pIn = nullptr;
        AM_MEDIA_TYPE* mt = nullptr;

        CHECK_HR(FindUnconnectedPin(pSrc, PINDIR_OUTPUT, &pOut));
        CHECK_HR(pOut->EnumMediaTypes(&pEnum));
        CHECK_HR(pEnum->Next(1, &mt, NULL));
        CHECK_HR(FindPinByIndex(_videoRenderer, PINDIR_INPUT, pinIndex, &pIn));
        CHECK_HR(pOut->Connect(pIn, mt));

    done:
        SAFE_RELEASE(pEnum);
        _DeleteMediaType(mt);
        SAFE_RELEASE(pOut);
        SAFE_RELEASE(pIn);
        return hr;
    }

    // HEIF����һ��HEVC��Ƶ����+һ��AAC��ƵADTS����ͷ����һЩԪ���ݡ�
    // ֧������ͼ��̬I֡�����ǽ���һ��I֡Ԥ������ΪThumbNail��С��HEVC��I֡��
    // HEIF�������ڱ�ʾ����Ƶ�ļ��������������ͳ�ľ�̬JPG�ļ���
    // HEIF��ʽ֧�ֶ�����ͷ�����ͼƬ������֧�ֶ�̬����������ȡ�
    // HEIF������RTSPЭ��ȼ۵�Э�����ơ�
    // RTSP��ý��ͨ��TCP|UDP���䡣HEIF��ʽ���ݿ��Դ�Channel|Storage��ȡ��
    HRESULT BuildHeifSource()
    {
        return E_NOTIMPL;
    }

    HRESULT BuildMp4Source()
    {
        return E_NOTIMPL;
    }

    //--------------------------------------------------------------------------
    // AV play control
    //--------------------------------------------------------------------------
    bool CheckChannel(xse_arg_t* arg)
    {
        int ch = arg->channel;
        if (ch < XSE_MIN_CHANNEL_ID || ch > XSE_MAX_CHANNEL_ID) {
            arg->result = xse_err_invalid_channel;
            return false;
        }
        return true;
    }

    // �����̣߳�MISC_THREAD_INDEX�̡߳�
    // UI�߳����ͨ��һ�������ĵ�����������ӿڵ��첽���á�
    // MISC�̻߳Ὣ���ֱ仯ת����һϵ�е�Tween�����������첽���Ǭ����Ų����Ч��
    // ������ӿڴ���Ļ�·����䣬������ӿڻ������ĺ��������Ļ�·�Rush��ǡ����λ�ã���׼������⣬����ֺ���
    // ���еĳߴ硢λ����صĶ������������[0,1]�������ϵ����OpenGL����D3D����ʱ��դ���׶�ת������Ļ�������ꡣ
    // ���뽫�������ᴩʼ�գ����ܳ��־������ꡣ
    // �ӿ����ڷ�����������Ķ��ͨ��������ڲ��Ų����ṩ��ԭʼ�Ķ�̬�������ݡ�
    // ÿ���ӿھ���չ�����������3Dչ�������ĵ���ǽ�ϻ���HUD��ҽ�ɫ��ͷ���������ͼ����Ⱦϵͳ�ĳ�������߾����ġ�
    HRESULT View(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        xse_arg_view_t* a = (xse_arg_view_t*)arg;

        hr = _videoRendererCmd->SetViewMode(a->mode);

        // �˲�������Ի�����̵߳ģ�������߳�ӵ�����յĺϳ�Ŀ�������ӿڲ�����Ϣ��
        // �����ô��ݸ��������
        // ������ı䵱ǰ����ģʽ��

        return hr;
    }

    // ���÷�ʽ�����ڵ��׼��̣߳�ͬ�����á�
    // �յ��˵��ã��������ͬ�������ڲ��󱸻���������������еľɳߴ�Ļ����������´����µĻ�������
    // ���´�Draw����ʱ�޻��������ã��򲻻�黹UI�߳�HOLDס�ľɱ��档ȷ��UI�߳����ǿ�����Ⱦһ֡���档
    // ����ʱ������ƣ��³ߴ�ĺϳɴ����ᱻ��䣬����UI�߳��п�����������µĻ��档
    // ע��黹�ɻ���ʱ����ϳ�����Ҫ�ж��Ƿ�ߴ�����ظ�ʽһ�£�����ͬ��Ӧ������֮�����´���һ����Ϊ���䡣
    // ��Ϊ�ͻ������󣬴��ڻ����δˢ�µĿհ���������ͬ��������Ƶ����
    // ��һ��WM_PAINT������Repaint��������UI�߳�ִ��ʱ���û��ͻῴ��覴á�
    // �ڿͻ������Ź����У��ܻ�����ô��֡ͼƬ����Ϊû�����¸պ÷������´��ڳߴ�Ĵ���棬
    // ������StretchBlt����˲�����һ��Ч�ʽ��ͣ����Ǻܿ��������ĳߴ���´�����ȫƥ��Ĵ����ͻ����ɡ�
    // ��ʱ��UI�߳��ֿ���ֱ��BitBlt�����ˡ�
    HRESULT SyncResize(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        xse_arg_sync_resize_t* a = (xse_arg_sync_resize_t*)arg;

        _videoRendererCmd->SetObjectRects(&a->pos_rect, &a->clip_rect);

        return hr;
    }

    HRESULT SyncUpdate(xse_arg_t* arg)
    {
        xse_arg_sync_update_t* a = (xse_arg_sync_update_t*)arg;
        VideoRenderer::TimeContext tc;
        tc.base_time = a->base_time;
        tc.last_update_time = a->last_update_time;
        tc.cur_update_time = a->cur_update_time;
        a->result = _videoRendererCmd->Update(&tc) ? xse_err_ok : xse_err_no_more_data;
        return S_OK;
    }

    HRESULT SyncRender(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        xse_arg_sync_render_t* a = (xse_arg_sync_render_t*)arg;

        {
            VideoRenderer::DeviceContext dc = { 0 };
            dc.hdcDraw = a->hdc;
            dc.boundRect = a->bound_rect;
            _videoRendererCmd->Render(&dc);
        }

        return hr;
    }

    HRESULT Open(xse_arg_t* arg);

    // ��RtspSource��live555 scheduler�߳��е��õģ�
    STDMETHODIMP_(void) OnOpenURLCompleted(void* ctx, RtspSource::ErrorCode err)
    {
        // ע�⣺��ʱ����arg����Ҫ������
        // ����Ϊ����ִ���߳��Ѿ�������ֻ�д��̷߳���arg.
        // ����ͬһ������������󣬱���һ���̰߳�ȫ��Ͷ�ݵ���һ���̡߳�
        // �����κ�ʱ�̣�����ֻ��һ���߳��ڶ�д�������ݡ�
        // ����߼����Ǵ��еģ��Բ����ķ������̰߳�ȫ�ġ�
        xse_arg_open_t* a = (xse_arg_open_t*)ctx;
        if (err == RtspSource::Success) {
            a->result = xse_err_ok;
        }
        else {
            a->result = xse_err_fail;
        }
        return;
    }

    HRESULT Play(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        int i = arg->channel;

        if (!CheckChannel(arg))
            return E_INVALIDARG;

        // TODO��ͨ���ο�ʱ�ӣ���ȡ��ǰ�ο�ʱ���������QueryPerformanceCounter)��
        _threadState[i] = ThreadState::PlayPending;
        VERIFY_HR(_videoRendererCmd->Run(i, 0));
        VERIFY_HR(_videoDecoder[i]->Run(i));
        VERIFY_HR(_source[i]->Run(i));
        _threadState[i] = ThreadState::Playing;

        return hr;
    }

    HRESULT Pause(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        int i = arg->channel;

        if (!CheckChannel(arg))
            return E_INVALIDARG;

        // �ͻ��˻���һ����ͣ�㣬������ȡ����ֱ���ͻ���ֱ��֡��������
        // �û����ֱ��ʱ����ĵ�ǰʱ�����Ҳ༴�ɶ��뵽����ֱ��״̬��
        _threadState[i] = ThreadState::PausePending;
        VERIFY_HR(_videoRendererCmd->Pause(i));
        VERIFY_HR(_videoDecoder[i]->Pause());
        VERIFY_HR(_source[i]->Pause());
        _threadState[i] = ThreadState::Paused;

        return hr;
    }

    HRESULT Stop(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        int i = arg->channel;

        if (!CheckChannel(arg)) {
            arg->result = xse_err_invalid_channel;
            return hr;
        }

        // ����ͨ������ֹͣ��ʱ��Ҫֹͣ��Ƶ��Ⱦ����
        if (_source[i] != nullptr) {
            if (_threadState[i] == ThreadState::Idle) {
                arg->result = xse_err_channel_not_started;
            }
            else {
                _threadState[i] = ThreadState::StopPending;
                VERIFY_HR(_videoRendererCmd->Stop(i));
                VERIFY_HR(_videoDecoder[i]->Stop());
                VERIFY_HR(_source[i]->Stop());
                _threadState[i] = ThreadState::Stopped;
                _threadState[i] = ThreadState::Idle;
            }
        }
        else {
            arg->result = xse_err_channel_not_started;
        }

        return hr;
    }

    HRESULT Seek(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        int i = arg->channel;

        if (!CheckChannel(arg))
            return E_INVALIDARG;

        return hr;
    }

    HRESULT Rate(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        int i = arg->channel;

        if (!CheckChannel(arg))
            return E_INVALIDARG;

        return hr;
    }

    HRESULT Zoom(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        int i = arg->channel;

        if (!CheckChannel(arg))
            return E_INVALIDARG;

        return E_NOTIMPL;
    }

    HRESULT Layout(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        int i = arg->channel;
        xse_arg_layout_t* a = (xse_arg_layout_t*)arg;
        VideoRenderer::ViewportDesc_t vd;

        if (!CheckChannel(arg))
            return E_INVALIDARG;

        vd.x = a->x;
        vd.y = a->y;
        vd.w = a->w;
        vd.h = a->h;
        hr = _videoRendererCmd->SetLayout(i, &vd);

        return hr;
    }

private:
    CSyncClock _refClock; // �ο�ʱ��
    CComPtr<IGraphBuilder> _graphBuilder; // hold quarz.dll reference
    PlayState _playState[CHANNEL_COUNT];
    CComPtr<IBaseFilter> _source[CHANNEL_COUNT]; // ��ģʽ��������RTSP IPC��ʵʱԤ������Ҳ�����ǿͻ��˵�¼���ļ�������
    CComPtr<IBaseFilter> _videoDecoder[CHANNEL_COUNT];
    CComPtr<IBaseFilter> _audioDecoder[CHANNEL_COUNT];
    CComPtr<IBaseFilter> _videoRenderer = nullptr; // �ۺ�����Ƶ�����
    CComPtr<VideoRenderer::ICommand> _videoRendererCmd = nullptr;
    CComPtr<IBaseFilter> _audioRenderer = nullptr; // �ۺ�����Ƶ�����

    //
    // ���ڲ����߳��������ޣ����ÿ���ռ���̷߳������ڲ��ò�����ռ��Э�̷�����
    // ��һ�������̷߳�����OS�ں��ܸ���Ч��֪�̵߳ĸ��أ�������Ч��֤���ȵĹ�ƽ�ԡ�
    // ����������˹������������ȣ��������һ�����ʱ�䲻ȷ���ļ����ܼ��͵����񣨱��磺ͼ��ʶ�𣩡�
    //      �������ռʽ���߳�����Э��ʽ��Э�̵ĸ���ԭ��
    // ��������ö������̣߳�����CPU�߼����İ󶨣�������Ե�ԣ����ܱ�֤cache�ľֲ��ԣ�
    //      ��������һ��CPU�����л�ȥ����ͬ�����������񣬷���ϴˢ�Լ������ݺʹ�����ٻ��棬����Ч�ʼ����½���
    // ���ģ����ö������̻߳��ƣ���������ר��ר�ã�һ��һ�������רע������
    //
    volatile bool _exitThread = false;
    int _curChannelCount = 0; // ��ǰ�Ѵ�����ͨ��������
    ThreadState _threadState[THREAD_COUNT];
    TaskItemQueue _pendingTaskQueue[THREAD_COUNT]; // �ȴ�ִ�е�����
    TaskItemQueue _doneTaskQueue[THREAD_COUNT]; // ����ɵ�����
    std::thread _taskExecuteThread[THREAD_COUNT];
    static volatile long _instanceCount; // ��Ծ��ʵ������ͳ��
};

template<typename T>
T* xse_clone_arg(xse_arg_t* arg)
{
    T* out = new T;
    memcpy(out, arg, sizeof(T));
    return out;
}

template<typename T>
CMixedGraph::TaskItem* xse_new_task(CMixedGraph::ApcFunc func, xse_arg_t* arg)
{
    return new CMixedGraph::TaskItem(func, xse_clone_arg<T>(arg));
}

template<typename T>
HRESULT xse_async(CMixedGraph* g, CMixedGraph::ApcFunc func, xse_arg_t* arg)
{
    return g->PostAPC(xse_new_task<T>(func, arg));
}