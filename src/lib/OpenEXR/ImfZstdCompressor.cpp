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

//#define USE_W_PREDICTOR 1 // otherwise ClampedGrad
//#define USE_STREAM_PER_BYTE 1

static int
Predict (
    const unsigned char* src, size_t idx, size_t colStride, size_t rowStride)
{
    #if USE_W_PREDICTOR
    if (idx < colStride) return 0;
    return src[idx - colStride];
    #else
    //if (idx < rowStride + colStride) return 0;
    //size_t idxN  = idx - rowStride;
    //size_t idxW  = idx - colStride;
    //size_t idxNW = idx - rowStride - colStride;

    if (idx < colStride) return 0;
    size_t idxN  = idx - (idx >= rowStride ? rowStride : colStride);
    size_t idxW = idx - (idx >= colStride ? colStride : colStride);
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
    assert (grad >= 0 && grad <= 255);
    return grad;
    #endif
}

static void
MyFilterBeforeCompression (
    const unsigned char* raw,
    size_t               size,
    size_t               colStride,
    size_t               rowStride,
    unsigned char*       outBuffer)
{
    #if 0
    FilterBeforeCompression(raw, size, outBuffer);
    #elif USE_STREAM_PER_BYTE
    assert ((size % colStride) == 0);
    size_t streamSize = size / colStride;
    size_t idx        = 0;
    for (size_t i = 0; i < streamSize; ++i) {
        for (size_t c = 0; c < colStride; ++c) {
            int v            = raw[idx];
            int p            = Predict (raw, idx, colStride, rowStride);
            outBuffer[c * streamSize + i] = v - p;
            ++idx;
        }
    }
    #else
    assert ((size % 2) == 0);
    size_t halfSize = size / 2;
    size_t i        = 0;
    while (i < size)
    {
        int v            = raw[i];
        int p            = Predict (raw, i, colStride, rowStride);
        outBuffer[i / 2] = v - p;
        ++i;
        v                = raw[i];
        p                = Predict (raw, i, colStride, rowStride);
        outBuffer[i / 2 + halfSize] = v - p;
        ++i;
    }
    #endif
}

static void
MyUnfilterAfterDecompression (
    unsigned char* src,
    size_t         size,
    size_t         colStride,
    size_t         rowStride,
    unsigned char* outBuffer)
{
    #if 0
    UnfilterAfterDecompression(tmpBuffer, size, outBuffer);
#elif USE_STREAM_PER_BYTE
    assert ((size % colStride) == 0);
    size_t streamSize = size / colStride;
    size_t idx        = 0;
    for (size_t i = 0; i < streamSize; ++i)
    {
        for (size_t c = 0; c < colStride; ++c)
        {
            int p = Predict (outBuffer, idx, colStride, rowStride);
            int v = src[c * streamSize + i] + p;
            outBuffer[idx] = v;
            ++idx;
        }
    }
    #else
    assert ((size % 2) == 0);
    size_t halfSize = size / 2;
    size_t i        = 0;
    while (i < size) {
        int p        = Predict (outBuffer, i, colStride, rowStride);
        int v        = src[i / 2] + p;
        outBuffer[i] = v;
        ++i;
        p            = Predict (outBuffer, i, colStride, rowStride);
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
    
    MyFilterBeforeCompression ((const unsigned char*)inPtr, inSize, 8, _rowStride, (unsigned char*)_tmpBuffer);
    
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
    
    MyUnfilterAfterDecompression((unsigned char*)_tmpBuffer, resSize, 8, _rowStride, (unsigned char*)_outBuffer);

    outPtr = _outBuffer;
    return (int)resSize;
}


OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_EXIT
