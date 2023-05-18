//
// DemuxerGraph与PusherGraph功能合二为一，称之为MixedGraph。
// 完全由构建好的电路图中filter自身的特性决定是否作为媒体流泵。
// Graph只会无脑的依次调用每个Filter的Run方法。
//
#pragma once
#include "FixedGraph.h"

class CMixedGraph : public CFixedGraph, public RtspSource::INotify
{
public:
    DECLARE_IUNKNOWN

    // 包含通道无关的MISC线程的状态
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

    // 必须先进入Paused（就绪态），再进入Playing（运行态），这是OS的线程调度基本原则。
    // 所有状态切换过程都是异步的，都被认为是不能立即完成的。
    // 实际播放过程必须和磁带随身听的使用过程对应起来。
    // Paused属于Playing的必备前置状态。从暂停到播放只需要接通电机供电开关即可。
    // Pasued状态可以单独应用，比如单张静态图片源查看，就只需要Paused状态即可。
    // 只有运动图像（视频）才需要Playing状态，驱动电机转动，不断换图片（电影胶片）。
    // 当你打开一个图片文件源的时候，就只需要进入Paused状态即可，不需要Playing状态。
    enum class PlayState {
        Closed, // 已取出磁带，已关仓，可Open
        OpenPending, // 开仓，放入磁带中...关仓。
        Opened, // 已放入磁带，已关仓，磁头处于抬起状态，画面全黑，可Close|Pause，通常自动转入Paused状态。

        PausePending, // 切换至暂停状态中...（磁头若处于抬起状态，则此过程要将磁头下压）
        Paused, // 暂停播放，电机停转，磁头保持下压状态，可Play|Stop，画面仅显示暂停时刻的静态帧。

        PlayPending, // 切换至连续播放状态中...
        Playing, // 播放中，电机转动，磁头保持下压状态，画面正常运动变化，可Pause|Stop

        StopPending, // 切换至停止状态中...让磁头抬起。
        Stopped, // 已停止播放，磁头已抬起，画面全黑，可Close|Pause。
        ClosePending, // 开仓，取出磁带中...关仓。
    };

    enum { CHANNEL_COUNT = XSE_MAX_CHANNEL_ID + 1 };
    enum { THREAD_COUNT = CHANNEL_COUNT + 1 };
    enum { MISC_THREAD_INDEX = CHANNEL_COUNT }; // 非通道特定命令（杂项命令）的执行线程的下标。

    typedef HRESULT(__thiscall CMixedGraph::* ApcFunc)(xse_arg_t*);

    // TODO：支持任务树，任何任务都可以递归划分成一系列在一个帧调度时间片(16ms)内可完成的子任务。
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
        // 仅为了保证IE11不会动态卸载quarz.dll导致默认的内存分配器不可用，可考虑不依赖quarz.dll?
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
        // 等待异步任务执行线程退出
        {
            for (int i = 0; i < THREAD_COUNT; ++i) {
                _taskExecuteThread[i].join();
            }
        }
        // 清理异步任务执行线程的任务队列和传信事件
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
        // 断开连接
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
        // 释放滤镜
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

    // 每个通道一个线程？所有函数调用原型封装在一个lamba或者函数对象中，执行者只需要调用其无参的()方法即可。
    // 每个具体的函数对象的()方法实现会正确的将参数（上下文绑定）作为实参传递给目标函数地址的。
    // 以前用std::bind()现在，直接用[this,a,b,c,d][&]{}就能轻松搞定函数对象构造，上下文保存（绑定）与调用。
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

        // RTSP直播源
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

        // 视频解码器
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

    // HEIF就是一段HEVC视频裸流+一段AAC音频ADTS流，头部了一些元数据。
    // 支持缩略图静态I帧，就是将第一个I帧预先缩放为ThumbNail大小的HEVC的I帧。
    // HEIF可以用于标示短视频文件。它用于替代传统的静态JPG文件。
    // HEIF格式支持多摄像头拍摄的图片，并且支持动态调整背景深度。
    // HEIF就是与RTSP协议等价的协议名称。
    // RTSP流媒体通过TCP|UDP传输。HEIF格式数据可以从Channel|Storage读取。
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

    // 调用线程：MISC_THREAD_INDEX线程。
    // UI线程最好通过一批连续的调用完成所有视口的异步设置。
    // MISC线程会将布局变化转换成一系列的Tween动画动作，异步完成乾坤大挪移特效。
    // 多余的视口从屏幕下方掉落，多出的视口会从最近的横坐标的屏幕下方Rush到恰当的位置，精准倒车入库，不差分毫。
    // 所有的尺寸、位置相关的动画都必须采用[0,1]相对坐标系，靠OpenGL或者D3D运行时光栅化阶段转换成屏幕像素坐标。
    // 必须将相对坐标贯穿始终，不能出现绝对坐标。
    // 视口用于分配大纹理表面的多个通道画面的内部排布，提供最原始的动态纹理数据。
    // 每个视口具体展现在哪里，比如3D展厅场景的电视墙上或者HUD玩家角色的头像框上是由图形渲染系统的场景设计者决定的。
    HRESULT View(xse_arg_t* arg)
    {
        HRESULT hr = S_OK;
        xse_arg_view_t* a = (xse_arg_view_t*)arg;

        hr = _videoRendererCmd->SetViewMode(a->mode);

        // 此操作是针对混合器线程的，混合器线程拥有最终的合成目标表面和视口布局信息。
        // 将调用传递给混合器。
        // 混合器改变当前呈现模式。

        return hr;
    }

