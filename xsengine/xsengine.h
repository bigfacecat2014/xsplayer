//-------------------------------------------------------------------------------------------------
// Content:
//      xs����API���塣
//
//-------------------------------------------------------------------------------------------------
#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */


//-------------------------------------------------------------------------------------------------
// xs����API����������Ͷ���
//-------------------------------------------------------------------------------------------------

// xs����ʵ�����
typedef void* xse_t;

// xs����Ĳ�����
enum xse_op_t {
    xse_op_idle,            // ����״̬֪ͨ�����ڴ�������Ļص���
    xse_op_open_url,        // ��URL����סURL����ʼȡ����
    xse_op_pause,           // ��ͣ���֣���ʾС�ڵ��ڵ�ǰʱ����Ҿ��������һ֡��֧�ֻ����뿪��
    xse_op_play,            // ��ʼ���֣�����stop�����ռ�ס��URL����ȡ����
    xse_op_stop,            // ֹͣ���֣�ֹͣȡ����seek��0��������գ�
    xse_op_seek,            // ����֡
    xse_op_rate,            // ���ڲ�������
    xse_op_zoom,            // �����ӿڣ�����ѡ�еľ�������
    xse_op_layout,          // �����ӿڲ���
    xse_op_view_mode,       // �л���ͼģʽ
    xse_op_sync_resize,     // �ͻ������α仯֪ͨ
    xse_op_sync_get_time,   // ��ȡ�ο�ʱ��
    xse_op_sync_update,     // ����������Ϣѭ���߳�ͬ�����û�ϳ�������Update
    xse_op_sync_render,     // ����������Ϣѭ���߳�ͬ�����û�ϳ�������Draw����
};

// xs����Ĵ�����
enum xse_err_t {
    xse_err_ok,
    xse_err_invalid_arg,
    xse_err_out_of_memory,
    xse_err_invalid_channel,
    xse_err_channel_not_started,
    xse_err_fail,
    xse_err_no_more_data,
};

// xs����������ִ��״̬
enum xse_state_t {
    xse_state_init,
    xse_state_queued,
    xse_state_canceled, // ������Ҫ���ֳɶ���ɶ�ʱ��Ƭ�������񣬿�ʵ�ֿ���ȡ���͸߲�����
    xse_state_executing,
    xse_state_done,
};

// xs��������ͳ���
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

// xs����API�첽ִ�����֪ͨ�ص�����ԭ��
// result������ʵ��������API�������������أ���ο���������������͵�˵����
typedef void (CALLBACK *xse_callback_t)(xse_arg_t* arg);

// ��������
struct xse_arg_t {
    xse_op_t op;        // ������
    void* ctx;          // �����ﾳ���ɵ��������¼���������ġ�TODO����ΪIUnknown*�������ü�����
    xse_callback_t cb;  // ���֪ͨ�ص�����ָ��
    int channel;        // ֵ��[-1,15]��0��ʾ���ֶβ����ã�û�����塣
    xse_state_t state;  // ִ��״̬���м�״̬����
    xse_err_t result;   // ִ�н��������ֵ����

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
// xse_op_open������������͡�
// �������ͣ�xse_general_result_t
//
struct xse_arg_open_t : xse_arg_t {
    wchar_t url[XSE_MAX_URL_LEN + 1];
    wchar_t user_name[XSE_MAX_USER_NAME_LEN + 1]; // �ɲ���4��GUID
    wchar_t password[XSE_MAX_PASSWORD_LEN + 1]; // �ɲ���MD5ֵhash���룬���߲��ö�̬AccessToken���ơ�
    bool auto_run;

