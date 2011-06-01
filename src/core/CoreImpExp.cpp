/*
* Copyright (c) 2003-2011 Rony Shapiro <ronys@users.sourceforge.net>.
* All rights reserved. Use of the code is allowed under the
* Artistic License 2.0 terms, as specified in the LICENSE file
* distributed with this code, or available from
* http://www.opensource.org/licenses/artistic-license-2.0.php
*/
// file CoreImpExp.cpp
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------
// Import/Export PWScore member functions
//-----------------------------------------------------------------
#include "PWScore.h"
#include "core.h"
#include "PWSprefs.h"
#include "Util.h"
#include "SysInfo.h"
#include "UTF8Conv.h"
#include "Report.h"
#include "VerifyFormat.h"
#include "PWSfileV3.h" // XXX cleanup with dynamic_cast
#include "StringXStream.h"

#include "XML/XMLDefs.h"  // Required if testing "USE_XML_LIBRARY"

#include "os/typedefs.h"
#include "os/dir.h"
#include "os/debug.h"
#include "os/file.h"
#include "os/mem.h"

#if USE_XML_LIBRARY == MSXML
#include "XML/MSXML/MFileXMLProcessor.h"
#elif USE_XML_LIBRARY == XERCES
#include "XML/Xerces/XFileXMLProcessor.h"
#endif

#include <fstream> // for WritePlaintextFile
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include <set>

const TCHAR *EXPORTHEADER  = _T("Group/Title\tUsername\tPassword\tURL\tAutoType\tCreated Time\tPassword Modified Time\tLast Access Time\tPassword Expiry Date\tPassword Expiry Interval\tRecord Modified Time\tPassword Policy\tHistory\tRun Command\tDCA\te-mail\tProtected\tSymbols\tNotes");
const TCHAR *KPEXPORTHEADER  = _T("Password Groups\tGroup Tree\tAccount\tLogin Name\tPassword\tWeb Site\tComments\tUUID\tIcon\tCreation Time\tLast Access\tLast Modification\tExpires\tAttachment Description\tAttachment");
const TCHAR *KPIMPORTEDPREFIX = _T("ImportedKeePass");
  
using namespace std;
using pws_os::CUUID;

// hide w_char/char differences where possible:
#ifdef UNICODE
typedef std::wifstream ifstreamT;
typedef std::wofstream ofstreamT;
#else
typedef std::ifstream ifstreamT;
typedef std::ofstream ofstreamT;
#endif
typedef std::vector<stringT>::iterator viter;

struct ExportTester {
  ExportTester(const stringT &subgroup_name,
               const int &subgroup_object, const int &subgroup_function)
  :  m_subgroup_name(subgroup_name), m_subgroup_object(subgroup_object),
  m_subgroup_function(subgroup_function)
  {}

  // operator for ItemList
  bool operator()(pair<CUUID, CItemData> p)
  {return operator()(p.second);}

  // operator for OrderedItemList
  bool operator()(const CItemData &item) {
    return item.Matches(m_subgroup_name,
                        m_subgroup_object, m_subgroup_function);
  }

private:
  ExportTester& operator=(const ExportTester&); // Do not implement
  const stringT &m_subgroup_name;
  const int &m_subgroup_object;
  const int &m_subgroup_function;
};

int PWScore::TestSelection(const bool bAdvanced,
                           const stringT &subgroup_name,
                           const int &subgroup_object,
                           const int &subgroup_function,
                           const OrderedItemList *il)
{
  // Check if any pass restricting criteria
  if (bAdvanced) {
    bool bAnyMatch(false);
    if (il != NULL) {
      if (find_if(il->begin(), il->end(),
                  ExportTester(subgroup_name,
                               subgroup_object,
                               subgroup_function)) != il->end())
        bAnyMatch = true;
    } else {
      if (find_if(m_pwlist.begin(), m_pwlist.end(),
                  ExportTester(subgroup_name, 
                               subgroup_object, 
                               subgroup_function)) != m_pwlist.end())
        bAnyMatch = true;
    }

    if (!bAnyMatch)
      return FAILURE;
  } else {
    if (il != NULL)
      return (il->size() == 0) ? NO_ENTRIES_EXPORTED : SUCCESS;
    else
      return (m_pwlist.size() == 0) ? NO_ENTRIES_EXPORTED : SUCCESS;
  }
  return SUCCESS;
}

inline bool bittest(const CItemData::FieldBits &bsFields,
                    const CItemData::FieldType &ft,
                    const bool &bIncluded)
{
  return bsFields.test(ft) ? bIncluded : !bIncluded;
}

StringX PWScore::BuildHeader(const CItemData::FieldBits &bsFields, const bool bIncluded)
{
  // User chose fields, build custom header
  // Header fields MUST be in the same order as actual fields written
  // See CItemData::GetPlaintext for TextExport
  stringT hdr(_T(""));
  const stringT TAB(_T("\t"));
  if (bittest(bsFields, CItemData::GROUP, bIncluded) && 
      bittest(bsFields, CItemData::TITLE, bIncluded)) {
    hdr = CItemData::FieldName(CItemData::GROUPTITLE) + TAB;
  } else if (bittest(bsFields, CItemData::GROUP, bIncluded)) {
    hdr = CItemData::FieldName(CItemData::GROUP) + TAB;
  } else if (bittest(bsFields, CItemData::TITLE, bIncluded)) {
    hdr = CItemData::FieldName(CItemData::TITLE) + TAB;
  }
  if (bittest(bsFields, CItemData::USER, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::USER) + TAB;
  }
  if (bittest(bsFields, CItemData::PASSWORD, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::PASSWORD) + TAB;
  }
  if (bittest(bsFields, CItemData::URL, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::URL) + TAB;
  }
  if (bittest(bsFields, CItemData::AUTOTYPE, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::AUTOTYPE) + TAB;
  }
  if (bittest(bsFields, CItemData::CTIME, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::CTIME) + TAB;
  }
  if (bittest(bsFields, CItemData::PMTIME, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::PMTIME) + TAB;
  }
  if (bittest(bsFields, CItemData::ATIME, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::ATIME) + TAB;
  }
  if (bittest(bsFields, CItemData::XTIME, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::XTIME) + TAB;
  }
  if (bittest(bsFields, CItemData::XTIME_INT, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::XTIME_INT) + TAB;
  }
  if (bittest(bsFields, CItemData::RMTIME, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::RMTIME) + TAB;
  }
  if (bittest(bsFields, CItemData::POLICY, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::POLICY) + TAB;
  }
  if (bittest(bsFields, CItemData::PWHIST, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::PWHIST) + TAB;
  }
  if (bittest(bsFields, CItemData::RUNCMD, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::RUNCMD) + TAB;
  }
  if (bittest(bsFields, CItemData::DCA, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::DCA) + TAB;
  }
  if (bittest(bsFields, CItemData::EMAIL, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::EMAIL) + TAB;
  }
  if (bittest(bsFields, CItemData::PROTECTED, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::PROTECTED) + TAB;
  }
  if (bittest(bsFields, CItemData::SYMBOLS, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::SYMBOLS) + TAB;
  }
  if (bittest(bsFields, CItemData::NOTES, bIncluded)) {
    hdr += CItemData::FieldName(CItemData::NOTES);
  }
  if (!hdr.empty()) {
    size_t hdr_len = hdr.length();
    if (hdr[hdr_len - 1] == _T('\t')) {
      hdr_len--;
      hdr = hdr.substr(0, hdr_len);
    }
  }
  return hdr.c_str();
}

struct TextRecordWriter {
  TextRecordWriter(const stringT &subgroup_name,
          const int &subgroup_object, const int &subgroup_function,
          const CItemData::FieldBits &bsFields,
          const TCHAR &delimiter, coStringXStream &ofs, FILE * &txtfile,
          int &numExported, CReport *prpt, PWScore *pcore) :
  m_subgroup_name(subgroup_name), m_subgroup_object(subgroup_object),
  m_subgroup_function(subgroup_function), m_bsFields(bsFields),
  m_delimiter(delimiter), m_ofs(ofs), m_txtfile(txtfile), m_pcore(pcore),
  m_prpt(prpt), m_numExported(numExported)
  {}

  // operator for ItemList
  void operator()(pair<CUUID, CItemData> p)
  {operator()(p.second);}

  // operator for OrderedItemList
  void operator()(const CItemData &item) {
    if (m_subgroup_name.empty() || 
        item.Matches(m_subgroup_name, m_subgroup_object,
        m_subgroup_function)) {
      const CItemData *pcibase = m_pcore->GetBaseEntry(&item);
      const StringX line = item.GetPlaintext(TCHAR('\t'),
                                             m_bsFields, m_delimiter, pcibase);
      if (!line.empty()) {
        StringX sx_exported = StringX(L"\xab") + 
                             item.GetGroup() + StringX(L"\xbb \xab") + 
                             item.GetTitle() + StringX(L"\xbb \xab") +
                             item.GetUser()  + StringX(L"\xbb");

        if (m_prpt != NULL)
          m_prpt->WriteLine(sx_exported.c_str());
        m_pcore->UpdateWizard(sx_exported.c_str());

        CUTF8Conv conv; // can't make a member, as no copy c'tor!
        const unsigned char *utf8;
        size_t utf8Len;
        if (conv.ToUTF8(line, utf8, utf8Len)) {
          m_ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
          m_ofs << endl;
          m_numExported++;
        } else {
          ASSERT(0);
        }
        // Write what we have and reset the buffer
        size_t numwritten = fwrite(m_ofs.str().c_str(), 1, m_ofs.str().length(), m_txtfile);
        ASSERT(numwritten == m_ofs.str().length());
        m_ofs.str("");
      } // !line.IsEmpty()
    } // we've a match
  }

private:
  TextRecordWriter& operator=(const TextRecordWriter&); // Do not implement
  const stringT &m_subgroup_name;
  const int &m_subgroup_object;
  const int &m_subgroup_function;
  const CItemData::FieldBits &m_bsFields;
  const TCHAR &m_delimiter;
  coStringXStream &m_ofs;
  FILE * &m_txtfile;
  PWScore *m_pcore;
  CReport *m_prpt;
  int &m_numExported;
};

