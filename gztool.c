//
// gztool
//
// Create small indexes for gzipped files and use them
// for quick and random data extraction.
// No more waiting when the end of a 10 GiB gzip is needed!
//
// based on parts from
// zlib/examples/zran.c & zlib/examples/zpipe.c by Mark Adler
// //github.com/madler/zlib
//
//.................................................
//
// LICENSE:
//
// v0.1 to v0.11* by Roberto S. Galende, 2019, 2020
// //github.com/circulosmeos/gztool
// A work by Roberto S. Galende 
// distributed under the same License terms covering
// zlib from Mark Adler (aka Zlib license):
//   This software is provided 'as-is', without any express or implied
//   warranty.  In no event will the authors be held liable for any damages
//   arising from the use of this software.
//   Permission is granted to anyone to use this software for any purpose,
//   including commercial applications, and to alter it and redistribute it
//   freely, subject to the following restrictions:
//   1. The origin of this software must not be misrepresented; you must not
//      claim that you wrote the original software. If you use this software
//      in a product, an acknowledgment in the product documentation would be
//      appreciated but is not required.
//   2. Altered source versions must be plainly marked as such, and must not be
//      misrepresented as being the original software.
//   3. This notice may not be removed or altered from any source distribution.
//
// License contained in zlib.h by Mark Adler is copied here:
//
/* zlib.h -- interface of the 'zlib' general purpose compression library
  version 1.2.11, January 15th, 2017
  Copyright (C) 1995-2017 Jean-loup Gailly and Mark Adler
  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.
  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:
  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu
  The data format used by the zlib library is described by RFCs (Request for
  Comments) 1950 to 1952 in the files http://tools.ietf.org/html/rfc1950
  (zlib format), rfc1951 (deflate format) and rfc1952 (gzip format).
*/
//.................................................
//
// Original zran.c notice is copied here:
//
    /* zran.c -- example of zlib/gzip stream indexing and random access
     * Copyright (C) 2005, 2012 Mark Adler
     * For conditions of distribution and use, see copyright notice in zlib.h
       Version 1.1  29 Sep 2012  Mark Adler */

    /* Version History:
     1.0  29 May 2005  First version
     1.1  29 Sep 2012  Fix memory reallocation error
     */

    /* Illustrate the use of Z_BLOCK, inflatePrime(), and inflateSetDictionary()
       for random access of a compressed file.  A file containing a zlib or gzip
       stream is provided on the command line.  The compressed stream is decoded in
       its entirety, and an index built with access points about every SPAN bytes
       in the uncompressed output.  The compressed file is left open, and can then
       be read randomly, having to decompress on the average SPAN/2 uncompressed
       bytes before getting to the desired block of data.
       An access point can be created at the start of any deflate block, by saving
       the starting file offset and bit of that block, and the 32K bytes of
       uncompressed data that precede that block.  Also the uncompressed offset of
       that block is saved to provide a referece for locating a desired starting
       point in the uncompressed stream.  build_index() works by decompressing the
       input zlib or gzip stream a block at a time, and at the end of each block
       deciding if enough uncompressed data has gone by to justify the creation of
       a new access point.  If so, that point is saved in a data structure that
       grows as needed to accommodate the points.
       To use the index, an offset in the uncompressed data is provided, for which
       the latest access point at or preceding that offset is located in the index.
       The input file is positioned to the specified location in the index, and if
       necessary the first few bits of the compressed data is read from the file.
       inflate is initialized with those bits and the 32K of uncompressed data, and
       the decompression then proceeds until the desired offset in the file is
       reached.  Then the decompression continues to read the desired uncompressed
       data from the file.
       Another approach would be to generate the index on demand.  In that case,
       requests for random access reads from the compressed data would try to use
       the index, but if a read far enough past the end of the index is required,
       then further index entries would be generated and added.
       There is some fair bit of overhead to starting inflation for the random
       access, mainly copying the 32K byte dictionary.  So if small pieces of the
       file are being accessed, it would make sense to implement a cache to hold
       some lookahead and avoid many calls to extract() for small lengths.
       Another way to build an index would be to use inflateCopy().  That would
       not be constrained to have access points at block boundaries, but requires
       more memory per access point, and also cannot be saved to file due to the
       use of pointers in the state.  The approach here allows for storage of the
       index in a file.
     */
//

// .................................................
// large file support (LFS) (files with size >2^31 (2 GiB) in linux, and >4 GiB in Windows)
#define _FILE_OFFSET_BITS 64    // defining _FILE_OFFSET_BITS with the value 64 (before including
                                // any header files) will turn off_t into a 64-bit type
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE     // off64_t for fseek64
// .................................................

//#define COMPILING_DEBIAN_PACKAGE
#ifdef COMPILING_DEBIAN_PACKAGE
    #include <config.h>
#else
    #define PACKAGE_NAME "gztool"
    #define PACKAGE_VERSION "0.11.5"
#endif

#include <stdint.h> // uint32_t, uint64_t, UINT32_MAX
#include <stdio.h>
#include <stdarg.h> // va_start, va_list, va_end
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>
#include <unistd.h> // getopt(), access(), sleep()
#include <ctype.h>  // isprint()
#include <sys/stat.h> // stat()
#include <math.h>   // pow()

// sets binary mode for stdin in Windows
#define STDIN 0
#define STDOUT 1
#ifdef _WIN32
# include <io.h>
# include <fcntl.h>
# define SET_BINARY_MODE(handle) setmode(handle, O_BINARY)
#else
# define SET_BINARY_MODE(handle) ((void)0)
#endif

#define local static

#define SPAN 10485760L      /* desired distance between access points */
#define WINSIZE 32768U      /* sliding window size */
#define CHUNK 16384         /* file input buffer size */
#define UNCOMPRESSED_WINDOW UINT32_MAX // window is an uncompressed WINSIZE size window
#define GZIP_INDEX_IDENTIFIER_STRING    "gzipindx"  // default index version (v0)
#define GZIP_INDEX_IDENTIFIER_STRING_V1 "gzipindX"  // index version with line number info
#define GZIP_INDEX_HEADER_SIZE   16  // header size in bytes of gztool's .gzi files
#define GZIP_HEADER_SIZE_BY_ZLIB 10  // header size in bytes of gzip files created by zlib:
        //github.com/madler/zlib/blob/master/zlib.h
        //     If deflateSetHeader is not used, the default gzip header has text false,
        //   the time set to zero, and os set to 255, with no extra, name, or comment
        //   fields.
// default waiting time in seconds when supervising a growing gzip file:
#define WAITING_TIME 4

/* access point entry */
struct point {
    uint64_t out;             /* corresponding offset in uncompressed data */
    uint64_t in;              /* offset in input file of first full byte */
    uint32_t bits;         /* number of bits (1-7) from byte at in - 1, or 0 */
    uint64_t window_beginning;/* offset at index file where this compressed window is stored */
    uint32_t window_size;  /* size of (compressed) window */
    unsigned char *window; /* preceding 32K of uncompressed data, compressed */
    // index v1:
    uint64_t line_number;  /* if index_version == 1 stores line number at this index point */
};
// NOTE: window_beginning is not stored on disk, it's an on-memory-only value

/* access point list */
struct access {
    uint64_t have;      /* number of list entries filled in */
    uint64_t size;      /* number of list entries allocated */
    uint64_t file_size; /* size of uncompressed file (useful for bgzip files) */
    struct point *list; /* allocated list */
    char *file_name; /* path to index file */
    int index_complete;     /* 1: index is complete; 0: index is (still) incomplete */
    // index v1:
    int index_version;      /* 0: default; 1: index with line numbers */
    uint32_t line_number_format; /* 0: linux \r | windows \n\r; 1: mac \n */
    uint64_t number_of_lines; /* number of lines (only used with v1 index format) */
};
// NOTE: file_name, index_complete and index_version are not stored on disk (on-memory-only values)

/* generic struct to return a function error code and a value */
struct returned_output {
    uint64_t value;
    int error;
};

enum EXIT_APP_VALUES { EXIT_OK = 0, EXIT_GENERIC_ERROR = 1, EXIT_INVALID_OPTION = 2 };

enum INDEX_AND_EXTRACTION_OPTIONS {
    JUST_CREATE_INDEX, SUPERVISE_DO,
    SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, EXTRACT_FROM_BYTE,
    EXTRACT_TAIL, COMPRESS_AND_CREATE_INDEX, DECOMPRESS,
    EXTRACT_FROM_LINE };

enum ACTION
    { ACT_NOT_SET, ACT_EXTRACT_FROM_BYTE, ACT_COMPRESS_CHUNK, ACT_DECOMPRESS_CHUNK,
      ACT_CREATE_INDEX, ACT_LIST_INFO, ACT_HELP, ACT_SUPERVISE, ACT_EXTRACT_TAIL,
      ACT_EXTRACT_TAIL_AND_CONTINUE, ACT_COMPRESS_AND_CREATE_INDEX, ACT_DECOMPRESS,
      ACT_EXTRACT_FROM_LINE };

enum VERBOSITY_LEVEL { VERBOSITY_NONE = 0, VERBOSITY_NORMAL = 1, VERBOSITY_EXCESSIVE = 2,
                       VERBOSITY_MANIAC = 3, VERBOSITY_CRAZY = 4, VERBOSITY_NUTS = 5 };

enum VERBOSITY_LEVEL verbosity_level = VERBOSITY_NORMAL;

// shared string for printing unit conversions without dealing with pointers:
// Always %.2f : "1234.67 Bytes\0" = 14 chars maximum
#define MAX_giveMeSIUnits_RETURNED_LENGTH 14
char number_output[MAX_giveMeSIUnits_RETURNED_LENGTH];

// `fprintf` substitute for printing with VERBOSITY_LEVEL
void printToStderr ( enum VERBOSITY_LEVEL verbosity, const char * format, ... ) {

    // if verbosity of message is above general verbosity_level, ignore message
    if ( verbosity <= verbosity_level ) {
        va_list args;
        va_start (args, format);
        vfprintf ( stderr, format, args );
        va_end (args);
    }

}

// Convert an integer to a short string (2 decimal places) using SI units.
// Please, note that this function uses always "number_output" global variable as output.
// INPUT:
// uint64_t input_number        : number to convert to binary SI units
// int binary                   : 0: decimal suffixes, 1: binary suffixes
// OUTPUT:
// pointer to buffer where string will be written:
// It is ALWAYS the "number_output" global variable.
char *giveMeSIUnits ( uint64_t input_number, int binary ) {

    int i;
    double number = (double)input_number;
    double BASE = ( (binary==1)? 1024.0: 1000.0 );

    char *SI_UNITS[7] =
        { "", "k", "M", "G", "T", "P", "E" };
    char *SI_BINARY_UNITS[7] =
        { "Bytes", "kiB", "MiB", "GiB", "TiB", "PiB", "EiB" };

    // totally empty string so that
    // end-of-string char is always correctly set no matter the number
    for ( i = 0; i < MAX_giveMeSIUnits_RETURNED_LENGTH; i++ )
        number_output[i] = '\0';

    for ( i = 0; number >= BASE; i++ )
        number /= BASE;

    snprintf( number_output, MAX_giveMeSIUnits_RETURNED_LENGTH,
        "%.2f %s", number, ( (binary==1)? SI_BINARY_UNITS[i]: SI_UNITS[i] ) );

    return number_output;

}


/**************
 * Endianness *
 **************/
static inline int endiannes_is_big(void)
{
    long one= 1;
    return !(*((char *)(&one)));
}
static inline uint32_t endiannes_swap_4(uint32_t v)
{
    v = ((v & 0x0000FFFFU) << 16) | (v >> 16);
    return ((v & 0x00FF00FFU) << 8) | ((v & 0xFF00FF00U) >> 8);
}
static inline void *endiannes_swap_4p(void *x)
{
    *(uint32_t*)x = endiannes_swap_4(*(uint32_t*)x);
    return x;
}
static inline uint64_t endiannes_swap_8(uint64_t v)
{
    v = ((v & 0x00000000FFFFFFFFLLU) << 32) | (v >> 32);
    v = ((v & 0x0000FFFF0000FFFFLLU) << 16) | ((v & 0xFFFF0000FFFF0000LLU) >> 16);
    return ((v & 0x00FF00FF00FF00FFLLU) << 8) | ((v & 0xFF00FF00FF00FF00LLU) >> 8);
}
static inline void *endiannes_swap_8p(void *x)
{
    *(uint64_t*)x = endiannes_swap_8(*(uint64_t*)x);
    return x;
}

// fread() substitute for endianness management of 4 and 8 bytes words
// Data in *ptr will be received as this target system waits them
// independently of how they were stored (always as big endian).
// INPUT:
// void * ptr   : pointer to buffer where data will be received
// size_t size  : size of data in bytes
// FILE * stream: stream to read
// OUTPUT:
// number of bytes read
local size_t fread_endian( void * ptr, size_t size, FILE * stream ) {

    size_t output = fread(ptr, size, 1, stream);

    if (endiannes_is_big()) {
        ;
    } else {
        // machine is little endian: flip bytes
        switch (size) {
            case 4:
                endiannes_swap_4p(ptr);
                break;
            case 8:
                endiannes_swap_8p(ptr);
                break;
            default:
                assert(0);
        }
    }

    return output;

}

// fwrite() substitute for endianness management of 4 and 8 bytes words
// Data in *ptr will be stored as big endian
// independently of how they were stored in this system.
// INPUT:
// void * ptr   : pointer to buffer where data is stored
// size_t size  : size of data in bytes
// FILE * stream: stream to write
// OUTPUT:
// number of bytes written
local size_t fwrite_endian( void * ptr, size_t size, FILE * stream ) {

    size_t output;
    void *endian_ptr;

    endian_ptr = malloc(size);

    if (endiannes_is_big()) {
        memcpy(endian_ptr, ptr, size);
    } else {
        // machine is little endian: flip bytes
        switch (size) {
            case 4:
                *(uint32_t *)endian_ptr = *(uint32_t *)ptr;
                endiannes_swap_4p(endian_ptr);
                break;
            case 8:
                *(uint64_t *)endian_ptr = *(uint64_t *)ptr;
                endiannes_swap_8p(endian_ptr);
                break;
            default:
                assert(0);
        }
    }

    output = fwrite(endian_ptr, size, 1, stream);

    free(endian_ptr);
    return output;

}
/*******************
 * end: Endianness *
 *******************/


// Compress source stream using zlib format.
// Used internally to compress the index' windows.
// based on def() from zlib/examples/zpipe.c by Mark Adler :
    /* Compress from file source to file dest until EOF on source.
       def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_STREAM_ERROR if an invalid compression
       level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
       version of the library linked do not match, or Z_ERRNO if there is
       an error reading or writing the files. */
// INPUT:
// unsigned char *source: pointer to data
// uint64_t *size       : size of data in bytes
// int level            : level of compression
// OUTPUT:
// pointer to compressed data (NULL on error)
local unsigned char *compress_chunk(unsigned char *source, uint64_t *size, int level)
{
    int ret, flush;
    unsigned have;
    z_stream strm;
    uint64_t i = 0;
    uint64_t output_size = 0;
    uint64_t input_size = *size;
    unsigned char *in, *out, *out_complete;

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, level);
    if (ret != Z_OK)
        return NULL;

    in = malloc(CHUNK);
    out = malloc(CHUNK);
    out_complete = malloc(CHUNK);

    /* compress until end of input */
    do {
        strm.avail_in = ((i + CHUNK) < input_size) ? CHUNK : (input_size - i);
        if ( memcpy(in, source + i, strm.avail_in) == NULL ) {
            (void)deflateEnd(&strm);
            goto compress_chunk_error;
        }
        flush = ((i + CHUNK) >= input_size) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;
        i += strm.avail_in;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK - strm.avail_out;
            if ( have != 0 && (
                 NULL == (out_complete = realloc(out_complete, output_size + have)) ||
                 NULL == memcpy(out_complete + output_size, out, have)
                 ) ) {
                (void)deflateEnd(&strm);
                goto compress_chunk_error;
            }
            output_size += have;
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in source processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);
    free(in);
    free(out);
    // return size of returned char array in size pointer parameter
    *size = output_size;
    return out_complete;

  compress_chunk_error:
    free(in);
    free(out);
    free(out_complete);
    return NULL;

}


// Decompress a source stream with zlib format.
// Used internally to decompress the index' windows.
// based on inf() from zlib/examples/zpipe.c by Mark Adler
    /* Decompress from file source to file dest until stream ends or EOF.
       inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_DATA_ERROR if the deflate data is
       invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
       the version of the library linked do not match, or Z_ERRNO if there
       is an error reading or writing the files. */
// INPUT:
// unsigned char *source: pointer to data
// uint64_t *size       : size of data in bytes
// OUTPUT:
// pointer to decompressed data (NULL on error)
local unsigned char *decompress_chunk(unsigned char *source, uint64_t *size)
{
    int ret;
    unsigned have;
    z_stream strm;
    uint64_t i = 0;
    uint64_t output_size = 0;
    uint64_t input_size = *size;
    unsigned char *in, *out, *out_complete;

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return NULL;

    in = malloc(CHUNK);
    out = malloc(CHUNK);
    out_complete = malloc(CHUNK);

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = (i + CHUNK < input_size) ? CHUNK : (input_size - i);
        if ( memcpy(in, source + i, strm.avail_in) == NULL ) {
            (void)inflateEnd(&strm);
            goto decompress_chunk_error;
        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;
        i += strm.avail_in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                goto decompress_chunk_error;
            }
            have = CHUNK - strm.avail_out;
            if ( have != 0 && (
                 NULL == (out_complete = realloc(out_complete, output_size + have)) ||
                 NULL == memcpy(out_complete + output_size, out, have)
                 ) ) {
                (void)inflateEnd(&strm);
                goto decompress_chunk_error;
            }
            output_size += have;
        } while (strm.avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    free(in);
    free(out);
    if (ret != Z_OK && ret != Z_STREAM_END) {
        printToStderr( VERBOSITY_NORMAL, "Decompression of index' chunk terminated with error (%d).\n", ret);
    }
    // return size of returned char array in size pointer parameter
    *size = output_size;
    return out_complete;

  decompress_chunk_error:
    free(in);
    free(out);
    free(out_complete);
    return NULL;
}


// Deallocate from memory
// an index built by decompress_and_build_index()
// INPUT:
// struct access *index: pointer to index
local void free_index(struct access *index)
{
    uint64_t i;
    if (index != NULL) {
        for(i=0; i < index->have; i++) {
            free(index->list[i].window);
        }
        free(index->list);
        free(index->file_name);
        free(index);
    }
}


// fill pointers of and index->list with NULL windows
// so that free_index() can proceed on any contingency
// INPUT:
// struct access *index : pointer to index
// uint64_t from        : initial point to fill
// uint64_t to          : last point to fill
local void empty_index_list(struct access *index, uint64_t from, uint64_t to)
{
    uint64_t i;
    for (i=from; i<to; i++) {
        index->list[i].window = NULL;
        index->list[i].window_size = 0;
    }
    return;
}


// Allocate an index structure
// and fill it with empty values
// OUTPUT:
// pointer to index
local struct access *create_empty_index()
{
    struct access *index;
    if ( NULL == ( index = malloc(sizeof(struct access)) ) ) {
        return NULL;
    }
    index->index_version = 0;
    index->file_size = 0;
    index->number_of_lines = 0;
    index->list = NULL;
    index->file_name = NULL;

    index->list = malloc(sizeof(struct point) << 3);
    empty_index_list( index, 0, 8 );
    if (index->list == NULL) {
        free(index);
        return NULL;
    }
    index->size = 8;
    index->have = 0;
    index->index_complete = 0;

    return index;
}


/* Add an entry to the access point list.  If out of memory, deallocate the
   existing list and return NULL. */
// INPUT:
// struct access *index : pointer to original index
// uint32_t bits        : (new entry data)
// uint64_t in          : (new entry data)
// uint64_t out         : (new entry data)
// unsigned left        : (new entry data)
// unsigned char *window: window data, already compressed with size window_size,
//                        or uncompressed with size WINSIZE,
//                        or store an empty window (NULL) because it resides on file.
// uint32_t window_size : size of passed *window (may be already compressed o not)
// uint64_t line_number : actual line number if index v1, 0 otherwise
// int compress_window  : 0: store window of size window_size, as it is, in point structure
//                        1: compress passed window of size window_size
// OUTPUT:
// pointer to (new) index (NULL on error)
local struct access *addpoint( struct access *index, uint32_t bits,
    uint64_t in, uint64_t out, unsigned left, unsigned char *window, uint32_t window_size,
    uint64_t line_number, int compress_window )
{
    struct point *next;
    uint64_t size = window_size;
    unsigned char *compressed_chunk;

    /* if list is empty, create it (starts with eight points) */
    if (index == NULL) {
        index = create_empty_index();
        if (index == NULL) return NULL;
    }

    /* if list is full, make it bigger */
    else if (index->have == index->size) {
        index->size <<= 1;
        next = realloc(index->list, sizeof(struct point) * index->size);
        if (next == NULL) {
            free_index(index);
            return NULL;
        }
        index->list = next;
        empty_index_list( index, index->have, index->size );
    }

    /* fill in entry and increment how many we have */
    next = index->list + index->have;
    next->bits = bits;
    next->in = in;
    next->out = out;
    next->line_number = line_number;

    if ( compress_window == 1 ) {
        next->window_size = window_size;
        next->window = NULL;
        if ( window_size > 0 ) {
            // compress window if window_size > 0: if not, a zero-length window is stored.
            next->window = malloc( window_size );
            // check left value with windows of size != WINSIZE
            if ( left > window_size ||
                 window_size != WINSIZE )
                left = 0;
            if (left)
                memcpy(next->window, window + window_size - left, left);
            if (left < window_size)
                memcpy(next->window + left, window, window_size - left);
            // compress window
            compressed_chunk = compress_chunk(next->window, &size, Z_DEFAULT_COMPRESSION);
            if (compressed_chunk == NULL) {
                printToStderr( VERBOSITY_NORMAL, "Error whilst compressing index chunk\nProcess aborted\n." );
                return NULL;
            }
            free(next->window);
            next->window = compressed_chunk;
            /* uint64_t size and uint32_t window_size, but windows are small, so this will always fit */
            next->window_size = size;
        }
        printToStderr( VERBOSITY_EXCESSIVE, "\t[%llu/%llu] window_size = %d\n", index->have+1, index->size, next->window_size);
    } else {
        // if NULL == window, this creates a NULL window: it resides on file,
        // and can/will later loaded on memory with its correct ->window_size;
        // if passed window is already compressed it will be stored as it is with size "window_size"
        next->window_size = window_size;
        next->window = window;
    }

    index->have++;

    /* return list, possibly reallocated */
    return index;
}


