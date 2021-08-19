//
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Contributors to the OpenEXR Project.
//


#include "ImfZstdCompressor.h"
#include "ImfCheckedArithmetic.h"
#include "ImfStandardAttributes.h"
#include "Iex.h"
#include "ImfNamespace.h"
#include "ImfSimd.h"
#include "ImfZip.h"
#include "zstd/zstd.h"
#include <assert.h>

OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_ENTER

//#define USE_EXISTING_EXR_FILTER 1
//#define USE_N_PREDICTOR 1
//#define USE_W_PREDICTOR 1
//#define USE_16BIT_FILTER 1

static int
Predict (
    #if USE_16BIT_FILTER
    const unsigned short* src,
    #else
    const unsigned char* src,
    #endif
    size_t idx, size_t rowStride)
{
#if USE_16BIT_FILTER
    size_t colStride = 1;
#else
    size_t colStride = 2;
#endif
    #if USE_N_PREDICTOR
    if (idx < rowStride) return 0;
    return src[idx - rowStride];
    #elif USE_W_PREDICTOR
    if (idx < colStride) return 0;
    return src[idx - colStride];
    #else
    //if (idx < rowStride + colStride) return 0;
    //size_t idxN  = idx - rowStride;
    //size_t idxW  = idx - colStride;
    //size_t idxNW = idx - rowStride - colStride;

    if (idx < colStride) return 0;
    size_t idxN  = idx - (idx >= rowStride ? rowStride : colStride);
    size_t idxW  = idx - colStride;
    size_t idxNW = idx - (idx >= rowStride + colStride ? rowStride + colStride
                                                       : colStride);
    int    vN    = src[idxN];
    int    vW    = src[idxW];
    int    vNW   = src[idxNW];
    int    grad  = vN + vW - vNW;
    int    lo    = std::min (vN, vW);
    int    hi    = std::max (vN, vW);
    if (grad < lo) grad = lo;
    if (grad > hi) grad = hi;
    #if USE_16BIT_FILTER
    assert (grad >= 0 && grad <= 0xFFFF);
    #else
    assert (grad >= 0 && grad <= 0xFF);
    #endif
    return grad;
    #endif
}

static void
MyFilterBeforeCompression (
    const unsigned char* raw,
    size_t               size,
    size_t               rowStride,
    unsigned char*       outBuffer)
{
#if USE_EXISTING_EXR_FILTER
    FilterBeforeCompression((const char*)raw, size, (char*)outBuffer);
    #elif USE_16BIT_FILTER
    assert ((size % 2) == 0);
    const unsigned short* raw2 = (const unsigned short*) raw;
    size_t halfSize = size / 2;
    size_t i = 0;
    while (i < halfSize)
    {
        int v            = raw2[i];
        int p            = Predict (raw2, i, rowStride/2);
        unsigned short nv = v - p;
        outBuffer[i] = nv & 0xFF;
        outBuffer[i + halfSize] = nv >> 8;
        ++i;
    }
    #else
    size_t halfSize = size / 2;
    size_t i        = 0;
    while (i < size)
    {
        int v            = raw[i];
        int p            = Predict (raw, i, rowStride);
        outBuffer[i / 2] = v - p;
        ++i;
        v                           = raw[i];
        p                           = Predict (raw, i, rowStride);
        outBuffer[i / 2 + halfSize] = v - p;
        ++i;
    }
    #endif
}

static void
MyUnfilterAfterDecompression (
    unsigned char* src,
    size_t         size,
    size_t         rowStride,
    unsigned char* outBuffer)
{
#if USE_EXISTING_EXR_FILTER
    UnfilterAfterDecompression ((char*)src, size, (char*)outBuffer);
#elif USE_16BIT_FILTER
    assert ((size % 2) == 0);
    unsigned short* outBuffer2 = (unsigned short*)outBuffer;
    size_t halfSize = size / 2;
    size_t i        = 0;
    while (i < halfSize)
    {
        unsigned short nv       = src[i] | (src[i + halfSize] << 8);
        int p        = Predict (outBuffer2, i, rowStride/2);
        int v        = nv + p;
        outBuffer2[i] = v;
        ++i;
    }
#else
    size_t halfSize = size / 2;
    size_t i        = 0;
    while (i < size)
    {
        int p        = Predict (outBuffer, i, rowStride);
        int v        = src[i / 2] + p;
        outBuffer[i] = v;
        ++i;
        p            = Predict (outBuffer, i, rowStride);
        v            = src[i / 2 + halfSize] + p;
        outBuffer[i] = v;
        ++i;
    }
#endif
}


ZstdCompressor::ZstdCompressor
    (const Header &hdr,
     size_t maxScanLineSize,
     size_t numScanLines)
:
    Compressor (hdr),
    _numScanLines (numScanLines),
    _tmpBuffer(0),
    _outBuffer (0),
    _outBufferCapacity (0),
    _cmpLevel(ZSTD_defaultCLevel())
{
    const auto& dw = hdr.dataWindow ();
    _rowStride         = dw.max.x - dw.min.x + 1; //@TODO: sampling
    _rowStride *= 8; //@TODO: support arbitrary formats not just RGBA Half
    _tmpBufferCapacity = uiMult (maxScanLineSize, numScanLines);
    _tmpBuffer = new char[_tmpBufferCapacity];

    _outBufferCapacity = ZSTD_compressBound(_tmpBufferCapacity);
    _outBuffer = new char[_outBufferCapacity];
    if (hasZstdCompressionLevel (hdr))
    {
        _cmpLevel = zstdCompressionLevel (hdr);
        if (_cmpLevel > ZSTD_maxCLevel()) _cmpLevel = ZSTD_maxCLevel();
        if (_cmpLevel < ZSTD_minCLevel()) _cmpLevel = ZSTD_minCLevel();
    }
}

ZstdCompressor::~ZstdCompressor ()
{
    delete [] _outBuffer;
    delete [] _tmpBuffer;
}

int
ZstdCompressor::numScanLines () const
{
    return _numScanLines;
}

int
ZstdCompressor::compress (const char *inPtr,
			 int inSize,
			 int minY,
			 const char *&outPtr)
{
    // Special case: empty input buffer
    if (inSize == 0)
    {
        outPtr = _outBuffer;
        return 0;
    }
    
    MyFilterBeforeCompression ((const unsigned char*)inPtr, inSize, _rowStride, (unsigned char*)_tmpBuffer);
    
    size_t cmpSize = ZSTD_compress(_outBuffer, _outBufferCapacity, _tmpBuffer, inSize, _cmpLevel);
    
    outPtr = _outBuffer;
    return (int)cmpSize;
}


int
ZstdCompressor::uncompress (const char *inPtr,
			   int inSize,
			   int minY,
			   const char *&outPtr)
{
    // Special case - empty input buffer
    if (inSize == 0)
    {
        outPtr = _outBuffer;
        return 0;
    }

    size_t resSize = ZSTD_decompress(_tmpBuffer, _tmpBufferCapacity, inPtr, inSize);
    if (resSize == 0)
        return 0;
    
    MyUnfilterAfterDecompression((unsigned char*)_tmpBuffer, resSize, _rowStride, (unsigned char*)_outBuffer);

    outPtr = _outBuffer;
    return (int)resSize;
}


OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_EXIT
