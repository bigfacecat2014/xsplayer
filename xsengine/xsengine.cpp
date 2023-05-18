#include "stdafx.h"
#include "global.h"

//-------------------------------------------------------------------------------------------------
// xs引擎更多有待实现的骚操作
//-------------------------------------------------------------------------------------------------
//// 对象访问控制
//xse_op_select,
//xse_op_set,
//xse_op_get,
//xse_op_call,
//xse_op_new, // when created, should only ref by tracked object list, refcount == 2，用户句柄+跟踪链表
//xse_op_delete, // 注意与new配对使用，养成良好的编程习惯。refcount == 1，跟踪链表
//
//// 结构控制
//xse_op_add, // after added, refcount == 3, 用户句柄+跟踪链表+对象树中的父节点。
//xse_op_remove, // after removed, should only ref by tracked object list. refcount == 2
//
//// 存储控制
//xse_op_load,
//xse_op_store,
//-------------------------------------------------------------------------------------------------
// 安全令牌有多种格式，用逗号分割的key-value对，所有二进制参数需要用BASE64编码。
// 如果字符串中有转义符号，则需要再加上一个正斜杠'\'。
// 比如：provider=Basic, user_name=admin, password=123456
// 比如：provider=OpenSSL, cert="AAAADDFEFEAEAdDDFFeabeababaAA"
// 比如：provider=SMS, mobile_phone_number="18207156792", verify_code="9527"
// 比如：provider=FaceId, face_id=123456, face_feature="FFBBFEFEAEAdDDFFeeBB"
// 比如：provider=FingerId, finger_feature="aaaaccccdddddabcdef"
// 比如：provider=SmartCard, key="abcdef"
// 这些安全令牌由专用对象保存合成，序列化,加密，解密，反序列化。
// 序列化格式采用flatbuffer格式。
// C++语言可以继承这个结构体，增加更易用的构造方法。
// 视口模式设置为16后，就保留最大16个链路，即使用户暂时切换为单视口模式了。
// 链路数量只增不减，以用户实际使用的最大值为准。
//
// 1.不要为不存在的需求，或者尚未实现的功能做保留设计。
// 2.让代码保留足够多的信息，有利于阅读，不要让阅读者总是需要像编译器那样去智能推测编写者的意图。
//   避免使用默认参数值设计。让调用代码包含完整的参数传递信息。
//