// serialize index into a file
// Note that the function writes and empty header when called with index_last_written_point = 0
// and also writes next points from index_last_written_point to index->have.
// To create a valid index, this function must be called a last time (at least, a 2nd time)
// to write the correct header values (and the (optional) tail (size of uncompressed file)).
// INPUT:
// FILE *output_file    : output stream
// struct access *index : pointer to index
// uint64_t index_last_written_point : last index point already written to file: its values
//                                     go from 1 to index->have, so that 0 has special value "None".
// OUTPUT:
// 0 on error, 1 on success
int serialize_index_to_file( FILE *output_file, struct access *index, uint64_t index_last_written_point ) {
    struct point *here = NULL;
    uint64_t temp;
    uint64_t offset;
    uint64_t i;

    /* proceed only if there's something reasonable to do */
    if (NULL == output_file || NULL == index) {
        return 0;
    }

    /*if (index->have <= 0)
        return 0;*/
    /* writing and empy index is allowed: writes the header (of size 4*8 = 32 bytes) */

    if ( index_last_written_point == 0 ) {

        /* write header */
        fseeko( output_file, 0, SEEK_SET);
        /* 0x0 8 bytes (to be compatible with .gzi for bgzip format: */
        /* the initial uint32_t is the number of bgzip-idx registers) */
        temp = 0;
        fwrite_endian(&temp, sizeof(temp), output_file);
        /* a 64 bits readable identifier */
        if ( index->index_version == 0 )
            fprintf(output_file, GZIP_INDEX_IDENTIFIER_STRING);
        else {
            fprintf(output_file, GZIP_INDEX_IDENTIFIER_STRING_V1);
            // there's an additional register on v1: line_number_format
            fwrite_endian(&(index->line_number_format), sizeof(index->line_number_format), output_file);
        }

        /* and now the raw data: the access struct "index" */

        // as this may be a growing index
        // values will be filled with special, not actual, values:
        // 0x0..0 , 0xf..f
        // Last write operation will overwrite these with correct values, that is, when
        // serialize_index_to_file() be called with index_last_written_point = index->have
        temp = 0;
        fwrite_endian(&temp, sizeof(temp), output_file); // have
        temp = UINT64_MAX;
        fwrite_endian(&temp, sizeof(temp), output_file); // size

    }

    offset = GZIP_INDEX_HEADER_SIZE + sizeof(temp) +
            ( (index->index_version == 1)? sizeof(index->line_number_format): 0 );

    // update index->size on disk with index->have data
    // ( if index->have == 0, no index points still, maintain UINT64_MAX )
    if ( index->have > 0 ) {
        // seek to index->have position
        fseeko( output_file, offset, SEEK_SET );
        // write index->have value; (when the index be closed, index->size on disk will be >0)
        fwrite_endian( &(index->have), sizeof(index->have), output_file );
    }

    // fseek to index position of index_last_written_point
    offset += sizeof(temp);
    for (i = 0; i < index_last_written_point; i++) {
        here = &(index->list[i]);
        offset += sizeof(here->out) + sizeof(here->in) +
                  sizeof(here->bits) + sizeof(here->window_size) +
                  ((here->window_size==UNCOMPRESSED_WINDOW)? WINSIZE: (here->window_size)) +
                  ( (index->index_version == 1)? sizeof(here->line_number): 0 );
    }
    fseeko( output_file, offset, SEEK_SET);
    printToStderr( VERBOSITY_MANIAC, "index_last_written_point = %llu\n", index_last_written_point );
    if (NULL!=here) {
        printToStderr( VERBOSITY_MANIAC, "%llu->window_size = %d\n", i, here->window_size );
    }
    printToStderr( VERBOSITY_MANIAC, "have = %llu, offset = %llu\n", index->have, offset );

    if ( index_last_written_point != index->have ) {
        for (i = index_last_written_point; i < index->have; i++) {
            here = &(index->list[i]);
            printToStderr( VERBOSITY_EXCESSIVE, "writing new point #%llu (%llu, %llu)\n", i+1, here->in, here->out );
            fwrite_endian(&(here->out),  sizeof(here->out),  output_file);
            fwrite_endian(&(here->in),   sizeof(here->in),   output_file);
            fwrite_endian(&(here->bits), sizeof(here->bits), output_file);
            if ( here->window_size==UNCOMPRESSED_WINDOW ) {
                temp = WINSIZE;
                fwrite_endian(&(temp), sizeof(here->window_size), output_file);
            } else {
                fwrite_endian(&(here->window_size), sizeof(here->window_size), output_file);
            }
            here->window_beginning = ftello(output_file);
            if ( NULL == here->window &&
                 here->window_size != 0 ) {
                printToStderr( VERBOSITY_NORMAL, "Index incomplete! - index writing aborted.\n" );
                return 0;
            } else {
                if ( here->window_size > 0 )
                    fwrite(here->window, here->window_size, 1, output_file);
            }
            // once written, point's window CAN (and will now) BE DELETED from memory
            free(here->window);
            here->window = NULL;
            if ( index->index_version == 1 ) {
                // there's an additional register on v1: line_number
                fwrite_endian(&(here->line_number), sizeof(here->line_number), output_file);
            }
        }
    } else {
        // Last write operation:
        // tail must be written:
        /* write size of uncompressed file (useful for bgzip files) */
        fwrite_endian(&(index->file_size), sizeof(index->file_size), output_file);
        if ( index->index_version == 1 ) {
            // there's an additional register on v1: number_of_lines
            fwrite_endian(&(index->number_of_lines), sizeof(index->number_of_lines), output_file);
        }
        // and header must be updated:
        // for this, move position to header:
        fseeko( output_file,
            GZIP_INDEX_HEADER_SIZE + ( (1 == index->index_version)? sizeof(index->line_number_format): 0 ),
            SEEK_SET );
        fwrite_endian(&index->have, sizeof(index->have), output_file);
        /* index->size is not written as only filled entries are usable */
        fwrite_endian(&index->have, sizeof(index->have), output_file);
        index->index_complete = 1; /* index is now complete */
    }

    // flush written content to disk
    fflush( output_file );

    return 1;
}


/* Basic checks of existing index file:
   - Checks that last index point ->in isn't greater than gzip file size
*/
// INPUT:
// struct access *index         : pointer to index. Can be NULL => no check.
// char *file_name     : gzip file name. Can be NULL or "" => no check.
// char *index_filename: index file name. Must be != NULL, but can be "". Only used to print warning.
// OUTPUT:
// 0 on error, 1 on success
int check_index_file( struct access *index, char *file_name, char *index_filename ) {

    if ( NULL != file_name &&
         strlen( file_name ) > 0 ) {
        if ( NULL != index &&
             index->have > 0 ) {
            // size of input file
            struct stat st;
            stat( file_name, &st );
            printToStderr( VERBOSITY_EXCESSIVE, "(%llu >= %llu)\n", st.st_size, ( index->list[index->have - 1].in ) );
            if ( index->have > 1 &&
                (uint64_t) st.st_size < ( index->list[index->have - 1].in )
                ) {
                printToStderr( VERBOSITY_NORMAL, "WARNING: Index file '%s' corresponds to a file bigger than '%s'\n",
                    index_filename, file_name );
                return 0;
            }
        }
    }

    return 1;

}


// Creates index for a gzip stream (file or STDIN);
// action_create_index() always call this function with an incomplete index file
// if it already exists.
// If an incomplete index is passed, it will be completed from the last
// available index point so the whole gzip stream is not processed again.
// Original (zran.c) comments:
    /* Make one entire pass through the compressed stream and build an index, with
       access points about every span bytes of uncompressed output -- span is
       chosen to balance the speed of random access against the memory requirements
       of the list, about 32K bytes per access point.  Note that data after the end
       of the first zlib or gzip stream in the file is ignored.  build_index()
       returns the number of access points on success (>= 1), Z_MEM_ERROR for out
       of memory, Z_DATA_ERROR for an error in the input file, or Z_ERRNO for a
       file read error.  On success, *built points to the resulting index. */
