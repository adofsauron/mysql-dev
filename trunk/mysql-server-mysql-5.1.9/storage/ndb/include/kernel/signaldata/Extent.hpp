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

#ifndef NDB_EXTENT_HPP
#define NDB_EXTENT_HPP

#include "SignalData.hpp"

struct AllocExtentReq {
  /**
   * Sender(s) / Reciver(s)
   */

  /**
   * For printing
   */
  
  STATIC_CONST( SignalLength = 3 );

  enum ErrorCode {
    UnmappedExtentPageIsNotImplemented = 1,
    NoExtentAvailable = 1601
  };
  
  union 
  {
    struct 
    {
      Uint32 tablespace_id;
      Uint32 table_id;
      Uint32 fragment_id;
    } request;
    struct 
    {
      Uint32 errorCode;
      Local_key page_id;
      Uint32 page_count;
    } reply;
  };
};

struct FreeExtentReq {
  /**
   * Sender(s) / Reciver(s)
   */

  /**
   * For printing
   */
  
  STATIC_CONST( SignalLength = 4 );

  enum ErrorCode {
    UnmappedExtentPageIsNotImplemented = 1
  };
  
  union 
  {
    struct 
    {
      Local_key key;
      Uint32 table_id;
      Uint32 tablespace_id;
    } request;
    struct 
    {
      Uint32 errorCode;
    } reply;
  };
};

struct AllocPageReq {
  /**
   * Sender(s) / Reciver(s)
   */

  /**
   * For printing
   */
  
  STATIC_CONST( SignalLength = 3 );

  enum ErrorCode {
    UnmappedExtentPageIsNotImplemented = 1,
    NoPageFree= 2
  };
  
  Local_key key; // in out
  Uint32 bits;   // in out
  union 
  {
    struct 
    {
      Uint32 table_id;
      Uint32 fragment_id;
      Uint32 tablespace_id;
    } request;
    struct 
    {
      Uint32 errorCode;
    } reply;
  };
};


#endif
