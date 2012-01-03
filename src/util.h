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

#ifndef UTIL_DOT_H
#define UTIL_DOT_H

int StringCompare (const void *str1, const void *str2);
int EndsWithCaseInsensitive(const char * str1, const char * str2);
char **DynamicStringArrayCreate (int iElements);
char **DynamicStringArrayDestroy (char **StringArray, int iElements);
char **DynamicStringArrayResize (char **StringArray, int *piElements, int iNewElements);
void DynamicStringArrayCheck (char **StringArray, int iElements);
#ifdef NDEBUG
#define CHECK_DYNAMIC_STRING_ARRAY( StringArray, iElements ) \
	(void*)0
#else
#define CHECK_DYNAMIC_STRING_ARRAY( StringArray, iElements ) \
	DynamicStringArrayCheck( StringArray, iElements )
#endif
char *get_cwd (void);
#endif
