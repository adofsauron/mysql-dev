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

#include <my_config.h>
#include "Backup.hpp"

#include <ndb_version.h>

#include <NdbTCP.h>
#include <Bitmask.hpp>

#include <signaldata/NodeFailRep.hpp>
#include <signaldata/ReadNodesConf.hpp>

#include <signaldata/DihFragCount.hpp>
#include <signaldata/ScanFrag.hpp>

#include <signaldata/GetTabInfo.hpp>
#include <signaldata/DictTabInfo.hpp>
#include <signaldata/ListTables.hpp>

#include <signaldata/FsOpenReq.hpp>
#include <signaldata/FsAppendReq.hpp>
#include <signaldata/FsCloseReq.hpp>
#include <signaldata/FsConf.hpp>
#include <signaldata/FsRef.hpp>
#include <signaldata/FsRemoveReq.hpp>

#include <signaldata/BackupImpl.hpp>
#include <signaldata/BackupSignalData.hpp>
#include <signaldata/BackupContinueB.hpp>
#include <signaldata/EventReport.hpp>

#include <signaldata/UtilSequence.hpp>

#include <signaldata/CreateTrig.hpp>
#include <signaldata/AlterTrig.hpp>
#include <signaldata/DropTrig.hpp>
#include <signaldata/FireTrigOrd.hpp>
#include <signaldata/TrigAttrInfo.hpp>
#include <AttributeHeader.hpp>

#include <signaldata/WaitGCP.hpp>
#include <signaldata/LCP.hpp>

#include <NdbTick.h>

static NDB_TICKS startTime;

static const Uint32 BACKUP_SEQUENCE = 0x1F000000;

#ifdef VM_TRACE
#define DEBUG_OUT(x) ndbout << x << endl
#else
#define DEBUG_OUT(x) 
#endif

//#define DEBUG_ABORT
//#define dbg globalSignalLoggers.log

static Uint32 g_TypeOfStart = NodeState::ST_ILLEGAL_TYPE;

#define SEND_BACKUP_STARTED_FLAG(A) (((A) & 0x3) > 0)
#define SEND_BACKUP_COMPLETED_FLAG(A) (((A) & 0x3) > 1)

void
Backup::execSTTOR(Signal* signal) 
{
  jamEntry();                            

  const Uint32 startphase  = signal->theData[1];
  const Uint32 typeOfStart = signal->theData[7];

  if (startphase == 3) {
    jam();
    g_TypeOfStart = typeOfStart;
    signal->theData[0] = reference();
    sendSignal(NDBCNTR_REF, GSN_READ_NODESREQ, signal, 1, JBB);
    return;
  }//if

  if(startphase == 7 && g_TypeOfStart == NodeState::ST_INITIAL_START &&
     c_masterNodeId == getOwnNodeId()){
    jam();
    createSequence(signal);
    return;
  }//if
  
  sendSTTORRY(signal);  
  return;
}//Dbdict::execSTTOR()

void
Backup::execREAD_NODESCONF(Signal* signal)
{
  jamEntry();
  ReadNodesConf * conf = (ReadNodesConf *)signal->getDataPtr();
 
  c_aliveNodes.clear();

  Uint32 count = 0;
  for (Uint32 i = 0; i<MAX_NDB_NODES; i++) {
    jam();
    if(NodeBitmask::get(conf->allNodes, i)){
      jam();
      count++;

      NodePtr node;
      ndbrequire(c_nodes.seize(node));
      
      node.p->nodeId = i;
      if(NodeBitmask::get(conf->inactiveNodes, i)) {
        jam();
	node.p->alive = 0;
      } else {
        jam();
	node.p->alive = 1;
	c_aliveNodes.set(i);
      }//if
    }//if
  }//for
  c_masterNodeId = conf->masterNodeId;
  ndbrequire(count == conf->noOfNodes);
  sendSTTORRY(signal);
}

void
Backup::sendSTTORRY(Signal* signal)
{
  signal->theData[0] = 0;
  signal->theData[3] = 1;
  signal->theData[4] = 3;
  signal->theData[5] = 7;
  signal->theData[6] = 255; // No more start phases from missra
  sendSignal(NDBCNTR_REF, GSN_STTORRY, signal, 7, JBB);
}

void
Backup::createSequence(Signal* signal)
{
  UtilSequenceReq * req = (UtilSequenceReq*)signal->getDataPtrSend();
  
  req->senderData  = RNIL;
  req->sequenceId  = BACKUP_SEQUENCE;
  req->requestType = UtilSequenceReq::Create;
  
  sendSignal(DBUTIL_REF, GSN_UTIL_SEQUENCE_REQ, 
	     signal, UtilSequenceReq::SignalLength, JBB);
}

void
Backup::execCONTINUEB(Signal* signal)
{
  jamEntry();
  const Uint32 Tdata0 = signal->theData[0];
  const Uint32 Tdata1 = signal->theData[1];
  const Uint32 Tdata2 = signal->theData[2];
  
  switch(Tdata0) {
  case BackupContinueB::START_FILE_THREAD:
  case BackupContinueB::BUFFER_UNDERFLOW:
  {
    jam();
    BackupFilePtr filePtr;
    c_backupFilePool.getPtr(filePtr, Tdata1);
    checkFile(signal, filePtr);
    return;
  }
  break;
  case BackupContinueB::BUFFER_FULL_SCAN:
  {
    jam();
    BackupFilePtr filePtr;
    c_backupFilePool.getPtr(filePtr, Tdata1);
    checkScan(signal, filePtr);
    return;
  }
  break;
  case BackupContinueB::BUFFER_FULL_FRAG_COMPLETE:
  {
    jam();
    BackupFilePtr filePtr;
    c_backupFilePool.getPtr(filePtr, Tdata1);
    fragmentCompleted(signal, filePtr);
    return;
  }
  break;
  case BackupContinueB::BUFFER_FULL_META:
  {
    jam();
    BackupRecordPtr ptr;
    c_backupPool.getPtr(ptr, Tdata1);
    
    BackupFilePtr filePtr;
    ptr.p->files.getPtr(filePtr, ptr.p->ctlFilePtr);
    FsBuffer & buf = filePtr.p->operation.dataBuffer;
    
    if(buf.getFreeSize() + buf.getMinRead() < buf.getUsableSize()) {
      jam();
      TablePtr tabPtr;
      c_tablePool.getPtr(tabPtr, Tdata2);
      
      DEBUG_OUT("Backup - Buffer full - " << buf.getFreeSize()
		<< " + " << buf.getMinRead()
		<< " < " << buf.getUsableSize()
		<< " - tableId = " << tabPtr.p->tableId);

      signal->theData[0] = BackupContinueB::BUFFER_FULL_META;
      signal->theData[1] = Tdata1;
      signal->theData[2] = Tdata2;
      sendSignalWithDelay(BACKUP_REF, GSN_CONTINUEB, signal, 100, 3);
      return;
    }//if
    
    TablePtr tabPtr;
    c_tablePool.getPtr(tabPtr, Tdata2);
    GetTabInfoReq * req = (GetTabInfoReq *)signal->getDataPtrSend();
    req->senderRef = reference();
    req->senderData = ptr.i;
    req->requestType = GetTabInfoReq::RequestById |
      GetTabInfoReq::LongSignalConf;
    req->tableId = tabPtr.p->tableId;
    sendSignal(DBDICT_REF, GSN_GET_TABINFOREQ, signal, 
	       GetTabInfoReq::SignalLength, JBB);
    return;
  }
  default:
    ndbrequire(0);
  }//switch
}

void
Backup::execDUMP_STATE_ORD(Signal* signal)
{
  jamEntry();
  
  if(signal->theData[0] == 20){
    if(signal->length() > 1){
      c_defaults.m_dataBufferSize = (signal->theData[1] * 1024 * 1024);
    }
    if(signal->length() > 2){
      c_defaults.m_logBufferSize = (signal->theData[2] * 1024 * 1024);
    }
    if(signal->length() > 3){
      c_defaults.m_minWriteSize = signal->theData[3] * 1024;
    }
    if(signal->length() > 4){
      c_defaults.m_maxWriteSize = signal->theData[4] * 1024;
    }
    
    infoEvent("Backup: data: %d log: %d min: %d max: %d",
	      c_defaults.m_dataBufferSize,
	      c_defaults.m_logBufferSize,
	      c_defaults.m_minWriteSize,
	      c_defaults.m_maxWriteSize);
    return;
  }
  if(signal->theData[0] == 21){
    BackupReq * req = (BackupReq*)signal->getDataPtrSend();
    req->senderData = 23;
    req->backupDataLen = 0;
    sendSignal(BACKUP_REF, GSN_BACKUP_REQ,signal,BackupReq::SignalLength, JBB);
    startTime = NdbTick_CurrentMillisecond();
    return;
  }

  if(signal->theData[0] == 22){
    const Uint32 seq = signal->theData[1];
    FsRemoveReq * req = (FsRemoveReq *)signal->getDataPtrSend();
    req->userReference = reference();
    req->userPointer = 23;
    req->directory = 1;
    req->ownDirectory = 1;
    FsOpenReq::setVersion(req->fileNumber, 2);
    FsOpenReq::setSuffix(req->fileNumber, FsOpenReq::S_CTL);
    FsOpenReq::v2_setSequence(req->fileNumber, seq);
    FsOpenReq::v2_setNodeId(req->fileNumber, getOwnNodeId());
    sendSignal(NDBFS_REF, GSN_FSREMOVEREQ, signal, 
	       FsRemoveReq::SignalLength, JBA);
    return;
  }

  if(signal->theData[0] == 23){
    /**
     * Print records
     */
    BackupRecordPtr ptr;
    for(c_backups.first(ptr); ptr.i != RNIL; c_backups.next(ptr)){
      infoEvent("BackupRecord %d: BackupId: %d MasterRef: %x ClientRef: %x",
		ptr.i, ptr.p->backupId, ptr.p->masterRef, ptr.p->clientRef);
      infoEvent(" State: %d", ptr.p->slaveState.getState());
      BackupFilePtr filePtr;
      for(ptr.p->files.first(filePtr); filePtr.i != RNIL; 
	  ptr.p->files.next(filePtr)){
	jam();
	infoEvent(" file %d: type: %d open: %d running: %d done: %d scan: %d",
		  filePtr.i, filePtr.p->fileType, filePtr.p->fileOpened,
		  filePtr.p->fileRunning, 
		  filePtr.p->fileClosing, filePtr.p->scanRunning);
      }
    }
  }
  if(signal->theData[0] == 24){
    /**
     * Print size of records etc.
     */
    infoEvent("Backup - dump pool sizes");
    infoEvent("BackupPool: %d BackupFilePool: %d TablePool: %d",
	      c_backupPool.getSize(), c_backupFilePool.getSize(), 
	      c_tablePool.getSize());
    infoEvent("AttrPool: %d TriggerPool: %d FragmentPool: %d",
	      c_backupPool.getSize(), c_backupFilePool.getSize(), 
	      c_tablePool.getSize());
    infoEvent("PagePool: %d",
	      c_pagePool.getSize());


    if(signal->getLength() == 2 && signal->theData[1] == 2424)
    {
      /**
       * Handle LCP
       */
      BackupRecordPtr lcp;
      ndbrequire(c_backups.first(lcp));
      
      ndbrequire(c_backupPool.getSize() == c_backupPool.getNoOfFree() + 1);
      if(lcp.p->tables.isEmpty())
      {
	ndbrequire(c_tablePool.getSize() == c_tablePool.getNoOfFree());
	ndbrequire(c_attributePool.getSize() == c_attributePool.getNoOfFree());
	ndbrequire(c_fragmentPool.getSize() == c_fragmentPool.getNoOfFree());
	ndbrequire(c_triggerPool.getSize() == c_triggerPool.getNoOfFree());
      }
      ndbrequire(c_backupFilePool.getSize() == c_backupFilePool.getNoOfFree() + 1);
      BackupFilePtr lcp_file;
      c_backupFilePool.getPtr(lcp_file, lcp.p->dataFilePtr);
      ndbrequire(c_pagePool.getSize() == 
		 c_pagePool.getNoOfFree() + 
		 lcp_file.p->pages.getSize());
    }
  }
}

bool
Backup::findTable(const BackupRecordPtr & ptr, 
		  TablePtr & tabPtr, Uint32 tableId) const
{
  for(ptr.p->tables.first(tabPtr); 
      tabPtr.i != RNIL; 
      ptr.p->tables.next(tabPtr)) {
    jam();
    if(tabPtr.p->tableId == tableId){
      jam();
      return true;
    }//if
  }//for
  tabPtr.i = RNIL;
  tabPtr.p = 0;
  return false;
}

static Uint32 xps(Uint32 x, Uint64 ms)
{
  float fx = x;
  float fs = ms;
  
  if(ms == 0 || x == 0) {
    jam();
    return 0;
  }//if
  jam();
  return ((Uint32)(1000.0f * (fx + fs/2.1f))) / ((Uint32)fs);
}

struct Number {
  Number(Uint32 r) { val = r;}
  Number & operator=(Uint32 r) { val = r; return * this; }
  Uint32 val;
};

NdbOut &
operator<< (NdbOut & out, const Number & val){
  char p = 0;
  Uint32 loop = 1;
  while(val.val > loop){
    loop *= 1000;
    p += 3;
  }
  if(loop != 1){
    p -= 3;
    loop /= 1000;
  }

  switch(p){
  case 0:
    break;
  case 3:
    p = 'k';
    break;
  case 6:
    p = 'M';
    break;
  case 9:
    p = 'G';
    break;
  default:
    p = 0;
  }
  char str[2];
  str[0] = p;
  str[1] = 0;
  Uint32 tmp = (val.val + (loop >> 1)) / loop;
#if 1
  if(p > 0)
    out << tmp << str;
  else
    out << tmp;
#else
  out << val.val;
#endif

  return out;
}

void
Backup::execBACKUP_CONF(Signal* signal)
{
  jamEntry();
  BackupConf * conf = (BackupConf*)signal->getDataPtr();
  
  ndbout_c("Backup %d has started", conf->backupId);
}

void
Backup::execBACKUP_REF(Signal* signal)
{
  jamEntry();
  BackupRef * ref = (BackupRef*)signal->getDataPtr();

  ndbout_c("Backup (%d) has NOT started %d", ref->senderData, ref->errorCode);
}

void
Backup::execBACKUP_COMPLETE_REP(Signal* signal)
{
  jamEntry();
  BackupCompleteRep* rep = (BackupCompleteRep*)signal->getDataPtr();
 
  startTime = NdbTick_CurrentMillisecond() - startTime;
  
  ndbout_c("Backup %d has completed", rep->backupId);
  const Uint32 bytes = rep->noOfBytes;
  const Uint32 records = rep->noOfRecords;

  Number rps = xps(records, startTime);
  Number bps = xps(bytes, startTime);

  ndbout << " Data [ "
	 << Number(records) << " rows " 
	 << Number(bytes) << " bytes " << startTime << " ms ] " 
	 << " => "
	 << rps << " row/s & " << bps << "b/s" << endl;

  bps = xps(rep->noOfLogBytes, startTime);
  rps = xps(rep->noOfLogRecords, startTime);

  ndbout << " Log [ "
	 << Number(rep->noOfLogRecords) << " log records " 
	 << Number(rep->noOfLogBytes) << " bytes " << startTime << " ms ] " 
	 << " => "
	 << rps << " records/s & " << bps << "b/s" << endl;

}

void
Backup::execBACKUP_ABORT_REP(Signal* signal)
{
  jamEntry();
  BackupAbortRep* rep = (BackupAbortRep*)signal->getDataPtr();
  
  ndbout_c("Backup %d has been aborted %d", rep->backupId, rep->reason);
}

const TriggerEvent::Value triggerEventValues[] = {
  TriggerEvent::TE_INSERT,
  TriggerEvent::TE_UPDATE,
  TriggerEvent::TE_DELETE
};

const Backup::State 
Backup::validSlaveTransitions[] = {
  INITIAL,  DEFINING,
  DEFINING, DEFINED,
  DEFINED,  STARTED,
  STARTED,  STARTED, // Several START_BACKUP_REQ is sent
  STARTED,  SCANNING,
  SCANNING, STARTED,
  STARTED,  STOPPING,
  STOPPING, CLEANING,
  CLEANING, INITIAL,
  
  INITIAL,  ABORTING, // Node fail
  DEFINING, ABORTING,
  DEFINED,  ABORTING,
  STARTED,  ABORTING,
  SCANNING, ABORTING,
  STOPPING, ABORTING,
  CLEANING, ABORTING, // Node fail w/ master takeover
  ABORTING, ABORTING, // Slave who initiates ABORT should have this transition
  
  ABORTING, INITIAL,
  INITIAL,  INITIAL
};

const Uint32
Backup::validSlaveTransitionsCount = 
sizeof(Backup::validSlaveTransitions) / sizeof(Backup::State);

void
Backup::CompoundState::setState(State newState){
  bool found = false;
  const State currState = state;
  for(unsigned i = 0; i<noOfValidTransitions; i+= 2) {
    jam();
    if(validTransitions[i]   == currState &&
       validTransitions[i+1] == newState){
      jam();
      found = true;
      break;
    }
  }

  //ndbrequire(found);
  
  if (newState == INITIAL)
    abortState = INITIAL;
  if(newState == ABORTING && currState != ABORTING) {
    jam();
    abortState = currState;
  }
  state = newState;
#ifdef DEBUG_ABORT
  if (newState != currState) {
    ndbout_c("%u: Old state = %u, new state = %u, abort state = %u",
	     id, currState, newState, abortState);
  }
#endif
}

void
Backup::CompoundState::forceState(State newState)
{
  const State currState = state;
  if (newState == INITIAL)
    abortState = INITIAL;
  if(newState == ABORTING && currState != ABORTING) {
    jam();
    abortState = currState;
  }
  state = newState;
#ifdef DEBUG_ABORT
  if (newState != currState) {
    ndbout_c("%u: FORCE: Old state = %u, new state = %u, abort state = %u",
	     id, currState, newState, abortState);
  }
#endif
}

Backup::Table::Table(ArrayPool<Attribute> & ah, 
		     ArrayPool<Fragment> & fh) 
  : attributes(ah), fragments(fh)
{
  triggerIds[0] = ILLEGAL_TRIGGER_ID;
  triggerIds[1] = ILLEGAL_TRIGGER_ID;
  triggerIds[2] = ILLEGAL_TRIGGER_ID;
  triggerAllocated[0] = false;
  triggerAllocated[1] = false;
  triggerAllocated[2] = false;
}

/*****************************************************************************
 * 
 * Node state handling
 *
 *****************************************************************************/