// INPUT:
// FILE *file_in            : input stream
// FILE *file_out           : output stream
// char *file_name : name of the input file associated with FILE *in.
//                            Can be "" (no file name: stdin used), but not NULL.
//                            Used only if there's no usable index && input (FILE *in)
//                            is associated with a file (not stdin) &&
//                            indx_n_extraction_opts == *_TAIL, for the use of the file
//                            size as approximation of the size of the tail to be output.
// uint64_t span            : span
// struct access **built: address of index pointer, equivalent to passed by reference.
//                        Note that index may be received with some (all) points already set
//                        from caller, if an index file was already available - and so this
//                        function must use it or create new points from the last available one,
//                        if needed.
// enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts:
//                      = JUST_CREATE_INDEX: usual behaviour
//                      = SUPERVISE_DO  : supervise a growing "in" gzip stream
//                      = SUPERVISE_DO_AND_EXTRACT_FROM_TAIL: like SUPERVISE_DO but
//                          this will also extract data to stdout, starting from
//                          the last available bytes (tail) on gzip when called.
//                      = EXTRACT_FROM_BYTE: extract from indicated offset byte, to stdout
//                      = EXTRACT_FROM_LINE: extract from indicated line, to stdout
// uint64_t offset      : if indx_n_extraction_opts == EXTRACT_FROM_BYTE, this is the offset byte
//                        in the uncompressed stream from which to extract to stdout.
//                        0 otherwise.
// uint64_t line_number_offset : if indx_n_extraction_opts == EXTRACT_FROM_LINE, this is the offset line
//                               in the uncompressed stream from which to extract to stdout.
//                               0 otherwise.
// char *index_filename   : index will be read/written on-the-fly
//                                    to this index file name.
// int write_index_to_disk  : 1: will write/update the index as usual;
//                            0: will just read, but do not write nor update (overwrite) the index on disk,
//                           and also, do not create/update the index in memory (**built and returned_output.value):
//                           This is done because if index is not written to disk, windows would need to be
//                           maintained in memory, increasing the memory footprint unnecessarily as the index
//                           is not (actually) used later in the app. (Windows could be also emptied, but again,
//                           index points are not used later.)
// int end_on_first_proper_gzip_eof : end file processing on first proper (at feof()) GZIP EOF
//                                    (to be used when the file contains surely only one gzip stream)
// int always_create_a_complete_index : create a 'complete' index file even in case of decompressing errors.
//                                      Also an index pointer (**built) is returned, instead of NULL.
// int waiting_time             : waiting time in seconds between reads when `-[ST]` (always >0)
// int extend_index_with_lines  : 0: create index without line numbers (v0 index)
//                                1: create index WITH line numbers (v1 index) using Unix format (\n)
//                                2: create index WITH line numbers (v1 index) using old Mac format (\r) (compatible with Windows \n\r)
//                                Nonetheless, note that if index previously exists and is
//                                index->index_version == 0 it cannot be extended to v1, and
//                                if it is index->index_version == 1, it cannot be made v0,
//                                so extend_index_with_lines IS RESPECTED ONLY IF INDEX DOES NOT EXIST YET.
// uint64_t expected_first_byte  : indicates that the first byte on compressed input is this, not 1,
//                                and so truncated compressed inputs can be used. Only can be used (>1) if
//                                indx_n_extraction_opts is EXTRACT_FROM_BYTE or EXTRACT_FROM_LINE.
// OUTPUT:
// struct returned_output: contains two values:
//      .error: Z_* error code or Z_OK if everything was ok
//      .value: size of built index (index->have)
local struct returned_output decompress_and_build_index(
    FILE *file_in, FILE *file_out, char *file_name, uint64_t span, struct access **built,
    enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts, uint64_t offset, uint64_t line_number_offset,
    char *index_filename, int write_index_to_disk,
    int end_on_first_proper_gzip_eof, int always_create_a_complete_index,
    int waiting_time, int extend_index_with_lines, uint64_t expected_first_byte )
{
    struct returned_output ret;
    uint64_t totin  = 0;           /* our own total counters to avoid 4GB limit */
    uint64_t totout = 0;           /* our own total counters to avoid 4GB limit */
    uint64_t totlines = 1;         /* counts total line numbers in uncompressed data from file_in */
    int there_are_more_chars = 0;  /* totlines may need to be decremented at the end depending on last stream char */
    uint64_t last   = 0;           /* totout value of last access point */
    uint64_t offset_in = 0;        /* offset in compressed data to reach (opposed to "offset" in uncompressed data) */
    uint64_t have_lines = 0;       /* number of lines in last chunk of uncompressed data */
    uint64_t avail_in_0;           /* because strm.avail_in may not exhausts every cycle! */
    uint64_t avail_out_0;          /* because strm.avail_out may not exhausts every cycle! */
    struct access *index = NULL;/* access points being generated */
    struct point *here = NULL;
    uint64_t actual_index_point = 0;  // only set initially to >0 if NULL != *built
    uint64_t output_data_counter = 0; // counts uncompressed bytes extracted to stdout
    uint64_t output_lines_counter = 0;// counts lines extracted to stdout
    unsigned char *decompressed_window = NULL;
    z_stream strm;
    FILE *index_file = NULL;
    size_t index_last_written_point = 0;
    int extraction_from_offset_in = 0;           // 1: extract data using offset_in value
    int start_extraction_on_first_depletion = 0; // 0: extract - no depletion interaction.
                                                 // 1: start extraction on first depletion.
    int decompressing_with_gzip_headers = 0;     // 1 if inflateInit2(&strm, 47);
    int gzip_eof_detected = 0;     // 1 if a GZIP file format EOF is detected, and so, feof() is not trustworthy
    unsigned char input[CHUNK];    // TODO: convert to malloc
    unsigned char window[WINSIZE]; // TODO: convert to malloc
    unsigned window_size = 0;      // restarted on every reinitiation of strm data
    unsigned char window2[WINSIZE];// TODO: convert to malloc
    unsigned window2_size = 0;     // size of data stored in window2 buffer
    uint64_t index_points_0 = 0;   // marker to detect index updates (and to not write if index isn't updated)
    uint64_t index_file_size_0 = 0;// marker to detect index updates (and to not write if index isn't updated)

    ret.value = 0;
    ret.error = Z_OK;

    // Inform index filename
    if ( NULL != (*built) &&
        // if index->have == 0 index is superfluous
        (*built)->have > 0 &&
        // this condition is never true because index is passed always as incomplete
        (*built)->index_complete == 1 ) {
        // even though index is complete, processing will occur because gzip file may have changed!
        printToStderr( VERBOSITY_NORMAL, "Using index '%s'...\n", index_filename );
    } else {
        if ( write_index_to_disk == 1 ) {
            if ( strlen(index_filename) > 0 )
                printToStderr( VERBOSITY_NORMAL, "Processing index to '%s'...\n", index_filename );
            else
                printToStderr( VERBOSITY_NORMAL, "Processing index to STDOUT...\n" );
        } else {
            if ( indx_n_extraction_opts != DECOMPRESS )
                printToStderr( VERBOSITY_NORMAL, "Reading index '%s'...\n", index_filename );
        }
    }

    /* open index_filename for binary reading & writing */
    if ( strlen(index_filename) > 0 &&
        ( NULL == (*built) ||           // if passed index doesn't exists yet,
          // or it's an incomplete index in which case it may need to be completed
          (*built)->index_complete == 0 )
        ) {
        if ( indx_n_extraction_opts != DECOMPRESS ) {
            printToStderr( VERBOSITY_EXCESSIVE, "write_index_to_disk = %d", write_index_to_disk );
            if ( write_index_to_disk == 1 ) {
                if ( access( index_filename, F_OK ) != -1 ) {
                    // index_filename already exist:
                    // "r+": Open a file for update (both for input and output). The file must exist.
                    // r+, because the index may be incomplete, and so decompress_and_build_index() will
                    // append new data and complete it (->have & ->size to correct values, not 0x0..0, 0xf..f).
                    index_file = fopen( index_filename, "r+b" );
                    printToStderr( VERBOSITY_EXCESSIVE, " (r+b)\n" );
                } else {
                    // index_filename does not exist:
                    index_file = fopen( index_filename, "w+b" );
                    printToStderr( VERBOSITY_EXCESSIVE, " (w+b)\n" );
                }
            } else {
                // open index file in read-only mode (if file does not exist, NULL == index_file)
                index_file = fopen( index_filename, "rb" );
                printToStderr( VERBOSITY_EXCESSIVE, " (rb)\n" );
            }
        }
    } else {
        // restrictions to not collide index output with data output to stdout
        // MUST have been made on caller.
        SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
        index_file = stdout;
    }
    if ( NULL == index_file && write_index_to_disk == 1 ) {
        printToStderr( VERBOSITY_NORMAL, "Could not write index to file '%s'.\n", index_filename );
        ret.error = Z_ERRNO;
        return ret;
    }

    if ( indx_n_extraction_opts == DECOMPRESS ) {
        indx_n_extraction_opts = EXTRACT_FROM_BYTE;
        offset = 0;
    }

    /* inflate the input, maintain a sliding window, and build an index -- this
       also validates the integrity of the compressed data using the check
       information at the end of the gzip or zlib stream */

    // if an index is already passed, use it:
    if ( NULL != (*built) &&
        (*built)->have > 0 ) {
        // NULL != *built (there is a previous index available: use it!)
        // if index->have == 0 index is superfluous

        index = *built;
        index_points_0 = index->have;   // marker to detect index updates (and to not write if index isn't updated)
        index_file_size_0 = index->file_size;// marker to detect index updates (and to not write if index isn't updated)

        /* initialize file and inflate state to start there */
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = 0;
        strm.next_in = Z_NULL;
        ret.error = inflateInit2(&strm, -15);         /* raw inflate */
        if (ret.error != Z_OK)
            return ret;

        index_last_written_point = index->have;

        //
        // Select an index point to start, depending on indx_n_extraction_opts
        //
        if ( indx_n_extraction_opts == SUPERVISE_DO ||
             indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
             indx_n_extraction_opts == JUST_CREATE_INDEX ||
             indx_n_extraction_opts == EXTRACT_TAIL
             ) {
            // move to last available index point, and continue from it
            actual_index_point = index->have - 1;
            // this index must be completed from last point: index->list[index->have-1]
            totin  = index->list[ actual_index_point ].in;
            totout = index->list[ actual_index_point ].out;
            totlines = index->list[ actual_index_point ].line_number;
            last = totout;
            here = &(index->list[ actual_index_point ]);
        }

        // this block of code never executes because index is always received as incomplete
        if ( index->index_complete == 1 ) {
            if ( indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
                 indx_n_extraction_opts == EXTRACT_TAIL ) {
                offset = ( index->file_size - totout ) /4*3;
                indx_n_extraction_opts = EXTRACT_FROM_BYTE;
            }
        }

        if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE ) {
            // move to the point needed for positioning on offset, or
            // move to last available point if offset can't be reached
            // with actually available index
            here = index->list;
            actual_index_point = 0;
            while (
                ++actual_index_point &&
                actual_index_point < index->have &&
                here[1].out <= offset
                )
                here++;
            actual_index_point--;
            totin  = index->list[ actual_index_point ].in;
            totout = index->list[ actual_index_point ].out;
            totlines = index->list[ actual_index_point ].line_number;
            last = totout;
            // offset value comes from caller as parameter
        }

        if ( indx_n_extraction_opts == EXTRACT_FROM_LINE ) {
            // move to the point needed for positioning on a line, or
            // move to last available point if offset can't be reached
            // with actually available index
            here = index->list;
            actual_index_point = 0;
            while (
                ++actual_index_point &&
                actual_index_point < index->have &&
                // if index point is on the desired line number, we must start
                // on the previous one to retrieve the line from its beginning:
                here[1].line_number < line_number_offset
                )
                here++;
            actual_index_point--;
            totin  = index->list[ actual_index_point ].in;
            totout = index->list[ actual_index_point ].out;
            totlines = index->list[ actual_index_point ].line_number;
            last = totout;
            // offset value comes from caller as parameter
        }

        assert( NULL != here );

        printToStderr( VERBOSITY_EXCESSIVE, "Starting from index point %llu (@%llu->%llu,L%llu).\n",
            actual_index_point+1, here->in, here->out, here->line_number );

        // fseek in data for correct position
        // using here index data:
        {
            uint64_t position = here->in - (here->bits ? 1 : 0);

            // checking expected_first_byte value:
            // (although this has already been done for `-b`, `-L` could not be checked until now)
            if ( ( expected_first_byte -1 ) > position ) {
                printToStderr( VERBOSITY_NORMAL, "ERROR: `-n %llu` is beyond required index point @%llu byte.\n",
                    expected_first_byte, position );
                ret.error = Z_ERRNO;
                // strm and index are already filled, so a simple return isn't possible:
                goto decompress_and_build_index_error;
            } else {
                position -= ( expected_first_byte -1 );
            }

            if ( stdin == file_in ) {
                // read input until here->in - (here->bits ? 1 : 0)
                uint64_t pos = 0;
                ret.error = 0;
                while ( pos < position ) {
                    if ( !fread(input, 1, (pos+CHUNK < position)? CHUNK: (position - pos), file_in) ) {
                        ret.error = -1;
                        break;
                    }
                    pos += CHUNK;
                }
            } else {
                ret.error = fseeko(file_in, position, SEEK_SET);
            }

            if (ret.error == -1)
                goto decompress_and_build_index_error;

            if (here->bits) {
                int i;
                i = getc(file_in);
                if (i == -1) {
                    ret.error = ferror(file_in) ? Z_ERRNO : Z_DATA_ERROR;
                    goto decompress_and_build_index_error;
                }
                (void)inflatePrime(&strm, here->bits, i >> (8 - here->bits));
            }

        }

        // obtain window and initialize with it zlib's Dictionary
        if (here->window == NULL && here->window_beginning != 0) {
            /* index' window data is not on memory,
            but we have position and size on index file, so we load it now */
            FILE *index_file;
            if ( index->file_name == NULL ||
                strlen(index->file_name) == 0 ) {
                printToStderr( VERBOSITY_NORMAL, "Error while opening index file.\nAborted.\n" );
                ret.error = Z_ERRNO;
                goto decompress_and_build_index_error;
            }
            if (NULL == (index_file = fopen(index->file_name, "rb")) ||
                0 != fseeko(index_file, here->window_beginning, SEEK_SET)
                ) {
                printToStderr( VERBOSITY_NORMAL, "Error while opening index file.\nAborted.\n" );
                ret.error = Z_ERRNO;
                goto decompress_and_build_index_error;
            }
            // here->window_beginning = 0; // this is not needed
            if ( here->window_size > 0 &&
                ( NULL == (here->window = malloc(here->window_size)) ||
                 !fread(here->window, here->window_size, 1, index_file) )
                ) {
                printToStderr( VERBOSITY_NORMAL, "Error while reading index file.\nAborted.\n" );
                ret.error = Z_ERRNO;
                goto decompress_and_build_index_error;
            }
            fclose(index_file);
        }

        // Windows are ALWAYS stored compressed, so decompress it now:
        /* decompress() use uint64_t counters, but index->list->window_size is smaller */
        // local_window_size in order to avoid deleting the on-memory here->window_size,
        // that may be needed later if index must be increased and written to disk (fseeko).
        window_size = 0;
        if ( here->window_size > 0 ) {
            uint64_t local_window_size = here->window_size;
            /* window is compressed on memory, so decompress it */
            decompressed_window = decompress_chunk(here->window, &local_window_size);
            if ( NULL == decompressed_window ) {
                ret.error = Z_ERRNO;
                goto decompress_and_build_index_error;
            }
            (void)inflateSetDictionary(&strm, decompressed_window, local_window_size); // (local_window_size may not be WINSIZE)
            free( decompressed_window );
            decompressed_window = NULL;
            window_size = local_window_size;
        }

    } // end if ( NULL != (*built) && (*built)->have > 0 ) {


    // more decisions for extracting uncompressed data
    if ( ( NULL != (*built) && stdin == file_in ) ||
         NULL == (*built) ||
         ( NULL != (*built) && (*built)->index_complete == 0 )
         ) {
        // index available and stdin is used as gzip data input,
        // or no index is available,
        // or index exists but it is incomplete.

        if ( stdin == file_in ) {
            // stdin is used as input for gzip data

            if ( indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ) {
                start_extraction_on_first_depletion = 1;
            }
            if ( indx_n_extraction_opts == EXTRACT_TAIL ) {
                start_extraction_on_first_depletion = 1;
            }

        } else {
            // there's a gzip filename

            if ( indx_n_extraction_opts == EXTRACT_TAIL ||
                 indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ) {
                // set offset_in (equivalent to offset but for ->in values)
                // to the last CHUNK of gzip data ... this can be a good tail...
                struct stat st;
                if ( strlen( file_name ) > 0 ) {
                    stat(file_name, &st);
                    if ( st.st_size > 0 ) {
                        extraction_from_offset_in = 1;
                        if ( st.st_size <= CHUNK ) {
                            // gzip file is really small:
                            // change operation mode to extract from byte 0
                            offset_in = 0;
                        } else {
                            offset_in = st.st_size - CHUNK;
                        }
                        printToStderr( VERBOSITY_EXCESSIVE, "offset_in=%llu\n", offset_in );
                    } else {
                        extraction_from_offset_in = 1;
                        start_extraction_on_first_depletion = 1;
                    }
                } else {
                    // STDIN: size cannot be calculated in advance
                    start_extraction_on_first_depletion = 1;
                }
            }

        } // end if ( stdin == file_in )

    } // end if ( ( NULL != *built && stdin == file_in ) ||
      //          NULL == *built ) ||
      //          ( NULL != (*built) && (*built)->index_complete == 0 ) )


    // decrement offset_in, offset and line_number_offset by actual position:
    if ( offset_in > 0 &&
        NULL != here ) {
        if ( here->in > offset_in )
            offset_in = 0;
        else
            offset_in -= here->in;
    }
    if ( offset > 0 &&
        NULL != here ) {
        if ( here->out > offset )
            offset = 0;
        else
            offset -= here->out;
    }
    if ( line_number_offset > 0 ) {
        if ( NULL != here ) {
            // there's an index loaded
            if ( here->line_number > line_number_offset )
                line_number_offset = 0;
            else
                line_number_offset -= here->line_number;
        } else {
            // there's no index available
            line_number_offset --;
        }
    }

    // default zlib initialization
    // when no index entry points have been found:
    if ( NULL == (*built) ||
        NULL == here ) {
        // NULL != *built (there is no previous index available: build it from scratch)

        /* initialize inflate */
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = 0;
        strm.next_in = Z_NULL;
        ret.error = inflateInit2(&strm, 47);      /* automatic zlib or gzip decoding (15 + automatic header detection) */
        decompressing_with_gzip_headers = 1;
        printToStderr( VERBOSITY_MANIAC, "inflateInit2 = %d\n", ret.error );
        if (ret.error != Z_OK)
            goto decompress_and_build_index_error;
        totin = totout = last = 0;
        totlines = 1;
        index = NULL;               /* will be allocated on first addpoint() */

        if ( extend_index_with_lines > 0 ) {
            // mark index as index_version = 1 to store line numbers when serialize();
            // in order to do this, index must be created now (empty)
            index = create_empty_index();
            if ( index == NULL ) { // Oops!?
                ret.error = Z_MEM_ERROR;
                goto decompress_and_build_index_error;
            }
            index->index_version = 1;
            if ( extend_index_with_lines == 2 )
                index->line_number_format = 1;
            else
                index->line_number_format = 0;
        }
    }

    // initialize output window
    strm.next_out = window;
    strm.avail_out = WINSIZE;
    avail_out_0 = strm.avail_out;
    do {
        /* get some compressed data from input file */

        // bgzip-compatible-streams code:
        if ( ret.error == Z_STREAM_END &&
             strm.avail_in > 0 ) { // if strm.avail_in is casually 0, this block of code isn't needed
            // readjust input data moving it to the beginning of buffer
            // (strm.next_in floats from input to input+CHUNK)
            memmove( input, strm.next_in, strm.avail_in ); // memmove() is overlapping-safe
        }
        // .................................................
        // note that here strm.avail_in > 0 only if ret.error == Z_STREAM_END
        int strm_avail_in0 = strm.avail_in;
        if ( !feof( file_in )) { // on last block, strm.avail_in > 0 is possible with feof(file_in)==1 already!
            strm.avail_in = fread(input + strm_avail_in0, 1, CHUNK - strm_avail_in0, file_in);
            printToStderr( VERBOSITY_MANIAC, "[read %d B]", strm.avail_in );
            strm.avail_in += strm_avail_in0;
        }
        if ( feof( file_in ) ) {
            // generic check for growing files (not related to bgzip-compatible-streams code):
            // check that file hasn't shrunk, what would mean that the file
            // has been overwritten from the beginning (possible with rsyslog logs, for example)
            if ( strlen( file_name ) > 0 ) {    // this check cannot be done on STDIN
                struct stat st;
                stat(file_name, &st);
                printToStderr( VERBOSITY_NUTS, "(%llu<%llu?)", st.st_size + GZIP_HEADER_SIZE_BY_ZLIB, totin );
                if ( (uint64_t)( st.st_size + GZIP_HEADER_SIZE_BY_ZLIB ) < totin ) {
                    // file has shrunk! so do a correct finish of the action_create_index (whatever it is)
                    // (Note that it is not possible that this condition arises when accessing a truncated gzip
                    // file with its corresponding (not-truncated) complete index file, because in this case
                    // fseeko() previously failed. Also if gzip-file size is 0 (which is possible and allowed)
                    // this condition won't arise because (10 < 0) is false (index doesn't exist) and
                    // (10 < 10) is false (index contains always the first access point))
                    printToStderr( VERBOSITY_EXCESSIVE, "\nDetected '%s' gzip file overwriting, so ending process\n", file_name );
                    break;
                }
            }
        }
        // .................................................
        // decompressor state MUST be reinitiated after Z_STREAM_END
        if ( ret.error == Z_STREAM_END ) {
            printToStderr( VERBOSITY_MANIAC, "Reinitializing zlib strm data...\n" );
            strm_avail_in0 = strm.avail_in;
            (void)inflateEnd(&strm);
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = 0;
            strm.next_in = Z_NULL;
            ret.error = inflateInit2(&strm, 47);      /* automatic zlib or gzip decoding (15 + automatic header detection) */
            decompressing_with_gzip_headers = 1;
            if (ret.error != Z_OK)
                goto decompress_and_build_index_error;
            strm.avail_in = strm_avail_in0;
            // it is compulsory to reinitiate also output data:
            strm.avail_out = WINSIZE;
            strm.next_out = window;
            avail_out_0 = strm.avail_out;
            window_size = 0;
        }
        // end of bgzip-compatible-streams code

        avail_in_0 = strm.avail_in;

        printToStderr( VERBOSITY_CRAZY, "output_data_counter=%llu,totin=%llu,totout=%llu,totlines=%llu,ftello=%llu,avail_in=%d\n",
            output_data_counter, totin, totout, totlines, ftello(file_in), strm.avail_in );

        if ( (indx_n_extraction_opts == SUPERVISE_DO ||
              indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
              indx_n_extraction_opts == EXTRACT_TAIL ) &&
             strm.avail_in == 0 ) {

            // check conditions to start output of uncompressed data
            printToStderr( VERBOSITY_CRAZY, ">>> %d, %d, %d",
                extraction_from_offset_in, start_extraction_on_first_depletion, indx_n_extraction_opts );
            if ( start_extraction_on_first_depletion == 1 ) {
                start_extraction_on_first_depletion = 0;

                // output uncompressed data
                unsigned have = WINSIZE - strm.avail_out;
                if ( have == 0 ) {
                    if ( window2_size > 0 ) {
                        // if we have previous data to show, show it, because now we're out of fresh data!
                        output_data_counter += window2_size;
                        printToStderr( VERBOSITY_MANIAC, "[..>%d]", window2_size );
                        if (fwrite(window2, 1, window2_size, file_out) != window2_size || ferror(file_out)) {
                            (void)inflateEnd(&strm);
                            ret.error = Z_ERRNO;
                            goto decompress_and_build_index_error;
                        }
                    } else {
                        // file has size 0 and hasn't (still) grown, so retain state
                        start_extraction_on_first_depletion = 1;
                    }
                } else {
                    output_data_counter += have;
                    printToStderr( VERBOSITY_MANIAC, "[.>%d]", have );
                    if (fwrite(strm.next_out, 1, have, file_out) != have || ferror(file_out)) {
                        (void)inflateEnd(&strm);
                        ret.error = Z_ERRNO;
                        goto decompress_and_build_index_error;
                    }
                }
                fflush(file_out);

                // continue extracting data as usual,
                offset = 0;
                offset_in = 0;
                line_number_offset = 0;
                // though as indx_n_extraction_opts != EXTRACT_FROM_* it'll
                // patiently waits if data exhausts.

            }

            if ( indx_n_extraction_opts == EXTRACT_TAIL ) {
                // the process ends here as all required data has been output
                // (index remains incomplete)
                ret.error = Z_OK;
                if ( NULL != index ) {
                    ret.value = index->have;
                }
                goto decompress_and_build_index_error;
            }

            // sleep and retry
            sleep( waiting_time );
            clearerr( file_in );
            continue;

        }

        if ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
             indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
             indx_n_extraction_opts == EXTRACT_FROM_LINE ||
             indx_n_extraction_opts == EXTRACT_TAIL ) {
            // with not Supervising options, strm.avail_in == 8 + eof is equivalent to Correct END OF GZIP at EOF
            if ( feof( file_in ) && strm.avail_in == 8 ) {
                printToStderr( VERBOSITY_EXCESSIVE, "Correct END OF GZIP file detected at EOF-8.\n" );
                gzip_eof_detected = 0; // to exit loop, as "gzip_eof_detected == 1" is one ORed condition
                                       // and now this variable is not needed anymore.
                break;
            }
        }

        if (ferror(file_in)) {
            ret.error = Z_ERRNO;
            goto decompress_and_build_index_error;
        }
        if (strm.avail_in == 0) {
            ret.error = Z_DATA_ERROR;
            goto decompress_and_build_index_error;
        }
        strm.next_in = input;

        /* process all of strm.next_in (size strm.avail_in), or until end of stream */
        do {
            /* reset sliding window if necessary */
            if (strm.avail_out == 0) {
                strm.avail_out = WINSIZE;
                strm.next_out = window;
                avail_out_0 = strm.avail_out;
            }

            /* inflate until out of input, output, or at end of block --
               update the total input and output counters */
            totin += strm.avail_in;
            totout += strm.avail_out;
            window_size += strm.avail_out; // update window_size available size
            ret.error = inflate(&strm, Z_BLOCK);      /* return at end of block */
            totin -= strm.avail_in;
            totout -= strm.avail_out;
            { // update window_size available size
                window_size -= strm.avail_out;
                if (window_size > WINSIZE)
                    window_size = WINSIZE;
            }
            // count lines in this decompressed chunk
            if ( NULL != index && // if lines must be counted, index passed must be !=NULL
                                  // to mark index->index_version == 1
                 index->index_version == 1 ) {
                // output data is in window + an offset
                unsigned char *output_data = window + (WINSIZE - avail_out_0);
                unsigned char *pos;
                int count = avail_out_0 - strm.avail_out;
                have_lines = 0;
                do {
                    pos = memchr( output_data, ( (0 == index->line_number_format)? '\n': '\r' ), count );
                    if ( NULL != pos ) {
                        totlines ++;
                        have_lines ++;
                        count -= ( pos - output_data ) + 1;
                        if ( count > 0 )
                            there_are_more_chars = 1;
                        else
                            there_are_more_chars = 0;
                        output_data = pos + 1;
                    }
                } while ( pos != NULL && pos < (output_data + count) );
            }

            // maintain a backup window for the case of sudden Z_STREAM_END
            // and indx_n_extraction_opts == *_TAIL
            if ( output_data_counter == 0 &&
                 ( NULL == index || index->index_complete == 0 ) &&
                 ( indx_n_extraction_opts == EXTRACT_TAIL ||
                   indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ) ) {
                if ( WINSIZE - strm.avail_out > 0 ) { // if have == 0, maintain previous (data and) window2_size value
                    window2_size = WINSIZE - avail_out_0;
                    printToStderr( VERBOSITY_NUTS, "(w2s=%d)", window2_size );
                    if ( window2_size <= WINSIZE )
                        memcpy( window2, window, window2_size );
                }
                // TODO: change to pointer flip instead of memcpy (possible?)
            }

            // .................................................
            // treat possible gzip tail:
            if ( ret.error == Z_STREAM_END &&
                 strm.avail_in >= 8 &&
                 decompressing_with_gzip_headers == 0 ) {
                // discard this data as it is probably the 8 gzip-tail bytes (4 CRC + 4 size%2^32)
                // and zlib is not able to consume it if inflateInit2(&strm, -15) (raw decompressing)
                strm.avail_in -= 8;
                strm.next_in += 8;
                totin += 8;         // data is discarded from strm input, but it MUST be counted in total input
                if ( offset_in >= 8 )
                    offset_in -= 8; // data is discarded from strm input, and also from offset_in
                else
                    offset_in = 0;
                printToStderr( VERBOSITY_EXCESSIVE,
                    "END OF GZIP passed @%llu while in raw mode (totout=%llu, avail_in=%d, ftello=%llu)\n",
                    totin, totout, strm.avail_in, ftello(file_in) );
                if ( end_on_first_proper_gzip_eof == 1 ||
                    ( ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
                    indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                    indx_n_extraction_opts == EXTRACT_FROM_LINE ||
                    indx_n_extraction_opts == EXTRACT_TAIL ) && feof( file_in ) ) )
                    gzip_eof_detected = 0;
                else
                    gzip_eof_detected = 1;
                break;
                // avail_in_0 doesn't need to be decremented as it tries to count raw stream input bytes
            }
            // end of treat possible gzip tail
            // .................................................
            if ( ret.error != Z_OK ) {
                if ( ret.error != Z_STREAM_END )
                    printToStderr( VERBOSITY_EXCESSIVE, "ERR %d: totin=%llu, totout=%llu, ftello=%llu\n", ret.error, totin, totout, ftello(file_in) );
                else
                    printToStderr( VERBOSITY_MANIAC, "Z_STREAM_END: totin=%llu, totout=%llu, ftello=%llu\n", totin, totout, ftello(file_in) );
            }

            if (ret.error == Z_NEED_DICT)
                ret.error = Z_DATA_ERROR;
            if (ret.error == Z_MEM_ERROR || ret.error == Z_DATA_ERROR) {
                goto decompress_and_build_index_error;
            }
            if (ret.error == Z_STREAM_END) {
                if ( indx_n_extraction_opts == JUST_CREATE_INDEX ||
                     indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                     indx_n_extraction_opts == EXTRACT_FROM_LINE ||
                     indx_n_extraction_opts == EXTRACT_TAIL ) {
                    // with not Supervising options, a Z_STREAM_END at feof() is correct!
                    if ( feof( file_in ) && strm.avail_in == 0 ) {
                        printToStderr( VERBOSITY_EXCESSIVE, "Correct END OF GZIP file detected at EOF.\n" );
                        gzip_eof_detected = 0; // to exit loop, as "gzip_eof_detected == 1" is one ORed condition
                                               // and now this variable is not needed anymore.
                        break;
                    }
                }
                if ( end_on_first_proper_gzip_eof == 0 )
                    gzip_eof_detected = 1;
                if ( gzip_eof_detected == 0 ) {
                    if ( end_on_first_proper_gzip_eof == 1 &&
                         feof( file_in ) ) {
                        gzip_eof_detected = 0;
                    } else {
                        gzip_eof_detected = 1;
                        printToStderr( VERBOSITY_NORMAL, "Warning: GZIP end detected in the middle of data: deactivating `-E`\n" );
                        end_on_first_proper_gzip_eof = 0;
                    }
                }
                if ( offset_in >= 8 )
                    offset_in -= 8; // data is discarded from strm input, and also from offset_in
                else
                    offset_in = 0;
                break;
            }

            //
            // if required by passed indx_n_extraction_opts option, extract to stdout:
            //
            // EXTRACT_FROM_BYTE: extract all:
            if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
                 indx_n_extraction_opts == EXTRACT_FROM_LINE ) {
                if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE ) {
                    unsigned have = avail_out_0 - strm.avail_out;
                    printToStderr( VERBOSITY_NUTS, "[>1>%llu,%d,%d,%d]", offset, have, strm.avail_out, strm.avail_in );
                    if ( offset > have ) {
                        offset -= have;
                    } else {
                        if ( ( offset > 0 && offset <= have ) ||
                            offset == 0 ) {
                            // print have - offset bytes
                            // If offset==0 (from offset byte on) this prints always all bytes:
                            output_data_counter += have - offset;
                            //
                            // also count lines, if necessary:
                            //
                            if ( offset != 0 &&   // Count lines ONLY in offset block, not in all window.
                                 NULL != index && // If lines must be counted, index passed must be !=NULL
                                                  // to mark index->index_version == 1
                                                  // and use index->line_number_format
                                 index->index_version == 1 ) {
                                // output data is in window + an offset
                                unsigned char *output_data = window + offset + (WINSIZE - avail_out_0);
                                unsigned char *pos;
                                int count = have - offset;
                                have_lines = 0;
                                do {
                                    pos = memchr( output_data, ( (0 == index->line_number_format)? '\n': '\r' ), count );
                                    if ( NULL != pos ) {
                                        count -= ( pos - output_data ) + 1;
                                        output_data = pos + 1;
                                        have_lines ++;
                                    }
                                } while ( pos != NULL && pos < (output_data + count) );
                            }
                            // if index->index_version == 0 then output_lines_counter is simply not used
                            output_lines_counter += have_lines;
                            //
                            // end of "also count lines, if necessary"
                            //
                            printToStderr( VERBOSITY_CRAZY, "[>1>%d]", have - offset );
                            if (fwrite(window + offset + (WINSIZE - avail_out_0), 1, have - offset, file_out) != (have - offset) ||
                                ferror(file_out)) {
                                (void)inflateEnd(&strm);
                                ret.error = Z_ERRNO;
                                goto decompress_and_build_index_error;
                            }
                            offset = 0;
                            fflush(file_out);
                        }
                    }
                    avail_out_0 = strm.avail_out;
                } else {
                    // indx_n_extraction_opts == EXTRACT_FROM_LINE
                    unsigned have = avail_out_0 - strm.avail_out;
                    printToStderr( VERBOSITY_NUTS, "[>3>%llu,%d,%d,%d]",
                        line_number_offset, have_lines, strm.avail_out, strm.avail_in );
                    if ( line_number_offset > have_lines ) {
                        line_number_offset -= have_lines;
                    } else {
                        if ( ( line_number_offset > 0 && line_number_offset <= have_lines ) ||
                            line_number_offset == 0 ) {
                            output_lines_counter += have_lines - line_number_offset;
                            printToStderr( VERBOSITY_CRAZY, "[>3>%d]", have_lines - line_number_offset );
                            // print have_lines - line_number_offset bytes
                            // If line_number_offset==0 (from line_number_offset byte on) this prints always all bytes:
                            if ( line_number_offset != 0 ) {
                                // calculate at which byte to start fwrite output:
                                unsigned char *output_data = window + (WINSIZE - avail_out_0);
                                unsigned char *pos;
                                int count = avail_out_0 - strm.avail_out;
                                do {
                                    pos = memchr( output_data, ( (0 == index->line_number_format)? '\n': '\r' ), count );
                                    if ( NULL != pos ) {
                                        count -= ( pos - output_data ) + 1;
                                        output_data = pos + 1;
                                        line_number_offset --;
                                    }
                                } while ( NULL != pos && line_number_offset > 0 );
                                assert ( NULL != pos && line_number_offset == 0 );
                                offset = pos - ( window + (WINSIZE - avail_out_0) ) + 1;
                            } else {
                                offset = 0;
                            }
                            output_data_counter += have - offset;
                            if (fwrite(window + offset + (WINSIZE - avail_out_0), 1, have - offset, file_out) != (have - offset) ||
                                ferror(file_out)) {
                                (void)inflateEnd(&strm);
                                ret.error = Z_ERRNO;
                                goto decompress_and_build_index_error;
                            }
                            line_number_offset = 0;
                            offset = 0;
                            fflush(file_out);
                        }
                    }
                    avail_out_0 = strm.avail_out;
                }
            } else {
                // extraction_from_offset_in marks the use of "offset_in"
                if ( extraction_from_offset_in == 1 ) {
                    unsigned have = avail_out_0 - strm.avail_out;
                    unsigned have_in = avail_in_0 - strm.avail_in;
                    avail_in_0 = strm.avail_in;
                    printToStderr( VERBOSITY_NUTS, "[>2>%llu,%d,%d]", offset_in, have_in, strm.avail_in );
                    if ( ( offset_in > 0 && offset_in <= have_in ) ||
                        offset_in <= 0 ) {
                        // print all "have" bytes as with offset_in it is not possible
                        // to know how much output discard (uncompressed != compressed)
                        output_data_counter += have;
                        printToStderr( VERBOSITY_CRAZY, "[>2>%d]", have );
                        if (fwrite(window + (WINSIZE - avail_out_0), 1, have, file_out) != have ||
                            ferror(file_out)) {
                            (void)inflateEnd(&strm);
                            ret.error = Z_ERRNO;
                            goto decompress_and_build_index_error;
                        }
                        offset_in = 0;
                        fflush(file_out);
                    } else {
                        offset_in -= have_in;
                    }
                }
                avail_out_0 = strm.avail_out;
            }

            /* if at end of block, consider adding an index entry (note that if
               data_type indicates an end-of-block, then all of the
               uncompressed data from that block has been delivered, and none
               of the compressed data after that block has been consumed,
               except for up to seven bits) -- the totout == 0 provides an
               entry point after the zlib or gzip header, and assures that the
               index always has at least one access point; we avoid creating an
               access point after the last block by checking bit 6 of data_type
             */
            if ((strm.data_type & 128) && !(strm.data_type & 64) &&
                (totout == 0 || totout - last > span)) {

                // check actual_index_point to see if we've passed
                // the end of the passed previous index, and so
                // we must addpoint() from now on :
                printToStderr( VERBOSITY_MANIAC, "actual_index_point = %llu,", actual_index_point+1 );
                printToStderr( VERBOSITY_MANIAC, "(%llu, %llu)\n", totout, last );
                if ( actual_index_point > 0 )
                    ++actual_index_point;
                if ( NULL != index &&
                    actual_index_point > (index->have - 1) ) {
                    actual_index_point = 0; // 0: actual_index_point won't be incremented or checked anymore
                }
                if ( actual_index_point == 0 &&
                    // addpoint() only if index doesn't yet exist or it is incomplete
                    ( NULL == index || index->index_complete == 0 ) &&
                    // do not add points if position is lower than the last index point available:
                    // (this can happen if -s is less now, than when the index was created!)
                    ( NULL == index ||
                       index->have == 0 || // if index->index_version == 1 index has been previously created (!=NULL)
                       ( index->have > 0 &&
                        index->list[index->have -1].in < totin &&
                        (index->list[index->have -1].out + span) <= totout ) )
                    ) {

                    if ( write_index_to_disk == 1 ) { // if `-W`, index is not written to disk, and it will also not be created/updated (!)

                        if ( NULL != index )
                            printToStderr( VERBOSITY_EXCESSIVE, "addpoint index->have = %llu, index_last_written_point = %llu\n",
                                index->have, index_last_written_point );
                        else
                            printToStderr( VERBOSITY_EXCESSIVE, "addpoint index->have = 0, index_last_written_point = %llu\n",
                                index_last_written_point );
                        index = addpoint(index, strm.data_type & 7, totin,
                                         totout, strm.avail_out, window, window_size, totlines, 1);
                        if ( NULL == index ) {
                            ret.error = Z_MEM_ERROR;
                            goto decompress_and_build_index_error;
                        }

                        // write added point!
                        // note that points written are automatically emptied of its window values
                        // in order to use as less memory as possible
                        if ( ! serialize_index_to_file( index_file, index, index_last_written_point ) )
                            goto decompress_and_build_index_error;

                    }

                    if ( NULL != index )
                        index_last_written_point = index->have;

                }

                last = totout;

            }

        } while ( strm.avail_in != 0 );

    } while ( ret.error != Z_STREAM_END || strm.avail_in > 0 || gzip_eof_detected == 1 );


    // last opportunity to output tail data
    // before deleting strm object
    if ( output_data_counter == 0 &&
        ( indx_n_extraction_opts == EXTRACT_TAIL ||
          indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ) ) {

        unsigned have = WINSIZE - strm.avail_out;

        printToStderr( VERBOSITY_EXCESSIVE, "last extraction: %d\n", have );

        if ( have > 0 ) {
            printToStderr( VERBOSITY_EXCESSIVE, "[.L>%d]", have );
            if (fwrite(strm.next_out, 1, have, file_out) != have || ferror(file_out)) {
                ret.error = Z_ERRNO;
            }
            output_data_counter += have;
        } else {
            // use backup window
            printToStderr( VERBOSITY_EXCESSIVE, "[..L>%d]", window2_size );
            if (fwrite(window2, 1, window2_size, file_out) != window2_size || ferror(file_out)) {
                ret.error = Z_ERRNO;
            }
            output_data_counter += window2_size;
        }

        fflush(file_out);

    }

    /* clean up */
    (void)inflateEnd(&strm);
    /* and return index (release unused entries in list) */
    if ( NULL != index &&
         index->have > 0 ) {
        struct point *next = realloc(index->list, sizeof(struct point) * index->have);
        if ( NULL == next ) {
            ret.error = Z_MEM_ERROR;
            ret.value = 0;
            printToStderr( VERBOSITY_EXCESSIVE, "Z_MEM_ERROR\n" );
            goto decompress_and_build_index_error;
        }
        index->list = next;
        index->size = index->have;
        index->file_size = totout; /* size of uncompressed file (useful for bgzip files) */
        if ( 0 == there_are_more_chars && totlines > 1 ) {
            totlines --;
        }
        index->number_of_lines = totlines; /* lines in uncompressed file */
    }

    // once all index values are filled, close index file: a last call must be done
    // with index_last_written_point = index->have
    if ( NULL != index &&
         index->index_complete == 0 &&
         write_index_to_disk == 1 ) {
        // use markers to detect index updates and to not write if index isn't updated
        if ( index_points_0 != index->have ||
             index_file_size_0 != index->file_size ) {
            printToStderr( VERBOSITY_EXCESSIVE, "Closing index file with %llu points and uncompressed file size of %llu bytes.\n",
                index->have, index->file_size );
            if ( ! serialize_index_to_file( index_file, index, index->have ) )
                goto decompress_and_build_index_error;
            if ( strlen(index_filename) > 0 )
                printToStderr( VERBOSITY_NORMAL, "Index written to '%s'.\n", index_filename );
            else
                printToStderr( VERBOSITY_NORMAL, "Index written to STDOUT.\n" );
            /* index is now complete */
        }
    }

    if ( NULL != index_file )
        fclose(index_file);

    // print output_data_counter info
    if ( output_data_counter > 0 ) {
        printToStderr( VERBOSITY_NORMAL, "%s (%llu Bytes) of data extracted.\n",
            giveMeSIUnits(output_data_counter, 1), output_data_counter );
    }

    // print output_lines_counter info
    if ( ( indx_n_extraction_opts == EXTRACT_FROM_LINE ||
           ( NULL != index && index->index_version > 0) ) &&
        output_lines_counter > 0 ) {
        printToStderr( VERBOSITY_NORMAL, "%llu lines extracted", output_lines_counter );
        if ( output_lines_counter > 1000 ) {
            printToStderr( VERBOSITY_NORMAL, " (%s).\n", giveMeSIUnits(output_lines_counter, 0) );
        } else {
            printToStderr( VERBOSITY_NORMAL, ".\n" );
        }
    }

    *built = index;
    if ( NULL != index )
        ret.value = index->have;
    else
        ret.value = 0;
    return ret;

    /* return error */
  decompress_and_build_index_error:

    // print output_data_counter info
    if ( output_data_counter > 0 ) {
        printToStderr( VERBOSITY_NORMAL, "%s (%llu Bytes) of data extracted.\n",
            giveMeSIUnits(output_data_counter, 1), output_data_counter );
    }

    // print output_lines_counter info
    if ( ( indx_n_extraction_opts == EXTRACT_FROM_LINE ||
           ( NULL != index && index->index_version > 0) ) &&
        output_lines_counter > 0 ) {
        printToStderr( VERBOSITY_NORMAL, "%llu lines extracted", output_lines_counter );
        if ( output_lines_counter > 1000 ) {
            printToStderr( VERBOSITY_NORMAL, " (%s).\n", giveMeSIUnits(output_lines_counter, 0) );
        } else {
            printToStderr( VERBOSITY_NORMAL, ".\n" );
        }
    }

    (void)inflateEnd(&strm);

    if ( always_create_a_complete_index == 1 &&
         NULL != index ) {
        index->file_size = totout; /* size of uncompressed file (useful for bgzip files) */
        if ( 0 == there_are_more_chars && totlines > 1 ) {
            totlines --;
        }
        index->number_of_lines = totlines; /* lines in uncompressed file */
        // return index pointer and write index to index file, ignoring the decompression error
        *built = index;
        if ( ! serialize_index_to_file( index_file, index, index->have ) )
            printToStderr( VERBOSITY_NORMAL, "ERROR whilst writing index file '%s'.\n", index_file );
    } else {
        if (index != NULL)
            free_index(index);
        *built = NULL;
    }
    if (index_file != NULL)
        fclose(index_file);
    return ret;

    // there's no need to free(here),
    // because it pointed to index's values, and they
    // will be freed with free_index().
}


