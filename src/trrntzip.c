//Copyright (C) 2005 TorrentZip Team (StatMat,shindakun,Ultrasubmarine,r3nh03k)
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later
//version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <utime.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <inttypes.h>

#define NDEBUG
#include <assert.h>

#include "zip.h"
#include "unzip.h"

#include "global.h"
#include "util.h"
#include "logging.h"

// We must change this at every new version
#define TZ_VERSION      "0.9"

#define MEGABYTE        1048576
#define ARRAY_ELEMENTS  256
#define ENDHEADERMAGIC  (0x06054b50)
#define COMMENT_LENGTH  22 // strlen("TORRENTZIPPED-XXXXXXXX")
#define DIVIDER         "--------------------------------------------------"
#define TMP_FILENAME    "trrntzip-XXXXXX"

// CheckZipStatus return codes
#define STATUS_BAD_SLASHES -6   // Zip has \ characters instead of / characters
#define STATUS_CONTAINS_DIRS -5 // Zip has DIR entries that need deleted
#define STATUS_ALLOC_ERROR -4   // Couldn't allocate memory.
#define STATUS_ERROR -3         // Corrupted zipfile or file is not a zipfile.
#define STATUS_BAD_COMMENT -2   // No comment or comment is not in the proper format.
#define STATUS_OUT_OF_DATE -1   // Has proper comment, but zipfile has been changed.
#define STATUS_OK 0             // File is A-Okay.

WORKSPACE *AllocateWorkspace (void);
void FreeWorkspace (WORKSPACE * ws);
int StringCompare (const void *str1, const void *str2);
char **GetFileList (unzFile UnZipHandle, char **FileNameArray, int *piElements);
int CheckZipStatus (unz64_s * UnzipStream, WORKSPACE * ws);
int ShouldFileBeRemoved(int iArray, WORKSPACE * ws);
int ZipHasDirEntry (WORKSPACE * ws);
int MigrateZip (const char *zip_path, const char * pDir, WORKSPACE * ws, MIGRATE * mig);
int RecursiveMigrate (const char *pszRelPath, WORKSPACE * ws, MIGRATE * mig);
int RecursiveMigrateDir (const char *pszRelPath, WORKSPACE * ws);
int RecursiveMigrateTop (const char * pszRelPath, WORKSPACE * ws );
void DisplayMigrateSummary (MIGRATE * mig);

// The created zip file global comment used to identify files
// This will be appended with the CRC32 of the central directory
const static char *gszApp = { "TORRENTZIPPED-" };

// Stores the path we startup in. This is so we can write logs to the right
// place
char *pszStartPath = NULL;

// The global flags that can be set with commandline parms.
// Setup here so as to avoid having to pass them to a lot of functions.
char qForceReZip = 0;
char qGUILaunch = 0;
char qNoRecursion = 0;
char qStripSubdirs = 0;

// Global flag to determine if any zipfile errors were detected
char qErrors = 0;

WORKSPACE *
AllocateWorkspace (void)
{
  WORKSPACE *ws = calloc (1, sizeof (WORKSPACE));

  if (ws == NULL)
    return NULL;

  // Allocate buffer for unpacking files into.
  ws->iBufSize = 4 * MEGABYTE;
  ws->pszUncompBuf = malloc (ws->iBufSize);

  if (ws->pszUncompBuf == NULL)
  {
    free (ws);
    return NULL;
  }

  // Allocate buffer for zip status checking.
  ws->iCheckBufSize = 10 * 1024;
  ws->pszCheckBuf = malloc (ws->iBufSize);

  if (ws->pszCheckBuf == NULL)
  {
    free (ws->pszCheckBuf);
    free (ws);
    return NULL;
  }

  // Allocate DynamicStringArray to hold filenames of zipped files.
  ws->iElements = ARRAY_ELEMENTS;
  ws->FileNameArray = DynamicStringArrayCreate (ws->iElements);

  if (!ws->FileNameArray)
  {
    free (ws->pszUncompBuf);
    free (ws->pszCheckBuf);
    free (ws);
    return NULL;
  }

  // Set up the dates just like MAMEZip
  // 1996 12 24 23:32 GMT+1 (MAME's first release date)
  ws->zi.tmz_date.tm_sec = 0;
  ws->zi.tmz_date.tm_min = 32;
  ws->zi.tmz_date.tm_hour = 23;
  ws->zi.tmz_date.tm_mday = 24;
  ws->zi.tmz_date.tm_mon = 11;
  ws->zi.tmz_date.tm_year = 1996;

  // Do not set file type (ASCII, BINARY)
  ws->zi.internal_fa = 0;
  // Do not use any RASH (Read only, Archive, System, Hidden) values
  ws->zi.external_fa = 0;
  ws->zi.dosDate = 0;

  return ws;
}

