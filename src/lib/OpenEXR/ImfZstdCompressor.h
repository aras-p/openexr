//
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Contributors to the OpenEXR Project.
//


#ifndef INCLUDED_IMF_ZSTD_COMPRESSOR_H
#define INCLUDED_IMF_ZSTD_COMPRESSOR_H

#include "ImfNamespace.h"
#include "ImfCompressor.h"

OPENEXR_IMF_INTERNAL_NAMESPACE_HEADER_ENTER


class ZstdCompressor: public Compressor
{
  public:

    ZstdCompressor (const Header &hdr, 
                   size_t maxScanLineSize,
                   size_t numScanLines);

    virtual ~ZstdCompressor ();

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
    char* _tmpBuffer;
    size_t _tmpBufferCapacity;
    char*	_outBuffer;
    size_t _outBufferCapacity;
    int _cmpLevel;
};

OPENEXR_IMF_INTERNAL_NAMESPACE_HEADER_EXIT

#endif