// read index from a file
// INPUT:
// FILE *input_file         : input stream
// int load_windows         : 0 do not yet load windows on memory; 1 to load them
// char *file_name : file path to index file, needed in case
//                            load_windows==0, so a later extract() can access the
//                            index file again to read the window data.
//                            Can be "" if using stdin, but not NULL.
// int extend_index_with_lines: if >0 (index v1) but index is v0, operation cannot be perfomed
//                              unless the index is an empty file, in which case the index
//                              pointer is correctly initialized with corresponding ->index_version
// OUTPUT:
// struct access *index : pointer to index, or NULL on error
struct access *deserialize_index_from_file(
    FILE *input_file, int load_windows, char *file_name,
    int extend_index_with_lines
) {
    struct point here;
    struct access *index = NULL;
    uint32_t index_complete = 1;
    uint64_t number_of_index_points = 0;
    uint64_t index_have, index_size, file_size;
    uint64_t position_at_file = 0;
    char header[GZIP_INDEX_HEADER_SIZE];
    struct stat st;
    int index_version = 0;           /* 0: default; 1: index with line numbers */

    // get index file size to calculate on-the-fly how many registers are
    // in order to being able to read still-growing index files (see `-S`)
    if ( strlen(file_name) > 0 ) {
        stat(file_name, &st);
        file_size = st.st_size;
    } else {
        // for stdin use max value
        file_size = UINT64_MAX;
    }

    // check index size == 0
    if ( file_size != 0 ) {
        if (fread(header, 1, GZIP_INDEX_HEADER_SIZE, input_file) == GZIP_INDEX_HEADER_SIZE &&
            *((uint64_t *)header) == 0 ) {
            if ( strncmp(&header[GZIP_INDEX_HEADER_SIZE/2], GZIP_INDEX_IDENTIFIER_STRING, GZIP_INDEX_HEADER_SIZE/2) != 0 ) {
                if ( strncmp(&header[GZIP_INDEX_HEADER_SIZE/2], GZIP_INDEX_IDENTIFIER_STRING_V1, GZIP_INDEX_HEADER_SIZE/2) != 0 ) {
                    printToStderr( VERBOSITY_NORMAL, "ERROR: File is not a valid gzip index file.\n" );
                    return NULL;
                } else {
                    index_version = 1;
                }
            } else {
                index_version = 0;
            }
        } else {
            printToStderr( VERBOSITY_NORMAL, "ERROR: File is not a valid gzip index file.\n" );
            return NULL;
        }
    } else {
        // for an empty index, return a pointer with zero data
        index = create_empty_index();
        if ( extend_index_with_lines > 0 )
            index->index_version = 1;
        return index;
    }

    if ( 0 == index_version &&
         extend_index_with_lines > 0 ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: Existing index has no line number information.\n" );
        printToStderr( VERBOSITY_NORMAL, "ERROR: Aborting on `-[LxX]` parameter(s).\n" );
        goto deserialize_index_from_file_error;
    }

    index = create_empty_index();
    index->index_version = index_version;

    // if index v1 there's a previous register with line separator format info:
    if ( index_version == 1 ) {
        fread_endian(&(index->line_number_format), sizeof(index->line_number_format), input_file);
    }

    fread_endian(&(index_have), sizeof(index_have), input_file);
    fread_endian(&(index_size), sizeof(index_size), input_file);

    printToStderr( VERBOSITY_MANIAC, "Number of index points declared: %llu - %llu\n", index_have, index_size );

    number_of_index_points = index_have;

    // index->size equals index->have when the index file is correctly closed,
    // and index->have on disk == 0 && index->have on disk = index->size whilst the index is growing:
    if ( index_have != index_size ) {
        printToStderr( VERBOSITY_NORMAL, "Index file is incomplete.\n" );
        index_complete = 0;
        number_of_index_points = index_size;
    }

    if ( verbosity_level == VERBOSITY_EXCESSIVE )
        printToStderr( VERBOSITY_EXCESSIVE, "Number of index points declared: %llu\n", number_of_index_points );

    position_at_file = GZIP_INDEX_HEADER_SIZE +
        ( (1 == index->index_version)? sizeof(index->line_number_format): 0 ) +
        sizeof(index_have)*2;

    // read the list of points
    do {

        fread_endian(&(here.out),  sizeof(here.out),  input_file);
        fread_endian(&(here.in),   sizeof(here.in),   input_file);
        fread_endian(&(here.bits), sizeof(here.bits), input_file);
        fread_endian(&(here.window_size), sizeof(here.window_size), input_file);
        position_at_file += sizeof(here.out) + sizeof(here.in) + sizeof(here.bits) + sizeof(here.window_size);
        printToStderr( VERBOSITY_MANIAC, "#%llu: READ window_size=%d",
            ((NULL==index)? 1: index->have +1), here.window_size );

        if ( here.bits > 8 )
        {
            printToStderr( VERBOSITY_MANIAC, "\t(%p, %d, %llu, %llu, %d, %llu)\n", index, here.bits, here.in, here.out, here.window_size, file_size );
            printToStderr( VERBOSITY_EXCESSIVE, "Unexpected data found in index file '%s' @%llu.\nIgnoring data subsequent to point %llu.\n",
                    file_name, position_at_file, index_size - number_of_index_points + 1 );
            break; // exit do loop
        }

        if ( load_windows == 0 ) {
            // do not load window on here.window, but leave it on disk: this is marked with
            // a here.window_beginning to the position of the window on disk
            here.window = NULL;
            here.window_beginning = position_at_file;
            // and position into file as if read had occur
            if ( stdin == input_file ) {
                // read input until here.window_size
                uint64_t pos = 0;
                uint64_t position = here.window_size;
                unsigned char *input = malloc(CHUNK);
                if ( NULL == input ) {
                    printToStderr( VERBOSITY_NORMAL, "Not enough memory to load index from STDIN.\n" );
                    goto deserialize_index_from_file_error;
                }
                while ( pos < position ) {
                    if ( !fread(input, 1, (pos+CHUNK < position)? CHUNK: (position - pos), input_file) ) {
                        printToStderr( VERBOSITY_NORMAL, "Could not read index from STDIN.\n" );
                        goto deserialize_index_from_file_error;
                    }
                    pos += CHUNK;
                }
                free(input);
            } else {
                if ( here.window_size > 0 )
                    fseeko(input_file, here.window_size, SEEK_CUR);
            }
        } else {
            // load compressed window on memory:
            if ( here.window_size > 0 ) {
                here.window = malloc(here.window_size);
                // load window on here.window: this is marked with
                // a here.window_beginning = 0 (which is impossible with gzipindx format)
                here.window_beginning = 0;
                if (here.window == NULL) {
                    printToStderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
                    goto deserialize_index_from_file_error;
                }
                if ( !fread(here.window, here.window_size, 1, input_file) ) {
                    printToStderr( VERBOSITY_NORMAL, "Error while reading index file.\n" );
                    goto deserialize_index_from_file_error;
                }
            } else {
                here.window = NULL;
            }
        }
        position_at_file += here.window_size;

        if ( index_version == 1 ) {
            fread_endian(&(here.line_number), sizeof(here.line_number), input_file);
            position_at_file += sizeof(here.line_number);
        } else {
            here.line_number = 0;
        }

        printToStderr( VERBOSITY_MANIAC, "(%p, %d, %llu, %llu, %d, %llu, %llu)\n",
            index, here.bits, here.in, here.out, here.window_size, here.window_beginning, here.line_number );
        // increase index structure with a new point
        // (here.window can be NULL if load_windows==0)
        index = addpoint( index, here.bits, here.in, here.out, 0, here.window, here.window_size, here.line_number, 0 );

        // after increasing index, copy values which were not passed to addpoint():
        index->list[index->have - 1].window_beginning = here.window_beginning;
        // note that even if (here.window != NULL) it MUST NOT be free() here, because
        // the pointer has been copied in a point of the index structure.

        printToStderr( VERBOSITY_NUTS, "{%llu>=%llu && %llu>0}", ( file_size - position_at_file ),
            ( sizeof(here.out) + sizeof(here.in) + sizeof(here.bits) +
              sizeof(here.window_size) ), number_of_index_points -1 );

    } while (
        ( file_size - position_at_file ) >=
        // at least an empty window must enter, otherwise end loop:
        ( sizeof(here.out) + sizeof(here.in) + sizeof(here.bits) +
          sizeof(here.window_size) )
        &&
        --number_of_index_points > 0
        );


    index->file_size = 0;

    if ( index_complete == 1 ){
        /* read size of uncompressed file (useful for bgzip files) */
        /* this field may not exist (maybe useful for growing gzip files?) */
        fread_endian(&(index->file_size), sizeof(index->file_size), input_file);
        /* read number of lines in uncompressed file */
        /* this field does not exist with v0 of index file */
        fread_endian(&(index->number_of_lines), sizeof(index->number_of_lines), input_file);
    }

    index->file_name = malloc( strlen(file_name) + 1 );
    if ( NULL == index->file_name ||
         NULL == memcpy( index->file_name, file_name, strlen(file_name) + 1 ) ) {
        printToStderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
        goto deserialize_index_from_file_error;
    }

    if ( index_complete == 1 )
        index->index_complete = 1; /* index is now complete */
    else
        index->index_complete = 0;

    return index;

  deserialize_index_from_file_error:
    free_index( index );
    return NULL;

}


// Compress source stream to dest stream using zlib compression format
// that is, with 2-byte header and adler-32 crc, or raw format.
// based on def from zlib/examples/zpipe.c by Mark Adler
    /* Compress from file source to file dest until EOF on source.
       def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_STREAM_ERROR if an invalid compression
       level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
       version of the library linked do not match, or Z_ERRNO if there is
       an error reading or writing the files. */
// INPUT:
// FILE *source  : source stream
// FILE *dest    : destination stream
// int level     : level of compression
// int raw_method: 0: zlib compression (header+Adler-32)
//                 1: raw compression (no header nor tail)
// OUTPUT:
// Z_* error code or Z_OK on success
local int compress_file( FILE *source, FILE *dest, int level, int raw_method )
{
    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char *in;
    unsigned char *out;

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    if ( raw_method == 0 ) {
        ret = deflateInit(&strm, level);
    } else {
        ret = deflateInit2(
        &strm, level, Z_DEFLATED, -15,
        8, Z_DEFAULT_STRATEGY);
    }
    if (ret != Z_OK)
        return ret;

    in = malloc(CHUNK);
    out = malloc(CHUNK);

    /* compress until end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)deflateEnd(&strm);
            goto compress_file_error;
        }
        flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)deflateEnd(&strm);
                goto compress_file_error;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in file processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);
    free(in);
    free(out);
    return Z_OK;

  compress_file_error:
    free(in);
    free(out);
    return Z_ERRNO;

}


// Decompress source stream to dest stream using zlib compression format
// that is, with 2-byte header and adler-32 crc, or raw format.
// based on inf from zlib/examples/zpipe.c by Mark Adler
    /* Decompress from file source to file dest until stream ends or EOF.
       inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_DATA_ERROR if the deflate data is
       invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
       the version of the library linked do not match, or Z_ERRNO if there
       is an error reading or writing the files. */
// INPUT:
// FILE *source : source stream
// FILE *dest   : destination stream
// int raw_method: 0: zlib decompression (header+Adler-32)
//                 1: raw decompression (no header nor tail)
// OUTPUT:
// Z_* error code or Z_OK on success
local int decompress_file( FILE *source, FILE *dest, int raw_method )
{
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char *in;
    unsigned char *out;

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    if ( raw_method == 0 ) {
        ret = inflateInit(&strm);
    } else {
        ret = inflateInit2(&strm, -15);
    }
    if (ret != Z_OK)
        return ret;

    in = malloc(CHUNK);
    out = malloc(CHUNK);

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK, source);
        if (ferror(source)) {
            (void)inflateEnd(&strm);
            ret = Z_ERRNO;
            goto decompress_file_error;
        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                goto decompress_file_error;
            }
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)inflateEnd(&strm);
                ret = Z_ERRNO;
                goto decompress_file_error;
            }
        } while (strm.avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    free(in);
    free(out);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;

  decompress_file_error:
    free(in);
    free(out);
    return ret;

}


// GZIP compress the file_in stream to file_out
// creating a .gzi index to index_filename file name,
// and returning it on *built.
// Note that the index will be of minimum size as this function
// creates Z_FULL_FLUSH marks on commpression stream every span bytes,
// so the window size will be always 0 bytes. This produces indexes
// almost as small as with bgzip compression, with better compression factor.
// Compression based on def() from zlib/examples/zpipe.c by Mark Adler
    /* Compress from file source to file dest until EOF on source.
       def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
       allocated for processing, Z_STREAM_ERROR if an invalid compression
       level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
       version of the library linked do not match, or Z_ERRNO if there is
       an error reading or writing the files. */
