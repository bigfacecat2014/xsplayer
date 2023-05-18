#include "stdafx.h"
#include "global.h"

//
// �ӿ����Ӧ�þ�����¶����Ա���޴���������������������
// ϵͳ���ݴ������ָ��������Ȼ�������źŵĸ��������������Լ�����Ԫ����ʧЧ��
// ������Ҫȥ���̳���������޴��������̡�
// �߼�˼ά�����ܵĳ���Ա������д���߼����ܵĴ��롣
// ��ȷ������� ���� ����������ơ�
//
volatile long CMixedGraph::_instanceCount = 0;

CMixedGraph* CMixedGraph::CreateInstace(HWND hwnd, HRESULT& hr)
{
    CMixedGraph* o = new CMixedGraph(hwnd, hr);
    o->AddRef();
    return o;
}

void CMixedGraph::TaskExecuteThread(int i)
{
    HRESULT hr = S_OK;
    TaskItem* ti = nullptr;
    bool blocked = false;

    while (!_exitThread) {
        ti = nullptr;
        _pendingTaskQueue[i].pop(ti); // ���ܻᱻ���������
        if (ti == nullptr || ti->pFunc == nullptr)
            break;
        hr = (this->*ti->pFunc)(ti->pArg);
        _doneTaskQueue[i].push(ti);
    }
}

HRESULT CMixedGraph::Open(xse_arg_t* arg)
{
    HRESULT hr = S_OK;
    xse_arg_open_t* a = (xse_arg_open_t*)arg;
    int i = a->channel;

    if (!CheckChannel(arg)) {
        a->result = xse_err_invalid_channel;
        return hr;
    }

    if (_threadState[i] != ThreadState::Idle) {
        a->result = xse_err_fail;
        return hr;
    }

    if (_source[i] == nullptr) {
        VERIFY_HR(BuildRtspLiveSource(i));
    }

    // TODO:���Դ��RtspClient���󣬹���һ������ͨѶselect�̣߳�
    // ��ηַ��߳������ж���Ļص����У��������CDispatcher���ࣿ
    // ���о߱����ܺʹ����첽�߳���Ϣ�Ķ�Ҫ��������������������߳�ͳһ�ɷ���Ϣִ�лص���
    // ��Ϣ���������ݣ�Ҳ������һ��APC������ַ������lambda����
    CComQIPtr<RtspSource::ICommand, &IID_IRtspSourceCommand> cmd(_source[i]);
    {
        _threadState[i] = ThreadState::OpenPending;
        cmd->SetNotifyReceiver(this);
        VERIFY_HR(cmd->OpenURL(a->url, a->user_name, a->password));
        _threadState[i] = ThreadState::Opened;
    }
    if (SUCCEEDED(hr) && a->auto_run) {
        {
            xse_arg_pause_t a;
            a.channel = i;
            VERIFY_HR(CMixedGraph::Pause(&a));
        }
        if (SUCCEEDED(hr)) {
            xse_arg_play_t a;
            a.channel = i;
            VERIFY_HR(CMixedGraph::Play(&a));
        }
    }

    return hr;
}