int PWScore::WritePlaintextFile(const StringX &filename,
                                const CItemData::FieldBits &bsFields,
                                const stringT &subgroup_name,
                                const int &subgroup_object,
                                const int &subgroup_function,
                                const TCHAR &delimiter, int &numExported, 
                                const OrderedItemList *il, CReport *prpt)
{
  numExported = 0;

  // Check if anything to do! 
  if (bsFields.count() == 0)
    return NO_ENTRIES_EXPORTED;

  // Although the MFC UI prevents the user selecting export of an
  // empty database, other UIs might not, so:
  if ((il != NULL && il->size() == 0) ||
      (il == NULL && m_pwlist.size() == 0))
    return NO_ENTRIES_EXPORTED;
 
  FILE *txtfile = pws_os::FOpen(filename.c_str(), _T("wt"));
  if (txtfile == NULL)
    return CANT_OPEN_FILE;

  CUTF8Conv conv;
  coStringXStream ofs;

  StringX hdr(_T(""));
  const unsigned char *utf8 = NULL;
  size_t utf8Len = 0;

  if (bsFields.count() == bsFields.size()) {
    // all fields to be exported, use pre-built header
    StringX exphdr = EXPORTHEADER;
    conv.ToUTF8(exphdr.c_str(), utf8, utf8Len);
    ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
    ofs << endl;
  } else {
    hdr = BuildHeader(bsFields, true);
    conv.ToUTF8(hdr, utf8, utf8Len);
    ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
    ofs << endl;
  }

  // Write what we have and reset the buffer
  fwrite(ofs.str().c_str(), 1, ofs.str().length(), txtfile);
  ofs.str("");

  TextRecordWriter put_text(subgroup_name, subgroup_object, subgroup_function,
                   bsFields, delimiter, ofs, txtfile, numExported, prpt, this);

  if (il != NULL) {
    for_each(il->begin(), il->end(), put_text);
  } else {
    for_each(m_pwlist.begin(), m_pwlist.end(), put_text);
  }

  // Close the file
  fclose(txtfile);

  return SUCCESS;
}

struct XMLRecordWriter {
  XMLRecordWriter(const stringT &subgroup_name,
                  const int subgroup_object, const int subgroup_function,
                  const CItemData::FieldBits &bsFields,
                  TCHAR delimiter, coStringXStream &ofs, FILE * &xmlfile,
                  int &numExported, CReport *prpt, PWScore *pcore) :
  m_subgroup_name(subgroup_name), m_subgroup_object(subgroup_object),
  m_subgroup_function(subgroup_function), m_bsFields(bsFields),
  m_delimiter(delimiter), m_ofs(ofs), m_xmlfile(xmlfile), m_id(0), m_pcore(pcore),
  m_numExported(numExported), m_prpt(prpt)
  {}

  // operator for ItemList
  void operator()(pair<CUUID, CItemData> p)
  {operator()(p.second);}

  // operator for OrderedItemList
  void operator()(const CItemData &item) {
    m_id++;
    if (m_subgroup_name.empty() ||
        item.Matches(m_subgroup_name,
                     m_subgroup_object, m_subgroup_function)) {
      StringX sx_exported = StringX(L"\xab") + 
                             item.GetGroup() + StringX(L"\xbb \xab") + 
                             item.GetTitle() + StringX(L"\xbb \xab") +
                             item.GetUser()  + StringX(L"\xbb");
      bool bforce_normal_entry(false);
      if (item.IsNormal()) {
        //  Check password doesn't incorrectly imply alias or shortcut entry
        StringX pswd;
        pswd = item.GetPassword();

        // Passwords are mandatory but, if missing, don't crash referencing character out of bounds!
        // Note: This value will not get to the XML file but the import will fail as the original entry
        // did not have a password and, as above, it is mandatory.
        if (pswd.length() == 0)
          pswd = _T("*MISSING*");

        int num_colons = Replace(pswd, _T(':'), _T(';')) + 1;
        if ((pswd.length() > 1 && pswd[0] == _T('[')) &&
            (pswd[pswd.length() - 1] == _T(']')) &&
            num_colons <= 3) {
          bforce_normal_entry = true;
        }
      }

      if (m_prpt != NULL)
        m_prpt->WriteLine(sx_exported.c_str());
      m_pcore->UpdateWizard(sx_exported.c_str());

      const CItemData *pcibase = m_pcore->GetBaseEntry(&item);
      string xml = item.GetXML(m_id, m_bsFields, m_delimiter, pcibase,
                               bforce_normal_entry);
      m_ofs.write(xml.c_str(),
                 static_cast<streamsize>(xml.length()));
      m_numExported++;
    }
    // Write what we have and reset the buffer
    size_t numwritten = fwrite(m_ofs.str().c_str(), 1, m_ofs.str().length(), m_xmlfile);
    ASSERT(numwritten == m_ofs.str().length());
    m_ofs.str("");
  }

private:
  XMLRecordWriter& operator=(const XMLRecordWriter&); // Do not implement
  const stringT &m_subgroup_name;
  const int m_subgroup_object;
  const int m_subgroup_function;
  const CItemData::FieldBits &m_bsFields;
  TCHAR m_delimiter;
  coStringXStream &m_ofs;
  FILE * &m_xmlfile;
  unsigned int m_id;
  PWScore *m_pcore;
  int &m_numExported;
  CReport *m_prpt;
};

int PWScore::WriteXMLFile(const StringX &filename,
                          const CItemData::FieldBits &bsFields,
                          const stringT &subgroup_name,
                          const int &subgroup_object, const int &subgroup_function,
                          const TCHAR &delimiter, int &numExported, const OrderedItemList *il,
                          const bool &bFilterActive, CReport *prpt)
{
  numExported = 0;

  // Although the MFC UI prevents the user selecting export of an
  // empty database, other UIs might not, so:
  if ((il != NULL && il->empty()) ||
      (il == NULL && m_pwlist.empty()))
    return NO_ENTRIES_EXPORTED;

  FILE *xmlfile = pws_os::FOpen(filename.c_str(), _T("wt"));
  if (xmlfile == NULL)
    return CANT_OPEN_FILE;

  CUTF8Conv conv;
  const unsigned char *utf8 = NULL;
  size_t utf8Len = 0;
  
  coStringXStream ofs;
  oStringXStream oss_xml;

  StringX pwh, tmp;
  stringT cs_temp;
  time_t time_now;

  time(&time_now);
  const StringX now = PWSUtil::ConvertToDateTimeString(time_now, TMC_XML);

  ofs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl;
  ofs << "<?xml-stylesheet type=\"text/xsl\" href=\"pwsafe.xsl\"?>" << endl;
  ofs << endl;
  ofs << "<passwordsafe" << endl;
  tmp = m_currfile;
  Replace(tmp, StringX(_T("&")), StringX(_T("&amp;")));

  StringX delStr;
  delStr += delimiter;
  conv.ToUTF8(delStr, utf8, utf8Len);
  ofs << "delimiter=\"";
  ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
  ofs << "\"" << endl;
  conv.ToUTF8(tmp, utf8, utf8Len);
  ofs << "Database=\"";
  ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
  ofs << "\"" << endl;
  conv.ToUTF8(now, utf8, utf8Len);
  ofs << "ExportTimeStamp=\"";
  ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
  ofs << "\"" << endl;
  ofs << "FromDatabaseFormat=\"";
  ostringstream osv; // take advantage of UTF-8 == ascii for version string
  osv << m_hdr.m_nCurrentMajorVersion
      << "." << setw(2) << setfill('0')
      << m_hdr.m_nCurrentMinorVersion;
  ofs.write(osv.str().c_str(), osv.str().length());
  ofs << "\"" << endl;
  if (!m_hdr.m_lastsavedby.empty() || !m_hdr.m_lastsavedon.empty()) {
    oStringXStream oss;
    oss << m_hdr.m_lastsavedby << _T(" on ") << m_hdr.m_lastsavedon;
    conv.ToUTF8(oss.str(), utf8, utf8Len);
    ofs << "WhoSaved=\"";
    ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
    ofs << "\"" << endl;
  }
  if (!m_hdr.m_whatlastsaved.empty()) {
    conv.ToUTF8(m_hdr.m_whatlastsaved, utf8, utf8Len);
    ofs << "WhatSaved=\"";
    ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
    ofs << "\"" << endl;
  }
  if (m_hdr.m_whenlastsaved != 0) {
    StringX wls = PWSUtil::ConvertToDateTimeString(m_hdr.m_whenlastsaved,
                                                   TMC_XML);
    conv.ToUTF8(wls.c_str(), utf8, utf8Len);
    ofs << "WhenLastSaved=\"";
    ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
    ofs << "\"" << endl;
  }

  CUUID huuid(*m_hdr.m_file_uuid.GetARep(), true); // true to print canoncally

  ofs << "Database_uuid=\"" << huuid << "\"" << endl;
  ofs << "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"" << endl;
  ofs << "xsi:noNamespaceSchemaLocation=\"pwsafe.xsd\">" << endl;
  ofs << endl;

  if (m_hdr.m_nITER > MIN_HASH_ITERATIONS) {
    ofs << "\t<NumberHashIterations>" << m_hdr.m_nITER << "</NumberHashIterations>";
    ofs << endl;
  }

  // Write what we have and reset the buffer
  fwrite(ofs.str().c_str(), 1, ofs.str().length(), xmlfile);
  ofs.str("");

  // write out preferences stored in database
  LoadAString(cs_temp, IDSC_XMLEXP_PREFERENCES);
  oss_xml << _T(" <!-- ") << cs_temp << _T(" --> ");
  conv.ToUTF8(oss_xml.str(), utf8, utf8Len);
  ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
  ofs << endl;
  oss_xml.str(_T(""));  // Clear buffer for next user

  stringT prefs = PWSprefs::GetInstance()->GetXMLPreferences();
  conv.ToUTF8(prefs.c_str(), utf8, utf8Len);
  ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);

  bool bStartComment(false);
  if (bFilterActive) {
    if (!bStartComment) {
      bStartComment = true;
      ofs << " <!-- " << endl;
    }
    LoadAString(cs_temp, IDSC_XMLEXP_FILTERACTIVE);
    oss_xml << _T("     ") << cs_temp;
    conv.ToUTF8(oss_xml.str(), utf8, utf8Len);
    ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
    ofs << endl;
    oss_xml.str(_T(""));  // Clear buffer for next user
  }

  if (!subgroup_name.empty() || bsFields.count() != bsFields.size()) {
    if (!bStartComment) {
      bStartComment = true;
      ofs << " <!-- " << endl;
    }
    // Some restrictions - put in a comment to that effect
    LoadAString(cs_temp, IDSC_XMLEXP_FLDRESTRICT);
    oss_xml << _T("     ") << cs_temp;
    conv.ToUTF8(oss_xml.str(), utf8, utf8Len);
    ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
    ofs << endl;
    oss_xml.str(_T(""));  // Clear buffer for next user

    if (!subgroup_name.empty()) {
      stringT cs_function, cs_case(_T(""));
      stringT cs_object = CItemData::EngFieldName(CItemData::FieldType(subgroup_object));

      int iCase(IDSC_CASE_INSENSITIVE);
      if (subgroup_function < 0) {
        iCase = IDSC_CASE_SENSITIVE;
      }

      LoadAString(cs_case, iCase);
      LoadAString(cs_function, PWSMatch::GetRule(PWSMatch::MatchRule(subgroup_function)));
      LoadAString(cs_temp, IDSC_XMLEXP_SUBSETACTIVE);

      if (!bStartComment) {
        bStartComment = true;
        ofs << " <!-- " << endl;
      }
      oss_xml << _T("     ") << cs_temp;
      conv.ToUTF8(oss_xml.str(), utf8, utf8Len);
      ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
      ofs << endl;
      oss_xml.str(_T(""));  // Clear buffer for next user

      oss_xml << _T("     ") << _T(" '") << cs_object     << _T("' ")
                             << _T(" '") << cs_function   << _T("' ")
                             << _T(" '") << subgroup_name << _T("' ")
                             << cs_case;
      conv.ToUTF8(oss_xml.str(), utf8, utf8Len);
      ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
      ofs << endl;
      oss_xml.str(_T(""));  // Clear buffer for next user
    }

    if (bsFields.count() != bsFields.size()) {
      if (!bStartComment) {
        bStartComment = true;
        ofs << " <!-- " << endl;
      }
      StringX hdr;
      LoadAString(cs_temp, IDSC_XMLEXP_SUBSETFIELDS);
      oss_xml << _T("     ") << cs_temp;
      conv.ToUTF8(oss_xml.str(), utf8, utf8Len);
      ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
      ofs << endl;
      oss_xml.str(_T(""));  // Clear buffer for next user

      hdr = BuildHeader(bsFields, false);
      size_t found = hdr.find(_T("\t"));
	    while (found != StringX::npos) {
		    if (found != StringX::npos) {
			    hdr.replace(found, 1, _T(", "));
		    }
		    found = hdr.find(_T("\t"));
	    }
      hdr = _T("     ") + hdr;
      conv.ToUTF8(hdr, utf8, utf8Len);
      ofs.write(reinterpret_cast<const char *>(utf8), utf8Len);
      ofs << endl;
    }
  }

  if (bStartComment) {
    bStartComment = false;
    ofs << " --> " << endl;
  }
  ofs << endl;

  // Write what we have and reset the buffer
  fwrite(ofs.str().c_str(), 1, ofs.str().length(), xmlfile);
  ofs.str("");

  XMLRecordWriter put_xml(subgroup_name, subgroup_object, subgroup_function,
                          bsFields, delimiter, ofs, xmlfile, numExported, prpt, this);

  if (il != NULL) {
    for_each(il->begin(), il->end(), put_xml);
  } else {
    for_each(m_pwlist.begin(), m_pwlist.end(), put_xml);
  }

  ofs << "</passwordsafe>" << endl;

  // Write what we have and reset the buffer, close the file
  fwrite(ofs.str().c_str(), 1, ofs.str().length(), xmlfile);
  ofs.str("");
  fclose(xmlfile);

 return SUCCESS;
}

