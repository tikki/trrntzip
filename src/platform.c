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

#include "platform.h"

#include <ctype.h>
#include <unistd.h>
#include <stdio.h>

#ifndef WIN32

#include <termios.h>

char *
strlwr (char *s)
{
  while (*s)
  {
    *s = (char) tolower (*s);
    ++s;
  }
  return s;
}

// Not sure if this is the best way to implement this, but it works.
int
getch (void)
{
  struct termios t, t2;
  char c = 0;

  tcgetattr (1, &t);
  t2 = t;
  cfmakeraw (&t2);
  tcsetattr (1, TCSANOW, &t2);
  fread (&c, 1, 1, stdin);
  tcsetattr (1, TCSANOW, &t);

  return c;
}

#endif