void
Backup::execNODE_FAILREP(Signal* signal)
{
  jamEntry();

  NodeFailRep * rep = (NodeFailRep*)signal->getDataPtr();
  
  bool doStuff = false;
  /*
  Start by saving important signal data which will be destroyed before the
  process is completed.
  */
  NodeId new_master_node_id = rep->masterNodeId;
  Uint32 theFailedNodes[NodeBitmask::Size];
  for (Uint32 i = 0; i < NodeBitmask::Size; i++)
    theFailedNodes[i] = rep->theNodes[i];
  
  c_masterNodeId = new_master_node_id;

  NodePtr nodePtr;
  for(c_nodes.first(nodePtr); nodePtr.i != RNIL; c_nodes.next(nodePtr)) {
    jam();
    if(NodeBitmask::get(theFailedNodes, nodePtr.p->nodeId)){
      if(nodePtr.p->alive){
	jam();
	ndbrequire(c_aliveNodes.get(nodePtr.p->nodeId));
	doStuff = true;
      } else {
        jam();
	ndbrequire(!c_aliveNodes.get(nodePtr.p->nodeId));
      }//if
      nodePtr.p->alive = 0;
      c_aliveNodes.clear(nodePtr.p->nodeId);
    }//if
  }//for

  if(!doStuff){
    jam();
    return;
  }//if
  
#ifdef DEBUG_ABORT
  ndbout_c("****************** Node fail rep ******************");
#endif

  NodeId newCoordinator = c_masterNodeId;
  BackupRecordPtr ptr;
  for(c_backups.first(ptr); ptr.i != RNIL; c_backups.next(ptr)) {
    jam();
    checkNodeFail(signal, ptr, newCoordinator, theFailedNodes);
  }
}

bool
Backup::verifyNodesAlive(BackupRecordPtr ptr,
			 const NdbNodeBitmask& aNodeBitMask)
{
  Uint32 version = getNodeInfo(getOwnNodeId()).m_version;
  for (Uint32 i = 0; i < MAX_NDB_NODES; i++) {
    jam();
    if(aNodeBitMask.get(i)) {
      if(!c_aliveNodes.get(i)){
        jam();
	ptr.p->setErrorCode(AbortBackupOrd::BackupFailureDueToNodeFail);
        return false;
      }//if
      if(getNodeInfo(i).m_version != version)
      {
	jam();
	ptr.p->setErrorCode(AbortBackupOrd::IncompatibleVersions);
	return false;
      }
    }//if
  }//for
  return true;
}

void
Backup::checkNodeFail(Signal* signal,
		      BackupRecordPtr ptr,
		      NodeId newCoord,
		      Uint32 theFailedNodes[NodeBitmask::Size])
{
  NdbNodeBitmask mask;
  mask.assign(2, theFailedNodes);

  /* Update ptr.p->nodes to be up to date with current alive nodes
   */
  NodePtr nodePtr;
  bool found = false;
  for(c_nodes.first(nodePtr); nodePtr.i != RNIL; c_nodes.next(nodePtr)) {
    jam();
    if(NodeBitmask::get(theFailedNodes, nodePtr.p->nodeId)) {
      jam();
      if (ptr.p->nodes.get(nodePtr.p->nodeId)) {
	jam();
	ptr.p->nodes.clear(nodePtr.p->nodeId); 
	found = true;
      }
    }//if
  }//for

  if(!found) {
    jam();
    return; // failed node is not part of backup process, safe to continue
  }

  if(mask.get(refToNode(ptr.p->masterRef)))
  {
    /**
     * Master died...abort
     */
    ptr.p->masterRef = reference();
    ptr.p->nodes.clear();
    ptr.p->nodes.set(getOwnNodeId());
    ptr.p->setErrorCode(AbortBackupOrd::BackupFailureDueToNodeFail);
    switch(ptr.p->m_gsn){
    case GSN_DEFINE_BACKUP_REQ:
    case GSN_START_BACKUP_REQ:
    case GSN_BACKUP_FRAGMENT_REQ:
    case GSN_STOP_BACKUP_REQ:
      // I'm currently processing...reply to self and abort...
      ptr.p->masterData.gsn = ptr.p->m_gsn;
      ptr.p->masterData.sendCounter = ptr.p->nodes;
      return;
    case GSN_DEFINE_BACKUP_REF:
    case GSN_DEFINE_BACKUP_CONF:
    case GSN_START_BACKUP_REF:
    case GSN_START_BACKUP_CONF:
    case GSN_BACKUP_FRAGMENT_REF:
    case GSN_BACKUP_FRAGMENT_CONF:
    case GSN_STOP_BACKUP_REF:
    case GSN_STOP_BACKUP_CONF:
      ptr.p->masterData.gsn = GSN_DEFINE_BACKUP_REQ;
      masterAbort(signal, ptr);
      return;
    case GSN_ABORT_BACKUP_ORD:
      // Already aborting
      return;
    }
  }
  else if (newCoord == getOwnNodeId())
  {
    /**
     * I'm master for this backup
     */
    jam();
    CRASH_INSERTION((10001));
#ifdef DEBUG_ABORT
    ndbout_c("**** Master: Node failed: Master id = %u", 
	     refToNode(ptr.p->masterRef));
#endif

    Uint32 gsn, len, pos;
    ptr.p->nodes.bitANDC(mask);
    switch(ptr.p->masterData.gsn){
    case GSN_DEFINE_BACKUP_REQ:
    {
      DefineBackupRef * ref = (DefineBackupRef*)signal->getDataPtr();
      ref->backupPtr = ptr.i;
      ref->backupId = ptr.p->backupId;
      ref->errorCode = AbortBackupOrd::BackupFailureDueToNodeFail;
      gsn= GSN_DEFINE_BACKUP_REF;
      len= DefineBackupRef::SignalLength;
      pos= &ref->nodeId - signal->getDataPtr();
      break;
    }
    case GSN_START_BACKUP_REQ:
    {
      StartBackupRef * ref = (StartBackupRef*)signal->getDataPtr();
      ref->backupPtr = ptr.i;
      ref->backupId = ptr.p->backupId;
      ref->errorCode = AbortBackupOrd::BackupFailureDueToNodeFail;
      gsn= GSN_START_BACKUP_REF;
      len= StartBackupRef::SignalLength;
      pos= &ref->nodeId - signal->getDataPtr();
      break;
    }
    case GSN_BACKUP_FRAGMENT_REQ:
    {
      BackupFragmentRef * ref = (BackupFragmentRef*)signal->getDataPtr();
      ref->backupPtr = ptr.i;
      ref->backupId = ptr.p->backupId;
      ref->errorCode = AbortBackupOrd::BackupFailureDueToNodeFail;
      gsn= GSN_BACKUP_FRAGMENT_REF;
      len= BackupFragmentRef::SignalLength;
      pos= &ref->nodeId - signal->getDataPtr();
      break;
    }
    case GSN_STOP_BACKUP_REQ:
    {
      StopBackupRef * ref = (StopBackupRef*)signal->getDataPtr();
      ref->backupPtr = ptr.i;
      ref->backupId = ptr.p->backupId;
      ref->errorCode = AbortBackupOrd::BackupFailureDueToNodeFail;
      gsn= GSN_STOP_BACKUP_REF;
      len= StopBackupRef::SignalLength;
      pos= &ref->nodeId - signal->getDataPtr();
      break;
    }
    case GSN_WAIT_GCP_REQ:
    case GSN_DROP_TRIG_REQ:
    case GSN_CREATE_TRIG_REQ:
    case GSN_ALTER_TRIG_REQ:
      ptr.p->setErrorCode(AbortBackupOrd::BackupFailureDueToNodeFail);
      return;
    case GSN_UTIL_SEQUENCE_REQ:
    case GSN_UTIL_LOCK_REQ:
      return;
    default:
      ndbrequire(false);
    }
    
    for(Uint32 i = 0; (i = mask.find(i+1)) != NdbNodeBitmask::NotFound; )
    {
      signal->theData[pos] = i;
      sendSignal(reference(), gsn, signal, len, JBB);
#ifdef DEBUG_ABORT
      ndbout_c("sending %d to self from %d", gsn, i);
#endif
    }
    return;
  }//if
  
  /**
   * I abort myself as slave if not master
   */
  CRASH_INSERTION((10021));
} 

void
Backup::execINCL_NODEREQ(Signal* signal)
{
  jamEntry();
  
  const Uint32 senderRef = signal->theData[0];
  const Uint32 inclNode  = signal->theData[1];

  NodePtr node;
  for(c_nodes.first(node); node.i != RNIL; c_nodes.next(node)) {
    jam();
    const Uint32 nodeId = node.p->nodeId;
    if(inclNode == nodeId){
      jam();
      
      ndbrequire(node.p->alive == 0);
      ndbrequire(!c_aliveNodes.get(nodeId));
      
      node.p->alive = 1;
      c_aliveNodes.set(nodeId);
      
      break;
    }//if
  }//for
  signal->theData[0] = reference();
  sendSignal(senderRef, GSN_INCL_NODECONF, signal, 1, JBB);
}

/*****************************************************************************
 * 
 * Master functionallity - Define backup
 *
 *****************************************************************************/

void
Backup::execBACKUP_REQ(Signal* signal)
{
  jamEntry();
  BackupReq * req = (BackupReq*)signal->getDataPtr();
  
  const Uint32 senderData = req->senderData;
  const BlockReference senderRef = signal->senderBlockRef();
  const Uint32 dataLen32 = req->backupDataLen; // In 32 bit words
  const Uint32 flags = signal->getLength() > 2 ? req->flags : 2;

  if(getOwnNodeId() != getMasterNodeId()) {
    jam();
    sendBackupRef(senderRef, flags, signal, senderData, BackupRef::IAmNotMaster);
    return;
  }//if

  if (m_diskless)
  {
    sendBackupRef(senderRef, flags, signal, senderData, 
		  BackupRef::CannotBackupDiskless);
    return;
  }
  
  if(dataLen32 != 0) {
    jam();
    sendBackupRef(senderRef, flags, signal, senderData, 
		  BackupRef::BackupDefinitionNotImplemented);
    return;
  }//if
  
#ifdef DEBUG_ABORT
  dumpUsedResources();
#endif
  /**
   * Seize a backup record
   */
  BackupRecordPtr ptr;
  c_backups.seize(ptr);
  if(ptr.i == RNIL) {
    jam();
    sendBackupRef(senderRef, flags, signal, senderData, BackupRef::OutOfBackupRecord);
    return;
  }//if

  ndbrequire(ptr.p->tables.isEmpty());
  
  ptr.p->m_gsn = 0;
  ptr.p->errorCode = 0;
  ptr.p->clientRef = senderRef;
  ptr.p->clientData = senderData;
  ptr.p->flags = flags;
  ptr.p->masterRef = reference();
  ptr.p->nodes = c_aliveNodes;
  ptr.p->backupId = 0;
  ptr.p->backupKey[0] = 0;
  ptr.p->backupKey[1] = 0;
  ptr.p->backupDataLen = 0;
  ptr.p->masterData.errorCode = 0;

  UtilSequenceReq * utilReq = (UtilSequenceReq*)signal->getDataPtrSend();
    
  ptr.p->masterData.gsn = GSN_UTIL_SEQUENCE_REQ;
  utilReq->senderData  = ptr.i;
  utilReq->sequenceId  = BACKUP_SEQUENCE;
  utilReq->requestType = UtilSequenceReq::NextVal;
  sendSignal(DBUTIL_REF, GSN_UTIL_SEQUENCE_REQ, 
	     signal, UtilSequenceReq::SignalLength, JBB);
}

void
Backup::execUTIL_SEQUENCE_REF(Signal* signal)
{
  BackupRecordPtr ptr;
  jamEntry();
  UtilSequenceRef * utilRef = (UtilSequenceRef*)signal->getDataPtr();
  ptr.i = utilRef->senderData;
  c_backupPool.getPtr(ptr);
  ndbrequire(ptr.p->masterData.gsn == GSN_UTIL_SEQUENCE_REQ);
  sendBackupRef(signal, ptr, BackupRef::SequenceFailure);
}//execUTIL_SEQUENCE_REF()


void
Backup::sendBackupRef(Signal* signal, BackupRecordPtr ptr, Uint32 errorCode)
{
  jam();
  sendBackupRef(ptr.p->clientRef, ptr.p->flags, signal, ptr.p->clientData, errorCode);
  cleanup(signal, ptr);
}

void
Backup::sendBackupRef(BlockReference senderRef, Uint32 flags, Signal *signal,
		      Uint32 senderData, Uint32 errorCode)
{
  jam();
  if (SEND_BACKUP_STARTED_FLAG(flags))
  {
    BackupRef* ref = (BackupRef*)signal->getDataPtrSend();
    ref->senderData = senderData;
    ref->errorCode = errorCode;
    ref->masterRef = numberToRef(BACKUP, getMasterNodeId());
    sendSignal(senderRef, GSN_BACKUP_REF, signal, BackupRef::SignalLength, JBB);
  }

  if(errorCode != BackupRef::IAmNotMaster){
    signal->theData[0] = NDB_LE_BackupFailedToStart;
    signal->theData[1] = senderRef;
    signal->theData[2] = errorCode;
    sendSignal(CMVMI_REF, GSN_EVENT_REP, signal, 3, JBB);
  }
}

void
Backup::execUTIL_SEQUENCE_CONF(Signal* signal)
{
  jamEntry();

  UtilSequenceConf * conf = (UtilSequenceConf*)signal->getDataPtr();
  
  if(conf->requestType == UtilSequenceReq::Create) 
  {
    jam();
    sendSTTORRY(signal); // At startup in NDB
    return;
  }

  BackupRecordPtr ptr;
  ptr.i = conf->senderData;
  c_backupPool.getPtr(ptr);

  ndbrequire(ptr.p->masterData.gsn == GSN_UTIL_SEQUENCE_REQ);

  if (ptr.p->checkError())
  {
    jam();
    sendBackupRef(signal, ptr, ptr.p->errorCode);
    return;
  }//if

  if (ERROR_INSERTED(10023)) 
  {
    sendBackupRef(signal, ptr, 323);
    return;
  }//if


  {
    Uint64 backupId;
    memcpy(&backupId,conf->sequenceValue,8);
    ptr.p->backupId= (Uint32)backupId;
  }
  ptr.p->backupKey[0] = (getOwnNodeId() << 16) | (ptr.p->backupId & 0xFFFF);
  ptr.p->backupKey[1] = NdbTick_CurrentMillisecond();

  ptr.p->masterData.gsn = GSN_UTIL_LOCK_REQ;
  Mutex mutex(signal, c_mutexMgr, ptr.p->masterData.m_defineBackupMutex);
  Callback c = { safe_cast(&Backup::defineBackupMutex_locked), ptr.i };
  ndbrequire(mutex.lock(c));

  return;
}

void
Backup::defineBackupMutex_locked(Signal* signal, Uint32 ptrI, Uint32 retVal){
  jamEntry();
  ndbrequire(retVal == 0);
  
  BackupRecordPtr ptr;
  ptr.i = ptrI;
  c_backupPool.getPtr(ptr);
  
  ndbrequire(ptr.p->masterData.gsn == GSN_UTIL_LOCK_REQ);

  ptr.p->masterData.gsn = GSN_UTIL_LOCK_REQ;
  Mutex mutex(signal, c_mutexMgr, ptr.p->masterData.m_dictCommitTableMutex);
  Callback c = { safe_cast(&Backup::dictCommitTableMutex_locked), ptr.i };
  ndbrequire(mutex.lock(c));
}

void
Backup::dictCommitTableMutex_locked(Signal* signal, Uint32 ptrI,Uint32 retVal)
{
  jamEntry();
  ndbrequire(retVal == 0);
  
  /**
   * We now have both the mutexes
   */
  BackupRecordPtr ptr;
  ptr.i = ptrI;
  c_backupPool.getPtr(ptr);

  ndbrequire(ptr.p->masterData.gsn == GSN_UTIL_LOCK_REQ);

  if (ERROR_INSERTED(10031)) {
    ptr.p->setErrorCode(331);
  }//if

  if (ptr.p->checkError())
  {
    jam();
    
    /**
     * Unlock mutexes
     */
    jam();
    Mutex mutex1(signal, c_mutexMgr, ptr.p->masterData.m_dictCommitTableMutex);
    jam();
    mutex1.unlock(); // ignore response
    
    jam();
    Mutex mutex2(signal, c_mutexMgr, ptr.p->masterData.m_defineBackupMutex);
    jam();
    mutex2.unlock(); // ignore response
    
    sendBackupRef(signal, ptr, ptr.p->errorCode);
    return;
  }//if
  
  sendDefineBackupReq(signal, ptr);
}

/*****************************************************************************
 * 
 * Master functionallity - Define backup cont'd (from now on all slaves are in)
 *
 *****************************************************************************/

bool
Backup::haveAllSignals(BackupRecordPtr ptr, Uint32 gsn, Uint32 nodeId)
{ 
  ndbrequire(ptr.p->masterRef == reference());
  ndbrequire(ptr.p->masterData.gsn == gsn);
  ndbrequire(!ptr.p->masterData.sendCounter.done());
  ndbrequire(ptr.p->masterData.sendCounter.isWaitingFor(nodeId));
  
  ptr.p->masterData.sendCounter.clearWaitingFor(nodeId);
  return ptr.p->masterData.sendCounter.done();
}

void
Backup::sendDefineBackupReq(Signal *signal, BackupRecordPtr ptr)
{
  /**
   * Sending define backup to all participants
   */
  DefineBackupReq * req = (DefineBackupReq*)signal->getDataPtrSend();
  req->backupId = ptr.p->backupId;
  req->clientRef = ptr.p->clientRef;
  req->clientData = ptr.p->clientData;
  req->senderRef = reference();
  req->backupPtr = ptr.i;
  req->backupKey[0] = ptr.p->backupKey[0];
  req->backupKey[1] = ptr.p->backupKey[1];
  req->nodes = ptr.p->nodes;
  req->backupDataLen = ptr.p->backupDataLen;
  req->flags = ptr.p->flags;
  
  ptr.p->masterData.gsn = GSN_DEFINE_BACKUP_REQ;
  ptr.p->masterData.sendCounter = ptr.p->nodes;
  NodeReceiverGroup rg(BACKUP, ptr.p->nodes);
  sendSignal(rg, GSN_DEFINE_BACKUP_REQ, signal, 
	     DefineBackupReq::SignalLength, JBB);
  
  /**
   * Now send backup data
   */
  const Uint32 len = ptr.p->backupDataLen;
  if(len == 0){
    /**
     * No data to send
     */
    jam();
    return;
  }//if
  
  /**
   * Not implemented
   */
  ndbrequire(0);
}

void
Backup::execDEFINE_BACKUP_REF(Signal* signal)
{
  jamEntry();

  DefineBackupRef* ref = (DefineBackupRef*)signal->getDataPtr();
  
  const Uint32 ptrI = ref->backupPtr;
  //const Uint32 backupId = ref->backupId;
  const Uint32 nodeId = ref->nodeId;
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);
  
  ptr.p->setErrorCode(ref->errorCode);
  defineBackupReply(signal, ptr, nodeId);
}

void
Backup::execDEFINE_BACKUP_CONF(Signal* signal)
{
  jamEntry();

  DefineBackupConf* conf = (DefineBackupConf*)signal->getDataPtr();
  const Uint32 ptrI = conf->backupPtr;
  //const Uint32 backupId = conf->backupId;
  const Uint32 nodeId = refToNode(signal->senderBlockRef());

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  if (ERROR_INSERTED(10024))
  {
    ptr.p->setErrorCode(324);
  }

  defineBackupReply(signal, ptr, nodeId);
}

