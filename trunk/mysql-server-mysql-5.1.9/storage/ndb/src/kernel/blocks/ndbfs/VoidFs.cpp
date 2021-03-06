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

#include <limits.h>
#include <errno.h>

#include "Ndbfs.hpp"
#include "AsyncFile.hpp"
#include "Filename.hpp"

#include <signaldata/FsOpenReq.hpp>
#include <signaldata/FsCloseReq.hpp>
#include <signaldata/FsReadWriteReq.hpp>
#include <signaldata/FsAppendReq.hpp>
#include <signaldata/FsRemoveReq.hpp>
#include <signaldata/FsConf.hpp>
#include <signaldata/FsRef.hpp>
#include <signaldata/NdbfsContinueB.hpp>
#include <signaldata/DumpStateOrd.hpp>

#include <RefConvert.hpp>
#include <NdbSleep.h>
#include <NdbOut.hpp>
#include <Configuration.hpp>

#define DEBUG(x) { ndbout << "FS::" << x << endl; }

VoidFs::VoidFs(Block_context & ctx) :
  SimulatedBlock(NDBFS, ctx)
{
  BLOCK_CONSTRUCTOR(VoidFs);
  
  // Set received signals
  addRecSignal(GSN_READ_CONFIG_REQ, &VoidFs::execREAD_CONFIG_REQ);
  addRecSignal(GSN_DUMP_STATE_ORD,  &VoidFs::execDUMP_STATE_ORD);
  addRecSignal(GSN_STTOR,  &VoidFs::execSTTOR);
  addRecSignal(GSN_FSOPENREQ, &VoidFs::execFSOPENREQ);
  addRecSignal(GSN_FSCLOSEREQ, &VoidFs::execFSCLOSEREQ);
  addRecSignal(GSN_FSWRITEREQ, &VoidFs::execFSWRITEREQ);
  addRecSignal(GSN_FSREADREQ, &VoidFs::execFSREADREQ);
  addRecSignal(GSN_FSSYNCREQ, &VoidFs::execFSSYNCREQ);
  addRecSignal(GSN_FSAPPENDREQ, &VoidFs::execFSAPPENDREQ);
  addRecSignal(GSN_FSREMOVEREQ, &VoidFs::execFSREMOVEREQ);
   // Set send signals
}

VoidFs::~VoidFs()
{
}

void 
VoidFs::execREAD_CONFIG_REQ(Signal* signal)
{
  const ReadConfigReq * req = (ReadConfigReq*)signal->getDataPtr();

  Uint32 ref = req->senderRef;
  Uint32 senderData = req->senderData;

  ReadConfigConf * conf = (ReadConfigConf*)signal->getDataPtrSend();
  conf->senderRef = reference();
  conf->senderData = senderData;
  sendSignal(ref, GSN_READ_CONFIG_CONF, signal, 
	     ReadConfigConf::SignalLength, JBB);
}

void
VoidFs::execSTTOR(Signal* signal)
{
  jamEntry();
  
  if(signal->theData[1] == 0){ // StartPhase 0
    jam();
    signal->theData[3] = 255;
    sendSignal(NDBCNTR_REF, GSN_STTORRY, signal, 4, JBB);
    return;
  }
  ndbrequire(0);
}

void 
VoidFs::execFSOPENREQ(Signal* signal)
{
  jamEntry();
  const FsOpenReq * const fsOpenReq = (FsOpenReq *)&signal->theData[0];
  const BlockReference userRef = fsOpenReq->userReference;
  const Uint32 userPointer = fsOpenReq->userPointer;

  Uint32 flags = fsOpenReq->fileFlags;
  if(flags == FsOpenReq::OM_READONLY){
    // Initialise FsRef signal
    FsRef * const fsRef = (FsRef *)&signal->theData[0];
    fsRef->userPointer = userPointer;
    fsRef->errorCode = FsRef::fsErrFileDoesNotExist;
    fsRef->osErrorCode = ~0; 
    sendSignal(userRef, GSN_FSOPENREF, signal, 3, JBB);
    return;
  }

  if(flags & FsOpenReq::OM_WRITEONLY || flags & FsOpenReq::OM_READWRITE){
    signal->theData[0] = userPointer;
    signal->theData[1] = c_maxFileNo++;
    sendSignal(userRef, GSN_FSOPENCONF, signal, 2, JBB);
  }
}

void 
VoidFs::execFSREMOVEREQ(Signal* signal)
{
  jamEntry();
  const FsRemoveReq * const req = (FsRemoveReq *)signal->getDataPtr();
  const Uint32 userRef = req->userReference;
  const Uint32 userPointer = req->userPointer;

  signal->theData[0] = userPointer;
  sendSignal(userRef, GSN_FSREMOVECONF, signal, 1, JBB);
}

/*
 * PR0: File Pointer DR0: User reference DR1: User Pointer DR2: Flag bit 0= 1
 * remove file
 */
void 
VoidFs::execFSCLOSEREQ(Signal * signal)
{
  jamEntry();
  
  const FsCloseReq * const req = (FsCloseReq *)signal->getDataPtr();
  const Uint32 userRef = req->userReference;
  const Uint32 userPointer = req->userPointer;
  
  signal->theData[0] = userPointer;
  sendSignal(userRef, GSN_FSCLOSECONF, signal, 1, JBB);
}

void 
VoidFs::execFSWRITEREQ(Signal* signal)
{
  jamEntry();
  const FsReadWriteReq * const req = (FsReadWriteReq *)signal->getDataPtr();
  const Uint32 userRef = req->userReference;
  const Uint32 userPointer = req->userPointer;

  signal->theData[0] = userPointer;
  sendSignal(userRef, GSN_FSWRITECONF, signal, 1, JBB);
}

void 
VoidFs::execFSREADREQ(Signal* signal)
{
  jamEntry();

  const FsReadWriteReq * const req = (FsReadWriteReq *)signal->getDataPtr();
  const Uint32 userRef = req->userReference;
  const Uint32 userPointer = req->userPointer;

  signal->theData[0] = userPointer;
  sendSignal(userRef, GSN_FSREADCONF, signal, 1, JBB);
#if 0
  FsRef * const fsRef = (FsRef *)&signal->theData[0];
  fsRef->userPointer = userPointer;
  fsRef->errorCode = FsRef::fsErrEnvironmentError;
  fsRef->osErrorCode = ~0; // Indicate local error
  sendSignal(userRef, GSN_FSREADREF, signal, 3, JBB);
#endif
}

void
VoidFs::execFSSYNCREQ(Signal * signal)
{
  jamEntry();

  BlockReference userRef = signal->theData[1];
  const UintR userPointer = signal->theData[2]; 

  signal->theData[0] = userPointer;
  sendSignal(userRef, GSN_FSSYNCCONF, signal, 1, JBB);

  return;
}

void 
VoidFs::execFSAPPENDREQ(Signal * signal)
{
  const FsAppendReq * const fsReq = (FsAppendReq *)&signal->theData[0];
  const UintR userPointer = fsReq->userPointer; 
  const BlockReference userRef = fsReq->userReference;
  const Uint32 size = fsReq->size;
  
  signal->theData[0] = userPointer;
  signal->theData[1] = size << 2;
  sendSignal(userRef, GSN_FSAPPENDCONF, signal, 2, JBB);
}

void
VoidFs::execDUMP_STATE_ORD(Signal* signal)
{
}//VoidFs::execDUMP_STATE_ORD()



BLOCK_FUNCTIONS(VoidFs)