#if !defined(USE_XML_LIBRARY) || (!defined(_WIN32) && USE_XML_LIBRARY == MSXML)
// Don't support importing XML on non-Windows platforms using Microsoft XML libraries
int PWScore::ImportXMLFile(const stringT &, const stringT &,
                           const stringT &, const bool &,
                           stringT &, stringT &, stringT &, stringT &,
                           int &, int &, int &, int &, int &,
                           CReport &, Command *&)
{
  return UNIMPLEMENTED;
}
#else
int PWScore::ImportXMLFile(const stringT &ImportedPrefix, const stringT &strXMLFileName,
                           const stringT &strXSDFileName, const bool &bImportPSWDsOnly,
                           stringT &strXMLErrors, stringT &strSkippedList,
                           stringT &strPWHErrorList, stringT &strRenameList, 
                           int &numValidated, int &numImported, int &numSkipped,
                           int &numPWHErrors, int &numRenamed, 
                           CReport &rpt, Command *&pcommand)
{
  UUIDVector Possible_Aliases, Possible_Shortcuts;
  MultiCommands *pmulticmds = MultiCommands::Create(this);
  pcommand = pmulticmds;

#if USE_XML_LIBRARY == MSXML
  MFileXMLProcessor iXML(this, &Possible_Aliases, &Possible_Shortcuts, pmulticmds, &rpt);
#elif USE_XML_LIBRARY == XERCES
  XFileXMLProcessor iXML(this, &Possible_Aliases, &Possible_Shortcuts, pmulticmds, &rpt);
#endif

  bool status, validation;
  int nITER(0);

  strXMLErrors = strPWHErrorList = strRenameList = _T("");

  validation = true;
  status = iXML.Process(validation, ImportedPrefix, strXMLFileName,
                        strXSDFileName, bImportPSWDsOnly, nITER);
  strXMLErrors = iXML.getXMLErrors();
  if (!status) {
    return XML_FAILED_VALIDATION;
  }
  numValidated = iXML.getNumEntriesValidated();

  validation = false;
  status = iXML.Process(validation, ImportedPrefix, strXMLFileName,
                        strXSDFileName, bImportPSWDsOnly, nITER);

  numImported = iXML.getNumEntriesImported();
  numSkipped = iXML.getNumEntriesSkipped();
  numRenamed = iXML.getNumEntriesRenamed();
  numPWHErrors = iXML.getNumEntriesPWHErrors();

  strXMLErrors = iXML.getXMLErrors();
  strSkippedList = iXML.getSkippedList();
  strRenameList = iXML.getRenameList();
  strPWHErrorList = iXML.getPWHErrorList();

  if (!status) {
    delete pcommand;
    pcommand = NULL;
    return XML_FAILED_IMPORT;
  }

  if (numImported > 0)
    SetDBChanged(true);

  return ((numRenamed + numPWHErrors) == 0) ? SUCCESS : OK_WITH_ERRORS;
}
#endif

static void ReportInvalidField(CReport &rpt, const string &value, int numlines)
{
  CUTF8Conv conv;
  StringX vx;
  conv.FromUTF8(reinterpret_cast<const unsigned char *>(value.c_str()), value.length(), vx);
  stringT cs_error;
  Format(cs_error, IDSC_IMPORTINVALIDFIELD, numlines, vx.c_str());
  rpt.WriteLine(cs_error);
}