void
FreeWorkspace (WORKSPACE * ws)
{
  if (ws->FileNameArray)
    DynamicStringArrayDestroy (ws->FileNameArray, ws->iElements);
  if (ws->pszCheckBuf != NULL)
    free (ws->pszCheckBuf);
  free (ws->pszUncompBuf);
  free (ws);
}

// Get the filelist from the zip file in canonical order
// Returns a sorted array
char **
GetFileList (unzFile UnZipHandle, char **FileNameArray, int *piElements)
{
  int rc = 0;
  int iCount = 0;
  char **TmpPtr = NULL;

  unz_file_info64 ZipInfo;

  // The contents of the input array will be over-written.

  rc = unzGoToFirstFile (UnZipHandle);

  while (!rc)
  {
    // Our dynamic array is no longer big enough for all
    // the filenames, so we have to grow the array size
    if ((iCount + 2) >= *piElements)
    {
      // Grow array geometrically.
      FileNameArray = DynamicStringArrayResize (FileNameArray, piElements, *piElements*2);
      if (!FileNameArray)
        return NULL;
    }

    unzGetCurrentFileInfo64 (UnZipHandle, &ZipInfo, FileNameArray[iCount], MAX_PATH, NULL, 0, NULL, 0);

    rc = unzGoToNextFile (UnZipHandle);
    iCount++;
  }

  FileNameArray[iCount][0] = 0;

  // Sort the dynamic array into canonical order
  qsort (FileNameArray, iCount, sizeof (char **), StringCompare);

  return (FileNameArray);
}

int
CheckZipStatus (unz64_s * UnzipStream, WORKSPACE * ws)
{
  unsigned long checksum, target_checksum = 0;
  off_t ch_length = UnzipStream->size_central_dir;
  off_t ch_offset = UnzipStream->central_pos - UnzipStream->size_central_dir;
  off_t i = 0;
  char comment_buffer[COMMENT_LENGTH + 1];
  char *ep = NULL;
  unsigned char x;
  FILE *f = (FILE *) UnzipStream->filestream;

  // Quick check that the file at least appears to be a zip file.
  rewind (f);
  if (fgetc (f) != 'P' && fgetc (f) != 'K')
    return STATUS_ERROR;

  // Assume a TZ style archive comment and read it in. This is located at the very end of the file.
  comment_buffer[COMMENT_LENGTH] = 0;
  if (fseeko (f, -COMMENT_LENGTH, SEEK_END))
    return STATUS_ERROR;

  fread (comment_buffer, 1, COMMENT_LENGTH, f);

  // Check static portion of comment.
  if (strncmp (gszApp, comment_buffer, COMMENT_LENGTH - 8))
    return STATUS_BAD_COMMENT;

  // Parse checksum portion of the comment.
  errno = 0;
  target_checksum = strtoul (comment_buffer + COMMENT_LENGTH - 8, &ep, 16);
  // Check to see if stroul was able to parse the entire checksum.
  if (errno || ep != comment_buffer + COMMENT_LENGTH)
    return STATUS_BAD_COMMENT;

  // Comment checks out so seek to 4 before it...
  if (fseeko (f, -(COMMENT_LENGTH + 4), SEEK_END))
    return STATUS_ERROR;

  if (ch_length > ws->iCheckBufSize)
  {
    ws->pszCheckBuf = realloc (ws->pszCheckBuf, ch_length + 1024);
    if (ws->pszCheckBuf == NULL)
      return STATUS_ALLOC_ERROR;
    else
      ws->iCheckBufSize = ch_length + 1024;
  }

  // Skip to start of the central header, and read it in.
  if (fseeko (f, ch_offset, SEEK_SET))
    return STATUS_ERROR;
 
  fread (ws->pszCheckBuf, 1, ch_length, f);

  // Calculate the crc32 of the central header.
  checksum = crc32 (crc32 (0L, NULL, 0), ws->pszCheckBuf, ch_length);

  return checksum == target_checksum ? STATUS_OK : STATUS_OUT_OF_DATE;
}