// INPUT:
// FILE *file_in : source stream
// FILE *file_out   : destination stream
// int level    : level of compression
// char *file_name : name of the input file associated with FILE *in.
//                            Can be "" (no file name: stdin used), but not NULL.
//                            Used only if input (FILE *in)
//                            is associated with a file (not stdin) to detect a
//                            possible overwriting of the source file (and then stop).
// uint64_t span    : span
// struct access **built: address of index pointer, equivalent to passed by reference.
//                        Note that *built == NULL on call ALWAYS, because index is
//                        constructed from scratch here.
// char *index_filename  : index will be read/written on-the-fly
//                                  to this index file name.
// int write_index_to_disk  : 1: will write/update the index as usual;
//                            0: do not write the index on disk,
//                           and also, do not create/update the index in memory (**built and returned_output.value):
//                           This is done because if index is not written to disk, windows would need to be
//                           maintained in memory, increasing the memory footprint unnecessarily as the index
//                           is not (actually) used later in the app. (Windows could be also emptied, but again,
//                           index points are not used later.)
// int end_on_first_eof : end file processing on first feof():
//                        to be used when we are not compressing a growing file.
// int always_create_a_complete_index : create a 'complete' index file even in case of compressing errors.
//                                      Also an index pointer (**built) is returned, instead of NULL.
// int waiting_time     : waiting time in seconds between reads when `-[S]`
// int extend_index_with_lines  : 0: create index without line numbers (v0 index)
//                                1: create index WITH line numbers (v1 index) using Unix format (\n)
//                                2: create index WITH line numbers (v1 index) using old Mac format (\r) (compatible with Windows \n\r)
// OUTPUT:
// struct returned_output: contains two values:
//      .error: Z_* error code or Z_OK if everything was ok
//      .value: size of built index (index->have)
local struct returned_output compress_and_build_index(
    FILE *file_in, FILE *file_out, int level, char *file_name, uint64_t span,
    struct access **built, char *index_filename, int write_index_to_disk,
    int end_on_first_eof, int always_create_a_complete_index,
    int waiting_time, int extend_index_with_lines )
{
    struct returned_output ret;
    uint64_t last   = 0;           /* uncompressed byte position of last access point */
    struct access *index = NULL;/* access points being generated */
    uint64_t totin  = 0;          // counts uncompressed bytes read from file_in
    uint64_t totout = 0;          // counts compressed bytes deflated to file_out
    uint64_t totlines = 1;        // counts total line numbers in file_in
    int there_are_more_chars = 0; // totlines may need to be decremented at the end depending on last stream char
    int flush = Z_NO_FLUSH;
    unsigned have;
    z_stream strm;
    FILE *index_file = NULL;
    size_t index_last_written_point = 0;
    unsigned char *input;
    unsigned char *output;

    ret.value = 0;
    ret.error = Z_OK;

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret.error = deflateInit2( //www.zlib.net/manual.html
                //github.com/madler/zlib/blob/master/zlib.h
                // Add 16 to windowBits to write a simple gzip header and trailer
                // around the compressed data instead of a zlib wrapper
        &strm, level, Z_DEFLATED, 15 + 16,
        8, Z_DEFAULT_STRATEGY);
    if (ret.error != Z_OK)
        return ret;

    input = malloc(CHUNK);
    output = malloc(CHUNK);

    if ( extend_index_with_lines > 0 ) {
        // mark index as index_version = 1 to store line numbers when serialize();
        // in order to do this, index must be created now (empty)
        index = create_empty_index();
        if ( index == NULL ) { // Oops!?
            ret.error = Z_MEM_ERROR;
            return ret;
        }
        index->index_version = 1;
        if ( extend_index_with_lines == 2 )
            index->line_number_format = 1;
        else
            index->line_number_format = 0;
    }

    /* open index_filename for binary writing */
    // index_filename DOES NOT previously exists: this has has been checked on caller
    if ( strlen(index_filename) > 0 ) {
        if ( write_index_to_disk == 1 ) {
            printToStderr( VERBOSITY_EXCESSIVE, "write_index_to_disk = %d", write_index_to_disk );
            if ( access( index_filename, F_OK ) != -1 ) {
                // index_filename already exist:
                // Abort, as we would overwrite it:
                printToStderr( VERBOSITY_NORMAL, "Index file '%s' already exist. Aborted.\n", index_filename );
                goto compress_and_build_index_error;
            } else {
                // index_filename does not exist:
                index_file = fopen( index_filename, "w+b" );
                printToStderr( VERBOSITY_EXCESSIVE, " (w+b)\n" );
            }
        }
    } else {
        SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
        index_file = stdout;
    }
    if ( NULL == index_file && write_index_to_disk == 1 ) {
        printToStderr( VERBOSITY_NORMAL, "Could not write index to file '%s'.\n", index_filename );
        goto compress_and_build_index_error;
    }

    /* compress until end of file */
    do {

        // check if a new index point must be created
        if ( // create an index point at uncompressed byte 0 (just before data)
             ( totin == 0 &&
               index_last_written_point == 0 )
             ||
             // create index points after Z_FULL_FLUSH marks on compressed data
             ( totin - last >= span &&
               Z_FULL_FLUSH == flush )
            ) {

            // write index point
            if ( write_index_to_disk == 1 ) { // if `-W`, index is not written to disk, and it will also not be created/updated (!)
                if ( NULL != index )
                    printToStderr( VERBOSITY_EXCESSIVE, "addpoint index->have = %llu, index_last_written_point = %llu\n",
                        index->have, index_last_written_point );
                else
                    printToStderr( VERBOSITY_EXCESSIVE, "addpoint index->have = 0, index_last_written_point = %llu\n",
                        index_last_written_point );
                index = addpoint(index, 0,
                                 ( ( 0==totout )? GZIP_HEADER_SIZE_BY_ZLIB: totout ), // compressed byte after header <=> uncompressed byte 0
                                 totin, 0, NULL, 0, totlines, 0);

                // write added point!
                // note that points written are automatically emptied of its window values
                // in order to use as less memory a s possible
                if ( ! serialize_index_to_file( index_file, index, index_last_written_point ) )
                    goto compress_and_build_index_error;
            }

            if ( NULL != index )
                index_last_written_point = index->have;

            last = totin;

        }

        strm.avail_in = fread(input, 1, CHUNK, file_in);
        printToStderr( VERBOSITY_MANIAC, "[read %d B]", strm.avail_in );
        totin += strm.avail_in;
        // count lines in this source chunk
        if ( extend_index_with_lines > 0 ) {
            // output data is in window + an offset
            unsigned char *output_data = input;
            unsigned char *pos;
            int count = strm.avail_in;
            do {
                pos = memchr( output_data, ( (extend_index_with_lines == 1)? '\n': '\r' ), count );
                if ( NULL != pos ) {
                    totlines ++;
                    count -= ( pos - output_data ) + 1;
                    if ( count > 0 )
                        there_are_more_chars = 1;
                    else
                        there_are_more_chars = 0;
                    output_data = pos + 1;
                }
            } while ( pos != NULL && pos < (output_data + count) );
        }

        if ( ferror(file_in) ) {
            (void)deflateEnd(&strm);
            goto compress_and_build_index_error;
        }

        if ( feof( file_in ) ) {
            if ( end_on_first_eof == 1 )
                flush = Z_FINISH;
            else
                flush = Z_SYNC_FLUSH; // Z_SYNC_FLUSH doesn't occupy space
        } else {
            flush = Z_NO_FLUSH;
        }
        if ( totin - last >= span ) {
            // create a FLUSH point for an index point with window of size zero
            // With gzip compression this should not affect compression performance
            flush = Z_FULL_FLUSH;
        }

        strm.next_in = input;

        /* run deflate() on input until output buffer not full, finish
           compression if all of file_in has been read in */
        do {

            strm.avail_out = CHUNK;
            strm.next_out = output;
            printToStderr( VERBOSITY_NUTS, "(avail_in=%d),",
                strm.avail_in );
            ret.error = deflate(&strm, flush);    /* no bad return value */
            assert(ret.error != Z_STREAM_ERROR);  /* state not clobbered */

            have = CHUNK - strm.avail_out;
            totout += have;

            printToStderr( VERBOSITY_NUTS, "flush=%d,have=%d,avail_out=%d,",
                flush, have, strm.avail_out );
            printToStderr( VERBOSITY_CRAZY, "totin=%llu,totout=%llu,ftello=%llu,avail_in=%d,",
                totin, totout, ftello(file_in), strm.avail_in );

            if (fwrite(output, 1, have, file_out) != have || ferror(file_out)) {
                (void)deflateEnd(&strm);
                goto compress_and_build_index_error;
            }

            // flush written content to disk
            //fflush( file_out );

        } while ( strm.avail_out == 0 );
        assert(strm.avail_in == 0);     /* all input will be used */

        // generic check for growing files:
        // check that file hasn't shrunk, what would mean that the file
        // has been overwritten from the beginning (possible with logs, for example)
        if ( feof( file_in ) &&
             end_on_first_eof == 0 ) { // if end_on_first_eof == 1, ret.error == Z_STREAM_END already
            if ( strlen( file_name ) > 0 ) {    // this check cannot be done on STDIN
                struct stat st;
                stat(file_name, &st);
                printToStderr( VERBOSITY_NUTS, "(%llu<%llu?)", st.st_size, totin );
                if ( (uint64_t) st.st_size < totin ) {
                    // file has shrunk! so do a correct finish of the action_create_index (whatever it is)
                    // (Note that it is not possible that this condition arises when accessing a file
                    // with size 0 (which is possible and allowed) because (0 < 0) is false )
                    printToStderr( VERBOSITY_EXCESSIVE, "\nDetected '%s' file overwriting, so ending process\n", file_name );
                    end_on_first_eof = 1;
                    // loop will continue and because feof() will activate again (because source file
                    // won't be overwritten so quick as to surpase actual total_in bytes) a Z_FINISH
                    // will be written, and a Z_STREAM_END will be returned to end the process.
                }
            }
        }

        if ( end_on_first_eof == 0 &&
             strm.avail_in == 0 &&
             feof( file_in ) ) {
            // flush written content to disk
            fflush( file_out );
            // wait for file to grow
            sleep( waiting_time );
            clearerr( file_in );
        }

        /* done when last data in file processed */

    } while ( flush != Z_FINISH );

    assert(ret.error == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);

    /* and return index (release unused entries in list) */
    if ( NULL != index &&
         index->have > 0 ) {
        struct point *next = realloc(index->list, sizeof(struct point) * index->have);
        if ( NULL == next ) {
            ret.error = Z_MEM_ERROR;
            ret.value = 0;
            printToStderr( VERBOSITY_EXCESSIVE, "Z_MEM_ERROR\n" );
            goto compress_and_build_index_error;
        }
        index->list = next;
        index->size = index->have;
        index->file_size = totin; /* size of uncompressed file */
        if ( 0 == there_are_more_chars && totlines > 1 ) {
            totlines --;
        }
        index->number_of_lines = totlines; /* lines in uncompressed file */
    }

    // once all index values are filled, close index file: a last call must be done
    // with index_last_written_point = index->have
    if ( NULL != index &&
         index->index_complete == 0 &&
         write_index_to_disk == 1 ) {
        printToStderr( VERBOSITY_EXCESSIVE, "Closing index file with %llu points and uncompressed file size of %llu bytes.\n",
            index->have, index->file_size );
        if ( ! serialize_index_to_file( index_file, index, index->have ) )
            goto compress_and_build_index_error;
        if ( strlen(index_filename) > 0 )
            printToStderr( VERBOSITY_NORMAL, "Index written to '%s'.\n", index_filename );
        else
            printToStderr( VERBOSITY_NORMAL, "Index written to STDOUT.\n" );
        /* index is now complete */
    }

    if ( NULL != index_file )
        fclose(index_file);

    // print totin info
    if ( totin > 0 ) {
        printToStderr( VERBOSITY_NORMAL, "%s (%llu Bytes) of data compressed.\n",
            giveMeSIUnits(totin, 1), totin );
        if ( extend_index_with_lines > 0 &&
            totlines > 0 ) {
            printToStderr( VERBOSITY_NORMAL, "%llu lines compressed.\n", totlines );
            if ( totlines > 1000 ) {
                printToStderr( VERBOSITY_NORMAL, " (%s).\n", giveMeSIUnits(totlines, 0) );
            } else {
                printToStderr( VERBOSITY_NORMAL, ".\n" );
            }
        }
        if ( totout > 0 )
            printToStderr( VERBOSITY_NORMAL, "%llu bytes of gzip data (%.2f%%).\n",
                totout, 100.0 - (double)totout / (double)totin * 100.0 );
    }

    *built = index;
    if ( NULL != index )
        ret.value = index->have;
    else
        ret.value = 0;

    free(input);
    free(output);

    return ret;

  compress_and_build_index_error:
    free(input);
    free(output);

    // flush written content to disk
    fflush( file_out );

    // print totin info
    if ( totin > 0 ) {
        printToStderr( VERBOSITY_NORMAL, "%s (%llu Bytes) of data compressed.\n",
            giveMeSIUnits(totin, 1), totin );
        if ( extend_index_with_lines > 0 &&
            totlines > 0 ) {
            printToStderr( VERBOSITY_NORMAL, "%llu lines compressed", ( (0 == there_are_more_chars)? totlines-1: totlines ) );
            if ( totlines > 1000 ) {
                printToStderr( VERBOSITY_NORMAL, " (%s).\n", giveMeSIUnits(totlines, 0) );
            } else {
                printToStderr( VERBOSITY_NORMAL, ".\n" );
            }
        }
        if ( totout > 0 )
            printToStderr( VERBOSITY_NORMAL, "%s (%llu Bytes) of gzip data (%.2f%%).\n",
                giveMeSIUnits(totout, 1), totout, 100.0 - (double)totout / (double)totin * 100.0 );
    }

    (void)deflateEnd(&strm);

    if ( always_create_a_complete_index == 1 ) {
        if ( NULL != index ) {
            index->file_size = totin; /* size of uncompressed file */
            if ( 0 == there_are_more_chars && totlines > 1 ) {
                totlines --;
            }
            index->number_of_lines = totlines; /* lines in uncompressed file */
            // return index pointer and write index to index file, ignoring the compression error
            *built = index;
            if ( write_index_to_disk == 1 ) {
                if ( ! serialize_index_to_file( index_file, index, index->have ) )
                    printToStderr( VERBOSITY_NORMAL, "ERROR whilst writing index file '%s'.\n", index_file );
            }
        }
    } else {
        if ( NULL != index )
            free_index(index);
        *built = NULL;
    }

    if ( NULL != index_file )
        fclose(index_file);
    if ( Z_OK == ret.error )
        ret.error = Z_ERRNO;
    return ret;

}


// Creates an index for a gzip file.
// If index file already exists, it is completed if it weren't complete,
// or directly used if it is complete.
// INPUT:
// char *file_name     : file name of gzip file for which index will be calculated,
//                                or that will be decompressed, depending on indx_n_extraction_opts.
//                                If strlen(file_name) == 0 stdin is used as gzip file.
// struct access **index        : memory address of index pointer (passed by reference)
// char *index_filename: file name where index will be written
//                                If strlen(index_filename) == 0 stdout is used as output for index.
// enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts:
//                                  value passed to decompress_and_build_index();
//                                  in case of SUPERVISE_DO* (not *DONT), wait here until gzip file exists.
// uint64_t offset                 : if supervise == EXTRACT_FROM_BYTE, this is the offset byte in
//                                the uncompressed stream from which to extract to stdout.
//                                0 otherwise.
// uint64_t line_number_offset  : if indx_n_extraction_opts == EXTRACT_FROM_LINE, this is the offset line
//                                in the uncompressed stream from which to extract to stdout.
//                                0 otherwise.
// uint64_t span_between_points    : span between index points in bytes
// int write_index_to_disk      : 1: will write/update the index as usual;
//                                0: will just read, but do not write nor update (overwrite) it
// int end_on_first_proper_gzip_eof: end file processing on first proper (at feof()) GZIP EOF
//                                   (to be used when the file contains surely only one gzip stream)
// int always_create_a_complete_index : create a 'complete' index file even in case of decompressing errors.
//                                      Also an index pointer (**built) is returned, instead of NULL.
// int waiting_time             : waiting time in seconds between reads when `-[ST]`
// int force_action             : with `-[cd]`, force destination file overwriting if it exists
// int wait_for_file_creation   : 0: exit with error if source file doesn't exist
//                                1: wait for file creation if it doesn't yet exist, when using `-[STcd]`
// int extend_index_with_lines  : create index with (1,2) or without (0) line numbers (v0 or v1 index):
//                                decision is make at de/compress_and_build_index(), not here.
// uint64_t expected_first_byte  : indicates that the first byte on compressed input is this, not 1,
//                                and so truncated compressed inputs can be used. Only can be used (>1) if
//                                indx_n_extraction_opts is EXTRACT_FROM_BYTE or EXTRACT_FROM_LINE.
//                                Directly passed here to decompress_and_build_index(), w/o further processing.
// OUTPUT:
// EXIT_* error code or EXIT_OK on success
local int action_create_index(
    char *file_name, struct access **index,
    char *index_filename, enum INDEX_AND_EXTRACTION_OPTIONS indx_n_extraction_opts,
    uint64_t offset, uint64_t line_number_offset, uint64_t span_between_points, int write_index_to_disk,
    int end_on_first_proper_gzip_eof, int always_create_a_complete_index,
    int waiting_time, int force_action, int wait_for_file_creation,
    int extend_index_with_lines, uint64_t expected_first_byte )
{

    FILE *file_in  = NULL;
    FILE *file_out = NULL;
    struct returned_output ret;
    uint64_t number_of_index_points = 0;
    int waiting = 0;

    // First of all, check that data output and index output do not collide:
    if ( strlen(file_name) == 0 &&
         strlen(index_filename) == 0 &&
         ( indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
           indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
           indx_n_extraction_opts == EXTRACT_FROM_LINE ||
           indx_n_extraction_opts == EXTRACT_TAIL )
        ) {
        // input is stdin, output is stdout, and no file name has been
        // indicated for index output, so action is not possible:
        printToStderr( VERBOSITY_NORMAL, "ERROR: Please, note that extracted data will be output to STDOUT\n" );
        printToStderr( VERBOSITY_NORMAL, "       so an index file name is needed (`-I`).\nAborted.\n" );
        return EXIT_GENERIC_ERROR;
    }

    // open <FILE>:
    if ( strlen( file_name ) > 0 ) {
action_create_index_wait_for_file_creation:
        file_in = fopen( file_name, "rb" );
        if ( NULL == file_in ) {
            if ( wait_for_file_creation == 1 &&
                 ( indx_n_extraction_opts == SUPERVISE_DO ||
                   indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL ||
                   indx_n_extraction_opts == COMPRESS_AND_CREATE_INDEX ||
                   indx_n_extraction_opts == DECOMPRESS ) ) {
                if ( waiting == 0 ) {
                    printToStderr( VERBOSITY_NORMAL, "Waiting for creation of file '%s'\n", file_name );
                    waiting = 1;
                }
                sleep( waiting_time );
                goto action_create_index_wait_for_file_creation;
            }
            printToStderr( VERBOSITY_NORMAL, "Could not open '%s' for reading.\nAborted.\n", file_name );
            return EXIT_GENERIC_ERROR;
        }
        printToStderr( VERBOSITY_NORMAL, "Processing '%s' ...\n", file_name );
    } else {
        // stdin
        SET_BINARY_MODE(STDIN); // sets binary mode for stdin in Windows
        file_in = stdin;
        printToStderr( VERBOSITY_NORMAL, "Processing STDIN ...\n" );
    }

    if ( indx_n_extraction_opts == COMPRESS_AND_CREATE_INDEX ) {
        // compression !

        // an output filename is necessary:
        // this block directly provides de FILE *file_out pointer
        if ( strlen(file_name) > 0 ) {
            char *out_filename;
            out_filename = malloc( strlen(file_name) + 3 + 1 );
            sprintf( out_filename, "%s.gz", file_name );
            // check that destination file doesn't exist
            if ( access( out_filename, F_OK ) != -1 ) {
                if ( 0 == force_action ) {
                    printToStderr( VERBOSITY_NORMAL, "Compress destination file '%s' already exist.\nAborted\n", out_filename );
                    free( out_filename );
                    return EXIT_GENERIC_ERROR;
                } else {
                    printToStderr( VERBOSITY_NORMAL, "Using `-f` force option: Deleting '%s' ...\n", out_filename );
                }
            }
            file_out = fopen( out_filename, "w+b" );
            if ( NULL == file_out ) {
                printToStderr( VERBOSITY_NORMAL, "Could not open '%s' for writing.\nAborted.\n", out_filename );
                free( out_filename );
                return EXIT_GENERIC_ERROR;
            }
            printToStderr( VERBOSITY_NORMAL, "Compressing to '%s'\n", out_filename );
            free( out_filename );
        } else {
            // stdin
            // If using STDIN, the output will be STDOUT
            SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
            file_out = stdout;
            printToStderr( VERBOSITY_NORMAL, "Compressing to STDOUT\n" );
        }

        ret = compress_and_build_index( file_in, file_out, Z_DEFAULT_COMPRESSION,
                file_name, span_between_points, index, index_filename,
                write_index_to_disk,
                ((end_on_first_proper_gzip_eof==0)?1:0), // `-E` behaviour is inverted with `-c` for ease of use
                always_create_a_complete_index, waiting_time,
                extend_index_with_lines );

        if ( NULL != index && NULL != *index ) {
            // it is important to set (*index)->file_name, if the index is to be reused,
            // because the window data must be read from disk to be used.
            if ( NULL == (*index)->file_name ) {
                (*index)->file_name = malloc( strlen(index_filename) + 1 );
                if ( NULL == (*index)->file_name ||
                     NULL == memcpy( (*index)->file_name, index_filename, strlen(index_filename) + 1 ) ) {
                    printToStderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
                    if ( NULL != (*index)->file_name )
                        free( (*index)->file_name );
                    return EXIT_GENERIC_ERROR;
                }
            }
        }

    } else {
        // decompression!

        // compute index:
        // if index_filename already exist, load it and use it
        // (if it is complete, it'll be used directly, if not it'll be
        // completed from last point - all in decompress_and_build_index() ).
        if ( strlen( index_filename ) > 0 &&
             access( index_filename, F_OK ) != -1 &&
             indx_n_extraction_opts != DECOMPRESS // DECOMPRESS does not require index
            ) {
            if ( NULL == index || NULL == (*index) ) {
                // index_filename already exist: try to load it
                FILE *index_file;
                index_file = fopen( index_filename, "rb" );
                if ( NULL != index_file ) {
                    *index = deserialize_index_from_file( index_file, 0, index_filename, extend_index_with_lines );
                    fclose( index_file );
                    if ( NULL == *index ) {
                        printToStderr( VERBOSITY_NORMAL, "Could not load index from file '%s'.\nAborted.\n", index_filename );
                        return EXIT_GENERIC_ERROR;
                    }
                    // index ok, continue
                    number_of_index_points = (*index)->have;
                    (*index)->index_complete = 0; // every index can be updated with new points in case gzip data grows!
                } else {
                    printToStderr( VERBOSITY_NORMAL, "Could not open '%s' for reading.\nAborted.\n", index_filename );
                    return EXIT_GENERIC_ERROR;
                }
            } else {
                number_of_index_points = (*index)->have;
                (*index)->index_complete = 0; // every index can be updated with new points in case gzip data grows!
            }
            // there's an index now, continue:
            // it is important to set (*index)->file_name, if the index is to be reused,
            // because the window data must be read from disk to be used.
            if ( NULL == (*index)->file_name ) {
                (*index)->file_name = malloc( strlen(index_filename) + 1 );
                if ( NULL == (*index)->file_name ||
                     NULL == memcpy( (*index)->file_name, index_filename, strlen(index_filename) + 1 ) ) {
                    printToStderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
                    if ( NULL != (*index)->file_name )
                        free( (*index)->file_name );
                    return EXIT_GENERIC_ERROR;
                }
            }

        }

        // checks on index read from file:
        if ( NULL != (*index) &&
             strlen( file_name ) > 0 ) {
            // (here, index_filename exists and index exists)
            check_index_file( (*index), file_name, index_filename );
            // returned value is not used - only warn user and continue
        }

        // stdout to binary mode if needed
        if ( indx_n_extraction_opts == EXTRACT_FROM_BYTE ||
             indx_n_extraction_opts == EXTRACT_FROM_LINE ||
             indx_n_extraction_opts == EXTRACT_TAIL ||
             indx_n_extraction_opts == SUPERVISE_DO_AND_EXTRACT_FROM_TAIL
             ) {
            SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
        }

        if ( indx_n_extraction_opts == DECOMPRESS ) {
            // file must have ".gz" extension so it can be decompressed
            if ( strlen( file_name ) > 3 && // avoid out-of-bounds
                 (char *)strstr(file_name, ".gz") ==
                 (char *)(file_name + strlen(file_name) - 3)
                ) {
                // if gzip-file name is 'FILE.gz', output file name will be 'FILE'
                char *output_filename = malloc( strlen(file_name) );
                sprintf(output_filename, "%s", file_name);
                output_filename[strlen(file_name) - 3] = '\0';
                printToStderr( VERBOSITY_NORMAL, "Decompressing to '%s'\n", output_filename );
                // now, if output_filename already exist, we cannot proceed...
                if ( access( output_filename, F_OK ) != -1 ) {
                    if ( force_action == 0 ) {
                        printToStderr( VERBOSITY_NORMAL, "Output file name '%s' already exists.\nAborted.\n", output_filename );
                        free ( output_filename );
                        return EXIT_GENERIC_ERROR;
                    }
                }
                // destination file will be overwritten:
                file_out = fopen( output_filename, "wb" );
                if ( NULL == file_out ) {
                    printToStderr( VERBOSITY_NORMAL, "Could not open '%s' for writing.\nAborted.\n", output_filename );
                    free ( output_filename );
                    return EXIT_GENERIC_ERROR;
                }
                free( output_filename );
            } else {
                // otherwise decompression cannot proceed
                printToStderr( VERBOSITY_NORMAL, "File '%s' does not have '.gz' extension.\nAborted.\n", file_name );
                return EXIT_GENERIC_ERROR;
            }

            ret = decompress_and_build_index( file_in, file_out, file_name, span_between_points, index,
                    DECOMPRESS, offset, line_number_offset, index_filename, 0, // write_index_to_disk = 0
                    1 , always_create_a_complete_index, // end_on_first_proper_gzip_eof = 1
                    waiting_time, extend_index_with_lines, 1 ); // expected_first_byte not used, so 1

        } else {
            file_out = stdout;

            ret = decompress_and_build_index( file_in, file_out, file_name, span_between_points, index,
                    indx_n_extraction_opts, offset, line_number_offset, index_filename, write_index_to_disk,
                    end_on_first_proper_gzip_eof, always_create_a_complete_index,
                    waiting_time, extend_index_with_lines, expected_first_byte );
        }

        if ( NULL != index && NULL != *index ) {
            // it is important to set (*index)->file_name, if the index is to be reused,
            // because the window data must be read from disk to be used.
            if ( NULL == (*index)->file_name ) {
                (*index)->file_name = malloc( strlen(index_filename) + 1 );
                if ( NULL == (*index)->file_name ||
                     NULL == memcpy( (*index)->file_name, index_filename, strlen(index_filename) + 1 ) ) {
                    printToStderr( VERBOSITY_NORMAL, "Not enough memory to load index from file.\n" );
                    if ( NULL != (*index)->file_name )
                        free( (*index)->file_name );
                    return EXIT_GENERIC_ERROR;
                }
            }
        }

    }

    if ( file_in != stdin )
        fclose(file_in);

    if ( NULL != file_out &&
         file_out != stdout )
        fclose( file_out );

    if ( ret.error < 0 ) {
        switch ( ret.error ) {
            case Z_MEM_ERROR:
                printToStderr( VERBOSITY_NORMAL, "ERROR: Out of memory.\n" );
                break;
            case Z_DATA_ERROR:
                if ( strlen(file_name) > 0 )
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Compressed data error in '%s'.\n", file_name );
                else
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Compressed data error in STDIN.\n" );
                break;
            case Z_ERRNO:
                if ( strlen(file_name) > 0 )
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Read error on '%s'.\n", file_name );
                else
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Read error on STDIN.\n" );
                break;
            default:
               printToStderr( VERBOSITY_NORMAL, "ERROR: Error %d while applying action.\n", ret.error );
       }
       return EXIT_GENERIC_ERROR;
    }

    if ( NULL != index && NULL != *index &&
         number_of_index_points != (*index)->have && write_index_to_disk == 1 ) {
        if ( number_of_index_points > 0 ) {
            printToStderr( VERBOSITY_NORMAL, "Updated index with %llu new access points.\n", ret.value - number_of_index_points);
            printToStderr( VERBOSITY_NORMAL, "Now index has %llu access points.\n", ret.value);
        } else {
            printToStderr( VERBOSITY_NORMAL, "Built index with %llu access points.\n", ret.value);
        }
    }

    return EXIT_OK;

}


