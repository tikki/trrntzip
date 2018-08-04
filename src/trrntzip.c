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

#include "global.h"

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

#include "zip.h"
#include "unzip.h"

#include "util.h"
#include "logging.h"

// We must change this at every new version
#define TZ_VERSION      "0.2"

#define MEGABYTE        1048576
#define ARRAY_ELEMENTS  255
#define ENDHEADERMAGIC  (0x06054b50)
#define COMMENT_LENGTH  22 // strlen("TORRENTZIPPED-XXXXXXXX")
#define DIVIDER         "--------------------------------------------------\n"
#define TMP_FILENAME    "trrntzip.tmp"

// CheckZipStatus return codes
#define STATUS_ALLOC_ERROR -4 // Couldn't allocate memory.
#define STATUS_ERROR -3 // Corrupted zipfile or file is not a zipfile.
#define STATUS_BAD_COMMENT -2 // No comment or comment is not in the proper
                              // format.
#define STATUS_OUT_OF_DATE -1 // Has proper comment, but zipfile has been
                              // changed.
#define STATUS_OK 0 // File is A-Okay.

WORKSPACE *AllocateWorkspace (void);
void FreeWorkspace (WORKSPACE * ws);
int StringCompare (const void *str1, const void *str2);
char **GetFileList (unzFile UnZipHandle, char **FileNameArray,
                    int *piElements);
int CheckZipStatus (FILE * f, WORKSPACE * ws);
int MigrateZip (const char *zip_path, char * pDir, WORKSPACE * ws, MIGRATE * mig);
int RecursiveMigrate (const char *pszPath, WORKSPACE * ws);

// The created zip file global comment used to identify files
// This will be appended with the CRC32 of the central directory
const static char *gszApp = { "TORRENTZIPPED-" };

// Stores the path we startup in. This is so we can write logs to the right
// place
char *pszStartPath = NULL;

// The global flags that can be set with commandline parms.
// Setup here so as to avoid having to pass them to a lot of functions.
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
  {
    return NULL;
  }

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
  ws->FileNameArray =
    DynamicStringArray (ws->FileNameArray, ws->iElements, 1);

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
    DynamicStringArray (ws->FileNameArray, ws->iElements, 0);
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

  unz_file_info ZipInfo;

  rc = unzGoToFirstFile (UnZipHandle);

  while (!rc)
  {
    // Our dynamic array is no longer big enough for all
    // the filenames, so we have to grow the array size
    if ((iCount + 1) == *piElements)
    {
      TmpPtr = FileNameArray;
      FileNameArray =
        (char **) realloc (FileNameArray,
                           (*piElements + 2) * sizeof (char *));
      if (!FileNameArray)
      {
        DynamicStringArray (TmpPtr, *piElements, 0);
        return NULL;
      }
      FileNameArray[*piElements] = (char *) calloc (1, MAX_PATH + 1);
      if (!FileNameArray[*piElements])
      {
        DynamicStringArray (FileNameArray, *piElements, 0);
        return NULL;
      }
      FileNameArray[*piElements + 1] = (char *) calloc (1, MAX_PATH + 1);
      if (!FileNameArray[*piElements + 1])
      {
        DynamicStringArray (FileNameArray, *piElements + 1, 0);
        return NULL;
      }
      *piElements += 2;
    }

    unzGetCurrentFileInfo (UnZipHandle, &ZipInfo, FileNameArray[iCount],
                           MAX_PATH, NULL, 0, NULL, 0);

    rc = unzGoToNextFile (UnZipHandle);
    iCount++;
  }

  memset (FileNameArray[iCount], 0, MAX_PATH + 1);

  // Sort the dynamic array into canonical order
  qsort (FileNameArray, iCount, sizeof (char **), StringCompare);

  return (FileNameArray);
}

