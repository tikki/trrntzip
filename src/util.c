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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NDEBUG
#include <assert.h>

#include "global.h"
#include "util.h"

int
StringCompare (const void *str1, const void *str2)
{
  const char **p1 = (const char **) str1;
  const char **p2 = (const char **) str2;
  return (strcasecmp (*p1, *p2));
}

int EndsWithCaseInsensitive(const char * str1, const char * str2)
{
  int n1, n2;
  n1 = strlen (str1);
  n2 = strlen (str2);
  if (n2<n1)
  {
    return strcasecmp (str1+n1-n2, str2);
  } else {
    return strcasecmp (str1, str2+n2-n1);
  }
}

// Create a dynamic string array
char **
DynamicStringArrayCreate (int iElements)
{
  int iCount;
  char **StringArray;

  StringArray = (char **) calloc (sizeof (char *), iElements);
  if (!StringArray)
    return NULL;

  for (iCount = 0; iCount < iElements; iCount++)
  {
    StringArray[iCount] = (char *) malloc (MAX_PATH + 1);

    // Check for error with above alloc
    // If there is, free up everything we managed to alloc
    // and return error
    if (!StringArray[iCount])
      return DynamicStringArrayDestroy( StringArray, iCount );
    StringArray[iCount][0] = 0;
  }
  return (StringArray);
}

// Destroy a dynamic string array
char **
DynamicStringArrayDestroy (char **StringArray, int iElements)
{
  int iCount;

  CHECK_DYNAMIC_STRING_ARRAY( StringArray, iElements );

  for (iCount = 0; iCount < iElements; iCount++)
  {
    free (StringArray[iCount]);
  }

  free (StringArray);

  return NULL;
}

// Resize a dynamic string array
// to have iNewElements in total.
// iNewElements may be larger or smaller than *piElements.
char **
DynamicStringArrayResize (char **StringArray, int *piElements, int iNewElements)
{
  int iCount;
  char **TmpPtr = NULL;

  CHECK_DYNAMIC_STRING_ARRAY( StringArray, *piElements );

  for (iCount = iNewElements; iCount < *piElements; iCount++)
  {
    free (StringArray[iCount]);
  }
  TmpPtr = StringArray;
  StringArray =
    (char **) realloc (StringArray,
                       iNewElements * sizeof (char *));
  if (!StringArray)
    return DynamicStringArrayDestroy (TmpPtr, *piElements);

  for (iCount = *piElements; iCount < iNewElements; iCount++)
  {
    StringArray[iCount] = (char *) malloc (MAX_PATH + 1);

    // Check for error with above alloc
    // If there is, free up everything we managed to alloc
    // and return error
    if (!StringArray[iCount])
      return DynamicStringArrayDestroy( StringArray, iCount );
    StringArray[iCount][0] = 0;
  }
  *piElements = iNewElements;

  CHECK_DYNAMIC_STRING_ARRAY( StringArray, *piElements );

  return (StringArray);
}

void DynamicStringArrayCheck (char **StringArray, int iElements)
{
  // All StringArray elements should be non-null,
  // because they were all allocated by DynamicStringArrayCreate.
  int i, l;
  for (i = 0; i < iElements; ++i) {
    assert( StringArray[i] );
    l = strlen( StringArray[i] );
    assert( l >= 0 );
    assert( l <= MAX_PATH );
  }
}

char *
get_cwd (void)
{
  char *pszCWD = NULL;
  int cchCWD = 1024;

  pszCWD = malloc (cchCWD);

  if (!pszCWD)
    return NULL;

  while (!getcwd (pszCWD, cchCWD - 2))
  {
    cchCWD += 1024;
    free (pszCWD);
    pszCWD = malloc (cchCWD);
    if (!pszCWD)
      return NULL;
  }

  cchCWD = strlen (pszCWD);

  if (pszCWD[cchCWD - 1] != DIRSEP)
  {
    pszCWD[cchCWD] = DIRSEP;
    pszCWD[cchCWD + 1] = 0;
  }

  return pszCWD;
}
