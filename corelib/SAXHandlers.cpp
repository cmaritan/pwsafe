/*
* Copyright (c) 2003-2008 Rony Shapiro <ronys@users.sourceforge.net>.
* All rights reserved. Use of the code is allowed under the
* Artistic License 2.0 terms, as specified in the LICENSE file
* distributed with this code, or available from
* http://www.opensource.org/licenses/artistic-license-2.0.php
*/
// SAXHandlers.cpp : implementation file
//

#include "corelib.h"
#include "PWScore.h"
#include "ItemData.h"
#include "util.h"
#include "SAXHandlers.h"
#include "UUIDGen.h"
#include "xml_import.h"
#include "corelib.h"
#include "PWSfileV3.h"
#include "PWSprefs.h"
#include "VerifyFormat.h"

// Stop warnings about unused formal parameters!
#pragma warning(disable : 4100)

//  -----------------------------------------------------------------------
//  PWSSAXErrorHandler Methods
//  -----------------------------------------------------------------------
PWSSAXErrorHandler::PWSSAXErrorHandler()
  : bErrorsFound(FALSE), m_strValidationResult(_T(""))
{
  m_refCnt = 0;
}

PWSSAXErrorHandler::~PWSSAXErrorHandler()
{
}

long __stdcall PWSSAXErrorHandler::QueryInterface(const struct _GUID &riid,void ** ppvObject)
{
  *ppvObject = NULL;
  if (riid == IID_IUnknown ||riid == __uuidof(ISAXContentHandler))
  {
    *ppvObject = static_cast<ISAXErrorHandler *>(this);
  }

  if (*ppvObject)
  {
    AddRef();
    return S_OK;
  }
  else return E_NOINTERFACE;
}

unsigned long __stdcall PWSSAXErrorHandler::AddRef()
{
  return ++m_refCnt; // NOT thread-safe
}

unsigned long __stdcall PWSSAXErrorHandler::Release()
{
  --m_refCnt; // NOT thread-safe
  if (m_refCnt == 0) {
    delete this;
    return 0; // Can't return the member of a deleted object.
  }
  else return m_refCnt;
}

HRESULT STDMETHODCALLTYPE PWSSAXErrorHandler::error(struct ISAXLocator * pLocator,
                                                     unsigned short * pwchErrorMessage,
                                                     HRESULT hrErrorCode )
{
  TCHAR szErrorMessage[MAX_PATH*2] = {0};
  TCHAR szFormatString[MAX_PATH*2] = {0};
  int iLineNumber, iCharacter;

#ifdef _UNICODE
#if (_MSC_VER >= 1400)
  _tcscpy_s(szErrorMessage, MAX_PATH * 2, pwchErrorMessage);
#else
  _tcscpy(szErrorMessage, pwchErrorMessage);
#endif
#else
#if (_MSC_VER >= 1400)
  size_t num_converted;
  wcstombs_s(&num_converted, szErrorMessage, MAX_PATH*2, pwchErrorMessage, MAX_PATH);
#else
  wcstombs(szErrorMessage, pwchErrorMessage, MAX_PATH);
#endif
#endif
  pLocator->getLineNumber(&iLineNumber);
  pLocator->getColumnNumber(&iCharacter);

  stringT cs_format;
  LoadAString(cs_format, IDSC_SAXGENERROR);

#if (_MSC_VER >= 1400)
  _stprintf_s(szFormatString, MAX_PATH*2, cs_format.c_str(),
    hrErrorCode, iLineNumber, iCharacter, szErrorMessage);
#else
  _stprintf(szFormatString, cs_format,
    hrErrorCode, iLineNumber, iCharacter, szErrorMessage);
#endif

  m_strValidationResult += szFormatString;

  bErrorsFound = TRUE;

  return S_OK;
}

HRESULT STDMETHODCALLTYPE  PWSSAXErrorHandler::fatalError(struct ISAXLocator * pLocator,
                                                          unsigned short * pwchErrorMessage,
                                                          HRESULT hrErrorCode )
{
  return S_OK;
}

HRESULT STDMETHODCALLTYPE  PWSSAXErrorHandler::ignorableWarning(struct ISAXLocator * pLocator,
                                                                unsigned short * pwchErrorMessage,
                                                                HRESULT hrErrorCode )
{
  return S_OK;
}