void
Backup::defineBackupReply(Signal* signal, BackupRecordPtr ptr, Uint32 nodeId)
{
  if (!haveAllSignals(ptr, GSN_DEFINE_BACKUP_REQ, nodeId)) {
    jam();
    return;
  }

  /**
   * Unlock mutexes
   */
  jam();
  Mutex mutex1(signal, c_mutexMgr, ptr.p->masterData.m_dictCommitTableMutex);
  jam();
  mutex1.unlock(); // ignore response

  jam();
  Mutex mutex2(signal, c_mutexMgr, ptr.p->masterData.m_defineBackupMutex);
  jam();
  mutex2.unlock(); // ignore response

  if(ptr.p->checkError())
  {
    jam();
    masterAbort(signal, ptr);
    return;
  }
  
  /**
   * Reply to client
   */
  CRASH_INSERTION((10034));

  if (SEND_BACKUP_STARTED_FLAG(ptr.p->flags))
  {
    BackupConf * conf = (BackupConf*)signal->getDataPtrSend();
    conf->backupId = ptr.p->backupId;
    conf->senderData = ptr.p->clientData;
    conf->nodes = ptr.p->nodes;
    sendSignal(ptr.p->clientRef, GSN_BACKUP_CONF, signal, 
	       BackupConf::SignalLength, JBB);
  }

  signal->theData[0] = NDB_LE_BackupStarted;
  signal->theData[1] = ptr.p->clientRef;
  signal->theData[2] = ptr.p->backupId;
  ptr.p->nodes.copyto(NdbNodeBitmask::Size, signal->theData+3);
  sendSignal(CMVMI_REF, GSN_EVENT_REP, signal, 3+NdbNodeBitmask::Size, JBB);

  /**
   * We've received GSN_DEFINE_BACKUP_CONF from all participants.
   *
   * Our next step is to send START_BACKUP_REQ to all participants,
   * who will then send CREATE_TRIG_REQ for all tables to their local
   * DBTUP.
   */
  TablePtr tabPtr;
  ptr.p->tables.first(tabPtr);

  sendStartBackup(signal, ptr, tabPtr);
}

/*****************************************************************************
 * 
 * Master functionallity - Prepare triggers
 *
 *****************************************************************************/
void
Backup::createAttributeMask(TablePtr tabPtr, 
			    Bitmask<MAXNROFATTRIBUTESINWORDS> & mask)
{
  mask.clear();
  Table & table = * tabPtr.p;
  Ptr<Attribute> attrPtr;
  table.attributes.first(attrPtr);
  for(; !attrPtr.isNull(); table.attributes.next(attrPtr))
  {
    jam();
    mask.set(attrPtr.p->data.attrId);
  }
}

void
Backup::sendCreateTrig(Signal* signal, 
			   BackupRecordPtr ptr, TablePtr tabPtr)
{
  CreateTrigReq * req =(CreateTrigReq *)signal->getDataPtrSend();

  /*
   * First, setup the structures
   */
  for(Uint32 j=0; j<3; j++) {
    jam();

    TriggerPtr trigPtr;
    if(!ptr.p->triggers.seize(trigPtr)) {
      jam();
      ptr.p->m_gsn = GSN_START_BACKUP_REF;
      StartBackupRef* ref = (StartBackupRef*)signal->getDataPtrSend();
      ref->backupPtr = ptr.i;
      ref->backupId = ptr.p->backupId;
      ref->errorCode = StartBackupRef::FailedToAllocateTriggerRecord;
      ref->nodeId = getOwnNodeId();
      sendSignal(ptr.p->masterRef, GSN_START_BACKUP_REF, signal,
		 StartBackupRef::SignalLength, JBB);
      return;
    } // if

    const Uint32 triggerId= trigPtr.i;
    tabPtr.p->triggerIds[j] = triggerId;
    tabPtr.p->triggerAllocated[j] = true;
    trigPtr.p->backupPtr = ptr.i;
    trigPtr.p->tableId = tabPtr.p->tableId;
    trigPtr.p->tab_ptr_i = tabPtr.i;
    trigPtr.p->logEntry = 0;
    trigPtr.p->event = j;
    trigPtr.p->maxRecordSize = 4096;
    trigPtr.p->operation =
      &ptr.p->files.getPtr(ptr.p->logFilePtr)->operation;
    trigPtr.p->operation->noOfBytes = 0;
    trigPtr.p->operation->noOfRecords = 0;
    trigPtr.p->errorCode = 0;
  } // for

  /*
   * now ask DBTUP to create
   */
  ptr.p->slaveData.gsn = GSN_CREATE_TRIG_REQ;
  ptr.p->slaveData.trigSendCounter = 3;
  ptr.p->slaveData.createTrig.tableId = tabPtr.p->tableId;

  req->setUserRef(reference());
  req->setReceiverRef(reference());
  req->setConnectionPtr(ptr.i);
  req->setRequestType(CreateTrigReq::RT_USER);

  Bitmask<MAXNROFATTRIBUTESINWORDS> attrMask;
  createAttributeMask(tabPtr, attrMask);
  req->setAttributeMask(attrMask);
  req->setTableId(tabPtr.p->tableId);
  req->setIndexId(RNIL);        // not used
  req->setTriggerType(TriggerType::SUBSCRIPTION);
  req->setTriggerActionTime(TriggerActionTime::TA_DETACHED);
  req->setMonitorReplicas(true);
  req->setMonitorAllAttributes(false);
  req->setOnline(true);

  for (int i=0; i < 3; i++) {
    req->setTriggerId(tabPtr.p->triggerIds[i]);
    req->setTriggerEvent(triggerEventValues[i]);

    sendSignal(DBTUP_REF, GSN_CREATE_TRIG_REQ,
	       signal, CreateTrigReq::SignalLength, JBB);
  }
}

void
Backup::execCREATE_TRIG_CONF(Signal* signal)
{
  jamEntry();
  CreateTrigConf * conf = (CreateTrigConf*)signal->getDataPtr();
  
  const Uint32 ptrI = conf->getConnectionPtr();
  const Uint32 tableId = conf->getTableId();
  const TriggerEvent::Value type = conf->getTriggerEvent();
  const Uint32 triggerId = conf->getTriggerId();

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  /**
   * Verify that I'm waiting for this conf
   *
   * ptr.p->masterRef != reference()
   * as slaves and masters have triggers now.
   */
  ndbrequire(ptr.p->slaveData.gsn == GSN_CREATE_TRIG_REQ);
  ndbrequire(ptr.p->slaveData.trigSendCounter.done() == false);
  ndbrequire(ptr.p->slaveData.createTrig.tableId == tableId);

  TablePtr tabPtr;
  ndbrequire(findTable(ptr, tabPtr, tableId));
  ndbrequire(type < 3); // if some decides to change the enums

  createTrigReply(signal, ptr);
}

void
Backup::execCREATE_TRIG_REF(Signal* signal)
{
  jamEntry();
  CreateTrigRef* ref = (CreateTrigRef*)signal->getDataPtr();

  const Uint32 ptrI = ref->getConnectionPtr();
  const Uint32 tableId = ref->getTableId();

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  /**
   * Verify that I'm waiting for this ref
   *
   * ptr.p->masterRef != reference()
   * as slaves and masters have triggers now
   */
  ndbrequire(ptr.p->slaveData.gsn == GSN_CREATE_TRIG_REQ);
  ndbrequire(ptr.p->slaveData.trigSendCounter.done() == false);
  ndbrequire(ptr.p->slaveData.createTrig.tableId == tableId);

  ptr.p->setErrorCode(ref->getErrorCode());

  createTrigReply(signal, ptr);
}

void
Backup::createTrigReply(Signal* signal, BackupRecordPtr ptr)
{
  CRASH_INSERTION(10003);

  /**
   * Check finished with table
   */
  ptr.p->slaveData.trigSendCounter--;
  if(ptr.p->slaveData.trigSendCounter.done() == false){
    jam();
    return;
  }//if

  if (ERROR_INSERTED(10025))
  {
    ptr.p->errorCode = 325;
  }

  if(ptr.p->checkError()) {
    jam();
    ptr.p->m_gsn = GSN_START_BACKUP_REF;
    StartBackupRef* ref = (StartBackupRef*)signal->getDataPtrSend();
    ref->backupPtr = ptr.i;
    ref->backupId = ptr.p->backupId;
    ref->errorCode = ptr.p->errorCode;
    ref->nodeId = getOwnNodeId();
    sendSignal(ptr.p->masterRef, GSN_START_BACKUP_REF, signal,
               StartBackupRef::SignalLength, JBB);
    return;
  }//if

  TablePtr tabPtr;
  ndbrequire(findTable(ptr, tabPtr, ptr.p->slaveData.createTrig.tableId));

  /**
   * Next table
   */
  ptr.p->tables.next(tabPtr);
  if(tabPtr.i != RNIL){
    jam();
    sendCreateTrig(signal, ptr, tabPtr);
    return;
  }//if

  /**
   * We've finished creating triggers.
   *
   * send conf and wait
   */
  ptr.p->m_gsn = GSN_START_BACKUP_CONF;
  StartBackupConf* conf = (StartBackupConf*)signal->getDataPtrSend();
  conf->backupPtr = ptr.i;
  conf->backupId = ptr.p->backupId;
  sendSignal(ptr.p->masterRef, GSN_START_BACKUP_CONF, signal,
	     StartBackupConf::SignalLength, JBB);
}

/*****************************************************************************
 * 
 * Master functionallity - Start backup
 *
 *****************************************************************************/
void
Backup::sendStartBackup(Signal* signal, BackupRecordPtr ptr, TablePtr tabPtr)
{

  ptr.p->masterData.startBackup.tablePtr = tabPtr.i;

  StartBackupReq* req = (StartBackupReq*)signal->getDataPtrSend();
  req->backupId = ptr.p->backupId;
  req->backupPtr = ptr.i;

  /**
   * We use trigger Ids that are unique to BACKUP.
   * These don't interfere with other triggers (e.g. from DBDICT)
   * as there is a special case in DBTUP.
   *
   * Consequently, backups during online upgrade won't work
   */
  ptr.p->masterData.gsn = GSN_START_BACKUP_REQ;
  ptr.p->masterData.sendCounter = ptr.p->nodes;
  NodeReceiverGroup rg(BACKUP, ptr.p->nodes);
  sendSignal(rg, GSN_START_BACKUP_REQ, signal,
	     StartBackupReq::SignalLength, JBB);
}

void
Backup::execSTART_BACKUP_REF(Signal* signal)
{
  jamEntry();

  StartBackupRef* ref = (StartBackupRef*)signal->getDataPtr();
  const Uint32 ptrI = ref->backupPtr;
  //const Uint32 backupId = ref->backupId;
  const Uint32 nodeId = ref->nodeId;

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  ptr.p->setErrorCode(ref->errorCode);
  startBackupReply(signal, ptr, nodeId);
}

void
Backup::execSTART_BACKUP_CONF(Signal* signal)
{
  jamEntry();
  
  StartBackupConf* conf = (StartBackupConf*)signal->getDataPtr();
  const Uint32 ptrI = conf->backupPtr;
  //const Uint32 backupId = conf->backupId;
  const Uint32 nodeId = refToNode(signal->senderBlockRef());
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  startBackupReply(signal, ptr, nodeId);
}

void
Backup::startBackupReply(Signal* signal, BackupRecordPtr ptr, Uint32 nodeId)
{

  CRASH_INSERTION((10004));

  if (!haveAllSignals(ptr, GSN_START_BACKUP_REQ, nodeId)) {
    jam();
    return;
  }

  if (ERROR_INSERTED(10026))
  {
    ptr.p->errorCode = 326;
  }

  if(ptr.p->checkError()){
    jam();
    masterAbort(signal, ptr);
    return;
  }

  /**
   * Wait for GCP
   */
  ptr.p->masterData.gsn = GSN_WAIT_GCP_REQ;
  ptr.p->masterData.waitGCP.startBackup = true;

  WaitGCPReq * waitGCPReq = (WaitGCPReq*)signal->getDataPtrSend();
  waitGCPReq->senderRef = reference();
  waitGCPReq->senderData = ptr.i;
  waitGCPReq->requestType = WaitGCPReq::CompleteForceStart;
  sendSignal(DBDIH_REF, GSN_WAIT_GCP_REQ, signal,
	     WaitGCPReq::SignalLength,JBB);
}

void
Backup::execWAIT_GCP_REF(Signal* signal)
{
  jamEntry();
  
  CRASH_INSERTION((10006));

  WaitGCPRef * ref = (WaitGCPRef*)signal->getDataPtr();
  const Uint32 ptrI = ref->senderData;
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  ndbrequire(ptr.p->masterRef == reference());
  ndbrequire(ptr.p->masterData.gsn == GSN_WAIT_GCP_REQ);

  WaitGCPReq * req = (WaitGCPReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->senderData = ptr.i;
  req->requestType = WaitGCPReq::CompleteForceStart;
  sendSignal(DBDIH_REF, GSN_WAIT_GCP_REQ, signal, 
	     WaitGCPReq::SignalLength,JBB);
}

void
Backup::execWAIT_GCP_CONF(Signal* signal){
  jamEntry();

  CRASH_INSERTION((10007));

  WaitGCPConf * conf = (WaitGCPConf*)signal->getDataPtr();
  const Uint32 ptrI = conf->senderData;
  const Uint32 gcp = conf->gcp;
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);
  
  ndbrequire(ptr.p->masterRef == reference());
  ndbrequire(ptr.p->masterData.gsn == GSN_WAIT_GCP_REQ);
  
  if(ptr.p->checkError()) {
    jam();
    masterAbort(signal, ptr);
    return;
  }//if
  
  if(ptr.p->masterData.waitGCP.startBackup) {
    jam();
    CRASH_INSERTION((10008));
    ptr.p->startGCP = gcp;
    ptr.p->masterData.sendCounter= 0;
    ptr.p->masterData.gsn = GSN_BACKUP_FRAGMENT_REQ;
    nextFragment(signal, ptr);
    return;
  } else {
    jam();
    if(gcp >= ptr.p->startGCP + 3)
    {
      CRASH_INSERTION((10009));
      ptr.p->stopGCP = gcp;
      /**
       * Backup is complete - begin cleanup
       * STOP_BACKUP_REQ is sent to participants.
       * They then drop the local triggers
       */
      sendStopBackup(signal, ptr);
      return;
    }//if
    
    /**
     * Make sure that we got entire stopGCP 
     */
    WaitGCPReq * req = (WaitGCPReq*)signal->getDataPtrSend();
    req->senderRef = reference();
    req->senderData = ptr.i;
    req->requestType = WaitGCPReq::CompleteForceStart;
    sendSignal(DBDIH_REF, GSN_WAIT_GCP_REQ, signal, 
	       WaitGCPReq::SignalLength,JBB);
    return;
  }
}

/*****************************************************************************
 * 
 * Master functionallity - Backup fragment
 *
 *****************************************************************************/
void
Backup::nextFragment(Signal* signal, BackupRecordPtr ptr)
{
  jam();

  BackupFragmentReq* req = (BackupFragmentReq*)signal->getDataPtrSend();
  req->backupPtr = ptr.i;
  req->backupId = ptr.p->backupId;

  NodeBitmask nodes = ptr.p->nodes;
  Uint32 idleNodes = nodes.count();
  Uint32 saveIdleNodes = idleNodes;
  ndbrequire(idleNodes > 0);

  TablePtr tabPtr;
  ptr.p->tables.first(tabPtr);
  for(; tabPtr.i != RNIL && idleNodes > 0; ptr.p->tables.next(tabPtr)) {
    jam();
    FragmentPtr fragPtr;
    Array<Fragment> & frags = tabPtr.p->fragments;
    const Uint32 fragCount = frags.getSize();
    
    for(Uint32 i = 0; i<fragCount && idleNodes > 0; i++) {
      jam();
      tabPtr.p->fragments.getPtr(fragPtr, i);
      const Uint32 nodeId = fragPtr.p->node;
      if(fragPtr.p->scanning != 0) {
        jam();
	ndbrequire(nodes.get(nodeId));
	nodes.clear(nodeId);
	idleNodes--;
      } else if(fragPtr.p->scanned == 0 && nodes.get(nodeId)){
	jam();
	fragPtr.p->scanning = 1;
	nodes.clear(nodeId);
	idleNodes--;
	
	req->tableId = tabPtr.p->tableId;
	req->fragmentNo = i;
	req->count = 0;

	ptr.p->masterData.sendCounter++;
	const BlockReference ref = numberToRef(BACKUP, nodeId);
	sendSignal(ref, GSN_BACKUP_FRAGMENT_REQ, signal,
		   BackupFragmentReq::SignalLength, JBB);
      }//if
    }//for
  }//for
  
  if(idleNodes != saveIdleNodes){
    jam();
    return;
  }//if

  /**
   * Finished with all tables
   */
  {
    ptr.p->masterData.gsn = GSN_WAIT_GCP_REQ;
    ptr.p->masterData.waitGCP.startBackup = false;
    
    WaitGCPReq * req = (WaitGCPReq*)signal->getDataPtrSend();
    req->senderRef = reference();
    req->senderData = ptr.i;
    req->requestType = WaitGCPReq::CompleteForceStart;
    sendSignal(DBDIH_REF, GSN_WAIT_GCP_REQ, signal, 
	       WaitGCPReq::SignalLength, JBB);
  }
}

void
Backup::execBACKUP_FRAGMENT_CONF(Signal* signal)
{
  jamEntry();

  CRASH_INSERTION((10010));
  
  BackupFragmentConf * conf = (BackupFragmentConf*)signal->getDataPtr();
  const Uint32 ptrI = conf->backupPtr;
  //const Uint32 backupId = conf->backupId;
  const Uint32 tableId = conf->tableId;
  const Uint32 fragmentNo = conf->fragmentNo;
  const Uint32 nodeId = refToNode(signal->senderBlockRef());
  const Uint32 noOfBytes = conf->noOfBytes;
  const Uint32 noOfRecords = conf->noOfRecords;

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  ptr.p->noOfBytes += noOfBytes;
  ptr.p->noOfRecords += noOfRecords;
  ptr.p->masterData.sendCounter--;

  TablePtr tabPtr;
  ndbrequire(findTable(ptr, tabPtr, tableId));

  FragmentPtr fragPtr;
  tabPtr.p->fragments.getPtr(fragPtr, fragmentNo);

  ndbrequire(fragPtr.p->scanned == 0);
  ndbrequire(fragPtr.p->scanning == 1);
  ndbrequire(fragPtr.p->node == nodeId);

  fragPtr.p->scanned = 1;
  fragPtr.p->scanning = 0;

  if (ERROR_INSERTED(10028)) 
  {
    ptr.p->errorCode = 328;
  }

  if(ptr.p->checkError()) 
  {
    if(ptr.p->masterData.sendCounter.done())
    {
      jam();
      masterAbort(signal, ptr);
      return;
    }//if
  }
  else
  {
    nextFragment(signal, ptr);
  }
}

