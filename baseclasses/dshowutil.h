//////////////////////////////////////////////////////////////////////////
// DSUtil.h: DirectShow helper functions.
// 
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//////////////////////////////////////////////////////////////////////////

// Conventions:
//
// Functions named "IsX" return true or false.
//
// Functions named "FindX" enumerate over a collection and return the first
// matching instance. 

#pragma once

#include <dshow.h>
#include <strsafe.h>
#include <assert.h>

#ifndef ASSERT
#define ASSERT assert
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(x) if (x) { x->Release(); x = NULL; }
#endif

#ifndef CHECK_HR
#define CHECK_HR(exp) { if (FAILED(hr = exp)) goto done; }
#endif

#ifndef VERIFY_HR
#define VERIFY_HR(x) hr = x; do { ASSERT(SUCCEEDED(hr)); } while(0)
#endif

/*
    Pin and Filter Functions
    -------------------
    GetPinCategory
    GetPinMediaType
    FindUnconnectedPin
    FindConnectedPin
	FindPinByCategory
	FindPinByIndex
    FindPinByMajorType
    FindPinByName
    FindPinInterface
    FindMatchingPin
    IsPinConnected
    IsPinDirection
    IsPinUnconnected
    IsSourceFilter
    IsRenderer

    Graph Building Functions 
    -----------------------
    AddSourceFilter
    ConnectFilters
    GetNextFilter
    GetConnectedFilter
    RemoveFilter

    Media Type Functions
    --------------------
    CreatePCMAudioType
    CreateRGBVideoType
    DeleteMediaType
    FreeMediaType
    CopyFormatBlock

    Misc Functions
    --------------
    FramesPerSecToFrameLength
    LetterBoxRect
    MsecToRefTime
    RectWidth
    RectHeight
    RefTimeToMsec
    RefTimeToSeconds
    SecondsToRefTime

*/



const LONGLONG ONE_SECOND = 10000000;  // One second, in 100-nanosecond units.
const LONGLONG ONE_MSEC = ONE_SECOND / 10000;  // One millisecond, in 100-nanosecond units

// Directions for filter graph data flow.
enum GraphDirection
{ 
    UPSTREAM, DOWNSTREAM
};


// Forward declares 

void    _FreeMediaType(AM_MEDIA_TYPE& mt);
void    _DeleteMediaType(AM_MEDIA_TYPE *pmt);



/**********************************************************************

    Pin Query Functions - Test pins for various things

**********************************************************************/


///////////////////////////////////////////////////////////////////////
// Name: GetPinCategory
// Desc: Returns the category of a pin
//
// Note: Pin categories are used by some kernel-mode filters to
//       distinguish different outputs. (e.g, capture and preview)
///////////////////////////////////////////////////////////////////////

inline HRESULT GetPinCategory(IPin *pPin, GUID *pPinCategory)
{
    if (pPin == NULL)
    {
        return E_POINTER;
    }
    if (pPinCategory == NULL)
    {
        return E_POINTER;
    }

    HRESULT hr = S_OK;
    DWORD cbReturned = 0;
    IKsPropertySet *pKs = NULL;

    CHECK_HR(pPin->QueryInterface(IID_IKsPropertySet, (void**)&pKs));

    // Try to retrieve the pin category.
    CHECK_HR(pKs->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, NULL, 0, 
        pPinCategory, sizeof(GUID), &cbReturned));
    
    // If this succeeded, pPinCategory now contains the category GUID.


done:
    SAFE_RELEASE(pKs);
    return hr;
}


///////////////////////////////////////////////////////////////////////
// Name: GetPinMediaType
// Desc: Given a pin, find a preferred media type 
//
// pPin         Pointer to the pin
// majorType    Preferred major type (GUID_NULL = don't care)
// subType      Preferred subtype (GUID_NULL = don't care)
// formatType   Preferred format type (GUID_NULL = don't care)
// ppmt         Receives a pointer to the media type. Can be NULL.
//
// Note: If you want to check whether a pin supports a desired media type,
//       but do not need the format details, set ppmt to NULL.
//
//       If ppmt is not NULL and the method succeeds, the caller must
//       delete the media type, including the format block. (Use the
//       DeleteMediaType function.)
///////////////////////////////////////////////////////////////////////

