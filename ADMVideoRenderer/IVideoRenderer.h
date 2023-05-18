// VideoRenderer = VideoMixer + VideoPresenter
// ��VideoPresenter������δ��VideoRenderer��ֿ���ֱ����VideoRenderer����ʵ�֡�
// ��Ϊ�Ҳ���Ҫһ��ʲô�����øɣ�ֻ����ָ������С�ܵ�VideoRenderer�ࡣ
// ������ʵ����һ��ֻ��һ��С��VideoMixer��ͬʱ�Լ����VideoPresenterְ�ܵ�VideoRenderer�ࡣ
#pragma once

namespace VideoRenderer {

    // ��Ƶ��ϳ�����������Pin����
    enum { INPUT_PIN_COUNT = 16 };

    enum FrameFormat_t {
        FF_UNKNOWN,
        FF_SURFACE9_YV12,
        FF_DIB_RGB32,
    };

    // δѹ����ԭʼ��Ƶ֡����
    struct InputFrameDesc_t {
        void* planes[4];
        REFERENCE_TIME pts; // Ŀ��ʵʱ���������㷨����������ʱ���ǰ��ɼ��㣬�õ�һ��������Ϣ�������ڻ����Ϸ������޸�ԭʼYUV���ء�
        void* regions; // �ɼ����߸�����䣬��Ƶ�����õ�����������ɸ����ӵĽṹ������
    };

    // ����Ƶ���������һ̨�ϳ������豸����16������Pin����Ӧ16��ͨ�����[1,16]��
    // Video Renderer ��OutputPin�������ӵ�VideoMixer����һ·Input Pin��Ӳ����
    // ��������������������ͨ����
    // �����˾����˵�ǰ������һ·�Ǵ���Ļ�������޷������������֣����ӿڲ���720P��1080P������Ӧ��̬����������
    // С�ӿ�һ�ɲ���360P����������
    struct OutputFrameDesc_t {
        FrameFormat_t fmt;
        void* planes[4];
        REFERENCE_TIME pts;
        void* regions; // ֻ��
    };

    // ��Ƶ�������ӿ�����
    struct ViewportDesc_t {
        float x; // ������[-1,1]���ӿ����Ͻ�x����
        float y; // ������[-1,1]���ӿ����Ͻ�y����
        float w; // ������[0,1]���ӿڿ��
        float h; // ������[0,1]���ӿڸ߶�
    };

    // ʱ�䣨ԭ���ӣ�������
    struct TimeContext {
        LONGLONG base_time; // ʱ�����
        LONGLONG last_update_time; // �ϴθ��¿�ʼʱ��
        LONGLONG cur_update_time; // ���θ��¿�ʼʱ��
    };

    // �����豸�ռ�������
    // �ڵ����ض���ʱ���������ģ�ͣ����ա��������ض��Ŀռ������ͼ����ɫ������
    struct DeviceContext {
        HDC hdcDraw; // ��Ⱦ��Ŀ���豸�����ġ�
        RECT boundRect; // �����豸����ϵ����Ҫ��Ⱦ������η�Χ�����������Ĳ��ֻᱻDC�ļ������Զ����õ���
        BOOL isZoomed; // �Ƿ���������ţ���100%�ߴ���֣���
        SIZE zoomNum;  // X�����ű� = ZoomNum.cx / ZoomDen.cx
        SIZE zoomDen;  // Y�����ű� = ZoomNum.cy / ZoomDen.cy
        LONGLONG base_time; // ʱ����㡣
        LONGLONG last_draw_begin_time; // ���ڳ�������ͳ��Ŀ�ġ�
        LONGLONG last_draw_end_time; // ���ڳ�������ͳ��Ŀ�ġ�
        LONGLONG cur_draw_begin_time;  // ���ڳ�������ͳ��Ŀ�ġ�
    };

    // ��Ƶ��Ⱦ����Ⱦ�¼�������
    interface INotify : public IUnknown
    {
        // �����̴߳������մ���Ƶ�������յ�����Ƶԭʼ֡����ʱ����Ŀ������׷�١�
        STDMETHOD_(void, OnInputPinReceived(int channel, InputFrameDesc_t* desc)) = 0;

        // ����̴߳�������׼���ϳ�һ��ͨ����һ֡���浽��ȾĿ����档
        STDMETHOD_(void, OnBeforeComposite(int channel, InputFrameDesc_t* desc)) = 0;
        // ����̴߳������Ѿ�StretchRect��ͨ����Ӧ���ӿ�����
        STDMETHOD_(void, OnAfterComposite(int channel, InputFrameDesc_t* desc)) = 0;

        // �������ڵĳ����̴߳������ϳɵ���Ƶ���漴�����֡�
        STDMETHOD_(void, OnBeforePresent(OutputFrameDesc_t* desc)) = 0;
        // �������ڵĳ����̴߳������ϳɵ���Ƶ�����Ѿ����֡�
        // ע�⣺
        //     Ŀ����Ľ���������б���������Ƶ�����ϵĸ����
        //     �ϳ���ֻ��һ��������Ⱦ���ɽ�����������״�����ںϳɵ���Ƶ�����ϡ�
        STDMETHOD_(void, OnAfterPresent(OutputFrameDesc_t* desc)) = 0;
    };

    // ���ýӿ��Ƿ��������������ڿͻ��˵ĵ����ͶӰ��������Ҫ��ͼ������ȡ�κ����á�
    // �������ö��Է������·���������Ϊ׼������Ⱦ�������ܣ������÷����᷵�ش����롣
    // �����������������ʱ����Ⱦ�����õ�ʵ������ֵδ���壬һ�������Ⱦ����ص��ڲ�Ĭ��ֵ��
    // ��������ʧ�ܵ����������ԱҪ���ݴ����룬��������ԭ���ų����ϡ�
    // ��Ƶ��Ⱦ�����ýӿ�
    interface ICommand : public IUnknown
    {
        // MixedGraph����Ⱦ���߹����߳�ר�õ����ýӿ�
        STDMETHOD(SetNotifyReceiver)(INotify* receiver) = 0;

        // MixedGraph����������ִ���߳�ר�õ����ýӿ�
        STDMETHOD(SetViewMode)(int mode) = 0;
        STDMETHOD(SetLayout)(int channel, const ViewportDesc_t* desc) = 0;
        STDMETHOD(SetSourceFrameInterval)(int channel, DWORD frameInterval) = 0;

        // MixedGraph��ͨ������ִ���߳�ר�ÿ��ƽӿ�
        STDMETHOD(Stop)(int channel) = 0;
        STDMETHOD(Pause)(int channel) = 0;
        STDMETHOD(Run)(int channel, REFERENCE_TIME startTime) = 0;

        // IE11��Tab�����׼��߳�ר�õ���ʾ���ƽӿ�
        STDMETHOD(SetObjectRects)(LPCRECT posRect, LPCRECT clipRect) = 0;
        STDMETHOD_(BOOL, Update(TimeContext* tc)) = 0;
        // ������Draw����ͨ��Update��ȡ������������ظ���Ⱦ��UI�߳�Holdס�ĵ�ǰ������
        // ��Update�ɹ��õ�����������ɵ���������Ϊ���������黹����ϳ���������װ�
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