//  -----------------------------------------------------------------------
//  PWSSAXContentHandler Methods
//  -----------------------------------------------------------------------
PWSSAXContentHandler::PWSSAXContentHandler()
{
  m_refCnt = 0;
  m_strElemContent.clear();
  m_numEntries = 0;
  m_ImportedPrefix = _T("");
  m_delimiter = _T('^');
  m_bheader = false;
  m_bDatabaseHeaderErrors = false;
  m_bRecordHeaderErrors = false;
  m_nITER = MIN_HASH_ITERATIONS;
  m_nRecordsWithUnknownFields = 0;

  // Following are user preferences stored in the database
  m_bDisplayExpandedAddEditDlg = -1;
  m_bMaintainDateTimeStamps = -1;
  m_bPWUseDigits = -1;
  m_bPWUseEasyVision = -1;
  m_bPWUseHexDigits = -1;
  m_bPWUseLowercase = -1;
  m_bPWUseSymbols = -1;
  m_bPWUseUppercase = -1;
  m_bPWMakePronounceable = -1;
  m_bSaveImmediately = -1;
  m_bSavePasswordHistory = -1;
  m_bShowNotesDefault = -1;
  m_bShowPasswordInTree = -1;
  m_bShowPWDefault = -1;
  m_bShowUsernameInTree = -1;
  m_bSortAscending = -1;
  m_bUseDefaultUser = -1;
  m_iIdleTimeout = -1;
  m_iNumPWHistoryDefault = -1;
  m_iPWDefaultLength = -1;
  m_iTreeDisplayStatusAtOpen = -1;
  m_iPWDigitMinLength = -1;
  m_iPWLowercaseMinLength = -1;
  m_iPWSymbolMinLength = -1;
  m_iPWUppercaseMinLength = -1;
  m_sDefaultAutotypeString = _T("");
  m_sDefaultUsername = _T("");
}

//  -----------------------------------------------------------------------
PWSSAXContentHandler::~PWSSAXContentHandler()
{
  m_ukhxl.clear();
}

void PWSSAXContentHandler::SetVariables(PWScore *core, const bool &bValidation,
                                        const stringT &ImportedPrefix, const TCHAR &delimiter,
                                        UUIDList *possible_aliases, UUIDList *possible_shortcuts)
{
  m_bValidation = bValidation;
  m_ImportedPrefix = ImportedPrefix;
  m_delimiter = delimiter;
  m_xmlcore = core;
  m_possible_aliases = possible_aliases;
  m_possible_shortcuts = possible_shortcuts;
}

long __stdcall PWSSAXContentHandler::QueryInterface(const struct _GUID &riid,void ** ppvObject)
{
  *ppvObject = NULL;
  if (riid == IID_IUnknown ||riid == __uuidof(ISAXContentHandler)) {
    *ppvObject = static_cast<ISAXContentHandler *>(this);
  }

  if (*ppvObject) {
    AddRef();
    return S_OK;
  }
  else return E_NOINTERFACE;
}

unsigned long __stdcall PWSSAXContentHandler::AddRef()
{
  return ++m_refCnt; // NOT thread-safe
}

unsigned long __stdcall PWSSAXContentHandler::Release()
{
  --m_refCnt; // NOT thread-safe
  if (m_refCnt == 0) {
    delete this;
    return 0; // Can't return the member of a deleted object.
  }
  else return m_refCnt;
}