void
Backup::execBACKUP_FRAGMENT_REF(Signal* signal)
{
  jamEntry();

  CRASH_INSERTION((10011));

  BackupFragmentRef * ref = (BackupFragmentRef*)signal->getDataPtr();
  const Uint32 ptrI = ref->backupPtr;
  //const Uint32 backupId = ref->backupId;
  const Uint32 nodeId = ref->nodeId;
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  TablePtr tabPtr;
  ptr.p->tables.first(tabPtr);
  for(; tabPtr.i != RNIL; ptr.p->tables.next(tabPtr)) {
    jam();
    FragmentPtr fragPtr;
    Array<Fragment> & frags = tabPtr.p->fragments;
    const Uint32 fragCount = frags.getSize();
    
    for(Uint32 i = 0; i<fragCount; i++) {
      jam();
      tabPtr.p->fragments.getPtr(fragPtr, i);
        if(fragPtr.p->scanning != 0 && nodeId == fragPtr.p->node) 
      {
        jam();
	ndbrequire(fragPtr.p->scanned == 0);
	fragPtr.p->scanned = 1;
	fragPtr.p->scanning = 0;
	goto done;
      }
    }
  }
  goto err;

done:
  ptr.p->masterData.sendCounter--;
  ptr.p->setErrorCode(ref->errorCode);
  
  if(ptr.p->masterData.sendCounter.done())
  {
    jam();
    masterAbort(signal, ptr);
    return;
  }//if

err:
  AbortBackupOrd *ord = (AbortBackupOrd*)signal->getDataPtrSend();
  ord->backupId = ptr.p->backupId;
  ord->backupPtr = ptr.i;
  ord->requestType = AbortBackupOrd::LogBufferFull;
  ord->senderData= ptr.i;
  execABORT_BACKUP_ORD(signal);
}

/*****************************************************************************
 *
 * Slave functionallity - Drop triggers
 *
 *****************************************************************************/

void
Backup::sendDropTrig(Signal* signal, BackupRecordPtr ptr)
{
  TablePtr tabPtr;
  ptr.p->slaveData.gsn = GSN_DROP_TRIG_REQ;

  if (ptr.p->slaveData.dropTrig.tableId == RNIL) {
    jam();
    ptr.p->tables.first(tabPtr);
  } else {
    jam();
    ndbrequire(findTable(ptr, tabPtr, ptr.p->slaveData.dropTrig.tableId));
    ptr.p->tables.next(tabPtr);
  }//if
  if (tabPtr.i != RNIL) {
    jam();
    sendDropTrig(signal, ptr, tabPtr);
  } else {
    /**
     * Insert footers
     */
    {
      BackupFilePtr filePtr;
      ptr.p->files.getPtr(filePtr, ptr.p->logFilePtr);
      Uint32 * dst;
      ndbrequire(filePtr.p->operation.dataBuffer.getWritePtr(&dst, 1));
      * dst = 0;
      filePtr.p->operation.dataBuffer.updateWritePtr(1);
    }

    {
      BackupFilePtr filePtr;
      ptr.p->files.getPtr(filePtr, ptr.p->ctlFilePtr);

      const Uint32 gcpSz = sizeof(BackupFormat::CtlFile::GCPEntry) >> 2;

      Uint32 * dst;
      ndbrequire(filePtr.p->operation.dataBuffer.getWritePtr(&dst, gcpSz));

      BackupFormat::CtlFile::GCPEntry * gcp = 
	(BackupFormat::CtlFile::GCPEntry*)dst;

      gcp->SectionType   = htonl(BackupFormat::GCP_ENTRY);
      gcp->SectionLength = htonl(gcpSz);
      gcp->StartGCP      = htonl(ptr.p->startGCP);
      gcp->StopGCP       = htonl(ptr.p->stopGCP - 1);
      filePtr.p->operation.dataBuffer.updateWritePtr(gcpSz);
    }

    { // UNLOCK while dropping trigger for better timeslicing
      TablePtr tabPtr;
      for(ptr.p->tables.first(tabPtr); tabPtr.i != RNIL;
	  ptr.p->tables.next(tabPtr))
      {
	signal->theData[0] = tabPtr.p->tableId;
	signal->theData[1] = 0; // unlock
	EXECUTE_DIRECT(DBDICT, GSN_BACKUP_FRAGMENT_REQ, signal, 2);
      }
    }
    closeFiles(signal, ptr);
  }
}

void
Backup::sendDropTrig(Signal* signal, BackupRecordPtr ptr, TablePtr tabPtr)
{
  jam();
  DropTrigReq * req = (DropTrigReq *)signal->getDataPtrSend();

  ptr.p->slaveData.gsn = GSN_DROP_TRIG_REQ;
  ptr.p->slaveData.trigSendCounter = 0;
  req->setConnectionPtr(ptr.i);
  req->setUserRef(reference()); // Sending to myself
  req->setRequestType(DropTrigReq::RT_USER);
  req->setIndexId(RNIL);
  req->setTriggerInfo(0);       // not used on DROP
  req->setTriggerType(TriggerType::SUBSCRIPTION);
  req->setTriggerActionTime(TriggerActionTime::TA_DETACHED);

  ptr.p->slaveData.dropTrig.tableId = tabPtr.p->tableId;
  req->setTableId(tabPtr.p->tableId);

  for (int i = 0; i < 3; i++) {
    Uint32 id = tabPtr.p->triggerIds[i];
    req->setTriggerId(id);
    req->setTriggerEvent(triggerEventValues[i]);
    sendSignal(DBTUP_REF, GSN_DROP_TRIG_REQ,
	       signal, DropTrigReq::SignalLength, JBB);
    ptr.p->slaveData.trigSendCounter ++;
  }
}

void
Backup::execDROP_TRIG_REF(Signal* signal)
{
  jamEntry();

  DropTrigRef* ref = (DropTrigRef*)signal->getDataPtr();
  const Uint32 ptrI = ref->getConnectionPtr();

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  ndbout << "ERROR DROPPING TRIGGER: " << ref->getConf()->getTriggerId();
  ndbout << " Err: " << (Uint32)ref->getErrorCode() << endl << endl;

  dropTrigReply(signal, ptr);
}

void
Backup::execDROP_TRIG_CONF(Signal* signal)
{
  jamEntry();
  
  DropTrigConf* conf = (DropTrigConf*)signal->getDataPtr();
  const Uint32 ptrI = conf->getConnectionPtr();
  const Uint32 triggerId= conf->getTriggerId();

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  dropTrigReply(signal, ptr);
}

void
Backup::dropTrigReply(Signal* signal, BackupRecordPtr ptr)
{
  CRASH_INSERTION((10012));

  ndbrequire(ptr.p->slaveData.gsn == GSN_DROP_TRIG_REQ);
  ndbrequire(ptr.p->slaveData.trigSendCounter.done() == false);

  // move from .masterData to .slaveData
  ptr.p->slaveData.trigSendCounter--;
  if(ptr.p->slaveData.trigSendCounter.done() == false){
    jam();
    return;
  }//if

  sendDropTrig(signal, ptr); // recursive next
}

/*****************************************************************************
 * 
 * Master functionallity - Stop backup
 *
 *****************************************************************************/
void
Backup::execSTOP_BACKUP_REF(Signal* signal)
{
  jamEntry();

  StopBackupRef* ref = (StopBackupRef*)signal->getDataPtr();
  const Uint32 ptrI = ref->backupPtr;
  //const Uint32 backupId = ref->backupId;
  const Uint32 nodeId = ref->nodeId;
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  ptr.p->setErrorCode(ref->errorCode);
  stopBackupReply(signal, ptr, nodeId);
}

void
Backup::sendStopBackup(Signal* signal, BackupRecordPtr ptr)
{
  jam();

  StopBackupReq* stop = (StopBackupReq*)signal->getDataPtrSend();
  stop->backupPtr = ptr.i;
  stop->backupId = ptr.p->backupId;
  stop->startGCP = ptr.p->startGCP;
  stop->stopGCP = ptr.p->stopGCP;

  ptr.p->masterData.gsn = GSN_STOP_BACKUP_REQ;
  ptr.p->masterData.sendCounter = ptr.p->nodes;
  NodeReceiverGroup rg(BACKUP, ptr.p->nodes);
  sendSignal(rg, GSN_STOP_BACKUP_REQ, signal, 
	     StopBackupReq::SignalLength, JBB);
}

void
Backup::execSTOP_BACKUP_CONF(Signal* signal)
{
  jamEntry();
  
  StopBackupConf* conf = (StopBackupConf*)signal->getDataPtr();
  const Uint32 ptrI = conf->backupPtr;
  //const Uint32 backupId = conf->backupId;
  const Uint32 nodeId = refToNode(signal->senderBlockRef());
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  ptr.p->noOfLogBytes += conf->noOfLogBytes;
  ptr.p->noOfLogRecords += conf->noOfLogRecords;
  
  stopBackupReply(signal, ptr, nodeId);
}

void
Backup::stopBackupReply(Signal* signal, BackupRecordPtr ptr, Uint32 nodeId)
{
  CRASH_INSERTION((10013));

  if (!haveAllSignals(ptr, GSN_STOP_BACKUP_REQ, nodeId)) {
    jam();
    return;
  }

  sendAbortBackupOrd(signal, ptr, AbortBackupOrd::BackupComplete);
  
  if(!ptr.p->checkError())
  {
    if (SEND_BACKUP_COMPLETED_FLAG(ptr.p->flags))
    {
      BackupCompleteRep * rep = (BackupCompleteRep*)signal->getDataPtrSend();
      rep->backupId = ptr.p->backupId;
      rep->senderData = ptr.p->clientData;
      rep->startGCP = ptr.p->startGCP;
      rep->stopGCP = ptr.p->stopGCP;
      rep->noOfBytes = ptr.p->noOfBytes;
      rep->noOfRecords = ptr.p->noOfRecords;
      rep->noOfLogBytes = ptr.p->noOfLogBytes;
      rep->noOfLogRecords = ptr.p->noOfLogRecords;
      rep->nodes = ptr.p->nodes;
      sendSignal(ptr.p->clientRef, GSN_BACKUP_COMPLETE_REP, signal,
		 BackupCompleteRep::SignalLength, JBB);
    }

    signal->theData[0] = NDB_LE_BackupCompleted;
    signal->theData[1] = ptr.p->clientRef;
    signal->theData[2] = ptr.p->backupId;
    signal->theData[3] = ptr.p->startGCP;
    signal->theData[4] = ptr.p->stopGCP;
    signal->theData[5] = ptr.p->noOfBytes;
    signal->theData[6] = ptr.p->noOfRecords;
    signal->theData[7] = ptr.p->noOfLogBytes;
    signal->theData[8] = ptr.p->noOfLogRecords;
    ptr.p->nodes.copyto(NdbNodeBitmask::Size, signal->theData+9);
    sendSignal(CMVMI_REF, GSN_EVENT_REP, signal, 9+NdbNodeBitmask::Size, JBB);
  }
  else
  {
    masterAbort(signal, ptr);
  }
}

/*****************************************************************************
 * 
 * Master functionallity - Abort backup
 *
 *****************************************************************************/
void
Backup::masterAbort(Signal* signal, BackupRecordPtr ptr)
{
  jam();
#ifdef DEBUG_ABORT
  ndbout_c("************ masterAbort");
#endif

  ndbassert(ptr.p->masterRef == reference());

  if(ptr.p->masterData.errorCode != 0)
  {
    jam();
    return;
  }

  if (SEND_BACKUP_COMPLETED_FLAG(ptr.p->flags))
  {
    BackupAbortRep* rep = (BackupAbortRep*)signal->getDataPtrSend();
    rep->backupId = ptr.p->backupId;
    rep->senderData = ptr.p->clientData;
    rep->reason = ptr.p->errorCode;
    sendSignal(ptr.p->clientRef, GSN_BACKUP_ABORT_REP, signal, 
	       BackupAbortRep::SignalLength, JBB);
  }
  signal->theData[0] = NDB_LE_BackupAborted;
  signal->theData[1] = ptr.p->clientRef;
  signal->theData[2] = ptr.p->backupId;
  signal->theData[3] = ptr.p->errorCode;
  sendSignal(CMVMI_REF, GSN_EVENT_REP, signal, 4, JBB);

  ndbrequire(ptr.p->errorCode);
  ptr.p->masterData.errorCode = ptr.p->errorCode;

  AbortBackupOrd *ord = (AbortBackupOrd*)signal->getDataPtrSend();
  ord->backupId = ptr.p->backupId;
  ord->backupPtr = ptr.i;
  ord->senderData= ptr.i;
  NodeReceiverGroup rg(BACKUP, ptr.p->nodes);
  
  switch(ptr.p->masterData.gsn){
  case GSN_DEFINE_BACKUP_REQ:
    ord->requestType = AbortBackupOrd::BackupFailure;
    sendSignal(rg, GSN_ABORT_BACKUP_ORD, signal, 
	       AbortBackupOrd::SignalLength, JBB);
    return;
  case GSN_CREATE_TRIG_REQ:
  case GSN_START_BACKUP_REQ:
  case GSN_ALTER_TRIG_REQ:
  case GSN_WAIT_GCP_REQ:
  case GSN_BACKUP_FRAGMENT_REQ:
    jam();
    ptr.p->stopGCP= ptr.p->startGCP + 1;
    sendStopBackup(signal, ptr); // dropping due to error
    return;
  case GSN_UTIL_SEQUENCE_REQ:
  case GSN_UTIL_LOCK_REQ:
    ndbrequire(false);
    return;
  case GSN_DROP_TRIG_REQ:
  case GSN_STOP_BACKUP_REQ:
    return;
  }
}

void
Backup::abort_scan(Signal * signal, BackupRecordPtr ptr)
{
  AbortBackupOrd *ord = (AbortBackupOrd*)signal->getDataPtrSend();
  ord->backupId = ptr.p->backupId;
  ord->backupPtr = ptr.i;
  ord->senderData= ptr.i;
  ord->requestType = AbortBackupOrd::AbortScan;

  TablePtr tabPtr;
  ptr.p->tables.first(tabPtr);
  for(; tabPtr.i != RNIL; ptr.p->tables.next(tabPtr)) {
    jam();
    FragmentPtr fragPtr;
    Array<Fragment> & frags = tabPtr.p->fragments;
    const Uint32 fragCount = frags.getSize();
    
    for(Uint32 i = 0; i<fragCount; i++) {
      jam();
      tabPtr.p->fragments.getPtr(fragPtr, i);
      const Uint32 nodeId = fragPtr.p->node;
      if(fragPtr.p->scanning != 0 && ptr.p->nodes.get(nodeId)) {
        jam();
	
	const BlockReference ref = numberToRef(BACKUP, nodeId);
	sendSignal(ref, GSN_ABORT_BACKUP_ORD, signal,
		   AbortBackupOrd::SignalLength, JBB);
	
      }
    }
  }
}

/*****************************************************************************
 * 
 * Slave functionallity: Define Backup 
 *
 *****************************************************************************/
void
Backup::defineBackupRef(Signal* signal, BackupRecordPtr ptr, Uint32 errCode)
{
  if(ptr.p->is_lcp()) 
  {
    jam();
    TablePtr tabPtr;
    FragmentPtr fragPtr;
    
    ptr.p->setErrorCode(errCode);
    ndbrequire(ptr.p->tables.first(tabPtr));
    tabPtr.p->fragments.getPtr(fragPtr, 0);
    
    LcpPrepareRef* ref= (LcpPrepareRef*)signal->getDataPtrSend();
    ref->senderData = ptr.p->clientData;
    ref->senderRef = reference();
    ref->tableId = tabPtr.p->tableId;
    ref->fragmentId = fragPtr.p->fragmentId;
    ref->errorCode = errCode;
    sendSignal(ptr.p->masterRef, GSN_LCP_PREPARE_REF, 
	       signal, LcpPrepareRef::SignalLength, JBB);
    return;
  }

  ptr.p->m_gsn = GSN_DEFINE_BACKUP_REF;
  ptr.p->setErrorCode(errCode);
  ndbrequire(ptr.p->errorCode != 0);
  
  DefineBackupRef* ref = (DefineBackupRef*)signal->getDataPtrSend();
  ref->backupId = ptr.p->backupId;
  ref->backupPtr = ptr.i;
  ref->errorCode = ptr.p->errorCode;
  ref->nodeId = getOwnNodeId();
  sendSignal(ptr.p->masterRef, GSN_DEFINE_BACKUP_REF, signal, 
	     DefineBackupRef::SignalLength, JBB);
}

void
Backup::execDEFINE_BACKUP_REQ(Signal* signal)
{
  jamEntry();

  DefineBackupReq* req = (DefineBackupReq*)signal->getDataPtr();
  
  BackupRecordPtr ptr;
  const Uint32 ptrI = req->backupPtr;
  const Uint32 backupId = req->backupId;
  const BlockReference senderRef = req->senderRef;

  if(senderRef == reference()){
    /**
     * Signal sent from myself -> record already seized
     */
    jam();
    c_backupPool.getPtr(ptr, ptrI);
  } else { // from other node
    jam();
#ifdef DEBUG_ABORT
    dumpUsedResources();
#endif
    if(!c_backups.seizeId(ptr, ptrI)) {
      jam();
      ndbrequire(false); // If master has succeeded slave should succed
    }//if
  }//if

  CRASH_INSERTION((10014));
  
  ptr.p->m_gsn = GSN_DEFINE_BACKUP_REQ;
  ptr.p->slaveState.forceState(INITIAL);
  ptr.p->slaveState.setState(DEFINING);
  ptr.p->slaveData.dropTrig.tableId = RNIL;
  ptr.p->errorCode = 0;
  ptr.p->clientRef = req->clientRef;
  ptr.p->clientData = req->clientData;
  if(senderRef == reference())
    ptr.p->flags = req->flags;
  else
    ptr.p->flags = req->flags & ~((Uint32)0x3); /* remove waitCompleted flags
						 * as non master should never
						 * reply
						 */
  ptr.p->masterRef = senderRef;
  ptr.p->nodes = req->nodes;
  ptr.p->backupId = backupId;
  ptr.p->backupKey[0] = req->backupKey[0];
  ptr.p->backupKey[1] = req->backupKey[1];
  ptr.p->backupDataLen = req->backupDataLen;
  ptr.p->masterData.errorCode = 0;
  ptr.p->noOfBytes = 0;
  ptr.p->noOfRecords = 0;
  ptr.p->noOfLogBytes = 0;
  ptr.p->noOfLogRecords = 0;
  ptr.p->currGCP = 0;
  ptr.p->startGCP = 0;
  ptr.p->stopGCP = 0;

  /**
   * Allocate files
   */
  BackupFilePtr files[3];
  Uint32 noOfPages[] = {
    NO_OF_PAGES_META_FILE,
    2,   // 32k
    0    // 3M
  };
  const Uint32 maxInsert[] = {
    2048,  // Temporarily to solve TR515
    4096,    // 4k
    16*3000, // Max 16 tuples
  };
  Uint32 minWrite[] = {
    8192,
    8192,
    32768
  };
  Uint32 maxWrite[] = {
    8192,
    8192,
    32768
  };
  
  minWrite[1] = c_defaults.m_minWriteSize;
  maxWrite[1] = c_defaults.m_maxWriteSize;
  noOfPages[1] = (c_defaults.m_logBufferSize + sizeof(Page32) - 1) / 
    sizeof(Page32);
  minWrite[2] = c_defaults.m_minWriteSize;
  maxWrite[2] = c_defaults.m_maxWriteSize;
  noOfPages[2] = (c_defaults.m_dataBufferSize + sizeof(Page32) - 1) / 
    sizeof(Page32);

  if (ptr.p->is_lcp())
  {
    noOfPages[2] = (c_defaults.m_lcp_buffer_size + sizeof(Page32) - 1) / 
      sizeof(Page32);
  }
  
  ptr.p->ctlFilePtr = ptr.p->logFilePtr = ptr.p->dataFilePtr = RNIL;

  for(Uint32 i = 0; i<3; i++) {
    jam();
    if(ptr.p->is_lcp() && i != 2)
    {
      files[i].i = RNIL;
      continue;
    }
    if(!ptr.p->files.seize(files[i])) {
      jam();
      defineBackupRef(signal, ptr, 
		      DefineBackupRef::FailedToAllocateFileRecord);
      return;
    }//if

    files[i].p->tableId = RNIL;
    files[i].p->backupPtr = ptr.i;
    files[i].p->filePointer = RNIL;
    files[i].p->fileClosing = 0;
    files[i].p->fileOpened = 0;
    files[i].p->fileRunning = 0;    
    files[i].p->scanRunning = 0;
    files[i].p->errorCode = 0;
    
    if(files[i].p->pages.seize(noOfPages[i]) == false) {
      jam();
      DEBUG_OUT("Failed to seize " << noOfPages[i] << " pages");
      defineBackupRef(signal, ptr, DefineBackupRef::FailedToAllocateBuffers);
      return;
    }//if
    Page32Ptr pagePtr;
    files[i].p->pages.getPtr(pagePtr, 0);
    
    const char * msg = files[i].p->
      operation.dataBuffer.setup((Uint32*)pagePtr.p, 
				 noOfPages[i] * (sizeof(Page32) >> 2),
				 128,
				 minWrite[i] >> 2,
				 maxWrite[i] >> 2,
				 maxInsert[i]);
    if(msg != 0) {
      jam();
      defineBackupRef(signal, ptr, DefineBackupRef::FailedToSetupFsBuffers);
      return;
    }//if

    switch(i){
    case 0:
      files[i].p->fileType = BackupFormat::CTL_FILE;
      ptr.p->ctlFilePtr = files[i].i;
      break;
    case 1:
      files[i].p->fileType = BackupFormat::LOG_FILE;
      ptr.p->logFilePtr = files[i].i;
      break;
    case 2:
      files[i].p->fileType = BackupFormat::DATA_FILE;
      ptr.p->dataFilePtr = files[i].i;
    }
  }//for
    
  if (!verifyNodesAlive(ptr, ptr.p->nodes)) {
    jam();
    defineBackupRef(signal, ptr, DefineBackupRef::Undefined);
    return;
  }//if
  if (ERROR_INSERTED(10027)) {
    jam();
    defineBackupRef(signal, ptr, 327);
    return;
  }//if

  if(ptr.p->backupDataLen == 0) {
    jam();
    backupAllData(signal, ptr);
    return;
  }//if
  
  if(ptr.p->is_lcp())
  {
    jam();
    getFragmentInfoDone(signal, ptr);
    return;
  }
  
  /**
   * Not implemented
   */
  ndbrequire(0);
}