int PWScore::ImportPlaintextFile(const StringX &ImportedPrefix,
                                 const StringX &filename,
                                 const TCHAR &fieldSeparator, const TCHAR &delimiter,
                                 const bool &bImportPSWDsOnly,
                                 stringT &strError,
                                 int &numImported, int &numSkipped,
                                 int &numPWHErrors, int &numRenamed,
                                 CReport &rpt, Command *&pcommand)
{
  stringT cs_error;
  CUTF8Conv conv;
  pcommand = NULL;

  // We need to use FOpen as the file name/file path may contain non-Latin
  // characters even though we need the file to contain ASCII and UTF-8 characters
  // and not Unicode (wchar_t).
  FILE *fs = pws_os::FOpen(filename.c_str(), _T("rt"));
  if (fs == NULL)
    return CANT_OPEN_FILE;

  // We need to use file stream I/O but can't with standard FILE I/O
  // so read in whole file and put it in a StringXStream (may contain sensitive data).
  // Hopefully, an import text file is not too large!
  const size_t IMPORT_BUFFER_SIZE = 4096;

  cStringXStream iss;
  unsigned char buffer[IMPORT_BUFFER_SIZE + 1];
  bool bError(false);
  size_t total(0);
  while(!feof(fs)) {
    size_t count = fread(buffer, 1, IMPORT_BUFFER_SIZE, fs);
    if (ferror(fs)) {
      bError = true;
      break;
    }
    buffer[count] = '\0';
    iss << buffer;
    total += count;
  }

  // Close the file
  fclose(fs);

  if (bError)
    return FAILURE;

  // The following's a stream of chars.  We need to process the header row 
  // as straight ASCII, and we need to handle rest as utf-8
  numImported = numSkipped = numRenamed = numPWHErrors = 0;
  int numlines = 0;

  CItemData ci_temp;
  vector<string> vs_Header;
  stringT cs_hdr = EXPORTHEADER;

  // Parse the header
  const unsigned char *hdr;
  size_t hdrlen;
  conv.ToUTF8(cs_hdr.c_str(), hdr, hdrlen);
  const string s_hdr(reinterpret_cast<const char *>(hdr));
  const char pTab[] = "\t";
  char pSeps[] = " ";

  // Order of fields determined in CItemData::GetPlaintext()
  enum Fields {GROUPTITLE, USER, PASSWORD, URL, AUTOTYPE,
               CTIME, PMTIME, ATIME, XTIME, XTIME_INT, RMTIME,
               POLICY, HISTORY, RUNCMD, DCA, EMAIL, PROTECTED, SYMBOLS, NOTES, 
               NUMFIELDS};

  int i_Offset[NUMFIELDS];
  for (int i = 0; i < NUMFIELDS; i++) {
    i_Offset[i] = -1;
  }

  pSeps[0] = static_cast<const char>(fieldSeparator);

  // Capture individual column titles:
  string::size_type to = 0, from;
  do {
    from = s_hdr.find_first_not_of(pTab, to);
    if (from == string::npos)
      break;
    to = s_hdr.find_first_of(pTab, from);
    vs_Header.push_back(s_hdr.substr(from,
                                     ((to == string::npos) ?
                                      string::npos : to - from)));
  } while (to != string::npos);

  // Following fails if a field was added in enum but not in
  // EXPORTHEADER, or vice versa.
  ASSERT(vs_Header.size() == NUMFIELDS);

  string s_header, linebuf;

  // Get header record
  if (!getline(iss, s_header, '\n')) {
    LoadAString(strError, IDSC_IMPORTNOHEADER);
    rpt.WriteLine(strError);
    return FAILURE;  // not even a title record!
  }

  // Capture individual column titles from s_header:
  // Set i_Offset[field] to column in which field is found in text file,
  // or leave at -1 if absent from text.
  unsigned num_found = 0;
  int itoken = 0;

  to = 0;
  do {
    from = s_header.find_first_not_of(pSeps, to);
    if (from == string::npos)
      break;
    to = s_header.find_first_of(pSeps, from);
    string token = s_header.substr(from,
                                   ((to == string::npos) ?
                                    string::npos : to - from));
    vector<string>::iterator it(std::find(vs_Header.begin(), vs_Header.end(), token));
    if (it != vs_Header.end()) {
      i_Offset[it - vs_Header.begin()] = itoken;
      num_found++;
    }
    itoken++;
  } while (to != string::npos);

  if (num_found == 0) {
    LoadAString(strError, IDSC_IMPORTNOCOLS);
    rpt.WriteLine(strError);
    return FAILURE;
  }

  // These are "must haves"!
  if (bImportPSWDsOnly &&
      (i_Offset[PASSWORD] == -1 || i_Offset[GROUPTITLE] == -1 ||
       i_Offset[USER] == -1)) {
    LoadAString(strError, IDSC_IMPORTPSWDNOCOLS);
    rpt.WriteLine(strError);
    return FAILURE;
  } else
  if (i_Offset[PASSWORD] == -1 || i_Offset[GROUPTITLE] == -1) {
    LoadAString(strError, IDSC_IMPORTMISSINGCOLS);
    rpt.WriteLine(strError);
    return FAILURE;
  }

  if (num_found < vs_Header.size()) {
    Format(cs_error, IDSC_IMPORTHDR, num_found);
    rpt.WriteLine(cs_error);
    LoadAString(cs_error, bImportPSWDsOnly ? IDSC_IMPORTKNOWNHDRS2 : IDSC_IMPORTKNOWNHDRS);
    rpt.WriteLine(cs_error, bImportPSWDsOnly);
    for (int i = 0; i < NUMFIELDS; i++) {
      if (i_Offset[i] >= 0) {
        const string &sHdr = vs_Header.at(i);
        StringX sh2;
        conv.FromUTF8(reinterpret_cast<const unsigned char *>(sHdr.c_str()), sHdr.length(), sh2);
        Format(cs_error, _T(" %s,"), sh2.c_str());
        rpt.WriteLine(cs_error, false);
      }
    }
    rpt.WriteLine();
    rpt.WriteLine();
  }

  bool bMaintainDateTimeStamps = PWSprefs::GetInstance()->
              GetPref(PWSprefs::MaintainDateTimeStamps);
  bool bIntoEmpty = m_pwlist.size() == 0;

  UUIDVector Possible_Aliases, Possible_Shortcuts;

  MultiCommands *pmulticmds = MultiCommands::Create(this);
  pcommand = pmulticmds;
  Command *pcmd1 = UpdateGUICommand::Create(this, UpdateGUICommand::WN_UNDO,
                                            UpdateGUICommand::GUI_UNDO_IMPORT);
  pmulticmds->Add(pcmd1);

  // Finished parsing header, go get the data!
  // Initialize set
  GTUSet setGTU;
  InitialiseGTU(setGTU);

  for (;;) {
    // read a single line.
    if (!getline(iss, linebuf, '\n')) break;
    numlines++;

    // remove MS-DOS linebreaks, if needed.
    if (!linebuf.empty() && *(linebuf.end() - 1) == '\r') {
      linebuf.resize(linebuf.size() - 1);
    }

    // skip blank lines
    if (linebuf.empty()) {
      Format(cs_error, IDSC_IMPORTEMPTYLINESKIPPED, numlines);
      rpt.WriteLine(cs_error);
      numSkipped++;
      continue;
    }

    // convert linebuf from UTF-8 to StringX
    StringX slinebuf;
    if (!conv.FromUTF8(reinterpret_cast<const unsigned char *>(linebuf.c_str()),
                       linebuf.length(), slinebuf)) {
      // XXX add an appropriate error message
      numSkipped++;
      continue;
    }

    // tokenize into separate elements
    itoken = 0;
    vector<stringT> tokens;
    for (size_t startpos = 0;
         startpos < slinebuf.size(); 
         /* startpos advanced in body */) {
      size_t nextchar = slinebuf.find_first_of(fieldSeparator, startpos);
      if (nextchar == StringX::npos)
        nextchar = slinebuf.size();
      if (nextchar > 0) {
        if (itoken != i_Offset[NOTES]) {
          const StringX tsx(slinebuf.substr(startpos, nextchar - startpos));
          tokens.push_back(tsx.c_str());
        } else { 
          // Notes field which may be double-quoted, and
          // if they are, they may span more than one line.
          stringT note(slinebuf.substr(startpos).c_str());
          size_t first_quote = note.find_first_of('\"');
          size_t last_quote = note.find_last_of('\"');
          if (first_quote == last_quote && first_quote != stringT::npos) {
            //there was exactly one quote, meaning that we've a multi-line Note
            bool noteClosed = false;
            do {
              if (!getline(iss, linebuf, '\n')) {
                Format(cs_error, IDSC_IMPMISSINGQUOTE, numlines);
                rpt.WriteLine(cs_error);
                return (numImported > 0) ? SUCCESS : INVALID_FORMAT;
              }
              numlines++;
              // remove MS-DOS linebreaks, if needed.
              if (!linebuf.empty() && *(linebuf.end() - 1) == TCHAR('\r')) {
                linebuf.resize(linebuf.size() - 1);
              }
              note += _T("\r\n");
              if (!conv.FromUTF8(reinterpret_cast<const unsigned char *>(linebuf.c_str()),
                                 linebuf.length(), slinebuf)) {
                // XXX add an appropriate error message
                numSkipped++;
                continue;
              }
              note += slinebuf.c_str();
              size_t fq = linebuf.find_first_of('\"');
              size_t lq = linebuf.find_last_of('\"');
              noteClosed = (fq == lq && fq != string::npos);
            } while (!noteClosed);
          } // multiline note processed
          tokens.push_back(note);
          break;
        } // Notes handling
      } // nextchar > 0
      startpos = nextchar + 1; // too complex for the 'for statement'
      itoken++;
    } // tokenization for loop

    // Sanity check
    if (tokens.size() < num_found) {
      Format(cs_error, IDSC_IMPORTLINESKIPPED, numlines, tokens.size(), num_found);
      rpt.WriteLine(cs_error);
      numSkipped++;
      continue;
    }

    const TCHAR *tc_whitespace = _T(" \t\r\n\f\v");
    // Make fields that are *only* whitespace = empty
    viter tokenIter;
    for (tokenIter = tokens.begin(); tokenIter != tokens.end(); tokenIter++) {
      const vector<stringT>::size_type len = tokenIter->length();

      // Don't bother if already empty
      if (len == 0)
        continue;

      // Dequote if: value big enough to have opening and closing quotes
      // (len >=2) and the first and last characters are doublequotes.
      // UNLESS there's at least one quote in the text itself
      if (len > 1 && (*tokenIter)[0] == _T('\"') && (*tokenIter)[len - 1] == _T('\"')) {
        const stringT dequoted = tokenIter->substr(1, len - 2);
        if (dequoted.find_first_of(_T('\"')) == stringT::npos)
          tokenIter->assign(dequoted);
      }

      // Empty field if purely whitespace
      if (tokenIter->find_first_not_of(tc_whitespace) == stringT::npos) {
        tokenIter->clear();
      }
    } // loop over tokens

    if (static_cast<size_t>(i_Offset[PASSWORD]) >= tokens.size() ||
        tokens[i_Offset[PASSWORD]].empty()) {
      Format(cs_error, IDSC_IMPORTNOPASSWORD, numlines);
      rpt.WriteLine(cs_error);
      numSkipped++;
      continue;
    }

    if (bImportPSWDsOnly) {
      StringX sxgroup(_T("")), sxtitle, sxuser;
      const stringT &grouptitle = tokens[i_Offset[GROUPTITLE]];
      stringT entrytitle;
      size_t lastdot = grouptitle.find_last_of(TCHAR('.'));
      if (lastdot != stringT::npos) {
        sxgroup = grouptitle.substr(0, lastdot).c_str();
        sxtitle = grouptitle.substr(lastdot + 1).c_str();
      } else {
        sxtitle = grouptitle.c_str();
      }

      if (sxtitle.empty()) {
        Format(cs_error, IDSC_IMPORTNOTITLE, numlines);
        rpt.WriteLine(cs_error);
        numSkipped++;
        continue;
      }

      if (tokens[i_Offset[PASSWORD]].empty()) {
        Format(cs_error, IDSC_IMPORTNOPASSWORD, numlines);
        rpt.WriteLine(cs_error);
        numSkipped++;
        continue;
      }
 
      sxuser = tokens[i_Offset[USER]].c_str();
      ItemListIter iter = Find(sxgroup, sxtitle, sxuser);
      if (iter == m_pwlist.end()) {
        stringT cs_online, cs_temp;
        LoadAString(cs_online, IDSC_IMPORT_ON_LINE);
        Format(cs_temp, IDSC_IMPORTENTRY, cs_online.c_str(), numlines, 
               sxgroup.c_str(), sxtitle.c_str(), sxuser.c_str());
        Format(cs_error, IDSC_IMPORTRECNOTFOUND, cs_temp.c_str());
        rpt.WriteLine(cs_error);
        numSkipped++;
      } else {
        CItemData *pci = &iter->second;
        Command *pcmd = UpdatePasswordCommand::Create(this, *pci,
                                                      tokens[i_Offset[PASSWORD]].c_str());
        pcmd->SetNoGUINotify();
        pmulticmds->Add(pcmd);
        if (bMaintainDateTimeStamps) {
          pci->SetATime();
        }
        numImported++;
      }
      continue;
    }

    // Start initializing the new record.
    ci_temp.Clear();
    ci_temp.CreateUUID();
    if (i_Offset[USER] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[USER]))
      ci_temp.SetUser(tokens[i_Offset[USER]].c_str());
    StringX csPassword = tokens[i_Offset[PASSWORD]].c_str();
    ci_temp.SetPassword(csPassword);

    // The group and title field are concatenated.
    // If the title field has periods, then they have been changed to the delimiter
    const stringT &grouptitle = tokens[i_Offset[GROUPTITLE]];
    stringT entrytitle;
    size_t lastdot = grouptitle.find_last_of(TCHAR('.'));
    if (lastdot != stringT::npos) {
      StringX newgroup(ImportedPrefix.empty() ?
                         _T("") : ImportedPrefix + _T("."));
      newgroup += grouptitle.substr(0, lastdot).c_str();
      ci_temp.SetGroup(newgroup);
      entrytitle = grouptitle.substr(lastdot + 1);
    } else {
      ci_temp.SetGroup(ImportedPrefix);
      entrytitle = grouptitle;
    }

    std::replace(entrytitle.begin(), entrytitle.end(), delimiter, TCHAR('.'));
    if (entrytitle.empty()) {
      Format(cs_error, IDSC_IMPORTNOTITLE, numlines);
      rpt.WriteLine(cs_error);
      numSkipped++;
      continue;
    }

    ci_temp.SetTitle(entrytitle.c_str());

    // Now make sure it is unique
    const StringX sx_group = ci_temp.GetGroup();
    StringX sx_title = ci_temp.GetTitle();
    const StringX sx_user = ci_temp.GetUser();
    StringX sxnewtitle(sx_title);
    bool conflict = !MakeEntryUnique(setGTU, sx_group, sxnewtitle, sx_user, IDSC_IMPORTNUMBER);
    if (conflict) {
      ci_temp.SetTitle(sxnewtitle);
      stringT cs_header;
      if (sx_group.empty())
        Format(cs_header, IDSC_IMPORTCONFLICTS2, numlines);
      else
        Format(cs_header, IDSC_IMPORTCONFLICTS1, numlines, sx_group.c_str());

      Format(cs_error, IDSC_IMPORTCONFLICTS0, cs_header.c_str(),
               sx_title.c_str(), sx_user.c_str(), sxnewtitle.c_str());
      rpt.WriteLine(cs_error);
      sx_title = sxnewtitle;
      numRenamed++;
    }

    if (i_Offset[URL] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[URL]))
      ci_temp.SetURL(tokens[i_Offset[URL]].c_str());
    if (i_Offset[AUTOTYPE] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[AUTOTYPE]))
      ci_temp.SetAutoType(tokens[i_Offset[AUTOTYPE]].c_str());
    if (i_Offset[CTIME] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[CTIME]))
      if (!ci_temp.SetCTime(tokens[i_Offset[CTIME]].c_str()))
        ReportInvalidField(rpt, vs_Header.at(CTIME), numlines);
    if (i_Offset[PMTIME] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[PMTIME]))
      if (!ci_temp.SetPMTime(tokens[i_Offset[PMTIME]].c_str()))
        ReportInvalidField(rpt, vs_Header.at(PMTIME), numlines);
    if (i_Offset[ATIME] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[ATIME]))
      if (!ci_temp.SetATime(tokens[i_Offset[ATIME]].c_str()))
        ReportInvalidField(rpt, vs_Header.at(ATIME), numlines);
    if (i_Offset[XTIME] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[XTIME]))
      if (!ci_temp.SetXTime(tokens[i_Offset[XTIME]].c_str()))
        ReportInvalidField(rpt, vs_Header.at(XTIME), numlines);
    if (i_Offset[XTIME_INT] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[XTIME_INT]))
      if (!ci_temp.SetXTimeInt(tokens[i_Offset[XTIME_INT]].c_str()))
        ReportInvalidField(rpt, vs_Header.at(XTIME_INT), numlines);
    if (i_Offset[RMTIME] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[RMTIME]))
      if (!ci_temp.SetRMTime(tokens[i_Offset[RMTIME]].c_str()))
        ReportInvalidField(rpt, vs_Header.at(RMTIME), numlines);
    if (i_Offset[POLICY] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[POLICY]))
      if (!ci_temp.SetPWPolicy(tokens[i_Offset[POLICY]].c_str()))
        ReportInvalidField(rpt, vs_Header.at(POLICY), numlines);
    if (i_Offset[HISTORY] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[HISTORY])) {
      StringX newPWHistory;
      stringT strPWHErrorList;
      Format(cs_error, IDSC_IMPINVALIDPWH, numlines);
      switch (VerifyImportPWHistoryString(tokens[i_Offset[HISTORY]].c_str(),
                                          newPWHistory, strPWHErrorList)) {
        case PWH_OK:
          ci_temp.SetPWHistory(newPWHistory.c_str());
          break;
        case PWH_IGNORE:
          break;
        case PWH_INVALID_HDR:
        case PWH_INVALID_STATUS:
        case PWH_INVALID_NUM:
        case PWH_INVALID_DATETIME:
        case PWH_INVALID_PSWD_LENGTH:
        case PWH_TOO_SHORT:
        case PWH_TOO_LONG:
        case PWH_INVALID_CHARACTER:
        default:
          rpt.WriteLine(cs_error, false);
          rpt.WriteLine(strPWHErrorList, false);
          LoadAString(cs_error, IDSC_PWHISTORYSKIPPED);
          rpt.WriteLine(cs_error);
          numPWHErrors++;
          break;
      }
    }
    if (i_Offset[RUNCMD] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[RUNCMD]))
      ci_temp.SetRunCommand(tokens[i_Offset[RUNCMD]].c_str());
    if (i_Offset[DCA] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[DCA]))
      ci_temp.SetDCA(tokens[i_Offset[DCA]].c_str());
    if (i_Offset[EMAIL] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[EMAIL]))
      ci_temp.SetEmail(tokens[i_Offset[EMAIL]].c_str());
    if (i_Offset[PROTECTED] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[PROTECTED]))
      if (tokens[i_Offset[PROTECTED]].compare(_T("Y")) == 0 || tokens[i_Offset[PROTECTED]].compare(_T("1")) == 0)
        ci_temp.SetProtected(true);
    if (i_Offset[SYMBOLS] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[SYMBOLS]))
      ci_temp.SetSymbols(tokens[i_Offset[SYMBOLS]].c_str());

    // The notes field begins and ends with a double-quote, with
    // replacement of delimiter by CR-LF.
    if (i_Offset[NOTES] >= 0 && tokens.size() > static_cast<size_t>(i_Offset[NOTES])) {
      stringT quotedNotes = tokens[i_Offset[NOTES]];
      if (!quotedNotes.empty()) {
        if (*quotedNotes.begin() == TCHAR('\"') &&
            *(quotedNotes.end() - 1) == TCHAR('\"')) {
          quotedNotes = quotedNotes.substr(1, quotedNotes.size() - 2);
        }
        size_t frompos = 0, pos;
        stringT fixedNotes;
        while (stringT::npos != (pos = quotedNotes.find(delimiter, frompos))) {
          fixedNotes += quotedNotes.substr(frompos, (pos - frompos));
          fixedNotes += _T("\r\n");
          frompos = pos + 1;
        }
        fixedNotes += quotedNotes.substr(frompos);
        ci_temp.SetNotes(fixedNotes.c_str());
      }
    }

    if (Replace(csPassword, _T(':'), _T(';')) <= 2) {
      if (csPassword.substr(0, 2) == _T("[[") &&
          csPassword.substr(csPassword.length() - 2) == _T("]]")) {
        Possible_Aliases.push_back(ci_temp.GetUUID());
      }
      if (csPassword.substr(0, 2) == _T("[~") &&
          csPassword.substr(csPassword.length() - 2) == _T("~]")) {
        Possible_Shortcuts.push_back(ci_temp.GetUUID());
      }
    }

    if (!bIntoEmpty) {
      ci_temp.SetStatus(CItemData::ES_ADDED);
    }

    // Get GUI to populate its field
    GUISetupDisplayInfo(ci_temp);

    // Add to commands to execute
    Command *pcmd = AddEntryCommand::Create(this, ci_temp);
    pcmd->SetNoGUINotify();
    pmulticmds->Add(pcmd);
    numImported++;

    StringX sx_imported = StringX(L"\xab") + 
                             sx_group + StringX(L"\xbb \xab") + 
                             sx_title + StringX(L"\xbb \xab") +
                             sx_user  + StringX(L"\xbb");
    rpt.WriteLine(sx_imported.c_str());
  } // file processing for (;;) loop

  Command *pcmdA = AddDependentEntriesCommand::Create(this,
                                                      Possible_Aliases, &rpt, 
                                                      CItemData::ET_ALIAS,
                                                      CItemData::PASSWORD);
  pcmdA->SetNoGUINotify();
  pmulticmds->Add(pcmdA);
  Command *pcmdS = AddDependentEntriesCommand::Create(this,
                                                      Possible_Shortcuts, &rpt, 
                                                      CItemData::ET_SHORTCUT,
                                                      CItemData::PASSWORD);
  pcmdS->SetNoGUINotify();
  pmulticmds->Add(pcmdS);
  Command *pcmd2 = UpdateGUICommand::Create(this, UpdateGUICommand::WN_EXECUTE_REDO,
                                            UpdateGUICommand::GUI_REDO_IMPORT);
  pmulticmds->Add(pcmd2);

  if (numImported > 0)
    SetDBChanged(true);

  return ((numSkipped + numRenamed + numPWHErrors)) == 0 ? SUCCESS : OK_WITH_ERRORS;
}