//  -----------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE  PWSSAXContentHandler::startDocument ( )
{
  m_strImportErrors = _T("");
  m_bentrybeingprocessed = false;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE  PWSSAXContentHandler::putDocumentLocator (struct ISAXLocator * pLocator )
{
  return S_OK;
}

//  ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE PWSSAXContentHandler::startElement(
  /* [in] */ wchar_t __RPC_FAR *pwchNamespaceUri,
  /* [in] */ int cchNamespaceUri,
  /* [in] */ wchar_t __RPC_FAR *pwchLocalName,
  /* [in] */ int cchLocalName,
  /* [in] */ wchar_t __RPC_FAR *pwchRawName,
  /* [in] */ int cchRawName,
  /* [in] */ ISAXAttributes __RPC_FAR *pAttributes)
{
  TCHAR szCurElement[MAX_PATH+1] = {0};

#ifdef _UNICODE
#if (_MSC_VER >= 1400)
  _tcsncpy_s(szCurElement, MAX_PATH+1, pwchRawName, cchRawName);
#else
  _tcsncpy(szCurElement, pwchRawName, cchRawName);
#endif
#else
#if (_MSC_VER >= 1400)
  size_t num_converted;
  wcstombs_s(&num_converted, szCurElement, MAX_PATH+1, pwchRawName, cchRawName);
#else
  wcstombs(szCurElement, pwchRawName, cchRawName);
#endif
#endif

  if (_tcscmp(szCurElement, _T("passwordsafe")) == 0) {
    if (m_bValidation) {
      int iAttribs = 0;
      pAttributes->getLength(&iAttribs);
      for (int i = 0; i < iAttribs; i++) {
        TCHAR szQName[MAX_PATH + 1] = {0};
        TCHAR szValue[MAX_PATH + 1] = {0};
        wchar_t *QName, *Value;
        int QName_length, Value_length;

        pAttributes->getQName(i, &QName, &QName_length);
        pAttributes->getValue(i, &Value, &Value_length);
#ifdef _UNICODE
#if (_MSC_VER >= 1400)
        _tcsncpy_s(szQName, MAX_PATH + 1, QName, QName_length);
        _tcsncpy_s(szValue, MAX_PATH + 1, Value, Value_length);
#else
        _tcsncpy(szQName, QName, QName_length);
        _tcsncpy(szValue, Value, Value_length);
#endif
#else
#if (_MSC_VER >= 1400)
        wcstombs_s(&num_converted, szQName, MAX_PATH+1, QName, QName_length);
        wcstombs_s(&num_converted, szValue, MAX_PATH+1, Value, Value_length);
#else
        wcstombs(szQName, QName, QName_length);
        wcstombs(szValue, Value, Value_length);
#endif
#endif
        if (_tcscmp(szQName, _T("delimiter")) == 0)
          m_delimiter = szValue[0];

        // We do not save or copy the imported file_uuid_array
        //   szQName == _T("Database_uuid")
      }
    }
  }

  if (m_bValidation)
    return S_OK;

  if (_tcscmp(szCurElement, _T("unknownheaderfields")) == 0) {
    m_ukhxl.clear();
    m_bheader = true;
  }

  if (_tcscmp(szCurElement, _T("field")) == 0) {
    int iAttribs = 0;
    pAttributes->getLength(&iAttribs);
    for (int i = 0; i < iAttribs; i++) {
      TCHAR szQName[MAX_PATH + 1] = {0};
      TCHAR szValue[MAX_PATH + 1] = {0};
      wchar_t *QName, *Value;
      int QName_length, Value_length;

      pAttributes->getQName(i, &QName, &QName_length);
      pAttributes->getValue(i, &Value, &Value_length);
#ifdef _UNICODE
#if (_MSC_VER >= 1400)
      _tcsncpy_s(szQName, MAX_PATH + 1, QName, QName_length);
      _tcsncpy_s(szValue, MAX_PATH + 1, Value, Value_length);
#else
      _tcsncpy(szQName, QName, QName_length);
      _tcsncpy(szValue, Value, Value_length);
#endif
#else
#if (_MSC_VER >= 1400)
      wcstombs_s(&num_converted, szQName, MAX_PATH+1, QName, QName_length);
      wcstombs_s(&num_converted, szValue, MAX_PATH+1, Value, Value_length);
#else
      wcstombs(szQName, QName, QName_length);
      wcstombs(szValue, Value, Value_length);
#endif
#endif
      if (_tcscmp(szQName, _T("ftype")) == 0)
        m_ctype = (unsigned char)_ttoi(szValue);
    }
  }

  if (_tcscmp(szCurElement, _T("entry")) == 0) {
    cur_entry = new pw_entry;
    cur_entry->group = _T("");
    cur_entry->title = _T("");
    cur_entry->username = _T("");
    cur_entry->password = _T("");
    cur_entry->url = _T("");
    cur_entry->autotype = _T("");
    cur_entry->ctime = _T("");
    cur_entry->atime = _T("");
    cur_entry->xtime = _T("");
    cur_entry->xtime_interval = _T("");
    cur_entry->pmtime = _T("");
    cur_entry->rmtime = _T("");
    cur_entry->changed = _T("");
    cur_entry->pwhistory = _T("");
    cur_entry->notes = _T("");
    cur_entry->uuid = _T("");
    cur_entry->pwp.Empty();
    cur_entry->entrytype = NORMAL;
    m_bentrybeingprocessed = true;
  }

  if (_tcscmp(szCurElement, _T("ctime")) == 0)
    m_whichtime = PW_CTIME;

  if (_tcscmp(szCurElement, _T("atime")) == 0)
    m_whichtime = PW_ATIME;

  // 'ltime' depreciated but must still be handled for a while!
  if (_tcscmp(szCurElement, _T("ltime")) == 0 ||
      _tcscmp(szCurElement, _T("xtime")) == 0)
    m_whichtime = PW_XTIME;

  if (_tcscmp(szCurElement, _T("pmtime")) == 0)
    m_whichtime = PW_PMTIME;

  if (_tcscmp(szCurElement, _T("rmtime")) == 0)
    m_whichtime = PW_RMTIME;

  if (_tcscmp(szCurElement, _T("changed")) == 0)
    m_whichtime = PW_CHANGED;

  m_strElemContent = _T("");

  return S_OK;
}

