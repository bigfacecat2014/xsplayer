#pragma once

// {5EFA6C3B-E602-4DB1-9C37-83B2B499CEEC}
DEFINE_GUID(IID_ILAVVideoConfig,
    0x5efa6c3b, 0xe602, 0x4db1, 0x9c, 0x37, 0x83, 0xb2, 0xb4, 0x99, 0xce, 0xec);

// {C9D001E5-6BD8-4153-A9E5-7109BAABC0DB}
DEFINE_GUID(IID_ILAVVideoStatus,
    0xc9d001e5, 0x6bd8, 0x4153, 0xa9, 0xe5, 0x71, 0x9, 0xba, 0xab, 0xc0, 0xdb);

// Codecs supported in the LAV Video configuration
// Codecs not listed here cannot be turned off. You can request codecs to be added to this list, if you wish.
enum LAVVideoCodec {
    Codec_HEVC,
    Codec_VideoNB            // Number of entries (do not use when dynamically linking)
};

// Codecs with hardware acceleration
enum LAVVideoHWCodec {
    HWCodec_HEVC,
    HWCodec_NB,
};

// Flags for HW Resolution support
#define LAVHWResFlag_SD      0x0001
#define LAVHWResFlag_HD      0x0002
#define LAVHWResFlag_UHD     0x0004

// Type of hardware accelerations
enum LAVHWAccel {
    HWAccel_None,
    HWAccel_CUDA,
    HWAccel_QuickSync,
    HWAccel_DXVA2Native,
    HWAccel_D3D11,
    HWAccel_NB,              // Number of HWAccels
};

//
// 公开的输出像素格式。可能与内部输入像素格式不完全相同。
//
enum LAVOutPixFmts {
    LAVOutPixFmt_None = -1,
    LAVOutPixFmt_YV12,            // 4:2:0, 8bit, planar, UV420P平面格式包含：I420和YV12，I420是Y|U|V，YV12是Y|V|U
    LAVOutPixFmt_RGB32,           // 32-bit RGB (BGRA)

    LAVOutPixFmt_NB               // Number of formats
};

enum LAVDitherMode {
    LAVDither_Ordered,
    LAVDither_Random
};

// LAV Video configuration interface
interface __declspec(uuid("5EFA6C3B-E602-4DB1-9C37-83B2B499CEEC")) ILAVVideoConfig : public IUnknown
{
    // Set|Get whether the aspect ratio encoded in the stream should be forwarded to the renderer,
    // or the aspect ratio specified by the source filter should be kept.
    // 0 = AR from the source filter
    // 1 = AR from the Stream
    // 2 = AR from stream if source is not reliable
    STDMETHOD(SetStreamAR)(DWORD bStreamAR) = 0;
    STDMETHOD_(DWORD, GetStreamAR)() = 0;

    // Set|Get the RGB output range for the YUV->RGB conversion
    // 0 = Auto (same as input), 1 = Limited (16-235), 2 = Full (0-255)
    STDMETHOD(SetRGBOutputRange)(DWORD dwRange) = 0;
    STDMETHOD_(DWORD, GetRGBOutputRange)() = 0;

    // Set|Get the dithering mode used
    STDMETHOD(SetDitherMode)(LAVDitherMode ditherMode) = 0;
    STDMETHOD_(LAVDitherMode, GetDitherMode)() = 0;

    // Set|Get the decode output buffer count
    STDMETHOD(SetOutputBufferCount)(int count) = 0;
    STDMETHOD_(int, GetOutputBufferCount)() = 0;
};

// LAV Video status interface
interface __declspec(uuid("C9D001E5-6BD8-4153-A9E5-7109BAABC0DB")) ILAVVideoStatus : public IUnknown
{
    // Get the name of the active decoder (can return NULL if none is active)
    STDMETHOD_(LPCWSTR, GetActiveDecoderName)() = 0;
};

extern HRESULT WINAPI LAVVideo_CreateInstance(IBaseFilter** ppObj);