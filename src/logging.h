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

#ifndef LOGGING_DOT_H
#define LOGGING_DOT_H

#include "global.h"

int OpenProcessLog (const char * pszWritePath, const char * pszRelPath, MIGRATE * mig);
FILE *OpenErrorLog (char qGUILaunch);
void logprint (FILE * stdf, FILE * f, char *format, ...);
void logprint3 (FILE * stdf, FILE * f1, FILE * f2, char *format, ...);

#endif