    // 调用方式：窗口的套间线程，同步调用。
    // 收到此调用，引擎必须同步调整内部后备缓冲区，清理掉所有的旧尺寸的缓冲区，重新创建新的缓冲区。
    // 若下次Draw到来时无缓冲区可用，则不会归还UI线程HOLD住的旧表面。确保UI线程总是可以渲染一帧画面。
    // 随着时间的推移，新尺寸的合成大表面会被填充，进而UI线程有可能立即获得新的画面。
    // 注意归还旧画面时，混合呈现器要判断是否尺寸和像素格式一致，若不同则，应当丢弃之，重新创建一个作为补充。
    // 因为客户区变大后，窗口会出现未刷新的空白区域，若不同步调整视频区域，
    // 下一个WM_PAINT发生后，Repaint动作会在UI线程执行时，用户就会看到瑕疵。
    // 在客户区缩放过程中，总会有那么几帧图片会因为没有最新刚好符合最新窗口尺寸的大表面，
    // 而发生StretchBlt，这瞬间会有一个效率降低，但是很快新生产的尺寸和新窗口完全匹配的大表面就会生成。
    // 到时候，UI线程又可以直接BitBlt加速了。
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

    // 在RtspSource的live555 scheduler线程中调用的，
    STDMETHODIMP_(void) OnOpenURLCompleted(void* ctx, RtspSource::ErrorCode err)
    {
        // 注意：此时访问arg不需要加锁。
        // 是因为任务执行线程已经阻塞，只有此线程访问arg.
        // 对于同一个任务参数对象，被从一个线程安全地投递到另一个线程。
        // 但是任何时刻，总是只有一个线程在读写它的数据。
        // 因此逻辑上是串行的，对参数的访问是线程安全的。
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

        // TODO：通过参考时钟，获取当前参考时间戳（基于QueryPerformanceCounter)。
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

        // 客户端会标记一个暂停点，并继续取流，直到客户端直播帧缓存满。
        // 用户点击直播时间轴的当前时间点的右侧即可对齐到最新直播状态。
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

        // 所有通道都都停止的时候，要停止视频渲染器。
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
    CSyncClock _refClock; // 参考时钟
    CComPtr<IGraphBuilder> _graphBuilder; // hold quarz.dll reference
    PlayState _playState[CHANNEL_COUNT];
    CComPtr<IBaseFilter> _source[CHANNEL_COUNT]; // 推模式，可能是RTSP IPC的实时预览流，也可能是客户端的录像文件推流。
    CComPtr<IBaseFilter> _videoDecoder[CHANNEL_COUNT];
    CComPtr<IBaseFilter> _audioDecoder[CHANNEL_COUNT];
    CComPtr<IBaseFilter> _videoRenderer = nullptr; // 聚合了视频混合器
    CComPtr<VideoRenderer::ICommand> _videoRendererCmd = nullptr;
    CComPtr<IBaseFilter> _audioRenderer = nullptr; // 聚合了音频混合器

    //
    // 由于并发线程数量有限，采用可抢占的线程方案优于采用不可抢占的协程方案。
    // 其一，采用线程方案，OS内核能更有效感知线程的负载，并能有效保证调度的公平性。
    // 其二，不用人工控制任务粒度，可以设计一个完成时间不确定的计算密集型的任务（比如：图像识别）。
    //      这就是抢占式的线程优于协作式的协程的根本原因。
    // 其二，采用独立的线程，若与CPU逻辑核心绑定（设置亲缘性）还能保证cache的局部性，
    //      不至于让一个CPU反复切换去处理不同工作集的任务，反复洗刷自己的数据和代码高速缓存，导致效率急剧下降。
    // 其四，采用独立多线程机制，可以做到专核专用，一核一类任务的专注能力。
    //
    volatile bool _exitThread = false;
    int _curChannelCount = 0; // 当前已创建的通道数量。
    ThreadState _threadState[THREAD_COUNT];
    TaskItemQueue _pendingTaskQueue[THREAD_COUNT]; // 等待执行的任务
    TaskItemQueue _doneTaskQueue[THREAD_COUNT]; // 已完成的任务
    std::thread _taskExecuteThread[THREAD_COUNT];
    static volatile long _instanceCount; // 活跃的实例数量统计
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