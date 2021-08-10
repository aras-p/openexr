//
// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Contributors to the OpenEXR Project.
//

#include "ImfZip.h"
#include "ImfCheckedArithmetic.h"
#include "ImfNamespace.h"
#include "ImfSimd.h"
#include "Iex.h"

#include <math.h>

//#define USE_LIBDEFLATE_COMPRESS
//#define USE_LIBDEFLATE_DECOMPRESS


#include <zlib.h>
#include "libdeflate/libdeflate.h"

OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_ENTER

Zip::Zip(size_t maxRawSize):
    _maxRawSize(maxRawSize),
    _tmpBuffer(0)
{
    _tmpBuffer = new char[_maxRawSize];
}

Zip::Zip(size_t maxScanLineSize, size_t numScanLines):
    _maxRawSize(0),
    _tmpBuffer(0)
{
    _maxRawSize = uiMult (maxScanLineSize, numScanLines);
    _tmpBuffer  = new char[_maxRawSize];
}

Zip::~Zip()
{
    if (_tmpBuffer) delete[] _tmpBuffer;
}

size_t
Zip::maxRawSize()
{
    return _maxRawSize;
}

size_t
Zip::maxCompressedSize()
{
    return uiAdd (uiAdd (_maxRawSize,
               size_t (ceil (_maxRawSize * 0.01))),
                  size_t (100));
}

void FilterBeforeCompression(const char* raw, size_t rawSize, char* outBuffer)
{
    // De-interleave the data and do a delta predictor
    unsigned char *t1 = (unsigned char *) outBuffer;
    unsigned char *t2 = (unsigned char *) outBuffer + (rawSize + 1) / 2;
    const unsigned char *r = (const unsigned char *) raw;
    const unsigned char *stop = r + rawSize;
    
    int p1 = 128;
    int p2 = r[(rawSize - 1)&~1];
    
    while (true)
    {
        if (r < stop)
        {
            int v = int(*(r++));
            int d = v - p1 + (128 + 256);
            p1 = v;
            *(t1++) = d;
        }
        else
            break;

        if (r < stop)
        {
            int v = int(*(r++));
            int d = v - p2 + (128 + 256);
            p2 = v;
            *(t2++) = d;
        }
        else
            break;
    }
}

int
Zip::compress(const char *raw, int rawSize, char *compressed, int level)
{
    FilterBeforeCompression(raw, rawSize, _tmpBuffer);

    //
    // Compress the data using zlib
    //

    uLongf outSize = int(ceil(rawSize * 1.01)) + 100;

    #ifdef USE_LIBDEFLATE_COMPRESS
    libdeflate_compressor* cmp = libdeflate_alloc_compressor(level);
    size_t cmpBytes = libdeflate_zlib_compress(cmp, _tmpBuffer, rawSize, compressed, outSize);
    if (cmpBytes == 0)
    {
        throw IEX_NAMESPACE::BaseExc ("Data compression (libdeflate) failed.");
    }
    outSize = cmpBytes;
    libdeflate_free_compressor(cmp);
    #else
    if (Z_OK != ::compress2 ((Bytef *)compressed, &outSize,
                (const Bytef *) _tmpBuffer, rawSize, level))
    {
        throw IEX_NAMESPACE::BaseExc ("Data compression (zlib) failed.");
    }
    #endif

    return outSize;
}

#ifdef IMF_HAVE_SSE4_1

static void
reconstruct_sse41(char *buf, size_t outSize)
{
    static const size_t bytesPerChunk = sizeof(__m128i);
    const size_t vOutSize = outSize / bytesPerChunk;

    const __m128i c = _mm_set1_epi8(-128);
    const __m128i shuffleMask = _mm_set1_epi8(15);

    // The first element doesn't have its high bit flipped during compression,
    // so it must not be flipped here.  To make the SIMD loop nice and
    // uniform, we pre-flip the bit so that the loop will unflip it again.
    buf[0] += -128;

    __m128i *vBuf = reinterpret_cast<__m128i *>(buf);
    __m128i vPrev = _mm_setzero_si128();
    for (size_t i=0; i<vOutSize; ++i)
    {
        __m128i d = _mm_add_epi8(_mm_loadu_si128(vBuf), c);

        // Compute the prefix sum of elements.
        d = _mm_add_epi8(d, _mm_slli_si128(d, 1));
        d = _mm_add_epi8(d, _mm_slli_si128(d, 2));
        d = _mm_add_epi8(d, _mm_slli_si128(d, 4));
        d = _mm_add_epi8(d, _mm_slli_si128(d, 8));
        d = _mm_add_epi8(d, vPrev);

        _mm_storeu_si128(vBuf++, d);

        // Broadcast the high byte in our result to all lanes of the prev
        // value for the next iteration.
        vPrev = _mm_shuffle_epi8(d, shuffleMask);
    }

    unsigned char prev = _mm_extract_epi8(vPrev, 15);
    for (size_t i=vOutSize*bytesPerChunk; i<outSize; ++i)
    {
        unsigned char d = prev + buf[i] - 128;
        buf[i] = d;
        prev = d;
    }
}

