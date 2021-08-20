//
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Contributors to the OpenEXR Project.
//


#ifndef INCLUDED_IMF_ZFP_COMPRESSOR_H
#define INCLUDED_IMF_ZFP_COMPRESSOR_H

#include "ImfNamespace.h"
#include "ImfCompressor.h"
#include <zfp.h>

OPENEXR_IMF_INTERNAL_NAMESPACE_HEADER_ENTER


class ZfpCompressor: public Compressor
{
  public:

    ZfpCompressor (
          const Header& hdr, 
                   size_t maxScanLineSize,
                   size_t numScanLines);

    virtual ~ZfpCompressor ();

    virtual int numScanLines () const;

    virtual int	compress (const char *inPtr,
			  int inSize,
			  int minY,
			  const char *&outPtr);

    virtual int	uncompress (const char *inPtr,
			    int inSize,
			    int minY,
			    const char *&outPtr);
  private:

    int		_numScanLines;
    float*      _floatBuffer;
    size_t      _floatBufferCapacity;
    char*	    _zfpBuffer;
    size_t      _zfpBufferCapacity;
    char*           _zstdBuffer;
    size_t          _zstdBufferCapacity;
    char*       _decompBuffer;
    size_t      _decompBufferCapacity;
    size_t _rowStride;
    zfp_stream  _zfpStream;
    zfp_field   _zfpField;
    int             _cmpLevel;
};

OPENEXR_IMF_INTERNAL_NAMESPACE_HEADER_EXIT

#endif