// list info for an index file, to stdout
// May be called with global verbosity_level == VERBOSITY_NONE in which case
// no info is printed to stdout but a value is returned.
// INPUT:
// char *file_name           : index file name. Maybe "" for STDIN.
// char *input_gzip_filename : gzip file associated with the index. May be NULL, but not "".
// enum VERBOSITY_LEVEL list_verbosity: level of detail of index checking:
//                                      minimum is 1 == VERBOSITY_NORMAL
// OUTPUT:
// EXIT_* error code or EXIT_OK on success
local int action_list_info( char *file_name, char *input_gzip_filename, enum VERBOSITY_LEVEL list_verbosity ) {

    FILE *in = NULL;
    struct access *index = NULL;
    uint64_t j;
    int ret_value = EXIT_OK;
    struct stat st;
    char *gzip_filename = NULL;
    struct stat st_gzip;
    st_gzip.st_size = 0;
    uint64_t comp_win_counter = 0;   // just to count bytes and calculate a file estimated "hardness"
    uint64_t uncomp_win_counter = 0; // just to count bytes and calculate a file estimated "hardness"

    // check input_gzip_filename
    if ( NULL != input_gzip_filename ) {
        gzip_filename = malloc( strlen(input_gzip_filename) + 1 );
        if ( NULL == gzip_filename )
            return EXIT_GENERIC_ERROR;
        sprintf( gzip_filename, "%s", input_gzip_filename );
    }

    // open index file:
    if ( strlen( file_name ) > 0 ) {
        if ( verbosity_level > VERBOSITY_NONE )
            fprintf( stdout, "Checking index file '%s' ...\n", file_name );
        in = fopen( file_name, "rb" );
        if ( NULL == in ) {
            printToStderr( VERBOSITY_NORMAL, "Could not open %s for reading.\nAborted.\n", file_name );
            return EXIT_GENERIC_ERROR;
        }
    } else {
        // stdin
        printToStderr( VERBOSITY_NORMAL, "Checking index from STDIN ...\n" );
        SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
        in = stdin;
    }

    // in case in == stdin, file_name == ""
    // but this doesn't matter as windows won't be inflated from disk, but from memory with 2nd parameter == 1.
    index = deserialize_index_from_file( in, ((list_verbosity==VERBOSITY_MANIAC)?1:0), file_name, 0 );

    if ( NULL != index &&
         strlen( file_name ) > 0 ) {
        stat( file_name, &st );
        if ( verbosity_level > VERBOSITY_NONE )
            fprintf( stdout, "\tSize of index file (v%d)  : %s (%llu Bytes)",
                index->index_version, giveMeSIUnits(st.st_size, 1), (long long unsigned)st.st_size );

        if ( st.st_size > 0 &&
             list_verbosity > VERBOSITY_NONE ) {
            // try to obtain the name of the original gzip data file
            // to calculate the ratio index_file_size / gzip_file_size

            if ( NULL == gzip_filename ) {
                gzip_filename = malloc( strlen(file_name) + 1 );
                if ( NULL != gzip_filename ) {
                    sprintf( gzip_filename, "%s", file_name );
                    if ( strlen( file_name ) > 4 && // avoid out-of-bounds
                         (char *)strstr(gzip_filename, ".gzi") ==
                            (char *)(gzip_filename + strlen(file_name) - 4) ) {
                        // if index file name is 'FILE.gzi', gzip-file name should be 'FILE.gz'
                        gzip_filename[strlen(gzip_filename)-1]='\0';
                        // let's see if we're certain:
                        if ( access( gzip_filename, F_OK ) != -1 ) {
                            // we've found the gzip-file name !
                            ;
                        } else {
                            // we haven't found it: may be the name is of the format "FILE.another_extension.gzi"
                            gzip_filename[strlen(gzip_filename)-3]='\0';
                            if ( access( gzip_filename, F_OK ) != -1 ) {
                                // we've found the gzip-file name !
                                ;
                            } else {
                                // we must give up guessing names here
                                free( gzip_filename );
                                gzip_filename = NULL;
                            }
                        }
                    } else {
                        // if ".gzi" doesn't fit as extension, we're up guessing!
                        free( gzip_filename );
                        gzip_filename = NULL;
                    }
                }
            }

            if ( NULL != gzip_filename ) {
                if( access( gzip_filename, F_OK ) != -1 ) {
                    check_index_file( index, gzip_filename, file_name );
                    stat( gzip_filename, &st_gzip );
                    if ( st_gzip.st_size > 0 &&
                         verbosity_level > VERBOSITY_NONE) {
                        fprintf( stdout, " (%.2f%%/gzip)\n", (double)st.st_size / (double)st_gzip.st_size * 100.0 );
                        fprintf( stdout, "\tGuessed gzip file name   : '%s'", gzip_filename );
                        if ( index->file_size > 0 ) {
                            fprintf( stdout, " (%.2f%%)",
                                100.0 - (double)st_gzip.st_size / (double)index->file_size * 100.0 );
                        }
                        fprintf( stdout, " %s (%llu Bytes)",
                            giveMeSIUnits(st_gzip.st_size, 1), (long long unsigned)st_gzip.st_size );
                    }
                } else {
                    if ( verbosity_level > VERBOSITY_NONE )
                        fprintf( stdout, "\n\tIndicated gzip file '%s' doesn't exist", gzip_filename );
                }
            }

        }

        if ( verbosity_level > VERBOSITY_NONE )
            fprintf( stdout, "\n" );

    }

    if ( ! index ) {

        if ( strlen( file_name ) > 0 ) {
            printToStderr( VERBOSITY_NORMAL, "Could not read index from file '%s'.\n", file_name );
        } else {
            printToStderr( VERBOSITY_NORMAL, "Could not read index from STDIN.\n" );
        }
        ret_value = EXIT_GENERIC_ERROR;
        goto action_list_info_error;

    } else {

        if ( verbosity_level > VERBOSITY_NONE ) {
            fprintf( stdout, "\tNumber of index points   : %llu", (long long unsigned)index->have );
            if ( index->have > 1000 ) {
                fprintf( stdout, " (%s)\n", giveMeSIUnits(index->have, 0) );
            } else {
                fprintf( stdout, "\n" );
            }
        }

        if ( verbosity_level > VERBOSITY_NONE ) {

            // print uncompressed file size
            if ( index->file_size != 0 ) {
                fprintf( stdout, "\tSize of uncompressed file: %s (%llu Bytes)\n",
                    giveMeSIUnits(index->file_size, 1), (long long unsigned)index->file_size );
            } else {
                if ( index->have > 1 ) {
                    fprintf( stdout, "\tSize of uncompressed file > %s (%llu Bytes)\n",
                        giveMeSIUnits(index->list[index->have -1].out, 1),
                        (long long unsigned)index->list[index->have -1].out );
                }
            }

            // print number of lines in uncompressed data
            if ( 1 == index->index_version ) {
                uint64_t local_number_of_lines = 0;
                fprintf( stdout, "\tNumber of lines          " );
                if ( 1 == index->index_complete ) {
                    local_number_of_lines = index->number_of_lines;
                    fprintf( stdout, ":" );
                } else {
                    if ( index->have > 1 ) {
                        local_number_of_lines = index->list[index->have -1].line_number;
                    }
                    fprintf( stdout, ">" );
                }
                fprintf( stdout, " %llu", (long long unsigned)local_number_of_lines );
                if ( local_number_of_lines > 1000 ) {
                    fprintf( stdout, " (%s)", giveMeSIUnits(local_number_of_lines, 0) );
                }
                fprintf( stdout, "\n" );
            }

            // print compression factor, estimated from available data if index isn't complete
            fprintf( stdout, "\tCompression factor       : " );
            if ( 1 == index->index_complete &&
                 NULL != gzip_filename  // gzip file may be unkown: compression cannot be calculated here
                ) {
                // index is complete
                if ( st_gzip.st_size > 0 &&
                     index->file_size > 0 ) {
                    fprintf( stdout, "%.2f%%\n",
                        100.0 - (double)st_gzip.st_size/(double)index->file_size*100.0 );
                }
            } else {
                // index is incomplete (or gzip file is not available):
                // use last available index point as data for percentage
                if ( index->have > 1 && // index has at least one useful index point
                     index->list[index->have -1].in  > 0 &&
                     index->list[index->have -1].out > 0 ) {
                    fprintf( stdout, "%.2f%%\n",
                        100.0 - (double)index->list[index->have -1].in/(double)index->list[index->have -1].out*100.0 );
                } else {
                    fprintf( stdout, "Not available\n");
                }
            }

        }

        // print and check index points stored in index
        if ( list_verbosity > VERBOSITY_NORMAL ) {
            if ( verbosity_level > VERBOSITY_NONE ) {
                if ( list_verbosity < VERBOSITY_MANIAC ) {
                    if ( 0 == index->index_version ) {
                        fprintf( stdout,
                          "\tList of points:\n\t#: @ compressed/uncompressed byte (window data size in Bytes @window's beginning at index file), ...\n" );
                    } else {
                        fprintf( stdout,
                          "\tList of points:\n\t#: @ compressed/uncompressed byte L#line_number (window data size in Bytes @window's beginning at index file), ...\n" );
                    }
                } else {
                    if ( 0 == index->index_version ) {
                        fprintf( stdout,
                          "\tList of points:\n\t#: @ compressed/uncompressed byte (compressed window size / uncompressed window size), ...\n" );
                    } else {
                        fprintf( stdout,
                          "\tList of points:\n\t#: @ compressed/uncompressed byte L#line_number (compressed window size / uncompressed window size), ...\n" );
                    }
                }
                if ( list_verbosity == VERBOSITY_MANIAC ) {
                    fprintf( stdout, "\tChecking compressed windows...\n" );
                }
            }
            for ( j=0; j<index->have; j++ ) {
                if ( list_verbosity == VERBOSITY_EXCESSIVE &&
                     verbosity_level > VERBOSITY_NONE ) {
                    fprintf( stdout, "#%llu: @ %llu / %llu ",
                        (long long unsigned)(j +1), (long long unsigned)index->list[j].in, (long long unsigned)index->list[j].out );
                    if ( 1 == index->index_version ) // print line number information
                        fprintf( stdout, "L%llu ", (long long unsigned)index->list[j].line_number );
                    fprintf( stdout, "( %d @%llu ), ", index->list[j].window_size, (long long unsigned)index->list[j].window_beginning );
                    if ( (j + 1) % 5 == 0 ) {
                        fprintf( stdout, "\n" );
                    }
                }
                if ( list_verbosity == VERBOSITY_MANIAC ) {
                    uint64_t local_window_size = index->list[j].window_size; // uint64_t to pass to decompress_chunk()
                    if ( local_window_size > 0 ) {
                        /* window is compressed on memory, so decompress it */
                        unsigned char *decompressed_window = NULL;
                        decompressed_window = decompress_chunk(index->list[j].window, &local_window_size);
                        if ( NULL == decompressed_window ) {
                            printToStderr( VERBOSITY_NORMAL,
                                "\nERROR: Could not decompress window #%llu from index file '%s'.\n",
                                j +1, file_name);
                            ret_value = EXIT_GENERIC_ERROR;
                        }
                        free( decompressed_window );
                        decompressed_window = NULL;
                    }
                    if ( verbosity_level > VERBOSITY_NONE ) {
                        comp_win_counter   += index->list[j].window_size;
                        uncomp_win_counter += local_window_size;
                        fprintf( stdout, "#%llu: @ %llu / %llu ",
                            (long long unsigned)(j +1),
                            (long long unsigned)index->list[j].in, (long long unsigned)index->list[j].out );
                        if ( 1 == index->index_version ) { // print line number information
                            fprintf( stdout, "L%llu ", (long long unsigned)index->list[j].line_number );
                        }
                        fprintf( stdout, "( %d/%llu %.2f%% ), ", index->list[j].window_size,
                            (long long unsigned)local_window_size,
                            ((local_window_size>0)?(100.0 - (double)(index->list[j].window_size) / (double)local_window_size * 100.0):0.0) );
                        if ( (j + 1) % 5 == 0 ) {
                            fprintf( stdout, "\n" );
                        }
                    }
                }
            }
            if ( verbosity_level > VERBOSITY_NONE )
                fflush( stdout );
        }

        if ( verbosity_level > VERBOSITY_NONE &&
             list_verbosity == VERBOSITY_MANIAC &&
             uncomp_win_counter > 0 ) {
            fprintf( stdout, "\n\tEstimated gzip data compression factor: %.2f%%",
                100.0 - (double)comp_win_counter/(double)uncomp_win_counter*100.0 );
            if ( NULL != gzip_filename &&
                 st_gzip.st_size > 0 &&
                 index->file_size > 0 ) {
                fprintf( stdout, "\n\tReal gzip data compression factor     : %.2f%%",
                    100.0 - (double)st_gzip.st_size/(double)index->file_size*100.0 );
            }
        }

        if (verbosity_level > VERBOSITY_NONE )
            fprintf( stdout, "\n" );

    }

action_list_info_error:
    if ( NULL != in )
        fclose( in );
    if ( NULL != index )
        free_index( index );
    if ( NULL != gzip_filename )
        free( gzip_filename );

    return ret_value;

}


// obtain an integer from a string that may used decimal point,
// with valid suffixes: kmgtpe (powers of 10) and KMGTPE (powers of 2),
// and valid prefixes: "0" (octal), "0x" or "0X" (hexadecimal).
// Examples:
// "83m" == 83*10^6, "9G" == 9*2^30, "0xa" == 10, "010k" = 8000, "23.5k" = 23500
// INPUT:
// char *original_input: string containing the data (only read)
// OUTPUT:
// 64 bit long integer number
uint64_t giveMeAnInteger( const char *original_input ) {

    unsigned i = 0;
    char *PowerSuffixes = "kmgtpeKMGTPE";
    unsigned PowerSuffixesLength = strlen(PowerSuffixes)/2;
    char *input = NULL;
    uint64_t result = 1;

    if ( NULL == original_input )
        return 0LL;

    input = malloc( strlen(original_input) +1 );
    memcpy(input, original_input, strlen(original_input) +1);

    if ( strlen(input) > 1 ) {
        // look for suffixes of size

        for ( i=0; i<strlen(PowerSuffixes); i++ ) {
            if ( (char *)strchr(input, PowerSuffixes[i]) == (char *)(input + strlen(input) -1) ) {
                if ( i >= PowerSuffixesLength ) {
                    result = pow( 2.0, 10.0 + 10.0*(double)(i - PowerSuffixesLength) );
                } else {
                    result = pow( 10.0, 3.0 + 3.0*(double)i );
                }
                input[strlen(input) -1] = '\0';
                break;
            }
        }

    }

    if ( strlen(input) > 1 ) {
        // look fo prefixes of base

        if ( input[0] == '0' ) {
            if ( strlen(input) > 2 &&
                 ( input[1] == 'x' || input[1] == 'X' ) ) {
                // hexadecimal
                result = strtoll( input +2, NULL, 16 ) * result;
            } else {
                // octal
                result = strtoll( input +1, NULL, 8 ) * result;
            }
            input[0] = '1';
            input[1] = '\0';
        }

    }

    result = (uint64_t)(strtod(input, NULL) * (double)result);

    if ( NULL != input )
        free( input );

    return result;

}


// .................................................


