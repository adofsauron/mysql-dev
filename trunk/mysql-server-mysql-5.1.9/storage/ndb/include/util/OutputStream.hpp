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

#ifndef OUTPUT_STREAM_HPP
#define OUTPUT_STREAM_HPP

#include <ndb_global.h>
#include <NdbTCP.h>

/**
 * Output stream
 */
class OutputStream {
public:
  virtual ~OutputStream() {}
  virtual int print(const char * fmt, ...) = 0;
  virtual int println(const char * fmt, ...) = 0;
  virtual void flush() {};
};

class FileOutputStream : public OutputStream {
  FILE * f;
public:
  FileOutputStream(FILE * file = stdout);
  virtual ~FileOutputStream() {}

  int print(const char * fmt, ...);
  int println(const char * fmt, ...);
  void flush() { fflush(f); }
};

class SocketOutputStream : public OutputStream {
  NDB_SOCKET_TYPE m_socket;
  unsigned m_timeout;
public:
  SocketOutputStream(NDB_SOCKET_TYPE socket, unsigned writeTimeout = 1000);
  virtual ~SocketOutputStream() {}

  int print(const char * fmt, ...);
  int println(const char * fmt, ...);
};

class SoftOseOutputStream : public OutputStream {
public:
  SoftOseOutputStream();
  virtual ~SoftOseOutputStream() {}

  int print(const char * fmt, ...);
  int println(const char * fmt, ...);
};

class NullOutputStream : public OutputStream {
public:
  virtual ~NullOutputStream() {}
  int print(const char * /* unused */, ...) { return 1;}
  int println(const char * /* unused */, ...) { return 1;}
};

#endif
