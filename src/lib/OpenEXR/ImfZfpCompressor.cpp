//
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Contributors to the OpenEXR Project.
//


#include "ImfZfpCompressor.h"
#include "ImfCheckedArithmetic.h"
#include "ImfStandardAttributes.h"
#include "Iex.h"
#include "ImfNamespace.h"
#include <assert.h>
#include "zstd/zstd.h"

OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_ENTER


ZfpCompressor::ZfpCompressor (
    const Header& hdr, size_t maxScanLineSize, size_t numScanLines)
    : Compressor (hdr)
    , _numScanLines (numScanLines)
    , _floatBuffer (0)
    , _floatBufferCapacity (0)
    , _decompBuffer (0)
    , _decompBufferCapacity (0)
    , _zfpBuffer (0)
    , _zfpBufferCapacity (0)
    , _zstdBuffer (0)
    , _zstdBufferCapacity (0)
    , _cmpLevel (1)
{
    const auto& dw    = hdr.dataWindow ();
    int         width = dw.max.x - dw.min.x + 1; //@TODO: sampling
    _rowStride =
        width * 8; //@TODO: support arbitrary formats not just RGBA Half
    _floatBufferCapacity =
        uiMult (size_t (width) * 4, numScanLines); // will expand FP16 to FP32
    _floatBuffer = new float[_floatBufferCapacity];

    memset (&_zfpStream, 0, sizeof (_zfpStream));
    zfp_stream_set_reversible (&_zfpStream);
    zfp_stream_set_execution (&_zfpStream, zfp_exec_serial);

    memset (&_zfpField, 0, sizeof (_zfpField));
    _zfpField.type = zfp_type_float;
    _zfpField.data = _floatBuffer;
    _zfpField.nx   = width * 4; //@TODO: not just 4 channel
    _zfpField.ny   = numScanLines;

    _zfpBufferCapacity = zfp_stream_maximum_size (&_zfpStream, &_zfpField);
    _zfpBuffer         = new char[_zfpBufferCapacity];

    _zstdBufferCapacity = ZSTD_compressBound (_zfpBufferCapacity);
    _zstdBuffer = new char[_zstdBufferCapacity];
    if (hasZstdCompressionLevel (hdr))
    {
        _cmpLevel = zstdCompressionLevel (hdr);
        if (_cmpLevel > ZSTD_maxCLevel ()) _cmpLevel = ZSTD_maxCLevel ();
        if (_cmpLevel < ZSTD_minCLevel ()) _cmpLevel = ZSTD_minCLevel ();
    }

    _decompBufferCapacity = _rowStride * numScanLines;
    _decompBuffer         = new char[_decompBufferCapacity];
}

ZfpCompressor::~ZfpCompressor ()
{
    delete[] _zstdBuffer;
    delete[] _zfpBuffer;
    delete[] _floatBuffer;
    delete[] _decompBuffer;
}

int
ZfpCompressor::numScanLines () const
{
    return _numScanLines;
}

int
ZfpCompressor::compress (
    const char* inPtr,
			 int inSize,
			 int minY,
			 const char *&outPtr)
{
    // Special case: empty input buffer
    if (inSize == 0)
    {
        outPtr = _zfpBuffer;
        return 0;
    }

    assert ((inSize % 2) == 0);
    assert (inSize / 2 <= _floatBufferCapacity);
    _zfpField.ny = inSize / 2 / _zfpField.nx;

    const uint16_t* inPtr2 = (const uint16_t*) inPtr;
    for (int i = 0; i < inSize / 2; ++i)
        _floatBuffer[i] = imath_half_to_float (inPtr2[i]);

    bitstream* bs = stream_open (_zfpBuffer, _zfpBufferCapacity);
    zfp_stream_set_bit_stream (&_zfpStream, bs);
    zfp_write_header (&_zfpStream, &_zfpField, ZFP_HEADER_FULL);
    size_t zfpSize = zfp_compress (&_zfpStream, &_zfpField);
    stream_close (bs);

    size_t zstdSize = ZSTD_compress (_zstdBuffer, _zstdBufferCapacity, _zfpBuffer, zfpSize, _cmpLevel);

    outPtr = _zstdBuffer;
    return (int)zstdSize;
}


int
ZfpCompressor::uncompress (
    const char* inPtr,
			   int inSize,
			   int minY,
			   const char *&outPtr)
{
    // Special case - empty input buffer
    if (inSize == 0)
    {
        outPtr = _decompBuffer;
        return 0;
    }

    size_t zfpSize = ZSTD_decompress (_zfpBuffer, _zfpBufferCapacity, inPtr, inSize);
    if (zfpSize == 0) {
        outPtr = _decompBuffer;
        return 0;
    }

    bitstream* bs = stream_open (_zfpBuffer, zfpSize);
    zfp_stream_set_bit_stream (&_zfpStream, bs);
    zfp_read_header (&_zfpStream, &_zfpField, ZFP_HEADER_FULL);
    size_t cmpSize = zfp_decompress (&_zfpStream, &_zfpField);
    stream_close (bs);
    if (cmpSize == 0) return 0;

    size_t    nvals   = _zfpField.nx * _zfpField.ny;
    uint16_t* outPtr2 = (uint16_t*)_decompBuffer;
    for (int i = 0; i < nvals; ++i)
        outPtr2[i] = imath_float_to_half (_floatBuffer[i]);
    outPtr = _decompBuffer;
    return nvals * 2;
}


OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_EXIT