int
CheckZipStatus (FILE * f, WORKSPACE * ws)
{
  unsigned long checksum, target_checksum = 0;
  unsigned long ch_length = 0;
  int i = 0;
  char comment_buffer[COMMENT_LENGTH + 1];
  char *ep = NULL;
  unsigned char x;

  // Quick check that the file at least appears to be a zip file.
  rewind (f);
  if (fgetc (f) != 'P' && fgetc (f) != 'K')
  {
    return STATUS_ERROR;
  }

  // Assume a TZ style archive comment and read it in.
  comment_buffer[COMMENT_LENGTH] = 0;
  if (fseek (f, -COMMENT_LENGTH, SEEK_END))
  {
    return STATUS_ERROR;
  }
  fread (comment_buffer, 1, COMMENT_LENGTH, f);

  // Check static portion of comment.
  if (strncmp (gszApp, comment_buffer, COMMENT_LENGTH - 8))
  {
    return STATUS_BAD_COMMENT;
  }

  // Parse checksum portion of the comment.
  errno = 0;
  target_checksum = strtoul (comment_buffer + COMMENT_LENGTH - 8, &ep, 16);
  // Check to see if stroul was able to parse the entire checksum.
  if (errno || ep != comment_buffer + COMMENT_LENGTH)
  {
    return STATUS_BAD_COMMENT;
  }

  // Comment checks out so seek to 4 before it...
  if (fseek (f, -(COMMENT_LENGTH + 4), SEEK_END))
  {
    return STATUS_ERROR;
  }

  // ...and find the end of the central header.
  while (1)
  {
    // We have to read like this to be compatible on
    // 32bit and 64bit (and higher) systems using unsigned
    // integers and on little-endian and big-endian processors.
    fread (&x, 1, 1, f);
    i = (unsigned long) x;
    fread (&x, 1, 1, f);
    i += ((unsigned long) x) << 8;
    fread (&x, 1, 1, f);
    i += ((unsigned long) x) << 16;
    fread (&x, 1, 1, f);
    i += ((unsigned long) x) << 24;
    if (i == ENDHEADERMAGIC)
      break;
    if (fseek (f, -5, SEEK_CUR))
      return STATUS_ERROR;
  }

  // Skip ahead and read in the central header length.
  if (fseek (f, 8, SEEK_CUR))
  {
    return STATUS_ERROR;
  }

  fread (&x, 1, 1, f);
  ch_length = (unsigned long) x;
  fread (&x, 1, 1, f);
  ch_length += ((unsigned long) x) << 8;
  fread (&x, 1, 1, f);
  ch_length += ((unsigned long) x) << 16;
  fread (&x, 1, 1, f);
  ch_length += ((unsigned long) x) << 24;

  if (ch_length > ws->iCheckBufSize)
  {
    ws->pszCheckBuf = realloc (ws->pszCheckBuf, ch_length + 1024);
    if (ws->pszCheckBuf == NULL)
    {
      return STATUS_ALLOC_ERROR;
    }
    else
    {
      ws->iCheckBufSize = ch_length + 1024;
    }
  }

  // Skip backward to the start of the central header, and read it in.
  if (fseek (f, -(ch_length + 16), SEEK_CUR))
  {
    return STATUS_ERROR;
  }
  fread (ws->pszCheckBuf, 1, ch_length, f);

  // Calculate the crc32 of the central header.
  checksum = crc32 (crc32 (0L, NULL, 0), ws->pszCheckBuf, ch_length);

  return checksum == target_checksum ? STATUS_OK : STATUS_OUT_OF_DATE;
}

