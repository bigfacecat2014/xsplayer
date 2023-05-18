#pragma once

#define WIN32_LEAN_AND_MEAN                 // Exclude rarely-used stuff from Windows headers
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS  // some CString constructors will be explicit
#define VC_EXTRALEAN                        // Exclude rarely-used stuff from Windows headers

#define _WIN32_WINNT 0x0601

#include <intsafe.h>
#include <strsafe.h>
#include <assert.h>

#include <Windows.h>
#include <windowsx.h>
#include <olectl.h>
#include <ddraw.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <wmistr.h>
#include <evntrace.h>
#include <perfstruct.h>
#include <dshow.h>
#include <dvdmedia.h>

#include <atlcoll.h>