// 构造电路图  
// 所有构造，更改，设置，加载命令都异步化，在一个工作线程中执行，不能对UI线程有丝毫阻塞。
// 可以在一个宿主hwnd内，创建多个graph对象。
// 也可以在多个宿主hwnd内，创建多个graph对象。
// TODO:给一个异步完成事件回调函数，在调用者线程上下文内触发回调。
HRESULT WINAPI xse_create(HWND hwnd, xse_t* xse)
{
    HRESULT hr = S_OK;
    CMixedGraph* g = nullptr;

    if (hwnd == 0)
        return E_INVALIDARG;

    // TODO: SetDllDirectory设置动态库的搜索目录。
    // AddDllDirectory增加DLL搜索路径。
    // SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_USER_DIRS)会影响后续所有的加载过程，否则
    // 只会影响到LoadLibraryEx(x,x,LOAD_LIBRARY_SEARCH_USER_DIRS)调用。
    // 为了程序执行时的确定性，建议不搜索系统目录，仅仅搜索进程指定的绝对路径。

    // 构造图
    g = CMixedGraph::CreateInstace(hwnd, hr);
    hr = g->Init();
    *xse = g;

    // TODO： 不要Load，先全部创建并连接起来。
    // 异步发送Load()命令，第二参数固定就是HEVC因此，没必要做任何复杂的动态协商，
    // 如果实际收到的包不是HEVC就报错。
    // 如果要支持H.264就再封装一个RtspH264SourceFilter

    // 由于dshow需要从注册表读取一些setup时的pin描述信息，才能完成graph的自动连接过程。
    // 控制部分源码未公开，因此唯一的方式是自己实现Graph的控制部分，
    // 并且彻底抛弃dshow架构，采用类似MediaFoundation的状态机架构。
    // 简化COM模型，禁止使用注册表相关操作，不需要RPC，不需要跨进程，不需要位置透明性等诸多复杂特性。
    // 如此一来，诸多系统的Dshow组件和MFT都可以随意组合使用了。
    // MPC-HC就实现了自己的GraphBuilder，把控制权掌握在自己手里。
    // 只用别人注册好的COM组件，自己不污染注册表。除非是IE插件需要。

    // 自己创建一个工作线程，调用CSourceStream类的process_input，再调用process_output尝试，
    // 读取解码结果，如果读不到，则继续等待更多输入，如果能读到，则输出结果投递给下一个filter.

    // 甭关心下级filter支持什么类型的输入什么的，直接把本级的输出丢给它，它爱咋处理咋处理。
    // 比如HEVC解码后，就是NV12或者YUV420P，那么就直接丢给下级，能否处理，如何处理不需要和他协商。
    // 你只需要把本级输出的格式信息携带在样本的属性中，如实的告诉下级解码器即可。

    // 所有的矢量图标按钮都是模型坐标系下的VB常量配上一段PS程序片段，进行适当的缩放即可。
    // 输入事件和输出结果可能是不对称的，因此必须单独调用，或者由不同的并发线程提供输入，一个线程
    // 负责处理输出，如不能及时处理，内部会发生阻塞输入接口调用者或者直接丢弃？无可用缓冲区。
    // 异步处理正确进行的前提是对象必须由内部缓冲区，先将输入锁存起来，
    // 如果输入队列缓冲区满，则要么丢失老的输入信息，要么立即阻塞输入调用者，这个与DSP语用有关。

    // 在每一个CMediaSample中填写足够的描述信息，允许每一个Sample都可以是不同的媒体类型。
    // 接口支持任意媒体类型。任意样本媒体无缝切换。不需要同时下载两个码流，下一个码流立即换成I帧开始即可。
    // 几乎每一个vps,sps,pps都可以切换，重新编码。
    // TODO:把解码器和编码器支持的媒体类型全部设置为NULL,允许运行时任意时刻切换具体的媒体类型。
    // 使用MediaSample中的媒体类型信息，而不是Pin上的类型信息。
    // 所有MediaSample类型信息都是指向一个固定的媒体类型引用。去掉所有有关GraphBuild的逻辑。

    // TODO: 支持功能包红点机制，有更新时出现红点，提示玩家点开查看。需要要玩家手动更新到最新，可以一键升级。

    // TODO: 支持直接连接一个NullRenderer，实现H265裸流的存盘。
    // TODO: 支持直接连接一个GDIRenderer，实现纯CPU渲染，不需要GPU。
    // TODO: 支持直接连接一个MP4FileSink，实现合成为MP4文件。
    // TODO：实现一个Demuxer，接受EventLoop线程的复合数据输入，分别向最大64A+64V个通道输出独立的音频或者视频流。
    // TODO: 每个屏幕最多同时显示64路视频，最多同时混合播放64路音频。这个全局限制可以通过重编译修改。
    // TODO: 任意一路解码器都可以随时动态变更输入的数据源，可以立即无缝衔接播放另外一个视频源的IDR帧，无须重新创建。
    //       这意味着，可以同时支持10000人在线，但是同时连麦，上电视的最多64个。其它人可以请求排队发言。
    //       或者观察者可以选择看哪些人，或者翻页查看。焦点在哪个视口，就播放那个视口的声音。
    //       可以边视频会议，一边打格斗游戏，其他人都可以观看战斗过程，只需要下载游戏模块即可。
    // TODO: 多边形渲染，空格键结束渲染，鼠标右键闭合多边形，ESC键取消最近一段渲染。
    // TODO：支持灰度渲染模式，用于加速GDI方式的渲染速度。（memset(dst, y_plane, width * 4))
    // TODO: 先用第三方HTTP下载组件快速实现80%的体验，盈利后再建立统一的HTTP/HTTPS协议栈，实现P2SP方式的CDN下载。
    // TODO: 创建YV12格式的Lockable的SwapChain，然后直接把解码的YV12直接memcpy进去。
    // TODO：程序启动的时候即可将16路音频+16路视频会话创建好，但是没有任何数据推送。
    //       所有Pin均已经激活（工作线程已开始），并进入无限空循环，等待数据到达。
    //       不要动态创建Source的Pin,完全可以预先一次性创建，并等待处理预期的HEVC和AAC数据即可。
    //       去掉系统所有的动态协商行为，对非预期的输入say no即可。
    // TODO: 定义一种容器文件，仅仅通过uudi引用不同类型的纯媒体流文件中的chunk,
    //       比如引用HEVC裸流，AAC裸流，Text文件，源代码文件中的各种类型的符号（如：类、函数）等等。
    // TODO: Cut HEVC裸流文件，按照关键帧对齐切割为任意帧数量的分段，支持多段裸流合并。

    return hr;
}

HRESULT WINAPI xse_control(xse_t xse, xse_arg_t* arg)
{
    CMixedGraph* g = static_cast<CMixedGraph*>(xse);
    if (g == nullptr)
        return E_INVALIDARG;

    if (arg == nullptr)
        return g->PostQuitMsg();

    switch (arg->op) {
    case xse_op_idle: return g->DispatchCompletedAPC();
    case xse_op_open_url: return xse_async<xse_arg_open_t>(g, &CMixedGraph::Open, arg);
    case xse_op_pause: return xse_async<xse_arg_t>(g, &CMixedGraph::Pause, arg);
    case xse_op_play: return xse_async<xse_arg_t>(g, &CMixedGraph::Play, arg);
    case xse_op_stop: return xse_async<xse_arg_t>(g, &CMixedGraph::Stop, arg);
    case xse_op_seek: return xse_async<xse_arg_seek_t>(g, &CMixedGraph::Seek, arg);
    case xse_op_rate: return xse_async<xse_arg_rate_t>(g, &CMixedGraph::Rate, arg);
    case xse_op_zoom: return xse_async<xse_arg_zoom_t>(g, &CMixedGraph::Zoom, arg);
    case xse_op_layout: return xse_async<xse_arg_layout_t>(g, &CMixedGraph::Layout, arg);
    case xse_op_view_mode: return xse_async<xse_arg_view_t>(g, &CMixedGraph::View, arg);
    case xse_op_sync_resize: return g->SyncResize(arg);
    case xse_op_sync_update: return g->SyncUpdate(arg);
    case xse_op_sync_render: return g->SyncRender(arg);
    default: return g->PostQuitMsg();
    }
} // end xse_control


HRESULT WINAPI xse_destroy(xse_t xse)
{
    CMixedGraph* g = (CMixedGraph*)xse;
    SAFE_RELEASE(g);
    return S_OK;
}
