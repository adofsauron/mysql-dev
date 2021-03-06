/* Copyright (C) 2003 MySQL AB

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

#ifndef PGMAN_CONTINUEB_H
#define PGMAN_CONTINUEB_H

#include "SignalData.hpp"

class PgmanContinueB {
  /**
   * Sender(s)/Reciver(s)
   */
  friend class Pgman;
private:
  enum {
    STATS_LOOP = 0,
    BUSY_LOOP = 1,
    CLEANUP_LOOP = 2,
    LCP_LOOP = 3,
    LCP_LOCKED = 4
  };
};

#endif