inline HRESULT GetPinMediaType(
    IPin *pPin,             // pointer to the pin
    REFGUID majorType,      // desired major type, or GUID_NULL = don't care
    REFGUID subType,        // desired subtype, or GUID_NULL = don't care
    REFGUID formatType,     // desired format type, of GUID_NULL = don't care
    AM_MEDIA_TYPE **ppmt    // Receives a pointer to the media type. (Can be NULL)
    )
{
    if (!pPin)
    {
        return E_POINTER;
    }

    IEnumMediaTypes *pEnum = NULL;
    AM_MEDIA_TYPE *pmt = NULL;

    HRESULT hr = S_OK;
    bool    bFound = false;
    
    CHECK_HR(pPin->EnumMediaTypes(&pEnum));

    while (hr = pEnum->Next(1, &pmt, NULL), hr == S_OK)
    {
        if ((majorType == GUID_NULL) || (majorType == pmt->majortype))
        {
            if ((subType == GUID_NULL) || (subType == pmt->subtype))
            {
                if ((formatType == GUID_NULL) || (formatType == pmt->formattype))
                {
                    // Found a match. 
                    if (ppmt)
                    {
                        *ppmt = pmt;  // Return it to the caller
                    }
                    else
                    {
                        _DeleteMediaType(pmt);
                    }
                    bFound = true;
                    break;
                }
            }
        }
        _DeleteMediaType(pmt);
    }

done:
    SAFE_RELEASE(pEnum);
    if (SUCCEEDED(hr))
    {
        if (!bFound)
        {
            hr = VFW_E_NOT_FOUND;
        }
    }
    return hr;
}


///////////////////////////////////////////////////////////////////////
// Name: IsPinConnected
// Desc: Query whether a pin is connected to another pin.
//
// Note: If you need to get the other pin, use IPin::ConnectedTo.
///////////////////////////////////////////////////////////////////////

inline HRESULT IsPinConnected(IPin *pPin, BOOL *pResult)
{
    if (pPin == NULL || pResult == NULL)
    {
        return E_POINTER;
    }

    IPin *pTmp = NULL;
    HRESULT hr = pPin->ConnectedTo(&pTmp);
    if (SUCCEEDED(hr))
    {
        *pResult = TRUE;
    }
    else if (hr == VFW_E_NOT_CONNECTED)
    {
        // The pin is not connected. This is not an error for our purposes.
        *pResult = FALSE;
        hr = S_OK;
    }

    SAFE_RELEASE(pTmp);
    return hr;
}


///////////////////////////////////////////////////////////////////////
// Name: IsPinUnconnected
// Desc: Query whether a pin is NOT connected to another pin.
//
///////////////////////////////////////////////////////////////////////

inline HRESULT IsPinUnconnected(IPin *pPin, BOOL *pResult)
{
    // Check if the pin connected.
    HRESULT hr = IsPinConnected(pPin, pResult);
    if (SUCCEEDED(hr))
    {
        // Reverse the result.
        *pResult = !(*pResult);
    }
    return hr;
}


///////////////////////////////////////////////////////////////////////
// Name: IsPinDirection
// Desc: Query whether a pin has a specified direction (input / output)
//
///////////////////////////////////////////////////////////////////////

inline HRESULT IsPinDirection(IPin *pPin, PIN_DIRECTION dir, BOOL *pResult)
{
    if (pPin == NULL || pResult == NULL)
    {
        return E_POINTER;
    }

    PIN_DIRECTION pinDir;
    HRESULT hr = pPin->QueryDirection(&pinDir);
    if (SUCCEEDED(hr))
    {
        *pResult = (pinDir == dir);
    }
    return hr;
}


/**********************************************************************

    Function objects

    These are used in some of the FindXXXX functions.

**********************************************************************/

// MatchPinName: 
// Function object to match a pin by name.
struct MatchPinName
{
    const WCHAR *m_wszName;

    MatchPinName(const WCHAR *wszName)
    {
        m_wszName = wszName;
    }

    HRESULT operator()(IPin *pPin, BOOL *pResult)
    {
        assert(pResult != NULL);

        PIN_INFO PinInfo;
        HRESULT hr = pPin->QueryPinInfo(&PinInfo);
        if (SUCCEEDED(hr))
        {
            PinInfo.pFilter->Release();

            // TODO: Use Strsafe
            if (wcscmp(m_wszName, PinInfo.achName) == 0)
            {
                *pResult = TRUE;
            }
            else
            {
                *pResult = FALSE;
            }
        }
        return hr;
    }
};


// MatchPinDirectionAndConnection
// Function object to match a pin by direction and connection

struct MatchPinDirectionAndConnection
{
    BOOL            m_bShouldBeConnected;
    PIN_DIRECTION   m_direction;


    MatchPinDirectionAndConnection(PIN_DIRECTION direction, BOOL bShouldBeConnected) 
        : m_bShouldBeConnected(bShouldBeConnected), m_direction(direction)
    {
    }

    HRESULT operator()(IPin *pPin, BOOL *pResult)
    {
        assert(pResult != NULL);

        BOOL bMatch = FALSE;
        BOOL bIsConnected = FALSE;

        HRESULT hr = IsPinConnected(pPin, &bIsConnected);
        if (SUCCEEDED(hr))
        {
            if (bIsConnected == m_bShouldBeConnected)
            {
                hr = IsPinDirection(pPin, m_direction, &bMatch);
            }
        }

        if (SUCCEEDED(hr))
        {
            *pResult = bMatch;
        }
        return hr;
    }
};


// MatchPinConnection
// Function object to match a pin connection status

struct MatchPinConnection
{
    BOOL            m_bShouldBeConnected;

    MatchPinConnection(BOOL bShouldBeConnected) 
        : m_bShouldBeConnected(bShouldBeConnected)
    {
    }

