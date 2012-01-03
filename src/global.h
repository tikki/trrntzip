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

#ifndef GLOBAL_DOT_H
#define GLOBAL_DOT_H

#include "platform.h"

#include <stdio.h>

#include "zip.h"

#define TZ_OK 0
#define TZ_ERR -1
#define TZ_CRITICAL -2
#define TZ_SKIPPED -3

#define MAX_PATH 1024

typedef struct _WORKSPACE
{
  zip_fileinfo zi;
  char **FileNameArray;
  int iElements;
  unsigned int iBufSize;
  char *pszUncompBuf;
  unsigned int iCheckBufSize;
  char *pszCheckBuf;
  FILE *fErrorLog;
} WORKSPACE;

typedef struct _MIGRATE
{
  unsigned int cEncounteredDirs, cEncounteredZips;
  unsigned int cRezippedZips, cOkayZips, cErrorZips;
  double ExecTime;
  time_t StartTime;
  int bErrorEncountered;
  FILE *fProcessLog;
} MIGRATE;

#endif