int PWScore::ImportKeePassV1TXTFile(const StringX &filename,
                                    int &numImported, int &numSkipped, int &numRenamed,
                                    CReport &rpt, Command *&pcommand)
{

  /*
  The format of the source file is from doing an export to TXT file in Keepass.

  The checkbox "Encode/replace newline characters by '\n'" MUST be selected bu the
  user during the export or this import will fail and may give unexpected results.

  The line that starts with '[' and ends with ']' is equivalant to the Title field.

  The following entries are supported:
     "Group: ", "Group Tree: ", "User Name: ", "URL: ", "Password: ",
     "Notes: ", "UUID: ", "Creation Time: ", "Last Access: ",
     "Last Modification: " or "Expires: "
  The following entries are ignored:
      "Attachment: ", Attachment Description: " & "Icon"

  */

  stringT cs_error;
  CUTF8Conv conv;
  pcommand = NULL;
  numImported  = numSkipped = numRenamed = 0;

  // We need to use FOpen as the file name/file path may contain non-Latin
  // characters even though we need the file to contain ASCII and UTF-8 characters
  // and not Unicode (wchar_t).
  FILE *fs = pws_os::FOpen(filename.c_str(), _T("rt"));
  if (fs == NULL)
    return CANT_OPEN_FILE;

  // We need to use file stream I/O but can't with standard FILE I/O
  // so read in whole file and put it in a StringXStream (may contain sensitive data).
  // Hopefully, an import text file is not too large!
  const size_t IMPORT_BUFFER_SIZE = 4096;

  cStringXStream iss;
  unsigned char buffer[IMPORT_BUFFER_SIZE + 1];
  bool bError(false);
  size_t total(0);
  while(!feof(fs)) {
    size_t count = fread(buffer, 1, IMPORT_BUFFER_SIZE, fs);
    if (ferror(fs)) {
      bError = true;
      break;
    }
    buffer[count] = '\0';
    iss << buffer;
    total += count;
  }

  // Close the file
  fclose(fs);

  if (bError)
    return FAILURE;

  string linebuf;
  time_t ctime, atime, mtime, xtime;
  uuid_array_t ua;

  int numlines(0);
  const StringX fwdslash = _T("/");
  const StringX dot = _T(".");
  const StringX txtCRLF = _T("\\r\\n");
  const StringX CRLF = _T("\r\n");

  MultiCommands *pmulticmds = MultiCommands::Create(this);
  pcommand = pmulticmds;
  Command *pcmd1 = UpdateGUICommand::Create(this, UpdateGUICommand::WN_UNDO,
                                            UpdateGUICommand::GUI_UNDO_IMPORT);
  pmulticmds->Add(pcmd1);

  bool bFirst(true);

  // Initialize set
  GTUSet setGTU;
  InitialiseGTU(setGTU);
  UUIDSet setUUID;
  InitialiseUUID(setUUID);

  for (;;) {
    // Clear out old stuff!
    StringX sx_group, sx_title, sx_user, sx_password, sx_URL, sx_notes, sx_Parent_Groups;
    string str_uuid, temp;
    ctime = atime = mtime = xtime = (time_t)0;
    memset(ua, 0, sizeof(ua));

    // read a single line.
    getline(iss, linebuf, '\n');

    // Check if end of file
    if (iss.eof())
      break;

    // Check if blank line
    if (linebuf.empty())
      continue;

    // the first line of the Keepass text file contains BOM characters
    if (bFirst && linebuf.length() > 3) {
      if((static_cast<unsigned char>(linebuf[0]) == 0xEF) &&
         (static_cast<unsigned char>(linebuf[1]) == 0xBB) &&
         (static_cast<unsigned char>(linebuf[2]) == 0xBF)) {
        // Remove BOM
        linebuf = linebuf.erase(0, 3);
      }
      bFirst = false;
    }

    // this line should always be a title contained in []'s
    if (*(linebuf.begin()) != '[' || *(linebuf.end() - 1) != ']') {
      return INVALID_FORMAT;
    }

    // set the title: line pattern: [<title>]
    temp = linebuf.substr(linebuf.find("[") + 1, linebuf.rfind("]") - 1).c_str();
    conv.FromUTF8((unsigned char *)temp.c_str(),  temp.length(), sx_title);

    bool bTitleFound(false);
    for (;;) {
      streamoff currentpos = iss.tellg();
      getline(iss, linebuf, '\n');
      // Check if blank line
      if (linebuf.empty())
        break;

      // Check if new entry
      if (*(linebuf.begin()) == '[' && *(linebuf.end() - 1) == ']') {
        // Ooops - go back
        iss.seekg(currentpos);
        bTitleFound = true;
        break;
      }

      if (linebuf.substr(0, 7) == "Group: ") {
        // set the initial group: line pattern: Group: <group>
        temp = linebuf.substr(7);
        conv.FromUTF8((unsigned char *)temp.c_str(), temp.length(), sx_group);

        // Replace any '.' by a '/'
        size_t pos = 0;
        while((pos = sx_group.find(dot, pos)) != StringX::npos) {
          sx_group.replace(pos, 1, fwdslash);
          pos++;
        }
      }

      else if (linebuf.substr(0, 12) == "Group Tree: ") {
        // set the parent groups str_parent_groups
        // Groups are in order separated by '\'
        // Groups with a '\' have it changed to a '/'
        // Groups with a '/' are unchanged
        // Group names can have dots in them and these will be replaced by a '/'

        temp = linebuf.substr(12);
        conv.FromUTF8((unsigned char *)temp.c_str(), temp.length(), sx_Parent_Groups);
        // Replace and '.' by a '/'
        size_t pos = 0;
        while((pos = sx_Parent_Groups.find(dot, pos)) != StringX::npos) {
          sx_Parent_Groups.replace(pos, 1, fwdslash);
          pos++;
        }

        // Tokenize by '\'
        vector<StringX> pgs;
        StringXStream ss(sx_Parent_Groups.c_str());
        StringX item;
        while (getline(ss, item, _T('\\'))) {
          pgs.push_back(item);
        }
        // Construct the parent groups
        sx_Parent_Groups.clear();
        for (size_t i = 0; i < pgs.size(); i++) {
          sx_Parent_Groups = sx_Parent_Groups + pgs[i] + dot;
        }
      }

      else if (linebuf.substr(0, 11) == "User Name: ") {
        // set the user: line pattern: UserName: <user>
        temp = linebuf.substr(11);
        conv.FromUTF8((unsigned char *)temp.c_str(), temp.length(), sx_user);
      }

      else if (linebuf.substr(0, 5) == "URL: ") {
        // set the url: line pattern: URL: <url>
        temp = linebuf.substr(5);
        conv.FromUTF8((unsigned char *)temp.c_str(), temp.length(), sx_URL);
      }
      
      else if (linebuf.substr(0, 10) == "Password: ") {
        // set the password: line pattern: Password: <passwd>
        temp = linebuf.substr(10);
        conv.FromUTF8((unsigned char *)temp.c_str(), temp.length(), sx_password);
      }

      else if (linebuf.substr(0, 6) == "UUID: ") {
        str_uuid = linebuf.substr(6);
        // Verify it is the correct length (should be or the schema is wrong!)
        if (str_uuid.length() == sizeof(uuid_array_t) * 2) {
          unsigned int x(0);
          for (size_t i = 0; i < sizeof(uuid_array_t); i++) {
            stringstream ss;
            ss.str(str_uuid.substr(i * 2, 2));
            ss >> hex >> x;
            ua[i] = static_cast<unsigned char>(x);
          }
        } else
          str_uuid.clear();
      }

      else if (linebuf.substr(0, 15) == "Creation Time: ") {
        string str_ctime = linebuf.substr(15);
        if (str_ctime.length() != 19)
          continue;
        str_ctime.replace(10, 1, "T");
#ifdef UNICODE
        std::wstring temp(str_ctime.length(),L' ');
        std::copy(str_ctime.begin(), str_ctime.end(), temp.begin());
#else
        std::string temp = str_ctime;
#endif
        VerifyXMLDateTimeString(temp, ctime);
      }

      else if (linebuf.substr(0, 13) == "Last Access: ") {
        string str_atime = linebuf.substr(13);
        if (str_atime.length() != 19)
          continue;
        str_atime.replace(10, 1, "T");
#ifdef UNICODE
        std::wstring temp(str_atime.length(),L' ');
        std::copy(str_atime.begin(), str_atime.end(), temp.begin());
#else
        std::string temp = str_ctime;
#endif
        VerifyXMLDateTimeString(temp, atime);
      }

      else if (linebuf.substr(0, 19) == "Last Modification: ") {
        string str_mtime = linebuf.substr(19);
        if (str_mtime.length() != 19)
          continue;
        str_mtime.replace(10, 1, "T");
#ifdef UNICODE
        std::wstring temp(str_mtime.length(),L' ');
        std::copy(str_mtime.begin(), str_mtime.end(), temp.begin());
#else
        std::string temp = str_ctime;
#endif
        VerifyXMLDateTimeString(temp, mtime);
      }

      else if (linebuf.substr(0, 9) == "Expires: ") {
        string str_xtime = linebuf.substr(9);
        if (str_xtime.length() != 19)
          continue;
        str_xtime.replace(10, 1, "T");
#ifdef UNICODE
        std::wstring temp(str_xtime.length(),L' ');
        std::copy(str_xtime.begin(), str_xtime.end(), temp.begin());
#else
        std::string temp = str_ctime;
#endif
        VerifyXMLDateTimeString(temp, xtime);
      }

      // set the first line of notes: line pattern: Notes: <notes>
      else if (linebuf.substr(0, 7) == "Notes: ") {
        temp = linebuf.substr(7);
        conv.FromUTF8((unsigned char *)temp.c_str(), temp.length(), sx_notes);
        size_t pos = 0;
        while((pos = sx_notes.find(txtCRLF, pos)) != StringX::npos) {
          sx_notes.replace(pos, txtCRLF.length(), CRLF);
          pos += CRLF.length();
        }
      }

      // Ignore any other text lines e.g. GroupTree & Icon, 
      else
      if (linebuf.substr(0, 24) == "Attachment Description: " ||
          linebuf.substr(0, 12) == "Attachment: " ||
          linebuf.substr(0, 6)  == "Icon: ") {
        continue;
      }

      if (bTitleFound)
        break;
    }

    // set up the group tree
    if (!sx_Parent_Groups.empty())
      sx_group = sx_Parent_Groups + sx_group;
    if (!sx_group.empty())
      sx_group = KPIMPORTEDPREFIX + dot + sx_group;
    else
      sx_group = KPIMPORTEDPREFIX;

    // Create & append the new record.
    CItemData ci_temp;
    bool bNewUUID(true);
    if (!str_uuid.empty()) {
      const CUUID uuid(ua);
      if (uuid != CUUID::NullUUID()) {
        UUIDSetPair pr_uuid = setUUID.insert(uuid);
        if (pr_uuid.second) {
          ci_temp.SetUUID(uuid);
          bNewUUID = false;
        }
      }
    }

    if (bNewUUID)
      ci_temp.CreateUUID();

    if (!sx_group.empty())
      ci_temp.SetGroup(sx_group);
    ci_temp.SetTitle(sx_title.empty() ? _T("Unknown") : sx_title);
    if (!sx_user.empty())
      ci_temp.SetUser(sx_user);
    ci_temp.SetPassword(sx_password.empty() ? _T("Unknown") : sx_password);
    if (!sx_notes.empty())
      ci_temp.SetNotes(sx_notes.c_str());
    if (!sx_URL.empty())
      ci_temp.SetURL(sx_URL);
    if (ctime > 0)
      ci_temp.SetCTime(ctime);
    if (atime > 0)
      ci_temp.SetATime(atime);
    if (mtime > 0) {
      ci_temp.SetPMTime(mtime);
      ci_temp.SetRMTime(mtime);
    }
    if (xtime > 0)
      ci_temp.SetXTime(xtime);

    // Now make sure it is unique
    StringX sxnewtitle(sx_title);
    bool conflict = !MakeEntryUnique(setGTU, sx_group, sxnewtitle, sx_user, IDSC_IMPORTNUMBER);
    if (conflict) {
      ci_temp.SetTitle(sxnewtitle);
      stringT cs_header;
      if (sx_group.empty())
        Format(cs_header, IDSC_IMPORTCONFLICTS2, numlines);
      else
        Format(cs_header, IDSC_IMPORTCONFLICTS1, numlines, sx_group.c_str());

      Format(cs_error, IDSC_IMPORTCONFLICTS0, cs_header.c_str(),
               sx_title.c_str(), sx_user.c_str(), sxnewtitle.c_str());
      rpt.WriteLine(cs_error);
      numRenamed++;
      sx_title = sxnewtitle;
    }

    ci_temp.SetStatus(CItemData::ES_ADDED);

    GUISetupDisplayInfo(ci_temp);
    Command *pcmd = AddEntryCommand::Create(this, ci_temp);
    pcmd->SetNoGUINotify();
    pmulticmds->Add(pcmd);
    numImported++;

    StringX sx_imported = StringX(L"\xab") + 
                             sx_group + StringX(L"\xbb \xab") + 
                             sx_title + StringX(L"\xbb \xab") +
                             sx_user  + StringX(L"\xbb");
    rpt.WriteLine(sx_imported.c_str());
  }

  SetDBChanged(true);
  return SUCCESS;
}

