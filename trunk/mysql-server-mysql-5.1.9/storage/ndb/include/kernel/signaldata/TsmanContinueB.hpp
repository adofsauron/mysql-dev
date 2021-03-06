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

#ifndef TSMAN_CONTINUEB_H
#define TSMAN_CONTINUEB_H

#include "SignalData.hpp"

class TsmanContinueB {
  /**
   * Sender(s)/Reciver(s)
   */
  friend class Tsman;
private:
  enum {
    LOAD_EXTENT_PAGES = 0,
    SCAN_TABLESPACE_EXTENT_HEADERS = 1,
    SCAN_DATAFILE_EXTENT_HEADERS = 2,
    END_LCP = 3,
    RELEASE_EXTENT_PAGES = 4
  };
};

#endif
