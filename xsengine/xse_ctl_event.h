#pragma once

template <class T>
class CProxy_IXSEngineEvents : public IConnectionPointImpl<T, &DIID__IXSEngineEvents, CComDynamicUnkArray>
{
public:
	VOID Fire_onEvent(int reqId, PCWSTR rspStr)
	{
		T* pT = static_cast<T*>(this);
		int nConnectionIndex;
		CComVariant* pvars = new CComVariant[2];
		int nConnections = m_vec.GetSize();

		CComBSTR bstrRspStr(rspStr);
		for (nConnectionIndex = 0; nConnectionIndex < nConnections; nConnectionIndex++)
		{
			pT->Lock();
			CComPtr<IUnknown> sp = m_vec.GetAt(nConnectionIndex);
			pT->Unlock();
			IDispatch* pDispatch = reinterpret_cast<IDispatch*>(sp.p);
			if (pDispatch != NULL)
			{

				pvars[1].vt = VT_I4;
				pvars[1].lVal = reqId;
				pvars[0].vt = VT_BSTR;
				pvars[0].bstrVal = bstrRspStr;// ::SysAllocString(rspStr);
				DISPPARAMS disp = { pvars, NULL, 2, 0 };
				pDispatch->Invoke(0x1, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &disp, NULL, NULL, NULL);
			}
		}

		delete[] pvars;
	}
};