void
Backup::backupAllData(Signal* signal, BackupRecordPtr ptr)
{
  /**
   * Get all tables from dict
   */
  ListTablesReq * req = (ListTablesReq*)signal->getDataPtrSend();
  req->senderRef = reference();
  req->senderData = ptr.i;
  req->requestData = 0;
  sendSignal(DBDICT_REF, GSN_LIST_TABLES_REQ, signal, 
	     ListTablesReq::SignalLength, JBB);
}

void
Backup::execLIST_TABLES_CONF(Signal* signal)
{
  jamEntry();
  
  ListTablesConf* conf = (ListTablesConf*)signal->getDataPtr();

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, conf->senderData);
  
  const Uint32 len = signal->length() - ListTablesConf::HeaderLength;
  for(unsigned int i = 0; i<len; i++) {
    jam();
    Uint32 tableId = ListTablesConf::getTableId(conf->tableData[i]);
    Uint32 tableType = ListTablesConf::getTableType(conf->tableData[i]);
    Uint32 state= ListTablesConf::getTableState(conf->tableData[i]);

    if (! (DictTabInfo::isTable(tableType) ||
	   DictTabInfo::isIndex(tableType) ||
	   DictTabInfo::isFilegroup(tableType) ||
	   DictTabInfo::isFile(tableType)))
    {
      jam();
      continue;
    }
    
    if (state != DictTabInfo::StateOnline)
    {
      jam();
      continue;
    }
    
    TablePtr tabPtr;
    ptr.p->tables.seize(tabPtr);
    if(tabPtr.i == RNIL) {
      jam();
      defineBackupRef(signal, ptr, DefineBackupRef::FailedToAllocateTables);
      return;
    }//if
    tabPtr.p->tableId = tableId;
    tabPtr.p->tableType = tableType;
  }//for
  
  if(len == ListTablesConf::DataLength) {
    jam();
    /**
     * Not finished...
     */
    return;
  }//if

  /**
   * All tables fetched
   */
  openFiles(signal, ptr);
}

void
Backup::openFiles(Signal* signal, BackupRecordPtr ptr)
{
  jam();

  BackupFilePtr filePtr;

  FsOpenReq * req = (FsOpenReq *)signal->getDataPtrSend();
  req->userReference = reference();
  req->fileFlags = 
    FsOpenReq::OM_WRITEONLY | 
    FsOpenReq::OM_TRUNCATE |
    FsOpenReq::OM_CREATE | 
    FsOpenReq::OM_APPEND;
  FsOpenReq::v2_setCount(req->fileNumber, 0xFFFFFFFF);
  
  /**
   * Ctl file
   */
  c_backupFilePool.getPtr(filePtr, ptr.p->ctlFilePtr);
  ndbrequire(filePtr.p->fileRunning == 0);
  filePtr.p->fileRunning = 1;

  req->userPointer = filePtr.i;
  FsOpenReq::setVersion(req->fileNumber, 2);
  FsOpenReq::setSuffix(req->fileNumber, FsOpenReq::S_CTL);
  FsOpenReq::v2_setSequence(req->fileNumber, ptr.p->backupId);
  FsOpenReq::v2_setNodeId(req->fileNumber, getOwnNodeId());
  sendSignal(NDBFS_REF, GSN_FSOPENREQ, signal, FsOpenReq::SignalLength, JBA);

  /**
   * Log file
   */
  c_backupFilePool.getPtr(filePtr, ptr.p->logFilePtr);
  ndbrequire(filePtr.p->fileRunning == 0);
  filePtr.p->fileRunning = 1;
  
  req->userPointer = filePtr.i;
  FsOpenReq::setVersion(req->fileNumber, 2);
  FsOpenReq::setSuffix(req->fileNumber, FsOpenReq::S_LOG);
  FsOpenReq::v2_setSequence(req->fileNumber, ptr.p->backupId);
  FsOpenReq::v2_setNodeId(req->fileNumber, getOwnNodeId());
  sendSignal(NDBFS_REF, GSN_FSOPENREQ, signal, FsOpenReq::SignalLength, JBA);

  /**
   * Data file
   */
  c_backupFilePool.getPtr(filePtr, ptr.p->dataFilePtr);
  ndbrequire(filePtr.p->fileRunning == 0);
  filePtr.p->fileRunning = 1;

  req->userPointer = filePtr.i;
  FsOpenReq::setVersion(req->fileNumber, 2);
  FsOpenReq::setSuffix(req->fileNumber, FsOpenReq::S_DATA);
  FsOpenReq::v2_setSequence(req->fileNumber, ptr.p->backupId);
  FsOpenReq::v2_setNodeId(req->fileNumber, getOwnNodeId());
  FsOpenReq::v2_setCount(req->fileNumber, 0);
  sendSignal(NDBFS_REF, GSN_FSOPENREQ, signal, FsOpenReq::SignalLength, JBA);
}

void
Backup::execFSOPENREF(Signal* signal)
{
  jamEntry();

  FsRef * ref = (FsRef *)signal->getDataPtr();
  
  const Uint32 userPtr = ref->userPointer;
  
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, userPtr);
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, filePtr.p->backupPtr);
  ptr.p->setErrorCode(ref->errorCode);
  openFilesReply(signal, ptr, filePtr);
}

void
Backup::execFSOPENCONF(Signal* signal)
{
  jamEntry();
  
  FsConf * conf = (FsConf *)signal->getDataPtr();
  
  const Uint32 userPtr = conf->userPointer;
  const Uint32 filePointer = conf->filePointer;
  
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, userPtr);
  filePtr.p->filePointer = filePointer; 
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, filePtr.p->backupPtr);

  ndbrequire(filePtr.p->fileOpened == 0);
  filePtr.p->fileOpened = 1;
  openFilesReply(signal, ptr, filePtr);
}

void
Backup::openFilesReply(Signal* signal, 
		       BackupRecordPtr ptr, BackupFilePtr filePtr)
{
  jam();

  /**
   * Mark files as "opened"
   */
  ndbrequire(filePtr.p->fileRunning == 1);
  filePtr.p->fileRunning = 0;
  
  /**
   * Check if all files have recived open_reply
   */
  for(ptr.p->files.first(filePtr); filePtr.i!=RNIL;ptr.p->files.next(filePtr)) 
  {
    jam();
    if(filePtr.p->fileRunning == 1) {
      jam();
      return;
    }//if
  }//for

  /**
   * Did open succeed for all files
   */
  if(ptr.p->checkError()) {
    jam();
    defineBackupRef(signal, ptr);
    return;
  }//if

  if(!ptr.p->is_lcp())
  {
    /**
     * Insert file headers
     */
    ptr.p->files.getPtr(filePtr, ptr.p->ctlFilePtr);
    if(!insertFileHeader(BackupFormat::CTL_FILE, ptr.p, filePtr.p)) {
      jam();
      defineBackupRef(signal, ptr, DefineBackupRef::FailedInsertFileHeader);
      return;
    }//if
    
    ptr.p->files.getPtr(filePtr, ptr.p->logFilePtr);
    if(!insertFileHeader(BackupFormat::LOG_FILE, ptr.p, filePtr.p)) {
      jam();
      defineBackupRef(signal, ptr, DefineBackupRef::FailedInsertFileHeader);
      return;
    }//if
    
    ptr.p->files.getPtr(filePtr, ptr.p->dataFilePtr);
    if(!insertFileHeader(BackupFormat::DATA_FILE, ptr.p, filePtr.p)) {
      jam();
      defineBackupRef(signal, ptr, DefineBackupRef::FailedInsertFileHeader);
      return;
    }//if
  }
  else
  {
    ptr.p->files.getPtr(filePtr, ptr.p->dataFilePtr);
    if(!insertFileHeader(BackupFormat::LCP_FILE, ptr.p, filePtr.p)) {
      jam();
      defineBackupRef(signal, ptr, DefineBackupRef::FailedInsertFileHeader);
      return;
    }//if
    
    ptr.p->ctlFilePtr = ptr.p->dataFilePtr;
  }
  
  /**
   * Start CTL file thread
   */
  ptr.p->files.getPtr(filePtr, ptr.p->ctlFilePtr);
  filePtr.p->fileRunning = 1;
  
  signal->theData[0] = BackupContinueB::START_FILE_THREAD;
  signal->theData[1] = filePtr.i;
  sendSignalWithDelay(BACKUP_REF, GSN_CONTINUEB, signal, 100, 2);

  /**
   * Insert table list in ctl file
   */
  FsBuffer & buf = filePtr.p->operation.dataBuffer;
  
  const Uint32 sz = 
    (sizeof(BackupFormat::CtlFile::TableList) >> 2) +
    ptr.p->tables.count() - 1;
  
  Uint32 * dst;
  ndbrequire(sz < buf.getMaxWrite());
  if(!buf.getWritePtr(&dst, sz)) {
    jam();
    defineBackupRef(signal, ptr, DefineBackupRef::FailedInsertTableList);
    return;
  }//if
  
  BackupFormat::CtlFile::TableList* tl = 
    (BackupFormat::CtlFile::TableList*)dst;
  tl->SectionType   = htonl(BackupFormat::TABLE_LIST);
  tl->SectionLength = htonl(sz);

  TablePtr tabPtr;
  Uint32 count = 0;
  for(ptr.p->tables.first(tabPtr); 
      tabPtr.i != RNIL;
      ptr.p->tables.next(tabPtr)){
    jam();
    tl->TableIds[count] = htonl(tabPtr.p->tableId);
    count++;
  }//for
  
  buf.updateWritePtr(sz);
  
  /**
   * Start getting table definition data
   */
  ndbrequire(ptr.p->tables.first(tabPtr));
  
  signal->theData[0] = BackupContinueB::BUFFER_FULL_META;
  signal->theData[1] = ptr.i;
  signal->theData[2] = tabPtr.i;
  sendSignalWithDelay(BACKUP_REF, GSN_CONTINUEB, signal, 100, 3);
  return;
}

bool
Backup::insertFileHeader(BackupFormat::FileType ft, 
			 BackupRecord * ptrP,
			 BackupFile * filePtrP){
  FsBuffer & buf = filePtrP->operation.dataBuffer;

  const Uint32 sz = sizeof(BackupFormat::FileHeader) >> 2;

  Uint32 * dst;
  ndbrequire(sz < buf.getMaxWrite());
  if(!buf.getWritePtr(&dst, sz)) {
    jam();
    return false;
  }//if
  
  BackupFormat::FileHeader* header = (BackupFormat::FileHeader*)dst;
  ndbrequire(sizeof(header->Magic) == sizeof(BACKUP_MAGIC));
  memcpy(header->Magic, BACKUP_MAGIC, sizeof(BACKUP_MAGIC));
  header->NdbVersion    = htonl(NDB_VERSION);
  header->SectionType   = htonl(BackupFormat::FILE_HEADER);
  header->SectionLength = htonl(sz - 3);
  header->FileType      = htonl(ft);
  header->BackupId      = htonl(ptrP->backupId);
  header->BackupKey_0   = htonl(ptrP->backupKey[0]);
  header->BackupKey_1   = htonl(ptrP->backupKey[1]);
  header->ByteOrder     = 0x12345678;
  
  buf.updateWritePtr(sz);
  return true;
}

void
Backup::execGET_TABINFOREF(Signal* signal)
{
  GetTabInfoRef * ref = (GetTabInfoRef*)signal->getDataPtr();
  
  const Uint32 senderData = ref->senderData;
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, senderData);

  defineBackupRef(signal, ptr, ref->errorCode);
}

void
Backup::execGET_TABINFO_CONF(Signal* signal)
{
  jamEntry();

  if(!assembleFragments(signal)) {
    jam();
    return;
  }//if

  GetTabInfoConf * const conf = (GetTabInfoConf*)signal->getDataPtr();
  //const Uint32 senderRef = info->senderRef;
  const Uint32 len = conf->totalLen;
  const Uint32 senderData = conf->senderData;
  const Uint32 tableType = conf->tableType;
  const Uint32 tableId = conf->tableId;

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, senderData);
  
  SegmentedSectionPtr dictTabInfoPtr;
  signal->getSection(dictTabInfoPtr, GetTabInfoConf::DICT_TAB_INFO);
  ndbrequire(dictTabInfoPtr.sz == len);

  TablePtr tabPtr ;
  ndbrequire(findTable(ptr, tabPtr, tableId));

  BackupFilePtr filePtr;
  ptr.p->files.getPtr(filePtr, ptr.p->ctlFilePtr);
  FsBuffer & buf = filePtr.p->operation.dataBuffer;
  Uint32* dst = 0;
  { // Write into ctl file
    Uint32 dstLen = len + 3;
    if(!buf.getWritePtr(&dst, dstLen)) {
      jam();
      ndbrequire(false);
      ptr.p->setErrorCode(DefineBackupRef::FailedAllocateTableMem);
      releaseSections(signal);
      defineBackupRef(signal, ptr);
      return;
    }//if
    if(dst != 0) {
      jam();

      BackupFormat::CtlFile::TableDescription * desc = 
        (BackupFormat::CtlFile::TableDescription*)dst;
      desc->SectionType = htonl(BackupFormat::TABLE_DESCRIPTION);
      desc->SectionLength = htonl(len + 3);
      desc->TableType = htonl(tableType);
      dst += 3;
      
      copy(dst, dictTabInfoPtr);
      buf.updateWritePtr(dstLen);
    }//if
  }

  releaseSections(signal);

  if(ptr.p->checkError()) {
    jam();
    defineBackupRef(signal, ptr);
    return;
  }//if

  if (!DictTabInfo::isTable(tabPtr.p->tableType))
  {
    jam();

    TablePtr tmp = tabPtr;
    ptr.p->tables.next(tabPtr);
    ptr.p->tables.release(tmp);
    goto next;
  }
  
  if (!parseTableDescription(signal, ptr, tabPtr, dst, len))
  {
    jam();
    defineBackupRef(signal, ptr);
    return;
  }
  
  if(!ptr.p->is_lcp())
  {
    jam();
    signal->theData[0] = tabPtr.p->tableId;
    signal->theData[1] = 1; // lock
    EXECUTE_DIRECT(DBDICT, GSN_BACKUP_FRAGMENT_REQ, signal, 2);
  }
  
  ptr.p->tables.next(tabPtr);
  
next:
  if(tabPtr.i == RNIL) 
  {
    /**
     * Done with all tables...
     */
    jam();
    
    if(ptr.p->is_lcp())
    {
      lcp_open_file_done(signal, ptr);
      return;
    }
    
    ndbrequire(ptr.p->tables.first(tabPtr));
    DihFragCountReq * const req = (DihFragCountReq*)signal->getDataPtrSend();
    req->m_connectionData = RNIL;
    req->m_tableRef = tabPtr.p->tableId;
    req->m_senderData = ptr.i;
    sendSignal(DBDIH_REF, GSN_DI_FCOUNTREQ, signal, 
               DihFragCountReq::SignalLength, JBB);
    return;
  }//if

  /**
   * Fetch next table...
   */
  signal->theData[0] = BackupContinueB::BUFFER_FULL_META;
  signal->theData[1] = ptr.i;
  signal->theData[2] = tabPtr.i;
  sendSignalWithDelay(BACKUP_REF, GSN_CONTINUEB, signal, 100, 3);
  return;
}

