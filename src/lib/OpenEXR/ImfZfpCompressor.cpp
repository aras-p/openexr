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

OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_ENTER


ZfpCompressor::ZfpCompressor (
    const Header& hdr, size_t maxScanLineSize, size_t numScanLines)
    : Compressor (hdr)
    , _numScanLines (numScanLines)
    , _tmpBuffer (0)
    , _outBuffer (0)
    , _outBufferCapacity (0)
{
    const auto& dw    = hdr.dataWindow ();
    int         width = dw.max.x - dw.min.x + 1; //@TODO: sampling
    _rowStride =
        width * 8; //@TODO: support arbitrary formats not just RGBA Half
    _tmpBufferCapacity =
        uiMult (size_t (width) * 4, numScanLines); // will expand FP16 to FP32
    _tmpBuffer = new float[_tmpBufferCapacity];

    memset (&_zfpStream, 0, sizeof (_zfpStream));
    zfp_stream_set_reversible (&_zfpStream);
    zfp_stream_set_execution (&_zfpStream, zfp_exec_serial);

    memset (&_zfpField, 0, sizeof (_zfpField));
    _zfpField.type = zfp_type_float;
    _zfpField.data = _tmpBuffer;
    _zfpField.nx   = width * 4; //@TODO: not just 4 channel
    _zfpField.ny   = numScanLines;

    _outBufferCapacity = zfp_stream_maximum_size (&_zfpStream, &_zfpField);
    _outBuffer         = new char[_outBufferCapacity];

    _decompBufferCapacity = _rowStride * numScanLines;
    _decompBuffer         = new char[_decompBufferCapacity];
}

ZfpCompressor::~ZfpCompressor ()
{
    delete [] _outBuffer;
    delete [] _tmpBuffer;
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
        outPtr = _outBuffer;
        return 0;
    }

    assert ((inSize % 2) == 0);
    assert (inSize / 2 <= _tmpBufferCapacity);
    _zfpField.ny = inSize / 2 / _zfpField.nx;

    const uint16_t* inPtr2 = (const uint16_t*) inPtr;
    for (int i = 0; i < inSize / 2; ++i)
        _tmpBuffer[i] = imath_half_to_float (inPtr2[i]);

    bitstream* bs = stream_open (_outBuffer, _outBufferCapacity);
    zfp_stream_set_bit_stream (&_zfpStream, bs);
    zfp_write_header (&_zfpStream, &_zfpField, ZFP_HEADER_FULL);
    size_t cmpSize = zfp_compress (&_zfpStream, &_zfpField);
    stream_close (bs);

    outPtr = _outBuffer;
    return (int)cmpSize;
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

    bitstream* bs = stream_open ((void*)inPtr, inSize);
    zfp_stream_set_bit_stream (&_zfpStream, bs);
    zfp_read_header (&_zfpStream, &_zfpField, ZFP_HEADER_FULL);
    size_t cmpSize = zfp_decompress (&_zfpStream, &_zfpField);
    stream_close (bs);
    if (cmpSize == 0) return 0;
    assert (cmpSize == inSize);

    size_t    nvals   = _zfpField.nx * _zfpField.ny;
    uint16_t* outPtr2 = (uint16_t*)_decompBuffer;
    for (int i = 0; i < nvals; ++i)
        outPtr2[i] = imath_float_to_half (_tmpBuffer[i]);
    outPtr = _decompBuffer;
    return nvals * 2;
}


OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_EXIT