int
MigrateZip (const char *zip_path, char * pDir, WORKSPACE * ws, MIGRATE * mig)
{
  unz_file_info ZipInfo;
  unzFile UnZipHandle = NULL;
  unz_s *UnzipStream = NULL;
  zipFile ZipHandle = NULL;

  // Used for CRC32 calc of central directory during rezipping
  zip_internal *zintinfo;
  linkedlist_datablock_internal *ldi;

  // Used for our dynamic filename array
  int iArray = 0;
  int iArray2 = 0;

  int rc = 0;
  int error = 0;

  char szTmpBuf[MAX_PATH + 1];
  char szFileName[MAX_PATH + 1];
  char *pszZipName = NULL;

  int iBytesRead = 0;

  unsigned long int cTotalBytesInZip = 0;
  unsigned int cTotalFilesInZip = 0;

  // Use to store the CRC32 of the central directory
  unsigned long crc = 0;

  struct stat istat;

  if (!access (TMP_FILENAME, F_OK) && remove (TMP_FILENAME))
  {
    logprint (stderr, ws->fErrorLog,
              "\n!!!! Temporary file exists and could not be "
              "automatically removed. Please remove the file %s%s "
              "and re-run this program. !!!!\n", pDir, TMP_FILENAME);
    return TZ_CRITICAL;
  }

  if ((UnZipHandle = unzOpen (zip_path)) == NULL)
  {
    logprint (stderr, ws->fErrorLog,
              "Error opening \"%s\". Unable to process zip.\n\n", zip_path);
    return TZ_ERR;
  }

  UnzipStream = (unz_s *) UnZipHandle;

  // Check if zip is non-TZ or altered-TZ
  rc = CheckZipStatus ((FILE *) UnzipStream->filestream, ws);

  // File won't be TorrentZipped (either not necessary or because of error)
  if (rc != STATUS_OUT_OF_DATE && rc != STATUS_BAD_COMMENT)
  {
    unzClose (UnZipHandle);
  }

  switch (rc)
  {
    // File is already TorrentZipped
  case STATUS_OK:
    logprint (stdout, mig->fProcessLog, "Skipping - %s\n\n", zip_path);
    return TZ_SKIPPED;

  case STATUS_ERROR:
    logprint (stderr, ws->fErrorLog,
              "Unable to process \"%s%s\". It seems to be corrupt.\n\n",
              pDir, zip_path);
    return TZ_ERR;

  case STATUS_ALLOC_ERROR:
    logprint (stderr, ws->fErrorLog, "Error allocating memory!\n");
    chdir ("..");
    return TZ_CRITICAL;

  case STATUS_OUT_OF_DATE:
  case STATUS_BAD_COMMENT:
    // Do nothing
    break;

  default:
    logprint (stderr, ws->fErrorLog, "Bad return on CheckZipStatus!\n");
    chdir ("..");
    return TZ_CRITICAL;
  }

  logprint (stdout, mig->fProcessLog, "Rezipping - %s\n\n", zip_path);
  logprint (stdout, mig->fProcessLog, "%s\n", DIVIDER);

  if ((ZipHandle = zipOpen (TMP_FILENAME, 0)) == NULL)
  {
    logprint (stderr, ws->fErrorLog,
              "Error creating temporary zip file. Unable to process \"%s%s\"\n\n",
              pDir, zip_path);
    unzClose (UnZipHandle);
    return TZ_ERR;
  }

  // Get the filelist from the zip file in canonical order
  ws->FileNameArray =
    GetFileList (UnZipHandle, ws->FileNameArray, &ws->iElements);

  // GetFileList couldn't allocate enough memory to store the filelist
  if (!ws->FileNameArray)
  {
    logprint (stderr, ws->fErrorLog, "Error allocating memory!\n");
    unzClose (UnZipHandle);
    return TZ_CRITICAL;
  }

  for (iArray = 0; strlen (ws->FileNameArray[iArray]); iArray++)
  {
    strcpy (szFileName, ws->FileNameArray[iArray]);
    rc = unzLocateFile (UnZipHandle, szFileName, 0);

    if (rc == UNZ_OK)
    {
      rc = unzGetCurrentFileInfo (UnZipHandle, &ZipInfo,
                                  szFileName, MAX_PATH, NULL, 0, NULL, 0);
      if (rc == UNZ_OK)
        rc = unzOpenCurrentFile (UnZipHandle);
    }

    if (rc != UNZ_OK)
    {
      logprint (stderr, ws->fErrorLog,
                "Unable to open \"%s\" from \"%s%s\"\n\n",
                szFileName, pDir, zip_path);
      error = 1;
      break;
    }

    logprint (stdout, mig->fProcessLog, "Adding - %s (%lu bytes)...",
              szFileName, ZipInfo.uncompressed_size);

    if (qStripSubdirs)
    {
      // To strip off path is there is one
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
  
      iArray2 = iArray - 1;
  
      while (iArray2 > -1)
      {
        if (!strcmp (pszZipName, ws->FileNameArray[iArray2]))
        {
          error = 1;
          break;
        }
        iArray2--;
      }
  
      if (error)
      {
        logprint (stdout, mig->fProcessLog, "Not done\n\n");
        logprint (stderr, ws->fErrorLog,
                  "Zip file \"%s%s\" contains more than one file "
                  "named \"%s\"\n\n", pDir, zip_path, pszZipName);
        break;
      }
    }
    else
      pszZipName = szFileName;

    rc = zipOpenNewFileInZip (ZipHandle, pszZipName, &ws->zi,
                              NULL, 0, NULL, 0,
                              NULL, Z_DEFLATED, Z_BEST_COMPRESSION);

    if (rc != ZIP_OK)
    {
      logprint (stderr, ws->fErrorLog,
                "Unable to open \"%s\" in replacement zip\n\n", pszZipName);
      error = 1;
      break;
    }

    for (;;)
    {
      iBytesRead =
        unzReadCurrentFile (UnZipHandle, ws->pszUncompBuf, ws->iBufSize);

      if (!iBytesRead)
      { // All bytes have been read.
        break;
      }

      if (iBytesRead < 0) // Error.
      {
        logprint (stderr, ws->fErrorLog, "Error while reading \"%s\" from "
                  "\"%s%s\"\n\n", szFileName, pDir, zip_path);
        error = 1;
        break;
      }

      rc = zipWriteInFileInZip (ZipHandle, ws->pszUncompBuf, iBytesRead);

      if (rc != ZIP_OK)
      {
        logprint (stderr, ws->fErrorLog, "Error while adding \"%s\" "
                  "to replacement zip\n\n", pszZipName);
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
        logprint (stderr, ws->fErrorLog, "CRC error in \"%s\" in \"%s%s\"!\n\n",
                  szFileName, pDir, zip_path);
      else
        logprint (stderr, ws->fErrorLog,
                  "Error while closing \"%s\" in \"%s%s\"!\n\n", szFileName,
                  pDir, zip_path);
      error = 1;
      break;
    }

    rc = zipCloseFileInZip (ZipHandle);

    if (rc != ZIP_OK)
    {
      logprint (stderr, ws->fErrorLog, "Error closing \"%s\" in new "
                "zip file \"%s%s\"!\n\n", pszZipName, pDir, zip_path);
      error = 1;
      break;
    }

    logprint (stdout, mig->fProcessLog, "Done\n\n");

    cTotalFilesInZip++;
  }

  // If there was an error above then clean up and return.
  if (error)
  {
    fprintf(mig->fProcessLog, "Not done\n\n");
    unzClose (UnZipHandle);
    zipClose (ZipHandle, NULL);
    remove (TMP_FILENAME);
    return TZ_ERR;
  }

  logprint (stdout, mig->fProcessLog, "%s\n", DIVIDER);

  unzClose (UnZipHandle);

  // Before we close the file, we need to calc the CRC32 of
  // the central directory (for detecting a changed TZ file later)
  zintinfo = (zip_internal *) ZipHandle;
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
    logprint (stderr, ws->fErrorLog, "Unable to close temporary zip file - "
              "cannot process \"%s%s\"!\n\n", pDir, zip_path);
    remove (TMP_FILENAME);
    return TZ_ERR;
  }

  stat (zip_path, &istat);

  if (remove (zip_path))
  {
    logprint (stderr, ws->fErrorLog,
              "Unable to remove \"%s%s\" for replacement with "
              "rezipped copy!\n\n", pDir, zip_path);
    remove (TMP_FILENAME);
    return TZ_ERR;
  }

  if (rename (TMP_FILENAME, zip_path))
  {
    logprint (stderr, ws->fErrorLog,
              "!!!! Could not rename temporary file %s%s "
              "to %s%s. The original file has already been deleted, "
              "so you must rename this file manually before re-running this "
              "program. Failure to do so will cause you to lose the contents "
              "of the file.\n", pDir, TMP_FILENAME, pDir, zip_path);
    return TZ_CRITICAL;
  }

  logprint (stdout, mig->fProcessLog,
            "Rezipped %u compressed file%s totaling %lu byte%s.\n\n--\n\n",
            cTotalFilesInZip, cTotalFilesInZip != 1 ? "s" : "",
            cTotalBytesInZip, cTotalBytesInZip != 1 ? "s" : "");

  return TZ_OK;
}

