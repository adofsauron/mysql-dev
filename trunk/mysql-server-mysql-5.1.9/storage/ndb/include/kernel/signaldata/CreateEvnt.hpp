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

#ifndef CREATE_EVNT_HPP
#define CREATE_EVNT_HPP

#include <ndberror.h>
#include "SignalData.hpp"
#include <NodeBitmask.hpp>
#include <signaldata/DictTabInfo.hpp>

/**
 * DropEvntReq.
 */
class DropEvntReq {
  friend bool printDROP_EVNT_REQ(FILE*, const Uint32*, Uint32, Uint16);

public:
  STATIC_CONST( SignalLength = 2 );
  SECTION( EVENT_NAME_SECTION = 0 );

  union {             // user block reference
    Uint32 senderRef;
    Uint32 m_userRef;
  };
  union {
    Uint32 senderData;
    Uint32 m_userData;            // user 
  };

  Uint32 getUserRef() const {
    return m_userRef;
  }
  void setUserRef(Uint32 val) {
    m_userRef = val;
  }
  Uint32 getUserData() const {
    return m_userData;
  }
  void setUserData(Uint32 val) {
    m_userData = val;
  }
};

/**
 * DropEvntConf.
 */
class DropEvntConf {
  friend bool printDROP_EVNT_CONF(FILE*, const Uint32*, Uint32, Uint16);

public:
  STATIC_CONST( SignalLength = 2 );

  union {             // user block reference
    Uint32 senderRef;
    Uint32 m_userRef;
  };
  union {
    Uint32 senderData;
    Uint32 m_userData;            // user 
  };

  Uint32 getUserRef() const {
    return m_userRef;
  }
  void setUserRef(Uint32 val) {
    m_userRef = val;
  }
  Uint32 getUserData() const {
    return m_userData;
  }
  void setUserData(Uint32 val) {
    m_userData = val;
  }
};

/**
 * DropEvntRef.
 */
class DropEvntRef {
  friend bool printDROP_EVNT_REF(FILE*, const Uint32*, Uint32, Uint16);

public:
  enum ErrorCode {
    NoError = 0,
    Undefined = 1,
    NF_FakeErrorREF = 11,
    Busy = 701,
    NotMaster = 702
  };
  STATIC_CONST( SignalLength = 7 );
  STATIC_CONST( SignalLength2 = SignalLength+1 );

  union {             // user block reference
    Uint32 senderRef;
    Uint32 m_userRef;
  };
  union {
    Uint32 senderData;
    Uint32 m_userData;            // user 
  };
  union {
    Uint32 errorCode;
    Uint32 m_errorCode;
  };
  Uint32 m_errorLine;
  Uint32 m_errorNode;
  // with SignalLength2
  Uint32 m_masterNodeId;
  Uint32 getUserRef() const {
    return m_userRef;
  }
  void setUserRef(Uint32 val) {
    m_userRef = val;
  }
  Uint32 getUserData() const {
    return m_userData;
  }
  void setUserData(Uint32 val) {
    m_userData = val;
  }
  Uint32 getErrorCode() const {
    return m_errorCode;
  }
  void setErrorCode(Uint32 val) {
    m_errorCode = val;
  }
  Uint32 getErrorLine() const {
    return m_errorLine;
  }
  void setErrorLine(Uint32 val) {
    m_errorLine = val;
  }
  Uint32 getErrorNode() const {
    return m_errorNode;
  }
  void setErrorNode(Uint32 val) {
    m_errorNode = val;
  }
  Uint32 getMasterNode() const {
    return m_masterNodeId;
  }
  void setMasterNode(Uint32 val) {
    m_masterNodeId = val;
  }
};

/**
 * CreateEvntReq.
 */
struct CreateEvntReq {
  friend bool printCREATE_EVNT_REQ(FILE*, const Uint32*, Uint32, Uint16);

  enum RequestType {
    RT_UNDEFINED = 0,
    RT_USER_CREATE = 1,
    RT_USER_GET = 2,

    RT_DICT_AFTER_GET = 0x1 << 4
    //    RT_DICT_MASTER    = 0x2 << 4,

    //    RT_DICT_COMMIT = 0xC << 4,
    //    RT_DICT_ABORT = 0xF << 4,
    //    RT_TC = 5 << 8
  };
  enum EventFlags {
    EF_REPORT_ALL = 0x1 << 16,
    EF_REPORT_SUBSCRIBE = 0x2 << 16,
    EF_ALL = 0xFFFF << 16
  };
  STATIC_CONST( SignalLengthGet = 3 );
  STATIC_CONST( SignalLengthCreate = 6+MAXNROFATTRIBUTESINWORDS );
  STATIC_CONST( SignalLength = 8+MAXNROFATTRIBUTESINWORDS );
  //  SECTION( ATTRIBUTE_LIST_SECTION = 0 );
  SECTION( EVENT_NAME_SECTION = 0 );

