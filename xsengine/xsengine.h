//-------------------------------------------------------------------------------------------------
// Content:
//      xs引擎API定义。
//
//-------------------------------------------------------------------------------------------------
#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */


//-------------------------------------------------------------------------------------------------
// xs引擎API相关数据类型定义
//-------------------------------------------------------------------------------------------------

// xs引擎实例句柄
typedef void* xse_t;

// xs引擎的操作码
enum xse_op_t {
    xse_op_idle,            // 空闲状态通知（用于触发引擎的回调）
    xse_op_open_url,        // 打开URL（记住URL并开始取流）
    xse_op_pause,           // 暂停呈现（显示小于等于当前时间戳且距离最近的一帧，支持画面秒开）
    xse_op_play,            // 开始呈现（若已stop，则按照记住的URL重新取流）
    xse_op_stop,            // 停止呈现（停止取流，seek到0，画面清空）
    xse_op_seek,            // 搜索帧
    xse_op_rate,            // 调节播放速率
    xse_op_zoom,            // 缩放视口（缩放选中的矩形区域）
    xse_op_layout,          // 调整视口布局
    xse_op_view_mode,       // 切换视图模式
    xse_op_sync_resize,     // 客户区矩形变化通知
    xse_op_sync_get_time,   // 获取参考时间
    xse_op_sync_update,     // 宿主窗口消息循环线程同步调用混合呈现器的Update
    xse_op_sync_render,     // 宿主窗口消息循环线程同步调用混合呈现器的Draw方法
};

// xs引擎的错误码
enum xse_err_t {
    xse_err_ok,
    xse_err_invalid_arg,
    xse_err_out_of_memory,
    xse_err_invalid_channel,
    xse_err_channel_not_started,
    xse_err_fail,
    xse_err_no_more_data,
};

// xs引擎的任务的执行状态
enum xse_state_t {
    xse_state_init,
    xse_state_queued,
    xse_state_canceled, // 大任务要划分成多个可短时间片的子任务，可实现快速取消和高并发。
    xse_state_executing,
    xse_state_done,
};

// xs引擎的整型常量
enum xse_const_int_t {
    XSE_MAX_URL_LEN = 256,
    XSE_MAX_USER_NAME_LEN = 128,
    XSE_MAX_PASSWORD_LEN = 128,
    XSE_INVALID_CHANNEL_ID = -1,
    XSE_MIN_CHANNEL_ID = 0,
    XSE_MAX_CHANNEL_ID = 15,
    XSE_MAX_CHANNEL_COUNT = XSE_MAX_CHANNEL_ID + 1,
    XSE_MIN_VIEW_MODE_ID = 0,
    XSE_MAX_VIEW_MODE_ID = 3,
};

struct xse_arg_t;

// xs引擎API异步执行完成通知回调函数原型
// result参数的实际类型与API输入参数类型相关，请参考具体输入参数类型的说明。
typedef void (CALLBACK *xse_callback_t)(xse_arg_t* arg);

// 参数基类
struct xse_arg_t {
    xse_op_t op;        // 操作码
    void* ctx;          // 调用语境，由调用则负责记录调用上下文。TODO：改为IUnknown*，可引用计数。
    xse_callback_t cb;  // 完成通知回调函数指针
    int channel;        // 值域[-1,15]，0表示此字段不可用，没有意义。
    xse_state_t state;  // 执行状态（中间状态）。
    xse_err_t result;   // 执行结果（返回值）。

    xse_arg_t() {
        op = xse_op_idle;
        ctx = nullptr;
        cb = nullptr;
        channel = XSE_MIN_CHANNEL_ID;
        state = xse_state_init;
        result = xse_err_ok;
    }
};

//
// xse_op_open的输入参数类型。
// 返回类型：xse_general_result_t
//
struct xse_arg_open_t : xse_arg_t {
    wchar_t url[XSE_MAX_URL_LEN + 1];
    wchar_t user_name[XSE_MAX_USER_NAME_LEN + 1]; // 可采用4个GUID
    wchar_t password[XSE_MAX_PASSWORD_LEN + 1]; // 可采用MD5值hash密码，或者采用动态AccessToken机制。
    bool auto_run;

    xse_arg_open_t() {
        op = xse_op_open_url;
        url[0] = 0;
        user_name[0] = 0;
        password[0] = 0;
        auto_run = true;
    }
};

// 播放控制-暂停操作的参数
struct xse_arg_pause_t : xse_arg_t {
    xse_arg_pause_t() { 
        op = xse_op_pause; 
    }
};

// 播放控制-开始播放（或继续）操作的参数
struct xse_arg_play_t : xse_arg_t {
    xse_arg_play_t() {
        op = xse_op_play;
    }
};

// 播放控制-停止操作的参数
struct xse_arg_stop_t : xse_arg_t {
    xse_arg_stop_t() { 
        op = xse_op_stop; 
    }
};

// 播放控制-帧定位（包含帧步进）操作的参数
struct xse_arg_seek_t : xse_arg_t {
    int mode; // 0=相对文件开头，1=当前位置，2=相对文件尾部。
    int frames; // 负数表示后退。若要按时间定位，可将时长折算成帧数。

    xse_arg_seek_t() {
        op = xse_op_seek;
        mode = 0;
        frames = 0;
    }
};

// 播放控制-调整播放速率操作的参数
struct xse_arg_rate_t : xse_arg_t {
    float rate; // 1.0表示一倍速正向播放。负数表示反向播放。

    xse_arg_rate_t() {
        op = xse_op_rate; 
        rate = 1.0;
    }
};