    xse_arg_open_t() {
        op = xse_op_open_url;
        url[0] = 0;
        user_name[0] = 0;
        password[0] = 0;
        auto_run = true;
    }
};

// ���ſ���-��ͣ�����Ĳ���
struct xse_arg_pause_t : xse_arg_t {
    xse_arg_pause_t() { 
        op = xse_op_pause; 
    }
};

// ���ſ���-��ʼ���ţ�������������Ĳ���
struct xse_arg_play_t : xse_arg_t {
    xse_arg_play_t() {
        op = xse_op_play;
    }
};

// ���ſ���-ֹͣ�����Ĳ���
struct xse_arg_stop_t : xse_arg_t {
    xse_arg_stop_t() { 
        op = xse_op_stop; 
    }
};

// ���ſ���-֡��λ������֡�����������Ĳ���
struct xse_arg_seek_t : xse_arg_t {
    int mode; // 0=����ļ���ͷ��1=��ǰλ�ã�2=����ļ�β����
    int frames; // ������ʾ���ˡ���Ҫ��ʱ�䶨λ���ɽ�ʱ�������֡����

    xse_arg_seek_t() {
        op = xse_op_seek;
        mode = 0;
        frames = 0;
    }
};

// ���ſ���-�����������ʲ����Ĳ���
struct xse_arg_rate_t : xse_arg_t {
    float rate; // 1.0��ʾһ�������򲥷š�������ʾ���򲥷š�

    xse_arg_rate_t() {
        op = xse_op_rate; 
        rate = 1.0;
    }
};

// ���ſ���-�������Ų����Ĳ���
struct xse_arg_zoom_t : xse_arg_t {
    float x; // ֵ��[0,1]��ԭʼ�����������ĵ�����x���ꡣ
    float y; // ֵ��[0,1]��ԭʼ�����������ĵ�����y���ꡣ
    float w; // ֵ��[0,16]��������ű�����С��1��Ч��Ϊ��С��
    float h; // ֵ��[0,16]���߶����ű�����С��1��Ч��Ϊ��С��

    xse_arg_zoom_t() {
        op = xse_op_zoom;
        x = 0.5;
        y = 0.5;
        w = 1.0;
        h = 1.0;
    }
};

//
// ���ſ���-���ֵ��������Ĳ���
// ������Բ��ֻ��ơ��ӿڲ㼶��������㣺
//     1.hwnd���ڵĿͻ�������ΪR1)��
//     2.����ͨ���ĸ��ӿڣ���ΪR2)��
//     3.�ض�ͨ�����ӿڣ���ΪR3)��
//
struct xse_arg_layout_t : xse_arg_t {
    float x; // ֵ��[-1,1]���ӿ����Ͻ�x���ꡣ0Ϊ����ˣ�1Ϊ���Ҷˡ�
    float y; // ֵ��[-1,1]���ӿ����Ͻ�y���ꡣ0Ϊ���϶ˣ�1Ϊ���¶ˡ�
    float w; // ֵ��[-1,1]���ӿڿ�ȡ�1Ϊ��Ը��ӿ�100%��ȡ�������ʾ���ҷ�ת��
    float h; // ֵ��[-1,1]���ӿڸ߶ȡ�1Ϊ��Ը��ӿ�100%�߶ȡ�������ʾ���µߵ���

    xse_arg_layout_t() {
        op = xse_op_layout;
        x = 0.0;
        y = 0.0;
        w = 1.0;
        h = 1.0;
    }
};

//
// ���ſ���-��ͼģʽ���ò����Ĳ���
// δִ���κ�view����ʱ������Ĭ��Ϊ���ӿ�ģʽ��
//
struct xse_arg_view_t : xse_arg_t {
    int mode; // ֵ��[0,16]����ֵ��ʾ�ӿ�������0��ʾ�����֣����ں�̨���š�

    xse_arg_view_t() {
        op = xse_op_view_mode;
        channel = XSE_INVALID_CHANNEL_ID; // ͨ���Ų����á�
        mode = 1;
    }
};

//
// ���ſ���-�ؼ��ͻ������α仯֪ͨ
//
struct xse_arg_sync_resize_t : xse_arg_t {
    RECT pos_rect; // �ؼ����������ڵ��߼�����ϵ�е�λ�þ���
    RECT clip_rect; // �������ڵ��߼�����ϵ�м����������

