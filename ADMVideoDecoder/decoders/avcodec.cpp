/*
 *      Copyright (C) 2010-2019 Hendrik Leppkes
 *      http://www.1f0.de
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "stdafx.h"
#include "avcodec.h"

#include "moreuuids.h"

#include "Media.h"
#include "IMediaSideData.h"
#include "IMediaSideDataFFmpeg.h"
#include "ByteParser.h"

#ifdef _DEBUG
#include "lavf_log.h"
#endif

extern "C" {
#include "libavutil/pixdesc.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/hdr_dynamic_metadata.h"
};

////////////////////////////////////////////////////////////////////////////////
// Constructor
////////////////////////////////////////////////////////////////////////////////

ILAVDecoder* CreateDecoderAVCodec() {
    return new CDecAvcodec();
}

////////////////////////////////////////////////////////////////////////////////
// Create DXVA2 Extended Flags from a AVFrame and AVCodecContext
////////////////////////////////////////////////////////////////////////////////

static DXVA2_ExtendedFormat GetDXVA2ExtendedFlags(AVCodecContext* ctx, AVFrame* frame)
{
    DXVA2_ExtendedFormat fmt;
    ZeroMemory(&fmt, sizeof(fmt));

    fillDXVAExtFormat(fmt, -1, ctx->color_primaries, ctx->colorspace, ctx->color_trc,
        ctx->chroma_sample_location, true);

    if (frame->format == AV_PIX_FMT_XYZ12LE || frame->format == AV_PIX_FMT_XYZ12BE)
        fmt.VideoPrimaries = DXVA2_VideoPrimaries_BT709;

    // Color Range, 0-255 or 16-235
    BOOL ffFullRange = (ctx->color_range == AVCOL_RANGE_JPEG)
        || frame->format == AV_PIX_FMT_YUVJ420P || frame->format == AV_PIX_FMT_YUVJ422P || frame->format == AV_PIX_FMT_YUVJ444P
        || frame->format == AV_PIX_FMT_YUVJ440P || frame->format == AV_PIX_FMT_YUVJ411P;
    fmt.NominalRange = ffFullRange ? DXVA2_NominalRange_0_255 : (ctx->color_range == AVCOL_RANGE_MPEG) ? DXVA2_NominalRange_16_235 : DXVA2_NominalRange_Unknown;

    return fmt;
}

////////////////////////////////////////////////////////////////////////////////
// avcodec -> LAV codec mappings
////////////////////////////////////////////////////////////////////////////////

// This mapping table should contain all pixel formats, except hardware formats (VDPAU, XVMC, DXVA, etc)
// A format that is not listed will be converted to YUV420
static struct PixelFormatMapping {
    AVPixelFormat  ffpixfmt;
    LAVPixelFormat lavpixfmt;
    BOOL           conversion;
    int            bpp;
} ff_pix_map[] = {
  { AV_PIX_FMT_YUV420P,   LAVPixFmt_YUV420, FALSE },
  { AV_PIX_FMT_RGB24,     LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_YUV410P,   LAVPixFmt_YUV420, TRUE  },
  { AV_PIX_FMT_GRAY8,     LAVPixFmt_YUV420, TRUE  },
  { AV_PIX_FMT_MONOWHITE, LAVPixFmt_YUV420, TRUE  },
  { AV_PIX_FMT_MONOBLACK, LAVPixFmt_YUV420, TRUE  },
  { AV_PIX_FMT_PAL8,      LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_YUVJ420P,  LAVPixFmt_YUV420, FALSE },
  { AV_PIX_FMT_BGR8,      LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_BGR4,      LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_BGR4_BYTE, LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_RGB8,      LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_RGB4,      LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_RGB4_BYTE, LAVPixFmt_RGB32,  TRUE  },

  { AV_PIX_FMT_GRAY16BE,  LAVPixFmt_YUV420, TRUE  },
  { AV_PIX_FMT_GRAY16LE,  LAVPixFmt_YUV420, TRUE  },
  { AV_PIX_FMT_YUVA420P,  LAVPixFmt_YUV420, TRUE  },

  { AV_PIX_FMT_RGB565BE,  LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_RGB565LE,  LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_RGB555BE,  LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_RGB555LE,  LAVPixFmt_RGB32,  TRUE  },

  { AV_PIX_FMT_BGR565BE,  LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_BGR565LE,  LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_BGR555BE,  LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_BGR555LE,  LAVPixFmt_RGB32,  TRUE  },

  { AV_PIX_FMT_RGB444LE,  LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_RGB444BE,  LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_BGR444LE,  LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_BGR444BE,  LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_YA8,       LAVPixFmt_YUV420, TRUE },

  { AV_PIX_FMT_GBRP,      LAVPixFmt_RGB32,  TRUE  },

  { AV_PIX_FMT_0RGB,      LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_RGB0,      LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_0BGR,      LAVPixFmt_RGB32,  TRUE  },
  { AV_PIX_FMT_BGR0,      LAVPixFmt_RGB32,  FALSE },
};

static AVCodecID ff_interlace_capable[] = {
  AV_CODEC_ID_DNXHD,
  AV_CODEC_ID_DVVIDEO,
  AV_CODEC_ID_FRWU,
  AV_CODEC_ID_MJPEG,
  AV_CODEC_ID_MPEG4,
  AV_CODEC_ID_MPEG2VIDEO,
  AV_CODEC_ID_H264,
  AV_CODEC_ID_VC1,
  AV_CODEC_ID_PNG,
  AV_CODEC_ID_PRORES,
  AV_CODEC_ID_RAWVIDEO,
  AV_CODEC_ID_UTVIDEO
};

static struct PixelFormatMapping getPixFmtMapping(AVPixelFormat pixfmt) {
    const PixelFormatMapping def = { pixfmt, LAVPixFmt_YUV420, TRUE, 8 };
    PixelFormatMapping result = def;
    for (int i = 0; i < countof(ff_pix_map); i++) {
        if (ff_pix_map[i].ffpixfmt == pixfmt) {
            result = ff_pix_map[i];
            break;
        }
    }
    result.bpp = 8;
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// AVCodec decoder implementation
////////////////////////////////////////////////////////////////////////////////

CDecAvcodec::CDecAvcodec(void)
    : CDecBase()
{
}

CDecAvcodec::~CDecAvcodec(void)
{
    DestroyDecoder();
}

// ILAVDecoder
STDMETHODIMP CDecAvcodec::Init()
{
#ifdef _DEBUG
    DbgSetModuleLevel(LOG_CUSTOM1, DWORD_MAX); // FFMPEG messages use custom1
    av_log_set_callback(lavf_log_callback);
#else
    av_log_set_callback(nullptr);
#endif
    return S_OK;
}

STDMETHODIMP CDecAvcodec::InitDecoder(AVCodecID codec, const CMediaType* pmt)
{
    DestroyDecoder();
    DbgLog((LOG_TRACE, 10, L"Initializing ffmpeg for codec %S", avcodec_get_name(codec)));

    BITMAPINFOHEADER* bmi = nullptr;
    videoFormatTypeHandler((const BYTE*)pmt->Format(), pmt->FormatType(), &bmi);

    m_pAVCodec = avcodec_find_decoder(codec);
    CheckPointer(m_pAVCodec, VFW_E_UNSUPPORTED_VIDEO);

    m_pAVCtx = avcodec_alloc_context3(m_pAVCodec);
    CheckPointer(m_pAVCtx, E_POINTER);

    DWORD dwDecFlags = m_pCallback->GetDecodeFlags();

    // Use parsing for mpeg1/2 at all times, or H264/HEVC when its not from LAV Splitter
    if (pmt->subtype == MEDIASUBTYPE_HEVC) {
        m_pParser = av_parser_init(codec);
    }

    LONG biRealWidth = bmi->biWidth;
    LONG biRealHeight = bmi->biHeight;
    if (pmt->formattype == FORMAT_VideoInfo2) {
        VIDEOINFOHEADER2* vih2 = (VIDEOINFOHEADER2*)pmt->Format();
        if (vih2->rcTarget.right != 0 && vih2->rcTarget.bottom != 0) {
            biRealWidth = vih2->rcTarget.right;
            biRealHeight = vih2->rcTarget.bottom;
        }
    }

    m_pAVCtx->codec_id = codec;
    m_pAVCtx->codec_tag = bmi->biCompression;
    m_pAVCtx->coded_width = bmi->biWidth;
    m_pAVCtx->coded_height = abs(bmi->biHeight);
    m_pAVCtx->bits_per_coded_sample = bmi->biBitCount;
    m_pAVCtx->err_recognition = 0;
    m_pAVCtx->workaround_bugs = FF_BUG_AUTODETECT;
    //m_pAVCtx->refcounted_frames     = 1;
    m_pAVCtx->thread_count = 1;

    m_pFrame = av_frame_alloc();
    CheckPointer(m_pFrame, E_POINTER);

    // Process Extradata
    size_t extralen = 0;
    getExtraData(*pmt, nullptr, &extralen);

    BOOL bH264avc = FALSE;
    if (extralen > 0) {
        DbgLog((LOG_TRACE, 10, L"-> Processing extradata of %d bytes", extralen));
        BYTE* extra = nullptr;
        {
            // Just copy extradata for other formats
            extra = (uint8_t*)av_mallocz(extralen + AV_INPUT_BUFFER_PADDING_SIZE);
            getExtraData(*pmt, extra, nullptr);
        }
        m_pAVCtx->extradata = extra;
        m_pAVCtx->extradata_size = (int)extralen;
    }

    m_CurrentThread = 0;
    m_rtStartCache = AV_NOPTS_VALUE;

    LAVPinInfo lavPinInfo = { 0 };
    BOOL bLAVInfoValid = SUCCEEDED(m_pCallback->GetLAVPinInfo(lavPinInfo));

    m_bInputPadded = (dwDecFlags & LAV_VIDEO_DEC_FLAG_LAVSPLITTER);

    // Setup codec-specific timing logic

    // Use ffmpegs logic to reorder timestamps
    // This is required for all codecs which use frame re-ordering or frame-threaded decoding (unless they specifically use DTS timestamps, ie. H264 in AVI)
    m_bFFReordering = !(dwDecFlags & LAV_VIDEO_DEC_FLAG_ONLY_DTS) && (m_pAVCodec->capabilities & (AV_CODEC_CAP_DELAY | AV_CODEC_CAP_FRAME_THREADS));

    // Stop time is unreliable, drop it and calculate it
    m_bCalculateStopTime = FALSE;

    // Real Video content has some odd timestamps
    // LAV Splitter does them allright with RV30/RV40, everything else screws them up
    m_bRVDropBFrameTimings = (codec == AV_CODEC_ID_RV10 || codec == AV_CODEC_ID_RV20 || ((codec == AV_CODEC_ID_RV30 || codec == AV_CODEC_ID_RV40) && (!(dwDecFlags & LAV_VIDEO_DEC_FLAG_LAVSPLITTER) || (bLAVInfoValid && (lavPinInfo.flags & LAV_STREAM_FLAG_RV34_MKV)))));

    // Enable B-Frame delay handling
    m_bBFrameDelay = !m_bFFReordering && !m_bRVDropBFrameTimings;

    m_bWaitingForKeyFrame = TRUE;
    m_bResumeAtKeyFrame = FALSE;

    if (FAILED(AdditionaDecoderInit())) {
        return E_FAIL;
    }

    if (bLAVInfoValid) {
        // Try to set the has_b_frames info if available
        if (lavPinInfo.has_b_frames >= 0) {
            DbgLog((LOG_TRACE, 10, L"-> Setting has_b_frames to %d", lavPinInfo.has_b_frames));
            m_pAVCtx->has_b_frames = lavPinInfo.has_b_frames;
        }
    }

    // Open the decoder
    m_bInInit = TRUE;
    int ret = avcodec_open2(m_pAVCtx, m_pAVCodec, nullptr);
    m_bInInit = FALSE;
    if (ret >= 0) {
        DbgLog((LOG_TRACE, 10, L"-> ffmpeg codec opened successfully (ret: %d)", ret));
        m_nCodecId = codec;
    }
    else {
        DbgLog((LOG_TRACE, 10, L"-> ffmpeg codec failed to open (ret: %d)", ret));
        DestroyDecoder();
        return VFW_E_UNSUPPORTED_VIDEO;
    }

    m_iInterlaced = 0;
    for (int i = 0; i < countof(ff_interlace_capable); i++) {
        if (codec == ff_interlace_capable[i]) {
            m_iInterlaced = -1;
            break;
        }
    }

    // Detect chroma and interlaced
    if (m_pAVCtx->extradata && m_pAVCtx->extradata_size) {
        if (codec == AV_CODEC_ID_HEVC) {
            // TODO: do something ?
        }
    }

    if (codec == AV_CODEC_ID_DNXHD)
        m_pAVCtx->pix_fmt = AV_PIX_FMT_YUV422P10;
    else if (codec == AV_CODEC_ID_FRAPS)
        m_pAVCtx->pix_fmt = AV_PIX_FMT_BGR24;

    if (bLAVInfoValid && codec != AV_CODEC_ID_FRAPS)
    {
        if (m_pAVCtx->pix_fmt != AV_PIX_FMT_DXVA2_VLD && m_pAVCtx->pix_fmt != AV_PIX_FMT_D3D11)
            m_pAVCtx->pix_fmt = lavPinInfo.pix_fmt;
        if (m_pAVCtx->sw_pix_fmt == AV_PIX_FMT_NONE)
            m_pAVCtx->sw_pix_fmt = lavPinInfo.pix_fmt;
    }

    DbgLog((LOG_TRACE, 10, L"AVCodec init successfull. interlaced: %d", m_iInterlaced));

    return S_OK;
}

STDMETHODIMP CDecAvcodec::DestroyDecoder()
{
    m_pAVCodec = nullptr;

    if (m_pParser) {
        av_parser_close(m_pParser);
        m_pParser = nullptr;
    }

    if (m_pAVCtx) {
        avcodec_close(m_pAVCtx);
        av_freep(&m_pAVCtx->hwaccel_context);
        av_freep(&m_pAVCtx->extradata);
        av_freep(&m_pAVCtx);
    }
    av_frame_free(&m_pFrame);
    av_freep(&m_pFFBuffer);
    m_nFFBufferSize = 0;

    m_nCodecId = AV_CODEC_ID_NONE;

    return S_OK;
}

static void lav_avframe_free(LAVFrame* frame)
{
    ASSERT(frame->priv_data);
    av_frame_free((AVFrame**)&frame->priv_data);
}

static void avpacket_mediasample_free(void* opaque, uint8_t* buffer)
{
    IMediaSample* pSample = (IMediaSample*)opaque;
    SafeRelease(&pSample);
}

STDMETHODIMP CDecAvcodec::FillAVPacketData(AVPacket* avpkt, const uint8_t* buffer, int buflen, IMediaSample* pSample, bool bRefCounting)
{
    if (m_bInputPadded && (m_pParser == nullptr))
    {
        avpkt->data = (uint8_t*)buffer;
        avpkt->size = buflen;

        if (pSample && bRefCounting && m_pCallback->HasDynamicInputAllocator())
        {
            avpkt->buf = av_buffer_create(avpkt->data, avpkt->size, avpacket_mediasample_free, pSample, AV_BUFFER_FLAG_READONLY);
            if (!avpkt->buf) {
                return E_OUTOFMEMORY;
            }
            pSample->AddRef();
        }
    }
    else
    {
        // create fresh packet
        if (av_new_packet(avpkt, buflen) < 0)
            return E_OUTOFMEMORY;

        // copy data over
        memcpy(avpkt->data, buffer, buflen);
    }

    // copy side-data from input sample
    if (pSample) {
        IMediaSideData* pSideData = nullptr;
        if (SUCCEEDED(pSample->QueryInterface(&pSideData))) {
            size_t nFFSideDataSize = 0;
            const MediaSideDataFFMpeg* pFFSideData = nullptr;
            if (FAILED(pSideData->GetSideData(IID_MediaSideDataFFMpeg, (const BYTE**)&pFFSideData, &nFFSideDataSize)) || nFFSideDataSize != sizeof(MediaSideDataFFMpeg)) {
                pFFSideData = nullptr;
            }

            SafeRelease(&pSideData);
            CopyMediaSideDataFF(avpkt, &pFFSideData);
        }
    }

    return S_OK;
}

STDMETHODIMP CDecAvcodec::Decode(const BYTE* buffer, int buflen, REFERENCE_TIME rtStartIn, REFERENCE_TIME rtStopIn, BOOL bSyncPoint, BOOL bDiscontinuity, IMediaSample* pSample)
{
    CheckPointer(m_pAVCtx, E_UNEXPECTED);

    // Put timestamps into the buffers if appropriate
    if (m_pAVCtx->active_thread_type & FF_THREAD_FRAME)
    {
        if (!m_bFFReordering) {
            m_tcThreadBuffer[m_CurrentThread].rtStart = rtStartIn;
            m_tcThreadBuffer[m_CurrentThread].rtStop = rtStopIn;
        }

        m_CurrentThread = (m_CurrentThread + 1) % m_pAVCtx->thread_count;
    }
    else if (m_bBFrameDelay) {
        m_tcBFrameDelay[m_nBFramePos].rtStart = rtStartIn;
        m_tcBFrameDelay[m_nBFramePos].rtStop = rtStopIn;
        m_nBFramePos = !m_nBFramePos;
    }

    // if we have a parser, it'll handle calling the decode function
    if (m_pParser)
    {
        return ParsePacket(buffer, buflen, rtStartIn, rtStopIn, pSample);
    }
    else
    {
        // Flush the decoder if appropriate
        if (buffer == nullptr)
        {
            return DecodePacket(nullptr, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
        }

        // build an AVPacket
        AVPacket* avpkt = av_packet_alloc();

        // set data pointers
        if (FAILED(FillAVPacketData(avpkt, buffer, buflen, pSample, true)))
        {
            return E_OUTOFMEMORY;
        }

        // timestamps
        avpkt->pts = rtStartIn;
        if (rtStartIn != AV_NOPTS_VALUE && rtStopIn != AV_NOPTS_VALUE)
            avpkt->duration = (int)(rtStopIn - rtStartIn);

        // flags
        avpkt->flags = bSyncPoint ? AV_PKT_FLAG_KEY : 0;

        // perform decoding
        HRESULT hr = DecodePacket(avpkt, rtStartIn, rtStopIn);

        // free packet after
        av_packet_free(&avpkt);

        // forward decoding failures, should only happen when a hardware decoder fails
        if (FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

STDMETHODIMP CDecAvcodec::ParsePacket(const BYTE* buffer, int buflen, REFERENCE_TIME rtStartIn, REFERENCE_TIME rtStopIn, IMediaSample* pSample)
{
    BOOL bFlush = (buffer == NULL);
    int used_bytes = 0;
    uint8_t* pDataBuffer = (uint8_t*)buffer;
    HRESULT hr = S_OK;

    // re-allocate with padding, if needed
    if (m_bInputPadded == false && buflen > 0) {
        // re-allocate buffer to have enough space
        BYTE* pBuf = (BYTE*)av_fast_realloc(m_pFFBuffer, &m_nFFBufferSize, buflen + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!pBuf)
            return E_FAIL;

        m_pFFBuffer = pBuf;

        // copy data to buffer
        memcpy(m_pFFBuffer, buffer, buflen);
        memset(m_pFFBuffer + buflen, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        pDataBuffer = m_pFFBuffer;
    }

    // loop over the data buffer until the parser has consumed all data
    while (buflen > 0 || bFlush) {
        REFERENCE_TIME rtStart = rtStartIn, rtStop = rtStopIn;

        uint8_t* pOutBuffer = nullptr;
        int pOutLen = 0;

        used_bytes = av_parser_parse2(m_pParser, m_pAVCtx, &pOutBuffer, &pOutLen, pDataBuffer, buflen, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

        if (used_bytes == 0 && pOutLen == 0 && !bFlush) {
            DbgLog((LOG_TRACE, 50, L"::Decode() - could not process buffer, starving?"));
            break;
        }
        else if (used_bytes > 0)
        {
            buflen -= used_bytes;
            pDataBuffer += used_bytes;
        }

        // Update start time cache
        // If more data was read then output, update the cache (incomplete frame)
        // If output is bigger, a frame was completed, update the actual rtStart with the cached value, and then overwrite the cache
        if (used_bytes > pOutLen) {
            if (rtStartIn != AV_NOPTS_VALUE)
                m_rtStartCache = rtStartIn;
        }
        else if (used_bytes == pOutLen || ((used_bytes + 9) == pOutLen)) {
            // Why +9 above?
            // Well, apparently there are some broken MKV muxers that like to mux the MPEG-2 PICTURE_START_CODE block (which is 9 bytes) in the package with the previous frame
            // This would cause the frame timestamps to be delayed by one frame exactly, and cause timestamp reordering to go wrong.
            // So instead of failing on those samples, lets just assume that 9 bytes are that case exactly.
            m_rtStartCache = rtStartIn = AV_NOPTS_VALUE;
        }
        else if (pOutLen > used_bytes) {
            rtStart = m_rtStartCache;
            m_rtStartCache = rtStartIn;
            // The value was used once, don't use it for multiple frames, that ends up in weird timings
            rtStartIn = AV_NOPTS_VALUE;
        }

        // decode any parsed data
        if (pOutLen > 0) {
            AVPacket* avpkt = av_packet_alloc();

            // set data pointers
            if (FAILED(FillAVPacketData(avpkt, pOutBuffer, pOutLen, pSample, false)))
            {
                return E_OUTOFMEMORY;
            }

            // timestamp
            avpkt->pts = rtStart;

            // decode the parsed packet
            hr = DecodePacket(avpkt, rtStart, rtStop);

            // and free it after
            av_packet_free(&avpkt);

            if (FAILED(hr)) {
                return hr;
            }
        }
        // or perform a flush at the end
        else if (bFlush)
        {
            hr = DecodePacket(nullptr, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
            if (FAILED(hr)) {
                return hr;
            }
            break;
        }
    }

    return S_OK;
}

STDMETHODIMP CDecAvcodec::DecodePacket(AVPacket* avpkt, REFERENCE_TIME rtStartIn, REFERENCE_TIME rtStopIn)
{
    int ret = 0;
    BOOL bEndOfSequence = FALSE;
    BOOL bDeliverFirst = FALSE;
    REFERENCE_TIME rtStart = rtStartIn;
    REFERENCE_TIME rtStop = rtStopIn;

send_packet:
    // send packet to the decoder
    ret = avcodec_send_packet(m_pAVCtx, avpkt);
    if (ret < 0) {
        // Check if post-decoding checks failed
        if (FAILED(PostDecode())) {
            return E_FAIL;
        }

        if (ret == AVERROR(EAGAIN))
        {
            if (bDeliverFirst)
            {
                DbgLog((LOG_ERROR, 10, L"::Decode(): repeated packet submission to the decoder failed"));
                ASSERT(0);
                return E_FAIL;
            }
            bDeliverFirst = TRUE;
        }
        else
            return S_FALSE;
    }
    else
    {
        bDeliverFirst = FALSE;
    }

    // loop over available frames
    while (1) {
        ret = avcodec_receive_frame(m_pAVCtx, m_pFrame);

        if (FAILED(PostDecode())) {
            av_frame_unref(m_pFrame);
            return E_FAIL;
        }

        // Decoding of this frame failed ... oh well!
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            av_frame_unref(m_pFrame);
            return S_FALSE;
        }

        // Judge frame usability
        // This determines if a frame is artifact free and can be delivered.
        if (m_bResumeAtKeyFrame && m_bWaitingForKeyFrame && ret >= 0) {
            if (m_pFrame->key_frame) {
                DbgLog((LOG_TRACE, 50, L"::Decode() - Found Key-Frame, resuming decoding at %I64d", m_pFrame->pts));
                m_bWaitingForKeyFrame = FALSE;
            }
            else {
                ret = AVERROR(EAGAIN);
            }
        }

        // Handle B-frame delay for frame threading codecs
        if ((m_pAVCtx->active_thread_type & FF_THREAD_FRAME) && m_bBFrameDelay) {
            m_tcBFrameDelay[m_nBFramePos] = m_tcThreadBuffer[m_CurrentThread];
            m_nBFramePos = !m_nBFramePos;
        }

        // no frame was decoded, bail out here
        if (ret < 0 || !m_pFrame->data[0]) {
            av_frame_unref(m_pFrame);
            break;
        }

        ///////////////////////////////////////////////////////////////////////////////////////////////
        // Determine the proper timestamps for the frame, based on different possible flags.
        ///////////////////////////////////////////////////////////////////////////////////////////////
        if (m_bFFReordering) {
            rtStart = m_pFrame->pts;
            if (m_pFrame->pkt_duration)
                rtStop = m_pFrame->pts + m_pFrame->pkt_duration;
            else
                rtStop = AV_NOPTS_VALUE;
        }
        else if (m_bBFrameDelay && m_pAVCtx->has_b_frames) {
            rtStart = m_tcBFrameDelay[m_nBFramePos].rtStart;
            rtStop = m_tcBFrameDelay[m_nBFramePos].rtStop;
        }
        else if (m_pAVCtx->active_thread_type & FF_THREAD_FRAME) {
            unsigned index = m_CurrentThread;
            rtStart = m_tcThreadBuffer[index].rtStart;
            rtStop = m_tcThreadBuffer[index].rtStop;
        }

        if (m_bRVDropBFrameTimings && m_pFrame->pict_type == AV_PICTURE_TYPE_B) {
            rtStart = AV_NOPTS_VALUE;
        }

        if (m_bCalculateStopTime)
            rtStop = AV_NOPTS_VALUE;

        ///////////////////////////////////////////////////////////////////////////////////////////////
        // All required values collected, deliver the frame
        ///////////////////////////////////////////////////////////////////////////////////////////////
        LAVFrame* pOutFrame = nullptr;
        AllocateFrame(&pOutFrame);

        AVRational display_aspect_ratio;
        int64_t num = (int64_t)m_pFrame->sample_aspect_ratio.num * m_pFrame->width;
        int64_t den = (int64_t)m_pFrame->sample_aspect_ratio.den * m_pFrame->height;
        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den, num, den, INT_MAX);

        pOutFrame->width = m_pFrame->width;
        pOutFrame->height = m_pFrame->height;
        pOutFrame->aspect_ratio = display_aspect_ratio;
        pOutFrame->repeat = m_pFrame->repeat_pict;
        pOutFrame->key_frame = m_pFrame->key_frame;
        pOutFrame->frame_type = av_get_picture_type_char(m_pFrame->pict_type);
        pOutFrame->ext_format = GetDXVA2ExtendedFlags(m_pAVCtx, m_pFrame);

        if (m_pFrame->interlaced_frame || (!m_pAVCtx->progressive_sequence && (m_nCodecId == AV_CODEC_ID_H264 || m_nCodecId == AV_CODEC_ID_MPEG2VIDEO)))
            m_iInterlaced = 1;
        else if (m_pAVCtx->progressive_sequence)
            m_iInterlaced = 0;

        if (m_nSoftTelecine > 0)
            m_nSoftTelecine--;

        // Don't apply aggressive deinterlacing to content that looks soft-telecined, as it would destroy the content
        pOutFrame->interlaced = 0;
        pOutFrame->tff = 0;
        pOutFrame->rtStart = rtStart;
        pOutFrame->rtStop = rtStop;
        PixelFormatMapping map = getPixFmtMapping((AVPixelFormat)m_pFrame->format);
        pOutFrame->format = map.lavpixfmt;
        pOutFrame->bpp = map.bpp;

        if (map.conversion) {
            ConvertPixFmt(m_pFrame, pOutFrame);
        }
        else {
            AVFrame* pFrameRef = av_frame_alloc();
            av_frame_ref(pFrameRef, m_pFrame);

            for (int i = 0; i < 4; i++) {
                pOutFrame->data[i] = pFrameRef->data[i];
                pOutFrame->stride[i] = pFrameRef->linesize[i];
            }

            pOutFrame->priv_data = pFrameRef;
            pOutFrame->destruct = lav_avframe_free;
        }

        if (bEndOfSequence)
            pOutFrame->flags |= LAV_FRAME_FLAG_END_OF_SEQUENCE;

        Deliver(pOutFrame);

        if (bEndOfSequence) {
            bEndOfSequence = FALSE;
             Deliver(m_pCallback->GetFlushFrame());
        }

        // increase thread count when flushing
        if (avpkt == nullptr) {
            m_CurrentThread = (m_CurrentThread + 1) % m_pAVCtx->thread_count;
        }
        av_frame_unref(m_pFrame);
    }

    // repeat sending the packet to the decoder if it failed first
    if (bDeliverFirst) {
        goto send_packet;
    }

    return S_OK;
}

STDMETHODIMP CDecAvcodec::Flush()
{
    if (m_pAVCtx && avcodec_is_open(m_pAVCtx)) {
        avcodec_flush_buffers(m_pAVCtx);
    }

    if (m_pParser) {
        av_parser_close(m_pParser);
        m_pParser = av_parser_init(m_nCodecId);
    }

    m_CurrentThread = 0;
    m_rtStartCache = AV_NOPTS_VALUE;
    m_bWaitingForKeyFrame = TRUE;
    m_nSoftTelecine = 0;

    m_nBFramePos = 0;
    m_tcBFrameDelay[0].rtStart = m_tcBFrameDelay[0].rtStop = AV_NOPTS_VALUE;
    m_tcBFrameDelay[1].rtStart = m_tcBFrameDelay[1].rtStop = AV_NOPTS_VALUE;

    if (!(m_pCallback->GetDecodeFlags() & LAV_VIDEO_DEC_FLAG_DVD) && (m_nCodecId == AV_CODEC_ID_H264 || m_nCodecId == AV_CODEC_ID_MPEG2VIDEO)) {
        CDecAvcodec::InitDecoder(m_nCodecId, &m_pCallback->GetInputMediaType());
    }

    return __super::Flush();
}

STDMETHODIMP CDecAvcodec::EndOfStream()
{
    Decode(nullptr, 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, FALSE, FALSE, nullptr);
    return S_OK;
}

STDMETHODIMP CDecAvcodec::GetPixelFormat(LAVPixelFormat* pPix, int* pBpp)
{
    AVPixelFormat pixfmt = m_pAVCtx ? m_pAVCtx->pix_fmt : AV_PIX_FMT_NONE;
    PixelFormatMapping mapping = getPixFmtMapping(pixfmt);
    if (pPix)
        *pPix = mapping.lavpixfmt;
    if (pBpp)
        *pBpp = mapping.bpp;
    return S_OK;
}

STDMETHODIMP CDecAvcodec::ConvertPixFmt(AVFrame* pFrame, LAVFrame* pOutFrame)
{
    // Allocate the buffers to write into
    HRESULT hr = AllocLAVFrameBuffers(pOutFrame);
    if (FAILED(hr))
        return hr;

    // Map to swscale compatible format
    AVPixelFormat dstFormat = getFFPixelFormatFromLAV(pOutFrame->format, pOutFrame->bpp);

    ptrdiff_t linesize[4];
    for (int i = 0; i < 4; i++)
        linesize[i] = pFrame->linesize[i];

    return S_OK;
}

STDMETHODIMP_(REFERENCE_TIME) CDecAvcodec::GetFrameDuration()
{
    if (m_pAVCtx->time_base.den && m_pAVCtx->time_base.num)
        return (REF_SECOND_MULT * m_pAVCtx->time_base.num / m_pAVCtx->time_base.den) * m_pAVCtx->ticks_per_frame;
    return 0;
}

STDMETHODIMP_(BOOL) CDecAvcodec::IsInterlaced(BOOL bAllowGuess)
{
    return FALSE;
}