// print brief help
local void print_brief_help() {

    fprintf( stderr, "\n" );
    fprintf( stderr, "  %s (v%s)\n", PACKAGE_NAME, PACKAGE_VERSION );
    fprintf( stderr, "  GZIP files indexer, compressor and data retriever.\n" );
    fprintf( stderr, "  Create small indexes for gzipped files and use them\n" );
    fprintf( stderr, "  for quick and random positioned data extraction.\n" );
    fprintf( stderr, "  //github.com/circulosmeos/gztool (by Roberto S. Galende)\n\n" );
    fprintf( stderr, "  $ gztool [-[abLnsv] #] [-cCdDeEfFhilStTwWxX|u[cCdD]] [-I <INDEX>] <FILE>...\n\n" );
    fprintf( stderr, "  `gztool -hh` for more help\n" );
    fprintf( stderr, "\n" );

}


// print help
local void print_help() {

    fprintf( stderr, "\n" );
    fprintf( stderr, "  %s (v%s)\n", PACKAGE_NAME, PACKAGE_VERSION );
    fprintf( stderr, "  GZIP files indexer, compressor and data retriever.\n" );
    fprintf( stderr, "  Create small indexes for gzipped files and use them\n" );
    fprintf( stderr, "  for quick and random positioned data extraction.\n" );
    fprintf( stderr, "  No more waiting when the end of a 10 GiB gzip is needed!\n" );
    fprintf( stderr, "  //github.com/circulosmeos/gztool (by Roberto S. Galende)\n\n" );
    fprintf( stderr, "  $ gztool [-[abLnsv] #] [-cCdDeEfFhilStTwWxX|u[cCdD]] [-I <INDEX>] <FILE>...\n\n" );
    fprintf( stderr, "  Note that actions `-bcStT` proceed to an index file creation (if\n" );
    fprintf( stderr, "  none exists) INTERLEAVED with data flow. As data flow and\n" );
    fprintf( stderr, "  index creation occur at the same time there's no waste of time.\n" );
    fprintf( stderr, "  Also you can interrupt actions at any moment and the remaining\n" );
    fprintf( stderr, "  index file will be reused (and completed if necessary) on the\n" );
    fprintf( stderr, "  next gztool run over the same data.\n\n" );
    fprintf( stderr, " -a #: Await # seconds between reads when `-[ST]|Ec`. Default is 4 s.\n" );
    fprintf( stderr, " -b #: extract data from indicated uncompressed byte position of\n" );
    fprintf( stderr, "     gzip file (creating or reusing an index file) to STDOUT.\n" );
    fprintf( stderr, "     Accepts '0', '0x', and suffixes 'kmgtpe' (^10) 'KMGTPE' (^2).\n" );
    fprintf( stderr, " -C: always create a 'Complete' index file, ignoring possible errors.\n" );
    fprintf( stderr, " -c: compress a file like with gzip, creating an index at the same time.\n" );
    fprintf( stderr, " -d: decompress a file like with gzip.\n" );
    fprintf( stderr, " -D: do not delete original file when using `-[cd]`.\n" );
    fprintf( stderr, " -e: if multiple files are indicated, continue on error (if any).\n" );
    fprintf( stderr, " -E: end processing on first GZIP end of file marker at EOF.\n" );
    fprintf( stderr, "     Nonetheless with `-c`, `-E` waits for more data even at EOF.\n" );
    fprintf( stderr, " -f: force file overwriting if destination file already exists.\n" );
    fprintf( stderr, " -F: force index creation/completion first, and then action: if\n" );
    fprintf( stderr, "     `-F` is not used, index is created interleaved with actions.\n" );
    fprintf( stderr, " -h: print brief help; `-hh` prints this help.\n" );
    fprintf( stderr, " -i: create index for indicated gzip file (For 'file.gz' the default\n" );
    fprintf( stderr, "     index file name will be 'file.gzi'). This is the default action.\n" );
    fprintf( stderr, " -I INDEX: index file name will be 'INDEX'.\n" );
    fprintf( stderr, " -l: check and list info contained in indicated index file.\n" );
    fprintf( stderr, "     `-ll` and `-lll` increase the level of index checking detail.\n" );
    fprintf( stderr, " -L #: extract data from indicated uncompressed line position of\n" );
    fprintf( stderr, "     gzip file (creating or reusing an index file) to STDOUT.\n" );
    fprintf( stderr, "     Accepts '0', '0x', and suffixes 'kmgtpe' (^10) 'KMGTPE' (^2).\n" );
    fprintf( stderr, " -n #: indicates that the first byte on compressed input is #, not 1,\n" );
    fprintf( stderr, "     and so truncated compressed inputs can be used if an index exists.\n" );
    fprintf( stderr, " -s #: span in uncompressed MiB between index points when\n" );
    fprintf( stderr, "     creating the index. By default is `10`.\n" );
    fprintf( stderr, " -S: Supervise indicated file: create a growing index,\n" );
    fprintf( stderr, "     for a still-growing gzip file. (`-i` is implicit).\n" );
    fprintf( stderr, " -t: tail (extract last bytes) to STDOUT on indicated gzip file.\n" );
    fprintf( stderr, " -T: tail (extract last bytes) to STDOUT on indicated still-growing\n" );
    fprintf( stderr, "     gzip file, and continue Supervising & extracting to STDOUT.\n" );
    fprintf( stderr, " -u [cCdD]: utility to compress (`-u c`) or decompress (`-u d`)\n" );
    fprintf( stderr, "          zlib-format files to STDOUT. Use `-u C` and `-u D`\n" );
    fprintf( stderr, "          to manage raw compressed files. No index involved.\n" );
    fprintf( stderr, " -v #: output verbosity: from `0` (none) to `5` (nuts).\n" );
    fprintf( stderr, "     Default is `1` (normal).\n" );
    fprintf( stderr, " -w: wait for creation if file doesn't exist, when using `-[cdST]`.\n" );
    fprintf( stderr, " -W: do not Write index to disk. But if one is already available\n" );
    fprintf( stderr, "     read and use it. Useful if the index is still under a `-S` run.\n" );
    fprintf( stderr, " -x: create index with line number information (win/*nix compatible).\n" );
    fprintf( stderr, "     (Index counts last line even w/o newline char (`wc` does not!)).\n" );
    fprintf( stderr, " -X: like `-x`, but newline character is '\\r' (old mac).\n" );
    fprintf( stderr, "\n" );
    fprintf( stderr, "  EXAMPLE: Extract data from 1 GiB byte (byte 2^30) on,\n" );
    fprintf( stderr, "  from `myfile.gz` to the file `myfile.txt`. Also gztool will\n" );
    fprintf( stderr, "  create (or reuse, or complete) an index file named `myfile.gzi`:\n" );
    fprintf( stderr, "  $ gztool -b 1G myfile.gz > myfile.txt\n" );
    fprintf( stderr, "\n" );

}


