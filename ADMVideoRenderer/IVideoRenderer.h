// VideoRenderer = VideoMixer + VideoPresenter
// 而VideoPresenter功能尚未与VideoRenderer类分开，直接在VideoRenderer类中实现。
// 因为我不需要一个什么都不用干，只负责指挥两个小弟的VideoRenderer类。
// 所以我实现了一个只带一个小弟VideoMixer，同时自己完成VideoPresenter职能的VideoRenderer类。
#pragma once

namespace VideoRenderer {

    // 视频混合呈现器的输入Pin个数
    enum { INPUT_PIN_COUNT = 16 };

    enum FrameFormat_t {
        FF_UNKNOWN,
        FF_SURFACE9_YV12,
        FF_DIB_RGB32,
    };

    // 未压缩的原始视频帧描述
    struct InputFrameDesc_t {
        void* planes[4];
        REFERENCE_TIME pts; // 目标实时检测与跟踪算法最好抢在这个时间点前完成计算，得到一个区域信息，叠加在画面上方，不修改原始YUV像素。
        void* regions; // 由监听者负责填充，视频分析得到的区域对象，由更复杂的结构描述。
    };

    // 把视频混合器看作一台合成物理设备，有16个输入Pin，对应16个通道编号[1,16]。
    // Video Renderer 的OutputPin物理连接的VideoMixer的哪一路Input Pin就硬件上
    // 决定了输入数据所属的通道。
    // 主持人决定了当前场景哪一路是大屏幕，观众无法调整场景布局，大视口采用720P或1080P的自适应动态高清码流。
    // 小视口一律采用360P的子码流。
    struct OutputFrameDesc_t {
        FrameFormat_t fmt;
        void* planes[4];
        REFERENCE_TIME pts;
        void* regions; // 只读
    };

    // 视频呈现器视口描述
    struct ViewportDesc_t {
        float x; // 定义域[-1,1]，视口左上角x坐标
        float y; // 定义域[-1,1]，视口左上角y坐标
        float w; // 定义域[0,1]，视口宽度
        float h; // 定义域[0,1]，视口高度
    };

    // 时间（原子钟）上下文
    struct TimeContext {
        LONGLONG base_time; // 时间奇点
        LONGLONG last_update_time; // 上次更新开始时间
        LONGLONG cur_update_time; // 本次更新开始时间
    };

    // 呈现设备空间上下文
    // 节点在特定的时间更新数据模型（“空”），在特定的空间输出视图（“色”）。
    struct DeviceContext {
        HDC hdcDraw; // 渲染的目标设备上下文。
        RECT boundRect; // 物理设备坐标系：需要渲染的脏矩形范围，负溢和正溢的部分会被DC的剪裁器自动剪裁掉。
        BOOL isZoomed; // 是否进行了缩放（非100%尺寸呈现）。
        SIZE zoomNum;  // X轴缩放比 = ZoomNum.cx / ZoomDen.cx
        SIZE zoomDen;  // Y轴缩放比 = ZoomNum.cy / ZoomDen.cy
        LONGLONG base_time; // 时间奇点。
        LONGLONG last_draw_begin_time; // 用于呈现性能统计目的。
        LONGLONG last_draw_end_time; // 用于呈现性能统计目的。
        LONGLONG cur_draw_begin_time;  // 用于呈现性能统计目的。
    };

    // 视频渲染器渲染事件监听器
    interface INotify : public IUnknown
    {
        // 解码线程触发：刚从视频解码器收到的视频原始帧。此时进行目标检测与追踪。
        STDMETHOD_(void, OnInputPinReceived(int channel, InputFrameDesc_t* desc)) = 0;

        // 混合线程触发：正准备合成一个通道的一帧画面到渲染目标表面。
        STDMETHOD_(void, OnBeforeComposite(int channel, InputFrameDesc_t* desc)) = 0;
        // 混合线程触发：已经StretchRect到通道对应的视口区域。
        STDMETHOD_(void, OnAfterComposite(int channel, InputFrameDesc_t* desc)) = 0;

        // 宿主窗口的呈现线程触发：合成的视频表面即将呈现。
        STDMETHOD_(void, OnBeforePresent(OutputFrameDesc_t* desc)) = 0;
        // 宿主窗口的呈现线程触发：合成的视频表面已经呈现。
        // 注意：
        //     目标检测的结果（区域列表）被当作视频表面上的覆盖物。
        //     合成器只需一个批次渲染即可将所有区域形状覆盖在合成的视频表面上。
        STDMETHOD_(void, OnAfterPresent(OutputFrameDesc_t* desc)) = 0;
    };

    // 配置接口是服务器端配置树在客户端的单向的投影方法，不要试图从它读取任何配置。
    // 所有配置都以服务器下发的配置树为准，若渲染器不接受，则配置方法会返回错误码。
    // 当配置输入参数错误时，渲染器采用的实际配置值未定义，一般采用渲染器相关的内部默认值。
    // 出现配置失败的情况，管理员要根据错误码，分析错误原因，排除故障。
    // 视频渲染器配置接口
    interface ICommand : public IUnknown
    {
        // MixedGraph的渲染管线构建线程专用的设置接口
        STDMETHOD(SetNotifyReceiver)(INotify* receiver) = 0;

        // MixedGraph的杂项命令执行线程专用的设置接口
        STDMETHOD(SetViewMode)(int mode) = 0;
        STDMETHOD(SetLayout)(int channel, const ViewportDesc_t* desc) = 0;
        STDMETHOD(SetSourceFrameInterval)(int channel, DWORD frameInterval) = 0;

        // MixedGraph的通道命令执行线程专用控制接口
        STDMETHOD(Stop)(int channel) = 0;
        STDMETHOD(Pause)(int channel) = 0;
        STDMETHOD(Run)(int channel, REFERENCE_TIME startTime) = 0;

        // IE11的Tab窗口套间线程专用的显示控制接口
        STDMETHOD(SetObjectRects)(LPCRECT posRect, LPCRECT clipRect) = 0;
        STDMETHOD_(BOOL, Update(TimeContext* tc)) = 0;
        // 若连续Draw而不通过Update获取新样本，则会重复渲染被UI线程Hold住的当前样本。
        // 若Update成功拿到新样本，则旧的样本会作为交易条件归还给混合呈现器重新装填。
        STDMETHOD_(void, Render(DeviceContext* dc)) = 0;
    };
} // end namespace VideoRenderer


// {5EFA6C3B-E602-4DB1-9C37-83B2B4991101}
DEFINE_GUID(IID_IVideoRendererCommand,
    0x5efa6c3b, 0xe602, 0x4db1, 0x9c, 0x37, 0x83, 0xb2, 0xb4, 0x99, 0x11, 0x01);

// {C9D001E5-6BD8-4153-A9E5-7109BAAB1101}
DEFINE_GUID(IID_IVideoRendererNotify,
    0xc9d001e5, 0x6bd8, 0x4153, 0xa9, 0xe5, 0x71, 0x9, 0xba, 0xab, 0x11, 0x01);

extern HRESULT WINAPI GDIRenderer_CreateInstance(HWND hwndHost, IBaseFilter** ppObj);
extern HRESULT WINAPI D3D9Renderer_CreateInstance(HWND hwndHost, IBaseFilter** ppObj);