bool
Backup::parseTableDescription(Signal* signal, 
			      BackupRecordPtr ptr, 
			      TablePtr tabPtr, 
			      const Uint32 * tabdescptr,
			      Uint32 len)
{
  SimplePropertiesLinearReader it(tabdescptr, len);
  
  it.first();
  
  DictTabInfo::Table tmpTab; tmpTab.init();
  SimpleProperties::UnpackStatus stat;
  stat = SimpleProperties::unpack(it, &tmpTab, 
				  DictTabInfo::TableMapping, 
				  DictTabInfo::TableMappingSize, 
				  true, true);
  ndbrequire(stat == SimpleProperties::Break);

  bool lcp = ptr.p->is_lcp();

  ndbrequire(tabPtr.p->tableId == tmpTab.TableId);
  ndbrequire(lcp || (tabPtr.p->tableType == tmpTab.TableType));
  
  /**
   * LCP should not save disk attributes but only mem attributes
   */
  
  /**
   * Initialize table object
   */
  tabPtr.p->schemaVersion = tmpTab.TableVersion;
  tabPtr.p->noOfAttributes = tmpTab.NoOfAttributes;
  tabPtr.p->noOfNull = 0;
  tabPtr.p->noOfVariable = 0; // Computed while iterating over attribs
  tabPtr.p->sz_FixedAttributes = 0; // Computed while iterating over attribs
  tabPtr.p->triggerIds[0] = ILLEGAL_TRIGGER_ID;
  tabPtr.p->triggerIds[1] = ILLEGAL_TRIGGER_ID;
  tabPtr.p->triggerIds[2] = ILLEGAL_TRIGGER_ID;
  tabPtr.p->triggerAllocated[0] = false;
  tabPtr.p->triggerAllocated[1] = false;
  tabPtr.p->triggerAllocated[2] = false;

  Uint32 disk = 0;
  const Uint32 count = tabPtr.p->noOfAttributes;
  for(Uint32 i = 0; i<count; i++) {
    jam();
    DictTabInfo::Attribute tmp; tmp.init();
    stat = SimpleProperties::unpack(it, &tmp, 
				    DictTabInfo::AttributeMapping, 
				    DictTabInfo::AttributeMappingSize,
				    true, true);
    
    ndbrequire(stat == SimpleProperties::Break);
    it.next(); // Move Past EndOfAttribute

    const Uint32 arr = tmp.AttributeArraySize;
    const Uint32 sz = 1 << tmp.AttributeSize;
    const Uint32 sz32 = (sz * arr + 31) >> 5;

    if(lcp && tmp.AttributeStorageType == NDB_STORAGETYPE_DISK)
    {
      disk++;
      continue;
    }

    AttributePtr attrPtr;
    if(!tabPtr.p->attributes.seize(attrPtr))
    {
      jam();
      ptr.p->setErrorCode(DefineBackupRef::FailedToAllocateAttributeRecord);
      return false;
    }
    
    attrPtr.p->data.m_flags = 0;
    attrPtr.p->data.attrId = tmp.AttributeId;

    attrPtr.p->data.m_flags |= 
      (tmp.AttributeNullableFlag ? Attribute::COL_NULLABLE : 0);
    attrPtr.p->data.m_flags |= (tmp.AttributeArrayType == NDB_ARRAYTYPE_FIXED)?
      Attribute::COL_FIXED : 0;
    attrPtr.p->data.sz32 = sz32;
    
    /**
     * 1) Fixed non-nullable
     * 2) Other
     */
    if(attrPtr.p->data.m_flags & Attribute::COL_FIXED && 
       !(attrPtr.p->data.m_flags & Attribute::COL_NULLABLE)) {
      jam();
      attrPtr.p->data.offset = tabPtr.p->sz_FixedAttributes;
      tabPtr.p->sz_FixedAttributes += sz32;
    } else {
      attrPtr.p->data.offset = ~0;
      tabPtr.p->noOfVariable++;
    }
  }//for


  if(lcp)
  {
    if (disk)
    {
      /**
       * Remove all disk attributes, but add DISK_REF (8 bytes)
       */
      tabPtr.p->noOfAttributes -= (disk - 1);
      
      AttributePtr attrPtr;
      ndbrequire(tabPtr.p->attributes.seize(attrPtr));
      
      Uint32 sz32 = 2;
      attrPtr.p->data.attrId = AttributeHeader::DISK_REF;
      attrPtr.p->data.m_flags = Attribute::COL_FIXED;
      attrPtr.p->data.sz32 = 2;
      
      attrPtr.p->data.offset = tabPtr.p->sz_FixedAttributes;
      tabPtr.p->sz_FixedAttributes += sz32;
    }
   
    {
      AttributePtr attrPtr;
      ndbrequire(tabPtr.p->attributes.seize(attrPtr));
      
      Uint32 sz32 = 2;
      attrPtr.p->data.attrId = AttributeHeader::ROWID;
      attrPtr.p->data.m_flags = Attribute::COL_FIXED;
      attrPtr.p->data.sz32 = 2;
      
      attrPtr.p->data.offset = tabPtr.p->sz_FixedAttributes;
      tabPtr.p->sz_FixedAttributes += sz32;
      tabPtr.p->noOfAttributes ++;
    }

    if (tmpTab.RowGCIFlag)
    {
      AttributePtr attrPtr;
      ndbrequire(tabPtr.p->attributes.seize(attrPtr));
      
      Uint32 sz32 = 2;
      attrPtr.p->data.attrId = AttributeHeader::ROW_GCI;
      attrPtr.p->data.m_flags = Attribute::COL_FIXED;
      attrPtr.p->data.sz32 = 2;
      
      attrPtr.p->data.offset = tabPtr.p->sz_FixedAttributes;
      tabPtr.p->sz_FixedAttributes += sz32;
      tabPtr.p->noOfAttributes ++;
    }
  }
  return true;
}

void
Backup::execDI_FCOUNTCONF(Signal* signal)
{
  jamEntry();
  DihFragCountConf * const conf = (DihFragCountConf*)signal->getDataPtr();
  const Uint32 userPtr = conf->m_connectionData;
  const Uint32 fragCount = conf->m_fragmentCount;
  const Uint32 tableId = conf->m_tableRef;
  const Uint32 senderData = conf->m_senderData;

  ndbrequire(userPtr == RNIL && signal->length() == 5);
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, senderData);

  TablePtr tabPtr;
  ndbrequire(findTable(ptr, tabPtr, tableId));
  
  ndbrequire(tabPtr.p->fragments.seize(fragCount) != false);
  for(Uint32 i = 0; i<fragCount; i++) {
    jam();
    FragmentPtr fragPtr;
    tabPtr.p->fragments.getPtr(fragPtr, i);
    fragPtr.p->scanned = 0;
    fragPtr.p->scanning = 0;
    fragPtr.p->tableId = tableId;
    fragPtr.p->fragmentId = i;
    fragPtr.p->node = RNIL;
  }//for
  
  /**
   * Next table
   */
  if(ptr.p->tables.next(tabPtr)) {
    jam();
    DihFragCountReq * const req = (DihFragCountReq*)signal->getDataPtrSend();
    req->m_connectionData = RNIL;
    req->m_tableRef = tabPtr.p->tableId;
    req->m_senderData = ptr.i;
    sendSignal(DBDIH_REF, GSN_DI_FCOUNTREQ, signal, 
               DihFragCountReq::SignalLength, JBB);
    return;
  }//if
  
  ptr.p->tables.first(tabPtr);
  getFragmentInfo(signal, ptr, tabPtr, 0);
}

void
Backup::getFragmentInfo(Signal* signal, 
			BackupRecordPtr ptr, TablePtr tabPtr, Uint32 fragNo)
{
  jam();
  
  for(; tabPtr.i != RNIL; ptr.p->tables.next(tabPtr)) {
    jam();
    const Uint32 fragCount = tabPtr.p->fragments.getSize();
    for(; fragNo < fragCount; fragNo ++) {
      jam();
      FragmentPtr fragPtr;
      tabPtr.p->fragments.getPtr(fragPtr, fragNo);
      
      if(fragPtr.p->scanned == 0 && fragPtr.p->scanning == 0) {
	jam();
	signal->theData[0] = RNIL;
	signal->theData[1] = ptr.i;
	signal->theData[2] = tabPtr.p->tableId;
	signal->theData[3] = fragNo;
	sendSignal(DBDIH_REF, GSN_DIGETPRIMREQ, signal, 4, JBB);
	return;
      }//if
    }//for
    fragNo = 0;
  }//for
  
  getFragmentInfoDone(signal, ptr);
}

void
Backup::execDIGETPRIMCONF(Signal* signal)
{
  jamEntry();
  
  const Uint32 userPtr = signal->theData[0];
  const Uint32 senderData = signal->theData[1];
  const Uint32 nodeCount = signal->theData[6];
  const Uint32 tableId = signal->theData[7];
  const Uint32 fragNo = signal->theData[8];

  ndbrequire(userPtr == RNIL && signal->length() == 9);
  ndbrequire(nodeCount > 0 && nodeCount <= MAX_REPLICAS);
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, senderData);

  TablePtr tabPtr;
  ndbrequire(findTable(ptr, tabPtr, tableId));

  FragmentPtr fragPtr;
  tabPtr.p->fragments.getPtr(fragPtr, fragNo);
  
  fragPtr.p->node = signal->theData[2];

  getFragmentInfo(signal, ptr, tabPtr, fragNo + 1);
}

void
Backup::getFragmentInfoDone(Signal* signal, BackupRecordPtr ptr)
{
  ptr.p->m_gsn = GSN_DEFINE_BACKUP_CONF;
  ptr.p->slaveState.setState(DEFINED);
  DefineBackupConf * conf = (DefineBackupConf*)signal->getDataPtr();
  conf->backupPtr = ptr.i;
  conf->backupId = ptr.p->backupId;
  sendSignal(ptr.p->masterRef, GSN_DEFINE_BACKUP_CONF, signal,
	     DefineBackupConf::SignalLength, JBB);
}


/*****************************************************************************
 * 
 * Slave functionallity: Start backup
 *
 *****************************************************************************/
void
Backup::execSTART_BACKUP_REQ(Signal* signal)
{
  jamEntry();

  CRASH_INSERTION((10015));

  StartBackupReq* req = (StartBackupReq*)signal->getDataPtr();
  const Uint32 ptrI = req->backupPtr;

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  ptr.p->slaveState.setState(STARTED);
  ptr.p->m_gsn = GSN_START_BACKUP_REQ;

  /**
   * Start file threads...
   */
  BackupFilePtr filePtr;
  for(ptr.p->files.first(filePtr);
      filePtr.i!=RNIL;
      ptr.p->files.next(filePtr)){
    jam();
    if(filePtr.p->fileRunning == 0) {
      jam();
      filePtr.p->fileRunning = 1;
      signal->theData[0] = BackupContinueB::START_FILE_THREAD;
      signal->theData[1] = filePtr.i;
      sendSignalWithDelay(BACKUP_REF, GSN_CONTINUEB, signal, 100, 2);
    }//if
  }//for

  /**
   * Tell DBTUP to create triggers
   */
  TablePtr tabPtr;
  ndbrequire(ptr.p->tables.first(tabPtr));
  sendCreateTrig(signal, ptr, tabPtr);
}

/*****************************************************************************
 * 
 * Slave functionallity: Backup fragment
 *
 *****************************************************************************/
void
Backup::execBACKUP_FRAGMENT_REQ(Signal* signal)
{
  jamEntry();
  BackupFragmentReq* req = (BackupFragmentReq*)signal->getDataPtr();

  CRASH_INSERTION((10016));

  const Uint32 ptrI = req->backupPtr;
  //const Uint32 backupId = req->backupId;
  const Uint32 tableId = req->tableId;
  const Uint32 fragNo = req->fragmentNo;
  const Uint32 count = req->count;

  /**
   * Get backup record
   */
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  ptr.p->slaveState.setState(SCANNING);
  ptr.p->m_gsn = GSN_BACKUP_FRAGMENT_REQ;

  /**
   * Get file
   */
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, ptr.p->dataFilePtr);
  
  ndbrequire(filePtr.p->backupPtr == ptrI);
  ndbrequire(filePtr.p->fileOpened == 1);
  ndbrequire(filePtr.p->fileRunning == 1);
  ndbrequire(filePtr.p->scanRunning == 0);
  ndbrequire(filePtr.p->fileClosing == 0);
  
  /**
   * Get table
   */
  TablePtr tabPtr;
  ndbrequire(findTable(ptr, tabPtr, tableId));

  /**
   * Get fragment
   */
  FragmentPtr fragPtr;
  tabPtr.p->fragments.getPtr(fragPtr, fragNo);

  ndbrequire(fragPtr.p->scanned == 0);
  ndbrequire(fragPtr.p->scanning == 0 || 
	     refToNode(ptr.p->masterRef) == getOwnNodeId());

  /**
   * Init operation
   */
  if(filePtr.p->tableId != tableId) {
    jam();
    filePtr.p->operation.init(tabPtr);
    filePtr.p->tableId = tableId;
  }//if
  
  /**
   * Check for space in buffer
   */
  if(!filePtr.p->operation.newFragment(tableId, fragPtr.p->fragmentId)) {
    jam();
    req->count = count + 1;
    sendSignalWithDelay(BACKUP_REF, GSN_BACKUP_FRAGMENT_REQ, signal, 50,
			signal->length());
    ptr.p->slaveState.setState(STARTED);
    return;
  }//if
  
  /**
   * Mark things as "in use"
   */
  fragPtr.p->scanning = 1;
  filePtr.p->fragmentNo = fragPtr.p->fragmentId;
  
  /**
   * Start scan
   */
  {
    filePtr.p->scanRunning = 1;
    
    Table & table = * tabPtr.p;
    ScanFragReq * req = (ScanFragReq *)signal->getDataPtrSend();
    const Uint32 parallelism = 16;
    const Uint32 attrLen = 5 + table.noOfAttributes;

    req->senderData = filePtr.i;
    req->resultRef = reference();
    req->schemaVersion = table.schemaVersion;
    req->fragmentNoKeyLen = fragPtr.p->fragmentId;
    req->requestInfo = 0;
    req->savePointId = 0;
    req->tableId = table.tableId;
    ScanFragReq::setReadCommittedFlag(req->requestInfo, 1);
    ScanFragReq::setLockMode(req->requestInfo, 0);
    ScanFragReq::setHoldLockFlag(req->requestInfo, 0);
    ScanFragReq::setKeyinfoFlag(req->requestInfo, 0);
    ScanFragReq::setAttrLen(req->requestInfo,attrLen); 
    if (ptr.p->is_lcp())
    {
      ScanFragReq::setScanPrio(req->requestInfo, 1);
      ScanFragReq::setTupScanFlag(req->requestInfo, 1);
      ScanFragReq::setNoDiskFlag(req->requestInfo, 1);
    }
    req->transId1 = 0;
    req->transId2 = (BACKUP << 20) + (getOwnNodeId() << 8);
    req->clientOpPtr= filePtr.i;
    req->batch_size_rows= parallelism;
    req->batch_size_bytes= 0;
    sendSignal(DBLQH_REF, GSN_SCAN_FRAGREQ, signal,
               ScanFragReq::SignalLength, JBB);
    
    signal->theData[0] = filePtr.i;
    signal->theData[1] = 0;
    signal->theData[2] = (BACKUP << 20) + (getOwnNodeId() << 8);
    
    // Return all
    signal->theData[3] = table.noOfAttributes;
    signal->theData[4] = 0;
    signal->theData[5] = 0;
    signal->theData[6] = 0;
    signal->theData[7] = 0;
    
    Uint32 dataPos = 8;
    Ptr<Attribute> attrPtr;
    table.attributes.first(attrPtr);
    for(; !attrPtr.isNull(); table.attributes.next(attrPtr))
    {
      jam();
      
      /**
       * LCP should not save disk attributes
       */
      ndbrequire(! (ptr.p->is_lcp() && 
		    attrPtr.p->data.m_flags & Attribute::COL_DISK));
      
      AttributeHeader::init(&signal->theData[dataPos], 
			    attrPtr.p->data.attrId, 0);
      dataPos++;
      if(dataPos == 25) {
        jam();
	sendSignal(DBLQH_REF, GSN_ATTRINFO, signal, 25, JBB);
	dataPos = 3;
      }//if
    }//for
    if(dataPos != 3) {
      jam();
      sendSignal(DBLQH_REF, GSN_ATTRINFO, signal, dataPos, JBB);
    }//if
  }
}

void
Backup::execSCAN_HBREP(Signal* signal)
{
  jamEntry();
}

void
Backup::execTRANSID_AI(Signal* signal)
{
  jamEntry();

  const Uint32 filePtrI = signal->theData[0];
  //const Uint32 transId1 = signal->theData[1];
  //const Uint32 transId2 = signal->theData[2];
  const Uint32 dataLen  = signal->length() - 3;
  
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, filePtrI);

  OperationRecord & op = filePtr.p->operation;
  
  TablePtr tabPtr;
  c_tablePool.getPtr(tabPtr, op.tablePtr);
  
  Table & table = * tabPtr.p;
  
  /**
   * Unpack data
   */
  op.attrSzTotal += dataLen;

  Uint32 srcSz = dataLen;
  Uint32 usedSz = 0;
  const Uint32 * src = &signal->theData[3];

  Ptr<Attribute> attrPtr;
  table.attributes.first(attrPtr);
  Uint32 columnNo = 0;
  
  while (usedSz < srcSz) 
  {
    jam();
    
    /**
     * Finished with one attribute now find next
     */
    const AttributeHeader attrHead(* src);
    const Uint32 attrId = attrHead.getAttributeId();
    const bool null = attrHead.isNULL();
    const Attribute::Data attr = attrPtr.p->data;
    ndbrequire(attrId == attr.attrId);
    
    usedSz += attrHead.getHeaderSize();
    src    += attrHead.getHeaderSize();
      
    if (null) {
      jam();
      ndbrequire(attr.m_flags & Attribute::COL_NULLABLE);
      op.nullVariable();
    } else {
      Uint32* dst;
      Uint32 dstSz = attrHead.getDataSize();
      if (attr.m_flags & Attribute::COL_FIXED && 
         ! (attr.m_flags & Attribute::COL_NULLABLE)) {
        jam();
        dst = op.newAttrib(attr.offset, dstSz);
        ndbrequire(dstSz == attr.sz32);
      } else {
        dst = op.newVariable(columnNo, attrHead.getByteSize());
        ndbrequire(dstSz <= attr.sz32);
      }
      
      memcpy(dst, src, (dstSz << 2));
      src    += dstSz;
      usedSz += dstSz;
    }
    table.attributes.next(attrPtr);
    columnNo++;
  }
  
  ndbrequire(usedSz == srcSz);
  ndbrequire(op.finished());
  op.newRecord(op.dst);
}

void 
Backup::OperationRecord::init(const TablePtr & ptr)
{
  
  tablePtr = ptr.i;
  noOfAttributes = ptr.p->noOfAttributes;
  
  sz_Bitmask = (ptr.p->noOfNull + 31) >> 5;
  sz_FixedAttribs = ptr.p->sz_FixedAttributes;

  if(ptr.p->noOfVariable == 0) {
    jam();
    maxRecordSize = 1 + sz_Bitmask + sz_FixedAttribs;
  } else {
    jam();
    maxRecordSize = 
      1 + sz_Bitmask + 2048 /* Max tuple size */ + 2 * ptr.p->noOfVariable;
  }//if
}

bool
Backup::OperationRecord::newFragment(Uint32 tableId, Uint32 fragNo)
{
  Uint32 * tmp;
  const Uint32 headSz = (sizeof(BackupFormat::DataFile::FragmentHeader) >> 2);
  const Uint32 sz = headSz + 16 * maxRecordSize;
  
  ndbrequire(sz < dataBuffer.getMaxWrite());
  if(dataBuffer.getWritePtr(&tmp, sz)) {
    jam();
    BackupFormat::DataFile::FragmentHeader * head = 
      (BackupFormat::DataFile::FragmentHeader*)tmp;

    head->SectionType   = htonl(BackupFormat::FRAGMENT_HEADER);
    head->SectionLength = htonl(headSz);
    head->TableId       = htonl(tableId);
    head->FragmentNo    = htonl(fragNo);
    head->ChecksumType  = htonl(0);

    opNoDone = opNoConf = opLen = 0;
    newRecord(tmp + headSz);
    scanStart = tmp;
    scanStop  = (tmp + headSz);
    
    noOfRecords = 0;
    noOfBytes = 0;
    return true;
  }//if
  return false;
}