//  ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE PWSSAXContentHandler::characters(
  /* [in] */ wchar_t __RPC_FAR *pwchChars,
  /* [in] */ int cchChars)
{
  if (m_bValidation)
    return S_OK;

  TCHAR* szData = new TCHAR[cchChars+2];

#ifdef _UNICODE
#if (_MSC_VER >= 1400)
  _tcsncpy_s(szData, cchChars+2, pwchChars, cchChars);
#else
  _tcsncpy(szData, pwchChars, cchChars);
#endif
#else
#if _MSC_VER >= 1400
  size_t num_converted;
  wcstombs_s(&num_converted, szData, cchChars+2, pwchChars, cchChars);
#else
  wcstombs(szData, pwchChars, cchChars);
#endif
#endif

  szData[cchChars]=0;
  m_strElemContent += szData;

  delete [] szData;
  szData = NULL;

  return S_OK;
}

//  -----------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE  PWSSAXContentHandler::endElement (
  unsigned short * pwchNamespaceUri,
  int cchNamespaceUri,
  unsigned short * pwchLocalName,
  int cchLocalName,
  unsigned short * pwchQName,
  int cchQName)
{
  TCHAR szCurElement[MAX_PATH+1] = {0};

#ifdef _UNICODE
#if (_MSC_VER >= 1400)
  _tcsncpy_s(szCurElement, MAX_PATH+1, pwchQName, cchQName);
#else
  _tcsncpy(szCurElement, pwchQName, cchQName);
#endif
#else
#if (_MSC_VER >= 1400)
  size_t num_converted;
  wcstombs_s(&num_converted, szCurElement, MAX_PATH+1, pwchQName, cchQName);
#else
  wcstombs(szCurElement, pwchQName, cchQName);
#endif
#endif

  if (m_bValidation) {
    if (_tcscmp(szCurElement, _T("entry")) == 0)
      m_numEntries++;

    return S_OK;
  }

  if (_tcscmp(szCurElement, _T("entry")) == 0) {
    uuid_array_t uuid_array;
    CItemData tempitem;
    tempitem.Clear();
    if (cur_entry->uuid.empty())
      tempitem.CreateUUID();
    else {
      // _stscanf_s always outputs to an "int" using %x even though
      // target is only 1.  Read into larger buffer to prevent data being
      // overwritten and then copy to where we want it!
      unsigned char temp_uuid_array[sizeof(uuid_array_t) + sizeof(int)];
      int nscanned = 0;
      const TCHAR *lpszuuid = cur_entry->uuid.c_str();
      for (unsigned i = 0; i < sizeof(uuid_array_t); i++) {
#if _MSC_VER >= 1400
        nscanned += _stscanf_s(lpszuuid, _T("%02x"), &temp_uuid_array[i]);
#else
        nscanned += _stscanf(lpszuuid, _T("%02x"), &temp_uuid_array[i]);
#endif
        lpszuuid += 2;
      }
      memcpy(uuid_array, temp_uuid_array, sizeof(uuid_array_t));
      if (nscanned != sizeof(uuid_array_t) ||
        m_xmlcore->Find(uuid_array) != m_xmlcore->GetEntryEndIter())
        tempitem.CreateUUID();
      else {
        tempitem.SetUUID(uuid_array);
      }
    }
    StringX newgroup;
    if (!m_ImportedPrefix.empty()) {
      newgroup = m_ImportedPrefix.c_str(); newgroup += _T(".");
    }
    EmptyIfOnlyWhiteSpace(cur_entry->group);
    newgroup += cur_entry->group;
    if (m_xmlcore->Find(newgroup, cur_entry->title, cur_entry->username) != 
      m_xmlcore->GetEntryEndIter()) {
        // Find a unique "Title"
        StringX Unique_Title;
        ItemListConstIter iter;
        int i = 0;
        stringT s_import;
        do {
          i++;
          Format(s_import, IDSC_IMPORTNUMBER, i);
          Unique_Title = cur_entry->title + s_import.c_str();
          iter = m_xmlcore->Find(newgroup, Unique_Title, cur_entry->username);
        } while (iter != m_xmlcore->GetEntryEndIter());
        cur_entry->title = Unique_Title;
    }
    tempitem.SetGroup(newgroup);
    EmptyIfOnlyWhiteSpace(cur_entry->title);
    if (!cur_entry->title.empty())
      tempitem.SetTitle(cur_entry->title, m_delimiter);
    EmptyIfOnlyWhiteSpace(cur_entry->username);
    if (!cur_entry->username.empty())
      tempitem.SetUser(cur_entry->username);
    if (!cur_entry->password.empty())
      tempitem.SetPassword(cur_entry->password);
    EmptyIfOnlyWhiteSpace(cur_entry->url);
    if (!cur_entry->url.empty())
      tempitem.SetURL(cur_entry->url);
    EmptyIfOnlyWhiteSpace(cur_entry->autotype);
    if (!cur_entry->autotype.empty())
      tempitem.SetAutoType(cur_entry->autotype);
    if (!cur_entry->ctime.empty())
      tempitem.SetCTime(cur_entry->ctime.c_str());
    if (!cur_entry->pmtime.empty())
      tempitem.SetPMTime(cur_entry->pmtime.c_str());
    if (!cur_entry->atime.empty())
      tempitem.SetATime(cur_entry->atime.c_str());
    if (!cur_entry->xtime.empty())
      tempitem.SetXTime(cur_entry->xtime.c_str());
    if (!cur_entry->xtime_interval.empty()) {
      int numdays = _ttoi(cur_entry->xtime_interval.c_str());
      if (numdays > 0 && numdays <= 3650)
        tempitem.SetXTimeInt(numdays);
    }
    if (!cur_entry->rmtime.empty())
      tempitem.SetRMTime(cur_entry->rmtime.c_str());

    if (cur_entry->pwp.flags != 0) {
      tempitem.SetPWPolicy(cur_entry->pwp);
    }

    StringX newPWHistory;
    stringT strPWHErrors, buffer;
    Format(buffer, IDSC_SAXERRORPWH, cur_entry->group.c_str(),
           cur_entry->title.c_str(), cur_entry->username.c_str());
    switch (VerifyImportPWHistoryString(cur_entry->pwhistory, newPWHistory, strPWHErrors)) {
      case PWH_OK:
        tempitem.SetPWHistory(newPWHistory.c_str());
        buffer.clear();
        break;
      case PWH_IGNORE:
        buffer.clear();
        break;
      case PWH_INVALID_HDR:
      case PWH_INVALID_STATUS:
      case PWH_INVALID_NUM:
      case PWH_INVALID_DATETIME:
      case PWH_INVALID_PSWD_LENGTH:
      case PWH_TOO_SHORT:
      case PWH_TOO_LONG:
      case PWH_INVALID_CHARACTER:
        buffer += strPWHErrors;
        break;
      default:
        ASSERT(0);
    }
    m_strImportErrors += buffer;
    EmptyIfOnlyWhiteSpace(cur_entry->notes);
    if (!cur_entry->notes.empty())
      tempitem.SetNotes(cur_entry->notes, m_delimiter);

    if (!cur_entry->uhrxl.empty()) {
      UnknownFieldList::const_iterator vi_IterUXRFE;
      for (vi_IterUXRFE = cur_entry->uhrxl.begin();
        vi_IterUXRFE != cur_entry->uhrxl.end();
        vi_IterUXRFE++) {
          UnknownFieldEntry unkrfe = *vi_IterUXRFE;
          /* #ifdef _DEBUG
          stringT cs_timestamp;
          cs_timestamp = PWSUtil::GetTimeStamp();
          TRACE(_T("%s: Record %s, %s, %s has unknown field: %02x, length %d/0x%04x, value:\n"),
          cs_timestamp, cur_entry->group, cur_entry->title, cur_entry->username, 
          unkrfe.uc_Type, (int)unkrfe.st_length, (int)unkrfe.st_length);
          PWSDebug::HexDump(unkrfe.uc_pUField, (int)unkrfe.st_length, cs_timestamp);
          #endif /* DEBUG */
          tempitem.SetUnknownField(unkrfe.uc_Type, (int)unkrfe.st_length, unkrfe.uc_pUField);
      }
    }

    // If a potential alias, add to the vector for later verification and processing
    if (cur_entry->entrytype == ALIAS) {
      tempitem.GetUUID(uuid_array);
      m_possible_aliases->push_back(uuid_array);
    }
    if (cur_entry->entrytype == SHORTCUT) {
      tempitem.GetUUID(uuid_array);
      m_possible_shortcuts->push_back(uuid_array);
    }

    m_xmlcore->AddEntry(tempitem);
    cur_entry->uhrxl.clear();
    delete cur_entry;
    m_numEntries++;
  }

  if (_tcscmp(szCurElement, _T("group")) == 0) {
    cur_entry->group = m_strElemContent;
  }

  if (_tcscmp(szCurElement, _T("title")) == 0) {
    cur_entry->title = m_strElemContent;
  }

  if (_tcscmp(szCurElement, _T("username")) == 0) {
    cur_entry->username = m_strElemContent;
  }

  if (_tcscmp(szCurElement, _T("password")) == 0) {
    cur_entry->password = m_strElemContent;
    if (Replace(m_strElemContent, _T(':'), _T(';')) <= 2) {
      if (m_strElemContent.substr(0, 2) == _T("[[") &&
          m_strElemContent.substr(m_strElemContent.length() - 2) == _T("]]")) {
          cur_entry->entrytype = ALIAS;
      }
      if (m_strElemContent.substr(0, 2) == _T("[~") &&
          m_strElemContent.substr(m_strElemContent.length() - 2) == _T("~]")) {
          cur_entry->entrytype = SHORTCUT;
      }
    }
  }

  if (_tcscmp(szCurElement, _T("url")) == 0) {
    cur_entry->url = m_strElemContent;
  }

  if (_tcscmp(szCurElement, _T("autotype")) == 0) {
    cur_entry->autotype = m_strElemContent;
  }

  if (_tcscmp(szCurElement, _T("notes")) == 0) {
    cur_entry->notes = m_strElemContent;
  }

  if (_tcscmp(szCurElement, _T("uuid")) == 0) {
    cur_entry->uuid = m_strElemContent;
  }

  if (_tcscmp(szCurElement, _T("status")) == 0) {
    stringT buffer;
    int i = _ttoi(m_strElemContent.c_str());
    Format(buffer, _T("%01x"), i);
    cur_entry->pwhistory = buffer.c_str();
  }

  if (_tcscmp(szCurElement, _T("max")) == 0) {
    stringT buffer;
    int i = _ttoi(m_strElemContent.c_str());
    Format(buffer, _T("%02x"), i);
    cur_entry->pwhistory += buffer.c_str();
  }

  if (_tcscmp(szCurElement, _T("num")) == 0) {
    stringT buffer;
    int i = _ttoi(m_strElemContent.c_str());
    Format(buffer, _T("%02x"), i);
    cur_entry->pwhistory += buffer.c_str();
  }

  if (_tcscmp(szCurElement, _T("ctime")) == 0) {
    Replace(cur_entry->ctime, _T('-'), _T('/'));
    m_whichtime = -1;
  }

  if (_tcscmp(szCurElement, _T("pmtime")) == 0) {
    Replace(cur_entry->pmtime, _T('-'), _T('/'));
    m_whichtime = -1;
  }

  if (_tcscmp(szCurElement, _T("atime")) == 0) {
    Replace(cur_entry->atime, _T('-'), _T('/'));
    m_whichtime = -1;
  }

  if (_tcscmp(szCurElement, _T("xtime")) == 0) {
    Replace(cur_entry->xtime, _T('-'), _T('/'));
    m_whichtime = -1;
  }

  if (_tcscmp(szCurElement, _T("rmtime")) == 0) {
    Replace(cur_entry->rmtime, _T('-'), _T('/'));
    m_whichtime = -1;
  }

  if (_tcscmp(szCurElement, _T("changed")) == 0) {
    Replace(cur_entry->changed, _T('-'), _T('/'));
    m_whichtime = -1;
  }

  if (_tcscmp(szCurElement, _T("oldpassword")) == 0) {
    Trim(cur_entry->changed);
    if (cur_entry->changed.empty()) {
      //                       1234567890123456789
      cur_entry->changed = _T("1970-01-01 00:00:00");
    }
    cur_entry->pwhistory += _T(" ") + cur_entry->changed;
    //cur_entry->changed.Empty();
    stringT buffer;
    Format(buffer, _T(" %04x %s"),
           m_strElemContent.length(), m_strElemContent.c_str());
    cur_entry->pwhistory += buffer.c_str();
    buffer.clear();
  }

  if (_tcscmp(szCurElement, _T("date")) == 0 && !m_strElemContent.empty()) {
    switch (m_whichtime) {
      case PW_CTIME:
        cur_entry->ctime = m_strElemContent;
        break;
      case PW_PMTIME:
        cur_entry->pmtime = m_strElemContent;
        break;
      case PW_ATIME:
        cur_entry->atime = m_strElemContent;
        break;
      case PW_XTIME:
        cur_entry->xtime = m_strElemContent;
        break;
      case PW_RMTIME:
        cur_entry->rmtime = m_strElemContent;
        break;
      case PW_CHANGED:
        cur_entry->changed = m_strElemContent;
        break;
      default:
        ASSERT(0);
    }
  }

  if (_tcscmp(szCurElement, _T("time")) == 0 && !m_strElemContent.empty()) {
    switch (m_whichtime) {
      case PW_CTIME:
        cur_entry->ctime += _T(" ") + m_strElemContent;
        break;
      case PW_PMTIME:
        cur_entry->pmtime += _T(" ") + m_strElemContent;
        break;
      case PW_ATIME:
        cur_entry->atime += _T(" ") + m_strElemContent;
        break;
      case PW_XTIME:
        cur_entry->xtime += _T(" ") + m_strElemContent;
        break;
      case PW_RMTIME:
        cur_entry->rmtime += _T(" ") + m_strElemContent;
        break;
      case PW_CHANGED:
        cur_entry->changed += _T(" ") + m_strElemContent;
        break;
      default:
        ASSERT(0);
    }
  }

  if (_tcscmp(szCurElement, _T("xtime_interval")) == 0 && !m_strElemContent.empty()) {
    cur_entry->xtime_interval = Trim(m_strElemContent);
  }

  if (_tcscmp(szCurElement, _T("unknownheaderfields")) == 0)
    m_bheader = false;

  if (_tcscmp(szCurElement, _T("unknownrecordfields")) == 0) {
    if (!cur_entry->uhrxl.empty())
      m_nRecordsWithUnknownFields++;
  }

  if (_tcscmp(szCurElement, _T("field")) == 0) {
    // _stscanf_s always outputs to an "int" using %x even though
    // target is only 1.  Read into larger buffer to prevent data being
    // overwritten and then copy to where we want it!
    const int length = m_strElemContent.length();
    // UNK_HEX_REP will represent unknown values
    // as hexadecimal, rather than base64 encoding.
    // Easier to debug.
#ifndef UNK_HEX_REP
    m_pfield = new unsigned char[(length / 3) * 4 + 4];
    size_t out_len;
    PWSUtil::Base64Decode(m_strElemContent, m_pfield, out_len);
    m_fieldlen = (int)out_len;
#else
    m_fieldlen = length / 2;
    m_pfield = new unsigned char[m_fieldlen + sizeof(int)];
    int nscanned = 0;
    TCHAR *lpsz_string = m_strElemContent.GetBuffer(length);
    for (int i = 0; i < m_fieldlen; i++) {
#if _MSC_VER >= 1400
      nscanned += _stscanf_s(lpsz_string, _T("%02x"), &m_pfield[i]);
#else
      nscanned += _stscanf(lpsz_string, _T("%02x"), &m_pfield[i]);
#endif
      lpsz_string += 2;
    }
    m_strElemContent.ReleaseBuffer();
#endif
    // We will use header field entry and add into proper record field
    // when we create the complete record entry
    UnknownFieldEntry ukxfe(m_ctype, m_fieldlen, m_pfield);
    if (m_bheader) {
      if (m_ctype >= PWSfileV3::HDR_LAST) {
        m_ukhxl.push_back(ukxfe);
/* #ifdef _DEBUG
        stringT cs_timestamp;
        cs_timestamp = PWSUtil::GetTimeStamp();
        TRACE(_T("%s: Header has unknown field: %02x, length %d/0x%04x, value:\n"),
        cs_timestamp, m_ctype, m_fieldlen, m_fieldlen);
        PWSDebug::HexDump(m_pfield, m_fieldlen, cs_timestamp);
#endif /* DEBUG */
      } else {
        m_bDatabaseHeaderErrors = true;
      }
    } else {
      if (m_ctype >= CItemData::LAST) {
        cur_entry->uhrxl.push_back(ukxfe);
      } else {
        m_bRecordHeaderErrors = true;
      }
    }
    trashMemory(m_pfield, m_fieldlen);
    delete[] m_pfield;
    m_pfield = NULL;
  }

  if (_tcscmp(szCurElement, _T("NumberHashIterations")) == 0) { 
    int i = _ttoi(m_strElemContent.c_str());
    if (i > MIN_HASH_ITERATIONS) {
      m_nITER = i;
    }
  }

  if (_tcscmp(szCurElement, _T("DisplayExpandedAddEditDlg")) == 0)
    m_bDisplayExpandedAddEditDlg = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("MaintainDateTimeStamps")) == 0)
    m_bMaintainDateTimeStamps = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWUseDigits")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.flags |= PWSprefs::PWPolicyUseDigits;
    else
      m_bPWUseDigits = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWUseEasyVision")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.flags |= PWSprefs::PWPolicyUseEasyVision;
    else
      m_bPWUseEasyVision = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWUseHexDigits")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.flags |= PWSprefs::PWPolicyUseHexDigits;
    else
      m_bPWUseHexDigits = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWUseLowercase")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.flags |= PWSprefs::PWPolicyUseLowercase;
    else
      m_bPWUseLowercase = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWUseSymbols")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.flags |= PWSprefs::PWPolicyUseSymbols;
    else
      m_bPWUseSymbols = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWUseUppercase")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.flags |= PWSprefs::PWPolicyUseUppercase;
    else
      m_bPWUseUppercase = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWMakePronounceable")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.flags |= PWSprefs::PWPolicyMakePronounceable;
    else
      m_bPWMakePronounceable = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("SaveImmediately")) == 0)
    m_bSaveImmediately = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("SavePasswordHistory")) == 0)
    m_bSavePasswordHistory = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("ShowNotesDefault")) == 0)
    m_bShowNotesDefault = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("ShowPWDefault")) == 0)
    m_bShowPWDefault = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("ShowPasswordInTree")) == 0)
    m_bShowPasswordInTree = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("ShowUsernameInTree")) == 0)
    m_bShowUsernameInTree = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("SortAscending")) == 0)
    m_bSortAscending = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("UseDefaultUser")) == 0)
    m_bUseDefaultUser = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWDefaultLength")) == 0)
    m_iPWDefaultLength = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("IdleTimeout")) == 0)
    m_iIdleTimeout = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("TreeDisplayStatusAtOpen")) == 0) {
    if (m_strElemContent == _T("AllCollapsed"))
      m_iTreeDisplayStatusAtOpen = PWSprefs::AllCollapsed;
    else if (m_strElemContent == _T("AllExpanded"))
      m_iTreeDisplayStatusAtOpen = PWSprefs::AllExpanded;
    else if (m_strElemContent == _T("AsPerLastSave"))
      m_iTreeDisplayStatusAtOpen = PWSprefs::AsPerLastSave;
  }

  if (_tcscmp(szCurElement, _T("NumPWHistoryDefault")) == 0)
    m_iNumPWHistoryDefault = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("DefaultUsername")) == 0)
    m_sDefaultUsername = m_strElemContent.c_str();

  if (_tcscmp(szCurElement, _T("DefaultAutotypeString")) == 0)
    m_sDefaultAutotypeString = m_strElemContent.c_str();

  if (_tcscmp(szCurElement, _T("PWLength")) == 0)
    cur_entry->pwp.length = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWDigitMinLength")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.digitminlength = _ttoi(m_strElemContent.c_str());
    else
      m_iPWDigitMinLength = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWLowercaseMinLength")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.lowerminlength = _ttoi(m_strElemContent.c_str());
    else
      m_iPWLowercaseMinLength = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWSymbolMinLength")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.symbolminlength = _ttoi(m_strElemContent.c_str());
    else
      m_iPWSymbolMinLength = _ttoi(m_strElemContent.c_str());

  if (_tcscmp(szCurElement, _T("PWUppercaseMinLength")) == 0)
    if (m_bentrybeingprocessed)
      cur_entry->pwp.upperminlength = _ttoi(m_strElemContent.c_str());
    else
      m_iPWUppercaseMinLength = _ttoi(m_strElemContent.c_str());

  return S_OK;
}

//  ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE  PWSSAXContentHandler::endDocument ( )
{
  return S_OK;
}

HRESULT STDMETHODCALLTYPE  PWSSAXContentHandler::startPrefixMapping (
  unsigned short * pwchPrefix,
  int cchPrefix,
  unsigned short * pwchUri,
  int cchUri )
{
  return S_OK;
}

HRESULT STDMETHODCALLTYPE  PWSSAXContentHandler::endPrefixMapping (
  unsigned short * pwchPrefix,
  int cchPrefix )
{
  return S_OK;
}

HRESULT STDMETHODCALLTYPE  PWSSAXContentHandler::ignorableWhitespace (
  unsigned short * pwchChars,
  int cchChars )
{
  return S_OK;
}

HRESULT STDMETHODCALLTYPE  PWSSAXContentHandler::processingInstruction (
  unsigned short * pwchTarget,
  int cchTarget,
  unsigned short * pwchData,
  int cchData )
{
  return S_OK;
}

HRESULT STDMETHODCALLTYPE  PWSSAXContentHandler::skippedEntity (
  unsigned short * pwchName,
  int cchName )
{
  return S_OK;
}