//check if the zip file entry is a directory that should be removed
//directory should not be removed if it is an empty directory
int
ShouldFileBeRemoved(int iArray, WORKSPACE * ws)
{
  char *pszZipName = NULL;
  pszZipName = strrchr (ws->FileNameArray[iArray], '/');

  // if '/' found and it is the last character in the filename
  if (pszZipName && !*(pszZipName + 1))
  {
    // If not the last file
    if (strlen (ws->FileNameArray[iArray+1]))
    {
  	  int c=0;
  	  while (ws->FileNameArray[iArray][c]==ws->FileNameArray[iArray+1][c] && ws->FileNameArray[iArray][c] && ws->FileNameArray[iArray+1][c])
  		c++;

	  if (!ws->FileNameArray[iArray][c])
	  {
	    // dirs need removed
	    return 1;
	  }
    }
  }
  return 0;

}

// find if the zipfiles contains any dir entries that should be removed
int
ZipHasDirEntry (WORKSPACE * ws)
{
  int iArray = 0;
  for (iArray = 0; strlen (ws->FileNameArray[iArray]); iArray++)
  {
	if (ShouldFileBeRemoved(iArray,ws))
	  return 1;
  }
  return 0;
}

int
FindAndFixBadSlashes (WORKSPACE * ws)
{
  int slashFound=0;

  int iArray = 0;
  for (iArray = 0; strlen (ws->FileNameArray[iArray]); iArray++)
  {
	 int c=0;
	 while (ws->FileNameArray[iArray][c])
	 {
	     if(ws->FileNameArray[iArray][c]=='\\')
		 {
     		 ws->FileNameArray[iArray][c]='/';
		     slashFound=1;
	     }
	     c++;
	 }
  }
  return slashFound;
}