  union {
    Uint32 m_userRef;             // user block reference
    Uint32 senderRef;             // user block reference
  };
  union {
    Uint32 m_userData;            // user 
    Uint32 senderData;            // user 
  };
  Uint32 m_requestInfo;
  Uint32 m_tableId;             // table to event
  Uint32 m_tableVersion;        // table version
  AttributeMask::Data m_attrListBitmask;
  Uint32 m_eventType;           // EventFlags (16 bits) + from DictTabInfo::TableType (16 bits)
  Uint32 m_eventId;             // event table id set by DICT/SUMA
  Uint32 m_eventKey;            // event table key set by DICT/SUMA
  Uint32 getUserRef() const {
    return m_userRef;
  }
  void setUserRef(Uint32 val) {
    m_userRef = val;
  }
  Uint32 getUserData() const {
    return m_userData;
  }
  void setUserData(Uint32 val) {
    m_userData = val;
  }
  CreateEvntReq::RequestType getRequestType() const {
    const Uint32 val = BitmaskImpl::getField(1, &m_requestInfo, 0, 16);
    return (CreateEvntReq::RequestType)val;
  }
  void setRequestType(CreateEvntReq::RequestType val) {
    m_requestInfo = (Uint32)val;
  }
  Uint32 getRequestFlag() const {
    return BitmaskImpl::getField(1, &m_requestInfo, 16, 16);
  };
  void addRequestFlag(Uint32 val) {
    val |= BitmaskImpl::getField(1, &m_requestInfo, 16, 16);
    BitmaskImpl::setField(1, &m_requestInfo, 16, 16, val);
  };
  Uint32 getTableId() const {
    return m_tableId;
  }
  void setTableId(Uint32 val) {
    m_tableId = val;
  }
  Uint32 getTableVersion() const {
    return m_tableVersion;
  }
  void setTableVersion(Uint32 val) {
    m_tableVersion = val;
  }
  AttributeMask getAttrListBitmask() const {
    AttributeMask tmp;
    tmp.assign(m_attrListBitmask);
    return tmp;
  }
  void setAttrListBitmask(const AttributeMask & val) {
    AttributeMask::assign(m_attrListBitmask.data, val);
  }
  Uint32 getEventType() const {
    return m_eventType & ~EF_ALL;
  }
  void setEventType(Uint32 val) {
    m_eventType = (m_eventType & EF_ALL) | (~EF_ALL & (Uint32)val);
  }
  Uint32 getEventId() const {
    return m_eventId;
  }
  void setEventId(Uint32 val) {
    m_eventId = val;
  }
  Uint32 getEventKey() const {
    return m_eventKey;
  }
  void setEventKey(Uint32 val) {
    m_eventKey = val;
  }
  void clearFlags() {
    m_eventType&= ~EF_ALL;
  }
  Uint32 getReportFlags() const {
    return  m_eventType & EF_ALL;
  }
  void setReportFlags(Uint32 val) {
    m_eventType = (val & EF_ALL) | (m_eventType & ~EF_ALL);
  }
  Uint32 getReportAll() const {
    return  m_eventType & EF_REPORT_ALL ;
  }
  void setReportAll() {
    m_eventType|= EF_REPORT_ALL;
  }
  Uint32 getReportSubscribe() const {
    return  m_eventType & EF_REPORT_SUBSCRIBE ;
  }
  void setReportSubscribe() {
    m_eventType|= EF_REPORT_SUBSCRIBE;
  }
};

/**
 * CreateEvntConf.
 */
class CreateEvntConf {
  friend bool printCREATE_EVNT_CONF(FILE*, const Uint32*, Uint32, Uint16);

public:
  //  STATIC_CONST( InternalLength = 3 );
  STATIC_CONST( SignalLength = 8+MAXNROFATTRIBUTESINWORDS );

  union {
    Uint32 m_userRef;             // user block reference
    Uint32 senderRef;             // user block reference
  };
  union {
    Uint32 m_userData;            // user 
    Uint32 senderData;            // user 
  };
  Uint32 m_requestInfo;
  Uint32 m_tableId;
  Uint32 m_tableVersion;        // table version
  AttributeMask m_attrListBitmask;
  Uint32 m_eventType;
  Uint32 m_eventId;
  Uint32 m_eventKey;

