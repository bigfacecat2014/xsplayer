#include "stdafx.h"
#include "global.h"

#include <initguid.h>
DEFINE_GUID(CLSID_SampleRenderer,
0x4d4b1600, 0x33ac, 0x11cf, 0xbf, 0x30, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a);

namespace VideoRenderer {

    //-------------------------------------------------------------------------------------------------
    // CGDIVideoInputPin implementation
    //-------------------------------------------------------------------------------------------------
    CGDIVideoInputPin::CGDIVideoInputPin(int channel, TCHAR* pObjectName, CGDIVideoRenderer* pRenderer,
        CCritSec* pInterfaceLock, HRESULT* phr, LPCWSTR pPinName)
        : CRendererInputPin(channel, pRenderer, phr, pPinName),
        m_pRenderer(pRenderer),
        m_pInterfaceLock(pInterfaceLock)
    {
        ASSERT(m_pRenderer);
        ASSERT(pInterfaceLock);
    }

    STDMETHODIMP CGDIVideoInputPin::GetAllocator(IMemAllocator** ppAllocator)
    {
        CheckPointer(ppAllocator, E_POINTER);
        CAutoLock cInterfaceLock(m_pInterfaceLock);

        if (m_pAllocator == NULL) {
            m_pAllocator = (IMemAllocator*)(m_pRenderer->_imageAllocator[m_channel]);
            m_pAllocator->AddRef();
        }

        m_pAllocator->AddRef();
        *ppAllocator = m_pAllocator;

        return S_OK;
    }

