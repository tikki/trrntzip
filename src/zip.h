/* zip.h -- IO for compress .zip files using zlib
   Version 1.01, May 8th, 2004
   
   Slightly modified by StatMat to expose internal data
   28th March, 2005

   Copyright (C) 1998-2004 Gilles Vollant

   This unzip package allow creates .ZIP file, compatible with PKZip 2.04g
     WinZip, InfoZip tools and compatible.
   Encryption and multi volume ZipFile (span) are not supported.
   Old compressions used by old PKZip 1.x are not supported

  For uncompress .zip file, look at unzip.h


   I WAIT FEEDBACK at mail info@winimage.com
   Visit also http://www.winimage.com/zLibDll/unzip.html for evolution

   Condition of use and distribution are the same than zlib :

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


*/

/* for more info about .ZIP format, see
      http://www.info-zip.org/pub/infozip/doc/appnote-981119-iz.zip
      http://www.info-zip.org/pub/infozip/doc/
   PkWare has also a specification at :
      ftp://ftp.pkware.com/probdesc.zip
*/

#ifndef _zip_H
#define _zip_H

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef _ZLIB_H
#include "zlib.h"
#endif

#ifndef _ZLIBIOAPI_H
#include "ioapi.h"
#endif

/*************************************************************/
/*************************************************************/

// This section has been moved here from within zip.c. This
// allows us to get to the internal data from within TorrentZip
// StatMat - 28/03/05

#ifndef Z_BUFSIZE
#define Z_BUFSIZE (16384)
#endif

#define SIZEDATA_INDATABLOCK (4096-(4*4))

#define LOCALHEADERMAGIC    (0x04034b50)
#define CENTRALHEADERMAGIC  (0x02014b50)
#define ENDHEADERMAGIC      (0x06054b50)

#define FLAG_LOCALHEADER_OFFSET (0x06)
#define CRC_LOCALHEADER_OFFSET  (0x0e)

#define SIZECENTRALHEADER (0x2e) /* 46 */

  typedef struct linkedlist_datablock_internal_s
  {
    struct linkedlist_datablock_internal_s *next_datablock;
    uLong avail_in_this_block;
    uLong filled_in_this_block;
    uLong unused;               /* for future use and alignement */
    unsigned char data[SIZEDATA_INDATABLOCK];
  } linkedlist_datablock_internal;

  typedef struct linkedlist_data_s
  {
    linkedlist_datablock_internal *first_block;
    linkedlist_datablock_internal *last_block;
  } linkedlist_data;


  typedef struct
  {
    z_stream stream;            /* zLib stream structure for inflate */
    int stream_initialised;     /* 1 is stream is initialised */
    uInt pos_in_buffered_data;  /* last written byte in buffered_data */

    uLong pos_local_header;     /* offset of the local header of the file
                                   currenty writing */
    char *central_header;       /* central header data for the current file */
    uLong size_centralheader;   /* size of the central header for cur file */
    uLong flag;                 /* flag of the file currently writing */

    int method;                 /* compression method of file currenty wr. */
    int raw;                    /* 1 for directly writing raw data */
    Byte buffered_data[Z_BUFSIZE]; /* buffer contain compressed data to be
                                      writ */
    uLong dosDate;
    uLong crc32;
    int encrypt;
#ifndef NOCRYPT
    unsigned long keys[3];      /* keys defining the pseudo-random sequence */
    const unsigned long *pcrc_32_tab;
    int crypt_header_size;
#endif
  } curfile_info;

  typedef struct
  {
    zlib_filefunc_def z_filefunc;
    voidpf filestream;          /* io structore of the zipfile */
    linkedlist_data central_dir; /* datablock with central dir in
                                    construction */
    int in_opened_file_inzip;   /* 1 if a file in the zip is currently writ. */
    curfile_info ci;            /* info on the file curretly writing */

    uLong begin_pos;            /* position of the beginning of the zipfile */
    uLong add_position_when_writting_offset;
    uLong number_entry;
  } zip_internal;

/*************************************************************/
/*************************************************************/

#if defined(STRICTZIP) || defined(STRICTZIPUNZIP)
/* like the STRICT of WIN32, we define a pointer that cannot be converted
    from (void*) without cast */
  typedef struct TagzipFile__
  {
    int unused;
  } zipFile__;
  typedef zipFile__ *zipFile;
#else
  typedef voidp zipFile;
#endif

#define ZIP_OK                          (0)
#define ZIP_EOF                         (0)
#define ZIP_ERRNO                       (Z_ERRNO)
#define ZIP_PARAMERROR                  (-102)
#define ZIP_BADZIPFILE                  (-103)
#define ZIP_INTERNALERROR               (-104)

#ifndef DEF_MEM_LEVEL
#  if MAX_MEM_LEVEL >= 8
#    define DEF_MEM_LEVEL 8
#  else
#    define DEF_MEM_LEVEL  MAX_MEM_LEVEL
#  endif
#endif
/* default memLevel */