int
MigrateZip (const char *zip_path, const char * pDir, WORKSPACE * ws, MIGRATE * mig)
{
  unz_file_info64 ZipInfo;
  unzFile UnZipHandle = NULL;
  unz64_s *UnzipStream = NULL;
  zipFile ZipHandle = NULL;
  int zip64 = 0;

  // Used for CRC32 calc of central directory during rezipping
  zip64_internal *zintinfo;
  linkedlist_datablock_internal *ldi;

  // Used for our dynamic filename array
  int iArray = 0;
  int iArray2 = 0;

  int rc = 0;
  int error = 0;

  char szTmpBuf[MAX_PATH + 1];
  char szFileName[MAX_PATH + 1];
  char szZipFileName[MAX_PATH + 1];
  char szTmpZipFileName[MAX_PATH + 1];
  char *pszZipName = NULL;

  int iBytesRead = 0;

  off_t cTotalBytesInZip = 0;
  unsigned int cTotalFilesInZip = 0;

  // Use to store the CRC32 of the central directory
  unsigned long crc = 0;

  if ( strcmp( pDir, "." ) == 0 )
  {
    sprintf( szTmpZipFileName, "%s", TMP_FILENAME );
    sprintf( szZipFileName, "%s", zip_path );
  } else {
    sprintf( szTmpZipFileName, "%s%c%s", pDir, DIRSEP, TMP_FILENAME );
    sprintf( szZipFileName, "%s%c%s", pDir, DIRSEP, zip_path );
  }
  mktemp(szTmpZipFileName);
  
  if (!access (szTmpZipFileName, F_OK) && remove (szTmpZipFileName))
  {
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "\n!!!! Temporary file exists and could not be automatically removed. Please remove the file %s and re-run this program. !!!!\n", szTmpZipFileName);
    return TZ_CRITICAL;
  }

  if (access (szZipFileName, R_OK|W_OK))
  {
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Error opening \"%s\". %s.\n", szZipFileName, strerror(errno));
    return TZ_ERR;
  }

  if ((UnZipHandle = unzOpen64 (szZipFileName)) == NULL)
  {
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Error opening \"%s\", zip format problem. Unable to process zip.\n", szZipFileName);
    return TZ_ERR;
  }

  UnzipStream = (unz64_s *) UnZipHandle;

  // Check if zip is non-TZ or altered-TZ
  rc = CheckZipStatus (UnzipStream, ws);

  switch (rc)
  {
  case STATUS_ERROR:
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Unable to process \"%s\". It seems to be corrupt.\n", szZipFileName);
    unzClose (UnZipHandle);
    return TZ_ERR;

  case STATUS_ALLOC_ERROR:
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Error allocating memory!\n");
    unzClose (UnZipHandle);
    return TZ_CRITICAL;
	
  case STATUS_OK:
  case STATUS_OUT_OF_DATE:
  case STATUS_BAD_COMMENT:
    // Continue to Re-zip this zip.
    break;

  default:
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Bad return on CheckZipStatus!\n");
    unzClose (UnZipHandle);
    return TZ_CRITICAL;
  }



  CHECK_DYNAMIC_STRING_ARRAY( ws->FileNameArray, ws->iElements );
  // Get the filelist from the zip file in canonical order
  ws->FileNameArray = GetFileList (UnZipHandle, ws->FileNameArray, &ws->iElements);
  
  CHECK_DYNAMIC_STRING_ARRAY( ws->FileNameArray, ws->iElements );

  // GetFileList couldn't allocate enough memory to store the filelist
  if (!ws->FileNameArray)
  {
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Error allocating memory!\n");
    unzClose (UnZipHandle);
    return TZ_CRITICAL;
  }

  // check if the zip has any \ characters in the filename that should be changed to /
  // the \ is an invalid directory, and should always be /
  if (FindAndFixBadSlashes(ws))
     rc=STATUS_BAD_SLASHES;
  
  if (rc==STATUS_OK && !qForceReZip)
  {
    // check if the zip has any dir entries that need removed
	if (ZipHasDirEntry(ws))
    {
      rc=STATUS_CONTAINS_DIRS;
    } else {
	  logprint (stdout, mig->fProcessLog, "Skipping, already TorrentZipped - %s\n", szZipFileName);
      unzClose (UnZipHandle);
      return TZ_SKIPPED;
    }
  }
  
  logprint (stdout, mig->fProcessLog, "Rezipping - %s\n", szZipFileName);
  logprint (stdout, mig->fProcessLog, "%s\n", DIVIDER);
  
  if ((ZipHandle = zipOpen64 (szTmpZipFileName, 0)) == NULL)
  {
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Error creating temporary zip file %s. Unable to process \"%s\"\n", szTmpZipFileName, szZipFileName);
    unzClose (UnZipHandle);
    return TZ_ERR;
  }

  for (iArray = 0; iArray < ws->iElements && ws->FileNameArray[iArray][0]; iArray++)
  {
    strcpy (szFileName, ws->FileNameArray[iArray]);
    rc = unzLocateFile (UnZipHandle, szFileName, 0);
    zip64 = 0;

    if (rc == UNZ_OK)
    {
      rc = unzGetCurrentFileInfo64 (UnZipHandle, &ZipInfo, szFileName, MAX_PATH, NULL, 0, NULL, 0);
      if (rc == UNZ_OK)
        rc = unzOpenCurrentFile (UnZipHandle);
    }

    if (rc != UNZ_OK)
    {
      logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Unable to open \"%s\" from \"%s\"\n", szFileName, szZipFileName);
      error = 1;
      break;
    }

	//check if the file is a DIR entry that should be removed
	if (ShouldFileBeRemoved(iArray,ws))
	{
 	  // remove this file.
	  logprint (stdout, mig->fProcessLog, "Directory %s Removed\n", szFileName);
	  continue;
    }

    // files >= 4G need to be zip64
    if(ZipInfo.uncompressed_size >= 0xFFFFFFFF)
     zip64 = 1;

    logprint (stdout, mig->fProcessLog, "Adding - %s (%" PRIu64 " bytes%s)...", szFileName, ZipInfo.uncompressed_size, (zip64 ? ", Zip64" : ""));

    if (qStripSubdirs)
    {
      // To strip off path if there is one
      pszZipName = strrchr (szFileName, '/');

      if (pszZipName)
      {
        if (!*(pszZipName + 1))
          continue; // Last char was '/' so is dir entry. Skip it.

        pszZipName = pszZipName + 1;
      }
      else
        pszZipName = szFileName;

      strcpy (ws->FileNameArray[iArray], pszZipName);

      // Search for duplicate files
      for ( iArray2 = iArray-1; iArray2 >= 0; iArray2-- )
      {
        if (!strcmp (pszZipName, ws->FileNameArray[iArray2]))
        {
          error = 1;
          break;
        }
      }

      if (error)
      {
        logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Zip file \"%s\" contains more than one file named \"%s\"\n", szZipFileName, pszZipName);
        break;
      }
    }
    else
      pszZipName = szFileName;

    rc = zipOpenNewFileInZip64 (ZipHandle, pszZipName, &ws->zi, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_BEST_COMPRESSION, zip64);

    if (rc != ZIP_OK)
    {
      logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Unable to open \"%s\" in replacement zip \"%s\"\n", pszZipName, szTmpZipFileName);
      error = 1;
      break;
    }

    for (;;)
    {
      iBytesRead = unzReadCurrentFile (UnZipHandle, ws->pszUncompBuf, ws->iBufSize);

      if (!iBytesRead)
      { // All bytes have been read.
        break;
      }

      if (iBytesRead < 0) // Error.
      {
        logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Error while reading \"%s\" from \"%s\"\n", szFileName, szZipFileName);
        error = 1;
        break;
      }

      rc = zipWriteInFileInZip (ZipHandle, ws->pszUncompBuf, iBytesRead);

      if (rc != ZIP_OK)
      {
        logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Error while adding \"%s\" to replacement zip \"%s\"\n", pszZipName, szTmpZipFileName);
        error = 1;
        break;
      }

      cTotalBytesInZip += iBytesRead;
    }

    if (error)
      break;

    rc = unzCloseCurrentFile (UnZipHandle);

    if (rc != UNZ_OK)
    {
      if (rc == UNZ_CRCERROR)
        logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "CRC error in \"%s\" in \"%s\"!\n", szFileName, szZipFileName);
      else
        logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Error while closing \"%s\" in \"%s\"!\n", szFileName, szZipFileName);
      
	  error = 1;
      break;
    }

    rc = zipCloseFileInZip (ZipHandle);

    if (rc != ZIP_OK)
    {
      logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Error closing \"%s\" in new zip file \"%s\"!\n", pszZipName, szTmpZipFileName);
      error = 1;
      break;
    }

    logprint (stdout, mig->fProcessLog, "Done\n");

    cTotalFilesInZip++;
  }

  // If there was an error above then clean up and return.
  if (error)
  {
    fprintf(mig->fProcessLog, "Not done\n");
    unzClose (UnZipHandle);
    zipClose (ZipHandle, NULL);
    remove (TMP_FILENAME);
    return TZ_ERR;
  }

  logprint (stdout, mig->fProcessLog, "%s\n", DIVIDER);

  unzClose (UnZipHandle);

  // Before we close the file, we need to calc the CRC32 of
  // the central directory (for detecting a changed TZ file later)
  zintinfo = (zip64_internal *) ZipHandle;
  crc = crc32 (0L, Z_NULL, 0);
  ldi = zintinfo->central_dir.first_block;
  while (ldi != NULL)
  {
    crc = crc32 (crc, ldi->data, ldi->filled_in_this_block);
    ldi = ldi->next_datablock;
  }

  // Set the global file comment, so that we know to skip this file in future
  sprintf (szTmpBuf, "%s%08lX", gszApp, crc);

  rc = zipClose (ZipHandle, szTmpBuf);

  if (rc != UNZ_OK)
  {
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "Unable to close temporary zip file \"%s\" - cannot process \"%s\"!\n", szTmpZipFileName, szZipFileName);
    remove (szTmpZipFileName);
    return TZ_ERR;
  }

