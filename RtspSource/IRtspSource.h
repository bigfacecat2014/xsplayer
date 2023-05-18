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
        // ý��Դ��֡����仯֪ͨ��
        STDMETHOD_(void, OnFrameIntervalChanged(int channel, DWORD frameInterval)) = 0;

        //// live555�����߳��յ�һ֡��Ƶ����׼��Ͷ�ݸ����Pin�������̡߳�
        //STDMETHOD_(void, OnReceivedH265Frame(int channel, void* fmt, void* data, DWORD dataSize)) = 0;
        //// ��Ƶ���Pin�������߳��յ�һ��֡��Ƶ����׼�������ν�����Ͷ�ݡ�
        //STDMETHOD_(void, OnBeforeDeliverH265Frame(int channel, void* fmt, void* data, DWORD dataSize)) = 0;
        //// live555�����߳��յ�һ����Ƶ����׼��Ͷ�ݸ����Pin�������̡߳�
        //STDMETHOD_(void, OnReceivedAACSegment(int channel, void* fmt, void* data, DWORD dataSize)) = 0;
        //// ��Ƶ���Pin�������߳��յ�һ����Ƶ����׼�������ν�����Ͷ�ݡ�
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

// Ѫ�Ľ�ѵ��
//     ��Ҫ������ܵ�ģ�顣��Ҫ���ͼ���������ܵĿշ��Ĵ��롣
//     ���ִ������������������ģ���Ӧ�仯�����������ġ���Ϊ��ʵ����ÿ��ϸ�����ѿ�¡��ʱ�򶼻�����
//     ���췢����ÿһ��ϸ�����ѵĹ����У�ģ��������ͼ�Բ���Ӧ������ѧ�Ǳ�����Ȼ���ɵġ�
//     ���ÿһ������ĸ�����Ƴ����廯�Ĵ��룬Ȼ��������ø���ǧ���򻯵���֯���Ժ�����֯������
//     ��ס��������ʱ�޿̲��ڷ����������������������ķ���չ������̫����ζ�żٴ�ա�
//     ��װ��������ȵ�λ�����������C����������һֱ�����׵���չ��ģ�飬����Ӱ�����еĴ��롣
//     ��һ�����uv,֧��UDP��TCP��
//     �ڶ������RTP,���ؿ���������ý�����͡�
extern HRESULT WINAPI RtspSource_CreateInstance(IBaseFilter** ppObj);
