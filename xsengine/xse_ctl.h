#pragma once

#ifdef _ADM_ACTIVEX

#include "resource.h"
#include <atlctl.h>
#include "xse_ctl_event.h"

class ATL_NO_VTABLE CXSEngine :
	public CComObjectRootEx<CComSingleThreadModel>,
	public CStockPropImpl<CXSEngine, IXSEngine, &IID_IXSEngine, &LIBID_XSEngineLib>,
	public CComControl<CXSEngine>,
	public IPersistStreamInitImpl<CXSEngine>,
	public IOleControlImpl<CXSEngine>,
	public IOleObjectImpl<CXSEngine>,
	public IOleInPlaceActiveObjectImpl<CXSEngine>,
	public IViewObjectExImpl<CXSEngine>,
	public IOleInPlaceObjectWindowlessImpl<CXSEngine>,
	public ISupportErrorInfo,
	public IConnectionPointContainerImpl<CXSEngine>,
	public IPersistStorageImpl<CXSEngine>,
	public ISpecifyPropertyPagesImpl<CXSEngine>,
	public IQuickActivateImpl<CXSEngine>,
	public IDataObjectImpl<CXSEngine>,
	public IProvideClassInfo2Impl<&CLSID_XSEngine, &DIID__IXSEngineEvents, &LIBID_XSEngineLib>,
	public IPropertyNotifySinkCP<CXSEngine>,
	public CComCoClass<CXSEngine, &CLSID_XSEngine>,
	public CProxy_IXSEngineEvents< CXSEngine >,
	public IObjectSafetyImpl<CXSEngine, INTERFACESAFE_FOR_UNTRUSTED_CALLER>
{
public:
	static volatile long _instanceCount; // 活跃的实例数量统计

	CXSEngine()
	{
		InterlockedIncrement(&_instanceCount);
		m_nSides = 3;
		m_clrFillColor = RGB(0, 0xFF, 0);
		m_lastTick = ::GetTickCount();
//#ifdef _DEBUG
		AllocConsole();
		FILE* fo = nullptr;
		_wfreopen_s(&fo, L"CONOUT$", L"w", stdout);
		_wfreopen_s(&fo, L"CONOUT$", L"w", stderr);
		fprintf(stdout, "hello, xsengine ole control.\n");
//#endif
		fprintf(stderr, "----------------------------------------CXSEngine(%p), InstanceCount=%d\n", this, _instanceCount);
	}

	~CXSEngine()
	{
		if (m_xse != nullptr) {
			for (int i = 0; i < XSE_MAX_CHANNEL_COUNT; ++i) {
				xse_arg_stop_t a;
				a.channel = i;
				xse_control(m_xse, &a);
			}
			xse_control(m_xse, nullptr);
			xse_destroy(m_xse);
			m_xse = nullptr;
		}
		if (m_fire_view_change_timer_id != 0) {
			::KillTimer(NULL, m_fire_view_change_timer_id);
			m_timer_id_map.erase(m_fire_view_change_timer_id);
		}
		InterlockedDecrement(&_instanceCount);
		fprintf(stderr, "---------------------------------------~CXSEngine(%p), InstanceCount=%d\n", this, _instanceCount);
	}

	DECLARE_REGISTRY_RESOURCEID(IDR_XSENGINE)

	BEGIN_COM_MAP(CXSEngine)
		COM_INTERFACE_ENTRY_IMPL(IConnectionPointContainer)
		COM_INTERFACE_ENTRY(IXSEngine)
		COM_INTERFACE_ENTRY(IDispatch)
		COM_INTERFACE_ENTRY(IViewObjectEx)
		COM_INTERFACE_ENTRY(IViewObject2)
		COM_INTERFACE_ENTRY(IViewObject)
		COM_INTERFACE_ENTRY(IOleInPlaceObjectWindowless)
		COM_INTERFACE_ENTRY(IOleInPlaceObject)
		COM_INTERFACE_ENTRY2(IOleWindow, IOleInPlaceObjectWindowless)
		COM_INTERFACE_ENTRY(IOleInPlaceActiveObject)
		COM_INTERFACE_ENTRY(IOleControl)
		COM_INTERFACE_ENTRY(IOleObject)
		COM_INTERFACE_ENTRY(IPersistStreamInit)
		COM_INTERFACE_ENTRY2(IPersist, IPersistStreamInit)
		COM_INTERFACE_ENTRY(ISupportErrorInfo)
		COM_INTERFACE_ENTRY(IConnectionPointContainer)
		COM_INTERFACE_ENTRY(ISpecifyPropertyPages)
		COM_INTERFACE_ENTRY(IQuickActivate)
		COM_INTERFACE_ENTRY(IPersistStorage)
		COM_INTERFACE_ENTRY(IDataObject)
		COM_INTERFACE_ENTRY(IProvideClassInfo)
		COM_INTERFACE_ENTRY(IProvideClassInfo2)
		COM_INTERFACE_ENTRY(IObjectSafety)
	END_COM_MAP()

	BEGIN_PROP_MAP(CXSEngine)
		PROP_DATA_ENTRY("_cx", m_sizeExtent.cx, VT_UI4)
		PROP_DATA_ENTRY("_cy", m_sizeExtent.cy, VT_UI4)
	END_PROP_MAP()

	BEGIN_CONNECTION_POINT_MAP(CXSEngine)
		CONNECTION_POINT_ENTRY(DIID__IXSEngineEvents)
		CONNECTION_POINT_ENTRY(IID_IPropertyNotifySink)
	END_CONNECTION_POINT_MAP()

	BEGIN_MSG_MAP(CXSEngine)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		CHAIN_MSG_MAP(CComControl<CXSEngine>)
		DEFAULT_REFLECTION_HANDLER()
	END_MSG_MAP()

	// ISupportsErrorInfo
	STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid)
	{
		static const IID* arr[] =
		{
			&IID_IXSEngine,
		};
		for (int i=0; i<sizeof(arr)/sizeof(arr[0]); i++)
		{
			if (InlineIsEqualGUID(*arr[i], riid))
				return S_OK;
		}
		return S_FALSE;
	}

	// IViewObjectEx
	DECLARE_VIEW_STATUS(VIEWSTATUS_SOLIDBKGND | VIEWSTATUS_OPAQUE)

