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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

int
StringCompare (const void *str1, const void *str2)
{
  const char **p1 = (const char **) str1;
  const char **p2 = (const char **) str2;
  return (strcasecmp (*p1, *p2));
}

// Create/destroy a dynamic string array
char **
DynamicStringArray (char **StringArray, int iElements, char qCreate)
{
  int iCount;
  int iError;

  // Create array
  if (qCreate)
  {
    StringArray = (char **) calloc (1, iElements * sizeof (char *));
    if (!StringArray)
      return NULL;

    for (iCount = 0; iCount < iElements; iCount++)
    {
      StringArray[iCount] = (char *) calloc (1, MAX_PATH + 1);

      // Check for error with above alloc
      // If there is, free up everything we managed to alloc
      // and return error
      if (!StringArray[iCount])
      {
        for (iError = 0; iError < iCount; iError++)
        {
          free (StringArray[iError]);
        }

        free (StringArray);

        return NULL;
      }
    }
  }
  // Destroy array
  else
  {
    for (iCount = 0; iCount < iElements; iCount++)
    {
      free (StringArray[iCount]);
    }

    free (StringArray);
  }

  return (StringArray);
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
