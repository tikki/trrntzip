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

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>

#include "util.h"
#include "logging.h"

static FILE *OpenLog (const char *szFileName);

// Global var to store if the logprint func is expecting more
// data to complete a line. This prevents the timestamp being
// multipally inserted before a line terminates in a logfile.
char continueline = 0;

// Used to print to screen and file at the same time
void
logprint (FILE * stdf, FILE * f, char *format, ...)
{
  time_t now;
  struct tm *t;
  va_list arglist;

  // Only print the timestamp if this is the beginning of a line
  if (!continueline)
  {
    now = time (NULL);
    t = localtime (&now);

    fprintf (f, "[%04d/%02d/%02d - %02d:%02d:%02d] ",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
  }

  // Look for a newline in the passed data
  continueline = !strchr (format, '\n');

  // Print to stdout or stderr
  if (stdf)
  {
    va_start (arglist, format);
    vfprintf (stdf, format, arglist);
    va_end (arglist);
  }

  // Print to logfile
  if (f)
  {
    va_start (arglist, format);
    vfprintf (f, format, arglist);
    va_end (arglist);
    fflush (f);
  }
}

int
OpenProcessLog (char * pszWritePath, MIGRATE * mig)
{
  int iPathLen = 0;
  time_t now;
  struct tm *t;
  char szLogname[MAX_PATH + 1];
  char *pszPath = NULL;
  char *pszDirname = NULL;

  now = time (NULL);
  t = localtime (&now);
  pszPath = get_cwd ();

  if (pszPath)
  {
    iPathLen = strlen (pszPath);

    if(iPathLen > 1)
    {
      // Strip off the last slash
      pszPath[iPathLen - 1] = 0;
      pszDirname = strrchr(pszPath, DIRSEP) + 1;

      if(pszPath[iPathLen - 2] == ':')
      {
        pszPath[iPathLen - 2] = 0;
        pszDirname = pszPath;
      }
    }
    else
    {
      *pszPath = 'r';
    }
  }

  sprintf (szLogname, "%s[%s]_[%04d-%02d-%02d - %02d-%02d-%02d]",
           pszWritePath, pszDirname, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
           t->tm_hour, t->tm_min, t->tm_sec);

  mig->fProcessLog = OpenLog (szLogname);

  if (mig->fProcessLog)
  {
    fprintf(mig->fProcessLog, "TorrentZip processing logfile for : \"%s\"\n\n",
            pszPath);
  }

  free (pszPath);

  return mig->fProcessLog ? TZ_OK : TZ_CRITICAL;
}

FILE *
OpenErrorLog (char qGUILaunch)
{
  struct stat istat;
  int rc;

  rc = stat ("error.log", &istat);

  if (!rc && istat.st_size && !qGUILaunch)
  {
    fprintf (stderr,
             "There is a previous 'error.log'. Are you sure you have dealt\n"
             "with the problems encountered last time this program was run?\n"
             "(Press 'y' to continue or any other key to exit.)\n\n");

    if (tolower (getch ()) != 'y')
    {
      fprintf (stderr, "Exiting.\n");
      return NULL;
    }
  }

  return (OpenLog ("error"));
}

static FILE *
OpenLog (const char *szFileName)
{
  FILE *f = NULL;
  char szLog[strlen (szFileName) + 5];

  strcpy (szLog, szFileName);
  strcat (szLog, ".log");

  f = fopen (szLog, "w");

  if (!f)
  {
    fprintf (stderr, "Could not open log file '%s'!\n", szLog);
  }

  return f;
}