// IXSEngine
public:
	// IE窗口尺寸变化时即会调用，此时获取窗口并初始化引擎是最佳时机。
	STDMETHOD(SetObjectRects)(_In_ LPCRECT prcPos, _In_ LPCRECT prcClip)
	{
		HRESULT hr = S_OK;

		IOleInPlaceObject_SetObjectRects(prcPos, prcClip);
		if (m_xse == nullptr) {
			this->m_spInPlaceSite->GetWindow(&m_hwndHost);
			hr = xse_create(m_hwndHost, &m_xse);

			xse_arg_view_t a;
			a.mode = 0;  // 初始化视图模式
			xse_control(m_xse, &a);

			// 启动定时器
			if (m_fire_view_change_timer_id == 0) {
				m_fire_view_change_timer_id = ::SetTimer(NULL, 0, 15, &FireViewChangeTimeProc);
				if (m_fire_view_change_timer_id != 0) {
					m_timer_id_map[m_fire_view_change_timer_id] = this;
				}
			}
		}
		if (m_xse != nullptr) {
			xse_arg_sync_resize_t a;
			a.pos_rect = *prcPos;
			a.clip_rect = *prcClip;
			hr = xse_control(m_xse, &a);
		}

		return hr;
	}

	// TODO:总是检查DIB Sample的呈现时间，如果没到也要呈现最后一帧？因为已经重新刷新了。
	// 定时间隔最好根据视频实际最大FPS来决定，没必要对25fps-30fps的视频进行60fps的渲染。
	// 60FPS有一个好处，就是能够渲染处平滑的动画。这个可能要和cocos2d引擎渲染节拍保持一致。
	// 对于60FPS的动画，建议直接多线程渲染，不要和HDC有任何关系，就是在独立的渲染线程渲染。
	// 不要影响浏览器的Tab窗口线程的消息处理。
	RECT m_windowModeBoundRect = { 0 };
	RECT m_lastBoundRect = { 0 };
	int m_drawCount = 0;
	bool m_is_windowed_state = true;
	bool m_is_quiting_fullscreen_state = false; // 正在退出全屏状态
	DWORD m_quiting_start_time = 0; // 退出起始时间
	const DWORD MAX_QUIT_FULLSCREEN_PERIOD = 3000; // 退出全屏还原为窗口的动画最大持续时间（单位：毫秒）
	DWORD m_lastUpdateBeginTime = 0;
	DWORD m_lastRenderBeginTime = 0;
	DWORD m_lastRenderEndTime = 0;

	HRESULT OnDraw(ATL_DRAWINFO& di)
	{
		HRESULT hr = S_OK;

		// 处理引擎的完成通知（回调）。
		if (m_xse != nullptr) {
			xse_arg_t a;
			xse_control(m_xse, &a);
		}

		DispatchRequestQueue();

		char fps_buf[256] = { 0 };
		{
			DWORD curTick = ::GetTickCount();
			if (m_lastTick == 0)
				m_lastTick = curTick;
			DWORD delta = curTick - m_lastTick;
			m_frameCount++;
			if (delta >= 1000) {
				DWORD fps = m_frameCount * 1000 / delta;
				fprintf(stderr, "fps=%d, bound={%d, %d} w=%d, h=%d\n", fps,
					di.prcBounds->left, di.prcBounds->top,
					di.prcBounds->right - di.prcBounds->left,
					di.prcBounds->bottom - di.prcBounds->top);
				m_lastTick = curTick;
				m_frameCount = 0;
			}
		}

		if (m_xse != nullptr) {
			// 退出全屏时，IE窗口有一个由大到小过渡动画导致的天成了一个异步持续动作。
			// 退出全屏状态的过程中，不要渲染，否则会导致IE网页的刷新被此此控件的重绘覆盖。
			if (!m_is_quiting_fullscreen_state) {
				SyncRender(di);
			}
			else {
				// 躲过了一次错误的渲染。
			}
			++m_drawCount;
			if (m_lastBoundRect.left != di.prcBounds->left
				|| m_lastBoundRect.top != di.prcBounds->top 
				|| m_lastBoundRect.right != di.prcBounds->right
				|| m_lastBoundRect.bottom != di.prcBounds->bottom) {
				fprintf(stderr, "(t=%d)(c=%d) bound={%d, %d} w=%d, h=%d\n",
					::timeGetTime(), m_drawCount,
					di.prcBounds->left, di.prcBounds->top,
					di.prcBounds->right - di.prcBounds->left,
					di.prcBounds->bottom - di.prcBounds->top);
				m_lastBoundRect = *(const RECT*)di.prcBounds;
			}

			if (m_is_quiting_fullscreen_state) {
 				DWORD dt = ::timeGetTime() - m_quiting_start_time;
				if (EqualRect(&m_windowModeBoundRect, &m_rcPos) || dt > MAX_QUIT_FULLSCREEN_PERIOD) {
					// 附加的第二个条件是为了预防万一窗口矩形无法精确还原到全屏之前的状态。
					// 一般情况只要过3s，IE窗口退出全屏的渐变动画肯定已经结束了。
					// 已经完成了退出全屏状态（还原至窗口状态）的持续动作。
					fprintf(stderr, "(t=%d)(c=%d) quit fullscreen complete.\n", ::timeGetTime(), m_drawCount);
					m_is_quiting_fullscreen_state = false;
					m_is_windowed_state = true; // 此时才正式还原到窗口状态，这是一个异步过程。
				}
			}
		}

		return S_OK;
	}

	bool SyncUpdate()
	{
		xse_arg_sync_update_t a;
		DWORD curTime = timeGetTime();
		a.base_time = curTime;
		a.last_update_time = m_lastUpdateBeginTime;
		m_lastUpdateBeginTime = curTime;
		a.cur_update_time = curTime;
		xse_control(m_xse, &a);
		return a.result == xse_err_ok ? true : false;
	}

	void SyncRender(ATL_DRAWINFO& di)
	{
		xse_arg_sync_render_t a;
		DWORD curTime = timeGetTime();
		a.base_time = curTime;
		a.last_render_begin_time = m_lastRenderBeginTime;
		a.last_render_end_time = m_lastRenderEndTime;
		a.cur_render_begin_time = curTime;
		a.hdc = di.hdcDraw;
		a.bound_rect = *(LPCRECT)di.prcBounds;
		xse_control(m_xse, &a);
		m_lastRenderBeginTime = curTime;
		m_lastRenderEndTime = timeGetTime();
	}

	DWORD _last_dt = 0;
	short _last_x = -1000;
	short _last_y = -1000;
	int _click_count = 0;

	LRESULT OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
	{
		if (_click_count == 0) {
			_last_dt = ::timeGetTime(); // 第一次按下，开始计时。
			_last_x = GET_X_LPARAM(lParam);
			_last_y = GET_Y_LPARAM(lParam);
		}
		return 0;
	}

	LRESULT OnLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
	{
		++_click_count;
		short x = GET_X_LPARAM(lParam);
		short y = GET_Y_LPARAM(lParam);
		short dist = abs(x - _last_x) + abs(y - _last_y); // 计算点击位置的曼哈顿街区距离偏差。
		DWORD dt = ::timeGetTime();
		if (dist > 32 || dt - _last_dt > 1000) {
			// 偏离了点击位置或者点击太慢，则重置计时器。
			_click_count = 0;
			_last_dt = 0;
			_last_x = -1000;
			_last_y = -1000;
		}
		else if (_click_count == 2) {
			// 完成了一次双击动作，也要重置计数器。
			Fire_onEvent(0, L"TFS");
			_click_count = 0;
			_last_dt = 0;
			_last_x = -1000;
			_last_y = -1000;
		}
		return 0;
	}

	HRESULT STDMETHODCALLTYPE OnWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, __RPC__out LRESULT* plResult)
	{
		HRESULT hr = S_OK;

		HWND hwnd = 0;
		this->m_spInPlaceSite->GetWindow(&hwnd);

		MSG m = { hwnd, msg, wParam, lParam, 0, 0, 0 };
		//TraceMsg(_T("OnWindowMessage:"), &m);

		hr = IOleInPlaceObjectWindowlessImpl<CXSEngine>::OnWindowMessage(msg, wParam, lParam, plResult);

		return hr;
	}

	void TryFireViewChange()
	{
		if (SyncUpdate()) {
			FireViewChange();
		}
	}

	static VOID CALLBACK FireViewChangeTimeProc(HWND, UINT, UINT_PTR id, DWORD)
	{
		TimerIdMap::iterator it = m_timer_id_map.find(id);
		if (it != m_timer_id_map.end()) {
			it->second->TryFireViewChange();
		}
	}

	// 若引擎尚未初始化，则先将请求先缓存起来，等引擎初始化完毕后再调用。
	// 若引擎已经初始化，则直接处理该请求。
	STDMETHOD(post)(BSTR reqStr, int* reqId)
	{
		const PCWSTR CMD_FULLSCREEN = L"FS";
		const PCWSTR CMD_WINDOW = L"WS";

		int id = 0;
		do {
			id = (int)InterlockedIncrement(&m_requestIdHolder);
		} while (id == 0); // 0用于表示无效值
		*reqId = id;

		// 用控件尺寸的事实来判断当前是否处于全屏状态。
		{
			HMONITOR hMonitor = ::MonitorFromWindow(m_hwndHost, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { 0 };
			mi.cbSize = sizeof(mi);
			::GetMonitorInfo(hMonitor, &mi);
			m_is_windowed_state = (HEIGHT(&mi.rcMonitor) != HEIGHT(&m_rcPos));
		}

		if (m_is_windowed_state) { // 当前是窗口状态
			if (wcsncmp(reqStr, CMD_FULLSCREEN, 2) == 0) {
				m_windowModeBoundRect = m_rcPos; // 记住窗口状态时的控件大小
				m_is_windowed_state = false; // 已切换为全屏状态。
				m_is_quiting_fullscreen_state = false; // 重置退出全屏的状态，防止出错，永远停止渲染。
				fprintf(stderr, "(t=%d)(c=%d) switching to fullscreen state. bound={%d, %d} w=%d, h=%d\n", ::timeGetTime(), m_drawCount,
					m_rcPos.left, m_rcPos.top, WIDTH(&m_rcPos), HEIGHT(&m_rcPos));
				return S_OK;;
			}
		}
		else { // 当前是全屏状态
			if (wcsncmp(reqStr, CMD_WINDOW, 2) == 0) {
				fprintf(stderr, "(t=%d)(c=%d) quit fullscreen now...\n", ::timeGetTime(), m_drawCount);
				m_is_quiting_fullscreen_state = true;
				m_quiting_start_time = ::timeGetTime();
				return S_OK;
			}
		}

		if (m_xse == nullptr) {
			_reqQueue.push(std::move(Request(reqStr, id)));
		}
		else {
			if (!HandleRequest(reqStr, id))
				*reqId = 0; // 表示此调用失败，且无异步完成事件通知。
		}

		return S_OK;
	}

	// 分发延迟的post请求队列
	void DispatchRequestQueue()
	{
		while (!_reqQueue.empty()) {
			Request& r = _reqQueue.front();
			HandleRequest(r.reqStr, r.reqId);
			_reqQueue.pop();
		}
	}

	bool HandleRequest(BSTR reqStr, int reqId)
	{
		if (wcslen(reqStr) < wcslen(L"op=stop;ch=1"))
			return false;

		// 字段名称
		const PCWSTR ARG_OP = L"op=";
		const PCWSTR ARG_CHANNEL = L"ch=";
		const PCWSTR ARG_URL = L"url=";
		const PCWSTR ARG_USER_NAME = L"user_name=";
		const PCWSTR ARG_PASSWORD = L"password=";
		const PCWSTR ARG_MODE = L"mode=";

		// 字段名称长度
		const int STR_LEN_OP = wcslen(ARG_OP);
		const int STR_LEN_CHANNEL = wcslen(ARG_CHANNEL);
		const int STR_LEN_URL = wcslen(ARG_URL);
		const int STR_LEN_USER_NAME = wcslen(ARG_USER_NAME);
		const int STR_LEN_PASSWORD = wcslen(ARG_PASSWORD);
		const int STR_LEN_MODE = wcslen(ARG_MODE);

		// OP字段的定义域。
		const PCWSTR OP_PLAY = L"play";
		const PCWSTR OP_STOP = L"stop";
		const PCWSTR OP_VIEW = L"view";

		PCWSTR opStart = StrStrW(reqStr, ARG_OP);
		PCWSTR op = opStart ? &opStart[STR_LEN_OP] : nullptr;

		if (op == nullptr || op[STR_LEN_OP] == 0)
			return false;

		if (wcsncmp(op, OP_PLAY, 4) == 0) {
			int ch = -1;
			wchar_t url[XSE_MAX_URL_LEN + 1] = { 0 };
			wchar_t userName[XSE_MAX_USER_NAME_LEN] = { 0 };
			wchar_t password[XSE_MAX_PASSWORD_LEN] = { 0 };
			// 解析通道号
			{
				PCWSTR channelStart = StrStrW(reqStr, ARG_CHANNEL);
				if (channelStart == nullptr || channelStart[STR_LEN_CHANNEL] == 0)
					return false;
				channelStart += STR_LEN_CHANNEL;
				ch = _wtoi(channelStart) - 1; // 引擎内部通道编号统一从0开始。
				if (ch < XSE_MIN_CHANNEL_ID || ch > XSE_MAX_CHANNEL_ID)
					return false;
			}
			// 解析URL
			{
				PCWSTR urlStart = StrStrW(reqStr, ARG_URL);
				if (urlStart == nullptr || urlStart[STR_LEN_URL] == 0)
					return false;
				urlStart += STR_LEN_URL;
				if (1 != swscanf_s(urlStart, L"%[^;$]", url, _countof(url))) {
					return false;
				}
			}
			// 解析用户名
			{
				PCWSTR userNameStart = StrStrW(reqStr, ARG_USER_NAME);
				if (userNameStart != nullptr && userNameStart[STR_LEN_USER_NAME]) {
					userNameStart += STR_LEN_USER_NAME;
					if (1 != swscanf_s(userNameStart, L"%[^;$]", userName, _countof(userName))) {
						return false;
					}
				}
				else {
					// 用户名可为空
				}
			}
			// 解析密码
			{
				PCWSTR passwordStart = StrStrW(reqStr, ARG_PASSWORD);
				if (passwordStart != nullptr && passwordStart[STR_LEN_PASSWORD]) {
					passwordStart += STR_LEN_PASSWORD;
					if (1 != swscanf_s(passwordStart, L"%[^;$]", password, _countof(password))) {
						return false;
					}
				}
				else {
					// 密码可为空
				}
			}
			// 向xse发送异步播放命令
			{
				xse_arg_open_t a;
				a.channel = ch;
				wcscpy_s(a.url, _countof(a.url), url);
				wcscpy_s(a.user_name, _countof(a.user_name), userName);
				wcscpy_s(a.password, _countof(a.password), password);
				a.cb = &CXSEngine::StaticOnPlayComplete;
				a.ctx = this;
				xse_control(m_xse, &a);
			}
			return true;
		}
		else if (wcsncmp(op, OP_STOP, 4) == 0) {
			int ch = -1;
			// 解析通道号
			{
				PCWSTR channelStart = StrStrW(reqStr, ARG_CHANNEL);
				if (channelStart == nullptr || channelStart[STR_LEN_CHANNEL] == 0)
					return false;
				channelStart += STR_LEN_CHANNEL;
				ch = _wtoi(channelStart) - 1; // 引擎内部通道编号统一从0开始。
				if (ch < XSE_MIN_CHANNEL_ID || ch > XSE_MAX_CHANNEL_ID)
					return false;
			}
			// 向xse发送异步停止命令
			{
				xse_arg_stop_t a;
				a.channel = ch;
				a.cb = &CXSEngine::StaticOnStopComplete;
				a.ctx = this;
				xse_control(m_xse, &a);
			}
			return true;
		}
		else if (wcsncmp(op, OP_VIEW, 4) == 0) {
			int mode = XSE_MIN_VIEW_MODE_ID;
			// 解析通道号
			{
				PCWSTR modeStart = StrStrW(reqStr, ARG_MODE);
				if (modeStart == nullptr || modeStart[STR_LEN_MODE] == 0)
					return false;
				modeStart += STR_LEN_MODE;
				mode = _wtoi(modeStart) - 1; // 引擎内部视图模式统一从0开始。
				if (mode < XSE_MIN_VIEW_MODE_ID || mode > XSE_MAX_VIEW_MODE_ID)
					return false;
			}
			// 向xse发送异步视图设置命令
			{
				xse_arg_view_t a;
				a.mode = mode;
				xse_control(m_xse, &a);
			}
			return true;
		}
		else {
			return false;
		}
	}

	void OnPlayComplete(xse_arg_open_t* arg)
	{
	}

	void OnStopComplete(xse_arg_stop_t* arg)
	{
		if (arg->result == xse_err_ok)
			FireViewChange(); // 擦除大表面的背景
	}

	static void CALLBACK StaticOnPlayComplete(xse_arg_t* arg)
	{
		((CXSEngine*)arg->ctx)->OnPlayComplete((xse_arg_open_t*)arg);
	}

	static void CALLBACK StaticOnStopComplete(xse_arg_t* arg)
	{
		((CXSEngine*)arg->ctx)->OnStopComplete((xse_arg_stop_t*)arg);
	}

public:
	enum { MAX_SIDES = 10000 };
	OLE_COLOR m_clrFillColor;
	int m_nSides = 3;
	POINT m_arrPoint[MAX_SIDES];

	HWND m_hwndHost = 0;
	DWORD m_lastTick = 0;
	DWORD m_frameCount = 0;

	bool m_inc_state = true;
	int m_reqId = 0;
	xse_t m_xse = nullptr;
	volatile unsigned int m_requestIdHolder = 0;

	typedef std::map<UINT_PTR, CXSEngine*> TimerIdMap;
	static TimerIdMap m_timer_id_map;
	UINT_PTR m_fire_view_change_timer_id = 0;

	struct Request {
		PWSTR reqStr;
		int reqId;

		Request(LPCWSTR req, int id) {
			reqStr = _wcsdup(req);
			reqId = id;
		}

		Request(Request&& r) {
			reqStr = r.reqStr;
			r.reqStr = nullptr;
			reqId = r.reqId;
			r.reqId = 0;
		}

		~Request() {
			if (reqStr != nullptr) {
				free(reqStr);
				reqStr = nullptr;
			}
			reqId = 0;
		}
	};
	typedef std::queue<Request> RequestQueue;
	RequestQueue _reqQueue;

}; // end class CXSEngine

#endif // end #ifdef _ADM_ACTIVEX