    STDMETHODIMP CGDIVideoInputPin::NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly)
    {
        CAutoLock cInterfaceLock(m_pInterfaceLock);

        HRESULT hr = CBaseInputPin::NotifyAllocator(pAllocator, bReadOnly);
        ASSERT(SUCCEEDED(hr));

        return S_OK;
    }

    HRESULT CGDIVideoInputPin::GetMediaType(int iPosition, __inout CMediaType* pMediaType)
    {
        if (iPosition < 0) {
            return E_INVALIDARG;
        }
        if (iPosition > 0) {
            return VFW_S_NO_MORE_ITEMS;
        }
        pMediaType->majortype = MEDIATYPE_Video;
        pMediaType->subtype = MEDIASUBTYPE_RGB32;
        pMediaType->formattype = FORMAT_VideoInfo2;

        return S_OK;
    }

    HRESULT CGDIVideoInputPin::CheckMediaType(const CMediaType* pmt)
    {
        return pmt->subtype == MEDIASUBTYPE_RGB32 ? S_OK : E_FAIL;
    }


    //-------------------------------------------------------------------------------------------------
    // CVideoRenderer implementation
    //-------------------------------------------------------------------------------------------------
    CGDIVideoRenderer::CGDIVideoRenderer(HWND hwndHost, TCHAR* pName, LPUNKNOWN pUnk, HRESULT* phr)
        : CBaseRenderer(CLSID_SampleRenderer, pName, pUnk, phr)
    {
        _hwndHost = hwndHost;

        // TODO��������Pin��ý�������ֶ��л�ȡ��ƵԴ�ߴ硣
        // ��������仯����ô��Ҫ��������SourceRect�ĳߴ硣�����Zoom��������ôӦ�����ö�����Zoom����Դ���Ρ�
        // �����ǲ���ֱ��ʹ�õĳ�ʼֵ��
        RECT sr = { 0, 0, 640, 360 };
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            _imageAllocator[i] = new CImageAllocator(this, TEXT(""), phr),

            _inputPin[i] = new CGDIVideoInputPin(i, TEXT(""), this, &m_InterfaceLock[i], phr, L"");
            m_pInputPin[i] = _inputPin[i]; // weak ptr.

            _drawImage[i] = new CDrawImage();
            _drawImage[i]->InitHostWnd(_hwndHost);
            _drawImage[i]->SetSourceRect(&sr);
        }
        // Ĭ��λ�þ��κͼ��þ���
        {
            RECT rc;
            ::GetClientRect(_hwndHost, &rc);
            _posRect = rc;
            _clipRect = rc;
            CalcLayout();
        }
    }

    CGDIVideoRenderer::~CGDIVideoRenderer()
    {
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            SAFE_DELETE(_imageAllocator[i]);
            SAFE_DELETE(_inputPin[i]);
            m_pInputPin[i] = nullptr;
            SAFE_DELETE(_drawImage[i]);
        }
    }

    HRESULT CGDIVideoRenderer::CheckMediaType(int channel, const CMediaType* pmtIn)
    {
        return _display.CheckMediaType(pmtIn);
    }

    CBasePin* CGDIVideoRenderer::GetPin(int n)
    {
        ASSERT(n >= 0 && n < INPUT_PIN_COUNT);
        if (n < 0 || n >= INPUT_PIN_COUNT) {
            return NULL;
        }

        if (m_pInputPin[n] == NULL) {
            m_pInputPin[n] = _inputPin[n];
        }

        return m_pInputPin[n];
    }

    STDMETHODIMP CGDIVideoRenderer::NonDelegatingQueryInterface(REFIID riid, void** ppv)
    {
        CheckPointer(ppv, E_POINTER);

        if (riid == IID_IVideoRendererCommand) {
            AddRef();
            *ppv = static_cast<ICommand*>(this);
            return S_OK;
        }
        return CBaseRenderer::NonDelegatingQueryInterface(riid, ppv);

    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::SetObjectRects(LPCRECT posRect, LPCRECT clipRect)
    {
        _posRect = *posRect;
        _clipRect = *clipRect;
        CalcLayout();
        return S_OK;
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::SetViewMode(int mode)
    {
        HRESULT hr = S_OK;

        if (mode < VIEW_MODE_1x1 || mode >= VIEW_MODE_COUNT) {
            return E_NOTIMPL;
        }

        _viewMode = (VIEW_MODE)mode;
        CalcLayout();

        return hr;
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::SetLayout(int channel, const ViewportDesc_t* desc)
    {
        HRESULT hr = S_OK;

        _viewportDesc[(int)_viewMode][channel] = *desc;

        return hr;
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::SetSourceFrameInterval(int channel, DWORD frameInterval)
    {
        HRESULT hr = S_OK;

        m_sourceFrameInterval[channel] = frameInterval;

        return hr;
    }
        
    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::SetNotifyReceiver(INotify* receiver)
    {
        return E_NOTIMPL;
    }

    // ����Դ���ʱ��������ڽ���������ʱ��ľ��Ҷ������Ѿ����û���κγ��ֲο���ֵ�ˡ�
    // ��ʱ����Ҫ�����յ������ĵ�ǰʱ�̣����¹��㣬�õ�����ƽ����Ч����������ʱ�䡣
    // һ���������Ϊ�������������������˵ĸ���ƽ���ĳ���ʱ�����
    // ��Ҳ��Ϊɶ��DirectShow��LiveSource����ʱ�����ԭ�򣬴���Ҳû�����壬�������ˡ�
    // ������ݽ�������´�ʱ�������Ҫ�ڴ����߳�ÿ����ȡ�����ֵ�ʱ��ͳ�������Ƿ�
    // �������ˣ��Ҵ����ֶ��г����˿��ѻ�����Ҫ����֡�����
    // ����������Ĵ����ֶ������ǿյ�״̬����ôӦ������֡�������������
    // TODO:��UI�̲߳Ż����ʱ����������߳̽����У������κ�ʱ������ؼ��㡣
    STDMETHODIMP_(BOOL __stdcall) CGDIVideoRenderer::Update(TimeContext* tc)
    {
        BOOL isChanged = FALSE;
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            if (Update(i, tc))
                isChanged = TRUE;
        }
        return isChanged;
    }

    BOOL CGDIVideoRenderer::Update(int channel, TimeContext* tc)
    {
        BOOL needPresent = FALSE;
        DWORD curTime = (DWORD)tc->cur_update_time;
        DWORD dt = 0; // ��ǰ��֡����ʱ�䡣

        // ���û���յ��κ�����ֱ�ӷ���
        if (!m_isFirstSampleReceived[channel])
            return FALSE;

        // һ���Խ��ѽ������������е�����ȫ�����Ƶ������ֶ��С�
        int count = m_presentSampleCount[channel];
        int newCount = 0;
        IMediaSample* tempQueue[JITTER_BUFFER] = { 0 };
        {
            CAutoLock lock(&m_pendingSampleQueueLock[channel]);
            newCount = m_pendingSampleCount[channel];
            if (newCount > 0) {
                memcpy(tempQueue, m_pendingSampleQueue[channel], newCount * sizeof(void*));
                m_pendingSampleCount[channel] = 0;
            }
        }
        memcpy(&m_presentSampleQueue[channel][count], tempQueue, newCount * sizeof(void*));
        count += newCount;
        m_presentSampleCount[channel] = count;
#ifdef _DEBUG
        for (int i = count; i < JITTER_BUFFER; ++i) {
            m_presentSampleQueue[channel][i] = nullptr;
        }
#endif

        // ����ʱ����ߣ���ǰʱ����
        if (m_isFirstSample[channel]) {
            m_isFirstSample[channel] = false;

            dt = m_presentInterval[channel] = m_sourceFrameInterval[channel];
            // ÿ��ͨ���ĳ���ʱ������߲�һ������Ϊÿ��ͨ��ý��ʱ���ǲ���صģ�������ȫ�����ġ�
            m_presentTimeBaseLine[channel] = curTime;
            m_nextPTS[channel] = curTime;
        }
        else {
            // ������PTS���������ٵ���ʱ�򣬾���Ҫ��̬�����ӳ��ˣ����򶶶���Խ��Խ������
            // ��Ϊ��ʵʱԴ�����ǲ���ͨ���������ƽӿڣ�Ҫ��ʵʱԴ�ӿ�����������
            // ֻ���ó���������ѡ�������ӳ٣�����һ����ٲ��š�
            // TODO: ��Ϊ��̬��������֡������������ʱ���������ˣ��������ںܾ��ˣ�����ô˵����Ҫ������ּ����
            // ����������������ƽ�����ڳ̶��йأ��������һ����ƽ��ÿ֡����20ms����ô��Ҫ�����ּ������20ms��
            if (m_nextPTS[channel] == 0) {
                if (count == 0 && channel == 0)
                    printf("oh, no, no, no, .......sample pool is dry.\n");
                // TODO: Ԥ����������ٶȵĹյ㣬�����ּ��ġ�
                if (count >= BP_IDEA_QUEUE_LEN) {
                    if (m_boostState[channel] == BS_SLOWER) {
                        // �����ع鵽����ֵ��Ȼ�����������١�
                        m_presentIntervalAdjust[channel] = 100;
                    }
                    m_boostState[channel] = BS_FASTER;
                }
                else {
                    if (m_boostState[channel] == BS_FASTER) {
                        // �����ع鵽����ֵ��Ȼ���������١�
                        m_presentIntervalAdjust[channel] = 100;
                    }
                    m_boostState[channel] = BS_SLOWER;
                }
                if (m_boostState[channel] == BS_FASTER) { // ���FPS
                    // ���ٵļ�����2���٣���֡���Ϊ����ֵ��50%��
                    if (m_presentIntervalAdjust[channel] > 50) {
                        m_presentIntervalAdjust[channel] -= 5; // ÿ�μ���10%��1���ڼ���������2���١�
                    }
                }
                else { // ����FPS
                    // ���ٵļ�����0.5���٣���֡���Ϊ����ֵ��200%��
                    if (m_presentIntervalAdjust[channel] < 200) {
                        m_presentIntervalAdjust[channel] += 5; // ÿ������10%��1���ڼ��ɽ�����0.5���١�
                    }
                }
                dt = (m_presentInterval[channel] * m_presentIntervalAdjust[channel]) / 100;
                m_nextPTS[channel] = m_lastPTS[channel] + dt;
            }
            else {
                // ��Ҫ����ʱ�䣬��Ϊ����ͷ����Sample��û�г��֡�
            }
        }

        // �ж��Ƿ���Ҫ��������
        needPresent = (count > 0 && m_nextPTS[channel] > 0 && curTime >= m_nextPTS[channel]);

        // TODO:������ʱ������̫���Ķ��������߼ӿ첥�ţ�
        // �ӿ���֣��������һֱ���ֹ��ڣ���������Դ������������δ���������ѻ���
        // ��ֻ֡���Ǵ�δ�����Դ�Ǹ����ڶ�֡��
        // ����ģ���Ϊֻ�е�һ��֡�������̴߳��˴�����ʱ�������һ֡��������������û�д�ʱ������������塣

        return needPresent;
    }

    HRESULT CGDIVideoRenderer::Render(int channel, HDC hdcDraw)
    {
        DWORD curTime = timeGetTime();
        REFERENCE_TIME tsStart = 0;
        REFERENCE_TIME tsStop = 0;

        // ����ʹ�ö����������
        if (m_presentSampleCount[channel] > 0) {
            // �ȹ黹��һ��������
            SAFE_RELEASE(m_lastPresentSample[channel]);
            // ��ȡ�µ�������
            IMediaSample* ms = m_presentSampleQueue[channel][0];
            assert(ms != nullptr);
            m_lastPresentSample[channel] = ms;
            DoRenderSample(channel, ms, hdcDraw);
        }
        else {
            if (m_lastPresentSample[channel] != nullptr) {
                DoRenderSample(channel, m_lastPresentSample[channel], hdcDraw);
            }
            else {
                return S_OK; // ���κ������ɹ���Ⱦ��
            }
        }

        m_lastPTS[channel] = timeGetTime(); // �����ۻ���
        m_nextPTS[channel] = 0; // ���ã�׼�����ڼ�¼��һ�������ĳ���ʱ�����

        // PTS���Ǹ������������ˣ��𲻵��κζ�ʱ�Ҿ�����������ã�����Ϊʱ�����Ҳ����Ѳ�����ġ�
        // �����Ǹոպòȵ����ϣ�������ģ��������1-3���룬�ⶼ���Ե���ƽ����
        // ʵ������ǣ�����ÿ֡�����˽ӽ�30ms�������϶����С�
        if (channel == 0) {
            if (m_debugLastPTS[channel] == 0) {
                m_debugLastPTS[channel] = curTime;
            }

            DWORD dt = curTime - m_debugLastPTS[channel];
            printf("pts[%d]=%3d, queue len=%d, adjust=%d\n", channel, dt - 40,
                m_presentSampleCount[channel], m_presentIntervalAdjust[channel]);
            m_debugLastPTS[channel] = curTime;
        }

        // ���ѳ��ֵ������ӳ��ֶ�����������
        if (m_presentSampleCount[channel] > 0) {
            --m_presentSampleCount[channel]; // �Ѿ����ѵ���һ��������
            int count = m_presentSampleCount[channel];
            memmove(&m_presentSampleQueue[channel][0], &m_presentSampleQueue[channel][1], count * sizeof(void*));
            m_presentSampleQueue[channel][count] = nullptr;
            ++m_receivedSampleCount[channel];
        }

        return S_OK;
    }

    STDMETHODIMP_(void __stdcall) CGDIVideoRenderer::Render(DeviceContext* dc)
    {
        // TODO���ƶ���SetObjectRects()������ȥ,�����ӿ���������������¼���ÿһ��ģʽÿһ��ͨ���ı߽���Ρ�
        // TODO:��Ϊ��Ⱦ�ϳɺõĴ���档
        for (int i = 0; i < INPUT_PIN_COUNT; ++i) {
            const RECT* r = &_viewportRect[(int)_viewMode][i];
            RECT tr = *r;
            ::OffsetRect(&tr, dc->boundRect.left, dc->boundRect.top);
            _drawImage[i]->SetTargetRect(&tr);
            // ����ʱ�������ˣ��Ż��������ֶ��еĵ�һ��������
            // 16��ͨ���У���Ȼ����һ��ͨ���Ѿ������˳���ʱ�������������
            Render(i, dc->hdcDraw);
        }
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::Stop(int channel)
    {
        return StopChannel(channel);
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::Pause(int channel)
    {
        return PauseChannel(channel);
    }

    STDMETHODIMP_(HRESULT __stdcall) CGDIVideoRenderer::Run(int channel, REFERENCE_TIME startTime)
    {
        return RunChannel(channel, startTime);
    }

    HRESULT CGDIVideoRenderer::Active(int channel)
    {
        SetRepaintStatus(channel, FALSE);
        return CBaseRenderer::Active(channel);
    }

    // ��ʱ��m_mtIn�����������Լ�����Ĳ�������ý�����͡�
    // ��Ϊ���ڽ�������ʱ�����ǿ��ܲ���֪�������������ͼƬ�Ŀ����Ϣ��
    // ý��Э�̹����У�������Ҳû�и���������������Pin��ý���������顣
    // ���һ���������������Ⱦ����������������������ŵ�ý���������ΪRGB32��
    // Ϊ�˼򵥻���
    // LAVVideo���ڽ����һ��֡��ʱ����CLAVVideo::DeliverToRenderer(LAVFrame* pFrame)������
    // �ж���Ҫm_bSendMediaTypeʱ���д�һЩý����ϸ��Ϣ��
    // ����IMediaSample*�ϸ���һЩý��������Ϣ����Ⱦ���������յ��������ʱ��
    // �����Ƿ����³�ʼ����Ⱦ�豸��
    // ��ʹ��Activate()��ʱ��Ҳ���ܵõ����ε�ý�����͵���ϸ��Ϣ��
    // ��ʱ���Լ򵥺��Բ�ȫ���ý���ʽ��Ϣ�������ν�����Reconnect�󣬻��ٴ����ø���ȷ��ý��������Ϣ��
    // �����ӳٵ��Ǹ�ʱ���ٸ�����ʾ��Ϣ��
    HRESULT CGDIVideoRenderer::SetMediaType(int channel, const CMediaType* pmt)
    {
        HRESULT hr = S_OK;
        int i = channel;
        CAutoLock channelLock(&m_InterfaceLock[channel]);

        CMediaType oldFormat(_mtIn[i]); // old media type

        // Fill out the optional fields in the VIDEOINFOHEADER2
        _mtIn[i] = *pmt;
        VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)_mtIn[i].Format();
        _display.UpdateFormat(vih2);
        _drawImage[i]->NotifyMediaType(&_mtIn[i]);
        RECT sr = { 0, 0, vih2->bmiHeader.biWidth, vih2->bmiHeader.biHeight };
        _drawImage[i]->SetSourceRect(&sr);
        _imageAllocator[i]->NotifyMediaType(&_mtIn[i]);

        return S_OK;
    }

    HRESULT CGDIVideoRenderer::BreakConnect(int channel)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);

        HRESULT hr = CBaseRenderer::BreakConnect(channel);
        if (FAILED(hr)) {
            return hr;
        }

        // ʲô��������Ⱦ��
        _mtIn[channel].ResetFormatBuffer();

        return S_OK;

    }
    HRESULT CGDIVideoRenderer::CompleteConnect(int channel, IPin* pReceivePin)
    {
        CAutoLock channelLock(&m_InterfaceLock[channel]);
        return CBaseRenderer::CompleteConnect(channel, pReceivePin);
    }

    HRESULT CGDIVideoRenderer::DoRenderSample(int channel, IMediaSample* pMediaSample, HDC hdcDraw)
    {
        return _drawImage[channel]->DrawImage(pMediaSample, hdcDraw);
    }

} // end namespace VideoRenderer

HRESULT WINAPI GDIRenderer_CreateInstance(HWND hwndHost, IBaseFilter** ppObj)
{
    HRESULT hr = S_OK;

    auto o = new VideoRenderer::CGDIVideoRenderer(hwndHost, TEXT(""), nullptr, &hr);
    ULONG ul = o->AddRef();
    *ppObj = static_cast<IBaseFilter*>(o);

    return hr;
}