// OUTPUT:
// EXIT_* error code or EXIT_OK on success
int main(int argc, char **argv)
{
    // variables for used for the different actions:
    char *file_name = NULL;
    FILE *in = NULL;
    FILE *index_file = NULL;
    struct access *index = NULL;

    // variables for grabbing the options:
    uint64_t extract_from_byte = 0;
    uint64_t extract_from_line = 0;
    uint64_t expected_first_byte = 1;
    uint64_t span_between_points = SPAN;
    char *index_filename = NULL;
    int continue_on_error = 0;
    int index_filename_indicated = 0;
    int force_action = 0;
    int force_strict_order = 0;
    int write_index_to_disk = 1;
    int end_on_first_proper_gzip_eof = 0;
    int always_create_a_complete_index = 0;
    int wait_for_file_creation = 0;
    int waiting_time = WAITING_TIME;
    int do_not_delete_original_file = 0;
    int extend_index_with_lines = 0;
    int raw_method = 0; // for use with `-[cd]`: 0: zlib; `-[CD]`: 1: raw
    char utility_option = '\0';
    uint64_t count_errors = 0;

    enum EXIT_APP_VALUES ret_value;
    enum ACTION action;
    enum VERBOSITY_LEVEL list_verbosity = VERBOSITY_NONE;
    enum VERBOSITY_LEVEL help_verbosity = VERBOSITY_NONE;

    int opt = 0;
    uint64_t i = 0;
    int actions_set = 0;


    action = ACT_NOT_SET;
    ret_value = EXIT_OK;
    while ((opt = getopt(argc, argv, "a:b:cCdDeEfFhiI:lL:n:s:StTu:v:wWxX")) != -1)
        switch (opt) {
            // help
            case 'h':
                help_verbosity++;
                if ( help_verbosity > VERBOSITY_EXCESSIVE )
                    help_verbosity = VERBOSITY_EXCESSIVE;
                break;
            // `-a #` default waiting time in seconds when supervising a growing gzip file (`-[ST]`)
            case 'a':
                waiting_time = (int)strtol( optarg, NULL, 10 );
                if ( waiting_time == 0 &&
                     ( strlen( optarg ) != 1 || '0' != optarg[0] ) ) {
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Invalid awaiting value of '%s'\n", optarg );
                    return EXIT_INVALID_OPTION;
                }
                break;
            // `-b #` extracts data from indicated position byte in uncompressed stream of <FILE>
            case 'b':
                extract_from_byte = (uint64_t)giveMeAnInteger( optarg );
                action = ACT_EXTRACT_FROM_BYTE;
                actions_set++;
                break;
            // `-c` compress <FILE> (or stdin if none) to FILE.gz and create index for it
            case 'c':
                action = ACT_COMPRESS_AND_CREATE_INDEX;
                actions_set++;
                break;
            // `-C` generates always a complete index file, ignoring possible decompression errors
            case 'C':
                always_create_a_complete_index = 1;
                break;
            // `-d` decompress <FILE> (or stdin if none) to stdout
            case 'd':
                action = ACT_DECOMPRESS;
                actions_set++;
                break;
            // `-D` no not delete original file with `-[cd]`
            case 'D':
                do_not_delete_original_file = 1;
                break;
            // `-e` continues on error if multiple input files indicated
            case 'e':
                continue_on_error = 1;
                break;
            // `-E` ends gzip processing on first proper (at feof()) GZIP EOF,
            // or with `-c` continue waiting for data even at eof()
            case 'E':
                end_on_first_proper_gzip_eof = 1;
                break;
            // `-f` forces index overwriting from scratch, if one exists
            case 'f':
                force_action = 1;
                break;
            // First create index, the process indicated action
            case 'F':
                force_strict_order = 1;
                break;
            // `-i` create index for <FILE>
            case 'i':
                action = ACT_CREATE_INDEX;
                actions_set++;
                break;
            // list number of bytes, but not 0 valued
            // `-I INDEX_FILE` creates index for <FILE> with name <INDEX_FILE>
            case 'I':
                // action = ACT_CREATE_INDEX but only if no other option indicated
                index_filename_indicated = 1;
                index_filename = malloc( strlen(optarg) + 1 );
                memcpy( index_filename, optarg, strlen(optarg) + 1 );
                break;
            // `-l` list info of index <FILE>
            case 'l':
                action = ACT_LIST_INFO;
                if ( list_verbosity == VERBOSITY_NONE )
                    actions_set++;
                list_verbosity++;
                if ( list_verbosity > VERBOSITY_MANIAC )
                    list_verbosity = VERBOSITY_MANIAC;
                break;
            // `-L #` extracts data from indicated line in uncompressed stream of <FILE>
            case 'L':
                extract_from_line = (uint64_t)giveMeAnInteger( optarg );
                action = ACT_EXTRACT_FROM_LINE;
                actions_set++;
                if ( extend_index_with_lines == 0 ) {
                    // if `-[xX]` is not indicated, newline format is Unix
                    extend_index_with_lines = 1;
                }
                break;
            // `-n #` indicates that the first byte on compressed input is #, not 1
            // and so truncated compressed inputs can be used. This is subject to checks later on.
            case 'n':
                expected_first_byte = (uint64_t)giveMeAnInteger( optarg );
                if ( expected_first_byte == 0 ) {
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Option `-n 0` invalid: must be >= 1.\n" );
                    return EXIT_INVALID_OPTION;
                }
                break;
            // `-s #` span between index points, in MiB
            case 's':
                // span is converted from MiB to bytes for internal use
                span_between_points = strtoll( optarg, NULL, 10 );
                if ( span_between_points > UINT64_MAX / 1024 / 1024 ) {
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Option `-s %llu` MiB value too high!\n", span_between_points );
                    return EXIT_INVALID_OPTION;
                }
                span_between_points = span_between_points * 1024 * 1024;
                break;
            // `-S` supervise a still-growing gzip <FILE> and create index for it
            case 'S':
                action = ACT_SUPERVISE;
                actions_set++;
                break;
            // `-t` tail file contents
            case 't':
                action = ACT_EXTRACT_TAIL;
                actions_set++;
                break;
            // `-T` tail file contents and continue Supervising (and extracting data from) gzip file
            case 'T':
                action = ACT_EXTRACT_TAIL_AND_CONTINUE;
                actions_set++;
                break;
            // `-u [cd]`: utility to compress (`-u c`) or decompress (`-u d`) zlib-format files
            case 'u':
                // span is converted to from MiB to bytes for internal use
                if ( strlen(optarg) == 1 ) {
                    switch ( optarg[0] ) {
                        case 'c':
                            action = ACT_COMPRESS_CHUNK;
                            actions_set++;
                            utility_option = optarg[0];
                            break;
                        case 'd':
                            action = ACT_DECOMPRESS_CHUNK;
                            actions_set++;
                            utility_option = optarg[0];
                            break;
                        case 'C':
                            action = ACT_COMPRESS_CHUNK;
                            raw_method = 1;
                            actions_set++;
                            utility_option = optarg[0];
                            break;
                        case 'D':
                            action = ACT_DECOMPRESS_CHUNK;
                            raw_method = 1;
                            actions_set++;
                            utility_option = optarg[0];
                            break;
                        default:
                            printToStderr( VERBOSITY_NORMAL, "ERROR: Invalid option `-u %s` (`-u [cCdD]`).\n", optarg );
                            return EXIT_INVALID_OPTION;
                    }
                } else {
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Invalid option `-u %s` (`-u [cCdD]`).\n", optarg );
                    return EXIT_INVALID_OPTION;
                }
                break;
            // `-v` verbosity
            case 'v':
                verbosity_level = (int)strtol( optarg, NULL, 10 );
                if ( ( optarg[0] != '0' && verbosity_level == 0 ) ||
                     strlen( optarg ) > 1 ||
                     verbosity_level > VERBOSITY_NUTS ) {
                    verbosity_level = VERBOSITY_NORMAL; // without this an erroneous `-v` may not be shown
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Invalid option `-v %s` (`-v [0..5]`).\n", optarg );
                    return EXIT_INVALID_OPTION;
                }
                break;
            // `-w` wait for file creation with `-[STcd]`
            case 'w':
                wait_for_file_creation = 1;
                break;
            // `-W` do not write nor update index on disk
            case 'W':
                write_index_to_disk = 0;
                break;
            // `-x` create extended index with line number information
            // using Unix newline format ('\n')  (compatible with Windows \r\n)
            case 'x':
                extend_index_with_lines = 1;
                break;
            // `-X` create extended index with line number information
            // using old mac newline format ('\r')
            case 'X':
                extend_index_with_lines = 2;
                break;
            case '?':
                if ( isprint (optopt) ) {
                    // print warning only if char option is unknown
                    if ( NULL == strchr("abcCdDeEfFhiIlLnSstTuvwWxX", optopt) ) {
                        printToStderr( VERBOSITY_NORMAL, "ERROR: Unknown option `-%c'.\n", optopt);
                        print_help();
                    }
                } else
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Unknown option character `\\x%x'.\n", optopt);
                printToStderr( VERBOSITY_NORMAL, "\n" );
                return EXIT_INVALID_OPTION;
            default:
                printToStderr( VERBOSITY_NORMAL, "\n" );
                abort ();
        }

    // check for help action and exit
    if ( help_verbosity > VERBOSITY_NONE ) {
        if ( help_verbosity == VERBOSITY_NORMAL )
            print_brief_help();
        else
            print_help();
        return EXIT_OK;
    }

    // Checking parameter merging and absence
    if ( actions_set > 1 ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: do not merge parameters `-[bcdilLStTu]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( write_index_to_disk == 0 &&
           ( force_action == 1 || force_strict_order == 1)
        ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: do not merge contradictory parameters `-W` and `-[fF]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( ( action != ACT_SUPERVISE && action != ACT_EXTRACT_TAIL_AND_CONTINUE &&
           action != ACT_COMPRESS_AND_CREATE_INDEX && action != ACT_DECOMPRESS ) &&
        wait_for_file_creation == 1 ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: `-w` only apply to `-[cdST]`\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( ( action == ACT_COMPRESS_CHUNK || action == ACT_DECOMPRESS_CHUNK ) &&
        ( force_action == 1 || force_strict_order == 1 || write_index_to_disk == 0 ||
            span_between_points != SPAN || index_filename_indicated == 1 ||
            end_on_first_proper_gzip_eof == 1 || always_create_a_complete_index == 1 ||
            waiting_time != WAITING_TIME )
        ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: `-[aCEfFIsW]` does not apply to `-u`\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( ( action == ACT_LIST_INFO ) &&
        ( force_action == 1 || force_strict_order == 1 || write_index_to_disk == 0 ||
            span_between_points != SPAN ||
            end_on_first_proper_gzip_eof == 1 || always_create_a_complete_index == 1 ||
            waiting_time != WAITING_TIME )
        ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: `-[aCEfFsW]` does not apply to `-l`\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( (unsigned int) waiting_time >= UINT32_MAX || waiting_time < 0 ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: Invalid awaiting value of '%d'\n", waiting_time );
        return EXIT_INVALID_OPTION;
    }

    if ( do_not_delete_original_file == 1 &&
         ( action != ACT_COMPRESS_AND_CREATE_INDEX && action != ACT_DECOMPRESS ) ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: `-D` option invalid when not using `-[cd]`\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( span_between_points <= 0 ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: Invalid `-s` parameter value: '%llu'\n", span_between_points );
        return EXIT_INVALID_OPTION;
    }
    if ( span_between_points != SPAN &&
        ( action == ACT_COMPRESS_CHUNK || action == ACT_DECOMPRESS_CHUNK || action == ACT_LIST_INFO )
        ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: `-s` parameter does not apply to `-[lu]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( extend_index_with_lines > 0 &&
        ( action == ACT_COMPRESS_CHUNK || action == ACT_DECOMPRESS_CHUNK || action == ACT_LIST_INFO )
        ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: `-[xX]` parameters do not apply to `-[lu]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( expected_first_byte != 1 &&
         ( action != ACT_EXTRACT_FROM_BYTE && action != ACT_EXTRACT_FROM_LINE ) ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: `-n` parameter can only be used with `-[bL]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( 0 == actions_set ) {
        // `-I <FILE>` is equivalent to `-i -I <FILE>`
        if ( action == ACT_NOT_SET && index_filename_indicated  == 1 ) {
            action = ACT_CREATE_INDEX;
            if ( (optind + 1) < argc ) {
                // too much files indicated to use `-I`
                printToStderr( VERBOSITY_NORMAL, "ERROR: `-I` is incompatible with multiple input files.\nAborted.\n\n" );
                return EXIT_INVALID_OPTION;
            }
        } else {
            // default action is `-i`
            action = ACT_CREATE_INDEX;
            actions_set = 1;
        }
    }

    if ( 1 == force_strict_order &&
        ( action == ACT_SUPERVISE ||
          action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
          action == ACT_LIST_INFO ||
          action == ACT_COMPRESS_CHUNK ||
          action == ACT_DECOMPRESS_CHUNK ||
          action == ACT_DECOMPRESS ) ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: There's no sense in using `-F` with `-[dlSTu]`.\n" );
        return EXIT_INVALID_OPTION;
    }

    if ( 1 == force_strict_order && action == ACT_COMPRESS_AND_CREATE_INDEX ) {
        printToStderr( VERBOSITY_NORMAL, "ERROR: `-F` not implemented with `-c`.\n" );
        return EXIT_INVALID_OPTION;
    }


    {   // inform action on stderr:
        char *action_string = NULL;
        switch ( action ) {
            case ACT_EXTRACT_FROM_BYTE:
                action_string = "Extract from byte = ";
                break;
            case ACT_COMPRESS_CHUNK:
                if ( raw_method == 1 )
                    action_string = "zlib raw compress";
                else
                    action_string = "zlib compress";
                break;
            case ACT_DECOMPRESS_CHUNK:
                if ( raw_method == 1 )
                    action_string = "zlib raw decompress";
                else
                    action_string = "zlib decompress";
                break;
            case ACT_CREATE_INDEX:
                action_string = "Create index for a gzip file";
                break;
            case ACT_SUPERVISE:
                action_string = "Supervise still-growing file";
                break;
            case ACT_LIST_INFO:
                action_string = "Check & list info in index file";
                break;
            case ACT_EXTRACT_TAIL:
                action_string = "Extract tail data";
                break;
            case ACT_EXTRACT_TAIL_AND_CONTINUE:
                action_string = "Extract from tail data from a still-growing file";
                break;
            case ACT_COMPRESS_AND_CREATE_INDEX:
                action_string = "Compress file and create index";
                break;
            case ACT_DECOMPRESS:
                action_string = "Decompress file";
                break;
            case ACT_EXTRACT_FROM_LINE:
                action_string = "Extract from line = ";
                break;
            default:
                printToStderr( VERBOSITY_NORMAL, "ERROR: Unexpected error! No action specified.\n" );
                return EXIT_GENERIC_ERROR;
        }
        switch( action ) {
            case ACT_EXTRACT_FROM_BYTE:
                printToStderr( VERBOSITY_NORMAL, "ACTION: %s%llu", action_string, extract_from_byte );
                if ( expected_first_byte > 1 ) {
                    printToStderr( VERBOSITY_NORMAL, " (input - %llu bytes)", expected_first_byte - 1 );
                }
                printToStderr( VERBOSITY_NORMAL, "\n\n" );
                break;
            case ACT_EXTRACT_FROM_LINE:
                printToStderr( VERBOSITY_NORMAL, "ACTION: %s%llu\n\n", action_string, extract_from_line );
                break;
            default:
                printToStderr( VERBOSITY_NORMAL, "ACTION: %s\n\n", action_string );
        }
    }


    // inform parameters with verbosity_level > VERBOSITY_NORMAL
    if ( verbosity_level > VERBOSITY_NORMAL ) {
        printToStderr( VERBOSITY_EXCESSIVE, "  -a: %d, \t-b: %llu, \t-c: %d\n",
            waiting_time, extract_from_byte, ( (action==ACT_COMPRESS_AND_CREATE_INDEX)? 1: 0 ) );
        printToStderr( VERBOSITY_EXCESSIVE, "  -C: %d, \t-d: %d, \t-D: %d, \t-e: %d\n",
            always_create_a_complete_index, ( (action==ACT_DECOMPRESS)? 1: 0 ),
            do_not_delete_original_file, continue_on_error );
        printToStderr( VERBOSITY_EXCESSIVE, "  -E: %d, \t-f: %d, \t-F: %d\n",
            end_on_first_proper_gzip_eof, force_action, force_strict_order );
        printToStderr( VERBOSITY_EXCESSIVE, "  -i: %d, \t-I: %s\n",
            ( (action==ACT_CREATE_INDEX)? 1: 0 ),
            ( (index_filename_indicated>0)? index_filename: NULL ));
        printToStderr( VERBOSITY_EXCESSIVE, "  -l: %d, \t-L: %llu, \t-n: %llu\n",
            ( (action==ACT_LIST_INFO)? list_verbosity: 0 ),
            extract_from_line, expected_first_byte );
        printToStderr( VERBOSITY_EXCESSIVE, "  -s: %llu, \t-S: %d, \t-t: %d\n",
            span_between_points, ( (action==ACT_SUPERVISE)? 1: 0 ), ( (action==ACT_EXTRACT_TAIL)? 1: 0 ) );
        printToStderr( VERBOSITY_EXCESSIVE, "  -T: %d, \t-u: %c, \t-v: %d, \t-w: %d, \t-W: %d\n",
            ( (action==ACT_EXTRACT_TAIL_AND_CONTINUE)? 1: 0 ), utility_option,
              verbosity_level, wait_for_file_creation, ( (write_index_to_disk==0)? 1: 0 ) );
        printToStderr( VERBOSITY_EXCESSIVE, "  -x: %d, \t-X: %d\n\n",
            ( (1 == extend_index_with_lines)? 1: 0), ( (2 == extend_index_with_lines)? 1: 0) );
    }


    if (optind == argc || argc == 1) {
        // file input is stdin

        // ACT_COMPRESS_AND_CREATE_INDEX requires an index file name:
        if ( action == ACT_COMPRESS_AND_CREATE_INDEX &&
             0 == index_filename_indicated
            ) {
            // if no index filename is set (`-I`), compression cannot proceed
            printToStderr( VERBOSITY_NORMAL, "ERROR: Index filename must be provided with `-I` when compressing STDIN.\n" );
            return EXIT_GENERIC_ERROR;
        }

        // check `-f` and execute delete if index file exists
        if ( ( action == ACT_CREATE_INDEX || action == ACT_SUPERVISE ||
               action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
               action == ACT_EXTRACT_FROM_BYTE || action == ACT_EXTRACT_FROM_LINE ||
               action == ACT_EXTRACT_TAIL ||
               action == ACT_COMPRESS_AND_CREATE_INDEX ) &&
             1 == index_filename_indicated &&
             access( index_filename, F_OK ) != -1 ) {
            // index file already exists

            /*if ( ( extend_index_with_lines > 0 && action != ACT_EXTRACT_FROM_LINE ) &&
                 ( force_action == 0 || ( force_action == 1 && write_index_to_disk == 0 ) ) )
                    printToStderr( VERBOSITY_NORMAL, "WARNING: `-[Xx]` will be ignored because index already exists.\n" );*/

            if ( force_action == 0 ) {
                if ( action == ACT_COMPRESS_AND_CREATE_INDEX ) {
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Index file '%s' already exists.\n\n", index_filename );
                    return EXIT_GENERIC_ERROR;
                } else {
                    printToStderr( VERBOSITY_NORMAL, "Index file '%s' already exists and will be used.\n", index_filename );
                    if ( write_index_to_disk == 0 ) {
                        printToStderr( VERBOSITY_NORMAL, "Index file will NOT be modified.\n" );
                    } else {
                        ; //printToStderr( VERBOSITY_NORMAL, "(Use `-f` to force overwriting.)\n" );
                    }
                }
            } else {
                if ( write_index_to_disk == 1 ) {
                    // force_action == 1 => delete index file
                    printToStderr( VERBOSITY_NORMAL, "Using `-f` force option: Deleting '%s' ...\n", index_filename );
                    // delete it
                    if ( remove( index_filename ) != 0 ) {
                        printToStderr( VERBOSITY_NORMAL, "ERROR: Could not delete '%s'.\n\n", index_filename );
                        return EXIT_GENERIC_ERROR;
                    }
                } else {
                    printToStderr( VERBOSITY_NORMAL, "Ignoring `-f` force option with `W` on '%s' ...\n", index_filename );
                }
            }

        }

        // `-F` has no sense with stdin
        if ( force_strict_order == 1 ) {
            printToStderr( VERBOSITY_NORMAL, "WARNING: There is no sense in using `-F` with STDIN input: ignoring `F`.\n" );
            force_strict_order = 0;
        }

        if ( action == ACT_DECOMPRESS ) {
            // `gztool -d` is just an alias for `gztool -b0` when using STDIN
            extract_from_byte = 0;
            action = ACT_EXTRACT_FROM_BYTE;
        }

        // file input is stdin
        switch ( action ) {

            case ACT_EXTRACT_FROM_BYTE:
                // stdin is a gzip file
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        EXTRACT_FROM_BYTE, extract_from_byte, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                    printToStderr( VERBOSITY_NORMAL, "\n" );
                    break;
                } else {
                    printToStderr( VERBOSITY_NORMAL, "ERROR: `-I INDEX` must be used when extracting from STDIN.\nAborted.\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }

            case ACT_EXTRACT_FROM_LINE:
                // stdin is a gzip file
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        EXTRACT_FROM_LINE, 0, extract_from_line, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                    printToStderr( VERBOSITY_NORMAL, "\n" );
                    break;
                } else {
                    printToStderr( VERBOSITY_NORMAL, "ERROR: `-I INDEX` must be used when extracting from STDIN.\nAborted.\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }

            case ACT_COMPRESS_CHUNK:
                // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                // If we're here it's because stdin will be used
                SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
                if ( Z_OK != compress_file( stdin, stdout, Z_DEFAULT_COMPRESSION, raw_method ) ) {
                    printToStderr( VERBOSITY_NORMAL, "ERROR while compressing STDIN.\nAborted.\n\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }
                ret_value = EXIT_OK;
                break;

            case ACT_DECOMPRESS_CHUNK:
                // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                // If we're here it's because stdin will be used
                SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                SET_BINARY_MODE(STDIN); // sets binary mode for stdout in Windows
                if ( Z_OK != decompress_file( stdin, stdout, raw_method ) ) {
                    printToStderr( VERBOSITY_NORMAL, "ERROR while decompressing STDIN.\nAborted.\n\n" );
                    ret_value = EXIT_GENERIC_ERROR;
                    break;
                }
                ret_value = EXIT_OK;
                break;

            case ACT_CREATE_INDEX:
                // stdin is a gzip file that must be indexed
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        JUST_CREATE_INDEX, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                } else {
                    ret_value = action_create_index( "", &index, "",
                        JUST_CREATE_INDEX, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                }
                printToStderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_LIST_INFO:
                if ( index_filename_indicated == 1 ) {
                    // Admit `-I` as a way to indicate an input index file name:
                    ret_value = action_list_info( index_filename, NULL, list_verbosity );
                } else {
                    // stdin is an index file that must be checked
                    ret_value = action_list_info( "", NULL, list_verbosity );
                }
                printToStderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_SUPERVISE:
                // stdin is a gzip file for which an index file must be created on-the-fly
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        SUPERVISE_DO, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                } else {
                    ret_value = action_create_index( "", &index, "",
                        SUPERVISE_DO, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                }
                printToStderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_EXTRACT_TAIL:
                // stdin is a gzip file
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        EXTRACT_TAIL, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                } else {
                    // if an index filename is not indicated, index will not be output
                    // as stdout is already used for data extraction
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Index filename is needed if STDIN is used as gzip input.\nAborted.\n" );
                    ret_value = EXIT_INVALID_OPTION;
                }
                break;

            case ACT_EXTRACT_TAIL_AND_CONTINUE:
                if ( index_filename_indicated == 1 ) {
                    ret_value = action_create_index( "", &index, index_filename,
                        SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                } else {
                    ret_value = action_create_index( "", &index, "",
                        SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                }
                printToStderr( VERBOSITY_NORMAL, "\n" );
                break;

            case ACT_COMPRESS_AND_CREATE_INDEX:
                // if code reaches here, and index_filename exists
                // TODO: implement `-F` for COMPRESS_AND_CREATE_INDEX ?
                ret_value = action_create_index( "", &index, index_filename,
                        COMPRESS_AND_CREATE_INDEX, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                break;

            default:
                printToStderr( VERBOSITY_NORMAL, "ERROR: action not specified.\n" );
                ret_value = EXIT_GENERIC_ERROR;

        }

    } else {

        if ( action == ACT_SUPERVISE &&
             ( argc - optind > 1 ) ) {
            // supervise only accepts one input gz file
            printToStderr( VERBOSITY_NORMAL, "`-S` option only accepts one gzip file parameter: %d indicated.\n", argc - optind );
            printToStderr( VERBOSITY_NORMAL, "Aborted.\n" );
            return EXIT_GENERIC_ERROR;
        }

        for ( i = optind; i < (uint64_t)argc; i++ ) {

            file_name = argv[i];

            ret_value = EXIT_OK;

            // if no index filename is set (`-I`), it is derived from each <FILE> parameter
            if ( 0 == index_filename_indicated ) {

                if ( NULL != index_filename ) {
                    free(index_filename);
                    index_filename = NULL;
                }
                index_filename = malloc( strlen(argv[i]) + 4 + 1 );
                sprintf( index_filename, "%s", argv[i] );

                if ( action == ACT_COMPRESS_AND_CREATE_INDEX ) {
                    sprintf(index_filename, "%s.gzi", argv[i]);
                } else {
                    if ( strlen( argv[i] ) > 3 && // avoid out-of-bounds
                         (char *)strstr(index_filename, ".gz") ==
                         (char *)(index_filename + strlen(argv[i]) - 3)
                        )
                        // if gzip-file name is 'FILE.gz', index file name will be 'FILE.gzi'
                        sprintf(index_filename, "%si", argv[i]);
                    else
                        // otherwise, the complete extension '.gzi' is appended
                        sprintf(index_filename, "%s.gzi", argv[i]);
                }

            }

            // free previous loop's resources
            if ( NULL != in ) {
                fclose( in );
                in = NULL;
            }
            if ( NULL != index ) {
                free_index( index );
                index = NULL;
            }
            if ( NULL != index_file ) {
                fclose( index_file );
                index_file = NULL;
            }

            if ( ( action == ACT_CREATE_INDEX || action == ACT_SUPERVISE ||
                   action == ACT_EXTRACT_TAIL_AND_CONTINUE ||
                   action == ACT_EXTRACT_FROM_BYTE || action == ACT_EXTRACT_FROM_LINE ||
                   action == ACT_EXTRACT_TAIL ||
                   action == ACT_COMPRESS_AND_CREATE_INDEX ) &&
                 access( index_filename, F_OK ) != -1 ) {
                // index file already exists

                /*if ( ( extend_index_with_lines > 0 && action != ACT_EXTRACT_FROM_LINE ) &&
                     ( force_action == 0 || ( force_action == 1 && write_index_to_disk == 0 ) ) )
                        printToStderr( VERBOSITY_NORMAL, "WARNING: `-[Xx]` will be ignored because index already exists.\n" );*/

                if ( force_action == 0 ) {
                    printToStderr( VERBOSITY_NORMAL, "Index file '%s' already exists and will be used.\n", index_filename );
                    if ( write_index_to_disk == 0 ) {
                        printToStderr( VERBOSITY_NORMAL, "Index file will NOT be modified.\n" );
                    } else {
                        ; //printToStderr( VERBOSITY_NORMAL, "(Use `-f` to force overwriting.)\n" );
                    }
                } else {
                    // force_action == 1
                    if ( write_index_to_disk == 1 ) {
                        // delete index file
                        printToStderr( VERBOSITY_NORMAL, "Using `-f` force option: Deleting '%s' ...\n", index_filename );
                        if ( remove( index_filename ) != 0 ) {
                            printToStderr( VERBOSITY_NORMAL, "ERROR: Could not delete '%s'.\nAborted.\n", index_filename );
                            ret_value = EXIT_GENERIC_ERROR;
                        }
                    } else {
                        printToStderr( VERBOSITY_NORMAL, "Ignoring `-f` force option with `W` on '%s' ...\n", index_filename );
                    }
                }

            }


            // check possible errors and `-e` before proceed
            if ( ret_value != EXIT_OK ) {
                if ( continue_on_error == 1 ) {
                    continue;
                } else {
                    break; // breaks for() loop
                }
            }


            // create index first if `-F`
            // (checking of conformity between `-F` and action has been done before)
            if ( force_strict_order == 1 ) {
                ret_value = action_create_index( file_name, &index, index_filename,
                            JUST_CREATE_INDEX, 0, 0, span_between_points,
                            write_index_to_disk, end_on_first_proper_gzip_eof,
                            always_create_a_complete_index, waiting_time, force_action,
                            wait_for_file_creation, extend_index_with_lines,
                            expected_first_byte );
                if ( ret_value != EXIT_OK ) {
                    printToStderr( VERBOSITY_NORMAL, "ERROR: Could not create index '%s'.\nAborted.\n", index_filename );
                    break; // breaks for() loop
                }
            }


            // "-bil" options can accept multiple files
            switch ( action ) {

                case ACT_EXTRACT_FROM_BYTE:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        EXTRACT_FROM_BYTE, extract_from_byte, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                    break;

                case ACT_EXTRACT_FROM_LINE:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        EXTRACT_FROM_LINE, 0, extract_from_line, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                    break;

                case ACT_COMPRESS_CHUNK:
                    // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                    // If we're here it's because there's an input file_name (at least one)
                    if ( NULL == (in = fopen( file_name, "rb" )) ) {
                        printToStderr( VERBOSITY_NORMAL, "ERROR while opening file '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                    if ( Z_OK != compress_file( in, stdout, Z_DEFAULT_COMPRESSION, raw_method ) ) {
                        printToStderr( VERBOSITY_NORMAL, "ERROR while compressing '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    break;

                case ACT_DECOMPRESS_CHUNK:
                    // compress chunk reads stdin or indicated file, and deflates in raw to stdout
                    // If we're here it's because there's an input file_name (at least one)
                    if ( NULL == (in = fopen( file_name, "rb" )) ) {
                        printToStderr( VERBOSITY_NORMAL, "ERROR while opening file '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                        break;
                    }
                    SET_BINARY_MODE(STDOUT); // sets binary mode for stdout in Windows
                    if ( Z_OK != decompress_file( in, stdout, raw_method ) ) {
                        printToStderr( VERBOSITY_NORMAL, "ERROR while decompressing '%s'\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    break;

                case ACT_CREATE_INDEX:
                    if ( force_strict_order == 0 )
                        // if force_strict_order == 1 action has already been done!
                        ret_value = action_create_index( file_name, &index, index_filename,
                            JUST_CREATE_INDEX, 0, 0, span_between_points,
                            write_index_to_disk, end_on_first_proper_gzip_eof,
                            always_create_a_complete_index, waiting_time, force_action,
                            wait_for_file_creation, extend_index_with_lines,
                            expected_first_byte );
                    break;

                case ACT_LIST_INFO:
                    if ( index_filename_indicated == 1 ) {
                        // if index filename has been indicated with `-I`
                        // then the actual parameter is the gzip filename
                        ret_value = action_list_info( index_filename, file_name, list_verbosity );
                    } else {
                        // if indicated file_name is not an index, but an index exists corresponding
                        // to it, use it for ease of use,
                        // BUT only if passed file exists, to avoid adding a spurious extension to
                        // imaginary FILE, obtaining FILE.gzi, the index for existent FILE.gz (!).
                        if ( access( file_name, F_OK ) != -1 &&
                            ( ( strlen( file_name ) > 4 && // avoid out-of-bounds
                                (char *)strstr(file_name, ".gzi") !=
                                (char *)(file_name + strlen(argv[i]) - 4) )
                             ||
                            strlen( file_name ) <= 4 )
                        ) {
                            printToStderr( VERBOSITY_EXCESSIVE,
                                "Provided file '%s' has not '.gzi' extension ...\n", file_name );
                            // file_name is not named "*.gzi"
                            // so let's see if there's a filename with the corresponding index name:
                            // fortunately, this index name has already been calculated: index_filename
                            if ( access( index_filename, F_OK ) != -1 ) {
                                // index_filename exists, so let's use it:
                                printToStderr( VERBOSITY_EXCESSIVE,
                                    "Detected '%s': using it as index file ...\n", index_filename );
                                ret_value = action_list_info( index_filename, file_name, list_verbosity );
                            } else {
                                // provided file_name must be used, as there's no '.gzi' companion
                                printToStderr( VERBOSITY_EXCESSIVE,
                                    "Not detected '%s' file ...\n", file_name );
                                ret_value = action_list_info( file_name, NULL, list_verbosity );
                            }
                        } else {
                            // this is the default case: the passed parameter file_name
                            // is the supossed index filename
                            ret_value = action_list_info( file_name, NULL, list_verbosity );
                        }
                    }
                    break;

                case ACT_SUPERVISE:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        SUPERVISE_DO, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                    printToStderr( VERBOSITY_NORMAL, "\n" );
                    break;

                case ACT_EXTRACT_TAIL:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        EXTRACT_TAIL, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                    break;

                case ACT_EXTRACT_TAIL_AND_CONTINUE:
                    ret_value = action_create_index( file_name, &index, index_filename,
                        SUPERVISE_DO_AND_EXTRACT_FROM_TAIL, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                    printToStderr( VERBOSITY_NORMAL, "\n" );
                    break;

                case ACT_COMPRESS_AND_CREATE_INDEX:
                    // if code reaches here, and index_filename exists
                    ret_value = action_create_index( file_name, &index, index_filename,
                        COMPRESS_AND_CREATE_INDEX, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                    if ( ret_value == EXIT_OK &&
                         do_not_delete_original_file == 0 ) {
                        printToStderr( VERBOSITY_EXCESSIVE, "Deleting file '%s'\n", file_name );
                        // a file_name + ".gz" has been created, but now the original file must be deleted
                        if ( remove( file_name ) != 0 ) {
                            printToStderr( VERBOSITY_NORMAL, "ERROR: Could not delete '%s'.\n", file_name );
                            ret_value = EXIT_GENERIC_ERROR;
                        }
                    }
                    break;

                case ACT_DECOMPRESS:
                    // `gztool -d` is just an alias for `gztool -b0` > file_name without extension
                    // and deletion of file_name.
                    ret_value = action_create_index( file_name, &index, index_filename,
                        DECOMPRESS, 0, 0, span_between_points,
                        write_index_to_disk, end_on_first_proper_gzip_eof,
                        always_create_a_complete_index, waiting_time, force_action,
                        wait_for_file_creation, extend_index_with_lines,
                        expected_first_byte );
                    if ( ret_value == EXIT_OK ) {
                        // delete original file, as with gzip
                        if ( strlen( file_name ) > 3 && // avoid out-of-bounds
                             (char *)strstr(file_name, ".gz") ==
                             (char *)(file_name + strlen(file_name) - 3)
                            ) {
                            // if gzip-file name is 'FILE.gz', output file has been 'FILE'
                            char *output_filename = malloc( strlen(file_name) );
                            sprintf(output_filename, "%s", file_name);
                            output_filename[strlen(file_name) - 3] = '\0';
                            if ( do_not_delete_original_file == 0 ) {
                                printToStderr( VERBOSITY_EXCESSIVE, "Deleting file '%s'\n", file_name );
                                // delete the original file
                                if ( remove( file_name ) != 0 ) {
                                    printToStderr( VERBOSITY_NORMAL,
                                        "WARNING: Decompression finished, but could not delete '%s'.\n", file_name );
                                    ret_value = EXIT_GENERIC_ERROR;
                                }
                            }
                            free( output_filename );
                        }
                    } else {
                        // decompression wasn't successful
                        printToStderr( VERBOSITY_NORMAL, "ERROR: decompressing '%s' file.\n", file_name );
                        ret_value = EXIT_GENERIC_ERROR;
                    }
                    break;

                default:
                    printToStderr( VERBOSITY_NORMAL, "ERROR: action not specified.\n" );
                    ret_value = EXIT_GENERIC_ERROR;

            }

            if ( action == ACT_LIST_INFO ) {
                // as ACT_LIST_INFO prints to stdout,
                // file input separator must be printed also to stdout
                if ( verbosity_level >= VERBOSITY_NORMAL )
                    fprintf( stdout, "\n" );
            } else {
                printToStderr( VERBOSITY_NORMAL, "\n" );
            }

            printToStderr( VERBOSITY_MANIAC, "ERROR code = %d\n", ret_value );

            if ( ret_value != EXIT_OK )
                count_errors++;

            if ( continue_on_error == 0 &&
                 ret_value != EXIT_OK ) {
                printToStderr( VERBOSITY_NORMAL, "Aborted.\n" );
                // break the for loop
                break;
            }

        }

    }

    if ( i > optind )
        printToStderr( VERBOSITY_NORMAL, "%llu files processed\n",
            ( i -optind + ( (count_errors>0 && continue_on_error == 0)?1:0 ) ) );
    if ( count_errors > 0 )
        printToStderr( VERBOSITY_NORMAL, "%llu files processed with errors!\n", count_errors );

    printToStderr( VERBOSITY_NORMAL, "\n" );

    // final freeing of resources
    if ( NULL != in ) {
        fclose( in );
    }
    if ( NULL != index ) {
        free_index( index );
    }
    if ( NULL != index_filename ) {
        free( index_filename );
    }
    if ( NULL != index_file ) {
        fclose( index_file );
    }

    if ( i > ( 1 + optind - ( (count_errors>0 && continue_on_error == 0)?1:0 ) ) ) {
        // if processing multiple files, return EXIT_GENERIC_ERROR if any one failed:
        if ( count_errors > 0 )
            return EXIT_GENERIC_ERROR;
        else
            return EXIT_OK;
    } else
        return ret_value;

}