    HRESULT operator()(IPin *pPin, BOOL *pResult)
    {
        assert(pResult != NULL);

        BOOL bIsConnected = FALSE;

        HRESULT hr = IsPinConnected(pPin, &bIsConnected);
        if (SUCCEEDED(hr))
        {
            *pResult = (bIsConnected == m_bShouldBeConnected);
        }
        return hr;
    }
};

// MatchPinDirectionAndCategory
// Function object to match a pin by direction and category.
struct MatchPinDirectionAndCategory
{
	const GUID*		m_pCategory;
    PIN_DIRECTION   m_direction;

	MatchPinDirectionAndCategory(PIN_DIRECTION direction, REFGUID guidCategory)
		: m_direction(direction), m_pCategory(&guidCategory)
	{
	}

    HRESULT operator()(IPin *pPin, BOOL *pResult)
    {
        assert(pResult != NULL);

        BOOL bMatch = FALSE;
		GUID category;

        HRESULT hr = IsPinDirection(pPin, m_direction, &bMatch);
			
        if (SUCCEEDED(hr) && bMatch)
        {
			hr = GetPinCategory(pPin, &category);
			if (SUCCEEDED(hr))
			{
				bMatch = (category == *m_pCategory);
			}
        }

        if (SUCCEEDED(hr))
        {
            *pResult = bMatch;
        }
        return hr;
    }
};


struct MatchPinMediaType
{
    GUID            m_majorType;
    MatchPinDirectionAndConnection  m_match1;

    MatchPinMediaType(REFGUID majorType, PIN_DIRECTION dir, BOOL bShouldBeConnected)
        : m_majorType(majorType), m_match1(dir, bShouldBeConnected)
    {
    }

    HRESULT operator()(IPin *pPin, BOOL *pResult)
    {
        assert(pResult != NULL);

        HRESULT hr = S_OK;
        BOOL bMatch = FALSE;

        // First try to match on direction and connection status.
        hr = m_match1(pPin, &bMatch);
        // Next, try to match media types.
        if (SUCCEEDED(hr) && bMatch)
        {
            // For a connected pin, try to match on the
            // media type for the pin connection. Otherwise,
            // try to match on the preferred media type.

            const BOOL bConnected = m_match1.m_bShouldBeConnected;
            if (bConnected)
            {
                AM_MEDIA_TYPE mt = { 0 };

                hr = pPin->ConnectionMediaType(&mt);
                if (SUCCEEDED(hr))
                {
                    bMatch = (mt.majortype == m_majorType);
                    _FreeMediaType(mt);
                }
            }
            else
            {
                hr = GetPinMediaType(pPin, m_majorType, GUID_NULL, GUID_NULL, NULL);
                if (hr == VFW_E_NOT_FOUND)
                {
                    bMatch = FALSE;
                    hr = S_OK;  // Not a failure case, just no match.
                }
            }
        }

        if (SUCCEEDED(hr))
        {
            *pResult = bMatch;
        }
        return hr;
    }
};

/**************************************************************************

    Pin Searching Functions

    These functions search a filter for a pin that matches some set of
    criteria. They return the first pin that matches, or VFW_E_NOT_FOUND.

**************************************************************************/



///////////////////////////////////////////////////////////////////////
// Name: FindPinInterface
// Desc: Search a filter for a pin that exposes a specified interface.
//       (Returns the first instance found.)
// 
// pFilter  Pointer to the filter to search.
// iid      IID of the interface.
// ppUnk    Receives the interface pointer.
// Q        Address of an ATL smart pointer.
//
// Note:    This function returns the first instance that it finds. 
//          If no pin is found, the function returns VFW_E_NOT_FOUND.
//          The templated version deduces the IID.
///////////////////////////////////////////////////////////////////////

inline HRESULT FindPinInterface(
    IBaseFilter *pFilter,  // Pointer to the filter to search.
    REFGUID iid,           // IID of the interface.
    void **ppUnk)          // Receives the interface pointer.
{
    if (!pFilter || !ppUnk) 
    {
        return E_POINTER;
    }

    HRESULT hr = S_OK;
    bool bFound = false;

    IEnumPins *pEnum = NULL;
    IPin *pPin = NULL;

    CHECK_HR(pFilter->EnumPins(&pEnum));

    // Query every pin for the interface.
    while (S_OK == pEnum->Next(1, &pPin, 0))
    {
        hr = pPin->QueryInterface(iid, ppUnk);
        SAFE_RELEASE(pPin);
        if (SUCCEEDED(hr))
        {
            bFound = true;
            break;
        }
    }

done:
    SAFE_RELEASE(pEnum);
    SAFE_RELEASE(pPin);

    return (bFound ? S_OK : VFW_E_NOT_FOUND);
}
template <class Q>
HRESULT FindPinInterface(IBaseFilter *pFilter, Q** pp)
{
    return FindPinInterface(pFilter, __uuidof(Q), (void**)pp);
}