#ifdef WIN32
  // This is here because rename does not atomically replace
  // an existing destination on WIN32.
  if (remove (szZipFileName))
  {
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "!!!! Unable to remove \"%s\" for replacement with rezipped copy.\n", szZipFileName);
    if (remove (szTmpZipFileName))
      logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "!!!! Could not remove temporary file \"%s\".\n", szTmpZipFileName);
 
 return TZ_ERR;
  }
#endif

  // rename atomically replaces the destination if it exists.
  if (rename (szTmpZipFileName, szZipFileName))
  {
#ifdef WIN32
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "!!!! Could not rename temporary file \"%s\" to \"%s\". The original file has already been deleted, "
              "so you must rename this file manually before re-running the program. Failure to do so will cause you to lose the contents of the file.\n", szTmpZipFileName, szZipFileName);
#else
    logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "!!!! Could not rename temporary file \"%s\" to \"%s\".\n", szTmpZipFileName, szZipFileName);
    if (remove (szTmpZipFileName))
      logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "!!!! Could not remove temporary file \"%s\".\n", szTmpZipFileName);
#endif
    return TZ_CRITICAL;
  }

  logprint (stdout, mig->fProcessLog, "Rezipped %u compressed file%s totaling %" PRIu64 " bytes.\n", cTotalFilesInZip, cTotalFilesInZip != 1 ? "s" : "", cTotalBytesInZip);

  return TZ_OK;
}