void ProcessKeePassCSVLine(const string &linebuf, std::vector<StringX> &tokens)
{
  char ch;
  bool bInField = false;
  CUTF8Conv conv;

  StringX item;
  StringX sxdata;
  
  for(size_t i = 0; i < linebuf.length(); i++ ) {
    ch = linebuf[i];

    if(ch == 0)
      continue;
    else
    if (ch == '\\' && linebuf[i + 1] != 0) {
      i++; // Skip escape character
      if (linebuf[i + 1] == 'r')
        item += '\r'; // Write escaped symbol
      else 
      if (linebuf[i + 1] == 'n')
        item += '\n'; // Write escaped symbol
      else
        item += linebuf[i]; // Write escaped symbol
    }
    else
    if (!bInField && ch == ',' && linebuf[i + 1] == ',') {
      conv.FromUTF8((unsigned char *)item.c_str(), item.length(), sxdata);
      tokens.push_back(sxdata); item.clear(); sxdata.clear();
    }
    else
    if (!bInField && ch == ',' && linebuf[i + 1] == '\r') {
      conv.FromUTF8((unsigned char *)item.c_str(), item.length(), sxdata);
      tokens.push_back(sxdata); item.clear(); sxdata.clear();
    }
    else
    if (!bInField && ch == ',' && linebuf[i + 1] == '\n') {
      conv.FromUTF8((unsigned char *)item.c_str(), item.length(), sxdata);
      tokens.push_back(sxdata); item.clear(); sxdata.clear();
    }
    else
    if (ch == '\"') {
      if (bInField) {
        tokens.push_back(item); item.clear();
        bInField = false;
      }
      else bInField = true;
    }
    else {
      if (bInField) {
        item += ch;
      }
    }
  }
}

