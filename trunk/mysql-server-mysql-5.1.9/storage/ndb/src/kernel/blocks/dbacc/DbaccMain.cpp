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

#define DBACC_C
#include "Dbacc.hpp"
#include <my_sys.h>

#include <AttributeHeader.hpp>
#include <signaldata/AccFrag.hpp>
#include <signaldata/AccScan.hpp>
#include <signaldata/NextScan.hpp>
#include <signaldata/AccLock.hpp>
#include <signaldata/EventReport.hpp>
#include <signaldata/FsConf.hpp>
#include <signaldata/FsRef.hpp>
#include <signaldata/FsRemoveReq.hpp>
#include <signaldata/DropTab.hpp>
#include <signaldata/DumpStateOrd.hpp>
#include <signaldata/TuxMaint.hpp>
#include <KeyDescriptor.hpp>

// TO_DO_RONM is a label for comments on what needs to be improved in future versions
// when more time is given.

#ifdef VM_TRACE
#define DEBUG(x) ndbout << "DBACC: "<< x << endl;
#else
#define DEBUG(x)
#endif


// Signal entries and statement blocks
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       COMMON SIGNAL RECEPTION MODULE                                              */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------- */
/* ******************--------------------------------------------------------------- */
/* CONTINUEB                                       CONTINUE SIGNAL                   */
/* ******************------------------------------+                                 */
/*   SENDER: ACC,    LEVEL B       */
void Dbacc::execCONTINUEB(Signal* signal) 
{
  Uint32 tcase;

  jamEntry();
  tcase = signal->theData[0];
  tdata0 = signal->theData[1];
  tresult = 0;
  switch (tcase) {
  case ZINITIALISE_RECORDS:
    jam();
    initialiseRecordsLab(signal, signal->theData[3], signal->theData[4]);
    return;
    break;
  case ZSEND_SCAN_HBREP:
    jam();
    sendScanHbRep(signal, tdata0);
    break;
  case ZREL_ROOT_FRAG:
    {
      jam();
      Uint32 tableId = signal->theData[1];
      releaseRootFragResources(signal, tableId);
      break;
    }
  case ZREL_FRAG:
    {
      jam();
      Uint32 fragIndex = signal->theData[1];
      releaseFragResources(signal, fragIndex);
      break;
    }
  case ZREL_DIR:
    {
      jam();
      Uint32 fragIndex = signal->theData[1];
      Uint32 dirIndex = signal->theData[2];
      Uint32 startIndex = signal->theData[3];
      releaseDirResources(signal, fragIndex, dirIndex, startIndex);
      break;
    }
  case ZREPORT_MEMORY_USAGE:{
    jam();
    static int c_currentMemUsed = 0;
    int now = (cnoOfAllocatedPages * 100)/cpagesize;
    const int thresholds[] = { 99, 90, 80, 0};
    
    Uint32 i = 0;
    const Uint32 sz = sizeof(thresholds)/sizeof(thresholds[0]);
    for(i = 0; i<sz; i++){
      if(now >= thresholds[i]){
	now = thresholds[i];
	break;
      }
    }
    
    if(now != c_currentMemUsed){
      reportMemoryUsage(signal, now > c_currentMemUsed ? 1 : -1);
    }
    
    c_currentMemUsed = now;
    
    signal->theData[0] = ZREPORT_MEMORY_USAGE;
    sendSignalWithDelay(reference(), GSN_CONTINUEB, signal, 2000, 1);    
    return;
  }

  default:
    ndbrequire(false);
    break;
  }//switch
  return;
}//Dbacc::execCONTINUEB()

/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/*                                                                           */
/*       END OF COMMON SIGNAL RECEPTION MODULE                               */
/*                                                                           */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/*                                                                           */
/*       SYSTEM RESTART MODULE                                               */
/*                                                                           */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
void Dbacc::execNDB_STTOR(Signal* signal) 
{
  Uint32 tstartphase;
  Uint32 tStartType;

  jamEntry();
  cndbcntrRef = signal->theData[0];
  cmynodeid = signal->theData[1];
  tstartphase = signal->theData[2];
  tStartType = signal->theData[3];
  switch (tstartphase) {
  case ZSPH1:
    jam();
    ndbsttorryLab(signal);
    return;
    break;
  case ZSPH2:
    ndbsttorryLab(signal);
    return;
    break;
  case ZSPH3:
    if ((tStartType == NodeState::ST_NODE_RESTART) ||
        (tStartType == NodeState::ST_INITIAL_NODE_RESTART)) {
      jam();
      //---------------------------------------------
      // csystemRestart is used to check what is needed
      // during log execution. When starting a node it
      // is not a log execution and rather a normal
      // execution. Thus we reset the variable here to
      // avoid unnecessary system crashes.
      //---------------------------------------------
      csystemRestart = ZFALSE;
    }//if
    break;
  case ZSPH6:
    jam();
    csystemRestart = ZFALSE;

    signal->theData[0] = ZREPORT_MEMORY_USAGE;
    sendSignalWithDelay(reference(), GSN_CONTINUEB, signal, 2000, 1);    
    break;
  default:
    jam();
    /*empty*/;
    break;
  }//switch
  ndbsttorryLab(signal);
  return;
}//Dbacc::execNDB_STTOR()

/* ******************--------------------------------------------------------------- */
/* STTOR                                              START /  RESTART               */
/* ******************------------------------------+                                 */
/*   SENDER: ANY,    LEVEL B       */
void Dbacc::execSTTOR(Signal* signal) 
{
  jamEntry();
  Uint32 tstartphase = signal->theData[1];
  switch (tstartphase) {
  case 1:
    jam();
    c_tup = (Dbtup*)globalData.getBlock(DBTUP);
    ndbrequire(c_tup != 0);
    break;
  }
  tuserblockref = signal->theData[3];
  csignalkey = signal->theData[6];
  sttorrysignalLab(signal);
  return;
}//Dbacc::execSTTOR()

/* --------------------------------------------------------------------------------- */
/* ZSPH1                                                                             */
/* --------------------------------------------------------------------------------- */
void Dbacc::ndbrestart1Lab(Signal* signal) 
{
  cmynodeid = globalData.ownId;
  cownBlockref = numberToRef(DBACC, cmynodeid);
  czero = 0;
  cminusOne = czero - 1;
  ctest = 0;
  csystemRestart = ZTRUE;
  return;
}//Dbacc::ndbrestart1Lab()

void Dbacc::initialiseRecordsLab(Signal* signal, Uint32 ref, Uint32 data) 
{
  switch (tdata0) {
  case 0:
    jam();
    initialiseTableRec(signal);
    break;
  case 1:
  case 2:
    break;
  case 3:
    jam();
    break;
  case 4:
    jam();
    initialiseDirRec(signal);
    break;
  case 5:
    jam();
    initialiseDirRangeRec(signal);
    break;
  case 6:
    jam();
    initialiseFragRec(signal);
    break;
  case 7:
    jam();
    initialiseOverflowRec(signal);
    break;
  case 8:
    jam();
    initialiseOperationRec(signal);
    break;
  case 9:
    jam();
    initialisePageRec(signal);
    break;
  case 10:
    jam();
    break;
  case 11:
    jam();
    initialiseScanRec(signal);
    break;
  case 12:
    jam();

    {
      ReadConfigConf * conf = (ReadConfigConf*)signal->getDataPtrSend();
      conf->senderRef = reference();
      conf->senderData = data;
      sendSignal(ref, GSN_READ_CONFIG_CONF, signal, 
		 ReadConfigConf::SignalLength, JBB);
    }
    return;
    break;
  default:
    ndbrequire(false);
    break;
  }//switch

  signal->theData[0] = ZINITIALISE_RECORDS;
  signal->theData[1] = tdata0 + 1;
  signal->theData[2] = 0;
  signal->theData[3] = ref;
  signal->theData[4] = data;
  sendSignal(reference(), GSN_CONTINUEB, signal, 5, JBB);
  return;
}//Dbacc::initialiseRecordsLab()

/* *********************************<< */
/* NDB_STTORRY                         */
/* *********************************<< */
void Dbacc::ndbsttorryLab(Signal* signal) 
{
  signal->theData[0] = cownBlockref;
  sendSignal(cndbcntrRef, GSN_NDB_STTORRY, signal, 1, JBB);
  return;
}//Dbacc::ndbsttorryLab()

/* *********************************<< */
/* SIZEALT_REP         SIZE ALTERATION */
/* *********************************<< */
void Dbacc::execREAD_CONFIG_REQ(Signal* signal) 
{
  const ReadConfigReq * req = (ReadConfigReq*)signal->getDataPtr();
  Uint32 ref = req->senderRef;
  Uint32 senderData = req->senderData;
  ndbrequire(req->noOfParameters == 0);
  
  jamEntry();

  const ndb_mgm_configuration_iterator * p = 
    m_ctx.m_config.getOwnConfigIterator();
  ndbrequire(p != 0);
  
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_ACC_DIR_RANGE, &cdirrangesize));
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_ACC_DIR_ARRAY, &cdirarraysize));
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_ACC_FRAGMENT, &cfragmentsize));
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_ACC_OP_RECS, &coprecsize));
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_ACC_OVERFLOW_RECS, 
					&coverflowrecsize));
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_ACC_PAGE8, &cpagesize));
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_ACC_TABLE, &ctablesize));
  ndbrequire(!ndb_mgm_get_int_parameter(p, CFG_ACC_SCAN, &cscanRecSize));
  initRecords();
  ndbrestart1Lab(signal);

  tdata0 = 0;
  initialiseRecordsLab(signal, ref, senderData);
  return;
}//Dbacc::execSIZEALT_REP()

/* *********************************<< */
/* STTORRY                             */
/* *********************************<< */
void Dbacc::sttorrysignalLab(Signal* signal) 
{
  signal->theData[0] = csignalkey;
  signal->theData[1] = 3;
  /* BLOCK CATEGORY */
  signal->theData[2] = 2;
  /* SIGNAL VERSION NUMBER */
  signal->theData[3] = ZSPH1;
  signal->theData[4] = 255;
  sendSignal(NDBCNTR_REF, GSN_STTORRY, signal, 5, JBB);
  /* END OF START PHASES */
  return;
}//Dbacc::sttorrysignalLab()

/* --------------------------------------------------------------------------------- */
/* INITIALISE_DIR_REC                                                                */
/*              INITIALATES THE DIRECTORY RECORDS.                                   */
/* --------------------------------------------------------------------------------- */
void Dbacc::initialiseDirRec(Signal* signal) 
{
  DirectoryarrayPtr idrDirptr;
  ndbrequire(cdirarraysize > 0);
  for (idrDirptr.i = 0; idrDirptr.i < cdirarraysize; idrDirptr.i++) {
    refresh_watch_dog();
    ptrAss(idrDirptr, directoryarray);
    for (Uint32 i = 0; i <= 255; i++) {
      idrDirptr.p->pagep[i] = RNIL;
    }//for
  }//for
  cdirmemory = 0;
  cfirstfreedir = RNIL;
}//Dbacc::initialiseDirRec()

/* --------------------------------------------------------------------------------- */
/* INITIALISE_DIR_RANGE_REC                                                          */
/*              INITIALATES THE DIR_RANGE RECORDS.                                   */
/* --------------------------------------------------------------------------------- */
void Dbacc::initialiseDirRangeRec(Signal* signal) 
{
  DirRangePtr idrDirRangePtr;

  ndbrequire(cdirrangesize > 0);
  for (idrDirRangePtr.i = 0; idrDirRangePtr.i < cdirrangesize; idrDirRangePtr.i++) {
    refresh_watch_dog();
    ptrAss(idrDirRangePtr, dirRange);
    idrDirRangePtr.p->dirArray[0] = idrDirRangePtr.i + 1;
    for (Uint32 i = 1; i < 256; i++) {
      idrDirRangePtr.p->dirArray[i] = RNIL;
    }//for
  }//for
  idrDirRangePtr.i = cdirrangesize - 1;
  ptrAss(idrDirRangePtr, dirRange);
  idrDirRangePtr.p->dirArray[0] = RNIL;
  cfirstfreeDirrange = 0;
}//Dbacc::initialiseDirRangeRec()

/* --------------------------------------------------------------------------------- */
/* INITIALISE_FRAG_REC                                                               */
/*              INITIALATES THE FRAGMENT RECORDS.                                    */
/* --------------------------------------------------------------------------------- */
void Dbacc::initialiseFragRec(Signal* signal) 
{
  FragmentrecPtr regFragPtr;
  ndbrequire(cfragmentsize > 0);
  for (regFragPtr.i = 0; regFragPtr.i < cfragmentsize; regFragPtr.i++) {
    jam();
    refresh_watch_dog();
    ptrAss(regFragPtr, fragmentrec);
    initFragGeneral(regFragPtr);
    regFragPtr.p->nextfreefrag = regFragPtr.i + 1;
  }//for
  regFragPtr.i = cfragmentsize - 1;
  ptrAss(regFragPtr, fragmentrec);
  regFragPtr.p->nextfreefrag = RNIL;
  cfirstfreefrag = 0;
}//Dbacc::initialiseFragRec()

/* --------------------------------------------------------------------------------- */
/* INITIALISE_OPERATION_REC                                                          */
/*              INITIALATES THE OPERATION RECORDS.                                   */
/* --------------------------------------------------------------------------------- */
void Dbacc::initialiseOperationRec(Signal* signal) 
{
  ndbrequire(coprecsize > 0);
  for (operationRecPtr.i = 0; operationRecPtr.i < coprecsize; operationRecPtr.i++) {
    refresh_watch_dog();
    ptrAss(operationRecPtr, operationrec);
    operationRecPtr.p->transactionstate = IDLE;
    operationRecPtr.p->operation = ZUNDEFINED_OP;
    operationRecPtr.p->opState = FREE_OP;
    operationRecPtr.p->nextOp = operationRecPtr.i + 1;
  }//for
  operationRecPtr.i = coprecsize - 1;
  ptrAss(operationRecPtr, operationrec);
  operationRecPtr.p->nextOp = RNIL;
  cfreeopRec = 0;
}//Dbacc::initialiseOperationRec()

/* --------------------------------------------------------------------------------- */
/* INITIALISE_OVERFLOW_REC                                                           */
/*              INITIALATES THE OVERFLOW RECORDS                                     */
/* --------------------------------------------------------------------------------- */
void Dbacc::initialiseOverflowRec(Signal* signal) 
{
  OverflowRecordPtr iorOverflowRecPtr;

  ndbrequire(coverflowrecsize > 0);
  for (iorOverflowRecPtr.i = 0; iorOverflowRecPtr.i < coverflowrecsize; iorOverflowRecPtr.i++) {
    refresh_watch_dog();
    ptrAss(iorOverflowRecPtr, overflowRecord);
    iorOverflowRecPtr.p->nextfreeoverrec = iorOverflowRecPtr.i + 1;
  }//for
  iorOverflowRecPtr.i = coverflowrecsize - 1;
  ptrAss(iorOverflowRecPtr, overflowRecord);
  iorOverflowRecPtr.p->nextfreeoverrec = RNIL;
  cfirstfreeoverrec = 0;
}//Dbacc::initialiseOverflowRec()

/* --------------------------------------------------------------------------------- */
/* INITIALISE_PAGE_REC                                                               */
/*              INITIALATES THE PAGE RECORDS.                                        */
/* --------------------------------------------------------------------------------- */
void Dbacc::initialisePageRec(Signal* signal) 
{
  ndbrequire(cpagesize > 0);
  cfreepage = 0;
  cfirstfreepage = RNIL;
  cnoOfAllocatedPages = 0;
}//Dbacc::initialisePageRec()


/* --------------------------------------------------------------------------------- */
/* INITIALISE_ROOTFRAG_REC                                                           */
/*              INITIALATES THE ROOTFRAG  RECORDS.                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* INITIALISE_SCAN_REC                                                               */
/*              INITIALATES THE QUE_SCAN RECORDS.                                    */
/* --------------------------------------------------------------------------------- */
void Dbacc::initialiseScanRec(Signal* signal) 
{
  ndbrequire(cscanRecSize > 0);
  for (scanPtr.i = 0; scanPtr.i < cscanRecSize; scanPtr.i++) {
    ptrAss(scanPtr, scanRec);
    scanPtr.p->scanNextfreerec = scanPtr.i + 1;
    scanPtr.p->scanState = ScanRec::SCAN_DISCONNECT;
    scanPtr.p->scanTimer = 0;
    scanPtr.p->scanContinuebCounter = 0;
  }//for
  scanPtr.i = cscanRecSize - 1;
  ptrAss(scanPtr, scanRec);
  scanPtr.p->scanNextfreerec = RNIL;
  cfirstFreeScanRec = 0;
}//Dbacc::initialiseScanRec()


/* --------------------------------------------------------------------------------- */
/* INITIALISE_TABLE_REC                                                              */
/*              INITIALATES THE TABLE RECORDS.                                       */
/* --------------------------------------------------------------------------------- */
void Dbacc::initialiseTableRec(Signal* signal) 
{
  ndbrequire(ctablesize > 0);
  for (tabptr.i = 0; tabptr.i < ctablesize; tabptr.i++) {
    refresh_watch_dog();
    ptrAss(tabptr, tabrec);
    for (Uint32 i = 0; i < MAX_FRAG_PER_NODE; i++) {
      tabptr.p->fragholder[i] = RNIL;
      tabptr.p->fragptrholder[i] = RNIL;
    }//for
  }//for
}//Dbacc::initialiseTableRec()

/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       END OF SYSTEM RESTART MODULE                                                */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       ADD/DELETE FRAGMENT MODULE                                                  */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */

// JONAS This methods "aer ett saall"
void Dbacc::execACCFRAGREQ(Signal* signal) 
{
  const AccFragReq * const req = (AccFragReq*)&signal->theData[0];
  jamEntry();
  if (ERROR_INSERTED(3001)) {
    jam();
    addFragRefuse(signal, 1);
    CLEAR_ERROR_INSERT_VALUE;
    return;
  }
  tabptr.i = req->tableId;
#ifndef VM_TRACE
  // config mismatch - do not crash if release compiled
  if (tabptr.i >= ctablesize) {
    jam();
    addFragRefuse(signal, 640);
    return;
  }
#endif
  ptrCheckGuard(tabptr, ctablesize, tabrec);
  ndbrequire((req->reqInfo & 0xF) == ZADDFRAG);
  ndbrequire(!getfragmentrec(signal, fragrecptr, req->fragId));
  if (cfirstfreefrag == RNIL) {
    jam();
    addFragRefuse(signal, ZFULL_FRAGRECORD_ERROR);
    return;
  }//if

  seizeFragrec(signal);
  initFragGeneral(fragrecptr);
  initFragAdd(signal, fragrecptr);

  if (!addfragtotab(signal, fragrecptr.i, req->fragId)) {
    jam();
    releaseFragRecord(signal, fragrecptr);
    addFragRefuse(signal, ZFULL_FRAGRECORD_ERROR);
    return;
  }//if
  if (cfirstfreeDirrange == RNIL) {
    jam();
    releaseFragRecord(signal, fragrecptr);
    addFragRefuse(signal, ZDIR_RANGE_ERROR);
    return;
  } else {
    jam();
    seizeDirrange(signal);
  }//if

  fragrecptr.p->directory = newDirRangePtr.i;
  seizeDirectory(signal);
  if (tresult < ZLIMIT_OF_ERROR) {
    jam();
    newDirRangePtr.p->dirArray[0] = sdDirptr.i;
  } else {
    jam();
    addFragRefuse(signal, tresult);
    return;
  }//if

  seizePage(signal);
  if (tresult > ZLIMIT_OF_ERROR) {
    jam();
    addFragRefuse(signal, tresult);
    return;
  }//if
  sdDirptr.p->pagep[0] = spPageptr.i;
  tipPageId = 0;
  inpPageptr = spPageptr;
  initPage(signal);
  if (cfirstfreeDirrange == RNIL) {
    jam();
    addFragRefuse(signal, ZDIR_RANGE_ERROR);
    return;
  } else {
    jam();
    seizeDirrange(signal);
  }//if
  fragrecptr.p->overflowdir = newDirRangePtr.i;
  seizeDirectory(signal);
  if (tresult < ZLIMIT_OF_ERROR) {
    jam();
    newDirRangePtr.p->dirArray[0] = sdDirptr.i;
  } else {
    jam();
    addFragRefuse(signal, tresult);
    return;
  }//if

  Uint32 userPtr = req->userPtr;
  BlockReference retRef = req->userRef;
  fragrecptr.p->rootState = ACTIVEROOT;

  AccFragConf * const conf = (AccFragConf*)&signal->theData[0];
  conf->userPtr = userPtr;
  conf->rootFragPtr = RNIL;
  conf->fragId[0] = fragrecptr.p->fragmentid;
  conf->fragId[1] = RNIL;
  conf->fragPtr[0] = fragrecptr.i;
  conf->fragPtr[1] = RNIL;
  conf->rootHashCheck = fragrecptr.p->roothashcheck;
  sendSignal(retRef, GSN_ACCFRAGCONF, signal, AccFragConf::SignalLength, JBB);
}//Dbacc::execACCFRAGREQ()

void Dbacc::addFragRefuse(Signal* signal, Uint32 errorCode) 
{
  const AccFragReq * const req = (AccFragReq*)&signal->theData[0];  
  AccFragRef * const ref = (AccFragRef*)&signal->theData[0];  
  Uint32 userPtr = req->userPtr;
  BlockReference retRef = req->userRef;

  ref->userPtr = userPtr;
  ref->errorCode = errorCode;
  sendSignal(retRef, GSN_ACCFRAGREF, signal, AccFragRef::SignalLength, JBB);
  return;
}//Dbacc::addFragRefuseEarly()

void
Dbacc::execDROP_TAB_REQ(Signal* signal){
  jamEntry();
  DropTabReq* req = (DropTabReq*)signal->getDataPtr();

  TabrecPtr tabPtr;
  tabPtr.i = req->tableId;
  ptrCheckGuard(tabPtr, ctablesize, tabrec);
  
  tabPtr.p->tabUserRef = req->senderRef;
  tabPtr.p->tabUserPtr = req->senderData;
  
  signal->theData[0] = ZREL_ROOT_FRAG;
  signal->theData[1] = tabPtr.i;
  sendSignal(cownBlockref, GSN_CONTINUEB, signal, 2, JBB);
}

void Dbacc::releaseRootFragResources(Signal* signal, Uint32 tableId)
{
  FragmentrecPtr rootPtr;
  TabrecPtr tabPtr;
  tabPtr.i = tableId;
  ptrCheckGuard(tabPtr, ctablesize, tabrec);
  for (Uint32 i = 0; i < MAX_FRAG_PER_NODE; i++) {
    jam();
    if (tabPtr.p->fragholder[i] != RNIL) {
      jam();
      tabPtr.p->fragholder[i] = RNIL;
      releaseFragResources(signal, tabPtr.p->fragptrholder[i]);
      return;
    }//if
  }//for
  
  /**
   * Finished...
   */

  DropTabConf * const dropConf = (DropTabConf *)signal->getDataPtrSend();
  dropConf->senderRef = reference();
  dropConf->senderData = tabPtr.p->tabUserPtr;
  dropConf->tableId = tabPtr.i;
  sendSignal(tabPtr.p->tabUserRef, GSN_DROP_TAB_CONF,
             signal, DropTabConf::SignalLength, JBB);
  
  tabPtr.p->tabUserPtr = RNIL;
  tabPtr.p->tabUserRef = 0;
}//Dbacc::releaseRootFragResources()

void Dbacc::releaseFragResources(Signal* signal, Uint32 fragIndex)
{
  FragmentrecPtr regFragPtr;
  regFragPtr.i = fragIndex;
  ptrCheckGuard(regFragPtr, cfragmentsize, fragmentrec);
  verifyFragCorrect(regFragPtr);
  if (regFragPtr.p->directory != RNIL) {
    jam();
    releaseDirResources(signal, regFragPtr.i, regFragPtr.p->directory, 0);
    regFragPtr.p->directory = RNIL;
  } else if (regFragPtr.p->overflowdir != RNIL) {
    jam();
    releaseDirResources(signal, regFragPtr.i, regFragPtr.p->overflowdir, 0);
    regFragPtr.p->overflowdir = RNIL;
  } else if (regFragPtr.p->firstOverflowRec != RNIL) {
    jam();
    releaseOverflowResources(signal, regFragPtr);
  } else if (regFragPtr.p->firstFreeDirindexRec != RNIL) {
    jam();
    releaseDirIndexResources(signal, regFragPtr);
  } else {
    jam();
    Uint32 tab = regFragPtr.p->mytabptr;
    releaseFragRecord(signal, regFragPtr);
    signal->theData[0] = ZREL_ROOT_FRAG;
    signal->theData[1] = tab;
    sendSignal(cownBlockref, GSN_CONTINUEB, signal, 2, JBB);
  }//if
}//Dbacc::releaseFragResources()

void Dbacc::verifyFragCorrect(FragmentrecPtr regFragPtr)
{
  ndbrequire(regFragPtr.p->lockOwnersList == RNIL);
}//Dbacc::verifyFragCorrect()

void Dbacc::releaseDirResources(Signal* signal, 
				Uint32 fragIndex, 
				Uint32 dirIndex, 
				Uint32 startIndex)
{
  DirRangePtr regDirRangePtr;
  regDirRangePtr.i = dirIndex;
  ptrCheckGuard(regDirRangePtr, cdirrangesize, dirRange);
  for (Uint32 i = startIndex; i < 256; i++) {
    jam();
    if (regDirRangePtr.p->dirArray[i] != RNIL) {
      jam();
      Uint32 directoryIndex = regDirRangePtr.p->dirArray[i];
      regDirRangePtr.p->dirArray[i] = RNIL;
      releaseDirectoryResources(signal, fragIndex, dirIndex, (i + 1), directoryIndex);
      return;
    }//if
  }//for
  rdDirRangePtr = regDirRangePtr;
  releaseDirrange(signal);
  signal->theData[0] = ZREL_FRAG;
  signal->theData[1] = fragIndex;
  sendSignal(cownBlockref, GSN_CONTINUEB, signal, 2, JBB);
}//Dbacc::releaseDirResources()

void Dbacc::releaseDirectoryResources(Signal* signal,
                                      Uint32 fragIndex,
                                      Uint32 dirIndex,
                                      Uint32 startIndex,
                                      Uint32 directoryIndex)
{
  DirectoryarrayPtr regDirPtr;
  regDirPtr.i = directoryIndex;
  ptrCheckGuard(regDirPtr, cdirarraysize, directoryarray);
  for (Uint32 i = 0; i < 256; i++) {
    jam();
    if (regDirPtr.p->pagep[i] != RNIL) {
      jam();
      rpPageptr.i = regDirPtr.p->pagep[i];
      ptrCheckGuard(rpPageptr, cpagesize, page8);
      releasePage(signal);
      regDirPtr.p->pagep[i] = RNIL;
    }//if
  }//for
  rdDirptr = regDirPtr;
  releaseDirectory(signal);
  signal->theData[0] = ZREL_DIR;
  signal->theData[1] = fragIndex;
  signal->theData[2] = dirIndex;
  signal->theData[3] = startIndex;
  sendSignal(cownBlockref, GSN_CONTINUEB, signal, 4, JBB);
}//Dbacc::releaseDirectoryResources()

void Dbacc::releaseOverflowResources(Signal* signal, FragmentrecPtr regFragPtr)
{
  Uint32 loopCount = 0;
  OverflowRecordPtr regOverflowRecPtr;
  while ((regFragPtr.p->firstOverflowRec != RNIL) &&
         (loopCount < 1)) {
    jam();
    regOverflowRecPtr.i = regFragPtr.p->firstOverflowRec;
    ptrCheckGuard(regOverflowRecPtr, coverflowrecsize, overflowRecord);
    regFragPtr.p->firstOverflowRec = regOverflowRecPtr.p->nextOverRec;
    rorOverflowRecPtr = regOverflowRecPtr;
    releaseOverflowRec(signal);
    loopCount++;
  }//while
  signal->theData[0] = ZREL_FRAG;
  signal->theData[1] = regFragPtr.i;
  sendSignal(cownBlockref, GSN_CONTINUEB, signal, 2, JBB);
}//Dbacc::releaseOverflowResources()

void Dbacc::releaseDirIndexResources(Signal* signal, FragmentrecPtr regFragPtr)
{
  Uint32 loopCount = 0;
  OverflowRecordPtr regOverflowRecPtr;
  while ((regFragPtr.p->firstFreeDirindexRec != RNIL) &&
         (loopCount < 1)) {
    jam();
    regOverflowRecPtr.i = regFragPtr.p->firstFreeDirindexRec;
    ptrCheckGuard(regOverflowRecPtr, coverflowrecsize, overflowRecord);
    regFragPtr.p->firstFreeDirindexRec = regOverflowRecPtr.p->nextOverList;
    rorOverflowRecPtr = regOverflowRecPtr;
    releaseOverflowRec(signal);
    loopCount++;
  }//while
  signal->theData[0] = ZREL_FRAG;
  signal->theData[1] = regFragPtr.i;
  sendSignal(cownBlockref, GSN_CONTINUEB, signal, 2, JBB);
}//Dbacc::releaseDirIndexResources()

void Dbacc::releaseFragRecord(Signal* signal, FragmentrecPtr regFragPtr) 
{
  regFragPtr.p->nextfreefrag = cfirstfreefrag;
  cfirstfreefrag = regFragPtr.i;
  initFragGeneral(regFragPtr);
}//Dbacc::releaseFragRecord()

/* -------------------------------------------------------------------------- */
/* ADDFRAGTOTAB                                                               */
/*       DESCRIPTION: PUTS A FRAGMENT ID AND A POINTER TO ITS RECORD INTO     */
/*                                TABLE ARRRAY OF THE TABLE RECORD.           */
/* -------------------------------------------------------------------------- */
bool Dbacc::addfragtotab(Signal* signal, Uint32 rootIndex, Uint32 fid) 
{
  for (Uint32 i = 0; i < MAX_FRAG_PER_NODE; i++) {
    jam();
    if (tabptr.p->fragholder[i] == RNIL) {
      jam();
      tabptr.p->fragholder[i] = fid;
      tabptr.p->fragptrholder[i] = rootIndex;
      return true;
    }//if
  }//for
  return false;
}//Dbacc::addfragtotab()

/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       END OF ADD/DELETE FRAGMENT MODULE                                           */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       CONNECTION MODULE                                                           */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* ******************--------------------------------------------------------------- */
/* ACCSEIZEREQ                                           SEIZE REQ                   */
/*                                                    SENDER: LQH,    LEVEL B        */
/*          ENTER ACCSEIZEREQ WITH                                                   */
/*                    TUSERPTR ,                     CONECTION PTR OF LQH            */
/*                    TUSERBLOCKREF                  BLOCK REFERENCE OF LQH          */
/* ******************--------------------------------------------------------------- */
/* ******************--------------------------------------------------------------- */
/* ACCSEIZEREQ                                           SEIZE REQ                   */
/* ******************------------------------------+                                 */
/*   SENDER: LQH,    LEVEL B       */
void Dbacc::execACCSEIZEREQ(Signal* signal) 
{
  jamEntry();
  tuserptr = signal->theData[0];
  /* CONECTION PTR OF LQH            */
  tuserblockref = signal->theData[1];
  /* BLOCK REFERENCE OF LQH          */
  tresult = 0;
  if (cfreeopRec == RNIL) {
    jam();
    refaccConnectLab(signal);
    return;
  }//if
  seizeOpRec(signal);
  ptrGuard(operationRecPtr);
  operationRecPtr.p->userptr = tuserptr;
  operationRecPtr.p->userblockref = tuserblockref;
  operationRecPtr.p->operation = ZUNDEFINED_OP;
  operationRecPtr.p->transactionstate = IDLE;
  /* ******************************< */
  /* ACCSEIZECONF                    */
  /* ******************************< */
  signal->theData[0] = tuserptr;
  signal->theData[1] = operationRecPtr.i;
  sendSignal(tuserblockref, GSN_ACCSEIZECONF, signal, 2, JBB);
  return;
}//Dbacc::execACCSEIZEREQ()

void Dbacc::refaccConnectLab(Signal* signal) 
{
  tresult = ZCONNECT_SIZE_ERROR;
  /* ******************************< */
  /* ACCSEIZEREF                     */
  /* ******************************< */
  signal->theData[0] = tuserptr;
  signal->theData[1] = tresult;
  sendSignal(tuserblockref, GSN_ACCSEIZEREF, signal, 2, JBB);
  return;
}//Dbacc::refaccConnectLab()

/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       END OF CONNECTION MODULE                                                    */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       EXECUTE OPERATION MODULE                                                    */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* INIT_OP_REC                                                                       */
/*           INFORMATION WHICH IS RECIEVED BY ACCKEYREQ WILL BE SAVED                */
/*           IN THE OPERATION RECORD.                                                */
/* --------------------------------------------------------------------------------- */
void Dbacc::initOpRec(Signal* signal) 
{
  register Uint32 Treqinfo;

  Treqinfo = signal->theData[2];

  operationRecPtr.p->hashValue = signal->theData[3];
  operationRecPtr.p->tupkeylen = signal->theData[4];
  operationRecPtr.p->xfrmtupkeylen = signal->theData[4];
  operationRecPtr.p->transId1 = signal->theData[5];
  operationRecPtr.p->transId2 = signal->theData[6];
  operationRecPtr.p->transactionstate = ACTIVE;
  operationRecPtr.p->commitDeleteCheckFlag = ZFALSE;
  operationRecPtr.p->operation = Treqinfo & 0x7;
  /* --------------------------------------------------------------------------------- */
  // opSimple is not used in this version. Is needed for deadlock handling later on.
  /* --------------------------------------------------------------------------------- */
  //  operationRecPtr.p->opSimple = (Treqinfo >> 3) & 0x1; 

  operationRecPtr.p->lockMode = (Treqinfo >> 4) & 0x3;

  Uint32 readFlag = (((Treqinfo >> 4) & 0x3) == 0);      // Only 1 if Read
  Uint32 dirtyFlag = (((Treqinfo >> 6) & 0x1) == 1);     // Only 1 if Dirty
  Uint32 dirtyReadFlag = readFlag & dirtyFlag;
  operationRecPtr.p->dirtyRead = dirtyReadFlag;

  operationRecPtr.p->nodeType = (Treqinfo >> 7) & 0x3;
  operationRecPtr.p->fid = fragrecptr.p->myfid;
  operationRecPtr.p->fragptr = fragrecptr.i;
  operationRecPtr.p->nextParallelQue = RNIL;
  operationRecPtr.p->prevParallelQue = RNIL;
  operationRecPtr.p->prevQueOp = RNIL;
  operationRecPtr.p->nextQueOp = RNIL;
  operationRecPtr.p->nextSerialQue = RNIL;
  operationRecPtr.p->prevSerialQue = RNIL;
  operationRecPtr.p->elementPage = RNIL;
  operationRecPtr.p->keyinfoPage = RNIL;
  operationRecPtr.p->lockOwner = ZFALSE;
  operationRecPtr.p->insertIsDone = ZFALSE;
  operationRecPtr.p->elementIsDisappeared = ZFALSE;
  operationRecPtr.p->insertDeleteLen = fragrecptr.p->elementLength;
  operationRecPtr.p->longPagePtr = RNIL;
  operationRecPtr.p->longKeyPageIndex = RNIL;
  operationRecPtr.p->scanRecPtr = RNIL;

  // bit to mark lock operation
  operationRecPtr.p->isAccLockReq = (Treqinfo >> 31) & 0x1;
  // undo log is not run via ACCKEYREQ

  if(ERROR_INSERTED(5900) || ERROR_INSERTED(5901))
  {
    for(unsigned i = 0; i<8 && i<signal->theData[4]; i++){
      operationRecPtr.p->keydata[i] = signal->theData[i+7];
    }
  }

}//Dbacc::initOpRec()

/* --------------------------------------------------------------------------------- */
/* SEND_ACCKEYCONF                                                                   */
/* --------------------------------------------------------------------------------- */
void Dbacc::sendAcckeyconf(Signal* signal) 
{
  signal->theData[0] = operationRecPtr.p->userptr;
  signal->theData[1] = operationRecPtr.p->operation;
  signal->theData[2] = operationRecPtr.p->fid;
  signal->theData[3] = operationRecPtr.p->localdata[0];
  signal->theData[4] = operationRecPtr.p->localdata[1];
  signal->theData[5] = fragrecptr.p->localkeylen;
}//Dbacc::sendAcckeyconf()


void Dbacc::ACCKEY_error(Uint32 fromWhere)
{
  switch(fromWhere) {
  case 0:
    ndbrequire(false);
  case 1:
    ndbrequire(false);
  case 2:
    ndbrequire(false);
  case 3:
    ndbrequire(false);
  case 4:
    ndbrequire(false);
  case 5:
    ndbrequire(false);
  case 6:
    ndbrequire(false);
  case 7:
    ndbrequire(false);
  case 8:
    ndbrequire(false);
  case 9:
    ndbrequire(false);
  default:
    ndbrequire(false);
  }//switch
}//Dbacc::ACCKEY_error()

/* ******************--------------------------------------------------------------- */
/* ACCKEYREQ                                         REQUEST FOR INSERT, DELETE,     */
/*                                                   RERAD AND UPDATE, A TUPLE.      */
/*                                                   SENDER: LQH,    LEVEL B         */
/*  SIGNAL DATA:      OPERATION_REC_PTR,             CONNECTION PTR                  */
/*                    TABPTR,                        TABLE ID = TABLE RECORD POINTER */
/*                    TREQINFO,                                                      */
/*                    THASHVALUE,                    HASH VALUE OF THE TUP           */
/*                    TKEYLEN,                       LENGTH OF THE PRIMARY KEYS      */
/*                    TKEY1,                         PRIMARY KEY 1                   */
/*                    TKEY2,                         PRIMARY KEY 2                   */
/*                    TKEY3,                         PRIMARY KEY 3                   */
/*                    TKEY4,                         PRIMARY KEY 4                   */
/* ******************--------------------------------------------------------------- */
void Dbacc::execACCKEYREQ(Signal* signal) 
{
  jamEntry();
  operationRecPtr.i = signal->theData[0];   /* CONNECTION PTR */
  fragrecptr.i = signal->theData[1];        /* FRAGMENT RECORD POINTER         */
  if (!((operationRecPtr.i < coprecsize) ||
	(fragrecptr.i < cfragmentsize))) {
    ACCKEY_error(0);
    return;
  }//if
  ptrAss(operationRecPtr, operationrec);
  ptrAss(fragrecptr, fragmentrec);  
  ndbrequire(operationRecPtr.p->transactionstate == IDLE);

  initOpRec(signal);
  // normalize key if any char attr
  if (operationRecPtr.p->tupkeylen && fragrecptr.p->hasCharAttr)
    xfrmKeyData(signal);

  /*---------------------------------------------------------------*/
  /*                                                               */
  /*       WE WILL USE THE HASH VALUE TO LOOK UP THE PROPER MEMORY */
  /*       PAGE AND MEMORY PAGE INDEX TO START THE SEARCH WITHIN.  */
  /*       WE REMEMBER THESE ADDRESS IF WE LATER NEED TO INSERT    */
  /*       THE ITEM AFTER NOT FINDING THE ITEM.                    */
  /*---------------------------------------------------------------*/
  getElement(signal);
  
  if (tgeResult == ZTRUE) {
    switch (operationRecPtr.p->operation) {
    case ZREAD:
    case ZUPDATE:
    case ZDELETE:
    case ZWRITE:
    case ZSCAN_OP:
      if (!tgeLocked){
	if(operationRecPtr.p->operation == ZWRITE)
	{
	  jam();
	  operationRecPtr.p->operation = ZUPDATE;
	}
        sendAcckeyconf(signal);
        if (operationRecPtr.p->dirtyRead == ZFALSE) {
	  /*---------------------------------------------------------------*/
	  // It is not a dirty read. We proceed by locking and continue with
	  // the operation.
	  /*---------------------------------------------------------------*/
          Uint32 eh = gePageptr.p->word32[tgeElementptr];
          operationRecPtr.p->scanBits = ElementHeader::getScanBits(eh);
          operationRecPtr.p->hashvaluePart = ElementHeader::getHashValuePart(eh);
          operationRecPtr.p->elementPage = gePageptr.i;
          operationRecPtr.p->elementContainer = tgeContainerptr;
          operationRecPtr.p->elementPointer = tgeElementptr;
          operationRecPtr.p->elementIsforward = tgeForward;

	  eh = ElementHeader::setLocked(operationRecPtr.i);
          dbgWord32(gePageptr, tgeElementptr, eh);
          gePageptr.p->word32[tgeElementptr] = eh;
	  
	  insertLockOwnersList(signal , operationRecPtr);
          return;
        } else {
          jam();
	  /*---------------------------------------------------------------*/
	  // It is a dirty read. We do not lock anything. Set state to
	  // IDLE since no COMMIT call will come.
	  /*---------------------------------------------------------------*/
          operationRecPtr.p->transactionstate = IDLE;
          operationRecPtr.p->operation = ZUNDEFINED_OP;
          return;
        }//if
      } else {
        jam();
        accIsLockedLab(signal);
        return;
      }//if
      break;
    case ZINSERT:
      jam();
      insertExistElemLab(signal);
      return;
      break;
    default:
      ndbrequire(false);
      break;
    }//switch
  } else if (tgeResult == ZFALSE) {
    switch (operationRecPtr.p->operation) {
    case ZINSERT:
    case ZWRITE:
      jam();
      // If a write operation makes an insert we switch operation to ZINSERT so
      // that the commit-method knows an insert has been made and updates noOfElements.
      operationRecPtr.p->operation = ZINSERT;
      operationRecPtr.p->insertIsDone = ZTRUE;
      insertelementLab(signal);
      return;
      break;
    case ZREAD:
    case ZUPDATE:
    case ZDELETE:
    case ZSCAN_OP:
      jam();
      acckeyref1Lab(signal, ZREAD_ERROR);
      return;
      break;
    default:
      ndbrequire(false);
      break;
    }//switch
  } else {
    jam();
    acckeyref1Lab(signal, tgeResult);
    return;
  }//if
  return;
}//Dbacc::execACCKEYREQ()

void
Dbacc::xfrmKeyData(Signal* signal)
{
  Uint32 table = fragrecptr.p->myTableId;
  Uint32 dst[MAX_KEY_SIZE_IN_WORDS * MAX_XFRM_MULTIPLY];
  Uint32 keyPartLen[MAX_ATTRIBUTES_IN_INDEX];
  Uint32* src = &signal->theData[7];
  Uint32 len = xfrm_key(table, src, dst, sizeof(dst) >> 2, keyPartLen);
  ndbrequire(len); // 0 means error
  memcpy(src, dst, len << 2);
  operationRecPtr.p->xfrmtupkeylen = len;
}

void Dbacc::accIsLockedLab(Signal* signal) 
{
  ndbrequire(csystemRestart == ZFALSE);
  queOperPtr.i = ElementHeader::getOpPtrI(gePageptr.p->word32[tgeElementptr]);
  ptrCheckGuard(queOperPtr, coprecsize, operationrec);
  if (operationRecPtr.p->dirtyRead == ZFALSE) {
    Uint32 return_result;
    if (operationRecPtr.p->lockMode == ZREADLOCK) {
      jam();
      priPageptr = gePageptr;
      tpriElementptr = tgeElementptr;
      return_result = placeReadInLockQueue(signal);
    } else {
      jam();
      pwiPageptr = gePageptr;
      tpwiElementptr = tgeElementptr;
      return_result = placeWriteInLockQueue(signal);
    }//if
    if (return_result == ZPARALLEL_QUEUE) {
      jam();
      sendAcckeyconf(signal);
      return;
    } else if (return_result == ZSERIAL_QUEUE) {
      jam();
      signal->theData[0] = RNIL;
      return;
    } else if (return_result == ZWRITE_ERROR) {
      jam();
      acckeyref1Lab(signal, return_result);
      return;
    }//if
    ndbrequire(false);
  } else {
    if (queOperPtr.p->elementIsDisappeared == ZFALSE) {
      jam();
      /*---------------------------------------------------------------*/
      // It is a dirty read. We do not lock anything. Set state to
      // IDLE since no COMMIT call will arrive.
      /*---------------------------------------------------------------*/
      sendAcckeyconf(signal);
      operationRecPtr.p->transactionstate = IDLE;
      operationRecPtr.p->operation = ZUNDEFINED_OP;
      return;
    } else {
      jam();
      /*---------------------------------------------------------------*/
      // The tuple does not exist in the committed world currently.
      // Report read error.
      /*---------------------------------------------------------------*/
      acckeyref1Lab(signal, ZREAD_ERROR);
      return;
    }//if
  }//if
}//Dbacc::accIsLockedLab()

/* --------------------------------------------------------------------------------- */
/*        I N S E R T      E X I S T      E L E M E N T                              */
/* --------------------------------------------------------------------------------- */
void Dbacc::insertExistElemLab(Signal* signal) 
{
  if (!tgeLocked){
    jam();
    acckeyref1Lab(signal, ZWRITE_ERROR);/* THE ELEMENT ALREADY EXIST */
    return;
  }//if
  accIsLockedLab(signal);
}//Dbacc::insertExistElemLab()

/* --------------------------------------------------------------------------------- */
/* INSERTELEMENT                                                                     */
/* --------------------------------------------------------------------------------- */
void Dbacc::insertelementLab(Signal* signal) 
{
  if (fragrecptr.p->firstOverflowRec == RNIL) {
    jam();
    allocOverflowPage(signal);
    if (tresult > ZLIMIT_OF_ERROR) {
      jam();
      acckeyref1Lab(signal, tresult);
      return;
    }//if
  }//if
  ndbrequire(operationRecPtr.p->tupkeylen <= fragrecptr.p->keyLength);

  Uint32 localKey;
  if(!operationRecPtr.p->isAccLockReq)
  {
    signal->theData[0] = operationRecPtr.p->userptr;
    Uint32 blockNo = refToBlock(operationRecPtr.p->userblockref);
    EXECUTE_DIRECT(blockNo, GSN_LQH_ALLOCREQ, signal, 1);
    jamEntry();
    if (signal->theData[0] != 0) {
      jam();
      Uint32 result_code = signal->theData[0];
      acckeyref1Lab(signal, result_code);
      return;
    }//if
    localKey = (signal->theData[1] << MAX_TUPLES_BITS) + signal->theData[2];
  }
  else
  {
    localKey = signal->theData[7];
  }
  
  insertLockOwnersList(signal, operationRecPtr);

  const Uint32 tmp = fragrecptr.p->k + fragrecptr.p->lhfragbits;
  operationRecPtr.p->hashvaluePart = 
    (operationRecPtr.p->hashValue >> tmp) & 0xFFFF;
  operationRecPtr.p->scanBits = 0;	/* NOT ANY ACTIVE SCAN */
  tidrElemhead = ElementHeader::setLocked(operationRecPtr.i);
  idrPageptr = gdiPageptr;
  tidrPageindex = tgdiPageindex;
  tidrForward = ZTRUE;
  idrOperationRecPtr = operationRecPtr;
  clocalkey[0] = localKey;
  operationRecPtr.p->localdata[0] = localKey;
  /* --------------------------------------------------------------------------------- */
  /*       WE SET THE LOCAL KEY TO MINUS ONE TO INDICATE IT IS NOT YET VALID.          */
  /* --------------------------------------------------------------------------------- */
  insertElement(signal);
  sendAcckeyconf(signal);
  return;
}//Dbacc::insertelementLab()

/* --------------------------------------------------------------------------------- */
/* PLACE_READ_IN_LOCK_QUEUE                                                          */
/*	INPUT:		OPERATION_REC_PTR	OUR OPERATION POINTER		    */
/*			QUE_OPER_PTR		LOCK QUEUE OWNER OPERATION POINTER  */
/*			PRI_PAGEPTR             PAGE POINTER OF ELEMENT             */
/*			TPRI_ELEMENTPTR         ELEMENT POINTER OF ELEMENT          */
/*	OUTPUT		TRESULT =                                                   */
/*			ZPARALLEL_QUEUE		OPERATION PLACED IN PARALLEL QUEUE  */
/*						OPERATION CAN PROCEED NOW.          */
/*			ZSERIAL_QUEUE		OPERATION PLACED IN SERIAL QUEUE    */
/*			ERROR CODE		OPERATION NEEDS ABORTING            */
/* THE ELEMENT WAS LOCKED AND WE WANT TO READ THE TUPLE. WE WILL CHECK THE LOCK      */
/* QUEUES TO PERFORM THE PROPER ACTION.                                              */
/*                                                                                   */
/* IN SOME PLACES IN THE CODE BELOW THAT HANDLES WHAT TO DO WHEN THE TUPLE IS LOCKED */
/* WE DO ASSUME THAT NEXT_PARALLEL_QUEUE AND NEXT_SERIAL_QUEUE ON OPERATION_REC_PTR  */
/* HAVE BEEN INITIALISED TO RNIL. THUS WE DO NOT PERFORM THIS ONCE MORE EVEN IF IT   */
/* COULD BE NICE FOR READABILITY.                                                    */
/* --------------------------------------------------------------------------------- */
Uint32 Dbacc::placeReadInLockQueue(Signal* signal) 
{
  if (getNoParallelTransaction(queOperPtr.p) == 1) {
    if ((queOperPtr.p->transId1 == operationRecPtr.p->transId1) && 
        (queOperPtr.p->transId2 == operationRecPtr.p->transId2)) {
      /* --------------------------------------------------------------------------------- */
      /* WE ARE PERFORMING A READ OPERATION AND THIS TRANSACTION ALREADY OWNS THE LOCK     */
      /* ALONE. PUT THE OPERATION LAST IN THE PARALLEL QUEUE.                              */
      /* --------------------------------------------------------------------------------- */
      jam();
      mlpqOperPtr = queOperPtr;
      moveLastParallelQueue(signal);
      operationRecPtr.p->localdata[0] = queOperPtr.p->localdata[0];
      operationRecPtr.p->localdata[1] = queOperPtr.p->localdata[1];
      operationRecPtr.p->prevParallelQue = mlpqOperPtr.i;
      mlpqOperPtr.p->nextParallelQue = operationRecPtr.i;
      switch (queOperPtr.p->lockMode) {
      case ZREADLOCK:
        jam();
        /*empty*/;
        break;
      default:
        jam();
        /* --------------------------------------------------------------------------------- */
        /*       IF THE TRANSACTION PREVIOUSLY SET A WRITE LOCK WE MUST ENSURE THAT ALL      */
        /*       OPERATIONS IN THE PARALLEL QUEUE HAVE WRITE LOCK MODE TO AVOID STRANGE BUGS.*/
        /* --------------------------------------------------------------------------------- */
        operationRecPtr.p->lockMode = queOperPtr.p->lockMode;
        break;
      }//switch
      return ZPARALLEL_QUEUE;
    }//if
  }//if
  if (queOperPtr.p->nextSerialQue == RNIL) {
    /* --------------------------------------------------------------------------------- */
    /* WE ARE PERFORMING A READ OPERATION AND THERE IS NO SERIAL QUEUE. IF THERE IS NO   */
    /* WRITE OPERATION THAT OWNS THE LOCK OR ANY WRITE OPERATION IN THE PARALLEL QUEUE   */
    /* IT IS ENOUGH TO CHECK THE LOCK MODE OF THE LEADER IN THE PARALLEL QUEUE. IF IT IS */
    /* A READ LOCK THEN WE PLACE OURSELVES IN THE PARALLEL QUEUE OTHERWISE WE GO ON TO   */
    /* PLACE OURSELVES IN THE SERIAL QUEUE.                                              */
    /* --------------------------------------------------------------------------------- */
    switch (queOperPtr.p->lockMode) {
    case ZREADLOCK:
      jam();
      mlpqOperPtr = queOperPtr;
      moveLastParallelQueue(signal);
      operationRecPtr.p->prevParallelQue = mlpqOperPtr.i;
      mlpqOperPtr.p->nextParallelQue = operationRecPtr.i;
      operationRecPtr.p->localdata[0] = queOperPtr.p->localdata[0];
      operationRecPtr.p->localdata[1] = queOperPtr.p->localdata[1];
      return ZPARALLEL_QUEUE;
    default:
      jam();
      queOperPtr.p->nextSerialQue = operationRecPtr.i;
      operationRecPtr.p->prevSerialQue = queOperPtr.i;
      break;
    }//switch
  } else {
    jam();
    placeSerialQueueRead(signal);
  }//if
  return ZSERIAL_QUEUE;
}//Dbacc::placeReadInLockQueue()

/* --------------------------------------------------------------------------------- */
/* WE WILL CHECK IF THIS TRANSACTION IS ALREADY PLACED AT SOME SPOT IN THE PARALLEL  */
/* SERIAL QUEUE WITHOUT ANY NEIGHBORS FROM OTHER TRANSACTION. IF SO WE WILL INSERT   */
/* IT IN THAT PARALLEL QUEUE.                                                        */
/* --------------------------------------------------------------------------------- */
void Dbacc::placeSerialQueueRead(Signal* signal) 
{
  readWriteOpPtr.i = queOperPtr.p->nextSerialQue;
  ptrCheckGuard(readWriteOpPtr, coprecsize, operationrec);
 PSQR_LOOP:
  jam();
  if (readWriteOpPtr.p->nextSerialQue == RNIL) {
    jam();
    /* --------------------------------------------------------------------------------- */
    /*       THERE WAS NO PREVIOUS OPERATION IN THIS TRANSACTION WHICH WE COULD PUT IT   */
    /*       IN THE PARALLEL QUEUE TOGETHER WITH.                                        */
    /* --------------------------------------------------------------------------------- */
    checkOnlyReadEntry(signal);
    return;
  }//if
  if (getNoParallelTransaction(readWriteOpPtr.p) == 1) {
    jam();
    /* --------------------------------------------------------------------------------- */
    /* THERE WAS ONLY ONE TRANSACTION INVOLVED IN THE PARALLEL QUEUE. IF THIS IS OUR     */
    /* TRANSACTION WE CAN STILL GET HOLD OF THE LOCK.                                    */
    /* --------------------------------------------------------------------------------- */
    if ((readWriteOpPtr.p->transId1 == operationRecPtr.p->transId1) && 
        (readWriteOpPtr.p->transId2 == operationRecPtr.p->transId2)) {
      jam();
      /* --------------------------------------------------------------------------------- */
      /* WE ARE PERFORMING A READ IN THE SAME TRANSACTION WHERE WE ALREADY                 */
      /* PREVIOUSLY HAVE EXECUTED AN OPERATION. INSERT-DELETE, READ-UPDATE, READ-READ,     */
      /* UPDATE-UPDATE, UPDATE-DELETE, READ-DELETE, INSERT-READ, INSERT-UPDATE ARE ALLOWED */
      /* COMBINATIONS. A NEW INSERT AFTER A DELETE IS NOT ALLOWED AND SUCH AN INSERT WILL  */
      /* GO TO THE SERIAL LOCK QUEUE WHICH IT WILL NOT LEAVE UNTIL A TIME-OUT AND THE      */
      /* TRANSACTION IS ABORTED. READS AND UPDATES AFTER DELETES IS ALSO NOT ALLOWED.      */
      /* --------------------------------------------------------------------------------- */
      mlpqOperPtr = readWriteOpPtr;
      moveLastParallelQueue(signal);
      readWriteOpPtr = mlpqOperPtr;
      operationRecPtr.p->prevParallelQue = readWriteOpPtr.i;
      readWriteOpPtr.p->nextParallelQue = operationRecPtr.i;
      operationRecPtr.p->localdata[0] = readWriteOpPtr.p->localdata[0];
      operationRecPtr.p->localdata[1] = readWriteOpPtr.p->localdata[1];
      switch (readWriteOpPtr.p->lockMode) {
      case ZREADLOCK:
        jam();
        /*empty*/;
        break;
      default:
        jam();
        /* --------------------------------------------------------------------------------- */
        /*       IF THE TRANSACTION PREVIOUSLY SET A WRITE LOCK WE MUST ENSURE THAT ALL      */
        /*       OPERATIONS IN THE PARALLEL QUEUE HAVE WRITE LOCK MODE TO AVOID STRANGE BUGS.*/
        /* --------------------------------------------------------------------------------- */
        operationRecPtr.p->lockMode = readWriteOpPtr.p->lockMode;
        break;
      }//switch
      return;
    }//if
  }//if
  readWriteOpPtr.i = readWriteOpPtr.p->nextSerialQue;
  ptrCheckGuard(readWriteOpPtr, coprecsize, operationrec);
  goto PSQR_LOOP;
}//Dbacc::placeSerialQueueRead()

/* --------------------------------------------------------------------------------- */
/*       WE WILL CHECK IF THE LAST ENTRY IN THE SERIAL QUEUE CONTAINS ONLY READ      */
/*       OPERATIONS. IF SO WE WILL INSERT IT IN THAT PARALLEL QUEUE. OTHERWISE WE    */
/*       WILL PLACE IT AT THE END OF THE SERIAL QUEUE.                               */
/* --------------------------------------------------------------------------------- */
void Dbacc::checkOnlyReadEntry(Signal* signal) 
{
  switch (readWriteOpPtr.p->lockMode) {
  case ZREADLOCK:
    jam();
    /* --------------------------------------------------------------------------------- */
    /* SINCE THIS LAST QUEUE ONLY CONTAINS READ LOCKS WE CAN JOIN THE PARALLEL QUEUE AT  */
    /* THE END.                                                                          */
    /* --------------------------------------------------------------------------------- */
    mlpqOperPtr = readWriteOpPtr;
    moveLastParallelQueue(signal);
    readWriteOpPtr = mlpqOperPtr;
    operationRecPtr.p->prevParallelQue = readWriteOpPtr.i;
    readWriteOpPtr.p->nextParallelQue = operationRecPtr.i;
    operationRecPtr.p->localdata[0] = readWriteOpPtr.p->localdata[0];
    operationRecPtr.p->localdata[1] = readWriteOpPtr.p->localdata[1];
    break;
  default:
    jam();                             /* PUT THE OPERATION RECORD IN THE SERIAL QUEUE */
    readWriteOpPtr.p->nextSerialQue = operationRecPtr.i;
    operationRecPtr.p->prevSerialQue = readWriteOpPtr.i;
    break;
  }//switch
}//Dbacc::checkOnlyReadEntry()

/* --------------------------------------------------------------------------------- */
/* GET_NO_PARALLEL_TRANSACTION                                                       */
/* --------------------------------------------------------------------------------- */
Uint32
Dbacc::getNoParallelTransaction(const Operationrec * op) 
{
  OperationrecPtr tmp;
  
  tmp.i= op->nextParallelQue;
  Uint32 transId[2] = { op->transId1, op->transId2 };
  while (tmp.i != RNIL) 
  {
    jam();
    ptrCheckGuard(tmp, coprecsize, operationrec);
    if (tmp.p->transId1 == transId[0] && tmp.p->transId2 == transId[1])
      tmp.i = tmp.p->nextParallelQue;
    else
      return 2;
  }
  return 1;
}//Dbacc::getNoParallelTransaction()

void Dbacc::moveLastParallelQueue(Signal* signal) 
{
  while (mlpqOperPtr.p->nextParallelQue != RNIL) {
    jam();
    mlpqOperPtr.i = mlpqOperPtr.p->nextParallelQue;
    ptrCheckGuard(mlpqOperPtr, coprecsize, operationrec);
  }//if
}//Dbacc::moveLastParallelQueue()

void Dbacc::moveLastParallelQueueWrite(Signal* signal) 
{
  /* --------------------------------------------------------------------------------- */
  /*       ENSURE THAT ALL OPERATIONS HAVE LOCK MODE SET TO WRITE SINCE WE INSERT A    */
  /*       WRITE LOCK INTO THE PARALLEL QUEUE.                                         */
  /* --------------------------------------------------------------------------------- */
  while (mlpqOperPtr.p->nextParallelQue != RNIL) {
    jam();
    mlpqOperPtr.p->lockMode = operationRecPtr.p->lockMode;
    mlpqOperPtr.i = mlpqOperPtr.p->nextParallelQue;
    ptrCheckGuard(mlpqOperPtr, coprecsize, operationrec);
  }//if
  mlpqOperPtr.p->lockMode = operationRecPtr.p->lockMode;
}//Dbacc::moveLastParallelQueueWrite()

/* --------------------------------------------------------------------------------- */
/* PLACE_WRITE_IN_LOCK_QUEUE                                                         */
/*	INPUT:		OPERATION_REC_PTR	OUR OPERATION POINTER		    */
/*			QUE_OPER_PTR		LOCK QUEUE OWNER OPERATION POINTER  */
/*			PWI_PAGEPTR             PAGE POINTER OF ELEMENT             */
/*			TPWI_ELEMENTPTR         ELEMENT POINTER OF ELEMENT          */
/*	OUTPUT		TRESULT =                                                   */
/*			ZPARALLEL_QUEUE		OPERATION PLACED IN PARALLEL QUEUE  */
/*						OPERATION CAN PROCEED NOW.          */
/*			ZSERIAL_QUEUE		OPERATION PLACED IN SERIAL QUEUE    */
/*			ERROR CODE		OPERATION NEEDS ABORTING            */
/* --------------------------------------------------------------------------------- */
Uint32 Dbacc::placeWriteInLockQueue(Signal* signal) 
{
  if (!((getNoParallelTransaction(queOperPtr.p) == 1) &&
	(queOperPtr.p->transId1 == operationRecPtr.p->transId1) &&
	(queOperPtr.p->transId2 == operationRecPtr.p->transId2))) {
    jam();
    placeSerialQueueWrite(signal);
    return ZSERIAL_QUEUE;
  }//if
  
  /* 
    WE ARE PERFORMING AN READ EXCLUSIVE, INSERT, UPDATE OR DELETE IN THE SAME
    TRANSACTION WHERE WE PREVIOUSLY HAVE EXECUTED AN OPERATION.
    Read-All, Update-All, Insert-All and Delete-Insert are allowed
    combinations.
    Delete-Read, Delete-Update and Delete-Delete are not an allowed
    combination and will result in tuple not found error.
  */
  mlpqOperPtr = queOperPtr;
  moveLastParallelQueueWrite(signal);

  if (operationRecPtr.p->operation == ZINSERT && 
      mlpqOperPtr.p->operation != ZDELETE){
    jam();
    return ZWRITE_ERROR;
  }//if

  if(operationRecPtr.p->operation == ZWRITE)
  {
    operationRecPtr.p->operation = 
      (mlpqOperPtr.p->operation == ZDELETE) ? ZINSERT : ZUPDATE;
  }
  
  operationRecPtr.p->localdata[0] = queOperPtr.p->localdata[0];
  operationRecPtr.p->localdata[1] = queOperPtr.p->localdata[1];
  operationRecPtr.p->prevParallelQue = mlpqOperPtr.i;
  mlpqOperPtr.p->nextParallelQue = operationRecPtr.i;
  return ZPARALLEL_QUEUE;
}//Dbacc::placeWriteInLockQueue()

/* --------------------------------------------------------------------------------- */
/*       WE HAVE TO PLACE IT SOMEWHERE IN THE SERIAL QUEUE INSTEAD.                  */
/* --------------------------------------------------------------------------------- */
void Dbacc::placeSerialQueueWrite(Signal* signal) 
{
  readWriteOpPtr = queOperPtr;
 PSQW_LOOP:
  if (readWriteOpPtr.p->nextSerialQue == RNIL) {
    jam();
    /* --------------------------------------------------------------------------------- */
    /* WE COULD NOT PUT IN ANY PARALLEL QUEUE. WE MUST PUT IT LAST IN THE SERIAL QUEUE.  */
    /* --------------------------------------------------------------------------------- */
    readWriteOpPtr.p->nextSerialQue = operationRecPtr.i;
    operationRecPtr.p->prevSerialQue = readWriteOpPtr.i;
    return;
  }//if
  readWriteOpPtr.i = readWriteOpPtr.p->nextSerialQue;
  ptrCheckGuard(readWriteOpPtr, coprecsize, operationrec);
  if (getNoParallelTransaction(readWriteOpPtr.p) == 1) {
    /* --------------------------------------------------------------------------------- */
    /* THERE WAS ONLY ONE TRANSACTION INVOLVED IN THE PARALLEL QUEUE. IF THIS IS OUR     */
    /* TRANSACTION WE CAN STILL GET HOLD OF THE LOCK.                                    */
    /* --------------------------------------------------------------------------------- */
    if ((readWriteOpPtr.p->transId1 == operationRecPtr.p->transId1) && 
        (readWriteOpPtr.p->transId2 == operationRecPtr.p->transId2)) {
      jam();
      /* --------------------------------------------------------------------------------- */
      /* WE ARE PERFORMING AN UPDATE OR DELETE IN THE SAME TRANSACTION WHERE WE ALREADY    */
      /* PREVIOUSLY HAVE EXECUTED AN OPERATION. INSERT-DELETE, READ-UPDATE, READ-READ,     */
      /* UPDATE-UPDATE, UPDATE-DELETE, READ-DELETE, INSERT-READ, INSERT-UPDATE ARE ALLOWED */
      /* COMBINATIONS. A NEW INSERT AFTER A DELETE IS NOT ALLOWED AND SUCH AN INSERT WILL  */
      /* GO TO THE SERIAL LOCK QUEUE WHICH IT WILL NOT LEAVE UNTIL A TIME-OUT AND THE      */
      /* TRANSACTION IS ABORTED. READS AND UPDATES AFTER DELETES IS ALSO NOT ALLOWED.      */
      /* --------------------------------------------------------------------------------- */
      mlpqOperPtr = readWriteOpPtr;
      moveLastParallelQueueWrite(signal);
      readWriteOpPtr = mlpqOperPtr;
      operationRecPtr.p->prevParallelQue = readWriteOpPtr.i;
      readWriteOpPtr.p->nextParallelQue = operationRecPtr.i;
      operationRecPtr.p->localdata[0] = readWriteOpPtr.p->localdata[0];
      operationRecPtr.p->localdata[1] = readWriteOpPtr.p->localdata[1];
      return;
    }//if
  }//if
  goto PSQW_LOOP;
}//Dbacc::placeSerialQueueWrite()

/* ------------------------------------------------------------------------- */
/* ACC KEYREQ END                                                            */
/* ------------------------------------------------------------------------- */
void Dbacc::acckeyref1Lab(Signal* signal, Uint32 result_code) 
{
  if (operationRecPtr.p->keyinfoPage != RNIL) {
    jam();
    rpPageptr.i = operationRecPtr.p->keyinfoPage;
    ptrCheckGuard(rpPageptr, cpagesize, page8);
    releasePage(signal);
    operationRecPtr.p->keyinfoPage = RNIL;
  }//if
  operationRecPtr.p->transactionstate = WAIT_COMMIT_ABORT;
  /* ************************<< */
  /* ACCKEYREF                  */
  /* ************************<< */
  signal->theData[0] = cminusOne;
  signal->theData[1] = result_code;
  return;
}//Dbacc::acckeyref1Lab()

/* ******************--------------------------------------------------------------- */
/* ACCMINUPDATE                                      UPDATE LOCAL KEY REQ            */
/*  DESCRIPTION: UPDATES LOCAL KEY OF AN ELEMENTS IN THE HASH TABLE                  */
/*               THIS SIGNAL IS WAITED AFTER ANY INSERT REQ                          */
/*          ENTER ACCMINUPDATE WITH                    SENDER: LQH,    LEVEL B       */
/*                    OPERATION_REC_PTR,               OPERATION RECORD PTR          */
/*                    CLOCALKEY(0),                    LOCAL KEY 1                   */
/*                    CLOCALKEY(1)                     LOCAL KEY 2                   */
/* ******************--------------------------------------------------------------- */
void Dbacc::execACCMINUPDATE(Signal* signal) 
{
  Page8Ptr ulkPageidptr;
  Uint32 tulkLocalPtr;
  Uint32 tlocalkey1, tlocalkey2;
  Uint32 TlogStart;

  jamEntry();
  operationRecPtr.i = signal->theData[0];
  tlocalkey1 = signal->theData[1];
  tlocalkey2 = signal->theData[2];
  ptrCheckGuard(operationRecPtr, coprecsize, operationrec);
  if (operationRecPtr.p->transactionstate == ACTIVE) {
    fragrecptr.i = operationRecPtr.p->fragptr;
    ulkPageidptr.i = operationRecPtr.p->elementPage;
    tulkLocalPtr = operationRecPtr.p->elementPointer + operationRecPtr.p->elementIsforward;
    ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
    ptrCheckGuard(ulkPageidptr, cpagesize, page8);
    dbgWord32(ulkPageidptr, tulkLocalPtr, tlocalkey1);
    arrGuard(tulkLocalPtr, 2048);
    ulkPageidptr.p->word32[tulkLocalPtr] = tlocalkey1;
    operationRecPtr.p->localdata[0] = tlocalkey1;
    if (fragrecptr.p->localkeylen == 1) {
      return;
    } else if (fragrecptr.p->localkeylen == 2) {
      jam();
      tulkLocalPtr = tulkLocalPtr + operationRecPtr.p->elementIsforward;
      operationRecPtr.p->localdata[1] = tlocalkey2;
      dbgWord32(ulkPageidptr, tulkLocalPtr, tlocalkey2);
      arrGuard(tulkLocalPtr, 2048);
      ulkPageidptr.p->word32[tulkLocalPtr] = tlocalkey2;
      return;
    } else {
      jam();
    }//if
  }//if
  ndbrequire(false);
}//Dbacc::execACCMINUPDATE()

/* ******************--------------------------------------------------------------- */
/* ACC_COMMITREQ                                        COMMIT  TRANSACTION          */
/*                                                     SENDER: LQH,    LEVEL B       */
/*       INPUT:  OPERATION_REC_PTR ,                                                 */
/* ******************--------------------------------------------------------------- */
void Dbacc::execACC_COMMITREQ(Signal* signal) 
{
  Uint8 Toperation;
  jamEntry();
  operationRecPtr.i = signal->theData[0];
  ptrCheckGuard(operationRecPtr, coprecsize, operationrec);
  ndbrequire(operationRecPtr.p->transactionstate == ACTIVE);
  fragrecptr.i = operationRecPtr.p->fragptr;
  ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
  commitOperation(signal);
  Toperation = operationRecPtr.p->operation;
  operationRecPtr.p->transactionstate = IDLE;
  operationRecPtr.p->operation = ZUNDEFINED_OP;
  if(Toperation != ZREAD){
    fragrecptr.p->m_commit_count++;
    if (Toperation != ZINSERT) {
      if (Toperation != ZDELETE) {
	return;
      } else {
	jam();
	fragrecptr.p->noOfElements--;
	fragrecptr.p->slack += operationRecPtr.p->insertDeleteLen;
	if (fragrecptr.p->slack > fragrecptr.p->slackCheck) { 
          /* TIME FOR JOIN BUCKETS PROCESS */
	  if (fragrecptr.p->expandCounter > 0) {
	    if (fragrecptr.p->expandFlag < 2) {
	      jam();
	      signal->theData[0] = fragrecptr.i;
	      signal->theData[1] = fragrecptr.p->p;
	      signal->theData[2] = fragrecptr.p->maxp;
	      signal->theData[3] = fragrecptr.p->expandFlag;
	      fragrecptr.p->expandFlag = 2;
	      sendSignal(cownBlockref, GSN_SHRINKCHECK2, signal, 4, JBB);
	    }//if
	  }//if
	}//if
      }//if
    } else {
      jam();                                                /* EXPAND PROCESS HANDLING */
      fragrecptr.p->noOfElements++;
      fragrecptr.p->slack -= operationRecPtr.p->insertDeleteLen;
      if (fragrecptr.p->slack >= (1u << 31)) { 
	/* IT MEANS THAT IF SLACK < ZERO */
	if (fragrecptr.p->expandFlag == 0) {
	  jam();
	  fragrecptr.p->expandFlag = 2;
	  signal->theData[0] = fragrecptr.i;
	  signal->theData[1] = fragrecptr.p->p;
	  signal->theData[2] = fragrecptr.p->maxp;
	  sendSignal(cownBlockref, GSN_EXPANDCHECK2, signal, 3, JBB);
	}//if
      }//if
    }
  }
  return;
}//Dbacc::execACC_COMMITREQ()

/* ******************--------------------------------------------------------------- */
/* ACC ABORT REQ                           ABORT ALL OPERATION OF THE TRANSACTION    */
/* ******************------------------------------+                                 */
/*   SENDER: LQH,    LEVEL B       */
/* ******************--------------------------------------------------------------- */
/* ACC ABORT REQ                                                 ABORT TRANSACTION   */
/* ******************------------------------------+                                 */
/*   SENDER: LQH,    LEVEL B       */
void Dbacc::execACC_ABORTREQ(Signal* signal) 
{
  jamEntry();
  accAbortReqLab(signal);
}//Dbacc::execACC_ABORTREQ()

void Dbacc::accAbortReqLab(Signal* signal)
{
  operationRecPtr.i = signal->theData[0];
  bool sendConf = signal->theData[1];
  ptrCheckGuard(operationRecPtr, coprecsize, operationrec);
  tresult = 0;	/*                ZFALSE           */
  if ((operationRecPtr.p->transactionstate == ACTIVE) ||
      (operationRecPtr.p->transactionstate == WAIT_COMMIT_ABORT)) {
    jam();
    fragrecptr.i = operationRecPtr.p->fragptr;
    ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
    operationRecPtr.p->transactionstate = ABORT;
    abortOperation(signal);
  } else {
    ndbrequire(operationRecPtr.p->transactionstate == IDLE);
    jam();
  }//if
  operationRecPtr.p->transactionstate = IDLE;
  operationRecPtr.p->operation = ZUNDEFINED_OP;
  if (! sendConf)
    return;
  signal->theData[0] = operationRecPtr.p->userptr;
  sendSignal(operationRecPtr.p->userblockref, GSN_ACC_ABORTCONF, signal, 1, JBB);
  return;
}//Dbacc::accAbortReqLab()

/*
 * Lock or unlock tuple.
 */
void Dbacc::execACC_LOCKREQ(Signal* signal)
{
  jamEntry();
  AccLockReq* sig = (AccLockReq*)signal->getDataPtrSend();
  AccLockReq reqCopy = *sig;
  AccLockReq* const req = &reqCopy;
  Uint32 lockOp = (req->requestInfo & 0xFF);
  if (lockOp == AccLockReq::LockShared ||
      lockOp == AccLockReq::LockExclusive) {
    jam();
    // find table
    tabptr.i = req->tableId;
    ptrCheckGuard(tabptr, ctablesize, tabrec);
    // find fragment (TUX will know it)
    if (req->fragPtrI == RNIL) {
      for (Uint32 i = 0; i < MAX_FRAG_PER_NODE; i++) {
        jam();
        if (tabptr.p->fragholder[i] == req->fragId){
	  jam();
	  req->fragPtrI = tabptr.p->fragptrholder[i];
	  break;
	}
      }
    }
    fragrecptr.i = req->fragPtrI;
    ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
    ndbrequire(req->fragId == fragrecptr.p->myfid);
    // caller must be explicit here
    ndbrequire(req->accOpPtr == RNIL);
    // seize operation to hold the lock
    if (cfreeopRec != RNIL) {
      jam();
      seizeOpRec(signal);
      // init as in ACCSEIZEREQ
      operationRecPtr.p->userptr = req->userPtr;
      operationRecPtr.p->userblockref = req->userRef;
      operationRecPtr.p->operation = ZUNDEFINED_OP;
      operationRecPtr.p->transactionstate = IDLE;
      operationRecPtr.p->scanRecPtr = RNIL;
      // do read with lock via ACCKEYREQ
      Uint32 lockMode = (lockOp == AccLockReq::LockShared) ? 0 : 1;
      Uint32 opCode = ZSCAN_OP;
      signal->theData[0] = operationRecPtr.i;
      signal->theData[1] = fragrecptr.i;
      signal->theData[2] = opCode | (lockMode << 4) | (1u << 31);
      signal->theData[3] = req->hashValue;
      signal->theData[4] = 0;   // search local key
      signal->theData[5] = req->transId1;
      signal->theData[6] = req->transId2;
      // enter local key in place of PK
      signal->theData[7] = req->tupAddr;
      EXECUTE_DIRECT(DBACC, GSN_ACCKEYREQ, signal, 8);
      // translate the result
      if (signal->theData[0] < RNIL) {
        jam();
        req->returnCode = AccLockReq::Success;
        req->accOpPtr = operationRecPtr.i;
      } else if (signal->theData[0] == RNIL) {
        jam();
        req->returnCode = AccLockReq::IsBlocked;
        req->accOpPtr = operationRecPtr.i;
      } else {
        ndbrequire(signal->theData[0] == (UintR)-1);
        releaseOpRec(signal);
        req->returnCode = AccLockReq::Refused;
        req->accOpPtr = RNIL;
      }
    } else {
      jam();
      req->returnCode = AccLockReq::NoFreeOp;
    }
    *sig = *req;
    return;
  }
  if (lockOp == AccLockReq::Unlock) {
    jam();
    // do unlock via ACC_COMMITREQ (immediate)
    signal->theData[0] = req->accOpPtr;
    EXECUTE_DIRECT(DBACC, GSN_ACC_COMMITREQ, signal, 1);
    releaseOpRec(signal);
    req->returnCode = AccLockReq::Success;
    *sig = *req;
    return;
  }
  if (lockOp == AccLockReq::Abort) {
    jam();
    // do abort via ACC_ABORTREQ (immediate)
    signal->theData[0] = req->accOpPtr;
    signal->theData[1] = false; // Dont send abort
    accAbortReqLab(signal);
    releaseOpRec(signal);
    req->returnCode = AccLockReq::Success;
    *sig = *req;
    return;
  }
  if (lockOp == AccLockReq::AbortWithConf) {
    jam();
    // do abort via ACC_ABORTREQ (with conf signal)
    signal->theData[0] = req->accOpPtr;
    signal->theData[1] = true; // send abort
    accAbortReqLab(signal);
    releaseOpRec(signal);
    req->returnCode = AccLockReq::Success;
    *sig = *req;
    return;
  }
  ndbrequire(false);
}

/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       END OF EXECUTE OPERATION MODULE                                             */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       MODULE:         INSERT                                                      */
/*               THE FOLLOWING SUBROUTINES ARE ONLY USED BY INSERT_ELEMENT. THIS     */
/*               ROUTINE IS THE SOLE INTERFACE TO INSERT ELEMENTS INTO THE INDEX.    */
/*               CURRENT USERS ARE INSERT REQUESTS, EXPAND CONTAINER AND SHRINK      */
/*               CONTAINER.                                                          */
/*                                                                                   */
/*               THE FOLLOWING SUBROUTINES ARE INCLUDED IN THIS MODULE:              */
/*               INSERT_ELEMENT                                                      */
/*               INSERT_CONTAINER                                                    */
/*               ADDNEWCONTAINER                                                     */
/*               GETFREELIST                                                         */
/*               INCREASELISTCONT                                                    */
/*               SEIZE_LEFTLIST                                                      */
/*               SEIZE_RIGHTLIST                                                     */
/*                                                                                   */
/*               THESE ROUTINES ARE ONLY USED BY THIS MODULE AND BY NO ONE ELSE.     */
/*               ALSO THE ROUTINES MAKE NO USE OF ROUTINES IN OTHER MODULES.         */
/*               TAKE_REC_OUT_OF_FREE_OVERPAGE AND RELEASE_OVERFLOW_REC ARE          */
/*               EXCEPTIONS TO THIS RULE.                                            */
/*                                                                                   */
/*               THE ONLY SHORT-LIVED VARIABLES USED IN OTHER PARTS OF THE BLOCK ARE */
/*               THOSE DEFINED AS INPUT AND OUTPUT IN INSERT_ELEMENT                 */
/*               SHORT-LIVED VARIABLES INCLUDE TEMPORARY VARIABLES, COMMON VARIABLES */
/*               AND POINTER VARIABLES.                                              */
/*               THE ONLY EXCEPTION TO THIS RULE IS FRAGRECPTR WHICH POINTS TO THE   */
/*               FRAGMENT RECORD. THIS IS MORE LESS STATIC ALWAYS DURING A SIGNAL    */
/*               EXECUTION.                                                          */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* INSERT_ELEMENT                                                                    */
/*       INPUT:                                                                      */
/*               IDR_PAGEPTR (POINTER TO THE ACTIVE PAGE REC)                        */
/*               TIDR_PAGEINDEX (INDEX OF THE CONTAINER)                             */
/*               TIDR_FORWARD (DIRECTION FORWARD OR BACKWARD)                        */
/*               TIDR_ELEMHEAD (HEADER OF ELEMENT TO BE INSERTED                     */
/*               CIDR_KEYS(ARRAY OF TUPLE KEYS)                                      */
/*               CLOCALKEY(ARRAY OF LOCAL KEYS).                                     */
/*               FRAGRECPTR                                                          */
/*               IDR_OPERATION_REC_PTR                                               */
/*               TIDR_KEY_LEN                                                        */
/*                                                                                   */
/*       OUTPUT:                                                                     */
/*               TIDR_PAGEINDEX (PAGE INDEX OF INSERTED ELEMENT)                     */
/*               IDR_PAGEPTR    (PAGE POINTER OF INSERTED ELEMENT)                   */
/*               TIDR_FORWARD   (CONTAINER DIRECTION OF INSERTED ELEMENT)            */
/*               NONE                                                                */
/* --------------------------------------------------------------------------------- */
void Dbacc::insertElement(Signal* signal) 
{
  DirRangePtr inrOverflowrangeptr;
  DirectoryarrayPtr inrOverflowDirptr;
  OverflowRecordPtr inrOverflowRecPtr;
  Page8Ptr inrNewPageptr;
  Uint32 tinrNextSamePage;
  Uint32 tinrTmp;

  do {
    insertContainer(signal);
    if (tidrResult != ZFALSE) {
      jam();
      return;
      /* INSERTION IS DONE, OR */
      /* AN ERROR IS DETECTED  */
    }//if
    if (((tidrContainerhead >> 7) & 0x3) != 0) {
      tinrNextSamePage = (tidrContainerhead >> 9) & 0x1;	/* CHECK BIT FOR CHECKING WHERE */
      /* THE NEXT CONTAINER IS IN THE SAME PAGE */
      tidrPageindex = tidrContainerhead & 0x7f;	/* NEXT CONTAINER PAGE INDEX 7 BITS */
      if (((tidrContainerhead >> 7) & 3) == ZLEFT) {
        jam();
        tidrForward = ZTRUE;
      } else if (((tidrContainerhead >> 7) & 3) == ZRIGHT) {
        jam();
        tidrForward = cminusOne;
      } else {
        ndbrequire(false);
        return;
      }//if
      if (tinrNextSamePage == ZFALSE) {
        jam();     /* NEXT CONTAINER IS IN AN OVERFLOW PAGE */
        tinrTmp = idrPageptr.p->word32[tidrContainerptr + 1];
        inrOverflowrangeptr.i = fragrecptr.p->overflowdir;
        ptrCheckGuard(inrOverflowrangeptr, cdirrangesize, dirRange);
        arrGuard((tinrTmp >> 8), 256);
        inrOverflowDirptr.i = inrOverflowrangeptr.p->dirArray[tinrTmp >> 8];
        ptrCheckGuard(inrOverflowDirptr, cdirarraysize, directoryarray);
        idrPageptr.i = inrOverflowDirptr.p->pagep[tinrTmp & 0xff];
        ptrCheckGuard(idrPageptr, cpagesize, page8);
      }//if
      ndbrequire(tidrPageindex < ZEMPTYLIST);
    } else {
      break;
    }//if
  } while (1);
  gflPageptr.p = idrPageptr.p;
  getfreelist(signal);
  if (tgflPageindex == ZEMPTYLIST) {
    jam();
    /* NO FREE BUFFER IS FOUND */
    if (fragrecptr.p->firstOverflowRec == RNIL) {
      jam();
      allocOverflowPage(signal);
      ndbrequire(tresult <= ZLIMIT_OF_ERROR);
    }//if
    inrOverflowRecPtr.i = fragrecptr.p->firstOverflowRec;
    ptrCheckGuard(inrOverflowRecPtr, coverflowrecsize, overflowRecord);
    inrNewPageptr.i = inrOverflowRecPtr.p->overpage;
    ptrCheckGuard(inrNewPageptr, cpagesize, page8);
    gflPageptr.p = inrNewPageptr.p;
    getfreelist(signal);
    ndbrequire(tgflPageindex != ZEMPTYLIST);
    tancNext = 0;
  } else {
    jam();
    inrNewPageptr = idrPageptr;
    tancNext = 1;
  }//if
  tslUpdateHeader = ZTRUE;
  tslPageindex = tgflPageindex;
  slPageptr.p = inrNewPageptr.p;
  if (tgflBufType == ZLEFT) {
    seizeLeftlist(signal);
    tidrForward = ZTRUE;
  } else {
    seizeRightlist(signal);
    tidrForward = cminusOne;
  }//if
  tancPageindex = tgflPageindex;
  tancPageid = inrNewPageptr.p->word32[ZPOS_PAGE_ID];
  tancBufType = tgflBufType;
  tancContainerptr = tidrContainerptr;
  ancPageptr.p = idrPageptr.p;
  addnewcontainer(signal);

  idrPageptr = inrNewPageptr;
  tidrPageindex = tgflPageindex;
  insertContainer(signal);
  ndbrequire(tidrResult == ZTRUE);
}//Dbacc::insertElement()

/* --------------------------------------------------------------------------------- */
/* INSERT_CONTAINER                                                                  */
/*           INPUT:                                                                  */
/*               IDR_PAGEPTR (POINTER TO THE ACTIVE PAGE REC)                        */
/*               TIDR_PAGEINDEX (INDEX OF THE CONTAINER)                             */
/*               TIDR_FORWARD (DIRECTION FORWARD OR BACKWARD)                        */
/*               TIDR_ELEMHEAD (HEADER OF ELEMENT TO BE INSERTED                     */
/*               CKEYS(ARRAY OF TUPLE KEYS)                                          */
/*               CLOCALKEY(ARRAY 0F LOCAL KEYS).                                     */
/*               TIDR_KEY_LEN                                                        */
/*               FRAGRECPTR                                                          */
/*               IDR_OPERATION_REC_PTR                                               */
/*           OUTPUT:                                                                 */
/*               TIDR_RESULT (ZTRUE FOR SUCCESS AND ZFALSE OTHERWISE)                */
/*               TIDR_CONTAINERHEAD (HEADER OF CONTAINER)                            */
/*               TIDR_CONTAINERPTR (POINTER TO CONTAINER HEADER)                     */
/*                                                                                   */
/*           DESCRIPTION:                                                            */
/*               THE FREE AREA OF THE CONTAINER WILL BE CALCULATED. IF IT IS         */
/*               LARGER THAN OR EQUAL THE ELEMENT LENGTH. THE ELEMENT WILL BE        */
/*               INSERT IN THE CONTAINER AND CONTAINER HEAD WILL BE UPDATED.         */
/*               THIS ROUTINE ALWAYS DEALS WITH ONLY ONE CONTAINER AND DO NEVER      */
/*               START ANYTHING OUTSIDE OF THIS CONTAINER.                           */
/*                                                                                   */
/*       SHORT FORM: IDR                                                             */
/* --------------------------------------------------------------------------------- */
void Dbacc::insertContainer(Signal* signal) 
{
  Uint32 tidrContainerlen;
  Uint32 tidrConfreelen;
  Uint32 tidrNextSide;
  Uint32 tidrNextConLen;
  Uint32 tidrIndex;
  Uint32 tidrInputIndex;
  Uint32 tidrContLen;
  Uint32 guard26;

  tidrResult = ZFALSE;
  tidrContainerptr = (tidrPageindex << ZSHIFT_PLUS) - (tidrPageindex << ZSHIFT_MINUS);
  tidrContainerptr = tidrContainerptr + ZHEAD_SIZE;
  /* --------------------------------------------------------------------------------- */
  /*       CALCULATE THE POINTER TO THE ELEMENT TO BE INSERTED AND THE POINTER TO THE  */
  /*       CONTAINER HEADER OF THE OTHER SIDE OF THE BUFFER.                           */
  /* --------------------------------------------------------------------------------- */
  if (tidrForward == ZTRUE) {
    jam();
    tidrNextSide = tidrContainerptr + (ZBUF_SIZE - ZCON_HEAD_SIZE);
    arrGuard(tidrNextSide + 1, 2048);
    tidrContainerhead = idrPageptr.p->word32[tidrContainerptr];
    tidrContainerlen = tidrContainerhead >> 26;
    tidrIndex = tidrContainerptr + tidrContainerlen;
  } else {
    jam();
    tidrNextSide = tidrContainerptr;
    tidrContainerptr = tidrContainerptr + (ZBUF_SIZE - ZCON_HEAD_SIZE);
    arrGuard(tidrContainerptr + 1, 2048);
    tidrContainerhead = idrPageptr.p->word32[tidrContainerptr];
    tidrContainerlen = tidrContainerhead >> 26;
    tidrIndex = (tidrContainerptr - tidrContainerlen) + (ZCON_HEAD_SIZE - 1);
  }//if
  if (tidrContainerlen > (ZBUF_SIZE - 3)) {
    return;
  }//if
  tidrConfreelen = ZBUF_SIZE - tidrContainerlen;
  /* --------------------------------------------------------------------------------- */
  /*       WE CALCULATE THE TOTAL LENGTH THE CONTAINER CAN EXPAND TO                   */
  /*       THIS INCLUDES THE OTHER SIDE OF THE BUFFER IF POSSIBLE TO EXPAND THERE.     */
  /* --------------------------------------------------------------------------------- */
  if (((tidrContainerhead >> 10) & 1) == 0) {
    jam();
    /* --------------------------------------------------------------------------------- */
    /*       WE HAVE NOT EXPANDED TO THE ENTIRE BUFFER YET. WE CAN THUS READ THE OTHER   */
    /*       SIDE'S CONTAINER HEADER TO READ HIS LENGTH.                                 */
    /* --------------------------------------------------------------------------------- */
    tidrNextConLen = idrPageptr.p->word32[tidrNextSide] >> 26;
    tidrConfreelen = tidrConfreelen - tidrNextConLen;
    if (tidrConfreelen > ZBUF_SIZE) {
      ndbrequire(false);
      /* --------------------------------------------------------------------------------- */
      /*       THE BUFFERS ARE PLACED ON TOP OF EACH OTHER. THIS SHOULD NEVER OCCUR.       */
      /* --------------------------------------------------------------------------------- */
      return;
    }//if
  } else {
    jam();
    tidrNextConLen = 1;	/* INDICATE OTHER SIDE IS NOT PART OF FREE LIST */
  }//if
  if (tidrConfreelen < fragrecptr.p->elementLength) {
    jam();
    /* --------------------------------------------------------------------------------- */
    /*       THE CONTAINER COULD NOT BE EXPANDED TO FIT THE NEW ELEMENT. WE HAVE TO      */
    /*       RETURN AND FIND A NEW CONTAINER TO INSERT IT INTO.                          */
    /* --------------------------------------------------------------------------------- */
    return;
  }//if
  tidrContainerlen = tidrContainerlen + fragrecptr.p->elementLength;
  if (tidrNextConLen == 0) {
    /* EACH SIDE OF THE BUFFER WHICH BELONG TO A FREE */
    /* LIST, HAS ZERO AS LENGTH. */
    if (tidrContainerlen > ZUP_LIMIT) {
      dbgWord32(idrPageptr, tidrContainerptr, idrPageptr.p->word32[tidrContainerptr] | (1 << 10));
      idrPageptr.p->word32[tidrContainerptr] = idrPageptr.p->word32[tidrContainerptr] | (1 << 10);
      tslUpdateHeader = ZFALSE;
      tslPageindex = tidrPageindex;
      slPageptr.p = idrPageptr.p;
      if (tidrForward == ZTRUE) {
        jam();
        seizeRightlist(signal);	/* REMOVE THE RIGHT SIDE OF THE BUFFER FROM THE LIST */
      } else {
        jam();
	/* OF THE FREE CONTAINERS */
        seizeLeftlist(signal);	/* REMOVE THE LEFT SIDE OF THE BUFFER FROM THE LIST */
      }//if
    }//if
  }//if
  /* OF THE FREE CONTAINERS */
  /* --------------------------------------------------------------------------------- */
  /*       WE HAVE NOW FOUND A FREE SPOT IN THE CURRENT CONTAINER. WE INSERT THE       */
  /*       ELEMENT HERE. THE ELEMENT CONTAINS A HEADER, A LOCAL KEY AND A TUPLE KEY.   */
  /*       BEFORE INSERTING THE ELEMENT WE WILL UPDATE THE OPERATION RECORD WITH THE   */
  /*       DATA CONCERNING WHERE WE INSERTED THE ELEMENT. THIS MAKES IT EASY TO FIND   */
  /*       THIS INFORMATION WHEN WE RETURN TO UPDATE THE LOCAL KEY OR RETURN TO COMMIT */
  /*       OR ABORT THE INSERT. IF NO OPERATION RECORD EXIST IT MEANS THAT WE ARE      */
  /*       PERFORMING THIS AS A PART OF THE EXPAND OR SHRINK PROCESS.                  */
  /* --------------------------------------------------------------------------------- */
  if (idrOperationRecPtr.i != RNIL) {
    jam();
    idrOperationRecPtr.p->elementIsforward = tidrForward;
    idrOperationRecPtr.p->elementPage = idrPageptr.i;
    idrOperationRecPtr.p->elementContainer = tidrContainerptr;
    idrOperationRecPtr.p->elementPointer = tidrIndex;
  }//if
  /* --------------------------------------------------------------------------------- */
  /*       WE CHOOSE TO UNDO LOG INSERTS BY WRITING THE BEFORE VALUE TO THE UNDO LOG.  */
  /*       WE COULD ALSO HAVE DONE THIS BY WRITING THIS BEFORE VALUE WHEN DELETING     */
  /*       ELEMENTS. WE CHOOSE TO PUT IT HERE SINCE WE THEREBY ENSURE THAT WE ALWAYS   */
  /*       UNDO LOG ALL WRITES TO PAGE MEMORY. IT SHOULD BE EASIER TO MAINTAIN SUCH A  */
  /*       STRUCTURE. IT IS RATHER DIFFICULT TO MAINTAIN A LOGICAL STRUCTURE WHERE     */
  /*       DELETES ARE INSERTS AND INSERTS ARE PURELY DELETES.                         */
  /* --------------------------------------------------------------------------------- */
  dbgWord32(idrPageptr, tidrIndex, tidrElemhead);
  idrPageptr.p->word32[tidrIndex] = tidrElemhead;	/* INSERTS THE HEAD OF THE ELEMENT */
  tidrIndex += tidrForward;
  guard26 = fragrecptr.p->localkeylen - 1;
  arrGuard(guard26, 2);
  for (tidrInputIndex = 0; tidrInputIndex <= guard26; tidrInputIndex++) {
    dbgWord32(idrPageptr, tidrIndex, clocalkey[tidrInputIndex]);
    arrGuard(tidrIndex, 2048);
    idrPageptr.p->word32[tidrIndex] = clocalkey[tidrInputIndex];	/* INSERTS LOCALKEY */
    tidrIndex += tidrForward;
  }//for
  tidrContLen = idrPageptr.p->word32[tidrContainerptr] << 6;
  tidrContLen = tidrContLen >> 6;
  dbgWord32(idrPageptr, tidrContainerptr, (tidrContainerlen << 26) | tidrContLen);
  idrPageptr.p->word32[tidrContainerptr] = (tidrContainerlen << 26) | tidrContLen;
  tidrResult = ZTRUE;
}//Dbacc::insertContainer()

/* --------------------------------------------------------------------------------- */
/* ADDNEWCONTAINER                                                                   */
/*       INPUT:                                                                      */
/*               TANC_CONTAINERPTR                                                   */
/*               ANC_PAGEPTR                                                         */
/*               TANC_NEXT                                                           */
/*               TANC_PAGEINDEX                                                      */
/*               TANC_BUF_TYPE                                                       */
/*               TANC_PAGEID                                                         */
/*       OUTPUT:                                                                     */
/*               NONE                                                                */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
void Dbacc::addnewcontainer(Signal* signal) 
{
  Uint32 tancTmp1;

  /* THE OLD DATA IS STORED ON AN UNDO PAGE */
  /* --------------------------------------------------------------------------------- */
  /*       KEEP LENGTH INFORMATION IN BIT 26-31.                                       */
  /*       SET BIT 9  INDICATING IF NEXT BUFFER IN THE SAME PAGE USING TANC_NEXT.      */
  /*       SET TYPE OF NEXT CONTAINER IN BIT 7-8.                                      */
  /*       SET PAGE INDEX OF NEXT CONTAINER IN BIT 0-6.                                */
  /*       KEEP INDICATOR OF OWNING OTHER SIDE OF BUFFER IN BIT 10.                    */
  /* --------------------------------------------------------------------------------- */
  tancTmp1 = ancPageptr.p->word32[tancContainerptr] >> 10;
  tancTmp1 = tancTmp1 << 1;
  tancTmp1 = tancTmp1 | tancNext;
  tancTmp1 = tancTmp1 << 2;
  tancTmp1 = tancTmp1 | tancBufType;	/* TYPE OF THE NEXT CONTAINER */
  tancTmp1 = tancTmp1 << 7;
  tancTmp1 = tancTmp1 | tancPageindex;
  dbgWord32(ancPageptr, tancContainerptr, tancTmp1);
  ancPageptr.p->word32[tancContainerptr] = tancTmp1;	/* HEAD OF THE CONTAINER IS UPDATED */
  dbgWord32(ancPageptr, tancContainerptr + 1, tancPageid);
  ancPageptr.p->word32[tancContainerptr + 1] = tancPageid;
}//Dbacc::addnewcontainer()

/* --------------------------------------------------------------------------------- */
/* GETFREELIST                                                                       */
/*         INPUT:                                                                    */
/*               GFL_PAGEPTR (POINTER TO A PAGE RECORD).                             */
/*         OUTPUT:                                                                   */
/*                TGFL_PAGEINDEX(POINTER TO A FREE BUFFER IN THE FREEPAGE), AND      */
/*                TGFL_BUF_TYPE( TYPE OF THE FREE BUFFER).                           */
/*         DESCRIPTION: SEARCHS IN THE FREE LIST OF THE FREE BUFFER IN THE PAGE HEAD */
/*                     (WORD32(1)),AND RETURN ADDRESS OF A FREE BUFFER OR NIL.       */
/*                     THE FREE BUFFER CAN BE A RIGHT CONTAINER OR A LEFT ONE        */
/*                     THE KIND OF THE CONTAINER IS NOTED BY TGFL_BUF_TYPE.          */
/* --------------------------------------------------------------------------------- */
void Dbacc::getfreelist(Signal* signal) 
{
  Uint32 tgflTmp;

  tgflTmp = gflPageptr.p->word32[ZPOS_EMPTY_LIST];
  tgflPageindex = (tgflTmp >> 7) & 0x7f;	/* LEFT FREE LIST */
  tgflBufType = ZLEFT;
  if (tgflPageindex == ZEMPTYLIST) {
    jam();
    tgflPageindex = tgflTmp & 0x7f;	/* RIGHT FREE LIST */
    tgflBufType = ZRIGHT;
  }//if
  ndbrequire(tgflPageindex <= ZEMPTYLIST);
}//Dbacc::getfreelist()

/* --------------------------------------------------------------------------------- */
/* INCREASELISTCONT                                                                  */
/*       INPUT:                                                                      */
/*               ILC_PAGEPTR     PAGE POINTER TO INCREASE NUMBER OF CONTAINERS IN    */
/*           A CONTAINER OF AN OVERFLOW PAGE (FREEPAGEPTR) IS ALLOCATED, NR OF       */
/*           ALLOCATED CONTAINER HAVE TO BE INCRESE BY ONE .                         */
/*           IF THE NUMBER OF ALLOCATED CONTAINERS IS ABOVE THE FREE LIMIT WE WILL   */
/*           REMOVE THE PAGE FROM THE FREE LIST.                                     */
/* --------------------------------------------------------------------------------- */
void Dbacc::increaselistcont(Signal* signal) 
{
  OverflowRecordPtr ilcOverflowRecPtr;

  dbgWord32(ilcPageptr, ZPOS_ALLOC_CONTAINERS, ilcPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] + 1);
  ilcPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] = ilcPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] + 1;
  if (ilcPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] > ZFREE_LIMIT) {
    if (ilcPageptr.p->word32[ZPOS_OVERFLOWREC] != RNIL) {
      jam();
      ilcOverflowRecPtr.i = ilcPageptr.p->word32[ZPOS_OVERFLOWREC];
      dbgWord32(ilcPageptr, ZPOS_OVERFLOWREC, RNIL);
      ilcPageptr.p->word32[ZPOS_OVERFLOWREC] = RNIL;
      ptrCheckGuard(ilcOverflowRecPtr, coverflowrecsize, overflowRecord);
      tfoOverflowRecPtr = ilcOverflowRecPtr;
      takeRecOutOfFreeOverpage(signal);
      rorOverflowRecPtr = ilcOverflowRecPtr;
      releaseOverflowRec(signal);
    }//if
  }//if
}//Dbacc::increaselistcont()

/* --------------------------------------------------------------------------------- */
/* SEIZE_LEFTLIST                                                                    */
/*       INPUT:                                                                      */
/*               TSL_PAGEINDEX           PAGE INDEX OF CONTAINER TO SEIZE            */
/*               SL_PAGEPTR              PAGE POINTER OF CONTAINER TO SEIZE          */
/*               TSL_UPDATE_HEADER       SHOULD WE UPDATE THE CONTAINER HEADER       */
/*                                                                                   */
/*       OUTPUT:                                                                     */
/*               NONE                                                                */
/*         DESCRIPTION: THE BUFFER NOTED BY TSL_PAGEINDEX WILL BE REMOVED FROM THE   */
/*                      LIST OF LEFT FREE CONTAINER, IN THE HEADER OF THE PAGE       */
/*                      (FREEPAGEPTR). PREVIOUS AND NEXT BUFFER OF REMOVED BUFFER    */
/*                      WILL BE UPDATED.                                             */
/* --------------------------------------------------------------------------------- */
void Dbacc::seizeLeftlist(Signal* signal) 
{
  Uint32 tsllTmp1;
  Uint32 tsllNewHead;
  Uint32 tsllHeadIndex;
  Uint32 tsllTmp;

  tsllHeadIndex = ((tslPageindex << ZSHIFT_PLUS) - (tslPageindex << ZSHIFT_MINUS)) + ZHEAD_SIZE;
  arrGuard(tsllHeadIndex + 1, 2048);
  tslNextfree = slPageptr.p->word32[tsllHeadIndex];
  tslPrevfree = slPageptr.p->word32[tsllHeadIndex + 1];
  if (tslPrevfree == ZEMPTYLIST) {
    jam();
    /* UPDATE FREE LIST OF LEFT CONTAINER IN PAGE HEAD */
    tsllTmp1 = slPageptr.p->word32[ZPOS_EMPTY_LIST];
    tsllTmp = tsllTmp1 & 0x7f;
    tsllTmp1 = (tsllTmp1 >> 14) << 14;
    tsllTmp1 = (tsllTmp1 | (tslNextfree << 7)) | tsllTmp;
    dbgWord32(slPageptr, ZPOS_EMPTY_LIST, tsllTmp1);
    slPageptr.p->word32[ZPOS_EMPTY_LIST] = tsllTmp1;
  } else {
    ndbrequire(tslPrevfree < ZEMPTYLIST);
    jam();
    tsllTmp = ((tslPrevfree << ZSHIFT_PLUS) - (tslPrevfree << ZSHIFT_MINUS)) + ZHEAD_SIZE;
    dbgWord32(slPageptr, tsllTmp, tslNextfree);
    slPageptr.p->word32[tsllTmp] = tslNextfree;
  }//if
  if (tslNextfree < ZEMPTYLIST) {
    jam();
    tsllTmp = (((tslNextfree << ZSHIFT_PLUS) - (tslNextfree << ZSHIFT_MINUS)) + ZHEAD_SIZE) + 1;
    dbgWord32(slPageptr, tsllTmp, tslPrevfree);
    slPageptr.p->word32[tsllTmp] = tslPrevfree;
  } else {
    ndbrequire(tslNextfree == ZEMPTYLIST);
    jam();
  }//if
  /* --------------------------------------------------------------------------------- */
  /*       IF WE ARE UPDATING THE HEADER WE ARE CREATING A NEW CONTAINER IN THE PAGE.  */
  /*       TO BE ABLE TO FIND ALL LOCKED ELEMENTS WE KEEP ALL CONTAINERS IN LINKED     */
  /*       LISTS IN THE PAGE.                                                          */
  /*                                                                                   */
  /*       ZPOS_EMPTY_LIST CONTAINS A NEXT POINTER IN BIT 16-22 THAT REFERS TO THE     */
  /*       FIRST CONTAINER IN A LIST OF USED RIGHT CONTAINERS IN THE PAGE.             */
  /*       ZPOS_EMPTY_LIST CONTAINS A NEXT POINTER IN BIT 23-29 THAT REFERS TO THE     */
  /*       FIRST CONTAINER IN A LIST OF USED LEFT CONTAINERS IN THE PAGE.              */
  /*       EACH CONTAINER IN THE LIST CONTAINS A NEXT POINTER IN BIT 11-17 AND IT      */
  /*       CONTAINS A PREVIOUS POINTER IN BIT 18-24.                                   */
  /*	WE ALSO SET BIT 25 TO INDICATE THAT IT IS A CONTAINER HEADER.               */
  /* --------------------------------------------------------------------------------- */
  if (tslUpdateHeader == ZTRUE) {
    jam();
    tslNextfree = (slPageptr.p->word32[ZPOS_EMPTY_LIST] >> 23) & 0x7f;
    tsllNewHead = ZCON_HEAD_SIZE;
    tsllNewHead = ((tsllNewHead << 8) + ZEMPTYLIST) + (1 << 7);
    tsllNewHead = (tsllNewHead << 7) + tslNextfree;
    tsllNewHead = tsllNewHead << 11;
    dbgWord32(slPageptr, tsllHeadIndex, tsllNewHead);
    slPageptr.p->word32[tsllHeadIndex] = tsllNewHead;
    tsllTmp = slPageptr.p->word32[ZPOS_EMPTY_LIST] & 0xc07fffff;
    tsllTmp = tsllTmp | (tslPageindex << 23);
    dbgWord32(slPageptr, ZPOS_EMPTY_LIST, tsllTmp);
    slPageptr.p->word32[ZPOS_EMPTY_LIST] = tsllTmp;
    if (tslNextfree < ZEMPTYLIST) {
      jam();
      tsllTmp = ((tslNextfree << ZSHIFT_PLUS) - (tslNextfree << ZSHIFT_MINUS)) + ZHEAD_SIZE;
      tsllTmp1 = slPageptr.p->word32[tsllTmp] & 0xfe03ffff;
      tsllTmp1 = tsllTmp1 | (tslPageindex << 18);
      dbgWord32(slPageptr, tsllTmp, tsllTmp1);
      slPageptr.p->word32[tsllTmp] = tsllTmp1;
    } else {
      ndbrequire(tslNextfree == ZEMPTYLIST);
      jam();
    }//if
  }//if
  ilcPageptr.p = slPageptr.p;
  increaselistcont(signal);
}//Dbacc::seizeLeftlist()

/* --------------------------------------------------------------------------------- */
/* SEIZE_RIGHTLIST                                                                   */
/*         DESCRIPTION: THE BUFFER NOTED BY TSL_PAGEINDEX WILL BE REMOVED FROM THE   */
/*                      LIST OF RIGHT FREE CONTAINER, IN THE HEADER OF THE PAGE      */
/*                      (SL_PAGEPTR). PREVIOUS AND NEXT BUFFER OF REMOVED BUFFER     */
/*                      WILL BE UPDATED.                                             */
/* --------------------------------------------------------------------------------- */
void Dbacc::seizeRightlist(Signal* signal) 
{
  Uint32 tsrlTmp1;
  Uint32 tsrlNewHead;
  Uint32 tsrlHeadIndex;
  Uint32 tsrlTmp;

  tsrlHeadIndex = ((tslPageindex << ZSHIFT_PLUS) - (tslPageindex << ZSHIFT_MINUS)) + ((ZHEAD_SIZE + ZBUF_SIZE) - ZCON_HEAD_SIZE);
  arrGuard(tsrlHeadIndex + 1, 2048);
  tslNextfree = slPageptr.p->word32[tsrlHeadIndex];
  tslPrevfree = slPageptr.p->word32[tsrlHeadIndex + 1];
  if (tslPrevfree == ZEMPTYLIST) {
    jam();
    tsrlTmp = slPageptr.p->word32[ZPOS_EMPTY_LIST];
    dbgWord32(slPageptr, ZPOS_EMPTY_LIST, ((tsrlTmp >> 7) << 7) | tslNextfree);
    slPageptr.p->word32[ZPOS_EMPTY_LIST] = ((tsrlTmp >> 7) << 7) | tslNextfree;
  } else {
    ndbrequire(tslPrevfree < ZEMPTYLIST);
    jam();
    tsrlTmp = ((tslPrevfree << ZSHIFT_PLUS) - (tslPrevfree << ZSHIFT_MINUS)) + ((ZHEAD_SIZE + ZBUF_SIZE) - ZCON_HEAD_SIZE);
    dbgWord32(slPageptr, tsrlTmp, tslNextfree);
    slPageptr.p->word32[tsrlTmp] = tslNextfree;
  }//if
  if (tslNextfree < ZEMPTYLIST) {
    jam();
    tsrlTmp = ((tslNextfree << ZSHIFT_PLUS) - (tslNextfree << ZSHIFT_MINUS)) + ((ZHEAD_SIZE + ZBUF_SIZE) - (ZCON_HEAD_SIZE - 1));
    dbgWord32(slPageptr, tsrlTmp, tslPrevfree);
    slPageptr.p->word32[tsrlTmp] = tslPrevfree;
  } else {
    ndbrequire(tslNextfree == ZEMPTYLIST);
    jam();
  }//if
  /* --------------------------------------------------------------------------------- */
  /*       IF WE ARE UPDATING THE HEADER WE ARE CREATING A NEW CONTAINER IN THE PAGE.  */
  /*       TO BE ABLE TO FIND ALL LOCKED ELEMENTS WE KEEP ALL CONTAINERS IN LINKED     */
  /*       LISTS IN THE PAGE.                                                          */
  /*                                                                                   */
  /*       ZPOS_EMPTY_LIST CONTAINS A NEXT POINTER IN BIT 16-22 THAT REFERS TO THE     */
  /*       FIRST CONTAINER IN A LIST OF USED RIGHT CONTAINERS IN THE PAGE.             */
  /*       ZPOS_EMPTY_LIST CONTAINS A NEXT POINTER IN BIT 23-29 THAT REFERS TO THE     */
  /*       FIRST CONTAINER IN A LIST OF USED LEFT CONTAINERS IN THE PAGE.              */
  /*       EACH CONTAINER IN THE LIST CONTAINS A NEXT POINTER IN BIT 11-17 AND IT      */
  /*       CONTAINS A PREVIOUS POINTER IN BIT 18-24.                                   */
  /* --------------------------------------------------------------------------------- */
  if (tslUpdateHeader == ZTRUE) {
    jam();
    tslNextfree = (slPageptr.p->word32[ZPOS_EMPTY_LIST] >> 16) & 0x7f;
    tsrlNewHead = ZCON_HEAD_SIZE;
    tsrlNewHead = ((tsrlNewHead << 8) + ZEMPTYLIST) + (1 << 7);
    tsrlNewHead = (tsrlNewHead << 7) + tslNextfree;
    tsrlNewHead = tsrlNewHead << 11;
    dbgWord32(slPageptr, tsrlHeadIndex, tsrlNewHead);
    slPageptr.p->word32[tsrlHeadIndex] = tsrlNewHead;
    tsrlTmp = slPageptr.p->word32[ZPOS_EMPTY_LIST] & 0xff80ffff;
    dbgWord32(slPageptr, ZPOS_EMPTY_LIST, tsrlTmp | (tslPageindex << 16));
    slPageptr.p->word32[ZPOS_EMPTY_LIST] = tsrlTmp | (tslPageindex << 16);
    if (tslNextfree < ZEMPTYLIST) {
      jam();
      tsrlTmp = ((tslNextfree << ZSHIFT_PLUS) - (tslNextfree << ZSHIFT_MINUS)) + ((ZHEAD_SIZE + ZBUF_SIZE) - ZCON_HEAD_SIZE);
      tsrlTmp1 = slPageptr.p->word32[tsrlTmp] & 0xfe03ffff;
      dbgWord32(slPageptr, tsrlTmp, tsrlTmp1 | (tslPageindex << 18));
      slPageptr.p->word32[tsrlTmp] = tsrlTmp1 | (tslPageindex << 18);
    } else {
      ndbrequire(tslNextfree == ZEMPTYLIST);
      jam();
    }//if
  }//if
  ilcPageptr.p = slPageptr.p;
  increaselistcont(signal);
}//Dbacc::seizeRightlist()

/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       END OF INSERT_ELEMENT MODULE                                                */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       MODULE:         GET_ELEMENT                                                 */
/*               THE FOLLOWING SUBROUTINES ARE ONLY USED BY GET_ELEMENT AND          */
/*               GETDIRINDEX. THIS ROUTINE IS THE SOLE INTERFACE TO GET ELEMENTS     */
/*               FROM THE INDEX. CURRENT USERS ARE ALL REQUESTS AND EXECUTE UNDO LOG */
/*                                                                                   */
/*               THE FOLLOWING SUBROUTINES ARE INCLUDED IN THIS MODULE:              */
/*               GET_ELEMENT                                                         */
/*               GET_DIRINDEX                                                        */
/*               SEARCH_LONG_KEY                                                     */
/*                                                                                   */
/*               THESE ROUTINES ARE ONLY USED BY THIS MODULE AND BY NO ONE ELSE.     */
/*               ALSO THE ROUTINES MAKE NO USE OF ROUTINES IN OTHER MODULES.         */
/*               THE ONLY SHORT-LIVED VARIABLES USED IN OTHER PARTS OF THE BLOCK ARE */
/*               THOSE DEFINED AS INPUT AND OUTPUT IN GET_ELEMENT AND GETDIRINDEX    */
/*               SHORT-LIVED VARIABLES INCLUDE TEMPORARY VARIABLES, COMMON VARIABLES */
/*               AND POINTER VARIABLES.                                              */
/*               THE ONLY EXCEPTION TO THIS RULE IS FRAGRECPTR WHICH POINTS TO THE   */
/*               FRAGMENT RECORD. THIS IS MORE LESS STATIC ALWAYS DURING A SIGNAL    */
/*               EXECUTION.                                                          */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* GETDIRINDEX                                                                       */
/*       SUPPORT ROUTINE FOR INSERT ELEMENT, GET ELEMENT AND COMMITDELETE            */
/*         INPUT:FRAGRECPTR ( POINTER TO THE ACTIVE FRAGMENT REC)                    */
/*               OPERATION_REC_PTR  (POINTER TO THE OPERATION REC).                  */
/*                                                                                   */
/*         OUTPUT:GDI_PAGEPTR ( POINTER TO THE PAGE OF THE ELEMENT)                  */
/*                TGDI_PAGEINDEX ( INDEX OF THE ELEMENT IN THE PAGE).                */
/*                                                                                   */
/*         DESCRIPTION: CHECK THE HASH VALUE OF THE OPERATION REC AND CALCULATE THE  */
/*                     THE ADDRESS OF THE ELEMENT IN THE HASH TABLE,(GDI_PAGEPTR,    */
/*                     TGDI_PAGEINDEX) ACCORDING TO LH3.                             */
/* --------------------------------------------------------------------------------- */
void Dbacc::getdirindex(Signal* signal) 
{
  DirRangePtr gdiDirRangePtr;
  DirectoryarrayPtr gdiDirptr;
  Uint32 tgdiTmp;
  Uint32 tgdiAddress;

  tgdiTmp = fragrecptr.p->k + fragrecptr.p->lhfragbits;	/* OBS K = 6 */
  tgdiPageindex = operationRecPtr.p->hashValue & ((1 << fragrecptr.p->k) - 1);
  tgdiTmp = operationRecPtr.p->hashValue >> tgdiTmp;
  tgdiTmp = (tgdiTmp << fragrecptr.p->k) | tgdiPageindex;
  tgdiAddress = tgdiTmp & fragrecptr.p->maxp;
  gdiDirRangePtr.i = fragrecptr.p->directory;
  ptrCheckGuard(gdiDirRangePtr, cdirrangesize, dirRange);
  if (tgdiAddress < fragrecptr.p->p) {
    jam();
    tgdiAddress = tgdiTmp & ((fragrecptr.p->maxp << 1) | 1);
  }//if
  tgdiTmp = tgdiAddress >> fragrecptr.p->k;
  arrGuard((tgdiTmp >> 8), 256);
  gdiDirptr.i = gdiDirRangePtr.p->dirArray[tgdiTmp >> 8];
  ptrCheckGuard(gdiDirptr, cdirarraysize, directoryarray);
  gdiPageptr.i = gdiDirptr.p->pagep[tgdiTmp & 0xff];	/* DIRECTORY INDEX OF SEND BUCKET PAGE */
  ptrCheckGuard(gdiPageptr, cpagesize, page8);
}//Dbacc::getdirindex()

Uint32
Dbacc::readTablePk(Uint32 localkey1)
{
  Uint32 tableId = fragrecptr.p->myTableId;
  Uint32 fragId = fragrecptr.p->myfid;
  Uint32 fragPageId = localkey1 >> MAX_TUPLES_BITS;
  Uint32 pageIndex = localkey1 & ((1 << MAX_TUPLES_BITS ) - 1);
#ifdef VM_TRACE
  memset(ckeys, 0x1f, (fragrecptr.p->keyLength * MAX_XFRM_MULTIPLY) << 2);
#endif
  int ret = c_tup->accReadPk(tableId, fragId, fragPageId, pageIndex, ckeys, true);
  jamEntry();
  ndbrequire(ret >= 0);
  return ret;
}

/* --------------------------------------------------------------------------------- */
/* GET_ELEMENT                                                                       */
/*        INPUT:                                                                     */
/*               OPERATION_REC_PTR                                                   */
/*               FRAGRECPTR                                                          */
/*        OUTPUT:                                                                    */
/*               TGE_RESULT      RESULT SUCCESS = ZTRUE OTHERWISE ZFALSE             */
/*               TGE_LOCKED      LOCK INFORMATION IF SUCCESSFUL RESULT               */
/*               GE_PAGEPTR      PAGE POINTER OF FOUND ELEMENT                       */
/*               TGE_CONTAINERPTR CONTAINER INDEX OF FOUND ELEMENT                   */
/*               TGE_ELEMENTPTR  ELEMENT INDEX OF FOUND ELEMENT                      */
/*               TGE_FORWARD     DIRECTION OF CONTAINER WHERE ELEMENT FOUND          */
/*                                                                                   */
/*        DESCRIPTION: THE SUBROUTIN GOES THROUGH ALL CONTAINERS OF THE ACTIVE       */
/*                     BUCKET, AND SERCH FOR ELEMENT.THE PRIMARY KEYS WHICH IS SAVED */
/*                     IN THE OPERATION REC ARE THE CHECK ITEMS IN THE SEARCHING.    */
/* --------------------------------------------------------------------------------- */

#if __ia64 == 1
#if __INTEL_COMPILER == 810
int ndb_acc_ia64_icc810_dummy_var = 0;
void ndb_acc_ia64_icc810_dummy_func()
{
  ndb_acc_ia64_icc810_dummy_var++;
}
#endif
#endif

void Dbacc::getElement(Signal* signal) 
{
  DirRangePtr geOverflowrangeptr;
  DirectoryarrayPtr geOverflowDirptr;
  OperationrecPtr geTmpOperationRecPtr;
  Uint32 tgeElementHeader;
  Uint32 tgeElemStep;
  Uint32 tgeContainerhead;
  Uint32 tgePageindex;
  Uint32 tgeActivePageDir;
  Uint32 tgeNextptrtype;
  register Uint32 tgeKeyptr;
  register Uint32 tgeRemLen;
  register Uint32 TelemLen = fragrecptr.p->elementLength;
  register Uint32* Tkeydata = (Uint32*)&signal->theData[7];

  getdirindex(signal);
  tgePageindex = tgdiPageindex;
  gePageptr = gdiPageptr;
  tgeResult = ZFALSE;
  /*
   * The value seached is
   * - table key for ACCKEYREQ, stored in TUP
   * - local key (1 word) for ACC_LOCKREQ and UNDO, stored in ACC
   */
  const bool searchLocalKey = operationRecPtr.p->tupkeylen == 0;

  ndbrequire(TelemLen == ZELEM_HEAD_SIZE + fragrecptr.p->localkeylen);
  tgeNextptrtype = ZLEFT;
  tgeLocked = 0;

  const Uint32 tmp = fragrecptr.p->k + fragrecptr.p->lhfragbits;
  const Uint32 opHashValuePart = (operationRecPtr.p->hashValue >> tmp) &0xFFFF;
  do {
    tgeContainerptr = (tgePageindex << ZSHIFT_PLUS) - (tgePageindex << ZSHIFT_MINUS);
    if (tgeNextptrtype == ZLEFT) {
      jam();
      tgeContainerptr = tgeContainerptr + ZHEAD_SIZE;
      tgeElementptr = tgeContainerptr + ZCON_HEAD_SIZE;
      tgeKeyptr = (tgeElementptr + ZELEM_HEAD_SIZE) + fragrecptr.p->localkeylen;
      tgeElemStep = TelemLen;
      tgeForward = 1;
      if (tgeContainerptr >= 2048) { ACCKEY_error(4); return;}
      tgeRemLen = gePageptr.p->word32[tgeContainerptr] >> 26;
      if ((tgeContainerptr + tgeRemLen - 1) >= 2048) { ACCKEY_error(5); return;}
    } else if (tgeNextptrtype == ZRIGHT) {
      jam();
      tgeContainerptr = tgeContainerptr + ((ZHEAD_SIZE + ZBUF_SIZE) - ZCON_HEAD_SIZE);
      tgeElementptr = tgeContainerptr - 1;
      tgeKeyptr = (tgeElementptr - ZELEM_HEAD_SIZE) - fragrecptr.p->localkeylen;
      tgeElemStep = 0 - TelemLen;
      tgeForward = (Uint32)-1;
      if (tgeContainerptr >= 2048) { ACCKEY_error(4); return;}
      tgeRemLen = gePageptr.p->word32[tgeContainerptr] >> 26;
      if ((tgeContainerptr - tgeRemLen) >= 2048) { ACCKEY_error(5); return;}
    } else {
      ACCKEY_error(6); return;
    }//if
    if (tgeRemLen >= ZCON_HEAD_SIZE + TelemLen) {
      if (tgeRemLen > ZBUF_SIZE) {
        ACCKEY_error(7); return;
      }//if
      /* --------------------------------------------------------------------------------- */
      // There is at least one element in this container. Check if it is the element
      // searched for.
      /* --------------------------------------------------------------------------------- */
      do {
        tgeElementHeader = gePageptr.p->word32[tgeElementptr];
        tgeRemLen = tgeRemLen - TelemLen;
        Uint32 hashValuePart;
        if (ElementHeader::getLocked(tgeElementHeader)) {
          jam();
          geTmpOperationRecPtr.i = ElementHeader::getOpPtrI(tgeElementHeader);
          ptrCheckGuard(geTmpOperationRecPtr, coprecsize, operationrec);
          hashValuePart = geTmpOperationRecPtr.p->hashvaluePart;
        } else {
          jam();
          hashValuePart = ElementHeader::getHashValuePart(tgeElementHeader);
        }
        if (hashValuePart == opHashValuePart) {
          jam();
          Uint32 localkey1 = gePageptr.p->word32[tgeElementptr + tgeForward];
          Uint32 localkey2 = 0;
          bool found;
          if (! searchLocalKey) {
            Uint32 len = readTablePk(localkey1);
            found = (len == operationRecPtr.p->xfrmtupkeylen) &&
                    (memcmp(Tkeydata, ckeys, len << 2) == 0);
          } else {
            jam();
            found = (localkey1 == Tkeydata[0]);
          }
          if (found) {
            jam();
            tgeLocked = ElementHeader::getLocked(tgeElementHeader);
            tgeResult = ZTRUE;
            operationRecPtr.p->localdata[0] = localkey1;
            operationRecPtr.p->localdata[1] = localkey2;
            return;
          }
        }
        if (tgeRemLen <= ZCON_HEAD_SIZE) {
          break;
        }
        tgeElementptr = tgeElementptr + tgeElemStep;
      } while (true);
    }//if
    if (tgeRemLen != ZCON_HEAD_SIZE) {
      ACCKEY_error(8); return;
    }//if
    tgeContainerhead = gePageptr.p->word32[tgeContainerptr];
    tgeNextptrtype = (tgeContainerhead >> 7) & 0x3;
    if (tgeNextptrtype == 0) {
      jam();
      return;	/* NO MORE CONTAINER */
    }//if
    tgePageindex = tgeContainerhead & 0x7f;	/* NEXT CONTAINER PAGE INDEX 7 BITS */
    if (tgePageindex > ZEMPTYLIST) {
      ACCKEY_error(9); return;
    }//if
    if (((tgeContainerhead >> 9) & 1) == ZFALSE) {
      jam();
      tgeActivePageDir = gePageptr.p->word32[tgeContainerptr + 1];	/* NEXT PAGE ID */
      geOverflowrangeptr.i = fragrecptr.p->overflowdir;
      ptrCheckGuard(geOverflowrangeptr, cdirrangesize, dirRange);
      arrGuard((tgeActivePageDir >> 8), 256);
      geOverflowDirptr.i = geOverflowrangeptr.p->dirArray[tgeActivePageDir >> 8];
      ptrCheckGuard(geOverflowDirptr, cdirarraysize, directoryarray);
      gePageptr.i = geOverflowDirptr.p->pagep[tgeActivePageDir & 0xff];
      ptrCheckGuard(gePageptr, cpagesize, page8);
    }//if
  } while (1);
  return;
}//Dbacc::getElement()

/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       END OF GET_ELEMENT MODULE                                                   */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       MODULE:         DELETE                                                      */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* COMMITDELETE                                                                      */
/*         INPUT: OPERATION_REC_PTR, PTR TO AN OPERATION RECORD.                     */
/*                FRAGRECPTR, PTR TO A FRAGMENT RECORD                               */
/*                                                                                   */
/*         OUTPUT:                                                                   */
/*                NONE                                                               */
/*         DESCRIPTION: DELETE OPERATIONS WILL BE COMPLETED AT THE COMMIT OF TRANSA- */
/*         CTION. THIS SUBROUTINE SEARCHS FOR ELEMENT AND DELETES IT. IT DOES SO BY  */
/*         REPLACING IT WITH THE LAST ELEMENT IN THE BUCKET. IF THE DELETED ELEMENT  */
/*         IS ALSO THE LAST ELEMENT THEN IT IS ONLY NECESSARY TO REMOVE THE ELEMENT. */
/* --------------------------------------------------------------------------------- */
void Dbacc::commitdelete(Signal* signal)
{
  jam();
  Uint32 localKey = operationRecPtr.p->localdata[0];
  Uint32 userptr= operationRecPtr.p->userptr;
  Uint32 scanInd = operationRecPtr.p->operation == ZSCAN_OP 
    || operationRecPtr.p->isAccLockReq;
 
  signal->theData[0] = fragrecptr.p->myfid;
  signal->theData[1] = fragrecptr.p->myTableId;
  Uint32 pageId = localKey >> MAX_TUPLES_BITS;
  Uint32 pageIndex = localKey & ((1 << MAX_TUPLES_BITS) - 1);
  signal->theData[2] = pageId;
  signal->theData[3] = pageIndex;
  signal->theData[4] = userptr;
  signal->theData[5] = scanInd;
  EXECUTE_DIRECT(DBLQH, GSN_TUP_DEALLOCREQ, signal, 6);
  jamEntry();
  
  getdirindex(signal);
  tlastPageindex = tgdiPageindex;
  lastPageptr.i = gdiPageptr.i;
  lastPageptr.p = gdiPageptr.p;
  tlastForward = ZTRUE;
  tlastContainerptr = (tlastPageindex << ZSHIFT_PLUS) - (tlastPageindex << ZSHIFT_MINUS);
  tlastContainerptr = tlastContainerptr + ZHEAD_SIZE;
  arrGuard(tlastContainerptr, 2048);
  tlastContainerhead = lastPageptr.p->word32[tlastContainerptr];
  tlastContainerlen = tlastContainerhead >> 26;
  lastPrevpageptr.i = RNIL;
  ptrNull(lastPrevpageptr);
  tlastPrevconptr = 0;
  getLastAndRemove(signal);

  delPageptr.i = operationRecPtr.p->elementPage;
  ptrCheckGuard(delPageptr, cpagesize, page8);
  tdelElementptr = operationRecPtr.p->elementPointer;
  /* --------------------------------------------------------------------------------- */
  // Here we have to take extreme care since we do not want locks to end up after the
  // log execution. Thus it is necessary to put back the element in unlocked shape.
  // We thus update the element header to ensure we log an unlocked element. We do not
  // need to restore it later since it is deleted immediately anyway.
  /* --------------------------------------------------------------------------------- */
  const Uint32 hv = operationRecPtr.p->hashvaluePart;
  const Uint32 eh = ElementHeader::setUnlocked(hv, 0);
  delPageptr.p->word32[tdelElementptr] = eh;
  if (operationRecPtr.p->elementPage == lastPageptr.i) {
    if (operationRecPtr.p->elementPointer == tlastElementptr) {
      jam();
      /* --------------------------------------------------------------------------------- */
      /*  THE LAST ELEMENT WAS THE ELEMENT TO BE DELETED. WE NEED NOT COPY IT.             */
      /* --------------------------------------------------------------------------------- */
      return;
    }//if
  }//if
  /* --------------------------------------------------------------------------------- */
  /*  THE DELETED ELEMENT IS NOT THE LAST. WE READ THE LAST ELEMENT AND OVERWRITE THE  */
  /*  DELETED ELEMENT.                                                                 */
  /* --------------------------------------------------------------------------------- */
  tdelContainerptr = operationRecPtr.p->elementContainer;
  tdelForward = operationRecPtr.p->elementIsforward;
  deleteElement(signal);
}//Dbacc::commitdelete()

/* --------------------------------------------------------------------------------- */
/* DELETE_ELEMENT                                                                    */
/*        INPUT: FRAGRECPTR, POINTER TO A FRAGMENT RECORD                            */
/*               LAST_PAGEPTR, POINTER TO THE PAGE OF THE LAST ELEMENT               */
/*               DEL_PAGEPTR, POINTER TO THE PAGE OF THE DELETED ELEMENT             */
/*               TLAST_ELEMENTPTR, ELEMENT POINTER OF THE LAST ELEMENT               */
/*               TDEL_ELEMENTPTR, ELEMENT POINTER OF THE DELETED ELEMENT             */
/*               TLAST_FORWARD, DIRECTION OF LAST ELEMENT                            */
/*               TDEL_FORWARD, DIRECTION OF DELETED ELEMENT                          */
/*               TDEL_CONTAINERPTR, CONTAINER POINTER OF DELETED ELEMENT             */
/*        DESCRIPTION: COPY LAST ELEMENT TO DELETED ELEMENT AND UPDATE UNDO LOG AND  */
/*                     UPDATE ANY ACTIVE OPERATION ON THE MOVED ELEMENT.             */
/* --------------------------------------------------------------------------------- */
void Dbacc::deleteElement(Signal* signal) 
{
  OperationrecPtr deOperationRecPtr;
  Uint32 tdeIndex;
  Uint32 tlastMoveElemptr;
  Uint32 tdelMoveElemptr;
  Uint32 guard31;

  if (tlastElementptr >= 2048)
    goto deleteElement_index_error1;
  {
    const Uint32 tdeElemhead = lastPageptr.p->word32[tlastElementptr];
    tlastMoveElemptr = tlastElementptr;
    tdelMoveElemptr = tdelElementptr;
    guard31 = fragrecptr.p->elementLength - 1;
    for (tdeIndex = 0; tdeIndex <= guard31; tdeIndex++) {
      dbgWord32(delPageptr, tdelMoveElemptr, lastPageptr.p->word32[tlastMoveElemptr]);
      if ((tlastMoveElemptr >= 2048) ||
	  (tdelMoveElemptr >= 2048))
	goto deleteElement_index_error2;
      delPageptr.p->word32[tdelMoveElemptr] = lastPageptr.p->word32[tlastMoveElemptr];
      tdelMoveElemptr = tdelMoveElemptr + tdelForward;
      tlastMoveElemptr = tlastMoveElemptr + tlastForward;
    }//for
    if (ElementHeader::getLocked(tdeElemhead)) {
      /* --------------------------------------------------------------------------------- */
      /* THE LAST ELEMENT IS LOCKED AND IS THUS REFERENCED BY AN OPERATION RECORD. WE NEED */
      /* TO UPDATE THE OPERATION RECORD WITH THE NEW REFERENCE TO THE ELEMENT.             */
      /* --------------------------------------------------------------------------------- */
      deOperationRecPtr.i = ElementHeader::getOpPtrI(tdeElemhead);
      ptrCheckGuard(deOperationRecPtr, coprecsize, operationrec);
      deOperationRecPtr.p->elementPage = delPageptr.i;
      deOperationRecPtr.p->elementContainer = tdelContainerptr;
      deOperationRecPtr.p->elementPointer = tdelElementptr;
      deOperationRecPtr.p->elementIsforward = tdelForward;
      /* --------------------------------------------------------------------------------- */
      // We need to take extreme care to not install locked records after system restart.
      // An undo of the delete will reinstall the moved record. We have to ensure that the
      // lock is removed to ensure that no such thing happen.
      /* --------------------------------------------------------------------------------- */
      Uint32 eh = ElementHeader::setUnlocked(deOperationRecPtr.p->hashvaluePart,
					     0);
      lastPageptr.p->word32[tlastElementptr] = eh;
    }//if
    return;
  }

 deleteElement_index_error1:
  arrGuard(tlastElementptr, 2048);
  return;

 deleteElement_index_error2:
  arrGuard(tdelMoveElemptr + guard31, 2048);
  arrGuard(tlastMoveElemptr, 2048);
  return;

}//Dbacc::deleteElement()

/* --------------------------------------------------------------------------------- */
/* GET_LAST_AND_REMOVE                                                               */
/*        INPUT:                                                                     */
/*               LAST_PAGEPTR       PAGE POINTER OF FIRST CONTAINER IN SEARCH OF LAST*/
/*               TLAST_CONTAINERPTR CONTAINER INDEX OF THE SAME                      */
/*               TLAST_CONTAINERHEAD CONTAINER HEADER OF THE SAME                    */
/*               TLAST_PAGEINDEX    PAGE INDEX OF THE SAME                           */
/*               TLAST_FORWARD      CONTAINER DIRECTION OF THE SAME                  */
/*               TLAST_CONTAINERLEN CONTAINER LENGTH OF THE SAME                     */
/*               LAST_PREVPAGEPTR   PAGE POINTER OF PREVIOUS CONTAINER OF THE SAME   */
/*               TLAST_PREVCONPTR   CONTAINER INDEX OF PREVIOUS CONTAINER OF THE SAME*/
/*                                                                                   */
/*       OUTPUT:                                                                     */
/*               ALL VARIABLES FROM INPUT BUT NOW CONTAINING INFO ABOUT LAST         */
/*               CONTAINER.                                                          */
/*               TLAST_ELEMENTPTR   LAST ELEMENT POINTER IN LAST CONTAINER           */
/* --------------------------------------------------------------------------------- */
void Dbacc::getLastAndRemove(Signal* signal) 
{
  DirRangePtr glrOverflowrangeptr;
  DirectoryarrayPtr glrOverflowDirptr;
  Uint32 tglrHead;
  Uint32 tglrTmp;

 GLR_LOOP_10:
  if (((tlastContainerhead >> 7) & 0x3) != 0) {
    jam();
    lastPrevpageptr.i = lastPageptr.i;
    lastPrevpageptr.p = lastPageptr.p;
    tlastPrevconptr = tlastContainerptr;
    tlastPageindex = tlastContainerhead & 0x7f;
    if (((tlastContainerhead >> 9) & 0x1) == ZFALSE) {
      jam();
      arrGuard(tlastContainerptr + 1, 2048);
      tglrTmp = lastPageptr.p->word32[tlastContainerptr + 1];
      glrOverflowrangeptr.i = fragrecptr.p->overflowdir;
      ptrCheckGuard(glrOverflowrangeptr, cdirrangesize, dirRange);
      arrGuard((tglrTmp >> 8), 256);
      glrOverflowDirptr.i = glrOverflowrangeptr.p->dirArray[tglrTmp >> 8];
      ptrCheckGuard(glrOverflowDirptr, cdirarraysize, directoryarray);
      lastPageptr.i = glrOverflowDirptr.p->pagep[tglrTmp & 0xff];
      ptrCheckGuard(lastPageptr, cpagesize, page8);
    }//if
    tlastContainerptr = (tlastPageindex << ZSHIFT_PLUS) - (tlastPageindex << ZSHIFT_MINUS);
    if (((tlastContainerhead >> 7) & 3) == ZLEFT) {
      jam();
      tlastForward = ZTRUE;
      tlastContainerptr = tlastContainerptr + ZHEAD_SIZE;
    } else if (((tlastContainerhead >> 7) & 3) == ZRIGHT) {
      jam();
      tlastForward = cminusOne;
      tlastContainerptr = ((tlastContainerptr + ZHEAD_SIZE) + ZBUF_SIZE) - ZCON_HEAD_SIZE;
    } else {
      ndbrequire(false);
      return;
    }//if
    arrGuard(tlastContainerptr, 2048);
    tlastContainerhead = lastPageptr.p->word32[tlastContainerptr];
    tlastContainerlen = tlastContainerhead >> 26;
    ndbrequire(tlastContainerlen >= ((Uint32)ZCON_HEAD_SIZE + fragrecptr.p->elementLength));
    goto GLR_LOOP_10;
  }//if
  tlastContainerlen = tlastContainerlen - fragrecptr.p->elementLength;
  if (tlastForward == ZTRUE) {
    jam();
    tlastElementptr = tlastContainerptr + tlastContainerlen;
  } else {
    jam();
    tlastElementptr = (tlastContainerptr + (ZCON_HEAD_SIZE - 1)) - tlastContainerlen;
  }//if
  rlPageptr.i = lastPageptr.i;
  rlPageptr.p = lastPageptr.p;
  trlPageindex = tlastPageindex;
  if (((tlastContainerhead >> 10) & 1) == 1) {
    /* --------------------------------------------------------------------------------- */
    /*       WE HAVE OWNERSHIP OF BOTH PARTS OF THE CONTAINER ENDS.                      */
    /* --------------------------------------------------------------------------------- */
    if (tlastContainerlen < ZDOWN_LIMIT) {
      /* --------------------------------------------------------------------------------- */
      /*       WE HAVE DECREASED THE SIZE BELOW THE DOWN LIMIT, WE MUST GIVE UP THE OTHER  */
      /*       SIDE OF THE BUFFER.                                                         */
      /* --------------------------------------------------------------------------------- */
      tlastContainerhead = tlastContainerhead ^ (1 << 10);
      trlRelCon = ZFALSE;
      if (tlastForward == ZTRUE) {
        jam();
        turlIndex = tlastContainerptr + (ZBUF_SIZE - ZCON_HEAD_SIZE);
        releaseRightlist(signal);
      } else {
        jam();
        tullIndex = tlastContainerptr - (ZBUF_SIZE - ZCON_HEAD_SIZE);
        releaseLeftlist(signal);
      }//if
    }//if
  }//if
  if (tlastContainerlen <= 2) {
    ndbrequire(tlastContainerlen == 2);
    if (lastPrevpageptr.i != RNIL) {
      jam();
      /* --------------------------------------------------------------------------------- */
      /*  THE LAST CONTAINER IS EMPTY AND IS NOT THE FIRST CONTAINER WHICH IS NOT REMOVED. */
      /*  DELETE THE LAST CONTAINER AND UPDATE THE PREVIOUS CONTAINER. ALSO PUT THIS       */
      /*  CONTAINER IN FREE CONTAINER LIST OF THE PAGE.                                    */
      /* --------------------------------------------------------------------------------- */
      ndbrequire(tlastPrevconptr < 2048);
      tglrTmp = lastPrevpageptr.p->word32[tlastPrevconptr] >> 9;
      dbgWord32(lastPrevpageptr, tlastPrevconptr, tglrTmp << 9);
      lastPrevpageptr.p->word32[tlastPrevconptr] = tglrTmp << 9;
      trlRelCon = ZTRUE;
      if (tlastForward == ZTRUE) {
        jam();
        tullIndex = tlastContainerptr;
        releaseLeftlist(signal);
      } else {
        jam();
        turlIndex = tlastContainerptr;
        releaseRightlist(signal);
      }//if
      return;
    }//if
  }//if
  tglrHead = tlastContainerhead << 6;
  tglrHead = tglrHead >> 6;
  tglrHead = tglrHead | (tlastContainerlen << 26);
  dbgWord32(lastPageptr, tlastContainerptr, tglrHead);
  arrGuard(tlastContainerptr, 2048);
  lastPageptr.p->word32[tlastContainerptr] = tglrHead;
}//Dbacc::getLastAndRemove()

/* --------------------------------------------------------------------------------- */
/* RELEASE_LEFTLIST                                                                  */
/*       INPUT:                                                                      */
/*               RL_PAGEPTR              PAGE POINTER OF CONTAINER TO BE RELEASED    */
/*               TRL_PAGEINDEX           PAGE INDEX OF CONTAINER TO BE RELEASED      */
/*               TURL_INDEX              INDEX OF CONTAINER TO BE RELEASED           */
/*               TRL_REL_CON             TRUE IF CONTAINER RELEASED OTHERWISE ONLY   */
/*                                       A PART IS RELEASED.                         */
/*                                                                                   */
/*       OUTPUT:                                                                     */
/*               NONE                                                                */
/*                                                                                   */
/*          THE FREE LIST OF LEFT FREE BUFFER IN THE PAGE WILL BE UPDATE             */
/*     TULL_INDEX IS INDEX TO THE FIRST WORD IN THE LEFT SIDE OF THE BUFFER          */
/* --------------------------------------------------------------------------------- */
void Dbacc::releaseLeftlist(Signal* signal) 
{
  Uint32 tullTmp;
  Uint32 tullTmp1;

  /* --------------------------------------------------------------------------------- */
  /*       IF A CONTAINER IS RELEASED AND NOT ONLY A PART THEN WE HAVE TO REMOVE IT    */
  /*       FROM THE LIST OF USED CONTAINERS IN THE PAGE. THIS IN ORDER TO ENSURE THAT  */
  /*       WE CAN FIND ALL LOCKED ELEMENTS DURING LOCAL CHECKPOINT.                    */
  /* --------------------------------------------------------------------------------- */
  if (trlRelCon == ZTRUE) {
    arrGuard(tullIndex, 2048);
    trlHead = rlPageptr.p->word32[tullIndex];
    trlNextused = (trlHead >> 11) & 0x7f;
    trlPrevused = (trlHead >> 18) & 0x7f;
    if (trlNextused < ZEMPTYLIST) {
      jam();
      tullTmp1 = (trlNextused << ZSHIFT_PLUS) - (trlNextused << ZSHIFT_MINUS);
      tullTmp1 = tullTmp1 + ZHEAD_SIZE;
      tullTmp = rlPageptr.p->word32[tullTmp1] & 0xfe03ffff;
      dbgWord32(rlPageptr, tullTmp1, tullTmp | (trlPrevused << 18));
      rlPageptr.p->word32[tullTmp1] = tullTmp | (trlPrevused << 18);
    } else {
      ndbrequire(trlNextused == ZEMPTYLIST);
      jam();
    }//if
    if (trlPrevused < ZEMPTYLIST) {
      jam();
      tullTmp1 = (trlPrevused << ZSHIFT_PLUS) - (trlPrevused << ZSHIFT_MINUS);
      tullTmp1 = tullTmp1 + ZHEAD_SIZE;
      tullTmp = rlPageptr.p->word32[tullTmp1] & 0xfffc07ff;
      dbgWord32(rlPageptr, tullTmp1, tullTmp | (trlNextused << 11));
      rlPageptr.p->word32[tullTmp1] = tullTmp | (trlNextused << 11);
    } else {
      ndbrequire(trlPrevused == ZEMPTYLIST);
      jam();
      /* --------------------------------------------------------------------------------- */
      /*       WE ARE FIRST IN THE LIST AND THUS WE NEED TO UPDATE THE FIRST POINTER.      */
      /* --------------------------------------------------------------------------------- */
      tullTmp = rlPageptr.p->word32[ZPOS_EMPTY_LIST] & 0xc07fffff;
      dbgWord32(rlPageptr, ZPOS_EMPTY_LIST, tullTmp | (trlNextused << 23));
      rlPageptr.p->word32[ZPOS_EMPTY_LIST] = tullTmp | (trlNextused << 23);
    }//if
  }//if
  dbgWord32(rlPageptr, tullIndex + 1, ZEMPTYLIST);
  arrGuard(tullIndex + 1, 2048);
  rlPageptr.p->word32[tullIndex + 1] = ZEMPTYLIST;
  tullTmp1 = (rlPageptr.p->word32[ZPOS_EMPTY_LIST] >> 7) & 0x7f;
  dbgWord32(rlPageptr, tullIndex, tullTmp1);
  arrGuard(tullIndex, 2048);
  rlPageptr.p->word32[tullIndex] = tullTmp1;
  if (tullTmp1 < ZEMPTYLIST) {
    jam();
    tullTmp1 = (tullTmp1 << ZSHIFT_PLUS) - (tullTmp1 << ZSHIFT_MINUS);
    tullTmp1 = (tullTmp1 + ZHEAD_SIZE) + 1;
    dbgWord32(rlPageptr, tullTmp1, trlPageindex);
    rlPageptr.p->word32[tullTmp1] = trlPageindex;	/* UPDATES PREV POINTER IN THE NEXT FREE */
  } else {
    ndbrequire(tullTmp1 == ZEMPTYLIST);
  }//if
  tullTmp = rlPageptr.p->word32[ZPOS_EMPTY_LIST];
  tullTmp = (((tullTmp >> 14) << 14) | (trlPageindex << 7)) | (tullTmp & 0x7f);
  dbgWord32(rlPageptr, ZPOS_EMPTY_LIST, tullTmp);
  rlPageptr.p->word32[ZPOS_EMPTY_LIST] = tullTmp;
  dbgWord32(rlPageptr, ZPOS_ALLOC_CONTAINERS, rlPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] - 1);
  rlPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] = rlPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] - 1;
  ndbrequire(rlPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] <= ZNIL);
  if (((rlPageptr.p->word32[ZPOS_EMPTY_LIST] >> ZPOS_PAGE_TYPE_BIT) & 3) == 1) {
    jam();
    colPageptr.i = rlPageptr.i;
    colPageptr.p = rlPageptr.p;
    ptrCheck(colPageptr, cpagesize, page8);
    checkoverfreelist(signal);
  }//if
}//Dbacc::releaseLeftlist()

/* --------------------------------------------------------------------------------- */
/* RELEASE_RIGHTLIST                                                                 */
/*       INPUT:                                                                      */
/*               RL_PAGEPTR              PAGE POINTER OF CONTAINER TO BE RELEASED    */
/*               TRL_PAGEINDEX           PAGE INDEX OF CONTAINER TO BE RELEASED      */
/*               TURL_INDEX              INDEX OF CONTAINER TO BE RELEASED           */
/*               TRL_REL_CON             TRUE IF CONTAINER RELEASED OTHERWISE ONLY   */
/*                                       A PART IS RELEASED.                         */
/*                                                                                   */
/*       OUTPUT:                                                                     */
/*               NONE                                                                */
/*                                                                                   */
/*         THE FREE LIST OF RIGHT FREE BUFFER IN THE PAGE WILL BE UPDATE.            */
/*         TURL_INDEX IS INDEX TO THE FIRST WORD IN THE RIGHT SIDE OF                */
/*         THE BUFFER, WHICH IS THE LAST WORD IN THE BUFFER.                         */
/* --------------------------------------------------------------------------------- */
void Dbacc::releaseRightlist(Signal* signal) 
{
  Uint32 turlTmp1;
  Uint32 turlTmp;

  /* --------------------------------------------------------------------------------- */
  /*       IF A CONTAINER IS RELEASED AND NOT ONLY A PART THEN WE HAVE TO REMOVE IT    */
  /*       FROM THE LIST OF USED CONTAINERS IN THE PAGE. THIS IN ORDER TO ENSURE THAT  */
  /*       WE CAN FIND ALL LOCKED ELEMENTS DURING LOCAL CHECKPOINT.                    */
  /* --------------------------------------------------------------------------------- */
  if (trlRelCon == ZTRUE) {
    jam();
    arrGuard(turlIndex, 2048);
    trlHead = rlPageptr.p->word32[turlIndex];
    trlNextused = (trlHead >> 11) & 0x7f;
    trlPrevused = (trlHead >> 18) & 0x7f;
    if (trlNextused < ZEMPTYLIST) {
      jam();
      turlTmp1 = (trlNextused << ZSHIFT_PLUS) - (trlNextused << ZSHIFT_MINUS);
      turlTmp1 = turlTmp1 + ((ZHEAD_SIZE + ZBUF_SIZE) - ZCON_HEAD_SIZE);
      turlTmp = rlPageptr.p->word32[turlTmp1] & 0xfe03ffff;
      dbgWord32(rlPageptr, turlTmp1, turlTmp | (trlPrevused << 18));
      rlPageptr.p->word32[turlTmp1] = turlTmp | (trlPrevused << 18);
    } else {
      ndbrequire(trlNextused == ZEMPTYLIST);
      jam();
    }//if
    if (trlPrevused < ZEMPTYLIST) {
      jam();
      turlTmp1 = (trlPrevused << ZSHIFT_PLUS) - (trlPrevused << ZSHIFT_MINUS);
      turlTmp1 = turlTmp1 + ((ZHEAD_SIZE + ZBUF_SIZE) - ZCON_HEAD_SIZE);
      turlTmp = rlPageptr.p->word32[turlTmp1] & 0xfffc07ff;
      dbgWord32(rlPageptr, turlTmp1, turlTmp | (trlNextused << 11));
      rlPageptr.p->word32[turlTmp1] = turlTmp | (trlNextused << 11);
    } else {
      ndbrequire(trlPrevused == ZEMPTYLIST);
      jam();
      /* --------------------------------------------------------------------------------- */
      /*       WE ARE FIRST IN THE LIST AND THUS WE NEED TO UPDATE THE FIRST POINTER       */
      /*       OF THE RIGHT CONTAINER LIST.                                                */
      /* --------------------------------------------------------------------------------- */
      turlTmp = rlPageptr.p->word32[ZPOS_EMPTY_LIST] & 0xff80ffff;
      dbgWord32(rlPageptr, ZPOS_EMPTY_LIST, turlTmp | (trlNextused << 16));
      rlPageptr.p->word32[ZPOS_EMPTY_LIST] = turlTmp | (trlNextused << 16);
    }//if
  }//if
  dbgWord32(rlPageptr, turlIndex + 1, ZEMPTYLIST);
  arrGuard(turlIndex + 1, 2048);
  rlPageptr.p->word32[turlIndex + 1] = ZEMPTYLIST;
  turlTmp1 = rlPageptr.p->word32[ZPOS_EMPTY_LIST] & 0x7f;
  dbgWord32(rlPageptr, turlIndex, turlTmp1);
  arrGuard(turlIndex, 2048);
  rlPageptr.p->word32[turlIndex] = turlTmp1;
  if (turlTmp1 < ZEMPTYLIST) {
    jam();
    turlTmp = (turlTmp1 << ZSHIFT_PLUS) - (turlTmp1 << ZSHIFT_MINUS);
    turlTmp = turlTmp + ((ZHEAD_SIZE + ZBUF_SIZE) - (ZCON_HEAD_SIZE - 1));
    dbgWord32(rlPageptr, turlTmp, trlPageindex);
    rlPageptr.p->word32[turlTmp] = trlPageindex;	/* UPDATES PREV POINTER IN THE NEXT FREE */
  } else {
    ndbrequire(turlTmp1 == ZEMPTYLIST);
  }//if
  turlTmp = rlPageptr.p->word32[ZPOS_EMPTY_LIST];
  dbgWord32(rlPageptr, ZPOS_EMPTY_LIST, ((turlTmp >> 7) << 7) | trlPageindex);
  rlPageptr.p->word32[ZPOS_EMPTY_LIST] = ((turlTmp >> 7) << 7) | trlPageindex;
  dbgWord32(rlPageptr, ZPOS_ALLOC_CONTAINERS, rlPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] - 1);
  rlPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] = rlPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] - 1;
  ndbrequire(rlPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] <= ZNIL);
  if (((rlPageptr.p->word32[ZPOS_EMPTY_LIST] >> ZPOS_PAGE_TYPE_BIT) & 3) == 1) {
    jam();
    colPageptr.i = rlPageptr.i;
    colPageptr.p = rlPageptr.p;
    checkoverfreelist(signal);
  }//if
}//Dbacc::releaseRightlist()

/* --------------------------------------------------------------------------------- */
/* CHECKOVERFREELIST                                                                 */
/*        INPUT: COL_PAGEPTR, POINTER OF AN OVERFLOW PAGE RECORD.                    */
/*        DESCRIPTION: CHECKS IF THE PAGE HAVE TO PUT IN FREE LIST OF OVER FLOW      */
/*                     PAGES. WHEN IT HAVE TO, AN OVERFLOW REC PTR WILL BE ALLOCATED */
/*                     TO KEEP NFORMATION  ABOUT THE PAGE.                           */
/* --------------------------------------------------------------------------------- */
void Dbacc::checkoverfreelist(Signal* signal) 
{
  Uint32 tcolTmp;

  tcolTmp = colPageptr.p->word32[ZPOS_ALLOC_CONTAINERS];
  if (tcolTmp <= ZFREE_LIMIT) {
    if (tcolTmp == 0) {
      jam();
      ropPageptr = colPageptr;
      releaseOverpage(signal);
    } else {
      jam();
      if (colPageptr.p->word32[ZPOS_OVERFLOWREC] == RNIL) {
	ndbrequire(cfirstfreeoverrec != RNIL);
	jam();
	seizeOverRec(signal);
	sorOverflowRecPtr.p->dirindex = colPageptr.p->word32[ZPOS_PAGE_ID];
	sorOverflowRecPtr.p->overpage = colPageptr.i;
	dbgWord32(colPageptr, ZPOS_OVERFLOWREC, sorOverflowRecPtr.i);
	colPageptr.p->word32[ZPOS_OVERFLOWREC] = sorOverflowRecPtr.i;
	porOverflowRecPtr = sorOverflowRecPtr;
	putOverflowRecInFrag(signal);
      }//if
    }//if
  }//if
}//Dbacc::checkoverfreelist()

/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/*                                                                           */
/*       END OF DELETE MODULE                                                */
/*                                                                           */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/*                                                                           */
/*       COMMIT AND ABORT MODULE                                             */
/*                                                                           */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */
/* ABORT_OPERATION                                                           */
/*DESCRIPTION: AN OPERATION RECORD CAN BE IN A LOCK QUEUE OF AN ELEMENT OR   */
/*OWNS THE LOCK. BY THIS SUBROUTINE THE LOCK STATE OF THE OPERATION WILL     */
/*BE CHECKED. THE OPERATION RECORD WILL BE REMOVED FROM THE QUEUE IF IT      */
/*BELONGED TO ANY ONE, OTHERWISE THE ELEMENT HEAD WILL BE UPDATED.           */
/* ------------------------------------------------------------------------- */
void Dbacc::abortOperation(Signal* signal) 
{
  OperationrecPtr aboOperRecPtr;
  OperationrecPtr TaboOperRecPtr;
  Page8Ptr aboPageidptr;
  Uint32 taboElementptr;
  Uint32 tmp2Olq;

  if (operationRecPtr.p->lockOwner == ZTRUE) {
    takeOutLockOwnersList(signal, operationRecPtr);
    if (operationRecPtr.p->insertIsDone == ZTRUE) { 
      jam();
      operationRecPtr.p->elementIsDisappeared = ZTRUE;
    }//if
    if ((operationRecPtr.p->nextParallelQue != RNIL) ||
        (operationRecPtr.p->nextSerialQue != RNIL)) {
      jam();
      releaselock(signal);
    } else {
      /* --------------------------------------------------------------------------------- */
      /* WE ARE OWNER OF THE LOCK AND NO OTHER OPERATIONS ARE QUEUED. IF INSERT OR STANDBY */
      /* WE DELETE THE ELEMENT OTHERWISE WE REMOVE THE LOCK FROM THE ELEMENT.              */
      /* --------------------------------------------------------------------------------- */
      if (operationRecPtr.p->elementIsDisappeared == ZFALSE) {
        jam();
        taboElementptr = operationRecPtr.p->elementPointer;
        aboPageidptr.i = operationRecPtr.p->elementPage;
        tmp2Olq = ElementHeader::setUnlocked(operationRecPtr.p->hashvaluePart,
					     operationRecPtr.p->scanBits);
        ptrCheckGuard(aboPageidptr, cpagesize, page8);
        dbgWord32(aboPageidptr, taboElementptr, tmp2Olq);
        arrGuard(taboElementptr, 2048);
        aboPageidptr.p->word32[taboElementptr] = tmp2Olq;
        return;
      } else {
        jam();
        commitdelete(signal);
      }//if
    }//if
  } else {
    /* --------------------------------------------------------------- */
    // We are not the lock owner.
    /* --------------------------------------------------------------- */
    jam();
    if (operationRecPtr.p->prevParallelQue != RNIL) {
      jam();
      /* ---------------------------------------------------------------------------------- */
      /* SINCE WE ARE NOT QUEUE LEADER WE NEED NOT CONSIDER IF THE ELEMENT IS TO BE DELETED.*/
      /* We will simply remove it from the parallel list without any other rearrangements.  */
      /* ---------------------------------------------------------------------------------- */
      aboOperRecPtr.i = operationRecPtr.p->prevParallelQue;
      ptrCheckGuard(aboOperRecPtr, coprecsize, operationrec);
      aboOperRecPtr.p->nextParallelQue = operationRecPtr.p->nextParallelQue;
      if (operationRecPtr.p->nextParallelQue != RNIL) {
        jam();
        aboOperRecPtr.i = operationRecPtr.p->nextParallelQue;
        ptrCheckGuard(aboOperRecPtr, coprecsize, operationrec);
        aboOperRecPtr.p->prevParallelQue = operationRecPtr.p->prevParallelQue;
      }//if
    } else if (operationRecPtr.p->prevSerialQue != RNIL) {
      /* ------------------------------------------------------------------------- */
      // We are not in the parallel queue owning the lock. Thus we are in another parallel
      // queue longer down in the serial queue. We are however first since prevParallelQue
      // == RNIL.
      /* ------------------------------------------------------------------------- */
      if (operationRecPtr.p->nextParallelQue != RNIL) {
        jam();
	/* ------------------------------------------------------------------------- */
	// We have an operation in the queue after us. We simply rearrange this parallel queue.
	// The new leader of this parallel queue will be operation in the serial queue.
	/* ------------------------------------------------------------------------- */
        aboOperRecPtr.i = operationRecPtr.p->nextParallelQue;
        ptrCheckGuard(aboOperRecPtr, coprecsize, operationrec);
        aboOperRecPtr.p->nextSerialQue = operationRecPtr.p->nextSerialQue;
        aboOperRecPtr.p->prevSerialQue = operationRecPtr.p->prevSerialQue;
        aboOperRecPtr.p->prevParallelQue = RNIL; // Queue Leader
        if (operationRecPtr.p->nextSerialQue != RNIL) {
          jam();
          TaboOperRecPtr.i = operationRecPtr.p->nextSerialQue;
          ptrCheckGuard(TaboOperRecPtr, coprecsize, operationrec);
          TaboOperRecPtr.p->prevSerialQue = aboOperRecPtr.i;
        }//if
        TaboOperRecPtr.i = operationRecPtr.p->prevSerialQue;
        ptrCheckGuard(TaboOperRecPtr, coprecsize, operationrec);
        TaboOperRecPtr.p->nextSerialQue = aboOperRecPtr.i;
      } else {
        jam();
	/* ------------------------------------------------------------------------- */
	// We are the only operation in this parallel queue. We will thus shrink the serial
	// queue.
	/* ------------------------------------------------------------------------- */
        aboOperRecPtr.i = operationRecPtr.p->prevSerialQue;
        ptrCheckGuard(aboOperRecPtr, coprecsize, operationrec);
        aboOperRecPtr.p->nextSerialQue = operationRecPtr.p->nextSerialQue;
        if (operationRecPtr.p->nextSerialQue != RNIL) {
          jam();
          aboOperRecPtr.i = operationRecPtr.p->nextSerialQue;
          ptrCheckGuard(aboOperRecPtr, coprecsize, operationrec);
          aboOperRecPtr.p->prevSerialQue = operationRecPtr.p->prevSerialQue;
        }//if
      }//if
    }//if
  }//if
  /* ------------------------------------------------------------------------- */
  // If prevParallelQue = RNIL and prevSerialQue = RNIL and we are not owner of the
  // lock then we cannot be in any lock queue at all.
  /* ------------------------------------------------------------------------- */
}//Dbacc::abortOperation()

void Dbacc::commitDeleteCheck()
{
  OperationrecPtr opPtr;
  OperationrecPtr lastOpPtr;
  OperationrecPtr deleteOpPtr;
  bool elementDeleted = false;
  bool deleteCheckOngoing = true;
  Uint32 hashValue = 0;
  lastOpPtr = operationRecPtr;
  opPtr.i = operationRecPtr.p->nextParallelQue;
  while (opPtr.i != RNIL) {
    jam();
    ptrCheckGuard(opPtr, coprecsize, operationrec);
    lastOpPtr = opPtr;
    opPtr.i = opPtr.p->nextParallelQue;
  }//while
  deleteOpPtr = lastOpPtr;
  do {
    if (deleteOpPtr.p->operation == ZDELETE) {
      jam();
      /* -------------------------------------------------------------------
       * IF THE CURRENT OPERATION TO BE COMMITTED IS A DELETE OPERATION DUE TO
       * A SCAN-TAKEOVER THE ACTUAL DELETE WILL BE PERFORMED BY THE PREVIOUS 
       * OPERATION (SCAN) IN THE PARALLEL QUEUE WHICH OWNS THE LOCK.
       * THE PROBLEM IS THAT THE SCAN OPERATION DOES NOT HAVE A HASH VALUE 
       * ASSIGNED TO IT SO WE COPY IT FROM THIS OPERATION.
       *
       * WE ASSUME THAT THIS SOLUTION WILL WORK BECAUSE THE ONLY WAY A 
       * SCAN CAN PERFORM A DELETE IS BY BEING FOLLOWED BY A NORMAL 
       * DELETE-OPERATION THAT HAS A HASH VALUE.
       * ----------------------------------------------------------------- */
      hashValue = deleteOpPtr.p->hashValue;
      elementDeleted = true;
      deleteCheckOngoing = false;
    } else if ((deleteOpPtr.p->operation == ZREAD) ||
               (deleteOpPtr.p->operation == ZSCAN_OP)) {
      /* -------------------------------------------------------------------
       * We are trying to find out whether the commit will in the end delete 
       * the tuple. Normally the delete will be the last operation in the 
       * list of operations on this. It is however possible to issue reads 
       * and scans in the same savepoint as the delete operation was issued 
       * and these can end up after the delete in the list of operations 
       * in the parallel queue. Thus if we discover a read or a scan 
       * we have to continue scanning the list looking for a delete operation.
       */
      deleteOpPtr.i = deleteOpPtr.p->prevParallelQue;
      if (deleteOpPtr.i == RNIL) {
        jam();
        deleteCheckOngoing = false;
      } else {
        jam();
        ptrCheckGuard(deleteOpPtr, coprecsize, operationrec);
      }//if
    } else {
      jam();
      /* ------------------------------------------------------------------ */
      /* Finding an UPDATE or INSERT before finding a DELETE 
       * means we cannot be deleting as the end result of this transaction.
       */
      deleteCheckOngoing = false;
    }//if
  } while (deleteCheckOngoing);
  opPtr = lastOpPtr;
  do {
    jam();
    opPtr.p->commitDeleteCheckFlag = ZTRUE;
    if (elementDeleted) {
      jam();
      opPtr.p->elementIsDisappeared = ZTRUE;
      opPtr.p->hashValue = hashValue;
    }//if
    opPtr.i = opPtr.p->prevParallelQue;
    if (opPtr.i == RNIL) {
      jam();
      break;
    }//if
    ptrCheckGuard(opPtr, coprecsize, operationrec);
  } while (true);
}//Dbacc::commitDeleteCheck()

/* ------------------------------------------------------------------------- */
/* COMMIT_OPERATION                                                          */
/* INPUT: OPERATION_REC_PTR, POINTER TO AN OPERATION RECORD                  */
/* DESCRIPTION: THE OPERATION RECORD WILL BE TAKE OUT OF ANY LOCK QUEUE.     */
/*         IF IT OWNS THE ELEMENT LOCK. HEAD OF THE ELEMENT WILL BE UPDATED. */
/* ------------------------------------------------------------------------- */
void Dbacc::commitOperation(Signal* signal) 
{
  OperationrecPtr tolqTmpPtr;
  Page8Ptr coPageidptr;
  Uint32 tcoElementptr;
  Uint32 tmp2Olq;

  if ((operationRecPtr.p->commitDeleteCheckFlag == ZFALSE) &&
      (operationRecPtr.p->operation != ZSCAN_OP) &&
      (operationRecPtr.p->operation != ZREAD)) {
    jam();
    /*  This method is used to check whether the end result of the transaction
        will be to delete the tuple. In this case all operation will be marked
        with elementIsDisappeared = true to ensure that the last operation
        committed will remove the tuple. We only run this once per transaction
        (commitDeleteCheckFlag = true if performed earlier) and we don't
        execute this code when committing a scan operation since committing
        a scan operation only means that the scan is continuing and the scan
        lock is released.
    */
    commitDeleteCheck();
  }//if
  if (operationRecPtr.p->lockOwner == ZTRUE) {
    takeOutLockOwnersList(signal, operationRecPtr);
    if ((operationRecPtr.p->nextParallelQue == RNIL) &&
        (operationRecPtr.p->nextSerialQue == RNIL) &&
        (operationRecPtr.p->elementIsDisappeared == ZFALSE)) {
      /* 
       This is the normal path through the commit for operations owning the
       lock without any queues and not a delete operation.
      */
      coPageidptr.i = operationRecPtr.p->elementPage;
      tcoElementptr = operationRecPtr.p->elementPointer;
      tmp2Olq = ElementHeader::setUnlocked(operationRecPtr.p->hashvaluePart,
					   operationRecPtr.p->scanBits);   
      ptrCheckGuard(coPageidptr, cpagesize, page8);
      dbgWord32(coPageidptr, tcoElementptr, tmp2Olq);
      arrGuard(tcoElementptr, 2048);
      coPageidptr.p->word32[tcoElementptr] = tmp2Olq;
      return;
    } else if ((operationRecPtr.p->nextParallelQue != RNIL) ||
               (operationRecPtr.p->nextSerialQue != RNIL)) {
      jam();
      /*
       The case when there is a queue lined up.
       Release the lock and pass it to the next operation lined up.
      */
      releaselock(signal);
      return;
    } else {
      jam();
      /*
       No queue and elementIsDisappeared is true. We perform the actual delete
       operation.
      */
      commitdelete(signal);
      return;
    }//if
  } else {
    /*
     THE OPERATION DOES NOT OWN THE LOCK. IT MUST BE IN A LOCK QUEUE OF THE
     ELEMENT.
    */
    ndbrequire(operationRecPtr.p->prevParallelQue != RNIL);
    jam();
    tolqTmpPtr.i = operationRecPtr.p->prevParallelQue;
    ptrCheckGuard(tolqTmpPtr, coprecsize, operationrec);
    tolqTmpPtr.p->nextParallelQue = operationRecPtr.p->nextParallelQue;
    if (operationRecPtr.p->nextParallelQue != RNIL) {
      jam();
      tolqTmpPtr.i = operationRecPtr.p->nextParallelQue;
      ptrCheckGuard(tolqTmpPtr, coprecsize, operationrec);
      tolqTmpPtr.p->prevParallelQue = operationRecPtr.p->prevParallelQue;
    }//if

    /**
     * Check possible lock upgrade
     * 1) Find lock owner
     * 2) Count transactions in parallel que
     * 3) If count == 1 and TRANSID(next serial) == TRANSID(lock owner)
     *      upgrade next serial
     */
    if(operationRecPtr.p->lockMode)
    {
      jam();
      /**
       * Committing a non shared operation can't lead to lock upgrade
       */
      return;
    }
    
    OperationrecPtr lock_owner;
    lock_owner.i = operationRecPtr.p->prevParallelQue;
    ptrCheckGuard(lock_owner, coprecsize, operationrec);
    Uint32 transid[2] = { lock_owner.p->transId1, 
			  lock_owner.p->transId2 };
    
    
    while(lock_owner.p->prevParallelQue != RNIL)
    {
      lock_owner.i = lock_owner.p->prevParallelQue;
      ptrCheckGuard(lock_owner, coprecsize, operationrec);
      
      if(lock_owner.p->transId1 != transid[0] || 
	 lock_owner.p->transId2 != transid[1])
      {
	jam();
	/**
	 * If more than 1 trans in lock queue -> no lock upgrade
	 */
	return;
      }
    }
    
    check_lock_upgrade(signal, lock_owner, operationRecPtr);
  }
}//Dbacc::commitOperation()

void
Dbacc::check_lock_upgrade(Signal* signal, 
			  OperationrecPtr lock_owner,
			  OperationrecPtr release_op)
{
  if((lock_owner.p->transId1 == release_op.p->transId1 &&
      lock_owner.p->transId2 == release_op.p->transId2) ||
     release_op.p->lockMode ||
     lock_owner.p->nextSerialQue == RNIL)
  {
    jam();
    /**
     * No lock upgrade if same trans or lock owner has no serial queue
     *                 or releasing non shared op
     */
    return;
  }

  OperationrecPtr next;
  next.i = lock_owner.p->nextSerialQue;
  ptrCheckGuard(next, coprecsize, operationrec);
  
  if(lock_owner.p->transId1 != next.p->transId1 ||
     lock_owner.p->transId2 != next.p->transId2)
  {
    jam();
    /**
     * No lock upgrad if !same trans in serial queue
     */
    return;
  }
  
  if (getNoParallelTransaction(lock_owner.p) > 1)
  {
    jam();
    /**
     * No lock upgrade if more than 1 transaction in parallell queue
     */
    return;
  }

  if (getNoParallelTransaction(next.p) > 1)
  {
    jam();
    /**
     * No lock upgrade if more than 1 transaction in next's parallell queue
     */
    return;
  }
  
  OperationrecPtr tmp;
  tmp.i = lock_owner.p->nextSerialQue = next.p->nextSerialQue;
  if(tmp.i != RNIL)
  {
    ptrCheckGuard(tmp, coprecsize, operationrec);
    ndbassert(tmp.p->prevSerialQue == next.i);
    tmp.p->prevSerialQue = lock_owner.i;
  }
  next.p->nextSerialQue = next.p->prevSerialQue = RNIL;
  
  // Find end of parallell que
  tmp = lock_owner;
  Uint32 lockMode = next.p->lockMode > lock_owner.p->lockMode ?
    next.p->lockMode : lock_owner.p->lockMode;
  while(tmp.p->nextParallelQue != RNIL)
  {
    jam();
    tmp.i = tmp.p->nextParallelQue;
    tmp.p->lockMode = lockMode;
    ptrCheckGuard(tmp, coprecsize, operationrec);
  }
  tmp.p->lockMode = lockMode;
  
  next.p->prevParallelQue = tmp.i;
  tmp.p->nextParallelQue = next.i;
  
  OperationrecPtr save = operationRecPtr;

  Uint32 localdata[2];
  localdata[0] = lock_owner.p->localdata[0];
  localdata[1] = lock_owner.p->localdata[1];
  do {
    next.p->localdata[0] = localdata[0];
    next.p->localdata[1] = localdata[1];
    next.p->lockMode = lockMode;
    
    operationRecPtr = next;
    executeNextOperation(signal);
    if (next.p->nextParallelQue != RNIL) 
    {
      jam();
      next.i = next.p->nextParallelQue;
      ptrCheckGuard(next, coprecsize, operationrec);
    } else {
      jam();
      break;
    }//if
  } while (1);
  
  operationRecPtr = save;
  
}

/* ------------------------------------------------------------------------- */
/* RELEASELOCK                                                               */
/*          RESETS LOCK OF AN ELEMENT.                                       */
/*          INFORMATION ABOUT THE ELEMENT IS SAVED IN THE OPERATION RECORD   */
/*          THESE INFORMATION IS USED TO UPDATE HEADER OF THE ELEMENT        */
/* ------------------------------------------------------------------------- */
void Dbacc::releaselock(Signal* signal) 
{
  OperationrecPtr rloOperPtr;
  OperationrecPtr trlOperPtr;
  OperationrecPtr trlTmpOperPtr;
  Uint32 TelementIsDisappeared;

  trlOperPtr.i = RNIL;
  if (operationRecPtr.p->nextParallelQue != RNIL) {
    jam();
    /** ---------------------------------------------------------------------
     * NEXT OPERATION TAKES OVER THE LOCK. 
     *  We will simply move the info from the leader 
     * to the new queue leader.
     * -------------------------------------------------------------------- */
    trlOperPtr.i = operationRecPtr.p->nextParallelQue;
    ptrCheckGuard(trlOperPtr, coprecsize, operationrec);
    copyInOperPtr = trlOperPtr;
    copyOperPtr = operationRecPtr;
    copyOpInfo(signal);
    trlOperPtr.p->prevParallelQue = RNIL;
    if (operationRecPtr.p->nextSerialQue != RNIL) {
      jam();
      /* -----------------------------------------------------------------
       * THERE IS A SERIAL QUEUE. MOVE IT FROM RELEASED OP REC TO THE 
       * NEW LOCK OWNER.     
       * ------------------------------------------------------------------ */
      trlOperPtr.p->nextSerialQue = operationRecPtr.p->nextSerialQue;
      trlTmpOperPtr.i = trlOperPtr.p->nextSerialQue;
      ptrCheckGuard(trlTmpOperPtr, coprecsize, operationrec);
      trlTmpOperPtr.p->prevSerialQue = trlOperPtr.i;
    }//if

    check_lock_upgrade(signal, copyInOperPtr, operationRecPtr);
  } else {
    ndbrequire(operationRecPtr.p->nextSerialQue != RNIL);
    jam();
    /** ---------------------------------------------------------------------
     * THE PARALLEL QUEUE IS EMPTY AND THE SERIAL QUEUE IS NOT EMPTY. 
     * WE NEED TO REARRANGE LISTS AND START A NUMBER OF OPERATIONS.
     * -------------------------------------------------------------------- */
    trlOperPtr.i = operationRecPtr.p->nextSerialQue;
    ptrCheckGuard(trlOperPtr, coprecsize, operationrec);
    copyOperPtr = operationRecPtr;
    copyInOperPtr = trlOperPtr;
    copyOpInfo(signal);
    trlOperPtr.p->prevSerialQue = RNIL;
    ndbrequire(trlOperPtr.p->prevParallelQue == RNIL);
    /* --------------------------------------------------------------------- */
    /* WE HAVE MOVED TO THE NEXT PARALLEL QUEUE. WE MUST START ALL OF THOSE  */
    /* OPERATIONS WHICH UP TILL NOW HAVE BEEN QUEUED WAITING FOR THE LOCK.   */
    /* --------------------------------------------------------------------- */
    rloOperPtr = operationRecPtr;
    trlTmpOperPtr = trlOperPtr;
    TelementIsDisappeared = trlOperPtr.p->elementIsDisappeared;
    Uint32 ThashValue = trlOperPtr.p->hashValue;
    do {
      /* ------------------------------------------------------------------ */
      // Ensure that all operations in the queue are assigned with the 
      // elementIsDisappeared to ensure that the element is removed after 
      // a previous delete. An insert does however revert this decision 
      // since the element is put back again. 
      // Local checkpoints complicate life here since they do not 
      // execute the next operation but simply change
      // the state on the operation. 
      // We need to set-up the variable elementIsDisappeared
      // properly even when local checkpoints and inserts/writes after 
      // deletes occur.
      /* ------------------------------------------------------------------- */
      trlTmpOperPtr.p->elementIsDisappeared = TelementIsDisappeared;
      if (TelementIsDisappeared == ZTRUE) {
	/* ----------------------------------------------------------------- */
	// If the elementIsDisappeared is set then we know that the 
	// hashValue is also set since it always originates from a 
	// committing abort or a aborting insert. 
	// Scans do not initialise the hashValue and must have this 
	// value initialised if they are to successfully commit the delete.
	/* ----------------------------------------------------------------- */
        jam();
        trlTmpOperPtr.p->hashValue = ThashValue;
      }//if
      trlTmpOperPtr.p->localdata[0] = trlOperPtr.p->localdata[0];
      trlTmpOperPtr.p->localdata[1] = trlOperPtr.p->localdata[1];
      /* ------------------------------------------------------------------- */
      // Restart the queued operation.
      /* ------------------------------------------------------------------- */
      operationRecPtr = trlTmpOperPtr;
      TelementIsDisappeared = executeNextOperation(signal);
      ThashValue = operationRecPtr.p->hashValue;
      if (trlTmpOperPtr.p->nextParallelQue != RNIL) {
        jam();
	/* ----------------------------------------------------------------- */
	// We will continue with the next operation in the parallel 
	// queue and start this as well.
	/* ----------------------------------------------------------------- */
        trlTmpOperPtr.i = trlTmpOperPtr.p->nextParallelQue;
        ptrCheckGuard(trlTmpOperPtr, coprecsize, operationrec);
      } else {
        jam();
        break;
      }//if
    } while (1);
    operationRecPtr = rloOperPtr;
  }//if

  // Insert the next op into the lock owner list
  insertLockOwnersList(signal, trlOperPtr);
  return;
}//Dbacc::releaselock()

/* --------------------------------------------------------------------------------- */
/* COPY_OP_INFO                                                                      */
/*        INPUT: COPY_IN_OPER_PTR AND COPY_OPER_PTR.                                 */
/*        DESCRIPTION:INFORMATION ABOUT THE ELEMENT WILL BE MOVED FROM  OPERATION    */
/*                      REC TO QUEUE OP REC. QUE OP REC TAKES OVER THE LOCK.         */
/* --------------------------------------------------------------------------------- */
void Dbacc::copyOpInfo(Signal* signal) 
{
  Page8Ptr coiPageidptr;

  copyInOperPtr.p->elementPage = copyOperPtr.p->elementPage;
  copyInOperPtr.p->elementIsforward = copyOperPtr.p->elementIsforward;
  copyInOperPtr.p->elementContainer = copyOperPtr.p->elementContainer;
  copyInOperPtr.p->elementPointer = copyOperPtr.p->elementPointer;
  copyInOperPtr.p->scanBits = copyOperPtr.p->scanBits;
  copyInOperPtr.p->hashvaluePart = copyOperPtr.p->hashvaluePart;
  copyInOperPtr.p->elementIsDisappeared = copyOperPtr.p->elementIsDisappeared;
  if (copyInOperPtr.p->elementIsDisappeared == ZTRUE) {
    /* --------------------------------------------------------------------------------- */
    // If the elementIsDisappeared is set then we know that the hashValue is also set
    // since it always originates from a committing abort or a aborting insert. Scans
    // do not initialise the hashValue and must have this value initialised if they are
    // to successfully commit the delete.
    /* --------------------------------------------------------------------------------- */
    jam();
    copyInOperPtr.p->hashValue = copyOperPtr.p->hashValue;
  }//if
  coiPageidptr.i = copyOperPtr.p->elementPage;
  ptrCheckGuard(coiPageidptr, cpagesize, page8);
  const Uint32 tmp = ElementHeader::setLocked(copyInOperPtr.i);
  dbgWord32(coiPageidptr, copyOperPtr.p->elementPointer, tmp);
  arrGuard(copyOperPtr.p->elementPointer, 2048);
  coiPageidptr.p->word32[copyOperPtr.p->elementPointer] = tmp;
  copyInOperPtr.p->localdata[0] = copyOperPtr.p->localdata[0];
  copyInOperPtr.p->localdata[1] = copyOperPtr.p->localdata[1];
}//Dbacc::copyOpInfo()

/* ******************--------------------------------------------------------------- */
/* EXECUTE NEXT OPERATION                                                            */
/*          NEXT OPERATION IN A LOCK QUEUE WILL BE EXECUTED.                         */
/* --------------------------------------------------------------------------------- */
Uint32 Dbacc::executeNextOperation(Signal* signal) 
{
  ndbrequire(operationRecPtr.p->transactionstate == ACTIVE);
  if (operationRecPtr.p->elementIsDisappeared == ZTRUE) {
    /* --------------------------------------------------------------------- */
    /* PREVIOUS OPERATION WAS DELETE OPERATION AND THE ELEMENT IS DELETED.   */
    /* --------------------------------------------------------------------- */
    if (((operationRecPtr.p->operation != ZINSERT) &&
	 (operationRecPtr.p->operation != ZWRITE)) ||
        (operationRecPtr.p->prevParallelQue != RNIL)) {
      if (operationRecPtr.p->operation != ZSCAN_OP ||
          operationRecPtr.p->isAccLockReq) {
        jam();
	/* ----------------------------------------------------------------- */
	// Updates and reads with a previous delete simply aborts with read 
	// error indicating that tuple did not exist. 
	// Also inserts and writes not being the first operation.
	/* ----------------------------------------------------------------- */
        operationRecPtr.p->transactionstate = WAIT_COMMIT_ABORT;
        signal->theData[0] = operationRecPtr.p->userptr;
        signal->theData[1] = ZREAD_ERROR;
        sendSignal(operationRecPtr.p->userblockref, GSN_ACCKEYREF, signal, 
		   2, JBB);
        return operationRecPtr.p->elementIsDisappeared;
      } else {
	/* ----------------------------------------------------------------- */
	/* ABORT OF OPERATION NEEDED BUT THE OPERATION IS A 
	 * SCAN => SPECIAL TREATMENT.
	 * IF THE SCAN WAITS IN QUEUE THEN WE MUST REMOVE THE OPERATION 
	 * FROM THE SCAN LOCK QUEUE AND IF NO MORE OPERATIONS ARE QUEUED 
	 * THEN WE SHOULD RESTART THE SCAN PROCESS. OTHERWISE WE SIMPLY 
	 * RELEASE THE OPERATION AND DECREASE THE NUMBER OF LOCKS HELD.
	 * ----------------------------------------------------------------- */
        takeOutScanLockQueue(operationRecPtr.p->scanRecPtr);
        putReadyScanQueue(signal, operationRecPtr.p->scanRecPtr);
        return operationRecPtr.p->elementIsDisappeared;
      }//if
    }//if
    /* --------------------------------------------------------------------- */
    // Insert and writes can continue but need to be converted to inserts.
    /* --------------------------------------------------------------------- */
    jam();
    operationRecPtr.p->elementIsDisappeared = ZFALSE;
    operationRecPtr.p->operation = ZINSERT;
    operationRecPtr.p->insertIsDone = ZTRUE;
  } else if (operationRecPtr.p->operation == ZINSERT) {
    bool abortFlag = true;
    if (operationRecPtr.p->prevParallelQue != RNIL) {
      OperationrecPtr prevOpPtr;
      jam();
      prevOpPtr.i = operationRecPtr.p->prevParallelQue;
      ptrCheckGuard(prevOpPtr, coprecsize, operationrec);
      if (prevOpPtr.p->operation == ZDELETE) {
        jam();
        abortFlag = false;
      }//if
    }//if
    if (abortFlag) {
      jam();
      /* ------------------------------------------------------------------- */
      /* ELEMENT STILL REMAINS AND WE ARE TRYING TO INSERT IT AGAIN.         */
      /*  THIS IS CLEARLY NOT A GOOD IDEA.                                   */
      /* ------------------------------------------------------------------- */
      operationRecPtr.p->transactionstate = WAIT_COMMIT_ABORT;
      signal->theData[0] = operationRecPtr.p->userptr;
      signal->theData[1] = ZWRITE_ERROR;
      sendSignal(operationRecPtr.p->userblockref, GSN_ACCKEYREF, signal, 
		 2, JBB);
      return operationRecPtr.p->elementIsDisappeared;
    }//if
  } 
  else if(operationRecPtr.p->operation == ZWRITE)
  {
    jam();
    operationRecPtr.p->operation = ZUPDATE;
    if (operationRecPtr.p->prevParallelQue != RNIL) {
      OperationrecPtr prevOpPtr;
      jam();
      prevOpPtr.i = operationRecPtr.p->prevParallelQue;
      ptrCheckGuard(prevOpPtr, coprecsize, operationrec);
      if (prevOpPtr.p->operation == ZDELETE) 
      {
        jam();
        operationRecPtr.p->operation = ZINSERT;
      }
    }
  }

  if (operationRecPtr.p->operation == ZSCAN_OP &&
      ! operationRecPtr.p->isAccLockReq) {
    jam();
    takeOutScanLockQueue(operationRecPtr.p->scanRecPtr);
    putReadyScanQueue(signal, operationRecPtr.p->scanRecPtr);
  } else {
    jam();
    sendAcckeyconf(signal);
    sendSignal(operationRecPtr.p->userblockref, GSN_ACCKEYCONF, 
	       signal, 6, JBB);
  }//if
  return operationRecPtr.p->elementIsDisappeared;
}//Dbacc::executeNextOperation()

/**
 * takeOutLockOwnersList
 *
 * Description: Take out an operation from the doubly linked 
 * lock owners list on the fragment.
 *
 */
void Dbacc::takeOutLockOwnersList(Signal* signal,
				  const OperationrecPtr& outOperPtr) 
{
  const Uint32 Tprev = outOperPtr.p->prevLockOwnerOp;
  const Uint32 Tnext = outOperPtr.p->nextLockOwnerOp;
  
#ifdef VM_TRACE
  // Check that operation is already in the list
  OperationrecPtr tmpOperPtr;
  bool inList = false;
  tmpOperPtr.i = fragrecptr.p->lockOwnersList;
  while (tmpOperPtr.i != RNIL){
    ptrCheckGuard(tmpOperPtr, coprecsize, operationrec);
    if (tmpOperPtr.i == outOperPtr.i)
      inList = true;
    tmpOperPtr.i = tmpOperPtr.p->nextLockOwnerOp;
  }
  ndbrequire(inList == true);
#endif
  
  ndbrequire(outOperPtr.p->lockOwner == ZTRUE);
  outOperPtr.p->lockOwner = ZFALSE;
  
  // Fast path through the code for the common case.
  if ((Tprev == RNIL) && (Tnext == RNIL)) {
    ndbrequire(fragrecptr.p->lockOwnersList == outOperPtr.i);
    fragrecptr.p->lockOwnersList = RNIL;
    return;
  } 

  // Check previous operation 
  if (Tprev != RNIL) {    
    jam();
    arrGuard(Tprev, coprecsize);
    operationrec[Tprev].nextLockOwnerOp = Tnext;
  } else {
    fragrecptr.p->lockOwnersList = Tnext;
  }//if

  // Check next operation
  if (Tnext == RNIL) {
    return;
  } else {
    jam();
    arrGuard(Tnext, coprecsize);
    operationrec[Tnext].prevLockOwnerOp = Tprev;
  }//if

  return;
}//Dbacc::takeOutLockOwnersList()

/**
 * insertLockOwnersList
 *
 * Description: Insert an operation first in the dubly linked lock owners 
 * list on the fragment.
 *
 */
void Dbacc::insertLockOwnersList(Signal* signal, 
				 const OperationrecPtr& insOperPtr) 
{
  OperationrecPtr tmpOperPtr;
  
#ifdef VM_TRACE
  // Check that operation is not already in list
  tmpOperPtr.i = fragrecptr.p->lockOwnersList;
  while(tmpOperPtr.i != RNIL){
    ptrCheckGuard(tmpOperPtr, coprecsize, operationrec);
    ndbrequire(tmpOperPtr.i != insOperPtr.i);
    tmpOperPtr.i = tmpOperPtr.p->nextLockOwnerOp;    
  }
#endif

  ndbrequire(insOperPtr.p->lockOwner == ZFALSE);

  insOperPtr.p->lockOwner = ZTRUE;
  insOperPtr.p->prevLockOwnerOp = RNIL;
  tmpOperPtr.i = fragrecptr.p->lockOwnersList;
  const Uint32 seq = fragrecptr.p->m_current_sequence_no;
  insOperPtr.p->nextLockOwnerOp = tmpOperPtr.i;
  insOperPtr.p->m_sequence_no = seq;
  
  fragrecptr.p->lockOwnersList = insOperPtr.i;
  fragrecptr.p->m_current_sequence_no = seq+1;
  if (tmpOperPtr.i == RNIL) {
    return;
  } else {
    jam();
    ptrCheckGuard(tmpOperPtr, coprecsize, operationrec);
    tmpOperPtr.p->prevLockOwnerOp = insOperPtr.i;
  }//if
}//Dbacc::insertLockOwnersList()


/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       END OF COMMIT AND ABORT MODULE                                              */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* ALLOC_OVERFLOW_PAGE                                                               */
/*          DESCRIPTION:                                                             */
/* --------------------------------------------------------------------------------- */
void Dbacc::allocOverflowPage(Signal* signal) 
{
  DirRangePtr aopDirRangePtr;
  DirectoryarrayPtr aopOverflowDirptr;
  OverflowRecordPtr aopOverflowRecPtr;
  Uint32 taopTmp1;
  Uint32 taopTmp2;
  Uint32 taopTmp3;

  tresult = 0;
  if ((cfirstfreepage == RNIL) &&
      (cfreepage >= cpagesize)) {
    jam();  
    zpagesize_error("Dbacc::allocOverflowPage");
    tresult = ZPAGESIZE_ERROR;
    return;
  }//if
  if (fragrecptr.p->firstFreeDirindexRec != RNIL) {
    jam();
    /* FRAGRECPTR:FIRST_FREE_DIRINDEX_REC POINTS  */
    /* TO THE FIRST ELEMENT IN A FREE LIST OF THE */
    /* DIRECTORY INDEX WICH HAVE NULL AS PAGE     */
    aopOverflowRecPtr.i = fragrecptr.p->firstFreeDirindexRec;
    ptrCheckGuard(aopOverflowRecPtr, coverflowrecsize, overflowRecord);
    troOverflowRecPtr.p = aopOverflowRecPtr.p;
    takeRecOutOfFreeOverdir(signal);
  } else if (cfirstfreeoverrec == RNIL) {
    jam();
    tresult = ZOVER_REC_ERROR;
    return;
  } else if ((cfirstfreedir == RNIL) &&
             (cdirarraysize <= cdirmemory)) {
    jam();
    tresult = ZDIRSIZE_ERROR;
    return;
  } else {
    jam();
    seizeOverRec(signal);
    aopOverflowRecPtr = sorOverflowRecPtr;
    aopOverflowRecPtr.p->dirindex = fragrecptr.p->lastOverIndex;
  }//if
  aopOverflowRecPtr.p->nextOverRec = RNIL;
  aopOverflowRecPtr.p->prevOverRec = RNIL;
  fragrecptr.p->firstOverflowRec = aopOverflowRecPtr.i;
  fragrecptr.p->lastOverflowRec = aopOverflowRecPtr.i;
  taopTmp1 = aopOverflowRecPtr.p->dirindex;
  aopDirRangePtr.i = fragrecptr.p->overflowdir;
  taopTmp2 = taopTmp1 >> 8;
  taopTmp3 = taopTmp1 & 0xff;
  ptrCheckGuard(aopDirRangePtr, cdirrangesize, dirRange);
  arrGuard(taopTmp2, 256);
  if (aopDirRangePtr.p->dirArray[taopTmp2] == RNIL) {
    jam();
    seizeDirectory(signal);
    ndbrequire(tresult <= ZLIMIT_OF_ERROR);
    aopDirRangePtr.p->dirArray[taopTmp2] = sdDirptr.i;
  }//if
  aopOverflowDirptr.i = aopDirRangePtr.p->dirArray[taopTmp2];
  seizePage(signal);
  ndbrequire(tresult <= ZLIMIT_OF_ERROR);
  ptrCheckGuard(aopOverflowDirptr, cdirarraysize, directoryarray);
  aopOverflowDirptr.p->pagep[taopTmp3] = spPageptr.i;
  tiopPageId = aopOverflowRecPtr.p->dirindex;
  iopOverflowRecPtr = aopOverflowRecPtr;
  iopPageptr = spPageptr;
  initOverpage(signal);
  aopOverflowRecPtr.p->overpage = spPageptr.i;
  if (fragrecptr.p->lastOverIndex <= aopOverflowRecPtr.p->dirindex) {
    jam();
    ndbrequire(fragrecptr.p->lastOverIndex == aopOverflowRecPtr.p->dirindex);
    fragrecptr.p->lastOverIndex++;
  }//if
}//Dbacc::allocOverflowPage()

/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       EXPAND/SHRINK MODULE                                                        */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* ******************--------------------------------------------------------------- */
/*EXPANDCHECK                                        EXPAND BUCKET ORD               */
/* SENDER: ACC,    LEVEL B         */
/*   INPUT:   FRAGRECPTR, POINTS TO A FRAGMENT RECORD.                               */
/*   DESCRIPTION: A BUCKET OF A FRAGMENT PAGE WILL BE EXPAND INTO TWO BUCKETS        */
/*                                 ACCORDING TO LH3.                                 */
/* ******************--------------------------------------------------------------- */
/* ******************--------------------------------------------------------------- */
/* EXPANDCHECK                                        EXPAND BUCKET ORD              */
/* ******************------------------------------+                                 */
/* SENDER: ACC,    LEVEL B         */
/* A BUCKET OF THE FRAGMENT WILL   */
/* BE EXPANDED ACORDING TO LH3,    */
/* AND COMMIT TRANSACTION PROCESS  */
/* WILL BE CONTINUED */
Uint32 Dbacc::checkScanExpand(Signal* signal)
{
  Uint32 Ti;
  Uint32 TreturnCode = 0;
  Uint32 TPageIndex;
  Uint32 TDirInd;
  Uint32 TSplit;
  Uint32 TreleaseInd = 0;
  Uint32 TreleaseScanBucket;
  Uint32 TreleaseScanIndicator[4];
  DirectoryarrayPtr TDirptr;
  DirRangePtr TDirRangePtr;
  Page8Ptr TPageptr;
  ScanRecPtr TscanPtr;

  TSplit = fragrecptr.p->p;
  for (Ti = 0; Ti < 4; Ti++) {
    TreleaseScanIndicator[Ti] = 0;
    if (fragrecptr.p->scan[Ti] != RNIL) {
      //-------------------------------------------------------------
      // A scan is ongoing on this particular local fragment. We have
      // to check its current state.
      //-------------------------------------------------------------
      TscanPtr.i = fragrecptr.p->scan[Ti];
      ptrCheckGuard(TscanPtr, cscanRecSize, scanRec);
      if (TscanPtr.p->activeLocalFrag == fragrecptr.i) {
        if (TscanPtr.p->scanBucketState ==  ScanRec::FIRST_LAP) {
          if (TSplit == TscanPtr.p->nextBucketIndex) {
            jam();
	    //-------------------------------------------------------------
	    // We are currently scanning this bucket. We cannot split it
	    // simultaneously with the scan. We have to pass this offer for
	    // splitting the bucket.
	    //-------------------------------------------------------------
            TreturnCode = 1;
            return TreturnCode;
          } else if (TSplit > TscanPtr.p->nextBucketIndex) {
            jam();
	    //-------------------------------------------------------------
	    // This bucket has not yet been scanned. We must reset the scanned
	    // bit indicator for this scan on this bucket.
	    //-------------------------------------------------------------
            TreleaseScanIndicator[Ti] = 1;
            TreleaseInd = 1;
          } else {
            jam();
          }//if
        } else if (TscanPtr.p->scanBucketState ==  ScanRec::SECOND_LAP) {
          jam();
	  //-------------------------------------------------------------
	  // We are performing a second lap to handle buckets that was
	  // merged during the first lap of scanning. During this second
	  // lap we do not allow any splits or merges.
	  //-------------------------------------------------------------
          TreturnCode = 1;
          return TreturnCode;
        } else {
          ndbrequire(TscanPtr.p->scanBucketState ==  ScanRec::SCAN_COMPLETED);
          jam();
	  //-------------------------------------------------------------
	  // The scan is completed and we can thus go ahead and perform
	  // the split.
	  //-------------------------------------------------------------
        }//if
      }//if
    }//if
  }//for
  if (TreleaseInd == 1) {
    TreleaseScanBucket = TSplit;
    TDirRangePtr.i = fragrecptr.p->directory;
    TPageIndex = TreleaseScanBucket & ((1 << fragrecptr.p->k) - 1);	/* PAGE INDEX OBS K = 6 */
    TDirInd = TreleaseScanBucket >> fragrecptr.p->k;	/* DIRECTORY INDEX OBS K = 6 */
    ptrCheckGuard(TDirRangePtr, cdirrangesize, dirRange);
    arrGuard((TDirInd >> 8), 256);
    TDirptr.i = TDirRangePtr.p->dirArray[TDirInd >> 8];
    ptrCheckGuard(TDirptr, cdirarraysize, directoryarray);
    TPageptr.i = TDirptr.p->pagep[TDirInd & 0xff];
    ptrCheckGuard(TPageptr, cpagesize, page8);
    for (Ti = 0; Ti < 4; Ti++) {
      if (TreleaseScanIndicator[Ti] == 1) {
        jam();
        scanPtr.i = fragrecptr.p->scan[Ti];
        ptrCheckGuard(scanPtr, cscanRecSize, scanRec);
        rsbPageidptr = TPageptr;
        trsbPageindex = TPageIndex;
        releaseScanBucket(signal);
      }//if
    }//for
  }//if
  return TreturnCode;
}//Dbacc::checkScanExpand()

void Dbacc::execEXPANDCHECK2(Signal* signal) 
{
  jamEntry();

  if(refToBlock(signal->getSendersBlockRef()) == DBLQH){
    jam();
    reenable_expand_after_redo_log_exection_complete(signal);
    return;
  }

  DirectoryarrayPtr newDirptr;

  fragrecptr.i = signal->theData[0];
  tresult = 0;	/* 0= FALSE,1= TRUE,> ZLIMIT_OF_ERROR =ERRORCODE */
  Uint32 tmp = 1;
  tmp = tmp << 31;
  ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
  fragrecptr.p->expandFlag = 0;
  if (fragrecptr.p->slack < tmp) {
    jam();
    /* IT MEANS THAT IF SLACK > ZERO */
    /*--------------------------------------------------------------*/
    /*       THE SLACK HAS IMPROVED AND IS NOW ACCEPTABLE AND WE    */
    /*       CAN FORGET ABOUT THE EXPAND PROCESS.                   */
    /*--------------------------------------------------------------*/
    return;
  }//if
  if (fragrecptr.p->firstOverflowRec == RNIL) {
    jam();
    allocOverflowPage(signal);
    if (tresult > ZLIMIT_OF_ERROR) {
      jam();
      /*--------------------------------------------------------------*/
      /* WE COULD NOT ALLOCATE ANY OVERFLOW PAGE. THUS WE HAVE TO STOP*/
      /* THE EXPAND SINCE WE CANNOT GUARANTEE ITS COMPLETION.         */
      /*--------------------------------------------------------------*/
      return;
    }//if
  }//if
  if (cfirstfreepage == RNIL) {
    if (cfreepage >= cpagesize) {
      jam();
      /*--------------------------------------------------------------*/
      /* WE HAVE TO STOP THE EXPAND PROCESS SINCE THERE ARE NO FREE   */
      /* PAGES. THIS MEANS THAT WE COULD BE FORCED TO CRASH SINCE WE  */
      /* CANNOT COMPLETE THE EXPAND. TO AVOID THE CRASH WE EXIT HERE. */
      /*--------------------------------------------------------------*/
      return;
    }//if
  }//if
  if (checkScanExpand(signal) == 1) {
    jam();
    /*--------------------------------------------------------------*/
    // A scan state was inconsistent with performing an expand
    // operation.
    /*--------------------------------------------------------------*/
    return;
  }//if

  /*--------------------------------------------------------------------------*/
  /*       WE START BY FINDING THE PAGE, THE PAGE INDEX AND THE PAGE DIRECTORY*/
  /*       OF THE NEW BUCKET WHICH SHALL RECEIVE THE ELEMENT WHICH HAVE A 1 IN*/
  /*       THE NEXT HASH BIT. THIS BIT IS USED IN THE SPLIT MECHANISM TO      */
  /*       DECIDE WHICH ELEMENT GOES WHERE.                                   */
  /*--------------------------------------------------------------------------*/
  expDirRangePtr.i = fragrecptr.p->directory;
  texpReceivedBucket = (fragrecptr.p->maxp + fragrecptr.p->p) + 1;	/* RECEIVED BUCKET */
  texpDirInd = texpReceivedBucket >> fragrecptr.p->k;
  newDirptr.i = RNIL;
  ptrNull(newDirptr);
  texpDirRangeIndex = texpDirInd >> 8;
  ptrCheckGuard(expDirRangePtr, cdirrangesize, dirRange);
  arrGuard(texpDirRangeIndex, 256);
  expDirptr.i = expDirRangePtr.p->dirArray[texpDirRangeIndex];
  if (expDirptr.i == RNIL) {
    jam();
    seizeDirectory(signal);
    if (tresult > ZLIMIT_OF_ERROR) {
      jam();
      return;
    } else {
      jam();
      newDirptr = sdDirptr;
      expDirptr = sdDirptr;
      expDirRangePtr.p->dirArray[texpDirRangeIndex] = sdDirptr.i;
    }//if
  } else {
    ptrCheckGuard(expDirptr, cdirarraysize, directoryarray);
  }//if
  texpDirPageIndex = texpDirInd & 0xff;
  expPageptr.i = expDirptr.p->pagep[texpDirPageIndex];
  if (expPageptr.i == RNIL) {
    jam();
    seizePage(signal);
    if (tresult > ZLIMIT_OF_ERROR) {
      jam();
      if (newDirptr.i != RNIL) {
        jam();
        rdDirptr.i = newDirptr.i;
        releaseDirectory(signal);
      }//if
      return;
    }//if
    expDirptr.p->pagep[texpDirPageIndex] = spPageptr.i;
    tipPageId = texpDirInd;
    inpPageptr = spPageptr;
    initPage(signal);
    fragrecptr.p->dirsize++;
    expPageptr = spPageptr;
  } else {
    ptrCheckGuard(expPageptr, cpagesize, page8);
  }//if

  fragrecptr.p->expReceivePageptr = expPageptr.i;
  fragrecptr.p->expReceiveIndex = texpReceivedBucket & ((1 << fragrecptr.p->k) - 1);
  /*--------------------------------------------------------------------------*/
  /*       THE NEXT ACTION IS TO FIND THE PAGE, THE PAGE INDEX AND THE PAGE   */
  /*       DIRECTORY OF THE BUCKET TO BE SPLIT.                               */
  /*--------------------------------------------------------------------------*/
  expDirRangePtr.i = fragrecptr.p->directory;
  cexcPageindex = fragrecptr.p->p & ((1 << fragrecptr.p->k) - 1);	/* PAGE INDEX OBS K = 6 */
  texpDirInd = fragrecptr.p->p >> fragrecptr.p->k;	/* DIRECTORY INDEX OBS K = 6 */
  ptrCheckGuard(expDirRangePtr, cdirrangesize, dirRange);
  arrGuard((texpDirInd >> 8), 256);
  expDirptr.i = expDirRangePtr.p->dirArray[texpDirInd >> 8];
  ptrCheckGuard(expDirptr, cdirarraysize, directoryarray);
  excPageptr.i = expDirptr.p->pagep[texpDirInd & 0xff];
  fragrecptr.p->expSenderIndex = cexcPageindex;
  fragrecptr.p->expSenderPageptr = excPageptr.i;
  if (excPageptr.i == RNIL) {
    jam();
    endofexpLab(signal);	/* EMPTY BUCKET */
    return;
  }//if
  fragrecptr.p->expReceiveForward = ZTRUE;
  ptrCheckGuard(excPageptr, cpagesize, page8);
  expandcontainer(signal);
  endofexpLab(signal);
  return;
}//Dbacc::execEXPANDCHECK2()
  
void Dbacc::endofexpLab(Signal* signal) 
{
  fragrecptr.p->p++;
  fragrecptr.p->slack += fragrecptr.p->maxloadfactor;
  fragrecptr.p->expandCounter++;
  if (fragrecptr.p->p > fragrecptr.p->maxp) {
    jam();
    fragrecptr.p->maxp = (fragrecptr.p->maxp << 1) | 1;
    fragrecptr.p->lhdirbits++;
    fragrecptr.p->hashcheckbit++;
    fragrecptr.p->p = 0;
  }//if
  Uint32 noOfBuckets = (fragrecptr.p->maxp + 1) + fragrecptr.p->p;
  Uint32 Thysteres = fragrecptr.p->maxloadfactor - fragrecptr.p->minloadfactor;
  fragrecptr.p->slackCheck = noOfBuckets * Thysteres;
  if (fragrecptr.p->slack > (1u << 31)) {
    jam();
    /* IT MEANS THAT IF SLACK < ZERO */
    /* --------------------------------------------------------------------------------- */
    /*       IT IS STILL NECESSARY TO EXPAND THE FRAGMENT EVEN MORE. START IT FROM HERE  */
    /*       WITHOUT WAITING FOR NEXT COMMIT ON THE FRAGMENT.                            */
    /* --------------------------------------------------------------------------------- */
    fragrecptr.p->expandFlag = 2;
    signal->theData[0] = fragrecptr.i;
    signal->theData[1] = fragrecptr.p->p;
    signal->theData[2] = fragrecptr.p->maxp;
    sendSignal(cownBlockref, GSN_EXPANDCHECK2, signal, 3, JBB);
  }//if
  return;
}//Dbacc::endofexpLab()

void Dbacc::reenable_expand_after_redo_log_exection_complete(Signal* signal){

  tabptr.i = signal->theData[0];
  Uint32 fragId = signal->theData[1];

  ptrCheckGuard(tabptr, ctablesize, tabrec);
  ndbrequire(getfragmentrec(signal, fragrecptr, fragId));
#if 0
  ndbout_c("reenable expand check for table %d fragment: %d", 
	   tabptr.i, fragId);
#endif

  switch(fragrecptr.p->expandFlag){
  case 0:
    /**
     * Hmm... this means that it's alreay has been reenabled...
     */
    fragrecptr.p->expandFlag = 1;
    break;
  case 1:
    /**
     * Nothing is going on start expand check
     */
  case 2:
    /**
     * A shrink is running, do expand check anyway
     *  (to reset expandFlag)
     */
    fragrecptr.p->expandFlag = 2; 
    signal->theData[0] = fragrecptr.i;
    signal->theData[1] = fragrecptr.p->p;
    signal->theData[2] = fragrecptr.p->maxp;
    sendSignal(cownBlockref, GSN_EXPANDCHECK2, signal, 3, JBB);
    break;
  }
}

void Dbacc::execDEBUG_SIG(Signal* signal) 
{
  jamEntry();
  expPageptr.i = signal->theData[0];

  progError(__LINE__, NDBD_EXIT_SR_UNDOLOG);
  return;
}//Dbacc::execDEBUG_SIG()

/* --------------------------------------------------------------------------------- */
/* EXPANDCONTAINER                                                                   */
/*        INPUT: EXC_PAGEPTR (POINTER TO THE ACTIVE PAGE RECORD)                     */
/*               CEXC_PAGEINDEX (INDEX OF THE BUCKET).                               */
/*                                                                                   */
/*        DESCRIPTION: THE HASH VALUE OF ALL ELEMENTS IN THE CONTAINER WILL BE       */
/*                  CHECKED. SOME OF THIS ELEMENTS HAVE TO MOVE TO THE NEW CONTAINER */
/* --------------------------------------------------------------------------------- */
void Dbacc::expandcontainer(Signal* signal) 
{
  Uint32 texcHashvalue;
  Uint32 texcTmp;
  Uint32 texcIndex;
  Uint32 guard20;

  cexcPrevpageptr = RNIL;
  cexcPrevconptr = 0;
  cexcForward = ZTRUE;
 EXP_CONTAINER_LOOP:
  cexcContainerptr = (cexcPageindex << ZSHIFT_PLUS) - (cexcPageindex << ZSHIFT_MINUS);
  if (cexcForward == ZTRUE) {
    jam();
    cexcContainerptr = cexcContainerptr + ZHEAD_SIZE;
    cexcElementptr = cexcContainerptr + ZCON_HEAD_SIZE;
  } else {
    jam();
    cexcContainerptr = ((cexcContainerptr + ZHEAD_SIZE) + ZBUF_SIZE) - ZCON_HEAD_SIZE;
    cexcElementptr = cexcContainerptr - 1;
  }//if
  arrGuard(cexcContainerptr, 2048);
  cexcContainerhead = excPageptr.p->word32[cexcContainerptr];
  cexcContainerlen = cexcContainerhead >> 26;
  cexcMovedLen = ZCON_HEAD_SIZE;
  if (cexcContainerlen <= ZCON_HEAD_SIZE) {
    ndbrequire(cexcContainerlen >= ZCON_HEAD_SIZE);
    jam();
    goto NEXT_ELEMENT;
  }//if
 NEXT_ELEMENT_LOOP:
  idrOperationRecPtr.i = RNIL;
  ptrNull(idrOperationRecPtr);
  /* --------------------------------------------------------------------------------- */
  /*       CEXC_PAGEINDEX         PAGE INDEX OF CURRENT CONTAINER BEING EXAMINED.      */
  /*       CEXC_CONTAINERPTR      INDEX OF CURRENT CONTAINER BEING EXAMINED.           */
  /*       CEXC_ELEMENTPTR        INDEX OF CURRENT ELEMENT BEING EXAMINED.             */
  /*       EXC_PAGEPTR            PAGE WHERE CURRENT ELEMENT RESIDES.                  */
  /*       CEXC_PREVPAGEPTR        PAGE OF PREVIOUS CONTAINER.                         */
  /*       CEXC_PREVCONPTR        INDEX OF PREVIOUS CONTAINER                          */
  /*       CEXC_FORWARD           DIRECTION OF CURRENT CONTAINER                       */
  /* --------------------------------------------------------------------------------- */
  arrGuard(cexcElementptr, 2048);
  tidrElemhead = excPageptr.p->word32[cexcElementptr];
  if (ElementHeader::getUnlocked(tidrElemhead)){
    jam();
    texcHashvalue = ElementHeader::getHashValuePart(tidrElemhead);
  } else {
    jam();
    idrOperationRecPtr.i = ElementHeader::getOpPtrI(tidrElemhead);
    ptrCheckGuard(idrOperationRecPtr, coprecsize, operationrec);
    texcHashvalue = idrOperationRecPtr.p->hashvaluePart;
  }//if
  if (((texcHashvalue >> fragrecptr.p->hashcheckbit) & 1) == 0) {
    jam();
    /* --------------------------------------------------------------------------------- */
    /*       THIS ELEMENT IS NOT TO BE MOVED. WE CALCULATE THE WHEREABOUTS OF THE NEXT   */
    /*       ELEMENT AND PROCEED WITH THAT OR END THE SEARCH IF THERE ARE NO MORE        */
    /*       ELEMENTS IN THIS CONTAINER.                                                 */
    /* --------------------------------------------------------------------------------- */
    goto NEXT_ELEMENT;
  }//if
  /* --------------------------------------------------------------------------------- */
  /*       THE HASH BIT WAS SET AND WE SHALL MOVE THIS ELEMENT TO THE NEW BUCKET.      */
  /*       WE START BY READING THE ELEMENT TO BE ABLE TO INSERT IT INTO THE NEW BUCKET.*/
  /*       THEN WE INSERT THE ELEMENT INTO THE NEW BUCKET. THE NEXT STEP IS TO DELETE  */
  /*       THE ELEMENT FROM THIS BUCKET. THIS IS PERFORMED BY REPLACING IT WITH THE    */
  /*       LAST ELEMENT IN THE BUCKET. IF THIS ELEMENT IS TO BE MOVED WE MOVE IT AND   */
  /*       GET THE LAST ELEMENT AGAIN UNTIL WE EITHER FIND ONE THAT STAYS OR THIS      */
  /*       ELEMENT IS THE LAST ELEMENT.                                                */
  /* --------------------------------------------------------------------------------- */
  texcTmp = cexcElementptr + cexcForward;
  guard20 = fragrecptr.p->localkeylen - 1;
  for (texcIndex = 0; texcIndex <= guard20; texcIndex++) {
    arrGuard(texcIndex, 2);
    arrGuard(texcTmp, 2048);
    clocalkey[texcIndex] = excPageptr.p->word32[texcTmp];
    texcTmp = texcTmp + cexcForward;
  }//for
  tidrPageindex = fragrecptr.p->expReceiveIndex;
  idrPageptr.i = fragrecptr.p->expReceivePageptr;
  ptrCheckGuard(idrPageptr, cpagesize, page8);
  tidrForward = fragrecptr.p->expReceiveForward;
  insertElement(signal);
  fragrecptr.p->expReceiveIndex = tidrPageindex;
  fragrecptr.p->expReceivePageptr = idrPageptr.i;
  fragrecptr.p->expReceiveForward = tidrForward;
 REMOVE_LAST_LOOP:
  jam();
  lastPageptr.i = excPageptr.i;
  lastPageptr.p = excPageptr.p;
  tlastContainerptr = cexcContainerptr;
  lastPrevpageptr.i = cexcPrevpageptr;
  ptrCheck(lastPrevpageptr, cpagesize, page8);
  tlastPrevconptr = cexcPrevconptr;
  arrGuard(tlastContainerptr, 2048);
  tlastContainerhead = lastPageptr.p->word32[tlastContainerptr];
  tlastContainerlen = tlastContainerhead >> 26;
  tlastForward = cexcForward;
  tlastPageindex = cexcPageindex;
  getLastAndRemove(signal);
  if (excPageptr.i == lastPageptr.i) {
    if (cexcElementptr == tlastElementptr) {
      jam();
      /* --------------------------------------------------------------------------------- */
      /*       THE CURRENT ELEMENT WAS ALSO THE LAST ELEMENT.                              */
      /* --------------------------------------------------------------------------------- */
      return;
    }//if
  }//if
  /* --------------------------------------------------------------------------------- */
  /*       THE CURRENT ELEMENT WAS NOT THE LAST ELEMENT. IF THE LAST ELEMENT SHOULD    */
  /*       STAY WE COPY IT TO THE POSITION OF THE CURRENT ELEMENT, OTHERWISE WE INSERT */
  /*       INTO THE NEW BUCKET, REMOVE IT AND TRY WITH THE NEW LAST ELEMENT.           */
  /* --------------------------------------------------------------------------------- */
  idrOperationRecPtr.i = RNIL;
  ptrNull(idrOperationRecPtr);
  arrGuard(tlastElementptr, 2048);
  tidrElemhead = lastPageptr.p->word32[tlastElementptr];
  if (ElementHeader::getUnlocked(tidrElemhead)) {
    jam();
    texcHashvalue = ElementHeader::getHashValuePart(tidrElemhead);
  } else {
    jam();
    idrOperationRecPtr.i = ElementHeader::getOpPtrI(tidrElemhead);
    ptrCheckGuard(idrOperationRecPtr, coprecsize, operationrec);
    texcHashvalue = idrOperationRecPtr.p->hashvaluePart;
  }//if
  if (((texcHashvalue >> fragrecptr.p->hashcheckbit) & 1) == 0) {
    jam();
    /* --------------------------------------------------------------------------------- */
    /*       THE LAST ELEMENT IS NOT TO BE MOVED. WE COPY IT TO THE CURRENT ELEMENT.     */
    /* --------------------------------------------------------------------------------- */
    delPageptr = excPageptr;
    tdelContainerptr = cexcContainerptr;
    tdelForward = cexcForward;
    tdelElementptr = cexcElementptr;
    deleteElement(signal);
  } else {
    jam();
    /* --------------------------------------------------------------------------------- */
    /*       THE LAST ELEMENT IS ALSO TO BE MOVED.                                       */
    /* --------------------------------------------------------------------------------- */
    texcTmp = tlastElementptr + tlastForward;
    for (texcIndex = 0; texcIndex < fragrecptr.p->localkeylen; texcIndex++) {
      arrGuard(texcIndex, 2);
      arrGuard(texcTmp, 2048);
      clocalkey[texcIndex] = lastPageptr.p->word32[texcTmp];
      texcTmp = texcTmp + tlastForward;
    }//for
    tidrPageindex = fragrecptr.p->expReceiveIndex;
    idrPageptr.i = fragrecptr.p->expReceivePageptr;
    ptrCheckGuard(idrPageptr, cpagesize, page8);
    tidrForward = fragrecptr.p->expReceiveForward;
    insertElement(signal);
    fragrecptr.p->expReceiveIndex = tidrPageindex;
    fragrecptr.p->expReceivePageptr = idrPageptr.i;
    fragrecptr.p->expReceiveForward = tidrForward;
    goto REMOVE_LAST_LOOP;
  }//if
 NEXT_ELEMENT:
  arrGuard(cexcContainerptr, 2048);
  cexcContainerhead = excPageptr.p->word32[cexcContainerptr];
  cexcMovedLen = cexcMovedLen + fragrecptr.p->elementLength;
  if ((cexcContainerhead >> 26) > cexcMovedLen) {
    jam();
    /* --------------------------------------------------------------------------------- */
    /*       WE HAVE NOT YET MOVED THE COMPLETE CONTAINER. WE PROCEED WITH THE NEXT      */
    /*       ELEMENT IN THE CONTAINER. IT IS IMPORTANT TO READ THE CONTAINER LENGTH      */
    /*       FROM THE CONTAINER HEADER SINCE IT MIGHT CHANGE BY REMOVING THE LAST        */
    /*       ELEMENT IN THE BUCKET.                                                      */
    /* --------------------------------------------------------------------------------- */
    cexcElementptr = cexcElementptr + (cexcForward * fragrecptr.p->elementLength);
    goto NEXT_ELEMENT_LOOP;
  }//if
  if (((cexcContainerhead >> 7) & 3) != 0) {
    jam();
    /* --------------------------------------------------------------------------------- */
    /*       WE PROCEED TO THE NEXT CONTAINER IN THE BUCKET.                             */
    /* --------------------------------------------------------------------------------- */
    cexcPrevpageptr = excPageptr.i;
    cexcPrevconptr = cexcContainerptr;
    nextcontainerinfoExp(signal);
    goto EXP_CONTAINER_LOOP;
  }//if
}//Dbacc::expandcontainer()

/* ******************--------------------------------------------------------------- */
/* SHRINKCHECK                                        JOIN BUCKET ORD                */
/*                                                   SENDER: ACC,    LEVEL B         */
/*   INPUT:   FRAGRECPTR, POINTS TO A FRAGMENT RECORD.                               */
/*   DESCRIPTION: TWO BUCKET OF A FRAGMENT PAGE WILL BE JOINED TOGETHER              */
/*                                 ACCORDING TO LH3.                                 */
/* ******************--------------------------------------------------------------- */
/* ******************--------------------------------------------------------------- */
/* SHRINKCHECK                                            JOIN BUCKET ORD            */
/* ******************------------------------------+                                 */
/*   SENDER: ACC,    LEVEL B       */
/* TWO BUCKETS OF THE FRAGMENT     */
/* WILL BE JOINED  ACORDING TO LH3 */
/* AND COMMIT TRANSACTION PROCESS  */
/* WILL BE CONTINUED */
Uint32 Dbacc::checkScanShrink(Signal* signal)
{
  Uint32 Ti;
  Uint32 TreturnCode = 0;
  Uint32 TPageIndex;
  Uint32 TDirInd;
  Uint32 TmergeDest;
  Uint32 TmergeSource;
  Uint32 TreleaseScanBucket;
  Uint32 TreleaseInd = 0;
  Uint32 TreleaseScanIndicator[4];
  DirectoryarrayPtr TDirptr;
  DirRangePtr TDirRangePtr;
  Page8Ptr TPageptr;
  ScanRecPtr TscanPtr;

  if (fragrecptr.p->p == 0) {
    jam();
    TmergeDest = fragrecptr.p->maxp >> 1;
  } else {
    jam();
    TmergeDest = fragrecptr.p->p - 1;
  }//if
  TmergeSource = fragrecptr.p->maxp + fragrecptr.p->p;
  for (Ti = 0; Ti < 4; Ti++) {
    TreleaseScanIndicator[Ti] = 0;
    if (fragrecptr.p->scan[Ti] != RNIL) {
      TscanPtr.i = fragrecptr.p->scan[Ti];
      ptrCheckGuard(TscanPtr, cscanRecSize, scanRec);
      if (TscanPtr.p->activeLocalFrag == fragrecptr.i) {
	//-------------------------------------------------------------
	// A scan is ongoing on this particular local fragment. We have
	// to check its current state.
	//-------------------------------------------------------------
        if (TscanPtr.p->scanBucketState ==  ScanRec::FIRST_LAP) {
          jam();
          if ((TmergeDest == TscanPtr.p->nextBucketIndex) ||
              (TmergeSource == TscanPtr.p->nextBucketIndex)) {
            jam();
	    //-------------------------------------------------------------
	    // We are currently scanning one of the buckets involved in the
	    // merge. We cannot merge while simultaneously performing a scan.
	    // We have to pass this offer for merging the buckets.
	    //-------------------------------------------------------------
            TreturnCode = 1;
            return TreturnCode;
          } else if (TmergeDest < TscanPtr.p->nextBucketIndex) {
            jam();
            TreleaseScanIndicator[Ti] = 1;
            TreleaseInd = 1;
          }//if
        } else if (TscanPtr.p->scanBucketState ==  ScanRec::SECOND_LAP) {
          jam();
	  //-------------------------------------------------------------
	  // We are performing a second lap to handle buckets that was
	  // merged during the first lap of scanning. During this second
	  // lap we do not allow any splits or merges.
	  //-------------------------------------------------------------
          TreturnCode = 1;
          return TreturnCode;
        } else if (TscanPtr.p->scanBucketState ==  ScanRec::SCAN_COMPLETED) {
          jam();
	  //-------------------------------------------------------------
	  // The scan is completed and we can thus go ahead and perform
	  // the split.
	  //-------------------------------------------------------------
        } else {
          jam();
          sendSystemerror(signal, __LINE__);
          return TreturnCode;
        }//if
      }//if
    }//if
  }//for
  if (TreleaseInd == 1) {
    jam();
    TreleaseScanBucket = TmergeSource;
    TDirRangePtr.i = fragrecptr.p->directory;
    TPageIndex = TreleaseScanBucket & ((1 << fragrecptr.p->k) - 1);	/* PAGE INDEX OBS K = 6 */
    TDirInd = TreleaseScanBucket >> fragrecptr.p->k;	/* DIRECTORY INDEX OBS K = 6 */
    ptrCheckGuard(TDirRangePtr, cdirrangesize, dirRange);
    arrGuard((TDirInd >> 8), 256);
    TDirptr.i = TDirRangePtr.p->dirArray[TDirInd >> 8];
    ptrCheckGuard(TDirptr, cdirarraysize, directoryarray);
    TPageptr.i = TDirptr.p->pagep[TDirInd & 0xff];
    ptrCheckGuard(TPageptr, cpagesize, page8);
    for (Ti = 0; Ti < 4; Ti++) {
      if (TreleaseScanIndicator[Ti] == 1) {
        jam();
        scanPtr.i = fragrecptr.p->scan[Ti];
        ptrCheckGuard(scanPtr, cscanRecSize, scanRec);
        rsbPageidptr.i = TPageptr.i;
        rsbPageidptr.p = TPageptr.p;
        trsbPageindex = TPageIndex;
        releaseScanBucket(signal);
        if (TmergeDest < scanPtr.p->minBucketIndexToRescan) {
          jam();
	  //-------------------------------------------------------------
	  // We have to keep track of the starting bucket to Rescan in the
	  // second lap.
	  //-------------------------------------------------------------
          scanPtr.p->minBucketIndexToRescan = TmergeDest;
        }//if
        if (TmergeDest > scanPtr.p->maxBucketIndexToRescan) {
          jam();
	  //-------------------------------------------------------------
	  // We have to keep track of the ending bucket to Rescan in the
	  // second lap.
	  //-------------------------------------------------------------
          scanPtr.p->maxBucketIndexToRescan = TmergeDest;
        }//if
      }//if
    }//for
  }//if
  return TreturnCode;
}//Dbacc::checkScanShrink()

void Dbacc::execSHRINKCHECK2(Signal* signal) 
{
  Uint32 tshrTmp1;

  jamEntry();
  fragrecptr.i = signal->theData[0];
  Uint32 oldFlag = signal->theData[3];
  ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
  fragrecptr.p->expandFlag = oldFlag;
  tresult = 0;	/* 0= FALSE,1= TRUE,> ZLIMIT_OF_ERROR =ERRORCODE */
  if (fragrecptr.p->slack <= fragrecptr.p->slackCheck) {
    jam();
    /* TIME FOR JOIN BUCKETS PROCESS */
    /*--------------------------------------------------------------*/
    /*       NO LONGER NECESSARY TO SHRINK THE FRAGMENT.            */
    /*--------------------------------------------------------------*/
    return;
  }//if
  if (fragrecptr.p->slack > (1u << 31)) {
    jam();
    /*--------------------------------------------------------------*/
    /* THE SLACK IS NEGATIVE, IN THIS CASE WE WILL NOT NEED ANY     */
    /* SHRINK.                                                      */
    /*--------------------------------------------------------------*/
    return;
  }//if
  texpDirInd = (fragrecptr.p->maxp + fragrecptr.p->p) >> fragrecptr.p->k;
  if (fragrecptr.p->firstOverflowRec == RNIL) {
    jam();
    allocOverflowPage(signal);
    if (tresult > ZLIMIT_OF_ERROR) {
      jam();
      return;
    }//if
  }//if
  if (cfirstfreepage == RNIL) {
    if (cfreepage >= cpagesize) {
      jam();
      /*--------------------------------------------------------------*/
      /* WE HAVE TO STOP THE SHRINK PROCESS SINCE THERE ARE NO FREE   */
      /* PAGES. THIS MEANS THAT WE COULD BE FORCED TO CRASH SINCE WE  */
      /* CANNOT COMPLETE THE SHRINK. TO AVOID THE CRASH WE EXIT HERE. */
      /*--------------------------------------------------------------*/
      return;
    }//if
  }//if
  if (checkScanShrink(signal) == 1) {
    jam();
    /*--------------------------------------------------------------*/
    // A scan state was inconsistent with performing a shrink
    // operation.
    /*--------------------------------------------------------------*/
    return;
  }//if
  if (fragrecptr.p->p == 0) {
    jam();
    fragrecptr.p->maxp = fragrecptr.p->maxp >> 1;
    fragrecptr.p->p = fragrecptr.p->maxp;
    fragrecptr.p->lhdirbits--;
    fragrecptr.p->hashcheckbit--;
  } else {
    jam();
    fragrecptr.p->p--;
  }//if

  /*--------------------------------------------------------------------------*/
  /*       WE START BY FINDING THE NECESSARY INFORMATION OF THE BUCKET TO BE  */
  /*       REMOVED WHICH WILL SEND ITS ELEMENTS TO THE RECEIVING BUCKET.      */
  /*--------------------------------------------------------------------------*/
  expDirRangePtr.i = fragrecptr.p->directory;
  cexcPageindex = ((fragrecptr.p->maxp + fragrecptr.p->p) + 1) & ((1 << fragrecptr.p->k) - 1);
  texpDirInd = ((fragrecptr.p->maxp + fragrecptr.p->p) + 1) >> fragrecptr.p->k;
  texpDirRangeIndex = texpDirInd >> 8;
  texpDirPageIndex = texpDirInd & 0xff;
  ptrCheckGuard(expDirRangePtr, cdirrangesize, dirRange);
  arrGuard(texpDirRangeIndex, 256);
  expDirptr.i = expDirRangePtr.p->dirArray[texpDirRangeIndex];
  ptrCheckGuard(expDirptr, cdirarraysize, directoryarray);
  excPageptr.i = expDirptr.p->pagep[texpDirPageIndex];
  fragrecptr.p->expSenderDirptr = expDirptr.i;
  fragrecptr.p->expSenderIndex = cexcPageindex;
  fragrecptr.p->expSenderPageptr = excPageptr.i;
  fragrecptr.p->expSenderDirIndex = texpDirInd;
  /*--------------------------------------------------------------------------*/
  /*       WE NOW PROCEED BY FINDING THE NECESSARY INFORMATION ABOUT THE      */
  /*       RECEIVING BUCKET.                                                  */
  /*--------------------------------------------------------------------------*/
  expDirRangePtr.i = fragrecptr.p->directory;
  texpReceivedBucket = fragrecptr.p->p >> fragrecptr.p->k;
  ptrCheckGuard(expDirRangePtr, cdirrangesize, dirRange);
  arrGuard((texpReceivedBucket >> 8), 256);
  expDirptr.i = expDirRangePtr.p->dirArray[texpReceivedBucket >> 8];
  ptrCheckGuard(expDirptr, cdirarraysize, directoryarray);
  fragrecptr.p->expReceivePageptr = expDirptr.p->pagep[texpReceivedBucket & 0xff];
  fragrecptr.p->expReceiveIndex = fragrecptr.p->p & ((1 << fragrecptr.p->k) - 1);
  fragrecptr.p->expReceiveForward = ZTRUE;
  if (excPageptr.i == RNIL) {
    jam();
    endofshrinkbucketLab(signal);	/* EMPTY BUCKET */
    return;
  }//if
  /*--------------------------------------------------------------------------*/
  /*       INITIALISE THE VARIABLES FOR THE SHRINK PROCESS.                   */
  /*--------------------------------------------------------------------------*/
  ptrCheckGuard(excPageptr, cpagesize, page8);
  cexcForward = ZTRUE;
  cexcContainerptr = (cexcPageindex << ZSHIFT_PLUS) - (cexcPageindex << ZSHIFT_MINUS);
  cexcContainerptr = cexcContainerptr + ZHEAD_SIZE;
  arrGuard(cexcContainerptr, 2048);
  cexcContainerhead = excPageptr.p->word32[cexcContainerptr];
  cexcContainerlen = cexcContainerhead >> 26;
  if (cexcContainerlen <= ZCON_HEAD_SIZE) {
    ndbrequire(cexcContainerlen == ZCON_HEAD_SIZE);
  } else {
    jam();
    shrinkcontainer(signal);
  }//if
  /*--------------------------------------------------------------------------*/
  /*       THIS CONTAINER IS NOT YET EMPTY AND WE REMOVE ALL THE ELEMENTS.    */
  /*--------------------------------------------------------------------------*/
  if (((cexcContainerhead >> 10) & 1) == 1) {
    jam();
    rlPageptr = excPageptr;
    trlPageindex = cexcPageindex;
    trlRelCon = ZFALSE;
    turlIndex = cexcContainerptr + (ZBUF_SIZE - ZCON_HEAD_SIZE);
    releaseRightlist(signal);
  }//if
  tshrTmp1 = ZCON_HEAD_SIZE;
  tshrTmp1 = tshrTmp1 << 26;
  dbgWord32(excPageptr, cexcContainerptr, tshrTmp1);
  arrGuard(cexcContainerptr, 2048);
  excPageptr.p->word32[cexcContainerptr] = tshrTmp1;
  if (((cexcContainerhead >> 7) & 0x3) == 0) {
    jam();
    endofshrinkbucketLab(signal);
    return;
  }//if
  nextcontainerinfoExp(signal);
  do {
    cexcContainerptr = (cexcPageindex << ZSHIFT_PLUS) - (cexcPageindex << ZSHIFT_MINUS);
    if (cexcForward == ZTRUE) {
      jam();
      cexcContainerptr = cexcContainerptr + ZHEAD_SIZE;
    } else {
      jam();
      cexcContainerptr = ((cexcContainerptr + ZHEAD_SIZE) + ZBUF_SIZE) - ZCON_HEAD_SIZE;
    }//if
    arrGuard(cexcContainerptr, 2048);
    cexcContainerhead = excPageptr.p->word32[cexcContainerptr];
    cexcContainerlen = cexcContainerhead >> 26;
    ndbrequire(cexcContainerlen > ZCON_HEAD_SIZE);
    /*--------------------------------------------------------------------------*/
    /*       THIS CONTAINER IS NOT YET EMPTY AND WE REMOVE ALL THE ELEMENTS.    */
    /*--------------------------------------------------------------------------*/
    shrinkcontainer(signal);
    cexcPrevpageptr = excPageptr.i;
    cexcPrevpageindex = cexcPageindex;
    cexcPrevforward = cexcForward;
    if (((cexcContainerhead >> 7) & 0x3) != 0) {
      jam();
      /*--------------------------------------------------------------------------*/
      /*       WE MUST CALL THE NEXT CONTAINER INFO ROUTINE BEFORE WE RELEASE THE */
      /*       CONTAINER SINCE THE RELEASE WILL OVERWRITE THE NEXT POINTER.       */
      /*--------------------------------------------------------------------------*/
      nextcontainerinfoExp(signal);
    }//if
    rlPageptr.i = cexcPrevpageptr;
    ptrCheckGuard(rlPageptr, cpagesize, page8);
    trlPageindex = cexcPrevpageindex;
    if (cexcPrevforward == ZTRUE) {
      jam();
      if (((cexcContainerhead >> 10) & 1) == 1) {
        jam();
        trlRelCon = ZFALSE;
        turlIndex = cexcContainerptr + (ZBUF_SIZE - ZCON_HEAD_SIZE);
        releaseRightlist(signal);
      }//if
      trlRelCon = ZTRUE;
      tullIndex = cexcContainerptr;
      releaseLeftlist(signal);
    } else {
      jam();
      if (((cexcContainerhead >> 10) & 1) == 1) {
        jam();
        trlRelCon = ZFALSE;
        tullIndex = cexcContainerptr - (ZBUF_SIZE - ZCON_HEAD_SIZE);
        releaseLeftlist(signal);
      }//if
      trlRelCon = ZTRUE;
      turlIndex = cexcContainerptr;
      releaseRightlist(signal);
    }//if
  } while (((cexcContainerhead >> 7) & 0x3) != 0);
  endofshrinkbucketLab(signal);
  return;
}//Dbacc::execSHRINKCHECK2()

void Dbacc::endofshrinkbucketLab(Signal* signal) 
{
  fragrecptr.p->expandCounter--;
  fragrecptr.p->slack -= fragrecptr.p->maxloadfactor;
  if (fragrecptr.p->expSenderIndex == 0) {
    jam();
    fragrecptr.p->dirsize--;
    if (fragrecptr.p->expSenderPageptr != RNIL) {
      jam();
      rpPageptr.i = fragrecptr.p->expSenderPageptr;
      ptrCheckGuard(rpPageptr, cpagesize, page8);
      releasePage(signal);
      expDirptr.i = fragrecptr.p->expSenderDirptr;
      ptrCheckGuard(expDirptr, cdirarraysize, directoryarray);
      expDirptr.p->pagep[fragrecptr.p->expSenderDirIndex & 0xff] = RNIL;
    }//if
    if (((((fragrecptr.p->p + fragrecptr.p->maxp) + 1) >> fragrecptr.p->k) & 0xff) == 0) {
      jam();
      rdDirptr.i = fragrecptr.p->expSenderDirptr;
      releaseDirectory(signal);
      expDirRangePtr.i = fragrecptr.p->directory;
      ptrCheckGuard(expDirRangePtr, cdirrangesize, dirRange);
      arrGuard((fragrecptr.p->expSenderDirIndex >> 8), 256);
      expDirRangePtr.p->dirArray[fragrecptr.p->expSenderDirIndex >> 8] = RNIL;
    }//if
  }//if
  if (fragrecptr.p->slack < (1u << 31)) {
    jam();
    /*--------------------------------------------------------------*/
    /* THE SLACK IS POSITIVE, IN THIS CASE WE WILL CHECK WHETHER    */
    /* WE WILL CONTINUE PERFORM ANOTHER SHRINK.                     */
    /*--------------------------------------------------------------*/
    Uint32 noOfBuckets = (fragrecptr.p->maxp + 1) + fragrecptr.p->p;
    Uint32 Thysteresis = fragrecptr.p->maxloadfactor - fragrecptr.p->minloadfactor;
    fragrecptr.p->slackCheck = noOfBuckets * Thysteresis;
    if (fragrecptr.p->slack > Thysteresis) {
      /*--------------------------------------------------------------*/
      /*       IT IS STILL NECESSARY TO SHRINK THE FRAGMENT MORE. THIS*/
      /*       CAN HAPPEN WHEN A NUMBER OF SHRINKS GET REJECTED       */
      /*       DURING A LOCAL CHECKPOINT. WE START A NEW SHRINK       */
      /*       IMMEDIATELY FROM HERE WITHOUT WAITING FOR A COMMIT TO  */
      /*       START IT.                                              */
      /*--------------------------------------------------------------*/
      if (fragrecptr.p->expandCounter > 0) {
        jam();
	/*--------------------------------------------------------------*/
	/*       IT IS VERY IMPORTANT TO NOT TRY TO SHRINK MORE THAN    */
	/*       WAS EXPANDED. IF MAXP IS SET TO A VALUE BELOW 63 THEN  */
	/*       WE WILL LOSE RECORDS SINCE GETDIRINDEX CANNOT HANDLE   */
	/*       SHRINKING BELOW 2^K - 1 (NOW 63). THIS WAS A BUG THAT  */
	/*       WAS REMOVED 2000-05-12.                                */
	/*--------------------------------------------------------------*/
        signal->theData[0] = fragrecptr.i;
        signal->theData[1] = fragrecptr.p->p;
        signal->theData[2] = fragrecptr.p->maxp;
        signal->theData[3] = fragrecptr.p->expandFlag;
	ndbrequire(fragrecptr.p->expandFlag < 2);
        fragrecptr.p->expandFlag = 2;
        sendSignal(cownBlockref, GSN_SHRINKCHECK2, signal, 4, JBB);
      }//if
    }//if
  }//if
  ndbrequire(fragrecptr.p->maxp >= (Uint32)((1 << fragrecptr.p->k) - 1));
  return;
}//Dbacc::endofshrinkbucketLab()

/* --------------------------------------------------------------------------------- */
/* SHRINKCONTAINER                                                                   */
/*        INPUT: EXC_PAGEPTR (POINTER TO THE ACTIVE PAGE RECORD)                     */
/*               CEXC_CONTAINERLEN (LENGTH OF THE CONTAINER).                        */
/*               CEXC_CONTAINERPTR (ARRAY INDEX OF THE CONTAINER).                   */
/*               CEXC_FORWARD (CONTAINER FORWARD (+1) OR BACKWARD (-1))              */
/*                                                                                   */
/*        DESCRIPTION: ALL ELEMENTS OF THE ACTIVE CONTAINER HAVE TO MOVE TO THE NEW  */
/*                  CONTAINER.                                                       */
/* --------------------------------------------------------------------------------- */
void Dbacc::shrinkcontainer(Signal* signal) 
{
  Uint32 tshrElementptr;
  Uint32 tshrRemLen;
  Uint32 tshrInc;
  Uint32 tshrTmp;
  Uint32 tshrIndex;
  Uint32 guard21;

  tshrRemLen = cexcContainerlen - ZCON_HEAD_SIZE;
  tshrInc = fragrecptr.p->elementLength;
  if (cexcForward == ZTRUE) {
    jam();
    tshrElementptr = cexcContainerptr + ZCON_HEAD_SIZE;
  } else {
    jam();
    tshrElementptr = cexcContainerptr - 1;
  }//if
 SHR_LOOP:
  idrOperationRecPtr.i = RNIL;
  ptrNull(idrOperationRecPtr);
  /* --------------------------------------------------------------------------------- */
  /*       THE CODE BELOW IS ALL USED TO PREPARE FOR THE CALL TO INSERT_ELEMENT AND    */
  /*       HANDLE THE RESULT FROM INSERT_ELEMENT. INSERT_ELEMENT INSERTS THE ELEMENT   */
  /*       INTO ANOTHER BUCKET.                                                        */
  /* --------------------------------------------------------------------------------- */
  arrGuard(tshrElementptr, 2048);
  tidrElemhead = excPageptr.p->word32[tshrElementptr];
  if (ElementHeader::getLocked(tidrElemhead)) {
    jam();
    /* --------------------------------------------------------------------------------- */
    /*       IF THE ELEMENT IS LOCKED WE MUST UPDATE THE ELEMENT INFO IN THE OPERATION   */
    /*       RECORD OWNING THE LOCK. WE DO THIS BY READING THE OPERATION RECORD POINTER  */
    /*       FROM THE ELEMENT HEADER.                                                    */
    /* --------------------------------------------------------------------------------- */
    idrOperationRecPtr.i = ElementHeader::getOpPtrI(tidrElemhead);
    ptrCheckGuard(idrOperationRecPtr, coprecsize, operationrec);
  }//if
  tshrTmp = tshrElementptr + cexcForward;
  guard21 = fragrecptr.p->localkeylen - 1;
  for (tshrIndex = 0; tshrIndex <= guard21; tshrIndex++) {
    arrGuard(tshrIndex, 2);
    arrGuard(tshrTmp, 2048);
    clocalkey[tshrIndex] = excPageptr.p->word32[tshrTmp];
    tshrTmp = tshrTmp + cexcForward;
  }//for
  tidrPageindex = fragrecptr.p->expReceiveIndex;
  idrPageptr.i = fragrecptr.p->expReceivePageptr;
  ptrCheckGuard(idrPageptr, cpagesize, page8);
  tidrForward = fragrecptr.p->expReceiveForward;
  insertElement(signal);
  /* --------------------------------------------------------------------------------- */
  /*       TAKE CARE OF RESULT FROM INSERT_ELEMENT.                                    */
  /* --------------------------------------------------------------------------------- */
  fragrecptr.p->expReceiveIndex = tidrPageindex;
  fragrecptr.p->expReceivePageptr = idrPageptr.i;
  fragrecptr.p->expReceiveForward = tidrForward;
  if (tshrRemLen < tshrInc) {
    jam();
    sendSystemerror(signal, __LINE__);
  }//if
  tshrRemLen = tshrRemLen - tshrInc;
  if (tshrRemLen != 0) {
    jam();
    tshrElementptr = tshrTmp;
    goto SHR_LOOP;
  }//if
}//Dbacc::shrinkcontainer()

/* --------------------------------------------------------------------------------- */
/* NEXTCONTAINERINFO_EXP                                                             */
/*        DESCRIPTION:THE CONTAINER HEAD WILL BE CHECKED TO CALCULATE INFORMATION    */
/*                    ABOUT NEXT CONTAINER IN THE BUCKET.                            */
/*          INPUT:       CEXC_CONTAINERHEAD                                          */
/*                       CEXC_CONTAINERPTR                                           */
/*                       EXC_PAGEPTR                                                 */
/*          OUTPUT:                                                                  */
/*             CEXC_PAGEINDEX (INDEX FROM WHICH PAGE INDEX CAN BE CALCULATED.        */
/*             EXC_PAGEPTR (PAGE REFERENCE OF NEXT CONTAINER)                        */
/*             CEXC_FORWARD                                                          */
/* --------------------------------------------------------------------------------- */
void Dbacc::nextcontainerinfoExp(Signal* signal) 
{
  tnciNextSamePage = (cexcContainerhead >> 9) & 0x1;	/* CHECK BIT FOR CHECKING WHERE */
  /* THE NEXT CONTAINER IS IN THE SAME PAGE */
  cexcPageindex = cexcContainerhead & 0x7f;	/* NEXT CONTAINER PAGE INDEX 7 BITS */
  if (((cexcContainerhead >> 7) & 3) == ZLEFT) {
    jam();
    cexcForward = ZTRUE;
  } else if (((cexcContainerhead >> 7) & 3) == ZRIGHT) {
    jam();
    cexcForward = cminusOne;
  } else {
    jam();
    sendSystemerror(signal, __LINE__);
    cexcForward = 0;	/* DUMMY FOR COMPILER */
  }//if
  if (tnciNextSamePage == ZFALSE) {
    jam();
    /* NEXT CONTAINER IS IN AN OVERFLOW PAGE */
    arrGuard(cexcContainerptr + 1, 2048);
    tnciTmp = excPageptr.p->word32[cexcContainerptr + 1];
    nciOverflowrangeptr.i = fragrecptr.p->overflowdir;
    ptrCheckGuard(nciOverflowrangeptr, cdirrangesize, dirRange);
    arrGuard((tnciTmp >> 8), 256);
    nciOverflowDirptr.i = nciOverflowrangeptr.p->dirArray[tnciTmp >> 8];
    ptrCheckGuard(nciOverflowDirptr, cdirarraysize, directoryarray);
    excPageptr.i = nciOverflowDirptr.p->pagep[tnciTmp & 0xff];
    ptrCheckGuard(excPageptr, cpagesize, page8);
  }//if
}//Dbacc::nextcontainerinfoExp()

void Dbacc::initFragAdd(Signal* signal,
                        FragmentrecPtr regFragPtr) 
{
  const AccFragReq * const req = (AccFragReq*)&signal->theData[0];  
  Uint32 lhFragBits = req->lhFragBits + 1;
  Uint32 minLoadFactor = (req->minLoadFactor * ZBUF_SIZE) / 100;
  Uint32 maxLoadFactor = (req->maxLoadFactor * ZBUF_SIZE) / 100;
  if (minLoadFactor >= maxLoadFactor) {
    jam();
    minLoadFactor = maxLoadFactor - 1;
  }//if
  regFragPtr.p->fragState = ACTIVEFRAG;
  // NOTE: next line must match calculation in Dblqh::execLQHFRAGREQ
  regFragPtr.p->myfid = req->fragId;
  regFragPtr.p->myTableId = req->tableId;
  ndbrequire(req->kValue == 6);
  regFragPtr.p->k = req->kValue;	/* TK_SIZE = 6 IN THIS VERSION */
  regFragPtr.p->expandCounter = 0;

  /**
   * Only allow shrink during SR
   *   - to make sure we don't run out of pages during REDO log execution
   *
   * Is later restored to 0 by LQH at end of REDO log execution
   */
  regFragPtr.p->expandFlag = 0;
  regFragPtr.p->p = 0;
  regFragPtr.p->maxp = (1 << req->kValue) - 1;
  regFragPtr.p->minloadfactor = minLoadFactor;
  regFragPtr.p->maxloadfactor = maxLoadFactor;
  regFragPtr.p->slack = (regFragPtr.p->maxp + 1) * maxLoadFactor;
  regFragPtr.p->lhfragbits = lhFragBits;
  regFragPtr.p->lhdirbits = 0;
  regFragPtr.p->hashcheckbit = 0; //lhFragBits;
  regFragPtr.p->localkeylen = req->localKeyLen;
  regFragPtr.p->nodetype = (req->reqInfo >> 4) & 0x3;
  regFragPtr.p->lastOverIndex = 0;
  regFragPtr.p->dirsize = 1;
  regFragPtr.p->keyLength = req->keyLength;
  ndbrequire(req->keyLength != 0);
  regFragPtr.p->elementLength = ZELEM_HEAD_SIZE + regFragPtr.p->localkeylen;
  Uint32 Tmp1 = (regFragPtr.p->maxp + 1) + regFragPtr.p->p;
  Uint32 Tmp2 = regFragPtr.p->maxloadfactor - regFragPtr.p->minloadfactor;
  Tmp2 = Tmp1 * Tmp2;
  regFragPtr.p->slackCheck = Tmp2;
  regFragPtr.p->mytabptr = req->tableId;
  regFragPtr.p->roothashcheck = req->kValue + req->lhFragBits;
  regFragPtr.p->noOfElements = 0;
  for (Uint32 i = 0; i < MAX_PARALLEL_SCANS_PER_FRAG; i++) {
    regFragPtr.p->scan[i] = RNIL;
  }//for
  
  Uint32 hasCharAttr = g_key_descriptor_pool.getPtr(req->tableId)->hasCharAttr;
  regFragPtr.p->hasCharAttr = hasCharAttr;
}//Dbacc::initFragAdd()

void Dbacc::initFragGeneral(FragmentrecPtr regFragPtr)
{
  regFragPtr.p->directory = RNIL;
  regFragPtr.p->overflowdir = RNIL;
  regFragPtr.p->firstOverflowRec = RNIL;
  regFragPtr.p->lastOverflowRec = RNIL;
  regFragPtr.p->lockOwnersList = RNIL;
  regFragPtr.p->firstFreeDirindexRec = RNIL;

  regFragPtr.p->activeDataPage = 0;
  regFragPtr.p->hasCharAttr = ZFALSE;
  regFragPtr.p->nextAllocPage = 0;
  regFragPtr.p->fragState = FREEFRAG;
}//Dbacc::initFragGeneral()


void
Dbacc::releaseLogicalPage(Fragmentrec * fragP, Uint32 logicalPageId){
  Ptr<struct DirRange> dirRangePtr;
  dirRangePtr.i = fragP->directory;
  ptrCheckGuard(dirRangePtr, cdirrangesize, dirRange);

  const Uint32 lp1 = logicalPageId >> 8;
  const Uint32 lp2 = logicalPageId & 0xFF;
  ndbrequire(lp1 < 256);

  Ptr<struct Directoryarray> dirArrPtr;
  dirArrPtr.i = dirRangePtr.p->dirArray[lp1];
  ptrCheckGuard(dirArrPtr, cdirarraysize, directoryarray);

  const Uint32 physicalPageId = dirArrPtr.p->pagep[lp2];
  
  rpPageptr.i = physicalPageId;
  ptrCheckGuard(rpPageptr, cpagesize, page8);
  releasePage(0);

  dirArrPtr.p->pagep[lp2] = RNIL;
}

void Dbacc::execACC_SCANREQ(Signal* signal) 
{
  jamEntry();
  AccScanReq * req = (AccScanReq*)&signal->theData[0];
  tuserptr = req->senderData;
  tuserblockref = req->senderRef;
  tabptr.i = req->tableId;
  tfid = req->fragmentNo;
  tscanFlag = req->requestInfo;
  tscanTrid1 = req->transId1;
  tscanTrid2 = req->transId2;
  
  tresult = 0;
  ptrCheckGuard(tabptr, ctablesize, tabrec);
  ndbrequire(getfragmentrec(signal, fragrecptr, tfid));
  
  Uint32 i;
  for (i = 0; i < MAX_PARALLEL_SCANS_PER_FRAG; i++) {
    jam();
    if (fragrecptr.p->scan[i] == RNIL) {
      jam();
      break;
    }
  }
  ndbrequire(i != MAX_PARALLEL_SCANS_PER_FRAG);
  ndbrequire(cfirstFreeScanRec != RNIL);
  seizeScanRec(signal);

  fragrecptr.p->scan[i] = scanPtr.i;
  scanPtr.p->scanBucketState =  ScanRec::FIRST_LAP;
  scanPtr.p->scanLockMode = AccScanReq::getLockMode(tscanFlag);
  scanPtr.p->scanReadCommittedFlag = AccScanReq::getReadCommittedFlag(tscanFlag);
  
  /* TWELVE BITS OF THE ELEMENT HEAD ARE SCAN */
  /* CHECK BITS. THE MASK NOTES WHICH BIT IS */
  /* ALLOCATED FOR THE ACTIVE SCAN */
  scanPtr.p->scanMask = 1 << i;
  scanPtr.p->scanUserptr = tuserptr;
  scanPtr.p->scanUserblockref = tuserblockref;
  scanPtr.p->scanTrid1 = tscanTrid1;
  scanPtr.p->scanTrid2 = tscanTrid2;
  scanPtr.p->scanLockHeld = 0;
  scanPtr.p->scanOpsAllocated = 0;
  scanPtr.p->scanFirstActiveOp = RNIL;
  scanPtr.p->scanFirstQueuedOp = RNIL;
  scanPtr.p->scanLastQueuedOp = RNIL;
  scanPtr.p->scanFirstLockedOp = RNIL;
  scanPtr.p->scanLastLockedOp = RNIL;
  scanPtr.p->scanState = ScanRec::WAIT_NEXT;
  initScanFragmentPart(signal);

  /*------------------------------------------------------*/
  /* We start the timeout loop for the scan process here. */
  /*------------------------------------------------------*/
  ndbrequire(scanPtr.p->scanTimer == 0);
  if (scanPtr.p->scanContinuebCounter == 0) {
    jam();
    scanPtr.p->scanContinuebCounter = 1;
    signal->theData[0] = ZSEND_SCAN_HBREP;
    signal->theData[1] = scanPtr.i;
    sendSignalWithDelay(cownBlockref, GSN_CONTINUEB, signal, 100, 2);
  }//if
  scanPtr.p->scanTimer = scanPtr.p->scanContinuebCounter;
  /* ************************ */
  /*  ACC_SCANCONF            */
  /* ************************ */
  signal->theData[0] = scanPtr.p->scanUserptr;
  signal->theData[1] = scanPtr.i;
  signal->theData[2] = 1; /* NR OF LOCAL FRAGMENT */
  signal->theData[3] = fragrecptr.p->fragmentid;
  signal->theData[4] = RNIL;
  signal->theData[7] = AccScanConf::ZNOT_EMPTY_FRAGMENT;
  sendSignal(scanPtr.p->scanUserblockref, GSN_ACC_SCANCONF, signal, 8, JBB);
  /* NOT EMPTY FRAGMENT */
  return;
}//Dbacc::execACC_SCANREQ()

/* ******************--------------------------------------------------------------- */
/*  NEXT_SCANREQ                                       REQUEST FOR NEXT ELEMENT OF   */
/* ******************------------------------------+   A FRAGMENT.                   */
/*   SENDER: LQH,    LEVEL B       */
void Dbacc::execNEXT_SCANREQ(Signal* signal) 
{
  Uint32 tscanNextFlag;
  jamEntry();
  scanPtr.i = signal->theData[0];
  operationRecPtr.i = signal->theData[1];
  tscanNextFlag = signal->theData[2];
  /* ------------------------------------------ */
  /* 1 = ZCOPY_NEXT  GET NEXT ELEMENT           */
  /* 2 = ZCOPY_NEXT_COMMIT COMMIT THE           */
  /* ACTIVE ELEMENT AND GET THE NEXT ONE        */
  /* 3 = ZCOPY_COMMIT COMMIT THE ACTIVE ELEMENT */
  /* 4 = ZCOPY_REPEAT GET THE ACTIVE ELEMENT    */
  /* 5 = ZCOPY_ABORT RELOCK THE ACTIVE ELEMENT  */
  /* 6 = ZCOPY_CLOSE THE SCAN PROCESS IS READY  */
  /* ------------------------------------------ */
  tresult = 0;
  ptrCheckGuard(scanPtr, cscanRecSize, scanRec);
  ndbrequire(scanPtr.p->scanState == ScanRec::WAIT_NEXT);

  scanPtr.p->scanTimer = scanPtr.p->scanContinuebCounter;
  switch (tscanNextFlag) {
  case NextScanReq::ZSCAN_NEXT:
    jam();
    /*empty*/;
    break;
  case NextScanReq::ZSCAN_NEXT_COMMIT:
  case NextScanReq::ZSCAN_COMMIT:
    jam();
    /* --------------------------------------------------------------------- */
    /* COMMIT ACTIVE OPERATION. 
     * SEND NEXT SCAN ELEMENT IF IT IS ZCOPY_NEXT_COMMIT.
     * --------------------------------------------------------------------- */
    ptrCheckGuard(operationRecPtr, coprecsize, operationrec); 
    fragrecptr.i = operationRecPtr.p->fragptr;
    ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
    if (!scanPtr.p->scanReadCommittedFlag) {
      commitOperation(signal);
    }//if
    takeOutActiveScanOp(signal);
    releaseOpRec(signal);
    scanPtr.p->scanOpsAllocated--;
    if (tscanNextFlag == NextScanReq::ZSCAN_COMMIT) {
      jam();
      signal->theData[0] = scanPtr.p->scanUserptr;
      Uint32 blockNo = refToBlock(scanPtr.p->scanUserblockref);
      EXECUTE_DIRECT(blockNo, GSN_NEXT_SCANCONF, signal, 1);
      return;
    }//if
    break;
  case NextScanReq::ZSCAN_CLOSE:
    jam();
    fragrecptr.i = scanPtr.p->activeLocalFrag;
    ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
    /* --------------------------------------------------------------------------------- */
    /* THE SCAN PROCESS IS FINISHED. RELOCK ALL LOCKED EL. RELESE ALL INVOLVED REC.      */
    /* --------------------------------------------------------------------------------- */
    releaseScanLab(signal);
    return;
    break;
  default:
    ndbrequire(false);
    break;
  }//switch
  signal->theData[0] = scanPtr.i;
  signal->theData[1] = AccCheckScan::ZNOT_CHECK_LCP_STOP;
  execACC_CHECK_SCAN(signal);
  return;
}//Dbacc::execNEXT_SCANREQ()

void Dbacc::checkNextBucketLab(Signal* signal) 
{
  DirRangePtr cscDirRangePtr;
  DirectoryarrayPtr cscDirptr;
  DirectoryarrayPtr tnsDirptr;
  Page8Ptr nsPageptr;
  Page8Ptr cscPageidptr;
  Page8Ptr gnsPageidptr;
  Page8Ptr tnsPageidptr;
  Uint32 tnsElementptr;
  Uint32 tnsContainerptr;
  Uint32 tnsIsLocked;
  Uint32 tnsTmp1;
  Uint32 tnsTmp2;
  Uint32 tnsCopyIndex1;
  Uint32 tnsCopyIndex2;
  Uint32 tnsCopyDir;

  tnsCopyDir = scanPtr.p->nextBucketIndex >> fragrecptr.p->k;
  tnsCopyIndex1 = tnsCopyDir >> 8;
  tnsCopyIndex2 = tnsCopyDir & 0xff;
  arrGuard(tnsCopyIndex1, 256);
  tnsDirptr.i = gnsDirRangePtr.p->dirArray[tnsCopyIndex1];
  ptrCheckGuard(tnsDirptr, cdirarraysize, directoryarray);
  tnsPageidptr.i = tnsDirptr.p->pagep[tnsCopyIndex2];
  ptrCheckGuard(tnsPageidptr, cpagesize, page8);
  gnsPageidptr.i = tnsPageidptr.i;
  gnsPageidptr.p = tnsPageidptr.p;
  tnsTmp1 = (1 << fragrecptr.p->k) - 1;
  tgsePageindex = scanPtr.p->nextBucketIndex & tnsTmp1;
  gsePageidptr.i = gnsPageidptr.i;
  gsePageidptr.p = gnsPageidptr.p;
  if (!getScanElement(signal)) {
    scanPtr.p->nextBucketIndex++;
    if (scanPtr.p->scanBucketState ==  ScanRec::SECOND_LAP) {
      if (scanPtr.p->nextBucketIndex > scanPtr.p->maxBucketIndexToRescan) {
	/* --------------------------------------------------------------------------------- */
	// We have finished the rescan phase. We are ready to proceed with the next fragment part.
	/* --------------------------------------------------------------------------------- */
        jam();
        checkNextFragmentLab(signal);
        return;
      }//if
    } else if (scanPtr.p->scanBucketState ==  ScanRec::FIRST_LAP) {
      if ((fragrecptr.p->p + fragrecptr.p->maxp) < scanPtr.p->nextBucketIndex) {
	/* --------------------------------------------------------------------------------- */
	// All buckets have been scanned a first time.
	/* --------------------------------------------------------------------------------- */
        if (scanPtr.p->minBucketIndexToRescan == 0xFFFFFFFF) {
          jam();
	  /* --------------------------------------------------------------------------------- */
	  // We have not had any merges behind the scan. Thus it is not necessary to perform
	  // any rescan any buckets and we can proceed immediately with the next fragment part.
	  /* --------------------------------------------------------------------------------- */
          checkNextFragmentLab(signal);
          return;
        } else {
          jam();
	  /* --------------------------------------------------------------------------------- */
	  // Some buckets are in the need of rescanning due to merges that have moved records
	  // from in front of the scan to behind the scan. During the merges we kept track of
	  // which buckets that need a rescan. We start with the minimum and end with maximum.
	  /* --------------------------------------------------------------------------------- */
          scanPtr.p->nextBucketIndex = scanPtr.p->minBucketIndexToRescan;
	  scanPtr.p->scanBucketState =  ScanRec::SECOND_LAP;
          if (scanPtr.p->maxBucketIndexToRescan > (fragrecptr.p->p + fragrecptr.p->maxp)) {
            jam();
	    /* --------------------------------------------------------------------------------- */
	    // If we have had so many merges that the maximum is bigger than the number of buckets
	    // then we will simply satisfy ourselves with scanning to the end. This can only happen
	    // after bringing down the total of buckets to less than half and the minimum should
	    // be 0 otherwise there is some problem.
	    /* --------------------------------------------------------------------------------- */
            if (scanPtr.p->minBucketIndexToRescan != 0) {
              jam();
              sendSystemerror(signal, __LINE__);
              return;
            }//if
            scanPtr.p->maxBucketIndexToRescan = fragrecptr.p->p + fragrecptr.p->maxp;
          }//if
        }//if
      }//if
    }//if
    if ((scanPtr.p->scanBucketState ==  ScanRec::FIRST_LAP) &&
        (scanPtr.p->nextBucketIndex <= scanPtr.p->startNoOfBuckets)) {
      /* --------------------------------------------------------------------------------- */
      // We will only reset the scan indicator on the buckets that existed at the start of the
      // scan. The others will be handled by the split and merge code.
      /* --------------------------------------------------------------------------------- */
      tnsTmp2 = (1 << fragrecptr.p->k) - 1;
      trsbPageindex = scanPtr.p->nextBucketIndex & tnsTmp2;
      if (trsbPageindex != 0) {
        jam();
        rsbPageidptr.i = gnsPageidptr.i;
        rsbPageidptr.p = gnsPageidptr.p;
      } else {
        jam();
        cscDirRangePtr.i = fragrecptr.p->directory;
        tmpP = scanPtr.p->nextBucketIndex >> fragrecptr.p->k;
        tmpP2 = tmpP >> 8;
        tmpP = tmpP & 0xff;
        ptrCheckGuard(cscDirRangePtr, cdirrangesize, dirRange);
        arrGuard(tmpP2, 256);
        cscDirptr.i = cscDirRangePtr.p->dirArray[tmpP2];
        ptrCheckGuard(cscDirptr, cdirarraysize, directoryarray);
        cscPageidptr.i = cscDirptr.p->pagep[tmpP];
        ptrCheckGuard(cscPageidptr, cpagesize, page8);
        tmp1 = (1 << fragrecptr.p->k) - 1;
        trsbPageindex = scanPtr.p->nextBucketIndex & tmp1;
        rsbPageidptr.i = cscPageidptr.i;
        rsbPageidptr.p = cscPageidptr.p;
      }//if
      releaseScanBucket(signal);
    }//if
    signal->theData[0] = scanPtr.i;
    signal->theData[1] = AccCheckScan::ZCHECK_LCP_STOP;
    sendSignal(cownBlockref, GSN_ACC_CHECK_SCAN, signal, 2, JBB);
    return;
  }//if
  /* ----------------------------------------------------------------------- */
  /*	AN ELEMENT WHICH HAVE NOT BEEN SCANNED WAS FOUND. WE WILL PREPARE IT */
  /*	TO BE SENT TO THE LQH BLOCK FOR FURTHER PROCESSING.                  */
  /*    WE ASSUME THERE ARE OPERATION RECORDS AVAILABLE SINCE LQH SHOULD HAVE*/
  /*    GUARANTEED THAT THROUGH EARLY BOOKING.                               */
  /* ----------------------------------------------------------------------- */
  tnsIsLocked = tgseIsLocked;
  tnsElementptr = tgseElementptr;
  tnsContainerptr = tgseContainerptr;
  nsPageptr.i = gsePageidptr.i;
  nsPageptr.p = gsePageidptr.p;
  seizeOpRec(signal);
  tisoIsforward = tgseIsforward;
  tisoContainerptr = tnsContainerptr;
  tisoElementptr = tnsElementptr;
  isoPageptr.i = nsPageptr.i;
  isoPageptr.p = nsPageptr.p;
  initScanOpRec(signal);
 
  if (!tnsIsLocked){
    if (!scanPtr.p->scanReadCommittedFlag) {
      jam();
      slPageidptr = nsPageptr;
      tslElementptr = tnsElementptr;
      setlock(signal);
      insertLockOwnersList(signal, operationRecPtr);
    }//if
  } else {
    arrGuard(tnsElementptr, 2048);
    queOperPtr.i = 
      ElementHeader::getOpPtrI(nsPageptr.p->word32[tnsElementptr]);
    ptrCheckGuard(queOperPtr, coprecsize, operationrec);
    if (queOperPtr.p->elementIsDisappeared == ZTRUE) {
      jam();
      /* --------------------------------------------------------------------------------- */
      // If the lock owner indicates the element is disappeared then we will not report this
      // tuple. We will continue with the next tuple.
      /* --------------------------------------------------------------------------------- */
      releaseOpRec(signal);
      scanPtr.p->scanOpsAllocated--;
      signal->theData[0] = scanPtr.i;
      signal->theData[1] = AccCheckScan::ZCHECK_LCP_STOP;
      sendSignal(cownBlockref, GSN_ACC_CHECK_SCAN, signal, 2, JBB);
      return;
    }//if
    if (!scanPtr.p->scanReadCommittedFlag) {
      Uint32 return_result;
      if (scanPtr.p->scanLockMode == ZREADLOCK) {
        jam();
        priPageptr = nsPageptr;
        tpriElementptr = tnsElementptr;
        return_result = placeReadInLockQueue(signal);
      } else {
        jam();
        pwiPageptr = nsPageptr;
        tpwiElementptr = tnsElementptr;
        return_result = placeWriteInLockQueue(signal);
      }//if
      if (return_result == ZSERIAL_QUEUE) {
	/* --------------------------------------------------------------------------------- */
	/*	WE PLACED THE OPERATION INTO A SERIAL QUEUE AND THUS WE HAVE TO WAIT FOR     */
	/*	THE LOCK TO BE RELEASED. WE CONTINUE WITH THE NEXT ELEMENT.                  */
	/* --------------------------------------------------------------------------------- */
        putOpScanLockQue();	/* PUT THE OP IN A QUE IN THE SCAN REC */
        signal->theData[0] = scanPtr.i;
        signal->theData[1] = AccCheckScan::ZCHECK_LCP_STOP;
        sendSignal(cownBlockref, GSN_ACC_CHECK_SCAN, signal, 2, JBB);
        return;
      } else if (return_result == ZWRITE_ERROR) {
        jam();
	/* --------------------------------------------------------------------------------- */
	// The tuple is either not committed yet or a delete in the same transaction (not
	// possible here since we are a scan). Thus we simply continue with the next tuple.
	/* --------------------------------------------------------------------------------- */
        releaseOpRec(signal);
	scanPtr.p->scanOpsAllocated--;
        signal->theData[0] = scanPtr.i;
        signal->theData[1] = AccCheckScan::ZCHECK_LCP_STOP;
        sendSignal(cownBlockref, GSN_ACC_CHECK_SCAN, signal, 2, JBB);
        return;
      }//if
      ndbassert(return_result == ZPARALLEL_QUEUE);
    }//if
  }//if
  /* --------------------------------------------------------------------------------- */
  // Committed read proceed without caring for locks immediately down here except when
  // the tuple was deleted permanently and no new operation has inserted it again.
  /* --------------------------------------------------------------------------------- */
  putActiveScanOp(signal);
  sendNextScanConf(signal);
  return;
}//Dbacc::checkNextBucketLab()


void Dbacc::checkNextFragmentLab(Signal* signal) 
{
  scanPtr.p->scanBucketState =  ScanRec::SCAN_COMPLETED;
  // The scan is completed. ACC_CHECK_SCAN will perform all the necessary 
  // checks to see
  // what the next step is.
  signal->theData[0] = scanPtr.i;
  signal->theData[1] = AccCheckScan::ZCHECK_LCP_STOP;
  execACC_CHECK_SCAN(signal);
  return;
}//Dbacc::checkNextFragmentLab()

void Dbacc::initScanFragmentPart(Signal* signal)
{
  DirRangePtr cnfDirRangePtr;
  DirectoryarrayPtr cnfDirptr;
  Page8Ptr cnfPageidptr;
  /* --------------------------------------------------------------------------------- */
  // Set the active fragment part.
  // Set the current bucket scanned to the first.
  // Start with the first lap.
  // Remember the number of buckets at start of the scan.
  // Set the minimum and maximum to values that will always be smaller and larger than.
  // Reset the scan indicator on the first bucket.
  /* --------------------------------------------------------------------------------- */
  scanPtr.p->activeLocalFrag = fragrecptr.i;
  scanPtr.p->nextBucketIndex = 0;	/* INDEX OF SCAN BUCKET */
  scanPtr.p->scanBucketState = ScanRec::FIRST_LAP;
  scanPtr.p->startNoOfBuckets = fragrecptr.p->p + fragrecptr.p->maxp;
  scanPtr.p->minBucketIndexToRescan = 0xFFFFFFFF;
  scanPtr.p->maxBucketIndexToRescan = 0;
  cnfDirRangePtr.i = fragrecptr.p->directory;
  ptrCheckGuard(cnfDirRangePtr, cdirrangesize, dirRange);
  cnfDirptr.i = cnfDirRangePtr.p->dirArray[0];
  ptrCheckGuard(cnfDirptr, cdirarraysize, directoryarray);
  cnfPageidptr.i = cnfDirptr.p->pagep[0];
  ptrCheckGuard(cnfPageidptr, cpagesize, page8);
  trsbPageindex = scanPtr.p->nextBucketIndex & ((1 << fragrecptr.p->k) - 1);
  rsbPageidptr.i = cnfPageidptr.i;
  rsbPageidptr.p = cnfPageidptr.p;
  releaseScanBucket(signal);
}//Dbacc::initScanFragmentPart()

/* --------------------------------------------------------------------------------- */
/* FLAG = 6 = ZCOPY_CLOSE THE SCAN PROCESS IS READY OR ABORTED. ALL OPERATION IN THE */
/* ACTIVE OR WAIT QUEUE ARE RELEASED, SCAN FLAG OF ROOT FRAG IS RESET AND THE SCAN   */
/* RECORD IS RELEASED.                                                               */
/* --------------------------------------------------------------------------------- */
void Dbacc::releaseScanLab(Signal* signal) 
{
  releaseAndCommitActiveOps(signal);
  releaseAndCommitQueuedOps(signal);
  releaseAndAbortLockedOps(signal);

  fragrecptr.i = scanPtr.p->activeLocalFrag;
  ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
  for (tmp = 0; tmp < MAX_PARALLEL_SCANS_PER_FRAG; tmp++) {
    jam();
    if (fragrecptr.p->scan[tmp] == scanPtr.i) {
      jam();
      fragrecptr.p->scan[tmp] = RNIL;
    }//if
  }//for
  // Stops the heartbeat.
  scanPtr.p->scanTimer = 0;
  signal->theData[0] = scanPtr.p->scanUserptr;
  signal->theData[1] = RNIL;
  signal->theData[2] = RNIL;
  sendSignal(scanPtr.p->scanUserblockref, GSN_NEXT_SCANCONF, signal, 3, JBB);
  releaseScanRec(signal);
  return;
}//Dbacc::releaseScanLab()


void Dbacc::releaseAndCommitActiveOps(Signal* signal) 
{
  OperationrecPtr trsoOperPtr;
  operationRecPtr.i = scanPtr.p->scanFirstActiveOp;
  while (operationRecPtr.i != RNIL) {
    jam();
    ptrCheckGuard(operationRecPtr, coprecsize, operationrec);
    trsoOperPtr.i = operationRecPtr.p->nextOp;
    fragrecptr.i = operationRecPtr.p->fragptr;
    ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
    if (!scanPtr.p->scanReadCommittedFlag) {
      jam();
      commitOperation(signal);
    }//if
    takeOutActiveScanOp(signal);
    releaseOpRec(signal);
    scanPtr.p->scanOpsAllocated--;
    operationRecPtr.i = trsoOperPtr.i;
  }//if
}//Dbacc::releaseAndCommitActiveOps()


void Dbacc::releaseAndCommitQueuedOps(Signal* signal) 
{
  OperationrecPtr trsoOperPtr;
  operationRecPtr.i = scanPtr.p->scanFirstQueuedOp;
  while (operationRecPtr.i != RNIL) {
    jam();
    ptrCheckGuard(operationRecPtr, coprecsize, operationrec);
    trsoOperPtr.i = operationRecPtr.p->nextOp;
    fragrecptr.i = operationRecPtr.p->fragptr;
    ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
    if (!scanPtr.p->scanReadCommittedFlag) {
      jam();
      commitOperation(signal);
    }//if
    takeOutReadyScanQueue(signal);
    releaseOpRec(signal);
    scanPtr.p->scanOpsAllocated--;
    operationRecPtr.i = trsoOperPtr.i;
  }//if
}//Dbacc::releaseAndCommitQueuedOps()

void Dbacc::releaseAndAbortLockedOps(Signal* signal) {

  OperationrecPtr trsoOperPtr;
  operationRecPtr.i = scanPtr.p->scanFirstLockedOp;
  while (operationRecPtr.i != RNIL) {
    jam();
    ptrCheckGuard(operationRecPtr, coprecsize, operationrec);
    trsoOperPtr.i = operationRecPtr.p->nextOp;
    fragrecptr.i = operationRecPtr.p->fragptr;
    ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
    if (!scanPtr.p->scanReadCommittedFlag) {
      jam();
      abortOperation(signal);
    }//if
    takeOutScanLockQueue(scanPtr.i);
    releaseOpRec(signal);
    scanPtr.p->scanOpsAllocated--;
    operationRecPtr.i = trsoOperPtr.i;
  }//if
}//Dbacc::releaseAndAbortLockedOps()

/* 3.18.3  ACC_CHECK_SCAN */
/* ******************--------------------------------------------------------------- */
/* ACC_CHECK_SCAN                                                                    */
/*          ENTER ACC_CHECK_SCAN WITH                                                */
/*                    SCAN_PTR                                                       */
/* ******************--------------------------------------------------------------- */
/* ******************--------------------------------------------------------------- */
/* ACC_CHECK_SCAN                                                                    */
/* ******************------------------------------+                                 */
void Dbacc::execACC_CHECK_SCAN(Signal* signal) 
{
  Uint32 TcheckLcpStop;
  jamEntry();
  scanPtr.i = signal->theData[0];
  TcheckLcpStop = signal->theData[1];
  ptrCheckGuard(scanPtr, cscanRecSize, scanRec);
  while (scanPtr.p->scanFirstQueuedOp != RNIL) {
    jam();
    //---------------------------------------------------------------------
    // An operation has been released from the lock queue. 
    // We are in the parallel queue of this tuple. We are 
    // ready to report the tuple now.
    //------------------------------------------------------------------------
    operationRecPtr.i = scanPtr.p->scanFirstQueuedOp;
    ptrCheckGuard(operationRecPtr, coprecsize, operationrec);
    takeOutReadyScanQueue(signal);
    fragrecptr.i = operationRecPtr.p->fragptr;
    ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
    if (operationRecPtr.p->elementIsDisappeared == ZTRUE) {
      jam();
      abortOperation(signal);
      releaseOpRec(signal);
      scanPtr.p->scanOpsAllocated--;
      continue;
    }//if
    putActiveScanOp(signal);
    sendNextScanConf(signal);
    return;
  }//while


  if ((scanPtr.p->scanBucketState == ScanRec::SCAN_COMPLETED) &&
      (scanPtr.p->scanLockHeld == 0)) {
    jam();
    //----------------------------------------------------------------------------
    // The scan is now completed and there are no more locks outstanding. Thus we
    // we will report the scan as completed to LQH.
    //----------------------------------------------------------------------------
    signal->theData[0] = scanPtr.p->scanUserptr;
    signal->theData[1] = RNIL;
    signal->theData[2] = RNIL;
    sendSignal(scanPtr.p->scanUserblockref, GSN_NEXT_SCANCONF, signal, 3, JBB);
    return;
  }//if
  if (TcheckLcpStop == AccCheckScan::ZCHECK_LCP_STOP) {
  //---------------------------------------------------------------------------
  // To ensure that the block of the fragment occurring at the start of a local
  // checkpoint is not held for too long we insert a release and reacquiring of
  // that lock here. This is performed in LQH. If we are blocked or if we have
  // requested a sleep then we will receive RNIL in the returning signal word.
  //---------------------------------------------------------------------------
    signal->theData[0] = scanPtr.p->scanUserptr;
    signal->theData[1] =
                    ((scanPtr.p->scanLockHeld >= ZSCAN_MAX_LOCK) ||
                     (scanPtr.p->scanBucketState ==  ScanRec::SCAN_COMPLETED));
    EXECUTE_DIRECT(DBLQH, GSN_CHECK_LCP_STOP, signal, 2);
    jamEntry();
    if (signal->theData[0] == RNIL) {
      jam();
      return;
    }//if
  }//if
  /**
   * If we have more than max locks held OR
   * scan is completed AND at least one lock held
   *  - Inform LQH about this condition
   */
  if ((scanPtr.p->scanLockHeld >= ZSCAN_MAX_LOCK) ||
      (cfreeopRec == RNIL) ||
      ((scanPtr.p->scanBucketState == ScanRec::SCAN_COMPLETED) &&
       (scanPtr.p->scanLockHeld > 0))) {
    jam();
    signal->theData[0] = scanPtr.p->scanUserptr;
    signal->theData[1] = RNIL; // No operation is returned
    signal->theData[2] = 512;  // MASV  
    sendSignal(scanPtr.p->scanUserblockref, GSN_NEXT_SCANCONF, signal, 3, JBB);
    return;
  }
  if (scanPtr.p->scanBucketState == ScanRec::SCAN_COMPLETED) {
    jam();
    signal->theData[0] = scanPtr.i;
    signal->theData[1] = AccCheckScan::ZCHECK_LCP_STOP;
    execACC_CHECK_SCAN(signal);
    return;
  }//if

  scanPtr.p->scanTimer = scanPtr.p->scanContinuebCounter;

  fragrecptr.i = scanPtr.p->activeLocalFrag;
  ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
  gnsDirRangePtr.i = fragrecptr.p->directory;
  ptrCheckGuard(gnsDirRangePtr, cdirrangesize, dirRange);
  checkNextBucketLab(signal);
  return;
}//Dbacc::execACC_CHECK_SCAN()

/* ******************---------------------------------------------------- */
/* ACC_TO_REQ                                       PERFORM A TAKE OVER   */
/* ******************-------------------+                                 */
/*   SENDER: LQH,    LEVEL B       */
void Dbacc::execACC_TO_REQ(Signal* signal) 
{
  OperationrecPtr tatrOpPtr;

  jamEntry();
  tatrOpPtr.i = signal->theData[1];     /*  OPER PTR OF ACC                */
  ptrCheckGuard(tatrOpPtr, coprecsize, operationrec);
  if (tatrOpPtr.p->operation == ZSCAN_OP) {
    tatrOpPtr.p->transId1 = signal->theData[2];
    tatrOpPtr.p->transId2 = signal->theData[3];
  } else {
    jam();
    signal->theData[0] = cminusOne;
    signal->theData[1] = ZTO_OP_STATE_ERROR;
  }//if
  return;
}//Dbacc::execACC_TO_REQ()

/* --------------------------------------------------------------------------------- */
/* CONTAINERINFO                                                                     */
/*        INPUT:                                                                     */
/*               CI_PAGEIDPTR (PAGE POINTER WHERE CONTAINER RESIDES)                 */
/*               TCI_PAGEINDEX (INDEX OF CONTAINER, USED TO CALCULATE PAGE INDEX)    */
/*               TCI_ISFORWARD (DIRECTION OF CONTAINER FORWARD OR BACKWARD)          */
/*                                                                                   */
/*        OUTPUT:                                                                    */
/*               TCI_CONTAINERPTR (A POINTER TO THE HEAD OF THE CONTAINER)           */
/*               TCI_CONTAINERLEN (LENGTH OF THE CONTAINER                           */
/*               TCI_CONTAINERHEAD (THE HEADER OF THE CONTAINER)                     */
/*                                                                                   */
/*        DESCRIPTION: THE ADDRESS OF THE CONTAINER WILL BE CALCULATED AND           */
/*                     ALL INFORMATION ABOUT THE CONTAINER WILL BE READ              */
/* --------------------------------------------------------------------------------- */
void Dbacc::containerinfo(Signal* signal) 
{
  tciContainerptr = (tciPageindex << ZSHIFT_PLUS) - (tciPageindex << ZSHIFT_MINUS);
  if (tciIsforward == ZTRUE) {
    jam();
    tciContainerptr = tciContainerptr + ZHEAD_SIZE;
  } else {
    jam();
    tciContainerptr = ((tciContainerptr + ZHEAD_SIZE) + ZBUF_SIZE) - ZCON_HEAD_SIZE;
  }//if
  arrGuard(tciContainerptr, 2048);
  tciContainerhead = ciPageidptr.p->word32[tciContainerptr];
  tciContainerlen = tciContainerhead >> 26;
}//Dbacc::containerinfo()

/* --------------------------------------------------------------------------------- */
/* GET_SCAN_ELEMENT                                                                  */
/*       INPUT:          GSE_PAGEIDPTR                                               */
/*                       TGSE_PAGEINDEX                                              */
/*       OUTPUT:         TGSE_IS_LOCKED (IF TRESULT /= ZFALSE)                       */
/*                       GSE_PAGEIDPTR                                               */
/*                       TGSE_PAGEINDEX                                              */
/* --------------------------------------------------------------------------------- */
bool Dbacc::getScanElement(Signal* signal) 
{
  tgseIsforward = ZTRUE;
 NEXTSEARCH_SCAN_LOOP:
  ciPageidptr.i = gsePageidptr.i;
  ciPageidptr.p = gsePageidptr.p;
  tciPageindex = tgsePageindex;
  tciIsforward = tgseIsforward;
  containerinfo(signal);
  sscPageidptr.i = gsePageidptr.i;
  sscPageidptr.p = gsePageidptr.p;
  tsscContainerlen = tciContainerlen;
  tsscContainerptr = tciContainerptr;
  tsscIsforward = tciIsforward;
  if (searchScanContainer(signal)) {
    jam();
    tgseIsLocked = tsscIsLocked;
    tgseElementptr = tsscElementptr;
    tgseContainerptr = tsscContainerptr;
    return true;
  }//if
  if (((tciContainerhead >> 7) & 0x3) != 0) {
    jam();
    nciPageidptr.i = gsePageidptr.i;
    nciPageidptr.p = gsePageidptr.p;
    tnciContainerhead = tciContainerhead;
    tnciContainerptr = tciContainerptr;
    nextcontainerinfo(signal);
    tgsePageindex = tnciPageindex;
    gsePageidptr.i = nciPageidptr.i;
    gsePageidptr.p = nciPageidptr.p;
    tgseIsforward = tnciIsforward;
    goto NEXTSEARCH_SCAN_LOOP;
  }//if
  return false;
}//Dbacc::getScanElement()

/* --------------------------------------------------------------------------------- */
/*  INIT_SCAN_OP_REC                                                                 */
/* --------------------------------------------------------------------------------- */
void Dbacc::initScanOpRec(Signal* signal) 
{
  Uint32 tisoTmp;
  Uint32 tisoLocalPtr;
  Uint32 guard24;

  scanPtr.p->scanOpsAllocated++;

  operationRecPtr.p->userptr = RNIL;
  operationRecPtr.p->scanRecPtr = scanPtr.i;
  operationRecPtr.p->operation = ZSCAN_OP;
  operationRecPtr.p->transactionstate = ACTIVE;
  operationRecPtr.p->commitDeleteCheckFlag = ZFALSE;
  operationRecPtr.p->lockMode = scanPtr.p->scanLockMode;
  operationRecPtr.p->fid = fragrecptr.p->myfid;
  operationRecPtr.p->fragptr = fragrecptr.i;
  operationRecPtr.p->elementIsDisappeared = ZFALSE;
  operationRecPtr.p->nextParallelQue = RNIL;
  operationRecPtr.p->prevParallelQue = RNIL;
  operationRecPtr.p->nextSerialQue = RNIL;
  operationRecPtr.p->prevSerialQue = RNIL;
  operationRecPtr.p->prevQueOp = RNIL;
  operationRecPtr.p->nextQueOp = RNIL;
  operationRecPtr.p->keyinfoPage = RNIL; // Safety precaution 
  operationRecPtr.p->transId1 = scanPtr.p->scanTrid1;
  operationRecPtr.p->transId2 = scanPtr.p->scanTrid2;
  operationRecPtr.p->lockOwner = ZFALSE;
  operationRecPtr.p->dirtyRead = 0;
  operationRecPtr.p->nodeType = 0; // Not a stand-by node
  operationRecPtr.p->elementIsforward = tisoIsforward;
  operationRecPtr.p->elementContainer = tisoContainerptr;
  operationRecPtr.p->elementPointer = tisoElementptr;
  operationRecPtr.p->elementPage = isoPageptr.i;
  operationRecPtr.p->isAccLockReq = ZFALSE;
  tisoLocalPtr = tisoElementptr + tisoIsforward;
  guard24 = fragrecptr.p->localkeylen - 1;
  for (tisoTmp = 0; tisoTmp <= guard24; tisoTmp++) {
    arrGuard(tisoTmp, 2);
    arrGuard(tisoLocalPtr, 2048);
    operationRecPtr.p->localdata[tisoTmp] = isoPageptr.p->word32[tisoLocalPtr];
    tisoLocalPtr = tisoLocalPtr + tisoIsforward;
  }//for
  arrGuard(tisoLocalPtr, 2048);
  operationRecPtr.p->keydata[0] = isoPageptr.p->word32[tisoLocalPtr];
  operationRecPtr.p->tupkeylen = fragrecptr.p->keyLength;
  operationRecPtr.p->xfrmtupkeylen = 0; // not used
}//Dbacc::initScanOpRec()

/* --------------------------------------------------------------------------------- */
/* NEXTCONTAINERINFO                                                                 */
/*        DESCRIPTION:THE CONTAINER HEAD WILL BE CHECKED TO CALCULATE INFORMATION    */
/*                    ABOUT NEXT CONTAINER IN THE BUCKET.                            */
/*          INPUT:       TNCI_CONTAINERHEAD                                          */
/*                       NCI_PAGEIDPTR                                               */
/*                       TNCI_CONTAINERPTR                                           */
/*          OUTPUT:                                                                  */
/*             TNCI_PAGEINDEX (INDEX FROM WHICH PAGE INDEX CAN BE CALCULATED).       */
/*             TNCI_ISFORWARD (IS THE NEXT CONTAINER FORWARD (+1) OR BACKWARD (-1)   */
/*             NCI_PAGEIDPTR (PAGE REFERENCE OF NEXT CONTAINER)                      */
/* --------------------------------------------------------------------------------- */
void Dbacc::nextcontainerinfo(Signal* signal) 
{
  tnciNextSamePage = (tnciContainerhead >> 9) & 0x1;	/* CHECK BIT FOR CHECKING WHERE */
  /* THE NEXT CONTAINER IS IN THE SAME PAGE */
  tnciPageindex = tnciContainerhead & 0x7f;	/* NEXT CONTAINER PAGE INDEX 7 BITS */
  if (((tnciContainerhead >> 7) & 3) == ZLEFT) {
    jam();
    tnciIsforward = ZTRUE;
  } else {
    jam();
    tnciIsforward = cminusOne;
  }//if
  if (tnciNextSamePage == ZFALSE) {
    jam();
    /* NEXT CONTAINER IS IN AN OVERFLOW PAGE */
    arrGuard(tnciContainerptr + 1, 2048);
    tnciTmp = nciPageidptr.p->word32[tnciContainerptr + 1];
    nciOverflowrangeptr.i = fragrecptr.p->overflowdir;
    ptrCheckGuard(nciOverflowrangeptr, cdirrangesize, dirRange);
    arrGuard((tnciTmp >> 8), 256);
    nciOverflowDirptr.i = nciOverflowrangeptr.p->dirArray[tnciTmp >> 8];
    ptrCheckGuard(nciOverflowDirptr, cdirarraysize, directoryarray);
    nciPageidptr.i = nciOverflowDirptr.p->pagep[tnciTmp & 0xff];
    ptrCheckGuard(nciPageidptr, cpagesize, page8);
  }//if
}//Dbacc::nextcontainerinfo()

/* --------------------------------------------------------------------------------- */
/* PUT_ACTIVE_SCAN_OP                                                                */
/* --------------------------------------------------------------------------------- */
void Dbacc::putActiveScanOp(Signal* signal) 
{
  OperationrecPtr pasOperationRecPtr;
  pasOperationRecPtr.i = scanPtr.p->scanFirstActiveOp;
  if (pasOperationRecPtr.i != RNIL) {
    jam();
    ptrCheckGuard(pasOperationRecPtr, coprecsize, operationrec);
    pasOperationRecPtr.p->prevOp = operationRecPtr.i;
  }//if
  operationRecPtr.p->nextOp = pasOperationRecPtr.i;
  operationRecPtr.p->prevOp = RNIL;
  scanPtr.p->scanFirstActiveOp = operationRecPtr.i;
}//Dbacc::putActiveScanOp()

/**
 * putOpScanLockQueue
 *
 * Description: Put an operation in the doubly linked 
 * lock list on a scan record. The list is used to 
 * keep track of which operations belonging
 * to the scan are put in serial lock list of another 
 * operation
 *
 * @note Use takeOutScanLockQueue to remove an operation
 *       from the list
 *
 */
void Dbacc::putOpScanLockQue() 
{

#ifdef VM_TRACE
  // DEBUG CODE
  // Check that there are as many operations in the lockqueue as 
  // scanLockHeld indicates
  OperationrecPtr tmpOp;
  int numLockedOpsBefore = 0;
  tmpOp.i = scanPtr.p->scanFirstLockedOp;
  while(tmpOp.i != RNIL){
    numLockedOpsBefore++;
    ptrCheckGuard(tmpOp, coprecsize, operationrec);
    if (tmpOp.p->nextOp == RNIL)
      ndbrequire(tmpOp.i == scanPtr.p->scanLastLockedOp);
    tmpOp.i = tmpOp.p->nextOp;
  } 
  ndbrequire(numLockedOpsBefore==scanPtr.p->scanLockHeld);
#endif

  OperationrecPtr pslOperationRecPtr;
  ScanRec theScanRec;
  theScanRec = *scanPtr.p;

  pslOperationRecPtr.i = scanPtr.p->scanLastLockedOp;
  operationRecPtr.p->prevOp = pslOperationRecPtr.i;
  operationRecPtr.p->nextOp = RNIL;
  if (pslOperationRecPtr.i != RNIL) {
    jam();
    ptrCheckGuard(pslOperationRecPtr, coprecsize, operationrec);
    pslOperationRecPtr.p->nextOp = operationRecPtr.i;
  } else {
    jam();
    scanPtr.p->scanFirstLockedOp = operationRecPtr.i;
  }//if
  scanPtr.p->scanLastLockedOp = operationRecPtr.i;
  scanPtr.p->scanLockHeld++;

}//Dbacc::putOpScanLockQue()

/* --------------------------------------------------------------------------------- */
/* PUT_READY_SCAN_QUEUE                                                              */
/* --------------------------------------------------------------------------------- */
void Dbacc::putReadyScanQueue(Signal* signal, Uint32 scanRecIndex) 
{
  OperationrecPtr prsOperationRecPtr;
  ScanRecPtr TscanPtr;

  TscanPtr.i = scanRecIndex;
  ptrCheckGuard(TscanPtr, cscanRecSize, scanRec);

  prsOperationRecPtr.i = TscanPtr.p->scanLastQueuedOp;
  operationRecPtr.p->prevOp = prsOperationRecPtr.i;
  operationRecPtr.p->nextOp = RNIL;
  TscanPtr.p->scanLastQueuedOp = operationRecPtr.i;
  if (prsOperationRecPtr.i != RNIL) {
    jam();
    ptrCheckGuard(prsOperationRecPtr, coprecsize, operationrec);
    prsOperationRecPtr.p->nextOp = operationRecPtr.i;
  } else {
    jam();
    TscanPtr.p->scanFirstQueuedOp = operationRecPtr.i;
  }//if
}//Dbacc::putReadyScanQueue()

/* --------------------------------------------------------------------------------- */
/* RELEASE_SCAN_BUCKET                                                               */
// Input:
//   rsbPageidptr.i     Index to page where buckets starts
//   rsbPageidptr.p     Pointer to page where bucket starts
//   trsbPageindex      Page index of starting container in bucket
/* --------------------------------------------------------------------------------- */
void Dbacc::releaseScanBucket(Signal* signal) 
{
  Uint32 trsbIsforward;

  trsbIsforward = ZTRUE;
 NEXTRELEASESCANLOOP:
  ciPageidptr.i = rsbPageidptr.i;
  ciPageidptr.p = rsbPageidptr.p;
  tciPageindex = trsbPageindex;
  tciIsforward = trsbIsforward;
  containerinfo(signal);
  rscPageidptr.i = rsbPageidptr.i;
  rscPageidptr.p = rsbPageidptr.p;
  trscContainerlen = tciContainerlen;
  trscContainerptr = tciContainerptr;
  trscIsforward = trsbIsforward;
  releaseScanContainer(signal);
  if (((tciContainerhead >> 7) & 0x3) != 0) {
    jam();
    nciPageidptr.i = rsbPageidptr.i;
    nciPageidptr.p = rsbPageidptr.p;
    tnciContainerhead = tciContainerhead;
    tnciContainerptr = tciContainerptr;
    nextcontainerinfo(signal);
    rsbPageidptr.i = nciPageidptr.i;
    rsbPageidptr.p = nciPageidptr.p;
    trsbPageindex = tnciPageindex;
    trsbIsforward = tnciIsforward;
    goto NEXTRELEASESCANLOOP;
  }//if
}//Dbacc::releaseScanBucket()

/* --------------------------------------------------------------------------------- */
/*  RELEASE_SCAN_CONTAINER                                                           */
/*       INPUT:           TRSC_CONTAINERLEN                                          */
/*                        RSC_PAGEIDPTR                                              */
/*                        TRSC_CONTAINERPTR                                          */
/*                        TRSC_ISFORWARD                                             */
/*                        SCAN_PTR                                                   */
/*                                                                                   */
/*            DESCRIPTION: SEARCHS IN A CONTAINER, AND THE SCAN BIT OF THE ELEMENTS  */
/*                            OF THE CONTAINER IS RESET                              */
/* --------------------------------------------------------------------------------- */
void Dbacc::releaseScanContainer(Signal* signal) 
{
  OperationrecPtr rscOperPtr;
  Uint32 trscElemStep;
  Uint32 trscElementptr;
  Uint32 trscElemlens;
  Uint32 trscElemlen;

  if (trscContainerlen < 4) {
    if (trscContainerlen != ZCON_HEAD_SIZE) {
      jam();
      sendSystemerror(signal, __LINE__);
    }//if
    return;	/* 2 IS THE MINIMUM SIZE OF THE ELEMENT */
  }//if
  trscElemlens = trscContainerlen - ZCON_HEAD_SIZE;
  trscElemlen = fragrecptr.p->elementLength;
  if (trscIsforward == 1) {
    jam();
    trscElementptr = trscContainerptr + ZCON_HEAD_SIZE;
    trscElemStep = trscElemlen;
  } else {
    jam();
    trscElementptr = trscContainerptr - 1;
    trscElemStep = 0 - trscElemlen;
  }//if
  do {
    arrGuard(trscElementptr, 2048);
    const Uint32 eh = rscPageidptr.p->word32[trscElementptr];
    const Uint32 scanMask = scanPtr.p->scanMask;
    if (ElementHeader::getUnlocked(eh)) {
      jam();
      const Uint32 tmp = ElementHeader::clearScanBit(eh, scanMask);
      dbgWord32(rscPageidptr, trscElementptr, tmp);
      rscPageidptr.p->word32[trscElementptr] = tmp;
    } else {
      jam();
      rscOperPtr.i = ElementHeader::getOpPtrI(eh);
      ptrCheckGuard(rscOperPtr, coprecsize, operationrec);
      rscOperPtr.p->scanBits &= ~scanMask;
    }//if
    trscElemlens = trscElemlens - trscElemlen;
    trscElementptr = trscElementptr + trscElemStep;
  } while (trscElemlens > 1);
  if (trscElemlens != 0) {
    jam();
    sendSystemerror(signal, __LINE__);
  }//if
}//Dbacc::releaseScanContainer()

/* --------------------------------------------------------------------------------- */
/* RELEASE_SCAN_REC                                                                  */
/* --------------------------------------------------------------------------------- */
void Dbacc::releaseScanRec(Signal* signal) 
{  
  // Check that all ops this scan has allocated have been 
  // released
  ndbrequire(scanPtr.p->scanOpsAllocated==0);

  // Check that all locks this scan might have aquired 
  // have been properly released
  ndbrequire(scanPtr.p->scanLockHeld == 0);
  ndbrequire(scanPtr.p->scanFirstLockedOp == RNIL);
  ndbrequire(scanPtr.p->scanLastLockedOp == RNIL);

  // Check that all active operations have been 
  // properly released
  ndbrequire(scanPtr.p->scanFirstActiveOp == RNIL);

  // Check that all queued operations have been 
  // properly released
  ndbrequire(scanPtr.p->scanFirstQueuedOp == RNIL);
  ndbrequire(scanPtr.p->scanLastQueuedOp == RNIL);

  // Put scan record in free list
  scanPtr.p->scanNextfreerec = cfirstFreeScanRec;
  scanPtr.p->scanState = ScanRec::SCAN_DISCONNECT;
  cfirstFreeScanRec = scanPtr.i;

}//Dbacc::releaseScanRec()

/* --------------------------------------------------------------------------------- */
/*  SEARCH_SCAN_CONTAINER                                                            */
/*       INPUT:           TSSC_CONTAINERLEN                                          */
/*                        TSSC_CONTAINERPTR                                          */
/*                        TSSC_ISFORWARD                                             */
/*                        SSC_PAGEIDPTR                                              */
/*                        SCAN_PTR                                                   */
/*       OUTPUT:          TSSC_IS_LOCKED                                             */
/*                                                                                   */
/*            DESCRIPTION: SEARCH IN A CONTAINER TO FIND THE NEXT SCAN ELEMENT.      */
/*                    TO DO THIS THE SCAN BIT OF THE ELEMENT HEADER IS CHECKED. IF   */
/*                    THIS BIT IS ZERO, IT IS SET TO ONE AND THE ELEMENT IS RETURNED.*/
/* --------------------------------------------------------------------------------- */
bool Dbacc::searchScanContainer(Signal* signal) 
{
  OperationrecPtr sscOperPtr;
  Uint32 tsscScanBits;
  Uint32 tsscElemlens;
  Uint32 tsscElemlen;
  Uint32 tsscElemStep;

  if (tsscContainerlen < 4) {
    jam();
    return false;	/* 2 IS THE MINIMUM SIZE OF THE ELEMENT */
  }//if
  tsscElemlens = tsscContainerlen - ZCON_HEAD_SIZE;
  tsscElemlen = fragrecptr.p->elementLength;
  /* LENGTH OF THE ELEMENT */
  if (tsscIsforward == 1) {
    jam();
    tsscElementptr = tsscContainerptr + ZCON_HEAD_SIZE;
    tsscElemStep = tsscElemlen;
  } else {
    jam();
    tsscElementptr = tsscContainerptr - 1;
    tsscElemStep = 0 - tsscElemlen;
  }//if
 SCANELEMENTLOOP001:
  arrGuard(tsscElementptr, 2048);
  const Uint32 eh = sscPageidptr.p->word32[tsscElementptr];
  tsscIsLocked = ElementHeader::getLocked(eh);
  if (!tsscIsLocked){
    jam();
    tsscScanBits = ElementHeader::getScanBits(eh);
    if ((scanPtr.p->scanMask & tsscScanBits) == 0) {
      jam();
      const Uint32 tmp = ElementHeader::setScanBit(eh, scanPtr.p->scanMask);
      dbgWord32(sscPageidptr, tsscElementptr, tmp);
      sscPageidptr.p->word32[tsscElementptr] = tmp;
      return true;
    }//if
  } else {
    jam();
    sscOperPtr.i = ElementHeader::getOpPtrI(eh);
    ptrCheckGuard(sscOperPtr, coprecsize, operationrec);
    if ((sscOperPtr.p->scanBits & scanPtr.p->scanMask) == 0) {
      jam();
      sscOperPtr.p->scanBits |= scanPtr.p->scanMask;
      return true;
    }//if
  }//if
  /* THE ELEMENT IS ALREADY SENT. */
  /* SEARCH FOR NEXT ONE */
  tsscElemlens = tsscElemlens - tsscElemlen;
  if (tsscElemlens > 1) {
    jam();
    tsscElementptr = tsscElementptr + tsscElemStep;
    goto SCANELEMENTLOOP001;
  }//if
  return false;
}//Dbacc::searchScanContainer()

/* --------------------------------------------------------------------------------- */
/*  SEND THE RESPONSE NEXT_SCANCONF AND POSSIBLE KEYINFO SIGNALS AS WELL.            */
/* --------------------------------------------------------------------------------- */
void Dbacc::sendNextScanConf(Signal* signal) 
{
  scanPtr.p->scanTimer = scanPtr.p->scanContinuebCounter;
  Uint32 blockNo = refToBlock(scanPtr.p->scanUserblockref);
  jam();
  /** ---------------------------------------------------------------------
   * LQH WILL NOT HAVE ANY USE OF THE TUPLE KEY LENGTH IN THIS CASE AND 
   * SO WE DO NOT PROVIDE IT. IN THIS CASE THESE VALUES ARE UNDEFINED. 
   * ---------------------------------------------------------------------- */
  signal->theData[0] = scanPtr.p->scanUserptr;
  signal->theData[1] = operationRecPtr.i;
  signal->theData[2] = operationRecPtr.p->fid;
  signal->theData[3] = operationRecPtr.p->localdata[0];
  signal->theData[4] = operationRecPtr.p->localdata[1];
  signal->theData[5] = fragrecptr.p->localkeylen;
  EXECUTE_DIRECT(blockNo, GSN_NEXT_SCANCONF, signal, 6);
  return;
}//Dbacc::sendNextScanConf()

/*---------------------------------------------------------------------------
 * sendScanHbRep     	      	             	      	             	       
 * Description: Using Dispatcher::execute() to send a heartbeat to DBTC
 *     	from DBLQH telling the scan is alive. We use the sendScanHbRep()
 *     	in DBLQH, this needs to be done here in DBACC since it can take
 *	a while before LQH receives an answer the normal way from ACC. 
 *--------------------------------------------------------------------------*/
void Dbacc::sendScanHbRep(Signal* signal, Uint32 scanPtrIndex)
{
  scanPtr.i = scanPtrIndex;
  ptrCheckGuard(scanPtr, cscanRecSize, scanRec);
 
  // If the timer status is on we continue with a new heartbeat in one second,
  // else the loop stops and we will not send a new CONTINUEB
  if (scanPtr.p->scanTimer != 0){
    if (scanPtr.p->scanTimer == scanPtr.p->scanContinuebCounter){
      jam();
      ndbrequire(scanPtr.p->scanState != ScanRec::SCAN_DISCONNECT);

      signal->theData[0] = scanPtr.p->scanUserptr;
      signal->theData[1] = scanPtr.p->scanTrid1;
      signal->theData[2] = scanPtr.p->scanTrid2;
      EXECUTE_DIRECT(DBLQH, GSN_SCAN_HBREP, signal, 3);
      jamEntry();
    }//if
    scanPtr.p->scanContinuebCounter++;
    signal->theData[0] = ZSEND_SCAN_HBREP;
    signal->theData[1] = scanPtr.i;
    sendSignalWithDelay(cownBlockref, GSN_CONTINUEB, signal, 100, 2);
  } else {
    jam();
    scanPtr.p->scanContinuebCounter = 0;
  }//if
}//Dbacc::sendScanHbRep()			

/* --------------------------------------------------------------------------------- */
/* SETLOCK                                                                           */
/*          DESCRIPTION:SETS LOCK ON AN ELEMENT. INFORMATION ABOUT THE ELEMENT IS    */
/*                      SAVED IN THE ELEMENT HEAD.A COPY OF THIS INFORMATION WILL    */
/*                       BE PUT IN THE OPERATION RECORD. A FIELD IN THE  HEADER OF   */
/*                       THE ELEMENT POINTS TO THE OPERATION RECORD.                 */
/* --------------------------------------------------------------------------------- */
void Dbacc::setlock(Signal* signal) 
{
  Uint32 tselTmp1;

  arrGuard(tslElementptr, 2048);
  tselTmp1 = slPageidptr.p->word32[tslElementptr];
  operationRecPtr.p->scanBits = ElementHeader::getScanBits(tselTmp1);
  operationRecPtr.p->hashvaluePart = ElementHeader::getHashValuePart(tselTmp1);

  tselTmp1 = ElementHeader::setLocked(operationRecPtr.i);
  dbgWord32(slPageidptr, tslElementptr, tselTmp1);
  slPageidptr.p->word32[tslElementptr] = tselTmp1;
}//Dbacc::setlock()

/* --------------------------------------------------------------------------------- */
/*  TAKE_OUT_ACTIVE_SCAN_OP                                                          */
/*         DESCRIPTION: AN ACTIVE SCAN OPERATION IS BELOGED TO AN ACTIVE LIST OF THE */
/*                      SCAN RECORD. BY THIS SUBRUTIN THE LIST IS UPDATED.           */
/* --------------------------------------------------------------------------------- */
void Dbacc::takeOutActiveScanOp(Signal* signal) 
{
  OperationrecPtr tasOperationRecPtr;

  if (operationRecPtr.p->prevOp != RNIL) {
    jam();
    tasOperationRecPtr.i = operationRecPtr.p->prevOp;
    ptrCheckGuard(tasOperationRecPtr, coprecsize, operationrec);
    tasOperationRecPtr.p->nextOp = operationRecPtr.p->nextOp;
  } else {
    jam();
    scanPtr.p->scanFirstActiveOp = operationRecPtr.p->nextOp;
  }//if
  if (operationRecPtr.p->nextOp != RNIL) {
    jam();
    tasOperationRecPtr.i = operationRecPtr.p->nextOp;
    ptrCheckGuard(tasOperationRecPtr, coprecsize, operationrec);
    tasOperationRecPtr.p->prevOp = operationRecPtr.p->prevOp;
  }//if
}//Dbacc::takeOutActiveScanOp()

/**
 * takeOutScanLockQueue
 *
 * Description: Take out an operation from the doubly linked 
 * lock list on a scan record.
 *
 * @note Use putOpScanLockQue to insert a operation in 
 *       the list
 *
 */
void Dbacc::takeOutScanLockQueue(Uint32 scanRecIndex) 
{
  OperationrecPtr tslOperationRecPtr;
  ScanRecPtr TscanPtr;

  TscanPtr.i = scanRecIndex;
  ptrCheckGuard(TscanPtr, cscanRecSize, scanRec);

  if (operationRecPtr.p->prevOp != RNIL) {
    jam();
    tslOperationRecPtr.i = operationRecPtr.p->prevOp;
    ptrCheckGuard(tslOperationRecPtr, coprecsize, operationrec);
    tslOperationRecPtr.p->nextOp = operationRecPtr.p->nextOp;
  } else {
    jam();
    // Check that first are pointing at operation to take out
    ndbrequire(TscanPtr.p->scanFirstLockedOp==operationRecPtr.i);
    TscanPtr.p->scanFirstLockedOp = operationRecPtr.p->nextOp;
  }//if
  if (operationRecPtr.p->nextOp != RNIL) {
    jam();
    tslOperationRecPtr.i = operationRecPtr.p->nextOp;
    ptrCheckGuard(tslOperationRecPtr, coprecsize, operationrec);
    tslOperationRecPtr.p->prevOp = operationRecPtr.p->prevOp;
  } else {
    jam();
    // Check that last are pointing at operation to take out
    ndbrequire(TscanPtr.p->scanLastLockedOp==operationRecPtr.i);
    TscanPtr.p->scanLastLockedOp = operationRecPtr.p->prevOp;
  }//if
  TscanPtr.p->scanLockHeld--;

#ifdef VM_TRACE
  // DEBUG CODE
  // Check that there are as many operations in the lockqueue as 
  // scanLockHeld indicates
  OperationrecPtr tmpOp;
  int numLockedOps = 0;
  tmpOp.i = TscanPtr.p->scanFirstLockedOp;
  while(tmpOp.i != RNIL){
    numLockedOps++;
    ptrCheckGuard(tmpOp, coprecsize, operationrec);
    if (tmpOp.p->nextOp == RNIL)
      ndbrequire(tmpOp.i == TscanPtr.p->scanLastLockedOp);
    tmpOp.i = tmpOp.p->nextOp;
  } 
  ndbrequire(numLockedOps==TscanPtr.p->scanLockHeld);
#endif
}//Dbacc::takeOutScanLockQueue()

/* --------------------------------------------------------------------------------- */
/* TAKE_OUT_READY_SCAN_QUEUE                                                         */
/* --------------------------------------------------------------------------------- */
void Dbacc::takeOutReadyScanQueue(Signal* signal) 
{
  OperationrecPtr trsOperationRecPtr;

  if (operationRecPtr.p->prevOp != RNIL) {
    jam();
    trsOperationRecPtr.i = operationRecPtr.p->prevOp;
    ptrCheckGuard(trsOperationRecPtr, coprecsize, operationrec);
    trsOperationRecPtr.p->nextOp = operationRecPtr.p->nextOp;
  } else {
    jam();
    scanPtr.p->scanFirstQueuedOp = operationRecPtr.p->nextOp;
  }//if
  if (operationRecPtr.p->nextOp != RNIL) {
    jam();
    trsOperationRecPtr.i = operationRecPtr.p->nextOp;
    ptrCheckGuard(trsOperationRecPtr, coprecsize, operationrec);
    trsOperationRecPtr.p->prevOp = operationRecPtr.p->prevOp;
  } else {
    jam();
    scanPtr.p->scanLastQueuedOp = operationRecPtr.p->nextOp;
  }//if
}//Dbacc::takeOutReadyScanQueue()

/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/*                                                                                   */
/*       END OF SCAN MODULE                                                          */
/*                                                                                   */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */

bool Dbacc::getfragmentrec(Signal* signal, FragmentrecPtr& rootPtr, Uint32 fid) 
{
  for (Uint32 i = 0; i < MAX_FRAG_PER_NODE; i++) {
    jam();
    if (tabptr.p->fragholder[i] == fid) {
      jam();
      fragrecptr.i = tabptr.p->fragptrholder[i];
      ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
      return true;
    }//if
  }//for
  return false;
}//Dbacc::getrootfragmentrec()

/* --------------------------------------------------------------------------------- */
/* INIT_OVERPAGE                                                                     */
/*         INPUT. IOP_PAGEPTR, POINTER TO AN OVERFLOW PAGE RECORD                    */
/*         DESCRIPTION: CONTAINERS AND FREE LISTS OF THE PAGE, GET INITIALE VALUE    */
/*         ACCORDING TO LH3 AND PAGE STRUCTOR DESCRIPTION OF NDBACC BLOCK            */
/* --------------------------------------------------------------------------------- */
void Dbacc::initOverpage(Signal* signal) 
{
  Uint32 tiopTmp;
  Uint32 tiopPrevFree;
  Uint32 tiopNextFree;

  for (tiopIndex = 0; tiopIndex <= 2047; tiopIndex++) {
    iopPageptr.p->word32[tiopIndex] = 0;
  }//for
  iopPageptr.p->word32[ZPOS_OVERFLOWREC] = iopOverflowRecPtr.i;
  iopPageptr.p->word32[ZPOS_CHECKSUM] = 0;
  iopPageptr.p->word32[ZPOS_PAGE_ID] = tiopPageId;
  iopPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] = 0;
  tiopTmp = ZEMPTYLIST;
  tiopTmp = (tiopTmp << 16) + (tiopTmp << 23);
  iopPageptr.p->word32[ZPOS_EMPTY_LIST] = tiopTmp + (1 << ZPOS_PAGE_TYPE_BIT);
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE PREVIOUS PART OF DOUBLY LINKED LIST FOR LEFT CONTAINERS.         */
  /* --------------------------------------------------------------------------------- */
  tiopIndex = ZHEAD_SIZE + 1;
  iopPageptr.p->word32[tiopIndex] = ZEMPTYLIST;
  for (tiopPrevFree = 0; tiopPrevFree <= ZEMPTYLIST - 2; tiopPrevFree++) {
    tiopIndex = tiopIndex + ZBUF_SIZE;
    iopPageptr.p->word32[tiopIndex] = tiopPrevFree;
  }//for
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE NEXT PART OF DOUBLY LINKED LIST FOR LEFT CONTAINERS.             */
  /* --------------------------------------------------------------------------------- */
  tiopIndex = ZHEAD_SIZE;
  for (tiopNextFree = 1; tiopNextFree <= ZEMPTYLIST - 1; tiopNextFree++) {
    iopPageptr.p->word32[tiopIndex] = tiopNextFree;
    tiopIndex = tiopIndex + ZBUF_SIZE;
  }//for
  iopPageptr.p->word32[tiopIndex] = ZEMPTYLIST;	/* LEFT_LIST IS UPDATED */
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE PREVIOUS PART OF DOUBLY LINKED LIST FOR RIGHT CONTAINERS.        */
  /* --------------------------------------------------------------------------------- */
  tiopIndex = (ZBUF_SIZE + ZHEAD_SIZE) - 1;
  iopPageptr.p->word32[tiopIndex] = ZEMPTYLIST;
  for (tiopPrevFree = 0; tiopPrevFree <= ZEMPTYLIST - 2; tiopPrevFree++) {
    tiopIndex = tiopIndex + ZBUF_SIZE;
    iopPageptr.p->word32[tiopIndex] = tiopPrevFree;
  }//for
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE NEXT PART OF DOUBLY LINKED LIST FOR RIGHT CONTAINERS.            */
  /* --------------------------------------------------------------------------------- */
  tiopIndex = (ZBUF_SIZE + ZHEAD_SIZE) - 2;
  for (tiopNextFree = 1; tiopNextFree <= ZEMPTYLIST - 1; tiopNextFree++) {
    iopPageptr.p->word32[tiopIndex] = tiopNextFree;
    tiopIndex = tiopIndex + ZBUF_SIZE;
  }//for
  iopPageptr.p->word32[tiopIndex] = ZEMPTYLIST;	/* RIGHT_LIST IS UPDATED */
}//Dbacc::initOverpage()

/* --------------------------------------------------------------------------------- */
/* INIT_PAGE                                                                         */
/*         INPUT. INP_PAGEPTR, POINTER TO A PAGE RECORD                              */
/*         DESCRIPTION: CONTAINERS AND FREE LISTS OF THE PAGE, GET INITIALE VALUE    */
/*         ACCORDING TO LH3 AND PAGE STRUCTOR DISACRIPTION OF NDBACC BLOCK           */
/* --------------------------------------------------------------------------------- */
void Dbacc::initPage(Signal* signal) 
{
  Uint32 tinpTmp1;
  Uint32 tinpIndex;
  Uint32 tinpTmp;
  Uint32 tinpPrevFree;
  Uint32 tinpNextFree;

  for (tiopIndex = 0; tiopIndex <= 2047; tiopIndex++) {
    inpPageptr.p->word32[tiopIndex] = 0;
  }//for
  /* --------------------------------------------------------------------------------- */
  /*       SET PAGE ID FOR USE OF CHECKPOINTER.                                        */
  /*       PREPARE CONTAINER HEADERS INDICATING EMPTY CONTAINERS WITHOUT NEXT.         */
  /* --------------------------------------------------------------------------------- */
  inpPageptr.p->word32[ZPOS_PAGE_ID] = tipPageId;
  tinpTmp1 = ZCON_HEAD_SIZE;
  tinpTmp1 = tinpTmp1 << 26;
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE ZNO_CONTAINERS PREDEFINED HEADERS ON LEFT SIZE.                  */
  /* --------------------------------------------------------------------------------- */
  tinpIndex = ZHEAD_SIZE;
  for (tinpTmp = 0; tinpTmp <= ZNO_CONTAINERS - 1; tinpTmp++) {
    inpPageptr.p->word32[tinpIndex] = tinpTmp1;
    tinpIndex = tinpIndex + ZBUF_SIZE;
  }//for
  /* WORD32(ZPOS_EMPTY_LIST) DATA STRUCTURE:*/
  /*--------------------------------------- */
  /*| PAGE TYPE|LEFT FREE|RIGHT FREE        */
  /*|     1    |  LIST   |  LIST            */
  /*|    BIT   | 7 BITS  | 7 BITS           */
  /*--------------------------------------- */
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE FIRST POINTER TO DOUBLY LINKED LIST OF FREE CONTAINERS.          */
  /*       INITIALISE EMPTY LISTS OF USED CONTAINERS.                                  */
  /*       INITIALISE LEFT FREE LIST TO 64 AND RIGHT FREE LIST TO ZERO.                */
  /*       ALSO INITIALISE PAGE TYPE TO NOT OVERFLOW PAGE.                             */
  /* --------------------------------------------------------------------------------- */
  tinpTmp = ZEMPTYLIST;
  tinpTmp = (tinpTmp << 16) + (tinpTmp << 23);
  tinpTmp = tinpTmp + (ZNO_CONTAINERS << 7);
  inpPageptr.p->word32[ZPOS_EMPTY_LIST] = tinpTmp;
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE PREVIOUS PART OF DOUBLY LINKED LIST FOR RIGHT CONTAINERS.        */
  /* --------------------------------------------------------------------------------- */
  tinpIndex = (ZHEAD_SIZE + ZBUF_SIZE) - 1;
  inpPageptr.p->word32[tinpIndex] = ZEMPTYLIST;
  for (tinpPrevFree = 0; tinpPrevFree <= ZEMPTYLIST - 2; tinpPrevFree++) {
    tinpIndex = tinpIndex + ZBUF_SIZE;
    inpPageptr.p->word32[tinpIndex] = tinpPrevFree;
  }//for
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE NEXT PART OF DOUBLY LINKED LIST FOR RIGHT CONTAINERS.            */
  /* --------------------------------------------------------------------------------- */
  tinpIndex = (ZHEAD_SIZE + ZBUF_SIZE) - 2;
  for (tinpNextFree = 1; tinpNextFree <= ZEMPTYLIST - 1; tinpNextFree++) {
    inpPageptr.p->word32[tinpIndex] = tinpNextFree;
    tinpIndex = tinpIndex + ZBUF_SIZE;
  }//for
  inpPageptr.p->word32[tinpIndex] = ZEMPTYLIST;
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE PREVIOUS PART OF DOUBLY LINKED LIST FOR LEFT CONTAINERS.         */
  /*       THE FIRST ZNO_CONTAINERS ARE NOT PUT INTO FREE LIST SINCE THEY ARE          */
  /*       PREDEFINED AS OCCUPIED.                                                     */
  /* --------------------------------------------------------------------------------- */
  tinpIndex = (ZNO_CONTAINERS * ZBUF_SIZE) + ZHEAD_SIZE;
  for (tinpNextFree = ZNO_CONTAINERS + 1; tinpNextFree <= ZEMPTYLIST - 1; tinpNextFree++) {
    inpPageptr.p->word32[tinpIndex] = tinpNextFree;
    tinpIndex = tinpIndex + ZBUF_SIZE;
  }//for
  inpPageptr.p->word32[tinpIndex] = ZEMPTYLIST;
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE NEXT PART OF DOUBLY LINKED LIST FOR LEFT CONTAINERS.             */
  /*       THE FIRST ZNO_CONTAINERS ARE NOT PUT INTO FREE LIST SINCE THEY ARE          */
  /*       PREDEFINED AS OCCUPIED.                                                     */
  /* --------------------------------------------------------------------------------- */
  tinpIndex = ((ZNO_CONTAINERS * ZBUF_SIZE) + ZHEAD_SIZE) + 1;
  inpPageptr.p->word32[tinpIndex] = ZEMPTYLIST;
  for (tinpPrevFree = ZNO_CONTAINERS; tinpPrevFree <= ZEMPTYLIST - 2; tinpPrevFree++) {
    tinpIndex = tinpIndex + ZBUF_SIZE;
    inpPageptr.p->word32[tinpIndex] = tinpPrevFree;
  }//for
  /* --------------------------------------------------------------------------------- */
  /*       INITIALISE HEADER POSITIONS NOT CURRENTLY USED AND ENSURE USE OF OVERFLOW   */
  /*       RECORD POINTER ON THIS PAGE LEADS TO ERROR.                                 */
  /* --------------------------------------------------------------------------------- */
  inpPageptr.p->word32[ZPOS_CHECKSUM] = 0;
  inpPageptr.p->word32[ZPOS_ALLOC_CONTAINERS] = 0;
  inpPageptr.p->word32[ZPOS_OVERFLOWREC] = RNIL;
}//Dbacc::initPage()

/* --------------------------------------------------------------------------------- */
/* PUT_OVERFLOW_REC_IN_FRAG                                                          */
/*         DESCRIPTION: AN OVERFLOW RECORD WITCH IS USED TO KEEP INFORMATION ABOUT   */
/*                      OVERFLOW PAGE WILL BE PUT IN A LIST OF OVERFLOW RECORDS IN   */
/*                      THE FRAGMENT RECORD.                                         */
/* --------------------------------------------------------------------------------- */
void Dbacc::putOverflowRecInFrag(Signal* signal) 
{
  OverflowRecordPtr tpifNextOverrecPtr;
  OverflowRecordPtr tpifPrevOverrecPtr;

  tpifNextOverrecPtr.i = fragrecptr.p->firstOverflowRec;
  tpifPrevOverrecPtr.i = RNIL;
  while (tpifNextOverrecPtr.i != RNIL) {
    ptrCheckGuard(tpifNextOverrecPtr, coverflowrecsize, overflowRecord);
    if (tpifNextOverrecPtr.p->dirindex < porOverflowRecPtr.p->dirindex) {
      jam();
      /* --------------------------------------------------------------------------------- */
      /*       PROCEED IN LIST TO THE NEXT IN THE LIST SINCE THE ENTRY HAD A LOWER PAGE ID.*/
      /*       WE WANT TO ENSURE THAT LOWER PAGE ID'S ARE KEPT FULL RATHER THAN THE        */
      /*       OPPOSITE TO ENSURE THAT HIGH PAGE ID'S CAN BE REMOVED WHEN SHRINKS ARE      */
      /*       PERFORMED.                                                                  */
      /* --------------------------------------------------------------------------------- */
      tpifPrevOverrecPtr = tpifNextOverrecPtr;
      tpifNextOverrecPtr.i = tpifNextOverrecPtr.p->nextOverRec;
    } else {
      jam();
      ndbrequire(tpifNextOverrecPtr.p->dirindex != porOverflowRecPtr.p->dirindex);
      /* --------------------------------------------------------------------------------- */
      /*       TRYING TO INSERT THE SAME PAGE TWICE. SYSTEM ERROR.                         */
      /* --------------------------------------------------------------------------------- */
      break;
    }//if
  }//while
  if (tpifNextOverrecPtr.i == RNIL) {
    jam();
    fragrecptr.p->lastOverflowRec = porOverflowRecPtr.i;
  } else {
    jam();
    tpifNextOverrecPtr.p->prevOverRec = porOverflowRecPtr.i;
  }//if
  if (tpifPrevOverrecPtr.i == RNIL) {
    jam();
    fragrecptr.p->firstOverflowRec = porOverflowRecPtr.i;
  } else {
    jam();
    tpifPrevOverrecPtr.p->nextOverRec = porOverflowRecPtr.i;
  }//if
  porOverflowRecPtr.p->prevOverRec = tpifPrevOverrecPtr.i;
  porOverflowRecPtr.p->nextOverRec = tpifNextOverrecPtr.i;
}//Dbacc::putOverflowRecInFrag()

/* --------------------------------------------------------------------------------- */
/* PUT_REC_IN_FREE_OVERDIR                                                           */
/* --------------------------------------------------------------------------------- */
void Dbacc::putRecInFreeOverdir(Signal* signal) 
{
  OverflowRecordPtr tpfoNextOverrecPtr;
  OverflowRecordPtr tpfoPrevOverrecPtr;

  tpfoNextOverrecPtr.i = fragrecptr.p->firstFreeDirindexRec;
  tpfoPrevOverrecPtr.i = RNIL;
  while (tpfoNextOverrecPtr.i != RNIL) {
    ptrCheckGuard(tpfoNextOverrecPtr, coverflowrecsize, overflowRecord);
    if (tpfoNextOverrecPtr.p->dirindex < priOverflowRecPtr.p->dirindex) {
      jam();
      /* --------------------------------------------------------------------------------- */
      /*       PROCEED IN LIST TO THE NEXT IN THE LIST SINCE THE ENTRY HAD A LOWER PAGE ID.*/
      /*       WE WANT TO ENSURE THAT LOWER PAGE ID'S ARE KEPT FULL RATHER THAN THE        */
      /*       OPPOSITE TO ENSURE THAT HIGH PAGE ID'S CAN BE REMOVED WHEN SHRINKS ARE      */
      /*       PERFORMED.                                                                  */
      /* --------------------------------------------------------------------------------- */
      tpfoPrevOverrecPtr = tpfoNextOverrecPtr;
      tpfoNextOverrecPtr.i = tpfoNextOverrecPtr.p->nextOverList;
    } else {
      jam();
      ndbrequire(tpfoNextOverrecPtr.p->dirindex != priOverflowRecPtr.p->dirindex);
      /* --------------------------------------------------------------------------------- */
      /*       ENSURE WE ARE NOT TRYING TO INSERT THE SAME PAGE TWICE.                     */
      /* --------------------------------------------------------------------------------- */
      break;
    }//if
  }//while
  if (tpfoNextOverrecPtr.i != RNIL) {
    jam();
    tpfoNextOverrecPtr.p->prevOverList = priOverflowRecPtr.i;
  }//if
  if (tpfoPrevOverrecPtr.i == RNIL) {
    jam();
    fragrecptr.p->firstFreeDirindexRec = priOverflowRecPtr.i;
  } else {
    jam();
    tpfoPrevOverrecPtr.p->nextOverList = priOverflowRecPtr.i;
  }//if
  priOverflowRecPtr.p->prevOverList = tpfoPrevOverrecPtr.i;
  priOverflowRecPtr.p->nextOverList = tpfoNextOverrecPtr.i;
}//Dbacc::putRecInFreeOverdir()

/* --------------------------------------------------------------------------------- */
/* RELEASE_DIRECTORY                                                                 */
/* --------------------------------------- ----------------------------------------- */
void Dbacc::releaseDirectory(Signal* signal) 
{
  ptrCheckGuard(rdDirptr, cdirarraysize, directoryarray);
  rdDirptr.p->pagep[0] = cfirstfreedir;
  cfirstfreedir = rdDirptr.i;
}//Dbacc::releaseDirectory()

/* --------------------------------------------------------------------------------- */
/* RELEASE_DIRRANGE                                                                  */
/* --------------------------------------------------------------------------------- */
void Dbacc::releaseDirrange(Signal* signal) 
{
  ptrCheckGuard(rdDirRangePtr, cdirrangesize, dirRange);
  rdDirRangePtr.p->dirArray[0] = cfirstfreeDirrange;
  cfirstfreeDirrange = rdDirRangePtr.i;
}//Dbacc::releaseDirrange()

/* --------------------------------------------------------------------------------- */
/* RELEASE OP RECORD                                                                 */
/*         PUT A FREE OPERATION IN A FREE LIST OF THE OPERATIONS                     */
/* --------------------------------------------------------------------------------- */
void Dbacc::releaseOpRec(Signal* signal) 
{
#if 0
  // DEBUG CODE
  // Check that the operation to be released isn't 
  // already in the list of free operations
  // Since this code loops through the entire list of free operations
  // it's only enabled in VM_TRACE mode
  OperationrecPtr opRecPtr;
  bool opInList = false;
  opRecPtr.i = cfreeopRec;
  while (opRecPtr.i != RNIL){
    if (opRecPtr.i == operationRecPtr.i){
      opInList = true;
      break;
    }
    ptrCheckGuard(opRecPtr, coprecsize, operationrec);
    opRecPtr.i = opRecPtr.p->nextOp;
  }
  ndbrequire(opInList == false);
#endif
  ndbrequire(operationRecPtr.p->lockOwner == ZFALSE);

  operationRecPtr.p->nextOp = cfreeopRec;
  cfreeopRec = operationRecPtr.i;	/* UPDATE FREE LIST OF OP RECORDS */
  operationRecPtr.p->prevOp = RNIL;
  operationRecPtr.p->opState = FREE_OP;
  operationRecPtr.p->transactionstate = IDLE;
  operationRecPtr.p->operation = ZUNDEFINED_OP;
}//Dbacc::releaseOpRec()

/* --------------------------------------------------------------------------------- */
/* RELEASE_OVERFLOW_REC                                                              */
/*         PUT A FREE OVERFLOW REC IN A FREE LIST OF THE OVERFLOW RECORDS            */
/* --------------------------------------------------------------------------------- */
void Dbacc::releaseOverflowRec(Signal* signal) 
{
  rorOverflowRecPtr.p->nextfreeoverrec = cfirstfreeoverrec;
  cfirstfreeoverrec = rorOverflowRecPtr.i;
}//Dbacc::releaseOverflowRec()

/* --------------------------------------------------------------------------------- */
/* RELEASE_OVERPAGE                                                                  */
/* --------------------------------------------------------------------------------- */
void Dbacc::releaseOverpage(Signal* signal) 
{
  DirRangePtr ropOverflowrangeptr;
  DirectoryarrayPtr ropOverflowDirptr;
  OverflowRecordPtr ropOverflowRecPtr;
  OverflowRecordPtr tuodOverflowRecPtr;
  Uint32 tropTmp;
  Uint32 tropTmp1;
  Uint32 tropTmp2;

  ropOverflowRecPtr.i = ropPageptr.p->word32[ZPOS_OVERFLOWREC];
  ndbrequire(ropOverflowRecPtr.i != RNIL);
  /* THE OVERFLOW REC WILL BE TAKEN OUT OF THE */
  /* FREELIST OF OVERFLOW PAGE WITH FREE */
  /* CONTAINER AND WILL BE PUT IN THE FREE LIST */
  /* OF THE FREE DIRECTORY INDEXES. */
  if ((fragrecptr.p->lastOverflowRec == ropOverflowRecPtr.i) &&
      (fragrecptr.p->firstOverflowRec == ropOverflowRecPtr.i)) {
    jam();
    return;	/* THERE IS ONLY ONE OVERFLOW PAGE */
  }//if
#if kalle
  logicalPage = 0;

  i = fragrecptr.p->directory;
  p = dirRange.getPtr(i);

  i1 = logicalPage >> 8;
  i2 = logicalPage & 0xFF;

  ndbrequire(i1 < 256);
  
  i = p->dirArray[i1];
  p = directoryarray.getPtr(i);

  physicPageId = p->pagep[i2];
  physicPageP = page8.getPtr(physicPageId);
  
  p->pagep[i2] = RNIL;
  rpPageptr = { physicPageId, physicPageP };
  releasePage(signal);
  
#endif

  /* ----------------------------------------------------------------------- */
  /* IT WAS OK TO RELEASE THE PAGE.                                          */
  /* ----------------------------------------------------------------------- */
  ptrCheckGuard(ropOverflowRecPtr, coverflowrecsize, overflowRecord);
  tfoOverflowRecPtr = ropOverflowRecPtr;
  takeRecOutOfFreeOverpage(signal);
  ropOverflowRecPtr.p->overpage = RNIL;
  priOverflowRecPtr = ropOverflowRecPtr;
  putRecInFreeOverdir(signal);
  tropTmp = ropPageptr.p->word32[ZPOS_PAGE_ID];
  ropOverflowrangeptr.i = fragrecptr.p->overflowdir;
  tropTmp1 = tropTmp >> 8;
  tropTmp2 = tropTmp & 0xff;
  ptrCheckGuard(ropOverflowrangeptr, cdirrangesize, dirRange);
  arrGuard(tropTmp1, 256);
  ropOverflowDirptr.i = ropOverflowrangeptr.p->dirArray[tropTmp1];
  ptrCheckGuard(ropOverflowDirptr, cdirarraysize, directoryarray);
  ropOverflowDirptr.p->pagep[tropTmp2] = RNIL;
  rpPageptr = ropPageptr;
  releasePage(signal);
  if (ropOverflowRecPtr.p->dirindex != (fragrecptr.p->lastOverIndex - 1)) {
    jam();
    return;
  }//if
  /* ----------------------------------------------------------------------- */
  /* THE LAST PAGE IN THE DIRECTORY WAS RELEASED IT IS NOW NECESSARY 
   * TO REMOVE ALL RELEASED OVERFLOW DIRECTORIES AT THE END OF THE LIST.   
   * ---------------------------------------------------------------------- */
  do {
    fragrecptr.p->lastOverIndex--;
    if (tropTmp2 == 0) {
      jam();
      ndbrequire(tropTmp1 != 0);
      ropOverflowrangeptr.p->dirArray[tropTmp1] = RNIL;
      rdDirptr.i = ropOverflowDirptr.i;
      releaseDirectory(signal);
      tropTmp1--;
      tropTmp2 = 255;
    } else {
      jam();
      tropTmp2--;
    }//if
    ropOverflowDirptr.i = ropOverflowrangeptr.p->dirArray[tropTmp1];
    ptrCheckGuard(ropOverflowDirptr, cdirarraysize, directoryarray);
  } while (ropOverflowDirptr.p->pagep[tropTmp2] == RNIL);
  /* ----------------------------------------------------------------------- */
  /* RELEASE ANY OVERFLOW RECORDS THAT ARE PART OF THE FREE INDEX LIST WHICH */
  /* DIRECTORY INDEX NOW HAS BEEN RELEASED.                                  */
  /* ----------------------------------------------------------------------- */
  tuodOverflowRecPtr.i = fragrecptr.p->firstFreeDirindexRec;
  jam();
  while (tuodOverflowRecPtr.i != RNIL) {
    jam();
    ptrCheckGuard(tuodOverflowRecPtr, coverflowrecsize, overflowRecord);
    if (tuodOverflowRecPtr.p->dirindex >= fragrecptr.p->lastOverIndex) {
      jam();
      rorOverflowRecPtr = tuodOverflowRecPtr;
      troOverflowRecPtr.p = tuodOverflowRecPtr.p;
      tuodOverflowRecPtr.i = troOverflowRecPtr.p->nextOverList;
      takeRecOutOfFreeOverdir(signal);
      releaseOverflowRec(signal);
    } else {
      jam();
      tuodOverflowRecPtr.i = tuodOverflowRecPtr.p->nextOverList;
    }//if
  }//while
}//Dbacc::releaseOverpage()

/* ------------------------------------------------------------------------- */
/* RELEASE_PAGE                                                              */
/* ------------------------------------------------------------------------- */
void Dbacc::releasePage(Signal* signal) 
{
#ifdef VM_TRACE
  bool inList = false;
  Uint32 numInList = 0;
  Page8Ptr tmpPagePtr;
  tmpPagePtr.i = cfirstfreepage;
  while (tmpPagePtr.i != RNIL){
    ptrCheckGuard(tmpPagePtr, cpagesize, page8);
    if (tmpPagePtr.i == rpPageptr.i){
      jam(); inList = true; 
      break;
    }    
    numInList++;
    tmpPagePtr.i = tmpPagePtr.p->word32[0];    
  }
  ndbrequire(inList == false);
  //  ndbrequire(numInList == cnoOfAllocatedPages);
#endif
  rpPageptr.p->word32[0] = cfirstfreepage;
  cfirstfreepage = rpPageptr.i;
  cnoOfAllocatedPages--;
}//Dbacc::releasePage()

/* --------------------------------------------------------------------------------- */
/* SEIZE_DIRECTORY                                                                   */
/*          DESCRIPTION: A DIRECTORY BLOCK (ZDIRBLOCKSIZE NUMBERS OF DIRECTORY       */
/*               RECORDS WILL BE ALLOCATED AND RETURNED.                             */
/*               SIZE OF DIRECTORY ERROR_CODE, WILL BE RETURNED IF THERE IS NO ANY   */
/*               FREE BLOCK                                                          */
/* --------------------------------------------------------------------------------- */
void Dbacc::seizeDirectory(Signal* signal) 
{
  Uint32 tsdyIndex;

  if (cfirstfreedir == RNIL) {
    jam();
    if (cdirarraysize <= cdirmemory) {
      jam();
      tresult = ZDIRSIZE_ERROR;
      return;
    } else {
      jam();
      sdDirptr.i = cdirmemory;
      ptrCheckGuard(sdDirptr, cdirarraysize, directoryarray);
      cdirmemory = cdirmemory + 1;
    }//if
  } else {
    jam();
    sdDirptr.i = cfirstfreedir;
    ptrCheckGuard(sdDirptr, cdirarraysize, directoryarray);
    cfirstfreedir = sdDirptr.p->pagep[0];
    sdDirptr.p->pagep[0] = RNIL;
  }//if
  for (tsdyIndex = 0; tsdyIndex <= 255; tsdyIndex++) {
    sdDirptr.p->pagep[tsdyIndex] = RNIL;
  }//for
}//Dbacc::seizeDirectory()

/* --------------------------------------------------------------------------------- */
/* SEIZE_DIRRANGE                                                                    */
/* --------------------------------------------------------------------------------- */
void Dbacc::seizeDirrange(Signal* signal) 
{
  Uint32 tsdeIndex;

  newDirRangePtr.i = cfirstfreeDirrange;
  ptrCheckGuard(newDirRangePtr, cdirrangesize, dirRange);
  cfirstfreeDirrange = newDirRangePtr.p->dirArray[0];
  for (tsdeIndex = 0; tsdeIndex <= 255; tsdeIndex++) {
    newDirRangePtr.p->dirArray[tsdeIndex] = RNIL;
  }//for
}//Dbacc::seizeDirrange()

/* --------------------------------------------------------------------------------- */
/* SEIZE    FRAGREC                                                                  */
/* --------------------------------------------------------------------------------- */
void Dbacc::seizeFragrec(Signal* signal) 
{
  fragrecptr.i = cfirstfreefrag;
  ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
  cfirstfreefrag = fragrecptr.p->nextfreefrag;
  fragrecptr.p->nextfreefrag = RNIL;
}//Dbacc::seizeFragrec()

/* --------------------------------------------------------------------------------- */
/* SEIZE_OP_REC                                                                      */
/* --------------------------------------------------------------------------------- */
void Dbacc::seizeOpRec(Signal* signal) 
{
  operationRecPtr.i = cfreeopRec;
  ptrCheckGuard(operationRecPtr, coprecsize, operationrec);
  cfreeopRec = operationRecPtr.p->nextOp;	/* UPDATE FREE LIST OF OP RECORDS */
  /* PUTS OPERTION RECORD PTR IN THE LIST */
  /* OF OPERATION IN CONNECTION RECORD */
  operationRecPtr.p->nextOp = RNIL;
}//Dbacc::seizeOpRec()

/* --------------------------------------------------------------------------------- */
/* SEIZE OVERFLOW RECORD                                                             */
/* --------------------------------------------------------------------------------- */
void Dbacc::seizeOverRec(Signal* signal) {
  sorOverflowRecPtr.i = cfirstfreeoverrec;
  ptrCheckGuard(sorOverflowRecPtr, coverflowrecsize, overflowRecord);
  cfirstfreeoverrec = sorOverflowRecPtr.p->nextfreeoverrec;
  sorOverflowRecPtr.p->nextfreeoverrec = RNIL;
  sorOverflowRecPtr.p->prevOverRec = RNIL;
  sorOverflowRecPtr.p->nextOverRec = RNIL;
}//Dbacc::seizeOverRec()


/** 
 * A ZPAGESIZE_ERROR has occured, out of index pages
 * Print some debug info if debug compiled
 */
void Dbacc::zpagesize_error(const char* where){
  DEBUG(where << endl
	<< "  ZPAGESIZE_ERROR" << endl
	<< "  cfirstfreepage=" << cfirstfreepage << endl
	<< "  cfreepage=" <<cfreepage<<endl
	<< "  cpagesize=" <<cpagesize<<endl
	<< "  cnoOfAllocatedPages="<<cnoOfAllocatedPages);
}


/* --------------------------------------------------------------------------------- */
/* SEIZE_PAGE                                                                        */
/* --------------------------------------------------------------------------------- */
void Dbacc::seizePage(Signal* signal) 
{
  tresult = 0;
  if (cfirstfreepage == RNIL) {
    if (cfreepage < cpagesize) {
      jam();
      spPageptr.i = cfreepage;
      ptrCheckGuard(spPageptr, cpagesize, page8);
      cfreepage++;
      cnoOfAllocatedPages++;
    } else {
      jam();
      zpagesize_error("Dbacc::seizePage");
      tresult = ZPAGESIZE_ERROR;
    }//if
  } else {
    jam();
    spPageptr.i = cfirstfreepage;
    ptrCheckGuard(spPageptr, cpagesize, page8);
    cfirstfreepage = spPageptr.p->word32[0];
    cnoOfAllocatedPages++;
  }//if
}//Dbacc::seizePage()

/* --------------------------------------------------------------------------------- */
/* SEIZE_ROOTFRAGREC                                                                 */
/* --------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------- */
/* SEIZE_SCAN_REC                                                                    */
/* --------------------------------------------------------------------------------- */
void Dbacc::seizeScanRec(Signal* signal) 
{
  scanPtr.i = cfirstFreeScanRec;
  ptrCheckGuard(scanPtr, cscanRecSize, scanRec);
  ndbrequire(scanPtr.p->scanState == ScanRec::SCAN_DISCONNECT);
  cfirstFreeScanRec = scanPtr.p->scanNextfreerec;
}//Dbacc::seizeScanRec()

/* --------------------------------------------------------------------------------- */
/* SEIZE_SR_VERSION_REC                                                              */
/* --------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------- */
/* SEND_SYSTEMERROR                                                                  */
/* --------------------------------------------------------------------------------- */
void Dbacc::sendSystemerror(Signal* signal, int line)
{
  progError(line, NDBD_EXIT_PRGERR);
}//Dbacc::sendSystemerror()

/* --------------------------------------------------------------------------------- */
/* TAKE_REC_OUT_OF_FREE_OVERDIR                                                      */
/* --------------------------------------------------------------------------------- */
void Dbacc::takeRecOutOfFreeOverdir(Signal* signal) 
{
  OverflowRecordPtr tofoOverrecPtr;
  if (troOverflowRecPtr.p->nextOverList != RNIL) {
    jam();
    tofoOverrecPtr.i = troOverflowRecPtr.p->nextOverList;
    ptrCheckGuard(tofoOverrecPtr, coverflowrecsize, overflowRecord);
    tofoOverrecPtr.p->prevOverList = troOverflowRecPtr.p->prevOverList;
  }//if
  if (troOverflowRecPtr.p->prevOverList != RNIL) {
    jam();
    tofoOverrecPtr.i = troOverflowRecPtr.p->prevOverList;
    ptrCheckGuard(tofoOverrecPtr, coverflowrecsize, overflowRecord);
    tofoOverrecPtr.p->nextOverList = troOverflowRecPtr.p->nextOverList;
  } else {
    jam();
    fragrecptr.p->firstFreeDirindexRec = troOverflowRecPtr.p->nextOverList;
  }//if
}//Dbacc::takeRecOutOfFreeOverdir()

/* --------------------------------------------------------------------------------- */
/* TAKE_REC_OUT_OF_FREE_OVERPAGE                                                     */
/*         DESCRIPTION: AN OVERFLOW PAGE WHICH IS EMPTY HAVE TO BE TAKE OUT OF THE   */
/*                      FREE LIST OF OVERFLOW PAGE. BY THIS SUBROUTINE THIS LIST     */
/*                      WILL BE UPDATED.                                             */
/* --------------------------------------------------------------------------------- */
void Dbacc::takeRecOutOfFreeOverpage(Signal* signal) 
{
  OverflowRecordPtr tfoNextOverflowRecPtr;
  OverflowRecordPtr tfoPrevOverflowRecPtr;

  if (tfoOverflowRecPtr.p->nextOverRec != RNIL) {
    jam();
    tfoNextOverflowRecPtr.i = tfoOverflowRecPtr.p->nextOverRec;
    ptrCheckGuard(tfoNextOverflowRecPtr, coverflowrecsize, overflowRecord);
    tfoNextOverflowRecPtr.p->prevOverRec = tfoOverflowRecPtr.p->prevOverRec;
  } else {
    ndbrequire(fragrecptr.p->lastOverflowRec == tfoOverflowRecPtr.i);
    jam();
    fragrecptr.p->lastOverflowRec = tfoOverflowRecPtr.p->prevOverRec;
  }//if
  if (tfoOverflowRecPtr.p->prevOverRec != RNIL) {
    jam();
    tfoPrevOverflowRecPtr.i = tfoOverflowRecPtr.p->prevOverRec;
    ptrCheckGuard(tfoPrevOverflowRecPtr, coverflowrecsize, overflowRecord);
    tfoPrevOverflowRecPtr.p->nextOverRec = tfoOverflowRecPtr.p->nextOverRec;
  } else {
    ndbrequire(fragrecptr.p->firstOverflowRec == tfoOverflowRecPtr.i);
    jam();
    fragrecptr.p->firstOverflowRec = tfoOverflowRecPtr.p->nextOverRec;
  }//if
}//Dbacc::takeRecOutOfFreeOverpage()

void
Dbacc::reportMemoryUsage(Signal* signal, int gth){
  signal->theData[0] = NDB_LE_MemoryUsage;
  signal->theData[1] = gth;
  signal->theData[2] = sizeof(* rpPageptr.p);
  signal->theData[3] = cnoOfAllocatedPages;
  signal->theData[4] = cpagesize;
  signal->theData[5] = DBACC;
  sendSignal(CMVMI_REF, GSN_EVENT_REP, signal, 6, JBB);
}

void
Dbacc::execDUMP_STATE_ORD(Signal* signal)
{
  DumpStateOrd * const dumpState = (DumpStateOrd *)&signal->theData[0];
  if (dumpState->args[0] == DumpStateOrd::AccDumpOneScanRec){
    Uint32 recordNo = RNIL;
    if (signal->length() == 2)
      recordNo = dumpState->args[1];
    else 
      return;

    if (recordNo >= cscanRecSize) 
      return;
    
    scanPtr.i = recordNo;
    ptrAss(scanPtr, scanRec);
    infoEvent("Dbacc::ScanRec[%d]: state=%d, transid(0x%x, 0x%x)",
	      scanPtr.i, scanPtr.p->scanState,scanPtr.p->scanTrid1,
	      scanPtr.p->scanTrid2);
    infoEvent(" timer=%d, continueBCount=%d, "
	      "activeLocalFrag=%d, nextBucketIndex=%d",
	      scanPtr.p->scanTimer,
	      scanPtr.p->scanContinuebCounter,
	      scanPtr.p->activeLocalFrag,
	      scanPtr.p->nextBucketIndex);
    infoEvent(" scanNextfreerec=%d firstActOp=%d firstLockedOp=%d, "
	      "scanLastLockedOp=%d firstQOp=%d lastQOp=%d",
	      scanPtr.p->scanNextfreerec,
	      scanPtr.p->scanFirstActiveOp,
	      scanPtr.p->scanFirstLockedOp,
	      scanPtr.p->scanLastLockedOp,
	      scanPtr.p->scanFirstQueuedOp,
	      scanPtr.p->scanLastQueuedOp);
    infoEvent(" scanUserP=%d, startNoBuck=%d, minBucketIndexToRescan=%d, "
	      "maxBucketIndexToRescan=%d",
	      scanPtr.p->scanUserptr,
	      scanPtr.p->startNoOfBuckets,
	      scanPtr.p->minBucketIndexToRescan,
	      scanPtr.p->maxBucketIndexToRescan);
    infoEvent(" scanBucketState=%d, scanLockHeld=%d, userBlockRef=%d, "
	      "scanMask=%d scanLockMode=%d",
	      scanPtr.p->scanBucketState,
	      scanPtr.p->scanLockHeld,
	      scanPtr.p->scanUserblockref,
	      scanPtr.p->scanMask,
	      scanPtr.p->scanLockMode);
    return;
  }

  // Dump all ScanRec(ords)
  if (dumpState->args[0] == DumpStateOrd::AccDumpAllScanRec){
    Uint32 recordNo = 0;
    if (signal->length() == 1)
      infoEvent("ACC: Dump all ScanRec - size: %d",
		cscanRecSize);
    else if (signal->length() == 2)
      recordNo = dumpState->args[1];
    else
      return;
    
    dumpState->args[0] = DumpStateOrd::AccDumpOneScanRec;
    dumpState->args[1] = recordNo;
    execDUMP_STATE_ORD(signal);
    
    if (recordNo < cscanRecSize-1){
      dumpState->args[0] = DumpStateOrd::AccDumpAllScanRec;
      dumpState->args[1] = recordNo+1;
      sendSignal(reference(), GSN_DUMP_STATE_ORD, signal, 2, JBB);
    }
    return;
  }

  // Dump all active ScanRec(ords)
  if (dumpState->args[0] == DumpStateOrd::AccDumpAllActiveScanRec){
    Uint32 recordNo = 0;
    if (signal->length() == 1)
      infoEvent("ACC: Dump active ScanRec - size: %d",
		cscanRecSize);
    else if (signal->length() == 2)
      recordNo = dumpState->args[1];
    else
      return;

    ScanRecPtr sp;
    sp.i = recordNo;
    ptrAss(sp, scanRec);
    if (sp.p->scanState != ScanRec::SCAN_DISCONNECT){
      dumpState->args[0] = DumpStateOrd::AccDumpOneScanRec;
      dumpState->args[1] = recordNo;
      execDUMP_STATE_ORD(signal);
    }

    if (recordNo < cscanRecSize-1){
      dumpState->args[0] = DumpStateOrd::AccDumpAllActiveScanRec;
      dumpState->args[1] = recordNo+1;
      sendSignal(reference(), GSN_DUMP_STATE_ORD, signal, 2, JBB);
    }
    return;
  }

  if(dumpState->args[0] == DumpStateOrd::DumpPageMemory){
    reportMemoryUsage(signal, 0);
    return;
  }

  if(dumpState->args[0] == DumpStateOrd::EnableUndoDelayDataWrite){
    ndbout << "Dbacc:: delay write of datapages for table = " 
	   << dumpState->args[1]<< endl;
    c_errorInsert3000_TableId = dumpState->args[1];
    SET_ERROR_INSERT_VALUE(3000);
    return;
  }

  if(dumpState->args[0] == DumpStateOrd::AccDumpOneOperationRec){
    Uint32 recordNo = RNIL;
    if (signal->length() == 2)
      recordNo = dumpState->args[1];
    else 
      return;

    if (recordNo >= coprecsize) 
      return;
    
    OperationrecPtr tmpOpPtr;
    tmpOpPtr.i = recordNo;
    ptrAss(tmpOpPtr, operationrec);
    infoEvent("Dbacc::operationrec[%d]: opState=%d, transid(0x%x, 0x%x)",
	      tmpOpPtr.i, tmpOpPtr.p->opState, tmpOpPtr.p->transId1,
	      tmpOpPtr.p->transId2);
    infoEvent("elementIsforward=%d, elementPage=%d, elementPointer=%d ",
	      tmpOpPtr.p->elementIsforward, tmpOpPtr.p->elementPage, 
	      tmpOpPtr.p->elementPointer);
    infoEvent("fid=%d, fragptr=%d, hashvaluePart=%d ",
	      tmpOpPtr.p->fid, tmpOpPtr.p->fragptr, 
	      tmpOpPtr.p->hashvaluePart);
    infoEvent("hashValue=%d, insertDeleteLen=%d, keyinfoPage=%d ",
	      tmpOpPtr.p->hashValue, tmpOpPtr.p->insertDeleteLen, 
	      tmpOpPtr.p->keyinfoPage);
    infoEvent("nextLockOwnerOp=%d, nextOp=%d, nextParallelQue=%d ",
	      tmpOpPtr.p->nextLockOwnerOp, tmpOpPtr.p->nextOp, 
	      tmpOpPtr.p->nextParallelQue);
    infoEvent("nextQueOp=%d, nextSerialQue=%d, prevOp=%d ",
	      tmpOpPtr.p->nextQueOp, tmpOpPtr.p->nextSerialQue, 
	      tmpOpPtr.p->prevOp);
    infoEvent("prevLockOwnerOp=%d, prevParallelQue=%d, prevQueOp=%d ",
	      tmpOpPtr.p->prevLockOwnerOp, tmpOpPtr.p->nextParallelQue, 
	      tmpOpPtr.p->prevQueOp);
    infoEvent("prevSerialQue=%d, scanRecPtr=%d, longPagePtr=%d ",
	      tmpOpPtr.p->prevSerialQue, tmpOpPtr.p->scanRecPtr, 
	      tmpOpPtr.p->longPagePtr);
    infoEvent("transactionstate=%d, elementIsDisappeared=%d, insertIsDone=%d ",
	      tmpOpPtr.p->transactionstate, tmpOpPtr.p->elementIsDisappeared, 
	      tmpOpPtr.p->insertIsDone);
    infoEvent("lockMode=%d, lockOwner=%d, nodeType=%d ",
	      tmpOpPtr.p->lockMode, tmpOpPtr.p->lockOwner, 
	      tmpOpPtr.p->nodeType);
    infoEvent("operation=%d, opSimple=%d, dirtyRead=%d,scanBits=%d ",
	      tmpOpPtr.p->operation, tmpOpPtr.p->opSimple, 
	      tmpOpPtr.p->dirtyRead, tmpOpPtr.p->scanBits);
    return;
  }

  if(dumpState->args[0] == DumpStateOrd::AccDumpNumOpRecs){

    Uint32 freeOpRecs = 0;
    OperationrecPtr opRecPtr;
    opRecPtr.i = cfreeopRec;
    while (opRecPtr.i != RNIL){
      freeOpRecs++;
      ptrCheckGuard(opRecPtr, coprecsize, operationrec);
      opRecPtr.i = opRecPtr.p->nextOp;
    }

    infoEvent("Dbacc::OperationRecords: num=%d, free=%d",	      
	      coprecsize, freeOpRecs);

    return;
  }
  if(dumpState->args[0] == DumpStateOrd::AccDumpFreeOpRecs){

    OperationrecPtr opRecPtr;
    opRecPtr.i = cfreeopRec;
    while (opRecPtr.i != RNIL){
      
      dumpState->args[0] = DumpStateOrd::AccDumpOneOperationRec;
      dumpState->args[1] = opRecPtr.i;
      execDUMP_STATE_ORD(signal);

      ptrCheckGuard(opRecPtr, coprecsize, operationrec);
      opRecPtr.i = opRecPtr.p->nextOp;
    }


    return;
  }

  if(dumpState->args[0] == DumpStateOrd::AccDumpNotFreeOpRecs){
    Uint32 recordStart = RNIL;
    if (signal->length() == 2)
      recordStart = dumpState->args[1];
    else 
      return;

    if (recordStart >= coprecsize) 
      return;

    for (Uint32 i = recordStart; i < coprecsize; i++){

      bool inFreeList = false;
      OperationrecPtr opRecPtr;
      opRecPtr.i = cfreeopRec;
      while (opRecPtr.i != RNIL){
	if (opRecPtr.i == i){
	  inFreeList = true;
	  break;
	}
	ptrCheckGuard(opRecPtr, coprecsize, operationrec);
	opRecPtr.i = opRecPtr.p->nextOp;
      }
      if (inFreeList == false){
	dumpState->args[0] = DumpStateOrd::AccDumpOneOperationRec;
	dumpState->args[1] = i;
	execDUMP_STATE_ORD(signal);	
      }
    }
    return;
  }

#if 0
  if (type == 100) {
    RelTabMemReq * const req = (RelTabMemReq *)signal->getDataPtrSend();
    req->primaryTableId = 2;
    req->secondaryTableId = RNIL;
    req->userPtr = 2;
    req->userRef = DBDICT_REF;
    sendSignal(cownBlockref, GSN_REL_TABMEMREQ, signal,
               RelTabMemReq::SignalLength, JBB);
    return;
  }//if
  if (type == 101) {
    RelTabMemReq * const req = (RelTabMemReq *)signal->getDataPtrSend();
    req->primaryTableId = 4;
    req->secondaryTableId = 5;
    req->userPtr = 4;
    req->userRef = DBDICT_REF;
    sendSignal(cownBlockref, GSN_REL_TABMEMREQ, signal,
               RelTabMemReq::SignalLength, JBB);
    return;
  }//if
  if (type == 102) {
    RelTabMemReq * const req = (RelTabMemReq *)signal->getDataPtrSend();
    req->primaryTableId = 6;
    req->secondaryTableId = 8;
    req->userPtr = 6;
    req->userRef = DBDICT_REF;
    sendSignal(cownBlockref, GSN_REL_TABMEMREQ, signal,
               RelTabMemReq::SignalLength, JBB);
    return;
  }//if
  if (type == 103) {
    DropTabFileReq * const req = (DropTabFileReq *)signal->getDataPtrSend();
    req->primaryTableId = 2;
    req->secondaryTableId = RNIL;
    req->userPtr = 2;
    req->userRef = DBDICT_REF;
    sendSignal(cownBlockref, GSN_DROP_TABFILEREQ, signal,
               DropTabFileReq::SignalLength, JBB);
    return;
  }//if
  if (type == 104) {
    DropTabFileReq * const req = (DropTabFileReq *)signal->getDataPtrSend();
    req->primaryTableId = 4;
    req->secondaryTableId = 5;
    req->userPtr = 4;
    req->userRef = DBDICT_REF;
    sendSignal(cownBlockref, GSN_DROP_TABFILEREQ, signal,
               DropTabFileReq::SignalLength, JBB);
    return;
  }//if
  if (type == 105) {
    DropTabFileReq * const req = (DropTabFileReq *)signal->getDataPtrSend();
    req->primaryTableId = 6;
    req->secondaryTableId = 8;
    req->userPtr = 6;
    req->userRef = DBDICT_REF;
    sendSignal(cownBlockref, GSN_DROP_TABFILEREQ, signal,
               DropTabFileReq::SignalLength, JBB);
    return;
  }//if
#endif
}//Dbacc::execDUMP_STATE_ORD()

void Dbacc::execSET_VAR_REQ(Signal* signal) 
{
#if 0
  SetVarReq* const setVarReq = (SetVarReq*)&signal->theData[0];
  ConfigParamId var = setVarReq->variable();
  int val = setVarReq->value();


  switch (var) {

  case NoOfDiskPagesToDiskAfterRestartACC:
    clblPagesPerTick = val;
    sendSignal(CMVMI_REF, GSN_SET_VAR_CONF, signal, 1, JBB);
    break;

  case NoOfDiskPagesToDiskDuringRestartACC:
    // Valid only during start so value not set.
    sendSignal(CMVMI_REF, GSN_SET_VAR_CONF, signal, 1, JBB);
    break;

  default:
    sendSignal(CMVMI_REF, GSN_SET_VAR_REF, signal, 1, JBB);
  } // switch
#endif

}//execSET_VAR_REQ()

void
Dbacc::execREAD_PSEUDO_REQ(Signal* signal){
  jamEntry();
  fragrecptr.i = signal->theData[0];
  Uint32 attrId = signal->theData[1];
  ptrCheckGuard(fragrecptr, cfragmentsize, fragmentrec);
  Uint64 tmp;
  switch(attrId){
  case AttributeHeader::ROW_COUNT:
    tmp = fragrecptr.p->noOfElements;
    break;
  case AttributeHeader::COMMIT_COUNT:
    tmp = fragrecptr.p->m_commit_count;
    break;
  default:
    tmp = 0;
  }
  memcpy(signal->theData, &tmp, 8); /* must be memcpy, gives strange results on
				     * ithanium gcc (GCC) 3.4.1 smp linux 2.4
				     * otherwise
				     */
  //  Uint32 * src = (Uint32*)&tmp;
  //  signal->theData[0] = src[0];
  //  signal->theData[1] = src[1];
}