// Get the filelist from the pszPath directory in canonical order
// Returns a sorted array
char **
GetDirFileList (const char* pszPath, int *piElements)
{
  int iCount = 0;
  DIR *dirp = NULL;
  struct dirent *direntp = NULL;
  char **FileNameArray = 0;

  *piElements = ARRAY_ELEMENTS;
  FileNameArray = DynamicStringArrayCreate (*piElements);
  if (!FileNameArray)
    return NULL;

  dirp = opendir (pszPath);

  if (dirp)
  {
    while (direntp = readdir (dirp))
    {
      if (iCount+2 >= *piElements)
      {
        // Grow array geometrically.
        FileNameArray = DynamicStringArrayResize( FileNameArray, piElements, *piElements*2 );
        if (!FileNameArray)
          return NULL;
      }
      strncpy ( FileNameArray[iCount], direntp->d_name, MAX_PATH + 1 );
      iCount++;
    }
    closedir (dirp);
  }

  FileNameArray[iCount][0] = 0;

  // Sort the dynamic array into canonical order
  qsort (FileNameArray, iCount, sizeof (char **), StringCompare);

  return (FileNameArray);
}

// Function to convert a dir or zip
int
RecursiveMigrate (const char *pszRelPath, WORKSPACE * ws, MIGRATE * mig)
{
  int rc = 0;

  char szRelPathBuf[MAX_PATH + 1];
  const char* pszFileName = NULL;

  struct stat istat;

  pszFileName = strrchr (pszRelPath, DIRSEP);
  if (pszFileName)
  {
    memcpy (szRelPathBuf, pszRelPath, pszFileName - pszRelPath);
    szRelPathBuf[pszFileName-pszRelPath] = 0;
    pszFileName++;
  } else {
    sprintf( szRelPathBuf, "." );
    pszFileName = pszRelPath;
  }

  rc = stat (pszRelPath, &istat);
  if (rc)
    logprint (stderr, ws->fErrorLog, "Could not stat \"%s\". %s\n", pszRelPath, strerror(errno) );

  if (S_ISDIR (istat.st_mode))
  {
    if (!qNoRecursion && strcmp (pszFileName, ".") && strcmp (pszFileName, ".."))
    {
      // Get our execution time (in seconds)
      // for the conversion process so far
      mig->ExecTime += difftime (time (NULL), mig->StartTime);

      rc = RecursiveMigrateDir (pszRelPath, ws);

      // Restart the timing for this instance of RecursiveMigrate()
      mig->StartTime = time (NULL);
    }
  }
  else if (EndsWithCaseInsensitive(pszFileName, ".zip") == 0)
  {
    mig->cEncounteredZips++;

    if (!mig->fProcessLog)
    {
      if (strcmp( szRelPathBuf, "." ) == 0)
        rc = OpenProcessLog (pszStartPath, pszFileName, mig);
      else
        rc = OpenProcessLog (pszStartPath, szRelPathBuf, mig);

      if (rc != TZ_OK)
        return TZ_CRITICAL;
    }

    if (istat.st_size > COMMENT_LENGTH)
    {
      rc = MigrateZip (pszFileName, szRelPathBuf, ws, mig);

      switch (rc)
      {
      case TZ_OK:
        mig->cRezippedZips++;
        break;
      case TZ_ERR:
        mig->cErrorZips++;
        mig->bErrorEncountered = 1;
        break;
      case TZ_CRITICAL:
        break;
      case TZ_SKIPPED:
        mig->cOkayZips++;
      }
    }
    else // Too small to be a valid zip file.
    {
      logprint3 (stderr, mig->fProcessLog, ws->fErrorLog, "\"%s\" is too small (%d byte%s). File may be corrupt.\n", pszRelPath, istat.st_size, istat.st_size==1 ? "" : "s");
      mig->cErrorZips++;
      mig->bErrorEncountered = 1;
    }
  }

  return rc == TZ_CRITICAL ? TZ_CRITICAL : TZ_OK;
}