int PWScore::ImportKeePassV1CSVFile(const StringX &filename,
                                    int &numImported, int &numSkipped, int &numRenamed,
                                    CReport &rpt, Command *&pcommand)
{
  stringT strError;
  stringT cs_error;
  CUTF8Conv conv;
  pcommand = NULL;
  numImported  = numSkipped = numRenamed = 0;

  // We need to use FOpen as the file name/file path may contain non-Latin
  // characters even though we need the file to contain ASCII and UTF-8 characters
  // and not Unicode (wchar_t).
  FILE *fs = pws_os::FOpen(filename.c_str(), _T("rt"));
  if (fs == NULL)
    return CANT_OPEN_FILE;

  // We need to use file stream I/O but can't with standard FILE I/O
  // so read in whole file and put it in a StringXStream (may contain sensitive data).
  // Hopefully, an import text file is not too large!
  const size_t IMPORT_BUFFER_SIZE = 4096;

  cStringXStream iss;
  unsigned char buffer[IMPORT_BUFFER_SIZE + 1];
  bool bError(false);
  size_t total(0);
  while(!feof(fs)) {
    size_t count = fread(buffer, 1, IMPORT_BUFFER_SIZE, fs);
    if (ferror(fs)) {
      bError = true;
      break;
    }
    buffer[count] = '\0';
    iss << buffer;
    total += count;
  }

  // Close the file
  fclose(fs);

  if (bError)
    return FAILURE;

  // The following's a stream of chars.  We need to process the header row 
  // as straight ASCII, and we need to handle rest as utf-8
  int numlines = 0;

  CItemData ci_temp;
  vector<StringX> vs_Header;

  // Parse the header
  const StringX s_hdr = KPEXPORTHEADER;
  const TCHAR pTab[] = _T("\t");

  enum Fields {GROUP, PARENTGROUPS, TITLE, USER, PASSWORD, URL, NOTES, UUID, ICON,
               CTIME, ATIME, PMTIME, XTIME, ATTACHMENTDESCR, ATTACHMENT, NUMFIELDS};

  int i_Offset[NUMFIELDS];
  for (int i = 0; i < NUMFIELDS; i++) {
    i_Offset[i] = -1;
  }

  // Capture individual column titles:
  StringX::size_type to = 0, from;
  do {
    from = s_hdr.find_first_not_of(pTab, to);
    if (from == string::npos)
      break;
    to = s_hdr.find_first_of(pTab, from);
    vs_Header.push_back(s_hdr.substr(from,
                                     ((to == string::npos) ?
                                      string::npos : to - from)));
  } while (to != string::npos);

  // Following fails if a field was added in enum but not in
  // EXPORTHEADER, or vice versa.
  ASSERT(vs_Header.size() == NUMFIELDS);

  string s_header, linebuf;

  if (!getline(iss, s_header, '\n'))
    return FAILURE;

  // the first line of the Keepass text file contains BOM characters
  if (s_header.length() > 3) {
    if((static_cast<unsigned char>(s_header[0]) == 0xEF) &&
       (static_cast<unsigned char>(s_header[1]) == 0xBB) &&
       (static_cast<unsigned char>(s_header[2]) == 0xBF)) {
      // Remove BOM
      s_header = s_header.erase(0, 3);
    }
  }

  // Parse the header line
  std::vector<StringX> hdr_tokens;
  ProcessKeePassCSVLine(s_header, hdr_tokens);
  
  // Capture individual column titles from s_header:
  // Set i_Offset[field] to column in which field is found in text file,
  // or leave at -1 if absent from text.
  unsigned num_found = 0;

  for (size_t i = 0; i < hdr_tokens.size(); i++) {
    vector<StringX>::iterator it(std::find(vs_Header.begin(), vs_Header.end(), hdr_tokens[i]));
    if (it != vs_Header.end()) {
      i_Offset[it - vs_Header.begin()] = i;
      num_found++;
    }
  }

  if (num_found == 0) {
    LoadAString(strError, IDSC_IMPORTNOCOLS);
    rpt.WriteLine(strError);
    return FAILURE;
  }

  // These are "must haves"!
  if (i_Offset[PASSWORD] == -1 || i_Offset[TITLE] == -1) {
    LoadAString(strError, IDSC_IMPORTMISSINGCOLS);
    rpt.WriteLine(strError);
    return FAILURE;
  }

  if (num_found < vs_Header.size()) {
    Format(cs_error, IDSC_IMPORTHDR, num_found);
    rpt.WriteLine(cs_error);
    LoadAString(cs_error, IDSC_IMPORTKNOWNHDRS);
    rpt.WriteLine(cs_error);
    for (int i = 0; i < NUMFIELDS; i++) {
      if (i_Offset[i] >= 0) {
        Format(cs_error, _T(" %s,"), vs_Header[i].c_str());
        rpt.WriteLine(cs_error, false);
      }
    }
    rpt.WriteLine();
    rpt.WriteLine();
  }

  MultiCommands *pmulticmds = MultiCommands::Create(this);
  pcommand = pmulticmds;
  Command *pcmd1 = UpdateGUICommand::Create(this, UpdateGUICommand::WN_UNDO,
                                            UpdateGUICommand::GUI_UNDO_IMPORT);
  pmulticmds->Add(pcmd1);

  // Finished parsing header, go get the data!
  // Initialize set
  GTUSet setGTU;
  InitialiseGTU(setGTU);
  UUIDSet setUUID;
  InitialiseUUID(setUUID);

  StringX sx_group, sx_title, sx_user, sx_parent_groups, sx_notes, sx_uuid;
  time_t ctime, atime, mtime, xtime;
  uuid_array_t ua;

  const StringX fwdslash = _T("/");
  const StringX bwdslash = _T("\\");
  const StringX dot = _T(".");
  const StringX txtCRLF = _T("\\r\\n");
  const StringX CRLF = _T("\r\n");

  for (;;) {
    // Clear out old stuff!
    sx_group = sx_title = sx_user = sx_parent_groups = sx_notes = sx_uuid = _T("");
    ctime = atime = mtime = xtime = (time_t)0;
    memset(ua, 0, sizeof(ua));

    // read a single line.
    if (!getline(iss, linebuf, '\n')) break;
    
    // Check if end of file
    if (iss.eof())
      break;

    // skip blank lines
    if (linebuf.empty()) {
      Format(cs_error, IDSC_IMPORTEMPTYLINESKIPPED, numlines);
      rpt.WriteLine(cs_error);
      numSkipped++;
      continue;
    }

    std::vector<StringX> tokens;
    ProcessKeePassCSVLine(linebuf, tokens);
    numlines++;

    // Sanity check
    if (tokens.size() < num_found) {
      Format(cs_error, IDSC_IMPORTLINESKIPPED, numlines, tokens.size(), num_found);
      rpt.WriteLine(cs_error);
      numSkipped++;
      continue;
    }

    if (static_cast<size_t>(i_Offset[PASSWORD]) >= tokens.size() ||
        tokens[i_Offset[PASSWORD]].empty()) {
      Format(cs_error, IDSC_IMPORTNOPASSWORD, numlines);
      rpt.WriteLine(cs_error);
      numSkipped++;
      continue;
    }
    if (static_cast<size_t>(i_Offset[TITLE]) >= tokens.size() ||
        tokens[i_Offset[TITLE]].empty()) {
      Format(cs_error, IDSC_IMPORTNOTITLE, numlines);
      rpt.WriteLine(cs_error);
      numSkipped++;
      continue;
    }
    
    if (!tokens[i_Offset[GROUP]].empty()) {
      sx_group = tokens[i_Offset[GROUP]];
      // Replace any '.' by a '/'
      size_t pos = 0;
      while((pos = sx_group.find(dot, pos)) != StringX::npos) {
        sx_group.replace(pos, 1, fwdslash);
        pos++;
      }
      // Replace any '\' by a '/'
      while((pos = sx_group.find(bwdslash, pos)) != StringX::npos) {
        sx_group.replace(pos, 1, fwdslash);
        pos++;
      }
    }

    if (!tokens[i_Offset[TITLE]].empty()) {
      sx_title = tokens[i_Offset[TITLE]];
      // Replace any '.' by a '/'
      size_t pos = 0;
      while((pos = sx_title.find(dot, pos)) != StringX::npos) {
        sx_title.replace(pos, 1, fwdslash);
        pos++;
      }
    }

    if (!tokens[i_Offset[USER]].empty()) {
      sx_user = tokens[i_Offset[USER]];
    }

    if (!tokens[i_Offset[PARENTGROUPS]].empty()) {
      // set the parent groups sx_parent_groups
      // Groups are in order separated by '\'
      // Groups with a '/' are unchanged
      // Group names can have dots in them and these will be replaced by a '/'

      StringX temp = tokens[i_Offset[PARENTGROUPS]];
      // Replace a '.' by a '/'
      size_t pos = 0;
      while((pos = temp.find(dot, pos)) != StringX::npos) {
        temp.replace(pos, 1, fwdslash);
        pos++;
      }

      // Tokenize by '\'
      vector<stringT> parent_groups;
      stringstreamT ss(temp.c_str());
      stringT item;
      while (getline(ss, item, _T('\\'))) {
        parent_groups.push_back(item);
      }

      // Construct the parent groups
      sx_parent_groups.clear();
      for (size_t i = 0; i < parent_groups.size(); i++) {
        sx_parent_groups = sx_parent_groups + parent_groups[i].c_str() + dot;
      }
    }

    if (!tokens[i_Offset[UUID]].empty()) {
      sx_uuid = tokens[i_Offset[UUID]];
      if (sx_uuid.length() == sizeof(uuid_array_t) * 2) {
        unsigned int x(0);
        for (size_t i = 0; i < sizeof(uuid_array_t); i++) {
          StringXStream ss;
          ss.str(sx_uuid.substr(i * 2, 2));
          ss >> hex >> x;
          ua[i] = static_cast<unsigned char>(x);
        }
      } else
        sx_uuid.clear();
    }

    if (!tokens[i_Offset[CTIME]].empty()) {
      StringX sx_ctime = tokens[i_Offset[CTIME]];
      if (sx_ctime.length() != 19)
        continue;
      sx_ctime.replace(10, 1, _T("T"));
#ifdef UNICODE
      std::wstring temp(sx_ctime.length(), L' ');
      std::copy(sx_ctime.begin(), sx_ctime.end(), temp.begin());
#else
      std::string temp = sx_ctime;
#endif
      VerifyXMLDateTimeString(temp, ctime);
    }

    if (!tokens[i_Offset[ATIME]].empty()) {
      StringX sx_atime = tokens[i_Offset[ATIME]];
      if (sx_atime.length() != 19)
        continue;
      sx_atime.replace(10, 1, _T("T"));
#ifdef UNICODE
      std::wstring temp(sx_atime.length(), L' ');
      std::copy(sx_atime.begin(), sx_atime.end(), temp.begin());
#else
      std::string temp = sx_ctime;
#endif
      VerifyXMLDateTimeString(temp, atime);
    }

    if (!tokens[i_Offset[PMTIME]].empty()) {
      StringX sx_mtime = tokens[i_Offset[PMTIME]];
      if (sx_mtime.length() != 19)
        continue;
      sx_mtime.replace(10, 1, _T("T"));
#ifdef UNICODE
      std::wstring temp(sx_mtime.length(), L' ');
      std::copy(sx_mtime.begin(), sx_mtime.end(), temp.begin());
#else
      std::string temp = sx_ctime;
#endif
      VerifyXMLDateTimeString(temp, mtime);
    }

    if (!tokens[i_Offset[XTIME]].empty()) {
      StringX sx_xtime = tokens[i_Offset[XTIME]];
      if (sx_xtime.length() != 19)
        continue;
      sx_xtime.replace(10, 1, _T("T"));
#ifdef UNICODE
      std::wstring temp(sx_xtime.length(), L' ');
      std::copy(sx_xtime.begin(), sx_xtime.end(), temp.begin());
#else
      std::string temp = sx_ctime;
#endif
      VerifyXMLDateTimeString(temp, xtime);
    }

    if (!tokens[i_Offset[NOTES]].empty()) {
      sx_notes = tokens[i_Offset[NOTES]];
      size_t pos = 0;
      while((pos = sx_notes.find(txtCRLF, pos)) != StringX::npos) {
        sx_notes.replace(pos, txtCRLF.length(), CRLF);
        pos += CRLF.length();
      }
    }

    // set up the group tree
    if (!sx_parent_groups.empty())
      sx_group = sx_parent_groups + sx_group;
    if (!sx_group.empty())
      sx_group = KPIMPORTEDPREFIX + dot + sx_group;
    else
      sx_group = KPIMPORTEDPREFIX;

    // Create & append the new record.
    CItemData ci_temp;
    bool bNewUUID(true);
    if (!sx_uuid.empty()) {
      const CUUID uuid(ua);
      if (uuid != CUUID::NullUUID()) {
        UUIDSetPair pr_uuid = setUUID.insert(uuid);
        if (pr_uuid.second) {
          ci_temp.SetUUID(uuid);
          bNewUUID = false;
        }
      }
    }

    if (bNewUUID)
      ci_temp.CreateUUID();

    if (!sx_group.empty())
      ci_temp.SetGroup(sx_group);
    ci_temp.SetTitle(sx_title.empty() ? _T("Unknown") : sx_title);
    if (!sx_user.empty())
      ci_temp.SetUser(sx_user);
    ci_temp.SetPassword(tokens[i_Offset[PASSWORD]].empty() ? _T("Unknown") : tokens[i_Offset[PASSWORD]]);
    if (!sx_notes.empty())
      ci_temp.SetNotes(sx_notes.c_str());
    if (!tokens[i_Offset[URL]].empty())
      ci_temp.SetURL(tokens[i_Offset[URL]]);
    if (ctime > 0)
      ci_temp.SetCTime(ctime);
    if (atime > 0)
      ci_temp.SetATime(atime);
    if (mtime > 0) {
      ci_temp.SetPMTime(mtime);
      ci_temp.SetRMTime(mtime);
    }
    if (xtime > 0)
      ci_temp.SetXTime(xtime);

    // Now make sure it is unique
    StringX sxnewtitle(sx_title);
    bool conflict = !MakeEntryUnique(setGTU, sx_group, sxnewtitle, sx_user, IDSC_IMPORTNUMBER);
    if (conflict) {
      ci_temp.SetTitle(sxnewtitle);
      stringT cs_header;
      if (sx_group.empty())
        Format(cs_header, IDSC_IMPORTCONFLICTS2, numlines);
      else
        Format(cs_header, IDSC_IMPORTCONFLICTS1, numlines, sx_group.c_str());

      Format(cs_error, IDSC_IMPORTCONFLICTS0, cs_header.c_str(),
               sx_title.c_str(), sx_user.c_str(), sxnewtitle.c_str());
      rpt.WriteLine(cs_error);
      sx_title = sxnewtitle;
      numRenamed++;
    }

    ci_temp.SetStatus(CItemData::ES_ADDED);

    // Get GUI to populate its field
    GUISetupDisplayInfo(ci_temp);

    // Add to commands to execute
    Command *pcmd = AddEntryCommand::Create(this, ci_temp);
    pcmd->SetNoGUINotify();
    pmulticmds->Add(pcmd);
    numImported++;
    StringX sx_imported = StringX(L"\xab") + 
                             sx_group + StringX(L"\xbb \xab") + 
                             sx_title + StringX(L"\xbb \xab") +
                             sx_user  + StringX(L"\xbb");
    rpt.WriteLine(sx_imported.c_str());
  } // file processing for (;;) loop

  if (numImported > 0)
    SetDBChanged(true);

  return ((numSkipped + numRenamed)) == 0 ? SUCCESS : OK_WITH_ERRORS;
}