#else

static void
reconstruct_scalar(char *buf, size_t outSize)
{
    unsigned char *t    = (unsigned char *) buf + 1;
    unsigned char *stop = (unsigned char *) buf + outSize;

    while (t < stop)
    {
        int d = int (t[-1]) + int (t[0]) - 128;
        t[0] = d;
        ++t;
    }
}

#endif


#ifdef IMF_HAVE_SSE2

static void
interleave_sse2(const char *source, size_t outSize, char *out)
{
    static const size_t bytesPerChunk = 2*sizeof(__m128i);

    const size_t vOutSize = outSize / bytesPerChunk;

    const __m128i *v1 = reinterpret_cast<const __m128i *>(source);
    const __m128i *v2 = reinterpret_cast<const __m128i *>(source + (outSize + 1) / 2);
    __m128i *vOut = reinterpret_cast<__m128i *>(out);

    for (size_t i=0; i<vOutSize; ++i) {
        __m128i a = _mm_loadu_si128(v1++);
        __m128i b = _mm_loadu_si128(v2++);

        __m128i lo = _mm_unpacklo_epi8(a, b);
        __m128i hi = _mm_unpackhi_epi8(a, b);

        _mm_storeu_si128(vOut++, lo);
        _mm_storeu_si128(vOut++, hi);
    }

    const char *t1 = reinterpret_cast<const char *>(v1);
    const char *t2 = reinterpret_cast<const char *>(v2);
    char *sOut = reinterpret_cast<char *>(vOut);

    for (size_t i=vOutSize*bytesPerChunk; i<outSize; ++i)
    {
        *(sOut++) = (i%2==0) ? *(t1++) : *(t2++);
    }
}

#else

static void
interleave_scalar(const char *source, size_t outSize, char *out)
{
    const char *t1 = source;
    const char *t2 = source + (outSize + 1) / 2;
    char *s = out;
    char *const stop = s + outSize;

    while (true)
    {
        if (s < stop)
            *(s++) = *(t1++);
        else
            break;

        if (s < stop)
            *(s++) = *(t2++);
        else
            break;
    }
}

#endif

void UnfilterAfterDecompression(char* tmpBuffer, size_t size, char* outBuffer)
{
    //
    // Predictor.
    //
#ifdef IMF_HAVE_SSE4_1
    reconstruct_sse41(tmpBuffer, size);
#else
    reconstruct_scalar(tmpBuffer, size);
#endif

    //
    // Reorder the pixel data.
    //
#ifdef IMF_HAVE_SSE2
    interleave_sse2(tmpBuffer, size, outBuffer);
#else
    interleave_scalar(tmpBuffer, size, outBuffer);
#endif    
}


int
Zip::uncompress(const char *compressed, int compressedSize,
                char *raw)
{
    //
    // Decompress the data using zlib
    //

    uLongf outSize = _maxRawSize;

#ifdef USE_LIBDEFLATE_DECOMPRESS
    libdeflate_decompressor* cmp = libdeflate_alloc_decompressor();
    size_t cmpBytes = 0;
    libdeflate_result cmpRes = libdeflate_zlib_decompress(cmp, compressed, compressedSize, _tmpBuffer, _maxRawSize, &cmpBytes);
    if (cmpRes != LIBDEFLATE_SUCCESS)
    {
        throw IEX_NAMESPACE::InputExc ("Data decompression (libdeflate) failed.");
    }
    outSize = cmpBytes;
    libdeflate_free_decompressor(cmp);
#else
    if (Z_OK != ::uncompress ((Bytef *)_tmpBuffer, &outSize,
                     (const Bytef *) compressed, compressedSize))
    {
        throw IEX_NAMESPACE::InputExc ("Data decompression (zlib) failed.");
    }
#endif

    if (outSize == 0)
    {
        return outSize;
    }
    
    UnfilterAfterDecompression(_tmpBuffer, outSize, raw);
    return outSize;
}

OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_EXIT