// Function to convert the contents of a directory.
// This function only receives directories, not files or zips.
int
RecursiveMigrateDir (const char *pszRelPath, WORKSPACE * ws)
{
  int rc = 0;

  char szTmpBuf[MAX_PATH + 1];
  char szRelPathBuf[MAX_PATH + 1];
  int iElements = 0;
  char **FileNameArray = NULL;
  int iCounter = 0;
  int n;
  int FileNameStartPos;

  DIR *dirp = NULL;
  struct dirent *direntp = NULL;
  struct stat istat;

  MIGRATE mig;

  memset (&mig, 0, sizeof (MIGRATE));

  // Get our start time for the conversion process of this dir/zip
  mig.StartTime = time (NULL);

  stat (pszRelPath, &istat);

  if (!S_ISDIR (istat.st_mode))
  {
    logprint (stderr, ws->fErrorLog, "Internal error: RecursiveMigrateDir called on non-directory \"%s\"!\n", pszRelPath);
    mig.bErrorEncountered = 1;
    return TZ_CRITICAL;
  }

  // Couldn't access specified path
  dirp = opendir (pszRelPath);
  if (!dirp)
  {
    logprint (stderr, ws->fErrorLog, "Could not access subdir \"%s\"! %s\n", pszRelPath, strerror(errno));
    mig.bErrorEncountered = 1;
  } else {
    closedir (dirp);
  }

  if (!mig.bErrorEncountered)
  {
    FileNameArray = GetDirFileList (pszRelPath, &iElements);
    if (!FileNameArray) {
      logprint (stderr, ws->fErrorLog, "Error allocating memory!\n");
      rc = TZ_CRITICAL;
    } else {
      if ( strcmp( pszRelPath, "." ) == 0 )
      {
        szTmpBuf[0] = 0;
        FileNameStartPos = 0;
      } else {
        FileNameStartPos = sprintf (szTmpBuf, "%s%c", pszRelPath, DIRSEP );
      }

      for (iCounter=0; (iCounter < iElements && FileNameArray[iCounter][0]); iCounter++)
      {
        sprintf (szTmpBuf+FileNameStartPos, "%s", FileNameArray[iCounter]);
        rc = RecursiveMigrate (szTmpBuf, ws, &mig);
        if (rc == TZ_CRITICAL)
          break;
      }
    }

    DynamicStringArrayDestroy (FileNameArray, iElements);

  }

  // Get our execution time (in seconds) for the conversion process 
  mig.ExecTime += difftime (time (NULL), mig.StartTime);

  if (rc != TZ_CRITICAL)
    DisplayMigrateSummary (&mig);

  return rc == TZ_CRITICAL ? TZ_CRITICAL : TZ_OK;
}

void DisplayMigrateSummary (MIGRATE * mig)
{
  double ExecTime;

  if(mig->fProcessLog)
  {
    ExecTime = mig->ExecTime;
    // Output it in hours, minutes and seconds
    logprint (stdout, mig->fProcessLog, "Execution time %d hours ",
              (int) ExecTime / (60 * 60));
    ExecTime = fmod (ExecTime, (60 * 60));
    logprint (stdout, mig->fProcessLog, "%d mins ", (int) ExecTime / 60);
    ExecTime = fmod (ExecTime, 60);
    logprint (stdout, mig->fProcessLog, "%d secs\n\n", (int) ExecTime);
    logprint (stdout, mig->fProcessLog, "Checked %u zip file%s.\n",
              mig->cEncounteredZips, mig->cEncounteredZips != 1 ? "s" : "");
    if (mig->cRezippedZips)
      logprint (stdout, mig->fProcessLog, "  %u file%s successfully rezipped.\n", mig->cRezippedZips, mig->cRezippedZips != 1 ? "s were" : " was");
    if (mig->cOkayZips)
      logprint (stdout, mig->fProcessLog, "  %u file%s already up to date.\n", mig->cOkayZips, mig->cOkayZips != 1 ? "s were" : " was");
    if (mig->cErrorZips)
      logprint (stdout, mig->fProcessLog, "  %u file%s had errors.\n", mig->cErrorZips, mig->cErrorZips != 1 ? "s" : "");

    if (mig->bErrorEncountered)
    {
      logprint (stdout, mig->fProcessLog, "!!!! There were problems! See \"%serror.log\" for details! !!!!\n", pszStartPath);
      qErrors = 1;
    }

    fclose (mig->fProcessLog);
    mig->fProcessLog = 0;
  }
}

int RecursiveMigrateTop (const char * pszRelPath, WORKSPACE * ws )
{
  int rc;
  MIGRATE mig;
  char szRelPathBuf[MAX_PATH + 1];
  int n;

  memset (&mig, 0, sizeof (MIGRATE));

  mig.StartTime = time (NULL);

  n = strlen( pszRelPath );
  if ( n > 0 && pszRelPath[n-1] == DIRSEP )
  {
    sprintf (szRelPathBuf, "%s", pszRelPath);
    szRelPathBuf[n-1] = 0;
    pszRelPath = szRelPathBuf;
  }

  rc = RecursiveMigrate (pszRelPath, ws, &mig);

  // Get our execution time (in seconds) for the conversion process 
  mig.ExecTime += difftime (time (NULL), mig.StartTime);

  if (rc != TZ_CRITICAL)
    DisplayMigrateSummary (&mig);

  return rc == TZ_CRITICAL ? TZ_CRITICAL : TZ_OK;
}

