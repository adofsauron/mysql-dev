/* Copyright (C) 2000 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* TODO: Test all functions  */

#define USES_TYPES
#include "mysys_priv.h"
#include "my_dir.h"

int main(int argc, char *argv[])
{
  MY_DIR	*a;
  uint		f;
  DBUG_ENTER ("main");
  DBUG_PROCESS (argv[0]);

  if (--argc > 0 && (*(++argv))[0] == '-' && (*argv)[1] == '#' )
    DBUG_PUSH (*argv+2);

  a = my_dir("./", 0);
  for (f = 0; f < a->number_off_files; f++)
  {
    printf("%s\n", a->dir_entry[f].name);
  }

  a = my_dir("./", MY_WANT_STAT);
  for (f = 0; f < a->number_off_files; f++)
  {
    printf("%s %d %d %d %s\n", a->dir_entry[f].name,
	   (int) a->dir_entry[f].mystat.st_size,
	   (int) a->dir_entry[f].mystat.st_uid,
	   (int) a->dir_entry[f].mystat.st_gid,
	   S_ISDIR(a->dir_entry[f].mystat.st_mode) ? "dir" : "");
  }
  return 0;
}