bool
Backup::OperationRecord::fragComplete(Uint32 tableId, Uint32 fragNo)
{
  Uint32 * tmp;
  const Uint32 footSz = sizeof(BackupFormat::DataFile::FragmentFooter) >> 2;

  if(dataBuffer.getWritePtr(&tmp, footSz + 1)) {
    jam();
    * tmp = 0; // Finish record stream
    tmp++;
    BackupFormat::DataFile::FragmentFooter * foot = 
      (BackupFormat::DataFile::FragmentFooter*)tmp;
    foot->SectionType   = htonl(BackupFormat::FRAGMENT_FOOTER);
    foot->SectionLength = htonl(footSz);
    foot->TableId       = htonl(tableId);
    foot->FragmentNo    = htonl(fragNo);
    foot->NoOfRecords   = htonl(noOfRecords);
    foot->Checksum      = htonl(0);
    dataBuffer.updateWritePtr(footSz + 1);
    return true;
  }//if
  return false;
}

bool
Backup::OperationRecord::newScan()
{
  Uint32 * tmp;
  ndbrequire(16 * maxRecordSize < dataBuffer.getMaxWrite());
  if(dataBuffer.getWritePtr(&tmp, 16 * maxRecordSize)) {
    jam();
    opNoDone = opNoConf = opLen = 0;
    newRecord(tmp);
    scanStart = tmp;
    scanStop = tmp;
    return true;
  }//if
  return false;
}

bool
Backup::OperationRecord::closeScan()
{
  opNoDone = opNoConf = opLen = 0;
  return true;
}

bool 
Backup::OperationRecord::scanConf(Uint32 noOfOps, Uint32 total_len)
{
  const Uint32 done = opNoDone-opNoConf;
  
  ndbrequire(noOfOps == done);
  ndbrequire(opLen == total_len);
  opNoConf = opNoDone;
  
  const Uint32 len = (scanStop - scanStart);
  ndbrequire(len < dataBuffer.getMaxWrite());
  dataBuffer.updateWritePtr(len);
  noOfBytes += (len << 2);
  return true;
}

void
Backup::execSCAN_FRAGREF(Signal* signal)
{
  jamEntry();

  ScanFragRef * ref = (ScanFragRef*)signal->getDataPtr();
  
  const Uint32 filePtrI = ref->senderData;
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, filePtrI);
  
  filePtr.p->errorCode = ref->errorCode;
  filePtr.p->scanRunning = 0;
  
  backupFragmentRef(signal, filePtr);
}

void
Backup::execSCAN_FRAGCONF(Signal* signal)
{
  jamEntry();

  CRASH_INSERTION((10017));

  ScanFragConf * conf = (ScanFragConf*)signal->getDataPtr();
  
  const Uint32 filePtrI = conf->senderData;
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, filePtrI);

  OperationRecord & op = filePtr.p->operation;
  
  op.scanConf(conf->completedOps, conf->total_len);
  const Uint32 completed = conf->fragmentCompleted;
  if(completed != 2) {
    jam();
    
    checkScan(signal, filePtr);
    return;
  }//if

  fragmentCompleted(signal, filePtr);
}

void
Backup::fragmentCompleted(Signal* signal, BackupFilePtr filePtr)
{
  jam();

  if(filePtr.p->errorCode != 0)
  {
    jam();    
    filePtr.p->scanRunning = 0;
    backupFragmentRef(signal, filePtr); // Scan completed
    return;
  }//if
    
  OperationRecord & op = filePtr.p->operation;
  if(!op.fragComplete(filePtr.p->tableId, filePtr.p->fragmentNo)) {
    jam();
    signal->theData[0] = BackupContinueB::BUFFER_FULL_FRAG_COMPLETE;
    signal->theData[1] = filePtr.i;
    sendSignalWithDelay(BACKUP_REF, GSN_CONTINUEB, signal, 50, 2);
    return;
  }//if
  
  filePtr.p->scanRunning = 0;
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, filePtr.p->backupPtr);

  BackupFragmentConf * conf = (BackupFragmentConf*)signal->getDataPtrSend();
  conf->backupId = ptr.p->backupId;
  conf->backupPtr = ptr.i;
  conf->tableId = filePtr.p->tableId;
  conf->fragmentNo = filePtr.p->fragmentNo;
  conf->noOfRecords = op.noOfRecords;
  conf->noOfBytes = op.noOfBytes;

  sendSignal(ptr.p->masterRef, GSN_BACKUP_FRAGMENT_CONF, signal,
	     BackupFragmentConf::SignalLength, JBB);
  
  ptr.p->m_gsn = GSN_BACKUP_FRAGMENT_CONF;
  ptr.p->slaveState.setState(STARTED);
  return;
}

void
Backup::backupFragmentRef(Signal * signal, BackupFilePtr filePtr)
{
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, filePtr.p->backupPtr);

  ptr.p->m_gsn = GSN_BACKUP_FRAGMENT_REF;
  
  BackupFragmentRef * ref = (BackupFragmentRef*)signal->getDataPtrSend();
  ref->backupId = ptr.p->backupId;
  ref->backupPtr = ptr.i;
  ref->nodeId = getOwnNodeId();
  ref->errorCode = filePtr.p->errorCode;
  sendSignal(ptr.p->masterRef, GSN_BACKUP_FRAGMENT_REF, signal,
	     BackupFragmentRef::SignalLength, JBB);
}
 
void
Backup::checkScan(Signal* signal, BackupFilePtr filePtr)
{  
  OperationRecord & op = filePtr.p->operation;

  if(filePtr.p->errorCode != 0)
  {
    jam();

    /**
     * Close scan
     */
    op.closeScan();
    ScanFragNextReq * req = (ScanFragNextReq *)signal->getDataPtrSend();
    req->senderData = filePtr.i;
    req->closeFlag = 1;
    req->transId1 = 0;
    req->transId2 = (BACKUP << 20) + (getOwnNodeId() << 8);
    sendSignal(DBLQH_REF, GSN_SCAN_NEXTREQ, signal, 
	       ScanFragNextReq::SignalLength, JBB);
    return;
  }//if
  
  if(op.newScan()) {
    jam();
    
    ScanFragNextReq * req = (ScanFragNextReq *)signal->getDataPtrSend();
    req->senderData = filePtr.i;
    req->closeFlag = 0;
    req->transId1 = 0;
    req->transId2 = (BACKUP << 20) + (getOwnNodeId() << 8);
    req->batch_size_rows= 16;
    req->batch_size_bytes= 0;
    if(ERROR_INSERTED(10032))
      sendSignalWithDelay(DBLQH_REF, GSN_SCAN_NEXTREQ, signal, 
			  100, ScanFragNextReq::SignalLength);
    else if(ERROR_INSERTED(10033))
    {
      SET_ERROR_INSERT_VALUE(10032);
      sendSignalWithDelay(DBLQH_REF, GSN_SCAN_NEXTREQ, signal, 
			  10000, ScanFragNextReq::SignalLength);
      
      BackupRecordPtr ptr;
      c_backupPool.getPtr(ptr, filePtr.p->backupPtr);
      AbortBackupOrd *ord = (AbortBackupOrd*)signal->getDataPtrSend();
      ord->backupId = ptr.p->backupId;
      ord->backupPtr = ptr.i;
      ord->requestType = AbortBackupOrd::FileOrScanError;
      ord->senderData= ptr.i;
      sendSignal(ptr.p->masterRef, GSN_ABORT_BACKUP_ORD, signal, 
		 AbortBackupOrd::SignalLength, JBB);
    }
    else
      sendSignal(DBLQH_REF, GSN_SCAN_NEXTREQ, signal, 
		 ScanFragNextReq::SignalLength, JBB);
    return;
  }//if
  
  signal->theData[0] = BackupContinueB::BUFFER_FULL_SCAN;
  signal->theData[1] = filePtr.i;
  sendSignalWithDelay(BACKUP_REF, GSN_CONTINUEB, signal, 50, 2);
}

void
Backup::execFSAPPENDREF(Signal* signal)
{
  jamEntry();
  
  FsRef * ref = (FsRef *)signal->getDataPtr();

  const Uint32 filePtrI = ref->userPointer;
  const Uint32 errCode = ref->errorCode;
  
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, filePtrI);

  filePtr.p->fileRunning = 0;  
  filePtr.p->errorCode = errCode;

  checkFile(signal, filePtr);
}

void
Backup::execFSAPPENDCONF(Signal* signal)
{
  jamEntry();

  CRASH_INSERTION((10018));

  //FsConf * conf = (FsConf*)signal->getDataPtr();
  const Uint32 filePtrI = signal->theData[0]; //conf->userPointer;
  const Uint32 bytes = signal->theData[1]; //conf->bytes;
  
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, filePtrI);
  
  OperationRecord & op = filePtr.p->operation;
  
  op.dataBuffer.updateReadPtr(bytes >> 2);

  checkFile(signal, filePtr);
}

void
Backup::checkFile(Signal* signal, BackupFilePtr filePtr)
{

#ifdef DEBUG_ABORT
  //  ndbout_c("---- check file filePtr.i = %u", filePtr.i);
#endif

  OperationRecord & op = filePtr.p->operation;
  
  Uint32 * tmp, sz; bool eof;
  if(op.dataBuffer.getReadPtr(&tmp, &sz, &eof)) 
  {
    jam();
    
    jam();
    FsAppendReq * req = (FsAppendReq *)signal->getDataPtrSend();
    req->filePointer   = filePtr.p->filePointer;
    req->userPointer   = filePtr.i;
    req->userReference = reference();
    req->varIndex      = 0;
    req->offset        = tmp - c_startOfPages;
    req->size          = sz;
    
    sendSignal(NDBFS_REF, GSN_FSAPPENDREQ, signal, 
	       FsAppendReq::SignalLength, JBA);
    return;
  }
  
  if(!eof) {
    jam();
    signal->theData[0] = BackupContinueB::BUFFER_UNDERFLOW;
    signal->theData[1] = filePtr.i;
    sendSignalWithDelay(BACKUP_REF, GSN_CONTINUEB, signal, 50, 2);
    return;
  }//if
  
  if(sz > 0) {
    jam();
    FsAppendReq * req = (FsAppendReq *)signal->getDataPtrSend();
    req->filePointer   = filePtr.p->filePointer;
    req->userPointer   = filePtr.i;
    req->userReference = reference();
    req->varIndex      = 0;
    req->offset        = tmp - c_startOfPages;
    req->size          = sz; // Round up
    
    sendSignal(NDBFS_REF, GSN_FSAPPENDREQ, signal, 
	       FsAppendReq::SignalLength, JBA);
    return;
  }//if
  
  filePtr.p->fileRunning = 0;
  filePtr.p->fileClosing = 1;
  
  FsCloseReq * req = (FsCloseReq *)signal->getDataPtrSend();
  req->filePointer = filePtr.p->filePointer;
  req->userPointer = filePtr.i;
  req->userReference = reference();
  req->fileFlag = 0;
#ifdef DEBUG_ABORT
  ndbout_c("***** a FSCLOSEREQ filePtr.i = %u", filePtr.i);
#endif
  sendSignal(NDBFS_REF, GSN_FSCLOSEREQ, signal, FsCloseReq::SignalLength, JBA);
}


/****************************************************************************
 * 
 * Slave functionallity: Perform logging
 *
 ****************************************************************************/
void
Backup::execBACKUP_TRIG_REQ(Signal* signal)
{
  /*
  TUP asks if this trigger is to be fired on this node.
  */
  TriggerPtr trigPtr;
  TablePtr tabPtr;
  FragmentPtr fragPtr;
  Uint32 trigger_id = signal->theData[0];
  Uint32 frag_id = signal->theData[1];
  Uint32 result;

  jamEntry();

  c_triggerPool.getPtr(trigPtr, trigger_id);

  c_tablePool.getPtr(tabPtr, trigPtr.p->tab_ptr_i);
  tabPtr.p->fragments.getPtr(fragPtr, frag_id);
  if (fragPtr.p->node != getOwnNodeId()) {

    jam();
    result = ZFALSE;
  } else {
    jam();
    result = ZTRUE;
  }//if
  signal->theData[0] = result;
}

void
Backup::execTRIG_ATTRINFO(Signal* signal) {
  jamEntry();

  CRASH_INSERTION((10019));

  TrigAttrInfo * trg = (TrigAttrInfo*)signal->getDataPtr();

  TriggerPtr trigPtr;
  c_triggerPool.getPtr(trigPtr, trg->getTriggerId());
  ndbrequire(trigPtr.p->event != ILLEGAL_TRIGGER_ID); // Online...

  if(trigPtr.p->errorCode != 0) {
    jam();
    return;
  }//if

  if(trg->getAttrInfoType() == TrigAttrInfo::BEFORE_VALUES) {
    jam();
    /**
     * Backup is doing REDO logging and don't need before values
     */
    return;
  }//if

  BackupFormat::LogFile::LogEntry * logEntry = trigPtr.p->logEntry;
  if(logEntry == 0) 
  {
    jam();
    Uint32 * dst;
    FsBuffer & buf = trigPtr.p->operation->dataBuffer;
    ndbrequire(trigPtr.p->maxRecordSize <= buf.getMaxWrite());

    if(ERROR_INSERTED(10030) ||
       !buf.getWritePtr(&dst, trigPtr.p->maxRecordSize)) 
    {
      jam();
      Uint32 save[TrigAttrInfo::StaticLength];
      memcpy(save, signal->getDataPtr(), 4*TrigAttrInfo::StaticLength);
      BackupRecordPtr ptr;
      c_backupPool.getPtr(ptr, trigPtr.p->backupPtr);
      trigPtr.p->errorCode = AbortBackupOrd::LogBufferFull;
      AbortBackupOrd *ord = (AbortBackupOrd*)signal->getDataPtrSend();
      ord->backupId = ptr.p->backupId;
      ord->backupPtr = ptr.i;
      ord->requestType = AbortBackupOrd::LogBufferFull;
      ord->senderData= ptr.i;
      sendSignal(ptr.p->masterRef, GSN_ABORT_BACKUP_ORD, signal, 
		 AbortBackupOrd::SignalLength, JBB);

      memcpy(signal->getDataPtrSend(), save, 4*TrigAttrInfo::StaticLength);
      return;
    }//if

    logEntry = (BackupFormat::LogFile::LogEntry *)dst;
    trigPtr.p->logEntry = logEntry;
    logEntry->Length       = 0;
    logEntry->TableId      = htonl(trigPtr.p->tableId);


    if(trigPtr.p->event==0)
      logEntry->TriggerEvent= htonl(TriggerEvent::TE_INSERT);
    else if(trigPtr.p->event==1)
      logEntry->TriggerEvent= htonl(TriggerEvent::TE_UPDATE);
    else if(trigPtr.p->event==2)
      logEntry->TriggerEvent= htonl(TriggerEvent::TE_DELETE);
    else {
      ndbout << "Bad Event: " << trigPtr.p->event << endl;
      ndbrequire(false);
    }
  } else {
    ndbrequire(logEntry->TableId == htonl(trigPtr.p->tableId));
//    ndbrequire(logEntry->TriggerEvent == htonl(trigPtr.p->event));
  }//if

  const Uint32 pos = logEntry->Length;
  const Uint32 dataLen = signal->length() - TrigAttrInfo::StaticLength;
  memcpy(&logEntry->Data[pos], trg->getData(), dataLen << 2);

  logEntry->Length = pos + dataLen;
}

void
Backup::execFIRE_TRIG_ORD(Signal* signal)
{
  jamEntry();
  FireTrigOrd* trg = (FireTrigOrd*)signal->getDataPtr();

  const Uint32 gci = trg->getGCI();
  const Uint32 trI = trg->getTriggerId();
  const Uint32 fragId = trg->fragId;

  TriggerPtr trigPtr;
  c_triggerPool.getPtr(trigPtr, trI);
  
  ndbrequire(trigPtr.p->event != ILLEGAL_TRIGGER_ID);

  if(trigPtr.p->errorCode != 0) {
    jam();
    return;
  }//if

  ndbrequire(trigPtr.p->logEntry != 0);
  Uint32 len = trigPtr.p->logEntry->Length;
  trigPtr.p->logEntry->FragId = htonl(fragId);

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, trigPtr.p->backupPtr);
  if(gci != ptr.p->currGCP)
  {
    jam();
    trigPtr.p->logEntry->TriggerEvent|= htonl(0x10000);
    trigPtr.p->logEntry->Data[len] = htonl(gci);
    len++;
    ptr.p->currGCP = gci;
  }

  len += (sizeof(BackupFormat::LogFile::LogEntry) >> 2) - 2;
  trigPtr.p->logEntry->Length = htonl(len);

  ndbrequire(len + 1 <= trigPtr.p->operation->dataBuffer.getMaxWrite());
  trigPtr.p->operation->dataBuffer.updateWritePtr(len + 1);
  trigPtr.p->logEntry = 0;
  
  trigPtr.p->operation->noOfBytes += (len + 1) << 2;
  trigPtr.p->operation->noOfRecords += 1;
}

void
Backup::sendAbortBackupOrd(Signal* signal, BackupRecordPtr ptr, 
			   Uint32 requestType)
{
  jam();
  AbortBackupOrd *ord = (AbortBackupOrd*)signal->getDataPtrSend();
  ord->backupId = ptr.p->backupId;
  ord->backupPtr = ptr.i;
  ord->requestType = requestType;
  ord->senderData= ptr.i;
  NodePtr node;
  for(c_nodes.first(node); node.i != RNIL; c_nodes.next(node)) {
    jam();
    const Uint32 nodeId = node.p->nodeId;
    if(node.p->alive && ptr.p->nodes.get(nodeId)) {
      jam();
      sendSignal(numberToRef(BACKUP, nodeId), GSN_ABORT_BACKUP_ORD, signal, 
		 AbortBackupOrd::SignalLength, JBB);
    }//if
  }//for
}

/*****************************************************************************
 * 
 * Slave functionallity: Stop backup
 *
 *****************************************************************************/
void
Backup::execSTOP_BACKUP_REQ(Signal* signal)
{
  jamEntry();
  StopBackupReq * req = (StopBackupReq*)signal->getDataPtr();
  
  CRASH_INSERTION((10020));

  const Uint32 ptrI = req->backupPtr;
  //const Uint32 backupId = req->backupId;
  const Uint32 startGCP = req->startGCP;
  const Uint32 stopGCP = req->stopGCP;

  /**
   * At least one GCP must have passed
   */
  ndbrequire(stopGCP > startGCP);

  /**
   * Get backup record
   */
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);

  ptr.p->slaveState.setState(STOPPING);
  ptr.p->m_gsn = GSN_STOP_BACKUP_REQ;
  ptr.p->startGCP= startGCP;
  ptr.p->stopGCP= stopGCP;

  /**
   * Destroy the triggers in local DBTUP we created
   */
  sendDropTrig(signal, ptr);
}

