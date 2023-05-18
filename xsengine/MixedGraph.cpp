#include "stdafx.h"
#include "global.h"

//
// 接口设计应该尽量暴露程序员的愚蠢，并督促其立即纠正。
// 系统的容错设计是指：容忍自然界输入信号的各种噪声，畸变以及电子元器件失效。
// 而不是要去容忍程序的马虎、愚蠢、低智商。
// 逻辑思维不严密的程序员不可能写出逻辑严密的代码。
// 简单确定的设计 优于 复杂灵活的设计。
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
        _pendingTaskQueue[i].pop(ti); // 可能会被阻塞在这里！
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

    // TODO:多个源的RtspClient对象，共享一个网络通讯select线程？
    // 如何分发线程内所有对象的回调队列？必须设计CDispatcher基类？
    // 所有具备接受和处理异步线程消息的都要从这个基类派生，交给线程统一派发消息执行回调。
    // 消息可以是数据，也可以是一个APC函数地址，或者lambda对象。
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
