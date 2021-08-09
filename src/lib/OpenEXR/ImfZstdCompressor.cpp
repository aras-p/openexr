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

OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_ENTER


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
    
    FilterBeforeCompression(inPtr, inSize, _tmpBuffer);
    
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
    
    UnfilterAfterDecompression(_tmpBuffer, resSize, _outBuffer);

    outPtr = _outBuffer;
    return (int)resSize;
}


OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_EXIT