// 播放控制-画面缩放操作的参数
struct xse_arg_zoom_t : xse_arg_t {
    float x; // 值域[0,1]，原始画面缩放中心点的相对x坐标。
    float y; // 值域[0,1]，原始画面缩放中心点的相对y坐标。
    float w; // 值域[0,16]，宽度缩放比例，小于1的效果为缩小。
    float h; // 值域[0,16]，高度缩放比例，小于1的效果为缩小。

    xse_arg_zoom_t() {
        op = xse_op_zoom;
        x = 0.5;
        y = 0.5;
        w = 1.0;
        h = 1.0;
    }
};

//
// 播放控制-布局调整操作的参数
// 采用相对布局机制。视口层级定义分三层：
//     1.hwnd窗口的客户区（记为R1)。
//     2.所有通道的父视口（记为R2)。
//     3.特定通道的视口（记为R3)。
//
struct xse_arg_layout_t : xse_arg_t {
    float x; // 值域[-1,1]，视口左上角x坐标。0为最左端，1为最右端。
    float y; // 值域[-1,1]，视口左上角y坐标。0为最上端，1为最下端。
    float w; // 值域[-1,1]，视口宽度。1为相对父视口100%宽度。负数表示左右反转。
    float h; // 值域[-1,1]，视口高度。1为相对父视口100%高度。负数表示上下颠倒。

    xse_arg_layout_t() {
        op = xse_op_layout;
        x = 0.0;
        y = 0.0;
        w = 1.0;
        h = 1.0;
    }
};

//
// 播放控制-视图模式设置操作的参数
// 未执行任何view操作时，引擎默认为单视口模式。
//
struct xse_arg_view_t : xse_arg_t {
    int mode; // 值域[0,16]，数值表示视口数量。0表示不呈现，仅在后台播放。

    xse_arg_view_t() {
        op = xse_op_view_mode;
        channel = XSE_INVALID_CHANNEL_ID; // 通道号不可用。
        mode = 1;
    }
};

//
// 播放控制-控件客户区矩形变化通知
//
struct xse_arg_sync_resize_t : xse_arg_t {
    RECT pos_rect; // 控件在宿主窗口的逻辑坐标系中的位置矩形
    RECT clip_rect; // 宿主窗口的逻辑坐标系中剪裁区域矩形

    xse_arg_sync_resize_t() {
        op = xse_op_sync_resize;
        pos_rect = { 0 };
        clip_rect = { 0 };
    }
};

//
// 播放控制-获取引擎的参考时间
//
struct xse_arg_sync_get_time_t : xse_arg_t {
    LONGLONG start_time; // 时钟启动时的时间奇点。
    LONGLONG cur_time; // 私有时间，倍速调整和偏置过的时间

    xse_arg_sync_get_time_t() {
        op = xse_op_sync_get_time;
        start_time = 0;
        cur_time = 0;
    }
};

//
// 播放控制-根据时间信息安排所有通道的待呈现样本的呈现时机，
// 根据时间更新数据模型（Model & ViewModel）（类似物理学描述物质基本组成的分子模型）。
// 返回：数据模型是否有变化。
//
struct xse_arg_sync_update_t : xse_arg_t {
    LONGLONG base_time; // 时间轴的基线（多故事并发执行序的起点参考竖线）。
    LONGLONG last_update_time; // 上一次执行数据模型更新时的开始时间。所有状态都要对齐到这个时刻。
    LONGLONG cur_update_time; // 本次更新的开始时间，用于计算帧间隔。
    bool is_changed; // 返回值：数据模型是否发生了变化。

    xse_arg_sync_update_t() {
        op = xse_op_sync_update;
        base_time = 0;     
        last_update_time = 0;
        cur_update_time = 0;
        is_changed = false;
    }
};

//
// 播放控制-在当前调用线程上下文下立即渲染
// 当视口布局改变时呈现缓冲区需要进行一次完整刷新。
// 如果视口布局不变，那么每一帧都可以进行局部刷新。
// 仅渲染小于等于当前时间且距离当前时间最近的那一个样本ms。
// 果断丢弃ms之前所有样本，防止出现连续的呈现时间晚点风暴。
// 丢帧是为了最大程度保证实时性。
// 数据模型更新可以是120fps，而视图渲染可能只有25fps，这取决于渲染的目标设备的硬件性能。
// 比如电影和不同制式的电视机就有着完全不同的分辨率和刷新率。
// 不同牌子的显示器和手机屏幕就可能有60fps/75fps/120fps/160fps等诸多的差异。
//
struct xse_arg_sync_render_t : xse_arg_t {
    LONGLONG base_time; // 时间轴的基线（多故事并发执行序的起点参考竖线）。
    LONGLONG last_render_begin_time; // 上一次渲染过程的开始时间。
    LONGLONG last_render_end_time; // 上一次执渲染过程的结束时间（等待VBLANK信号并提交BackBuffer完成后的时间）。
    LONGLONG cur_render_begin_time; // 本次渲染的开始时间，用于计算帧间隔。
    HDC hdc; // 渲染的目标设备上下文
    RECT bound_rect; // 设备上下文的呈现目标矩形边界（物理像素坐标）。

    xse_arg_sync_render_t() {
        op = xse_op_sync_render;
        hdc = 0;
        bound_rect = { 0 };
    }
};



//-------------------------------------------------------------------------------------------------
// xs引擎的API函数声明
//-------------------------------------------------------------------------------------------------

HRESULT WINAPI xse_create(HWND hwnd, xse_t* xse);
HRESULT WINAPI xse_control(xse_t xse, xse_arg_t* arg);
HRESULT WINAPI xse_destroy(xse_t xse);


#ifdef __cplusplus
}
#endif	/* __cplusplus */