// Function to convert the zips
int
RecursiveMigrate (const char *pszPath, WORKSPACE * ws)
{
  int rc = 0;

  char szTmpBuf[MAX_PATH + 1];
  char qZipFile = 0;
  char *pDir = NULL;

  DIR *dirp = NULL;
  struct dirent *direntp = NULL;
  struct stat istat;

  MIGRATE mig;

  time_t StartTime = 0;
  double ExecTime = 0;

  // Get our start time for the conversion process of this dir/zip
  StartTime = time (NULL);

  memset (&mig, 0, sizeof (MIGRATE));
  sprintf (szTmpBuf, "%s", pszPath);
  strlwr (szTmpBuf);

  if (!strstr (szTmpBuf, ".zip\0"))
  {    
    rc = chdir (pszPath);
  
    // Couldn't change to specified path
    if (rc)
    {
      logprint (stderr, ws->fErrorLog, "Could not change to subdir \"%s\"!\n\n",
                pszPath);
      mig.bErrorEncountered = 1;
    }
  }
  else
  {
    // Couldn't find the passed zip file
    if (access (pszPath, F_OK))
    {
      logprint (stderr, ws->fErrorLog, "Could not find the zip file \"%s\"!\n\n",
                pszPath);
      mig.bErrorEncountered = 1;
    }

    // We need to change to the dir of the zipfile if there is one.
    // This is because the logging will pick up the current dir for
    // the filename
    if (pDir = strrchr (pszPath, DIRSEP))
    {
      *pDir = 0;
      chdir (pszPath);
      *pDir = DIRSEP;
    }

    qZipFile = 1;
  }

  if (!mig.bErrorEncountered)
  {
    pDir = get_cwd ();
  
    if (!pDir)
    {
      logprint (stderr, ws->fErrorLog, "Could not get startup path!\n\n");
      return TZ_CRITICAL;
    }

    dirp = opendir (".");
  
    if (dirp)
    {
      // First set all the files to read-only. This is so we can skip
      // our new zipfiles if they are returned by readdir() a second time.
      while (direntp = readdir (dirp))
      {
        // Quick fudge to make the code below work
        if (qZipFile) strcpy (direntp->d_name, pszPath);
  
        stat (direntp->d_name, &istat);
  
        if (!S_ISDIR (istat.st_mode))
        {
          sprintf (szTmpBuf, "%s", direntp->d_name);
          strlwr (szTmpBuf);
  
          if (strstr (szTmpBuf, ".zip\0"))
          {
            chmod (direntp->d_name, S_IRUSR);
          }
        }
        // Zip file is actually a dir
        else if (qZipFile)
        {
          closedir (dirp);
          logprint (stderr, ws->fErrorLog, 
                    "The zip file \"%s\" is actually a directory!\n\n", pszPath);
          mig.bErrorEncountered = 1;
          return TZ_OK; 
        }
  
        // We were passed a zip filename, so just break out of the loop
        if (qZipFile) break;
      }
  
      rewinddir (dirp);
  
      while (qZipFile || (direntp = readdir (dirp)))
      {
        stat (direntp->d_name, &istat);
  
        if (S_ISDIR (istat.st_mode))
        {
          if (!qNoRecursion && strcmp (direntp->d_name, ".") &&
              strcmp (direntp->d_name, ".."))
          {
            // Get our execution time (in seconds)
            // for the conversion process so far
            ExecTime += difftime (time (NULL), StartTime);

            rc = RecursiveMigrate (direntp->d_name, ws);

            // Restart the timing for this instance of RecursiveMigrate()
            StartTime = time (NULL);

            if (rc == TZ_CRITICAL)
            {
              break;
            }
          }
        }
        else
        {
          sprintf (szTmpBuf, "%s", direntp->d_name);
          strlwr (szTmpBuf);
  
          if (strstr (szTmpBuf, ".zip\0") && !(istat.st_mode & S_IWUSR))
          {            
            chmod (direntp->d_name, S_IWUSR);
            mig.cEncounteredZips++;
  
            if (!mig.fProcessLog)
            {
              rc = OpenProcessLog (pszStartPath, &mig);
              
              if (rc != TZ_OK)
              {    
                break;
              }
            }
  
            if (istat.st_size > COMMENT_LENGTH)
            {
              rc = MigrateZip (direntp->d_name, strchr(pDir, DIRSEP) ? "" : pDir,
                                ws, &mig);
  
              switch (rc)
              {
              case TZ_OK:
                mig.cRezippedZips++;
                break;
              case TZ_ERR:
                mig.cErrorZips++;
                mig.bErrorEncountered = 1;
                break;
              case TZ_CRITICAL:
                break;
              case TZ_SKIPPED:
                mig.cOkayZips++;
              }

              // Break out of the while loop if we
              // encountered a critical error
              if (rc == TZ_CRITICAL) break;
            }
            else // Too small to be a valid zip file.
            {
              logprint (stderr, ws->fErrorLog, "\"%s\" is too small. "
                        "File may be corrupt\n\n", direntp->d_name);
              mig.cErrorZips++;
              mig.bErrorEncountered = 1;
            }
          }
  
          // We were passed a zip filename, so just break out of the loop
          if (qZipFile) break;
        }
      }

      // Get our execution time (in seconds) for the conversion process 
      ExecTime += difftime (time (NULL), StartTime);

      closedir (dirp);
    }

    free (pDir);
  }

  chdir ("..");

  if (rc != TZ_CRITICAL)
  {
    if(mig.fProcessLog)
    {
      // Output it in hours, minutes and seconds
      logprint (stdout, mig.fProcessLog, "Execution time %d hours ",
                (int) ExecTime / (60 * 60));
      ExecTime = fmod (ExecTime, (60 * 60));
      logprint (stdout, mig.fProcessLog, "%d mins ", (int) ExecTime / 60);
      ExecTime = fmod (ExecTime, 60);
      logprint (stdout, mig.fProcessLog, "%d secs\n\n", (int) ExecTime);
      logprint (stdout, mig.fProcessLog, "Checked %u zip file%s.\n",
                mig.cEncounteredZips, mig.cEncounteredZips != 1 ? "s" : "");
      if (mig.cRezippedZips)
        logprint (stdout, mig.fProcessLog,
                  "  %u file%s successfully rezipped.\n", mig.cRezippedZips,
                  mig.cRezippedZips != 1 ? "s were" : " was");
      if (mig.cOkayZips)
        logprint (stdout, mig.fProcessLog, "  %u file%s already up to date.\n",
                  mig.cOkayZips, mig.cOkayZips != 1 ? "s were" : " was");
      if (mig.cErrorZips)
        logprint (stdout, mig.fProcessLog, "  %u file%s had errors.\n",
                  mig.cErrorZips, mig.cErrorZips != 1 ? "s" : "");
    
      if (mig.bErrorEncountered)
      {
        logprint (stdout, mig.fProcessLog,
                  "!!!! There were problems! See 'error.log' for details. !!!!\n\n");
        qErrors = 1;
      }

      fclose (mig.fProcessLog);
    }
  }

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
        fprintf (stdout, "Copyright (C) 2005 TorrentZip Team :\n");
        fprintf (stdout, "StatMat, shindakun, Ultrasubmarine, r3nh03k and goosecreature\n");
        fprintf (stdout, "Homepage : http://sourceforge.net/projects/trrntzip\n\n");
        fprintf (stdout, "Usage: trrntzip [OPTIONS] [PATH/ZIP FILE]\n\n");
        fprintf (stdout, "Options:\n\n");
        fprintf (stdout, "-d : strip sub-directories from zips\n");
        fprintf (stdout, "-s : prevent sub-directory recursion\n");
        fprintf (stdout, "-v : show version\n");
        return TZ_OK;

      case 'd':
        // Strip subdirs from zips
        qStripSubdirs = 1;
        break;

      case 'g':
        // GUI launch process
        qGUILaunch = 1;
        break;

      case 's':
        // Disable dir recursion
        qNoRecursion = 1;
        break;

      case 'v':
        // GUI requesting TZ version
        fprintf (stdout, "TorrentZip v%s\n", TZ_VERSION);
        return TZ_OK;

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
    chdir (argv[0]);
  }
#endif

  pszStartPath = get_cwd();
  
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
      rc = RecursiveMigrate (argv[iCount], ws);
    }

    fclose (ws->fErrorLog);

    if (qErrors)
    {
      if (!qGUILaunch)
      {
        fprintf (stdout, "Press any key to exit.\n");
        getch ();
      }
    }
    else
    {
      chdir (pszStartPath);
      remove ("error.log");
    }
  }

  FreeWorkspace (ws);
  free(pszStartPath);

  return rc;
}
