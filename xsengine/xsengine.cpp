#include "stdafx.h"
#include "global.h"

//-------------------------------------------------------------------------------------------------
// xs��������д�ʵ�ֵ�ɧ����
//-------------------------------------------------------------------------------------------------
//// ������ʿ���
//xse_op_select,
//xse_op_set,
//xse_op_get,
//xse_op_call,
//xse_op_new, // when created, should only ref by tracked object list, refcount == 2���û����+��������
//xse_op_delete, // ע����new���ʹ�ã��������õı��ϰ�ߡ�refcount == 1����������
//
//// �ṹ����
//xse_op_add, // after added, refcount == 3, �û����+��������+�������еĸ��ڵ㡣
//xse_op_remove, // after removed, should only ref by tracked object list. refcount == 2
//
//// �洢����
//xse_op_load,
//xse_op_store,
//-------------------------------------------------------------------------------------------------
// ��ȫ�����ж��ָ�ʽ���ö��ŷָ��key-value�ԣ����ж����Ʋ�����Ҫ��BASE64���롣
// ����ַ�������ת����ţ�����Ҫ�ټ���һ����б��'\'��
// ���磺provider=Basic, user_name=admin, password=123456
// ���磺provider=OpenSSL, cert="AAAADDFEFEAEAdDDFFeabeababaAA"
// ���磺provider=SMS, mobile_phone_number="18207156792", verify_code="9527"
// ���磺provider=FaceId, face_id=123456, face_feature="FFBBFEFEAEAdDDFFeeBB"
// ���磺provider=FingerId, finger_feature="aaaaccccdddddabcdef"
// ���磺provider=SmartCard, key="abcdef"
// ��Щ��ȫ������ר�ö��󱣴�ϳɣ����л�,���ܣ����ܣ������л���
// ���л���ʽ����flatbuffer��ʽ��
// C++���Կ��Լ̳�����ṹ�壬���Ӹ����õĹ��췽����
// �ӿ�ģʽ����Ϊ16�󣬾ͱ������16����·����ʹ�û���ʱ�л�Ϊ���ӿ�ģʽ�ˡ�
// ��·����ֻ�����������û�ʵ��ʹ�õ����ֵΪ׼��
//
// 1.��ҪΪ�����ڵ����󣬻�����δʵ�ֵĹ�����������ơ�
// 2.�ô��뱣���㹻�����Ϣ���������Ķ�����Ҫ���Ķ���������Ҫ�����������ȥ�����Ʋ��д�ߵ���ͼ��
//   ����ʹ��Ĭ�ϲ���ֵ��ơ��õ��ô�����������Ĳ���������Ϣ��
//