  Uint32 getUserRef() const {
    return m_userRef;
  }
  void setUserRef(Uint32 val) {
    m_userRef = val;
  }
  Uint32 getUserData() const {
    return m_userData;
  }
  void setUserData(Uint32 val) {
    m_userData = val;
  }
  CreateEvntReq::RequestType getRequestType() const {
    return (CreateEvntReq::RequestType)m_requestInfo;
  }
  void setRequestType(CreateEvntReq::RequestType val) {
    m_requestInfo = (Uint32)val;
  }
  Uint32 getTableId() const {
    return m_tableId;
  }
  void setTableId(Uint32 val) {
    m_tableId = val;
  }
  Uint32 getTableVersion() const {
    return m_tableVersion;
  }
  void setTableVersion(Uint32 val) {
    m_tableVersion = val;
  }
  AttributeMask getAttrListBitmask() const {
    return m_attrListBitmask;
  }
  void setAttrListBitmask(const AttributeMask & val) {
    m_attrListBitmask = val;
  }
  Uint32 getEventType() const {
    return m_eventType;
  }
  void setEventType(Uint32 val) {
    m_eventType = (Uint32)val;
  }
  Uint32 getEventId() const {
    return m_eventId;
  }
  void setEventId(Uint32 val) {
    m_eventId = val;
  }
  Uint32 getEventKey() const {
    return m_eventKey;
  }
  void setEventKey(Uint32 val) {
    m_eventKey = val;
  }
};

/**
 * CreateEvntRef.
 */
struct CreateEvntRef {
  friend class SafeCounter;
  friend bool printCREATE_EVNT_REF(FILE*, const Uint32*, Uint32, Uint16);

  STATIC_CONST( SignalLength = 11 );
  STATIC_CONST( SignalLength2 = SignalLength + 1 );
  enum ErrorCode {
    NoError = 0,
    Undefined = 1,
    NF_FakeErrorREF = 11,
    Busy = 701,
    NotMaster = 702
  };
  union {
    Uint32 m_userRef;             // user block reference
    Uint32 senderRef;             // user block reference
  };
  union {
    Uint32 m_userData;            // user 
    Uint32 senderData;            // user 
  };

  Uint32 m_requestInfo;
  Uint32 m_tableId;
  Uint32 m_tableVersion;        // table version
  Uint32 m_eventType;
  Uint32 m_eventId;
  Uint32 m_eventKey;
  Uint32 errorCode;
  Uint32 m_errorLine;
  Uint32 m_errorNode;
  // with SignalLength2
  Uint32 m_masterNodeId;
  Uint32 getUserRef() const {
    return m_userRef;
  }
  void setUserRef(Uint32 val) {
    m_userRef = val;
  }
  Uint32 getUserData() const {
    return m_userData;
  }
  void setUserData(Uint32 val) {
    m_userData = val;
  }
  CreateEvntReq::RequestType getRequestType() const {
    return (CreateEvntReq::RequestType)m_requestInfo;
  }
  void setRequestType(CreateEvntReq::RequestType val) {
    m_requestInfo = (Uint32)val;
  }
  Uint32 getTableId() const {
    return m_tableId;
  }
  void setTableId(Uint32 val) {
    m_tableId = val;
  }
  Uint32 getTableVersion() const {
    return m_tableVersion;
  }
  void setTableVersion(Uint32 val) {
    m_tableVersion = val;
  }

  Uint32 getEventType() const {
    return m_eventType;
  }
  void setEventType(Uint32 val) {
    m_eventType = (Uint32)val;
  }
  Uint32 getEventId() const {
    return m_eventId;
  }
  void setEventId(Uint32 val) {
    m_eventId = val;
  }
  Uint32 getEventKey() const {
    return m_eventKey;
  }
  void setEventKey(Uint32 val) {
    m_eventKey = val;
  }

  Uint32 getErrorCode() const {
    return errorCode;
  }
  void setErrorCode(Uint32 val) {
    errorCode = val;
  }
  Uint32 getErrorLine() const {
    return m_errorLine;
  }
  void setErrorLine(Uint32 val) {
    m_errorLine = val;
  }
  Uint32 getErrorNode() const {
    return m_errorNode;
  }
  void setErrorNode(Uint32 val) {
    m_errorNode = val;
  }
  Uint32 getMasterNode() const {
    return m_masterNodeId;
  }
  void setMasterNode(Uint32 val) {
    m_masterNodeId = val;
  }
};
#endif