///////////////////////////////////////////////////////////////////////
// Name: FindMatchingPin (template)
// Desc: Return the first pin on a filter that matches a caller-supplied 
//       function or function object
//
// FN must be either
//   (a) A function with the signature HRESULT (IPin*, BOOL *)
//   (b) A class that implements HRESULT operator()(IPin*, BOOL *)
//
// FindMatchingPin halts if FN fails or returns TRUE
///////////////////////////////////////////////////////////////////////

template <class PinPred>
HRESULT FindMatchingPin(IBaseFilter *pFilter, PinPred FN, IPin **ppPin)
{
    IEnumPins* pEnum = NULL;
    IPin* pPin = NULL;

    HRESULT hr = pFilter->EnumPins(&pEnum);
    if (FAILED(hr))
        return hr;

    BOOL bFound = FALSE;
    while (S_OK == pEnum->Next(1, &pPin, NULL))
    {
        hr = FN(pPin, &bFound);
        if (FAILED(hr))
        {
            pPin->Release();
            break;
        }
        if (bFound)
        {
            *ppPin = pPin;
            break;
        }

        pPin->Release();
    }

    pEnum->Release();

    if (!bFound)
        hr = VFW_E_NOT_FOUND;

    return hr;
}



///////////////////////////////////////////////////////////////////////
// Name: FindPinByIndex
// Desc: Return the Nth pin with the specified pin direction.
///////////////////////////////////////////////////////////////////////

inline HRESULT FindPinByIndex(IBaseFilter *pFilter, PIN_DIRECTION PinDir,
	UINT nIndex, IPin **ppPin)
{
	if (!pFilter || !ppPin)
	{
		return E_POINTER;
	}

    HRESULT hr = S_OK;
	bool bFound = false;
	UINT count = 0;

    IEnumPins *pEnum = NULL;
	IPin *pPin = NULL;

	CHECK_HR(pFilter->EnumPins(&pEnum));

	while (S_OK == (hr = pEnum->Next(1, &pPin, NULL)))
	{
		PIN_DIRECTION ThisDir;
		CHECK_HR(pPin->QueryDirection(&ThisDir));

		if (ThisDir == PinDir)
		{
			if (nIndex == count)
			{
				*ppPin = pPin;			// return to caller
                (*ppPin)->AddRef();
				bFound = true;
				break;
			}
			count++;
		}
		SAFE_RELEASE(pPin);
	}

done:
    SAFE_RELEASE(pPin);
	SAFE_RELEASE(pEnum);

    return (bFound ? S_OK : VFW_E_NOT_FOUND);
}

///////////////////////////////////////////////////////////////////////
// Name: FindUnconnectedPin
// Desc: Return the first unconnected input pin or output pin.
///////////////////////////////////////////////////////////////////////

inline HRESULT FindUnconnectedPin(
    IBaseFilter *pFilter,   // Pointer to the filter.
    PIN_DIRECTION PinDir,   // Direction of the pin to find.
    IPin **ppPin            // Receives a pointer to the pin.
    )
{
    return FindMatchingPin(pFilter, MatchPinDirectionAndConnection(PinDir, FALSE), ppPin);
}



///////////////////////////////////////////////////////////////////////
// Name: FindConnectedPin
// Desc: Return the first connected input pin or output pin
///////////////////////////////////////////////////////////////////////

inline HRESULT FindConnectedPin(
    IBaseFilter *pFilter,   // Pointer to the filter.
    PIN_DIRECTION PinDir,   // Direction of the pin to find.
    IPin **ppPin            // Receives a pointer to the pin.
    )
{
    return FindMatchingPin(pFilter, MatchPinDirectionAndConnection(PinDir, TRUE), ppPin);
}


///////////////////////////////////////////////////////////////////////
// Name: FindPinByCategory
// Desc: Find the first pin that matches a specified pin category
//       and direction.
///////////////////////////////////////////////////////////////////////

inline HRESULT FindPinByCategory(
	IBaseFilter *pFilter,   // Pointer to the filter.
	REFGUID guidCategory,   // Category GUID
	PIN_DIRECTION PinDir,   // Pin direction to match.
	IPin **ppPin
	)
{
	return FindMatchingPin(pFilter, MatchPinDirectionAndCategory(PinDir, guidCategory), ppPin);
}




///////////////////////////////////////////////////////////////////////
// Name: FindPinByName
// Desc: Find a pin by name.
//
// Note: Generally, you should find pins by direction, media type,
//       and/or pin category. However, in some cases you may need to
//       find a pin by name.
///////////////////////////////////////////////////////////////////////