void
Backup::closeFiles(Signal* sig, BackupRecordPtr ptr)
{
  /**
   * Close all files
   */
  BackupFilePtr filePtr;
  int openCount = 0;
  for(ptr.p->files.first(filePtr); filePtr.i!=RNIL; ptr.p->files.next(filePtr))
  {
    if(filePtr.p->fileOpened == 0) {
      jam();
      continue;
    }
    
    jam();
    openCount++;
    
    if(filePtr.p->fileClosing == 1){
      jam();
      continue;
    }//if
    
    filePtr.p->fileClosing = 1;
    
    if(filePtr.p->fileRunning == 1){
      jam();
#ifdef DEBUG_ABORT
      ndbout_c("Close files fileRunning == 1, filePtr.i=%u", filePtr.i);
#endif
      filePtr.p->operation.dataBuffer.eof();
    } else {
      jam();
      
      FsCloseReq * req = (FsCloseReq *)sig->getDataPtrSend();
      req->filePointer = filePtr.p->filePointer;
      req->userPointer = filePtr.i;
      req->userReference = reference();
      req->fileFlag = 0;
#ifdef DEBUG_ABORT
      ndbout_c("***** b FSCLOSEREQ filePtr.i = %u", filePtr.i);
#endif
      sendSignal(NDBFS_REF, GSN_FSCLOSEREQ, sig, 
		 FsCloseReq::SignalLength, JBA);
    }//if
  }//for
  
  if(openCount == 0){
    jam();
    closeFilesDone(sig, ptr);
  }//if
}

void
Backup::execFSCLOSEREF(Signal* signal)
{
  jamEntry();
  
  FsRef * ref = (FsRef*)signal->getDataPtr();
  const Uint32 filePtrI = ref->userPointer;
  
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, filePtrI);

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, filePtr.p->backupPtr);
  
  filePtr.p->fileOpened = 1;
  FsConf * conf = (FsConf*)signal->getDataPtr();
  conf->userPointer = filePtrI;
  
  execFSCLOSECONF(signal);
}

void
Backup::execFSCLOSECONF(Signal* signal)
{
  jamEntry();

  FsConf * conf = (FsConf*)signal->getDataPtr();
  const Uint32 filePtrI = conf->userPointer;
  
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, filePtrI);

#ifdef DEBUG_ABORT
  ndbout_c("***** FSCLOSECONF filePtrI = %u", filePtrI);
#endif

  ndbrequire(filePtr.p->fileClosing == 1);
  ndbrequire(filePtr.p->fileOpened == 1);
  ndbrequire(filePtr.p->fileRunning == 0);
  ndbrequire(filePtr.p->scanRunning == 0);	     
  
  filePtr.p->fileOpened = 0;
  filePtr.p->operation.dataBuffer.reset();
  
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, filePtr.p->backupPtr);
  for(ptr.p->files.first(filePtr); filePtr.i!=RNIL;ptr.p->files.next(filePtr)) 
  {
    jam();
    if(filePtr.p->fileOpened == 1) {
      jam();
#ifdef DEBUG_ABORT
      ndbout_c("waiting for more FSCLOSECONF's filePtr.i = %u", filePtr.i);
#endif
      return; // we will be getting more FSCLOSECONF's
    }//if
  }//for
  closeFilesDone(signal, ptr);
}

void
Backup::closeFilesDone(Signal* signal, BackupRecordPtr ptr)
{
  jam();

  if(ptr.p->is_lcp())
  {
    lcp_close_file_conf(signal, ptr);
    return;
  }
  
  jam();
  BackupFilePtr filePtr;
  ptr.p->files.getPtr(filePtr, ptr.p->logFilePtr);
  
  StopBackupConf* conf = (StopBackupConf*)signal->getDataPtrSend();
  conf->backupId = ptr.p->backupId;
  conf->backupPtr = ptr.i;
  conf->noOfLogBytes = filePtr.p->operation.noOfBytes;
  conf->noOfLogRecords = filePtr.p->operation.noOfRecords;
  sendSignal(ptr.p->masterRef, GSN_STOP_BACKUP_CONF, signal,
	     StopBackupConf::SignalLength, JBB);
  
  ptr.p->m_gsn = GSN_STOP_BACKUP_CONF;
  ptr.p->slaveState.setState(CLEANING);
}

/*****************************************************************************
 * 
 * Slave functionallity: Abort backup
 *
 *****************************************************************************/
/*****************************************************************************
 * 
 * Slave functionallity: Abort backup
 *
 *****************************************************************************/
void
Backup::execABORT_BACKUP_ORD(Signal* signal)
{
  jamEntry();
  AbortBackupOrd* ord = (AbortBackupOrd*)signal->getDataPtr();

  const Uint32 backupId = ord->backupId;
  const AbortBackupOrd::RequestType requestType = 
    (AbortBackupOrd::RequestType)ord->requestType;
  const Uint32 senderData = ord->senderData;
  
#ifdef DEBUG_ABORT
  ndbout_c("******** ABORT_BACKUP_ORD ********* nodeId = %u", 
	   refToNode(signal->getSendersBlockRef()));
  ndbout_c("backupId = %u, requestType = %u, senderData = %u, ",
	   backupId, requestType, senderData);
  dumpUsedResources();
#endif

  BackupRecordPtr ptr;
  if(requestType == AbortBackupOrd::ClientAbort) {
    if (getOwnNodeId() != getMasterNodeId()) {
      jam();
      // forward to master
#ifdef DEBUG_ABORT
      ndbout_c("---- Forward to master nodeId = %u", getMasterNodeId());
#endif
      sendSignal(calcBackupBlockRef(getMasterNodeId()), GSN_ABORT_BACKUP_ORD, 
		 signal, AbortBackupOrd::SignalLength, JBB);
      return;
    }
    jam();
    for(c_backups.first(ptr); ptr.i != RNIL; c_backups.next(ptr)) {
      jam();
      if(ptr.p->backupId == backupId && ptr.p->clientData == senderData) {
        jam();
	break;
      }//if
    }//for
    if(ptr.i == RNIL) {
      jam();
      return;
    }//if
  } else {
    if (c_backupPool.findId(senderData)) {
      jam();
      c_backupPool.getPtr(ptr, senderData);
    } else { 
      jam();
#ifdef DEBUG_ABORT
      ndbout_c("Backup: abort request type=%u on id=%u,%u not found",
	       requestType, backupId, senderData);
#endif
      return;
    }
  }//if
  
  ptr.p->m_gsn = GSN_ABORT_BACKUP_ORD;
  const bool isCoordinator = (ptr.p->masterRef == reference());
  
  bool ok = false;
  switch(requestType){

    /**
     * Requests sent to master
     */
  case AbortBackupOrd::ClientAbort:
    jam();
    // fall through
  case AbortBackupOrd::LogBufferFull:
    jam();
    // fall through
  case AbortBackupOrd::FileOrScanError:
    jam();
    ndbrequire(isCoordinator);
    ptr.p->setErrorCode(requestType);
    if(ptr.p->masterData.gsn == GSN_BACKUP_FRAGMENT_REQ)
    {
      /**
       * Only scans are actively aborted
       */
      abort_scan(signal, ptr);
    }
    return;
    
    /**
     * Requests sent to slave
     */
  case AbortBackupOrd::AbortScan:
    jam();
    ptr.p->setErrorCode(requestType);
    return;
    
  case AbortBackupOrd::BackupComplete:
    jam();
    cleanup(signal, ptr);
    return;
  case AbortBackupOrd::BackupFailure:
  case AbortBackupOrd::BackupFailureDueToNodeFail:
  case AbortBackupOrd::OkToClean:
  case AbortBackupOrd::IncompatibleVersions:
#ifndef VM_TRACE
  default:
#endif
    ptr.p->setErrorCode(requestType);
    ok= true;
  }
  ndbrequire(ok);
  
  Uint32 ref= ptr.p->masterRef;
  ptr.p->masterRef = reference();
  ptr.p->nodes.clear();
  ptr.p->nodes.set(getOwnNodeId());


  ptr.p->stopGCP= ptr.p->startGCP + 1;
  sendStopBackup(signal, ptr);
}


void
Backup::dumpUsedResources()
{
  jam();
  BackupRecordPtr ptr;

  for(c_backups.first(ptr); ptr.i != RNIL; c_backups.next(ptr)) {
    ndbout_c("Backup id=%u, slaveState.getState = %u, errorCode=%u",
	     ptr.p->backupId,
	     ptr.p->slaveState.getState(),
	     ptr.p->errorCode);

    TablePtr tabPtr;
    for(ptr.p->tables.first(tabPtr);
	tabPtr.i != RNIL;
	ptr.p->tables.next(tabPtr)) {
      jam();
      for(Uint32 j = 0; j<3; j++) {
	jam();
	TriggerPtr trigPtr;
	if(tabPtr.p->triggerAllocated[j]) {
	  jam();
	  c_triggerPool.getPtr(trigPtr, tabPtr.p->triggerIds[j]);
	  ndbout_c("Allocated[%u] Triggerid = %u, event = %u",
		 j,
		 tabPtr.p->triggerIds[j],
		 trigPtr.p->event);
	}//if
      }//for
    }//for
    
    BackupFilePtr filePtr;
    for(ptr.p->files.first(filePtr);
	filePtr.i != RNIL;
	ptr.p->files.next(filePtr)) {
      jam();
      ndbout_c("filePtr.i = %u, filePtr.p->fileOpened=%u fileRunning=%u "
	       "scanRunning=%u",
	       filePtr.i,
	       filePtr.p->fileOpened,
	       filePtr.p->fileRunning,
	       filePtr.p->scanRunning);
    }//for
  }
}

void
Backup::cleanup(Signal* signal, BackupRecordPtr ptr)
{

  TablePtr tabPtr;
  for(ptr.p->tables.first(tabPtr); tabPtr.i != RNIL;ptr.p->tables.next(tabPtr))
  {
    jam();
    tabPtr.p->attributes.release();
    tabPtr.p->fragments.release();
    for(Uint32 j = 0; j<3; j++) {
      jam();
      TriggerPtr trigPtr;
      if(tabPtr.p->triggerAllocated[j]) {
        jam();
	c_triggerPool.getPtr(trigPtr, tabPtr.p->triggerIds[j]);
	trigPtr.p->event = ILLEGAL_TRIGGER_ID;
        tabPtr.p->triggerAllocated[j] = false;
      }//if
      tabPtr.p->triggerIds[j] = ILLEGAL_TRIGGER_ID;
    }//for
    {
      signal->theData[0] = tabPtr.p->tableId;
      signal->theData[1] = 0; // unlock
      EXECUTE_DIRECT(DBDICT, GSN_BACKUP_FRAGMENT_REQ, signal, 2);
    }
  }//for

  BackupFilePtr filePtr;
  for(ptr.p->files.first(filePtr);
      filePtr.i != RNIL; 
      ptr.p->files.next(filePtr)) {
    jam();
    ndbrequire(filePtr.p->fileOpened == 0);
    ndbrequire(filePtr.p->fileRunning == 0);
    ndbrequire(filePtr.p->scanRunning == 0);
    filePtr.p->pages.release();
  }//for

  ptr.p->files.release();
  ptr.p->tables.release();
  ptr.p->triggers.release();
  ptr.p->backupId = ~0;
  
  if(ptr.p->checkError())
    removeBackup(signal, ptr);
  else
    c_backups.release(ptr);
}


void
Backup::removeBackup(Signal* signal, BackupRecordPtr ptr)
{
  jam();
  
  FsRemoveReq * req = (FsRemoveReq *)signal->getDataPtrSend();
  req->userReference = reference();
  req->userPointer = ptr.i;
  req->directory = 1;
  req->ownDirectory = 1;
  FsOpenReq::setVersion(req->fileNumber, 2);
  FsOpenReq::setSuffix(req->fileNumber, FsOpenReq::S_CTL);
  FsOpenReq::v2_setSequence(req->fileNumber, ptr.p->backupId);
  FsOpenReq::v2_setNodeId(req->fileNumber, getOwnNodeId());
  sendSignal(NDBFS_REF, GSN_FSREMOVEREQ, signal, 
	     FsRemoveReq::SignalLength, JBA);
}

void
Backup::execFSREMOVEREF(Signal* signal)
{
  jamEntry();
  FsRef * ref = (FsRef*)signal->getDataPtr();
  const Uint32 ptrI = ref->userPointer;

  FsConf * conf = (FsConf*)signal->getDataPtr();
  conf->userPointer = ptrI;
  execFSREMOVECONF(signal);
}

void
Backup::execFSREMOVECONF(Signal* signal){
  jamEntry();

  FsConf * conf = (FsConf*)signal->getDataPtr();
  const Uint32 ptrI = conf->userPointer;
  
  /**
   * Get backup record
   */
  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, ptrI);
  c_backups.release(ptr);
}

/**
 * LCP
 */
void
Backup::execLCP_PREPARE_REQ(Signal* signal)
{
  jamEntry();
  LcpPrepareReq req = *(LcpPrepareReq*)signal->getDataPtr();

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, req.backupPtr);

  bool first= true;
  TablePtr tabPtr;
  if(ptr.p->tables.first(tabPtr) && tabPtr.p->tableId != req.tableId)
  {
    jam();
    first= false;
    tabPtr.p->attributes.release();
    tabPtr.p->fragments.release();
    ptr.p->tables.release();
    ptr.p->errorCode = 0;
  } 

  if(ptr.p->tables.first(tabPtr) && ptr.p->errorCode == 0)
  {
    jam();
    FragmentPtr fragPtr;
    tabPtr.p->fragments.getPtr(fragPtr, 0);
    fragPtr.p->fragmentId = req.fragmentId;
    
    lcp_open_file_done(signal, ptr);
    return;
  } 
  else if(ptr.p->errorCode == 0)
  {
    jam();
    FragmentPtr fragPtr;
    if(!ptr.p->tables.seize(tabPtr) || !tabPtr.p->fragments.seize(1))
    {
      if(!tabPtr.isNull())
	ptr.p->tables.release();
      ndbrequire(false); // TODO
    }
    tabPtr.p->tableId = req.tableId;
    tabPtr.p->fragments.getPtr(fragPtr, 0);
    tabPtr.p->tableType = DictTabInfo::UserTable;
    fragPtr.p->fragmentId = req.fragmentId;
    fragPtr.p->lcp_no = req.lcpNo;
    fragPtr.p->scanned = 0;
    fragPtr.p->scanning = 0;
    fragPtr.p->tableId = req.tableId;
  } 
  else
  {
    jam();
    FragmentPtr fragPtr;
    tabPtr.p->fragments.getPtr(fragPtr, 0);
    fragPtr.p->fragmentId = req.fragmentId;
    defineBackupRef(signal, ptr, ptr.p->errorCode);
    return;
  }
  
  if(first)
  {
    jam();
    // start file thread
    ptr.p->backupId= req.backupId;
    lcp_open_file(signal, ptr);
    return;
  }
  else
  {
    jam();
    ndbrequire(ptr.p->backupId == req.backupId);
  }
  
  /**
   * Close previous file
   */
  jam();
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, ptr.p->dataFilePtr);
  filePtr.p->fileClosing = 1;
  filePtr.p->operation.dataBuffer.eof();
}

void
Backup::lcp_close_file_conf(Signal* signal, BackupRecordPtr ptr)
{
  if(!ptr.p->tables.isEmpty())
  {
    jam();
    lcp_open_file(signal, ptr);
    return;
  }
  
  lcp_send_end_lcp_conf(signal, ptr);
}

void
Backup::lcp_open_file(Signal* signal, BackupRecordPtr ptr)
{
  FsOpenReq * req = (FsOpenReq *)signal->getDataPtrSend();
  req->userReference = reference();
  req->fileFlags = 
    FsOpenReq::OM_WRITEONLY | 
    FsOpenReq::OM_TRUNCATE |
    FsOpenReq::OM_CREATE | 
    FsOpenReq::OM_APPEND;
  FsOpenReq::v2_setCount(req->fileNumber, 0xFFFFFFFF);
  
  TablePtr tabPtr;
  FragmentPtr fragPtr;
  
  ndbrequire(ptr.p->tables.first(tabPtr));
  tabPtr.p->fragments.getPtr(fragPtr, 0);

  /**
   * Lcp file
   */
  BackupFilePtr filePtr;
  c_backupFilePool.getPtr(filePtr, ptr.p->dataFilePtr);
  ndbrequire(filePtr.p->fileRunning == 0);
  filePtr.p->fileClosing = 0;
  filePtr.p->fileRunning = 1;
  
  req->userPointer = filePtr.i;
  FsOpenReq::setVersion(req->fileNumber, 5);
  FsOpenReq::setSuffix(req->fileNumber, FsOpenReq::S_DATA);
  FsOpenReq::v5_setLcpNo(req->fileNumber, fragPtr.p->lcp_no);
  FsOpenReq::v5_setTableId(req->fileNumber, tabPtr.p->tableId);
  sendSignal(NDBFS_REF, GSN_FSOPENREQ, signal, FsOpenReq::SignalLength, JBA);
}

void
Backup::lcp_open_file_done(Signal* signal, BackupRecordPtr ptr)
{
  TablePtr tabPtr;
  FragmentPtr fragPtr;

  ndbrequire(ptr.p->tables.first(tabPtr));
  tabPtr.p->fragments.getPtr(fragPtr, 0);
  
  ptr.p->slaveState.setState(STARTED);
  
  LcpPrepareConf* conf= (LcpPrepareConf*)signal->getDataPtrSend();
  conf->senderData = ptr.p->clientData;
  conf->senderRef = reference();
  conf->tableId = tabPtr.p->tableId;
  conf->fragmentId = fragPtr.p->fragmentId;
  sendSignal(ptr.p->masterRef, GSN_LCP_PREPARE_CONF, 
	     signal, LcpPrepareConf::SignalLength, JBB);
}

void
Backup::execEND_LCPREQ(Signal* signal)
{
  EndLcpReq* req= (EndLcpReq*)signal->getDataPtr();

  BackupRecordPtr ptr;
  c_backupPool.getPtr(ptr, req->backupPtr);
  ndbrequire(ptr.p->backupId == req->backupId);

  ptr.p->slaveState.setState(STOPPING);

  TablePtr tabPtr;
  if(ptr.p->tables.first(tabPtr))
  {
    tabPtr.p->attributes.release();
    tabPtr.p->fragments.release();
    ptr.p->tables.release();
  
    BackupFilePtr filePtr;
    c_backupFilePool.getPtr(filePtr, ptr.p->dataFilePtr);
    filePtr.p->fileClosing = 1;
    filePtr.p->operation.dataBuffer.eof();
    return;
  } 
  
  lcp_send_end_lcp_conf(signal, ptr);
}

void
Backup::lcp_send_end_lcp_conf(Signal* signal, BackupRecordPtr ptr)
{
  EndLcpConf* conf= (EndLcpConf*)signal->getDataPtr();

  conf->senderData = ptr.p->clientData;
  conf->senderRef = reference();
  
  ptr.p->errorCode = 0;
  ptr.p->slaveState.setState(CLEANING);
  ptr.p->slaveState.setState(INITIAL);
  ptr.p->slaveState.setState(DEFINING);
  ptr.p->slaveState.setState(DEFINED);

  sendSignal(ptr.p->masterRef, GSN_END_LCPCONF,
	     signal, EndLcpConf::SignalLength, JBB);
}
