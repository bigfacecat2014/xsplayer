#pragma once

#include <initguid.h>

namespace RtspSource {

    enum OpCode
    {
        Unknown,
        Open,
        Play,
        Stop,
        Reconnect,
        Done
    };

    enum ErrorCode
    {
        Success = 0,
        WrongState,
        ClientCreateFailed,
        ServerNotReachable,
        OptionsFailed,
        DescribeFailed,
        SdpInvalid,
        NoSubsessions,
        SetupFailed,
        NoSubsessionsSetup,
        PlayFailed,
        SinkCreationFailed,
        ReconnectFailed
    };

    // called in ICommand calling thread apartment
    interface INotify : public IUnknown
    {
        // 媒体源的帧间隔变化通知。
        STDMETHOD_(void, OnFrameIntervalChanged(int channel, DWORD frameInterval)) = 0;

        //// live555调度线程收到一帧视频，正准备投递给输出Pin的推流线程。
        //STDMETHOD_(void, OnReceivedH265Frame(int channel, void* fmt, void* data, DWORD dataSize)) = 0;
        //// 视频输出Pin的推流线程收到一个帧视频，正准备向下游解码器投递。
        //STDMETHOD_(void, OnBeforeDeliverH265Frame(int channel, void* fmt, void* data, DWORD dataSize)) = 0;
        //// live555调度线程收到一段音频，正准备投递给输出Pin的推流线程。
        //STDMETHOD_(void, OnReceivedAACSegment(int channel, void* fmt, void* data, DWORD dataSize)) = 0;
        //// 视频输出Pin的推流线程收到一段音频，正准备向下游解码器投递。
        //STDMETHOD_(void, OnBeforeDeliverAACSegment(int channel, void* fmt, void* data, DWORD dataSize)) = 0;
    };

    interface ICommand : public IUnknown
    {
        STDMETHOD_(void, SetChannelId(int channel)) = 0;
        STDMETHOD_(void, SetInitialSeekTime(DOUBLE secs)) = 0;
        STDMETHOD_(void, SetStreamingOverTcp(BOOL streamOverTcp)) = 0;
        STDMETHOD_(void, SetTunnelingOverHttpPort(WORD tunnelOverHttpPort)) = 0;
        STDMETHOD_(void, SetAutoReconnectionPeriod(DWORD dwMSecs)) = 0;
        STDMETHOD_(void, SetLatency(DWORD dwMSecs)) = 0;
        STDMETHOD_(void, SetSendLivenessCommand(BOOL sendLiveness)) = 0;
        STDMETHOD_(void, SetNotifyReceiver(INotify* receiver)) = 0;
        STDMETHOD(OpenURL(PCWSTR url, PCWSTR userName, PCWSTR password)) = 0;
    };
} // end namespace RtspSource

// {5EFA6C3B-E602-4DB1-9C37-83B2B4992101}
DEFINE_GUID(IID_IRtspSourceCommand,
    0x5efa6c3b, 0xe602, 0x4db1, 0x9c, 0x37, 0x83, 0xb2, 0xb4, 0x99, 0x21, 0x01);

// 血的教训：
//     不要设计万能的模块。不要泛型技术设计万能的空泛的代码。
//     这种代码往往功能是最弱的，适应变化的能力是最差的。因为真实世界每个细胞分裂克隆的时候都会由误差，
//     变异发生在每一个细胞分裂的过程中，模板这种试图以不变应万变的哲学是背离自然规律的。
//     针对每一个具体的个体设计出具体化的代码，然后把它们用各种千变万化的组织策略函数组织起来。
//     记住：变异无时无刻不在发生，世界总是向着熵增的方向发展，抽象太高意味着假大空。
//     封装的最佳粒度单位就是最基本的C函数，可以一直很容易的扩展新模块，而不影响现有的代码。
//     第一层就是uv,支持UDP和TCP。
//     第二层就是RTP,负载可以是任意媒体类型。
extern HRESULT WINAPI RtspSource_CreateInstance(IBaseFilter** ppObj);