inline HRESULT FindPinByName(IBaseFilter *pFilter, const WCHAR *wszName, IPin **ppPin)
{
    if (!pFilter || !wszName || !ppPin)
    {
        return E_POINTER;
    }

    // Verify that wszName is not longer than MAX_PIN_NAME
    size_t cch;
    HRESULT hr = StringCchLengthW(wszName, MAX_PIN_NAME, &cch);

    if (SUCCEEDED(hr))
    {
        hr = FindMatchingPin(pFilter, MatchPinName(wszName), ppPin);
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////
// Name: FindPinByMajorType
// Desc: Find a pin that matches a major media type GUID.
//
// This function also matches my pin direction and pin connection
// status. Hypothetically, this function could allow "don't care"
// values for these. But in my experience, you generally know this
// information for real-world uses (eg., if you are building the graph, 
// versus finding pins on a completed graph).
///////////////////////////////////////////////////////////////////////

inline HRESULT FindPinByMajorType(
    IBaseFilter     *pFilter,   // Pointer to the filter.
    REFGUID         majorType,  // Major media type.
    PIN_DIRECTION   PinDir,     // Pin direction to match.
    BOOL            bConnected, // If TRUE, look for connected pins.
                                // Otherwise, look for unconnected pins.
    IPin            **ppPin     // Receives a pointer to the pin.
    )
{
    if (!pFilter || !ppPin)
    {
        return E_POINTER;
    }

    HRESULT hr = S_OK;

    hr = FindMatchingPin(pFilter, MatchPinMediaType(majorType, PinDir, bConnected), ppPin);

    return hr;
}

/**********************************************************************

    Filter Query Functions  - Test filters for various conditions.

**********************************************************************/

///////////////////////////////////////////////////////////////////////
// Name: IsSourceFilter
// Desc: Query whether a filter is a source filter.
///////////////////////////////////////////////////////////////////////

inline HRESULT IsSourceFilter(IBaseFilter *pFilter, BOOL *pResult)
{
    if (pFilter == NULL || pResult == NULL)
    {
        return E_POINTER;
    }

    IAMFilterMiscFlags *pFlags = NULL;
    IFileSourceFilter *pFileSrc = NULL;

    BOOL bIsSource = FALSE;

    // If the filter exposes the IAMFilterMiscFlags interface, we use
    // IAMFilterMiscFlags::GetMiscFlags to test if this is a source filter.

    // Otherwise, it is a source filter if it exposes IFileSourceFilter.

    // First try IAMFilterMiscFlags
    HRESULT hr = pFilter->QueryInterface(IID_IAMFilterMiscFlags, (void**)&pFlags);
    if (SUCCEEDED(hr))
    {

        ULONG flags = pFlags->GetMiscFlags();
        if (flags &  AM_FILTER_MISC_FLAGS_IS_SOURCE)
        {
            bIsSource = TRUE;
        }
    }
    else
    {
        // Next, look for IFileSourceFilter. 
        hr = pFilter->QueryInterface(IID_IFileSourceFilter, (void**)&pFileSrc);
        if (SUCCEEDED(hr))
        {
            bIsSource = TRUE;
        }
    }

    if (SUCCEEDED(hr))
    {
        *pResult = bIsSource;
    }

    SAFE_RELEASE(pFlags);
    SAFE_RELEASE(pFileSrc);
    return hr;
}


///////////////////////////////////////////////////////////////////////
// Name: IsRenderer
// Desc: Query whether a filter is a renderer filter.
///////////////////////////////////////////////////////////////////////

inline HRESULT IsRenderer(IBaseFilter *pFilter, BOOL *pResult)
{
    if (pFilter == NULL || pResult == NULL)
    {
        return E_POINTER;
    }

    HRESULT hr = S_OK;
    BOOL bIsRenderer = FALSE;

    IAMFilterMiscFlags *pFlags = NULL;
    IEnumPins *pEnum = NULL;
    IPin *pPin = NULL;


    // First try IAMFilterMiscFlags. 
    hr = pFilter->QueryInterface(IID_IAMFilterMiscFlags, (void**)&pFlags);
    if (SUCCEEDED(hr))
    {
        ULONG flags = pFlags->GetMiscFlags();
        if (flags & AM_FILTER_MISC_FLAGS_IS_RENDERER)
        {
            bIsRenderer = TRUE;
        }
    }

    if (!bIsRenderer)
    {
        // Look for the following conditions:

        // 1) Zero output pins AND at least 1 unmapped input pin
        // - or -
        // 2) At least 1 rendered input pin.

        // definitions:
        // unmapped input pin = IPin::QueryInternalConnections returns E_NOTIMPL
        // rendered input pin = IPin::QueryInternalConnections returns "0" slots

        // These cases are somewhat obscure and probably don't apply to many filters
        // that actually exist.

        hr = pFilter->EnumPins(&pEnum);
        if (SUCCEEDED(hr))
        {
            bool bFoundRenderedInputPin = false;
            bool bFoundUnmappedInputPin = false;
            bool bFoundOuputPin = false;

            while (pEnum->Next(1, &pPin, NULL) == S_OK)
            {
                BOOL bIsOutput = FALSE;
                hr = IsPinDirection(pPin, PINDIR_OUTPUT, &bIsOutput);
                if (FAILED(hr))
                {
                    break;
                }
                else if (bIsOutput)
                {
                    // This is an output pin.
                    bFoundOuputPin = true;
                }
                else
                {
                    // It's an input pin. Is it mapped to an output pin?
                    ULONG nPin = 0;
                    hr = pPin->QueryInternalConnections(NULL, &nPin);
                    if (hr == S_OK)
                    {
                        // The count (nPin) was zero, and the method returned S_OK, so
                        // this input pin is mapped to exactly zero ouput pins. 
                        // Therefore, it is a rendered input pin. 
                        bFoundRenderedInputPin = true;

                        // We have met condition (2) above, so we can stop looking.
                        break;

                        // Note: S_FALSE here means "the count (nPin) was not large
                        // enough", ie, this pin is mapped to one or more output pins.

                    }
                    else if (hr == E_NOTIMPL)
                    {
                        // This pin is not mapped to any particular output pin. 
                        bFoundUnmappedInputPin = true;
                        hr = S_OK;
                    }
                    else if (FAILED(hr))
                    {
                        // Unexpected error
                        break;
                    }

                }
                pPin->Release();  // Release for the next loop.
            }  // while

           
            if (bFoundRenderedInputPin)
            {
                bIsRenderer = TRUE; // condition (1) above
            }
            else if (bFoundUnmappedInputPin && !bFoundOuputPin)
            {
                bIsRenderer = TRUE;  // condition (2) above
            }
            else
            {
                hr = S_FALSE;
            }
        }
    }

    if (SUCCEEDED(hr))
    {
        *pResult = bIsRenderer;
    }

    SAFE_RELEASE(pFlags);
    SAFE_RELEASE(pEnum);
    SAFE_RELEASE(pPin);
    return hr;
}

///////////////////////////////////////////////////////////////////////
// Name: GetConnectedFilter
// Desc: Returns the filter on the other side of a pin. 
//
// ie, Given a pin, get the pin connected to it, and return that pin's filter.
// 
// pPin     Pointer to the pin.
// ppFilter Receives a pointer to the filter.
//
///////////////////////////////////////////////////////////////////////

inline HRESULT GetConnectedFilter(IPin *pPin, IBaseFilter **ppFilter)
{
    if (!pPin || !ppFilter)
    {
        return E_POINTER;
    }

    HRESULT hr = S_OK;
    IPin *pPeer = NULL;
    PIN_INFO info;

    ZeroMemory(&info, sizeof(info));

    CHECK_HR(pPin->ConnectedTo(&pPeer));
    CHECK_HR(pPeer->QueryPinInfo(&info));

    assert(info.pFilter != NULL);
    if (info.pFilter)
    {
        *ppFilter = info.pFilter;   // Return pointer to caller.
        (*ppFilter)->AddRef();
        hr = S_OK;
    }
    else
    {
        hr = E_UNEXPECTED;  // Pin does not have an owning filter! That's weird! 
    }

done:
    SAFE_RELEASE(pPeer);
    SAFE_RELEASE(info.pFilter);
    return hr;
}

///////////////////////////////////////////////////////////////////////
// Name: GetNextFilter
// Desc: Find a filter's upstream or downstream neighbor. 
//       (Returns the first instance found.)
// 
// If there is no matching filter, the function returns VFW_E_NOT_CONNECTED.
///////////////////////////////////////////////////////////////////////

HRESULT inline GetNextFilter(
    IBaseFilter *pFilter, // Pointer to the starting filter
    GraphDirection Dir,    // Direction to search (upstream or downstream)
    IBaseFilter **ppNext) // Receives a pointer to the next filter.
{

    PIN_DIRECTION PinDirection = (Dir == UPSTREAM ? PINDIR_INPUT : PINDIR_OUTPUT); 

    if (!pFilter || !ppNext) 
    {
        return E_POINTER;
    }

    IPin *pPin = NULL;
    HRESULT hr = FindConnectedPin(pFilter, PinDirection, &pPin);
    if (SUCCEEDED(hr))
    {
        hr = GetConnectedFilter(pPin, ppNext);
    }

    SAFE_RELEASE(pPin);

    return hr;
}

/**************************************************************************

    Misc. Helper Functions

**************************************************************************/

// CopyFormatBlock: 
// Allocates memory for the format block in the media type and copies the 
// buffer into the format block. Also releases the previous format block.
inline HRESULT CopyFormatBlock(AM_MEDIA_TYPE *pmt, const BYTE *pFormat, DWORD cbSize)
{
    if (pmt == NULL)
    {
        return E_POINTER;
    }

    if (cbSize == 0)
    {
        _FreeMediaType(*pmt);
        return S_OK;
    }

    if (pFormat == NULL)
    {
        return E_INVALIDARG;
    }

    // Reallocate the format block.
    // Note: It is valid to pass NULL to CoTaskMemRealloc.
    // Note: If CoTaskMemRealloc fails to allocate a new block, it does
    //       free the old block. 
    BYTE *pbFormat = (BYTE*)CoTaskMemRealloc(pmt->pbFormat, cbSize);  
    if (pbFormat == NULL)
    {
        return E_OUTOFMEMORY;
    }

    pmt->pbFormat = pbFormat;
    pmt->cbFormat = cbSize;
    CopyMemory(pmt->pbFormat, pFormat, cbSize);
    return S_OK;
}



// RectWidth: Returns the width of a rectangle.
inline LONG RectWidth(const RECT& rc) { return rc.right - rc.left; }

// RectHeight: Returns the height of a rectangle.
inline LONG RectHeight(const RECT& rc) { return rc.bottom - rc.top; }


/********************* Time conversion functions *********************/

///////////////////////////////////////////////////////////////////////
// FramesPerSecToFrameLength
// Converts from frames-to-second to frame duration.
///////////////////////////////////////////////////////////////////////

inline REFERENCE_TIME FramesPerSecToFrameLength(double fps) 
{ 
    return (REFERENCE_TIME)((double)ONE_SECOND / fps);
}

///////////////////////////////////////////////////////////////////////
// RefTimeToMsec
// Convert REFERENCE_TIME units to milliseconds (taken from CRefTime)
///////////////////////////////////////////////////////////////////////

inline LONG RefTimeToMsec(const REFERENCE_TIME& time)
{
	return (LONG)(time / (ONE_SECOND / ONE_MSEC));
}

///////////////////////////////////////////////////////////////////////
// RefTimeToSeconds
// Converts reference time (100 nanosecond units) to floating-point seconds.
///////////////////////////////////////////////////////////////////////

inline double RefTimeToSeconds(const REFERENCE_TIME& rt)
{
    return double(rt) / double(ONE_SECOND);
}

///////////////////////////////////////////////////////////////////////
// SecondsToRefTime
// Converts seconds to reference time.
///////////////////////////////////////////////////////////////////////

inline REFERENCE_TIME SecondsToRefTime(const double& sec)
{
    return (REFERENCE_TIME)(sec * double(ONE_SECOND));
}

///////////////////////////////////////////////////////////////////////
// MsecToRefTime
// Converts milliseconds to reference time.
///////////////////////////////////////////////////////////////////////

inline REFERENCE_TIME MsecToRefTime(const LONG& msec)
{
    return (REFERENCE_TIME)(msec * ONE_MSEC);
}


/********************* Audio and Video Format Functions *********************/


///////////////////////////////////////////////////////////////////////
// Name: CreatePCMAudioType
// Desc: Initialize a PCM audio type with a WAVEFORMATEX format block.
//       (This function does not handle WAVEFORMATEXTENSIBLE formats.)
//
// If the method succeeds, call FreeMediaType to free the format block.
///////////////////////////////////////////////////////////////////////

inline HRESULT CreatePCMAudioType(
    AM_MEDIA_TYPE& mt,      // Media type to populate
    WORD nChannels,         // Number of channels
    DWORD nSamplesPerSec,   // Samples per second
    WORD wBitsPerSample     // Bits per sample
    )
{
    _FreeMediaType(mt);


    mt.pbFormat = (BYTE*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
    if (!mt.pbFormat)
    {
        return E_OUTOFMEMORY;
    }
    mt.cbFormat = sizeof(WAVEFORMATEX);

    mt.majortype = MEDIATYPE_Audio;
    mt.subtype = MEDIASUBTYPE_PCM;
    mt.formattype = FORMAT_WaveFormatEx;

    WAVEFORMATEX* pWav = (WAVEFORMATEX*)mt.pbFormat;
    pWav->wFormatTag = WAVE_FORMAT_PCM;
    pWav->nChannels  = nChannels;
    pWav->nSamplesPerSec = nSamplesPerSec;
    pWav->wBitsPerSample = wBitsPerSample;
    pWav->cbSize = 0;

    // Derived values
    pWav->nBlockAlign = nChannels * (wBitsPerSample  / 8);
    pWav->nAvgBytesPerSec = nSamplesPerSec * pWav->nBlockAlign;

    return S_OK;
}



///////////////////////////////////////////////////////////////////////
// Name: CreateRGBVideoType
// Desc: Initialize an uncompressed RGB video media type.
//       (Allocates the palette table for palettized video)
//
// mt:         Media type to populate
// iBitDepth:  Bits per pixel. Must be 1, 4, 8, 16, 24, or 32
// width:      width in pixels
// height:     height in pixels. Use > 0 for bottom-up DIBs, < 0 for top-down DIB
// fps:        Frame rate, in frames per second
//
// If the method succeeds, call FreeMediaType to free the format block.
///////////////////////////////////////////////////////////////////////

inline HRESULT CreateRGBVideoType(AM_MEDIA_TYPE &mt, long width, long height, double fps)
{
    if (width < 0) {
        return E_INVALIDARG;
    }

    _FreeMediaType(mt);

    mt.pbFormat = (BYTE*)CoTaskMemAlloc(sizeof(VIDEOINFOHEADER2));
    if (mt.pbFormat == nullptr) {
        return E_OUTOFMEMORY;
    }
    mt.cbFormat = sizeof(VIDEOINFOHEADER2);
    mt.majortype == MEDIATYPE_Video;
    mt.subtype = MEDIASUBTYPE_RGB32;
    mt.subtype = FORMAT_VideoInfo2;
    mt.bTemporalCompression = FALSE;
    mt.bFixedSizeSamples = TRUE;

    VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)mt.pbFormat;
    ZeroMemory(vih2, sizeof(VIDEOINFOHEADER2));
    vih2->AvgTimePerFrame = FramesPerSecToFrameLength(fps);

    BITMAPINFOHEADER& bmi = vih2->bmiHeader;
    bmi.biSize = sizeof(BITMAPINFOHEADER);
    bmi.biWidth = width;
    bmi.biHeight = height;
    bmi.biPlanes = 1;
    bmi.biBitCount = 32;
    bmi.biCompression = BI_RGB;
    bmi.biSizeImage = DIBSIZE(vih2->bmiHeader);
    mt.lSampleSize = bmi.biSizeImage;

    return S_OK;
}

///////////////////////////////////////////////////////////////////////
// Name: LetterBoxRect
// Desc: Find the largest rectangle that fits inside rcDest and has
//       the specified aspect ratio.
// 
// aspectRatio: Desired aspect ratio
// rcDest:      Destination rectangle (defines the bounds)
// prcResult:   Pointer to a RECT struct. The method fills in the
//              struct with the letterboxed rectangle.
//
///////////////////////////////////////////////////////////////////////

inline HRESULT LetterBoxRect(const SIZE &aspectRatio, const RECT &rcDest, RECT *prcResult)
{
    if (prcResult == NULL)
    {
        return E_POINTER;
    }

    // Avoid divide by zero (even though MulDiv handles this)
    if (aspectRatio.cx == 0 || aspectRatio.cy == 0)
    {
        return E_INVALIDARG;
    }

    LONG width, height;

    LONG SrcWidth = aspectRatio.cx;
    LONG SrcHeight = aspectRatio.cy;
    LONG DestWidth = rcDest.right - rcDest.left;
    LONG DestHeight = rcDest.bottom - rcDest.top;


   // First try: Letterbox along the sides. ("pillarbox")
    width = MulDiv(DestHeight, SrcWidth, SrcHeight);
    height = DestHeight;
    if (width > DestWidth)
    {
        // Letterbox along the top and bottom.
        width = DestWidth;
        height = MulDiv(DestWidth, SrcHeight, SrcWidth);
    }

    if (width == -1 || height == -1)
    {
        // MulDiv caught an overflow or divide by zero)
        return E_FAIL;
    }

    assert(width <= DestWidth);
    assert(height <= DestHeight);

    // Fill in the rectangle
    prcResult->left = rcDest.left + ((DestWidth - width) / 2);
    prcResult->right = prcResult->left + width;
    prcResult->top = rcDest.top + ((DestHeight - height) / 2);
    prcResult->bottom = prcResult->top + height;

    return S_OK;
}


/********************* Media type functions *********************/


//----------------------------------------------------------------------------
// SetMediaTypeFormat: Sets the format block of a media type
// 
// pmt: Pointer to the AM_MEDIA_TYPE structure. Cannot be NULL
// pBuffer: Pointer to the format block. (Can be NULL)
// cbBuffer: Size of pBuffer in bytes
//
// This function clears the old format block and copies the new
// format block into the media type structure.
//----------------------------------------------------------------------------

inline HRESULT SetMediaTypeFormatBlock(AM_MEDIA_TYPE *pmt, BYTE *pBuffer, DWORD cbBuffer)
{
	if (!pmt)
	{
		return E_POINTER;
	}
	if (!pBuffer && cbBuffer > 0)
	{
		return E_INVALIDARG;
	}

	HRESULT hr = S_OK;

	pmt->pbFormat = (BYTE*)CoTaskMemRealloc(pmt->pbFormat, cbBuffer);
	pmt->cbFormat = cbBuffer;

	if (cbBuffer > 0)
	{
		if (pmt->pbFormat)
		{
			CopyMemory(pmt->pbFormat, pBuffer, cbBuffer);
		}
		else
		{
			pmt->cbFormat = 0;
			hr = E_OUTOFMEMORY;  // CoTaskMemRealloc failed
		}
	}		

	return hr;
}



// The following functions are defined in the DirectShow base class library.
// They are redefined here for convenience, because many applications do not
// need to link to the base class library.

// FreeMediaType: Release the format block for a media type.
inline void _FreeMediaType(AM_MEDIA_TYPE& mt)
{
    if (mt.cbFormat != 0)
    {
        CoTaskMemFree((PVOID)mt.pbFormat);
        mt.cbFormat = 0;
        mt.pbFormat = NULL;
    }
    if (mt.pUnk != NULL)
    {
        // Unecessary because pUnk should not be used, but safest.
        mt.pUnk->Release();
        mt.pUnk = NULL;
    }
}

// DeleteMediaType:
// Delete a media type structure that was created on the heap, including the
// format block)
inline void _DeleteMediaType(AM_MEDIA_TYPE *pmt)
{
    if (pmt != NULL)
    {
        _FreeMediaType(*pmt); 
        CoTaskMemFree(pmt);
    }
}


#ifndef __STREAMS__
#define FreeMediaType _FreeMediaType
#define DeleteMediaType _DeleteMediaType
#endif