    xse_arg_sync_resize_t() {
        op = xse_op_sync_resize;
        pos_rect = { 0 };
        clip_rect = { 0 };
    }
};

//
// ���ſ���-��ȡ����Ĳο�ʱ��
//
struct xse_arg_sync_get_time_t : xse_arg_t {
    LONGLONG start_time; // ʱ������ʱ��ʱ����㡣
    LONGLONG cur_time; // ˽��ʱ�䣬���ٵ�����ƫ�ù���ʱ��

    xse_arg_sync_get_time_t() {
        op = xse_op_sync_get_time;
        start_time = 0;
        cur_time = 0;
    }
};

//
// ���ſ���-����ʱ����Ϣ��������ͨ���Ĵ����������ĳ���ʱ����
// ����ʱ���������ģ�ͣ�Model & ViewModel������������ѧ�������ʻ�����ɵķ���ģ�ͣ���
// ���أ�����ģ���Ƿ��б仯��
//
struct xse_arg_sync_update_t : xse_arg_t {
    LONGLONG base_time; // ʱ����Ļ��ߣ�����²���ִ��������ο����ߣ���
    LONGLONG last_update_time; // ��һ��ִ������ģ�͸���ʱ�Ŀ�ʼʱ�䡣����״̬��Ҫ���뵽���ʱ�̡�
    LONGLONG cur_update_time; // ���θ��µĿ�ʼʱ�䣬���ڼ���֡�����
    bool is_changed; // ����ֵ������ģ���Ƿ����˱仯��

    xse_arg_sync_update_t() {
        op = xse_op_sync_update;
        base_time = 0;     
        last_update_time = 0;
        cur_update_time = 0;
        is_changed = false;
    }
};

//
// ���ſ���-�ڵ�ǰ�����߳���������������Ⱦ
// ���ӿڲ��ָı�ʱ���ֻ�������Ҫ����һ������ˢ�¡�
// ����ӿڲ��ֲ��䣬��ôÿһ֡�����Խ��оֲ�ˢ�¡�
// ����ȾС�ڵ��ڵ�ǰʱ���Ҿ��뵱ǰʱ���������һ������ms��
// ���϶���ms֮ǰ������������ֹ���������ĳ���ʱ�����籩��
// ��֡��Ϊ�����̶ȱ�֤ʵʱ�ԡ�
// ����ģ�͸��¿�����120fps������ͼ��Ⱦ����ֻ��25fps����ȡ������Ⱦ��Ŀ���豸��Ӳ�����ܡ�
// �����Ӱ�Ͳ�ͬ��ʽ�ĵ��ӻ���������ȫ��ͬ�ķֱ��ʺ�ˢ���ʡ�
// ��ͬ���ӵ���ʾ�����ֻ���Ļ�Ϳ�����60fps/75fps/120fps/160fps�����Ĳ��졣
//
struct xse_arg_sync_render_t : xse_arg_t {
    LONGLONG base_time; // ʱ����Ļ��ߣ�����²���ִ��������ο����ߣ���
    LONGLONG last_render_begin_time; // ��һ����Ⱦ���̵Ŀ�ʼʱ�䡣
    LONGLONG last_render_end_time; // ��һ��ִ��Ⱦ���̵Ľ���ʱ�䣨�ȴ�VBLANK�źŲ��ύBackBuffer��ɺ��ʱ�䣩��
    LONGLONG cur_render_begin_time; // ������Ⱦ�Ŀ�ʼʱ�䣬���ڼ���֡�����
    HDC hdc; // ��Ⱦ��Ŀ���豸������
    RECT bound_rect; // �豸�����ĵĳ���Ŀ����α߽磨�����������꣩��

    xse_arg_sync_render_t() {
        op = xse_op_sync_render;
        hdc = 0;
        bound_rect = { 0 };
    }
};



//-------------------------------------------------------------------------------------------------
// xs�����API��������
//-------------------------------------------------------------------------------------------------

HRESULT WINAPI xse_create(HWND hwnd, xse_t* xse);
HRESULT WINAPI xse_control(xse_t xse, xse_arg_t* arg);
HRESULT WINAPI xse_destroy(xse_t xse);


#ifdef __cplusplus
}
#endif	/* __cplusplus */
