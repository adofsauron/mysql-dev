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

#include <signaldata/TuxMaint.hpp>
#include <SignalLoggerManager.hpp>
#include <AttributeHeader.hpp>

bool
printTUX_MAINT_REQ(FILE* output, const Uint32* theData, Uint32 len, Uint16 rbn)
{
  //const bool inOut = rbn & (1 << 15);
  const TuxMaintReq* const sig = (const TuxMaintReq*)theData;
  fprintf(output, " errorCode=%d\n", sig->errorCode);
  fprintf(output, " table: id=%u", sig->tableId);
  fprintf(output, " index: id=%u", sig->indexId);
  fprintf(output, " fragment: id=%u\n", sig->fragId);
  fprintf(output, " tuple: loc=%u.%u version=%u\n", sig->pageId, sig->pageIndex, sig->tupVersion);
  const Uint32 opCode = sig->opInfo & 0xFF;
  const Uint32 opFlag = sig->opInfo >> 8;
  switch (opCode ) {
  case TuxMaintReq::OpAdd:
    fprintf(output, " opCode=Add opFlag=%u\n", opFlag);
    break;
  case TuxMaintReq::OpRemove:
    fprintf(output, " opCode=Remove opFlag=%u\n", opFlag);
    break;
  default:
    fprintf(output, " opInfo=%x ***invalid***\n", sig->opInfo);
    break;
  }
  return true;
}