/* tm_zip contain date/time info */
  typedef struct tm_zip_s
  {
    uInt tm_sec;                /* seconds after the minute - [0,59] */
    uInt tm_min;                /* minutes after the hour - [0,59] */
    uInt tm_hour;               /* hours since midnight - [0,23] */
    uInt tm_mday;               /* day of the month - [1,31] */
    uInt tm_mon;                /* months since January - [0,11] */
    uInt tm_year;               /* years - [1980..2044] */
  } tm_zip;

  typedef struct
  {
    tm_zip tmz_date;            /* date in understandable format */
    uLong dosDate;              /* if dos_date == 0, tmu_date is used */
    /*    uLong       flag;        *//* general purpose bit flag 2 bytes */

    uLong internal_fa;          /* internal file attributes 2 bytes */
    uLong external_fa;          /* external file attributes 4 bytes */
  } zip_fileinfo;

  typedef const char *zipcharpc;


#define APPEND_STATUS_CREATE        (0)
#define APPEND_STATUS_CREATEAFTER   (1)
#define APPEND_STATUS_ADDINZIP      (2)

  extern zipFile ZEXPORT zipOpen OF ((const char *pathname, int append));
/*
  Create a zipfile.
     pathname contain on Windows XP a filename like "c:\\zlib\\zlib113.zip" or on
       an Unix computer "zlib/zlib113.zip".
     if the file pathname exist and append==APPEND_STATUS_CREATEAFTER, the zip
       will be created at the end of the file.
         (useful if the file contain a self extractor code)
     if the file pathname exist and append==APPEND_STATUS_ADDINZIP, we will
       add files in existing zip (be sure you don't add file that doesn't exist)
     If the zipfile cannot be opened, the return value is NULL.
     Else, the return value is a zipFile Handle, usable with other function
       of this zip package.
*/

/* Note : there is no delete function into a zipfile.
   If you want delete file into a zipfile, you must open a zipfile, and create another
   Of couse, you can use RAW reading and writing to copy the file you did not want delte
*/

  extern zipFile ZEXPORT zipOpen2 OF ((const char *pathname,
                                       int append,
                                       zipcharpc * globalcomment,
                                       zlib_filefunc_def *
                                       pzlib_filefunc_def));

  extern int ZEXPORT zipOpenNewFileInZip OF ((zipFile file,
                                              const char *filename,
                                              const zip_fileinfo * zipfi,
                                              const void *extrafield_local,
                                              uInt size_extrafield_local,
                                              const void *extrafield_global,
                                              uInt size_extrafield_global,
                                              const char *comment,
                                              int method, int level));
/*
  Open a file in the ZIP for writing.
  filename : the filename in zip (if NULL, '-' without quote will be used
  *zipfi contain supplemental information
  if extrafield_local!=NULL and size_extrafield_local>0, extrafield_local
    contains the extrafield data the the local header
  if extrafield_global!=NULL and size_extrafield_global>0, extrafield_global
    contains the extrafield data the the local header
  if comment != NULL, comment contain the comment string
  method contain the compression method (0 for store, Z_DEFLATED for deflate)
  level contain the level of compression (can be Z_DEFAULT_COMPRESSION)
*/


  extern int ZEXPORT zipOpenNewFileInZip2 OF ((zipFile file,
                                               const char *filename,
                                               const zip_fileinfo * zipfi,
                                               const void *extrafield_local,
                                               uInt size_extrafield_local,
                                               const void *extrafield_global,
                                               uInt size_extrafield_global,
                                               const char *comment,
                                               int method,
                                               int level, int raw));

/*
  Same than zipOpenNewFileInZip, except if raw=1, we write raw file
 */

  extern int ZEXPORT zipOpenNewFileInZip3 OF ((zipFile file,
                                               const char *filename,
                                               const zip_fileinfo * zipfi,
                                               const void *extrafield_local,
                                               uInt size_extrafield_local,
                                               const void *extrafield_global,
                                               uInt size_extrafield_global,
                                               const char *comment,
                                               int method,
                                               int level,
                                               int raw,
                                               int windowBits,
                                               int memLevel,
                                               int strategy,
                                               const char *password,
                                               uLong crcForCtypting));

/*
  Same than zipOpenNewFileInZip2, except
    windowBits,memLevel,,strategy : see parameter strategy in deflateInit2
    password : crypting password (NULL for no crypting)
    crcForCtypting : crc of file to compress (needed for crypting)
 */


  extern int ZEXPORT zipWriteInFileInZip OF ((zipFile file,
                                              const void *buf, unsigned len));
/*
  Write data in the zipfile
*/

  extern int ZEXPORT zipCloseFileInZip OF ((zipFile file));
/*
  Close the current file in the zipfile
*/

  extern int ZEXPORT zipCloseFileInZipRaw OF ((zipFile file,
                                               uLong uncompressed_size,
                                               uLong crc32));
/*
  Close the current file in the zipfile, for fiel opened with
    parameter raw=1 in zipOpenNewFileInZip2
  uncompressed_size and crc32 are value for the uncompressed size
*/

  extern int ZEXPORT zipClose OF ((zipFile file, const char *global_comment));
/*
  Close the zipfile
*/

#ifdef __cplusplus
}
#endif

#endif                          /* _zip_H */