int
main (int argc, char **argv)
{
  WORKSPACE *ws;
  int iCount = 0;
  int iOptionsFound = 0;
  int rc = 0;
  char *ptr = NULL;
  char szStartPath[MAX_PATH + 1];
  char szErrorLogFileName[MAX_PATH + 1];

  for (iCount = 1 ; iCount < argc ; iCount++)
  { 
    if (argv[iCount][0] == '-') 
    {
      iOptionsFound++;
      strlwr (argv[iCount]);

      switch (argv[iCount][1])
      {
      case '?':
        fprintf (stdout, "\nTorrentZip v%s\n\n", TZ_VERSION);
        fprintf (stdout, "Copyright (C) 2012 TorrentZip Team :\n");
        fprintf (stdout, "StatMat, shindakun, Ultrasubmarine, r3nh03k, goosecreature, gordonj\n");
        fprintf (stdout, "Homepage : http://sourceforge.net/projects/trrntzip\n\n");
        fprintf (stdout, "Usage: trrntzip [OPTIONS] [PATH/ZIP FILE]\n\n");
        fprintf (stdout, "Options:\n\n");
        fprintf (stdout, "-? : show this help\n");
        fprintf (stdout, "-d : strip sub-directories from zips\n");
        fprintf (stdout, "-s : prevent sub-directory recursion\n");
        fprintf (stdout, "-f : force re-zip\n");
        fprintf (stdout, "-v : show version\n");
        fprintf (stdout, "-g : pause when finished\n");
        return TZ_OK;

      case 'd':
        // Strip subdirs from zips
        qStripSubdirs = 1;
        break;

      case 's':
        // Disable dir recursion
        qNoRecursion = 1;
        break;

      case 'f':
        // Force rezip
		qForceReZip = 1;
		break;

      case 'v':
        // GUI requesting TZ version
        fprintf (stdout, "TorrentZip v%s\n", TZ_VERSION);
        return TZ_OK;

      case 'g':
        // GUI launch process
        qGUILaunch = 1;
        break;

      default:
        fprintf (stderr, "Unknown option : %s\n", argv[iCount]);
      }
    }
  }

  if (argc < 2 || iOptionsFound == (argc - 1))
  {
    fprintf (stderr, "\ntrrntzip: missing path\n");
    fprintf (stderr, "Usage: trrntzip [OPTIONS] [PATH/ZIP FILE]\n");
    return TZ_ERR;
  }

  ws = AllocateWorkspace ();

  if (ws == NULL)
  {
    fprintf (stderr, "Error allocating memory!\n");
    return TZ_CRITICAL;
  }

#ifdef WIN32
  // Must get trrntzip.exe path from argv[0] and
  // change to it. Under windows, if you drag a dir to
  // the exe, it will use the user's "Documents and Settings"
  // dir if we don't do this.
  ptr = strrchr (argv[0], DIRSEP);
  if (ptr)
  {
    *ptr = '\0';
    sprintf (szStartPath, "%s", argv[0]);
    pszStartPath = szStartPath;
  } else {
    pszStartPath = get_cwd();
  }
#else
  pszStartPath = get_cwd();
#endif

  if (!pszStartPath)
  {
    fprintf (stderr, "Could not get startup path!\n");
    return TZ_ERR;
  }

  ws->fErrorLog = OpenErrorLog (qGUILaunch);

  if (ws->fErrorLog)
  {
    // Start process for each passed path/zip file
    for(iCount = iOptionsFound + 1 ; iCount < argc ; iCount++)
    {
      rc = RecursiveMigrateTop (argv[iCount], ws);
    }

    fclose (ws->fErrorLog);

    if (qErrors)
    {
      #ifdef WIN32
      // This is only needed on Windows, to keep the
      // command window window from disappearing when 
      // the program completes.
      if (!qGUILaunch)
      {
        fprintf (stdout, "Press any key to exit.\n");
        fflush (stdout);
        getch ();
      }
      #endif
    } else {
      sprintf (szErrorLogFileName, "%s%c%s", pszStartPath, DIRSEP, "error.log");
      remove (szErrorLogFileName);
    }
  }

  FreeWorkspace (ws);
  if (pszStartPath != szStartPath)
  {
    free(pszStartPath);
  }

  return rc;
}
