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

#ifndef CREATE_FILEGROUP_IMPL_HPP
#define CREATE_FILEGROUP_IMPL_HPP

#include "SignalData.hpp"

struct CreateFilegroupImplReq {
  /**
   * Sender(s) / Reciver(s)
   */
  friend class Dbdict;
  friend class Tsman;
  friend class Lgman;

  /**
   * For printing
   */
  friend bool printCREATE_FILEGROUP_IMPL_REQ(FILE*, const Uint32*, Uint32, Uint16);
  
  STATIC_CONST( TablespaceLength = 6 );
  STATIC_CONST( LogfileGroupLength = 5 );
  
  Uint32 senderData;
  Uint32 senderRef;  
  Uint32 filegroup_id;
  Uint32 filegroup_version;
  
  union {
    struct {
      Uint32 extent_size;
      Uint32 logfile_group_id;
    } tablespace;
    struct {
      Uint32 buffer_size; // In pages
    } logfile_group;
  };
};

struct CreateFilegroupImplRef {
  /**
   * Sender(s)
   */
  friend class Dbdict;
  friend class Tsman;
  friend class Lgman;
  
  /**
   * For printing
   */
  friend bool printCREATE_FILEGROUP_IMPL_REF(FILE*, const Uint32*, Uint32, Uint16);
  
  STATIC_CONST( SignalLength = 3 );

  enum ErrorCode {
    NoError = 0,
    FilegroupAlreadyExists = 1502,
    OutOfFilegroupRecords = 1503,
    OutOfLogBufferMemory = 1504,
    OneLogfileGroupLimit = 1514
  };
  
  Uint32 senderData;
  Uint32 senderRef;
  Uint32 errorCode;
};

struct CreateFilegroupImplConf {
  /**
   * Sender(s)
   */
  friend class Dbdict;
  friend class Tsman;
  friend class Lgman;
  
  /**
   * For printing
   */
  friend bool printCREATE_FILEGROUP_IMPL_CONF(FILE*, const Uint32*, Uint32, Uint16);
  
  STATIC_CONST( SignalLength = 2 );

  Uint32 senderData;
  Uint32 senderRef;
};

struct CreateFileImplReq {
  /**
   * Sender(s) / Reciver(s)
   */
  friend class Dbdict;
  friend class Tsman;
  friend class Lgman;

  /**
   * For printing
   */
  friend bool printCREATE_FILE_IMPL_REQ(FILE*, const Uint32*, Uint32, Uint16);
  
  STATIC_CONST( DatafileLength = 9 );
  STATIC_CONST( UndofileLength = 8 );
  STATIC_CONST( CommitLength = 6 );
  STATIC_CONST( AbortLength = 6 );
  SECTION( FILENAME = 0 );
  
  enum RequestInfo {
    Create = 0x1,
    CreateForce = 0x2,
    Open = 0x4,
    Commit = 0x8,
    Abort = 0x10
  };
  
  Uint32 senderData;
  Uint32 senderRef;

  Uint32 requestInfo;
  Uint32 file_id;
  Uint32 filegroup_id;
  Uint32 filegroup_version;
  Uint32 file_size_hi;
  Uint32 file_size_lo;

  union {
    struct {
      Uint32 extent_size;
    } tablespace;
  };
};

struct CreateFileImplRef {
  friend class Dbdict;
  friend class Tsman;
  friend class Lgman;
  
  /**
   * For printing
   */
  friend bool printCREATE_FILE_IMPL_REF(FILE*, const Uint32*, Uint32, Uint16);
  
  STATIC_CONST( SignalLength = 5 );

  enum ErrorCode {
    NoError = 0,
    InvalidFilegroup = 1505,
    InvalidFilegroupVersion = 1506,
    FileNoAlreadyExists = 1507,
    OutOfFileRecords = 1508,
    FileError = 1509,
    InvalidFileMetadata = 1510,
    OutOfMemory = 1511,
    FileReadError = 1512,
    FilegroupNotOnline = 1513
  };
  
  Uint32 senderData;
  Uint32 senderRef;
  Uint32 errorCode;
  Uint32 fsErrCode;
  Uint32 osErrCode;
};

struct CreateFileImplConf {
  friend class Dbdict;
  friend class Tsman;
  friend class Lgman;
  
  
  /**
   * For printing
   */
  friend bool printCREATE_FILE_IMPL_CONF(FILE*, const Uint32*, Uint32, Uint16);
  
  STATIC_CONST( SignalLength = 4 );

  Uint32 senderData;
  Uint32 senderRef;
};

#endif