// �����·ͼ  
// ���й��죬���ģ����ã���������첽������һ�������߳���ִ�У����ܶ�UI�߳���˿��������
// ������һ������hwnd�ڣ��������graph����
// Ҳ�����ڶ������hwnd�ڣ��������graph����
// TODO:��һ���첽����¼��ص��������ڵ������߳��������ڴ����ص���
HRESULT WINAPI xse_create(HWND hwnd, xse_t* xse)
{
    HRESULT hr = S_OK;
    CMixedGraph* g = nullptr;

    if (hwnd == 0)
        return E_INVALIDARG;

    // TODO: SetDllDirectory���ö�̬�������Ŀ¼��
    // AddDllDirectory����DLL����·����
    // SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_USER_DIRS)��Ӱ��������еļ��ع��̣�����
    // ֻ��Ӱ�쵽LoadLibraryEx(x,x,LOAD_LIBRARY_SEARCH_USER_DIRS)���á�
    // Ϊ�˳���ִ��ʱ��ȷ���ԣ����鲻����ϵͳĿ¼��������������ָ���ľ���·����

    // ����ͼ
    g = CMixedGraph::CreateInstace(hwnd, hr);
    hr = g->Init();
    *xse = g;

    // TODO�� ��ҪLoad����ȫ������������������
    // �첽����Load()����ڶ������̶�����HEVC��ˣ�û��Ҫ���κθ��ӵĶ�̬Э�̣�
    // ���ʵ���յ��İ�����HEVC�ͱ���
    // ���Ҫ֧��H.264���ٷ�װһ��RtspH264SourceFilter

    // ����dshow��Ҫ��ע����ȡһЩsetupʱ��pin������Ϣ���������graph���Զ����ӹ��̡�
    // ���Ʋ���Դ��δ���������Ψһ�ķ�ʽ���Լ�ʵ��Graph�Ŀ��Ʋ��֣�
    // ���ҳ�������dshow�ܹ�����������MediaFoundation��״̬���ܹ���
    // ��COMģ�ͣ���ֹʹ��ע�����ز���������ҪRPC������Ҫ����̣�����Ҫλ��͸���Ե���ิ�����ԡ�
    // ���һ�������ϵͳ��Dshow�����MFT�������������ʹ���ˡ�
    // MPC-HC��ʵ�����Լ���GraphBuilder���ѿ���Ȩ�������Լ����
    // ֻ�ñ���ע��õ�COM������Լ�����Ⱦע���������IE�����Ҫ��

    // �Լ�����һ�������̣߳�����CSourceStream���process_input���ٵ���process_output���ԣ�
    // ��ȡ�������������������������ȴ��������룬����ܶ�������������Ͷ�ݸ���һ��filter.

    // �¹����¼�filter֧��ʲô���͵�����ʲô�ģ�ֱ�Ӱѱ��������������������զ����զ����
    // ����HEVC����󣬾���NV12����YUV420P����ô��ֱ�Ӷ����¼����ܷ�����δ�����Ҫ����Э�̡�
    // ��ֻ��Ҫ�ѱ�������ĸ�ʽ��ϢЯ���������������У���ʵ�ĸ����¼����������ɡ�

    // ���е�ʸ��ͼ�갴ť����ģ������ϵ�µ�VB��������һ��PS����Ƭ�Σ������ʵ������ż��ɡ�
    // �����¼��������������ǲ��ԳƵģ���˱��뵥�����ã������ɲ�ͬ�Ĳ����߳��ṩ���룬һ���߳�
    // ������������粻�ܼ�ʱ�����ڲ��ᷢ����������ӿڵ����߻���ֱ�Ӷ������޿��û�������
    // �첽������ȷ���е�ǰ���Ƕ���������ڲ����������Ƚ���������������
    // ���������л�����������Ҫô��ʧ�ϵ�������Ϣ��Ҫô����������������ߣ������DSP�����йء�

    // ��ÿһ��CMediaSample����д�㹻��������Ϣ������ÿһ��Sample�������ǲ�ͬ��ý�����͡�
    // �ӿ�֧������ý�����͡���������ý���޷��л�������Ҫͬʱ����������������һ��������������I֡��ʼ���ɡ�
    // ����ÿһ��vps,sps,pps�������л������±��롣
    // TODO:�ѽ������ͱ�����֧�ֵ�ý������ȫ������ΪNULL,��������ʱ����ʱ���л������ý�����͡�
    // ʹ��MediaSample�е�ý��������Ϣ��������Pin�ϵ�������Ϣ��
    // ����MediaSample������Ϣ����ָ��һ���̶���ý���������á�ȥ�������й�GraphBuild���߼���

    // TODO: ֧�ֹ��ܰ������ƣ��и���ʱ���ֺ�㣬��ʾ��ҵ㿪�鿴����ҪҪ����ֶ����µ����£�����һ��������

    // TODO: ֧��ֱ������һ��NullRenderer��ʵ��H265�����Ĵ��̡�
    // TODO: ֧��ֱ������һ��GDIRenderer��ʵ�ִ�CPU��Ⱦ������ҪGPU��
    // TODO: ֧��ֱ������һ��MP4FileSink��ʵ�ֺϳ�ΪMP4�ļ���
    // TODO��ʵ��һ��Demuxer������EventLoop�̵߳ĸ����������룬�ֱ������64A+64V��ͨ�������������Ƶ������Ƶ����
    // TODO: ÿ����Ļ���ͬʱ��ʾ64·��Ƶ�����ͬʱ��ϲ���64·��Ƶ�����ȫ�����ƿ���ͨ���ر����޸ġ�
    // TODO: ����һ·��������������ʱ��̬������������Դ�����������޷��νӲ�������һ����ƵԴ��IDR֡���������´�����
    //       ����ζ�ţ�����ͬʱ֧��10000�����ߣ�����ͬʱ�����ϵ��ӵ����64���������˿��������Ŷӷ��ԡ�
    //       ���߹۲��߿���ѡ����Щ�ˣ����߷�ҳ�鿴���������ĸ��ӿڣ��Ͳ����Ǹ��ӿڵ�������
    //       ���Ա���Ƶ���飬һ�ߴ����Ϸ�������˶����Թۿ�ս�����̣�ֻ��Ҫ������Ϸģ�鼴�ɡ�
    // TODO: �������Ⱦ���ո��������Ⱦ������Ҽ��պ϶���Σ�ESC��ȡ�����һ����Ⱦ��
    // TODO��֧�ֻҶ���Ⱦģʽ�����ڼ���GDI��ʽ����Ⱦ�ٶȡ���memset(dst, y_plane, width * 4))
    // TODO: ���õ�����HTTP�����������ʵ��80%�����飬ӯ�����ٽ���ͳһ��HTTP/HTTPSЭ��ջ��ʵ��P2SP��ʽ��CDN���ء�
    // TODO: ����YV12��ʽ��Lockable��SwapChain��Ȼ��ֱ�Ӱѽ����YV12ֱ��memcpy��ȥ��
    // TODO������������ʱ�򼴿ɽ�16·��Ƶ+16·��Ƶ�Ự�����ã�����û���κ��������͡�
    //       ����Pin���Ѿ���������߳��ѿ�ʼ�������������޿�ѭ�����ȴ����ݵ��
    //       ��Ҫ��̬����Source��Pin,��ȫ����Ԥ��һ���Դ��������ȴ�����Ԥ�ڵ�HEVC��AAC���ݼ��ɡ�
    //       ȥ��ϵͳ���еĶ�̬Э����Ϊ���Է�Ԥ�ڵ�����say no���ɡ�
    // TODO: ����һ�������ļ�������ͨ��uudi���ò�ͬ���͵Ĵ�ý�����ļ��е�chunk,
    //       ��������HEVC������AAC������Text�ļ���Դ�����ļ��еĸ������͵ķ��ţ��磺�ࡢ�������ȵȡ�
    // TODO: Cut HEVC�����ļ������չؼ�֡�����и�Ϊ����֡�����ķֶΣ�֧�ֶ�������ϲ���

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
