/*
 * Silly subclass of CTreeCtrl just to implement Drag&Drop.
 *
 * Based on MFC sample code from CMNCTRL1
 */


#include "stdafx.h"
#include "MyTreeCtrl.h"
#include "DboxMain.h"
#include "corelib/ItemData.h"
#include "corelib/MyString.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static const TCHAR GROUP_SEP = TCHAR('.');

CMyTreeCtrl::CMyTreeCtrl() : m_bDragging(false), m_pimagelist(NULL)
{
}

CMyTreeCtrl::~CMyTreeCtrl()
{
  delete m_pimagelist;
}


BEGIN_MESSAGE_MAP(CMyTreeCtrl, CTreeCtrl)
	//{{AFX_MSG_MAP(CMyTreeCtrl)
	ON_NOTIFY_REFLECT(TVN_ENDLABELEDIT, OnEndLabelEdit)
	ON_NOTIFY_REFLECT(TVN_BEGINDRAG, OnBeginDrag)
	ON_NOTIFY_REFLECT(TVN_BEGINRDRAG, OnBeginDrag)
	ON_WM_MOUSEMOVE()
	ON_WM_DESTROY()
	ON_WM_LBUTTONUP()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void CMyTreeCtrl::OnDestroy()
{
  CImageList  *pimagelist;

  pimagelist = GetImageList(TVSIL_NORMAL);
  if (pimagelist != NULL) {
    pimagelist->DeleteImageList();
    delete pimagelist;
  }
}

void CMyTreeCtrl::SetNewStyle(long lStyleMask, BOOL bSetBits)
{
  long        lStyleOld;

  lStyleOld = GetWindowLong(m_hWnd, GWL_STYLE);
  lStyleOld &= ~lStyleMask;
  if (bSetBits)
    lStyleOld |= lStyleMask;

  SetWindowLong(m_hWnd, GWL_STYLE, lStyleOld);
  SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

void CMyTreeCtrl::UpdateLeafsGroup(HTREEITEM hItem, CString prefix)
{
  if (IsLeafNode(hItem)) {
    DWORD itemData = GetItemData(hItem);
    ASSERT(itemData != NULL);
    CItemData *ci = (CItemData *)itemData;
    ci->SetGroup(CMyString(prefix));
  } else { // update prefix with current group name and recurse
    if (!prefix.IsEmpty())
      prefix += GROUP_SEP;
    prefix += GetItemText(hItem);
    HTREEITEM child;
    for(child = GetChildItem(hItem); child != NULL; child = GetNextSiblingItem(child)) {
      UpdateLeafsGroup(child, prefix);
    }
  }
}

void CMyTreeCtrl::OnEndLabelEdit(LPNMHDR pnmhdr, LRESULT *pLResult)
{
  TV_DISPINFO     *ptvinfo;

  ptvinfo = (TV_DISPINFO *)pnmhdr;
  if (ptvinfo->item.pszText != NULL) {
    ptvinfo->item.mask = TVIF_TEXT;
    SetItem(&ptvinfo->item);
    HTREEITEM ti = ptvinfo->item.hItem;
    if (IsLeafNode(ptvinfo->item.hItem)) {
      // Update leaf's title
      DWORD itemData = GetItemData(ti);
      ASSERT(itemData != NULL);
      CItemData *ci = (CItemData *)itemData;
      ci->SetTitle(ptvinfo->item.pszText);
      DboxMain *parent = (DboxMain *)GetParent();
      // update corresponding List text
      DisplayInfo *di = (DisplayInfo *)ci->GetDisplayInfo();
      ASSERT(di != NULL);
      int lindex = di->list_index;
      parent->UpdateListItemTitle(lindex, ptvinfo->item.pszText);
      // Mark database as modified
      parent->SetChanged(true);
    } else {
      // Update all leaf chldren with new path element
      // prefix is path up to and NOT including renamed node
      CString prefix;
      HTREEITEM parent, current = ti;
      do {
	parent = GetParentItem(current);
	if (parent == NULL) {
	  break;
	}
	current = parent;
	if (!prefix.IsEmpty())
	  prefix = GROUP_SEP + prefix;
	prefix = GetItemText(current) + prefix;
      } while (1);
      UpdateLeafsGroup(ti, prefix);
    }
  }
  *pLResult = TRUE;
}

void CMyTreeCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
  HTREEITEM           hitem;
  UINT                flags;

  if (m_bDragging) {
    ASSERT(m_pimagelist != NULL);
    m_pimagelist->DragMove(point);
    if ((hitem = HitTest(point, &flags)) != NULL) {
      m_pimagelist->DragLeave(this);
      SelectDropTarget(hitem);
      m_hitemDrop = hitem;
      m_pimagelist->DragEnter(this, point);
    }
  }

  CTreeCtrl::OnMouseMove(nFlags, point);
}

bool CMyTreeCtrl::IsChildNodeOf(HTREEITEM hitemChild, HTREEITEM hitemSuspectedParent)
{
  do {
    if (hitemChild == hitemSuspectedParent)
      break;
  } while ((hitemChild = GetParentItem(hitemChild)) != NULL);

  return (hitemChild != NULL);
}

bool CMyTreeCtrl::IsLeafNode(HTREEITEM hItem)
{
  // ItemHasChildren() won't work in the general case
  BOOL status;
  int i, dummy;
  status = GetItemImage(hItem, i, dummy);
  ASSERT(status);
  return (i == LEAF);
}

void CMyTreeCtrl::DeleteWithParents(HTREEITEM hItem)
{
  // We don't want nodes that have no children to remain
  HTREEITEM p;
  do {
    p = GetParentItem(hItem);
    DeleteItem(hItem);
    if (ItemHasChildren(p))
      break;
    hItem = p;
  } while (p != TVI_ROOT);
}

CString CMyTreeCtrl::GetGroup(HTREEITEM hItem)
{
  CString retval;
  CString nodeText;
  while (hItem != NULL) {
    nodeText = GetItemText(hItem);
    if (!retval.IsEmpty())
      nodeText += GROUP_SEP;
    retval = nodeText + retval;
    hItem = GetParentItem(hItem);
  }
  return retval;
}


static CMyString GetPathElem(CMyString &path)
{
  // Get first path element and chop it off, i.e., if
  // path = "a.b.c.d"
  // will return "a" and path will be "b.c.d"
  // (assuming GROUP_SEP is '.')

  CMyString retval;
  int N = path.Find(GROUP_SEP);
  if (N == -1) {
    retval = path;
    path = _T("");
  } else {
    const int Len = path.GetLength();
    retval = CMyString(path.Left(N));
    path = CMyString(path.Right(Len - N - 1));
  }
  return retval;
}

static bool ExistsInTree(CTreeCtrl &Tree, HTREEITEM node,
			 const CMyString &s, HTREEITEM &si)
{
  // returns true iff s is a direct descendant of node
  HTREEITEM ti = Tree.GetChildItem(node);
  
  while (ti != NULL) {
    const CMyString itemText = Tree.GetItemText(ti);
    if (itemText == s) {
      si = ti;
      return true;
    }
    ti = Tree.GetNextItem(ti, TVGN_NEXT);
  }
  return false;
}

HTREEITEM CMyTreeCtrl::AddGroup(const CString &group)
{
  // Add a group at the end of path
  HTREEITEM ti = TVI_ROOT;
  HTREEITEM si;
  if (!group.IsEmpty()) {
    CMyString path = group;
    CMyString s;
    do {
      s = GetPathElem(path);
      if (!ExistsInTree(*this, ti, s, si)) {
	ti = InsertItem(s, ti, TVI_SORT);
	SetItemImage(ti, CMyTreeCtrl::NODE, CMyTreeCtrl::NODE);
      } else
	ti = si;
    } while (!path.IsEmpty());
  }
  return ti;
}

bool CMyTreeCtrl::TransferItem(HTREEITEM hitemDrag, HTREEITEM hitemDrop)
{
  TV_INSERTSTRUCT     tvstruct;
  TCHAR               sztBuffer[128];
  HTREEITEM           hNewItem, hFirstChild;
  DWORD itemData = GetItemData(hitemDrag);

  // avoid an infinite recursion
  tvstruct.item.hItem = hitemDrag;
  tvstruct.item.cchTextMax = sizeof(sztBuffer)/sizeof(TCHAR) - 1;
  tvstruct.item.pszText = sztBuffer;
  tvstruct.item.mask = (TVIF_CHILDREN | TVIF_HANDLE | TVIF_IMAGE
			| TVIF_SELECTEDIMAGE | TVIF_TEXT);
  GetItem(&tvstruct.item);  // get information of the dragged element
  tvstruct.hParent = hitemDrop;
  tvstruct.hInsertAfter = TVI_SORT;
  tvstruct.item.mask = TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_TEXT;
  hNewItem = InsertItem(&tvstruct);
  if (itemData != 0) { // Non-NULL itemData implies Leaf
    CItemData *ci = (CItemData *)itemData;
    // Update Group
    CMyString path, elem;
    HTREEITEM p, q = hNewItem;
    do {
      p = GetParentItem(q);
      if (p != NULL) {
	elem = CMyString(GetItemText(p));
	if (!path.IsEmpty())
	  elem += GROUP_SEP;
	path = elem + path;
	q = p;
      } else
	break;
    } while (1);
    ci->SetGroup(path);
    // Mark database as modified!
    ((DboxMain *)GetParent())->SetChanged(true);
    // Update DisplayInfo record associated with ItemData
    DisplayInfo *di = (DisplayInfo *)ci->GetDisplayInfo();
    ASSERT(di != NULL);
    di->tree_item = hNewItem;
  }
  SetItemData(hNewItem, itemData);

  while ((hFirstChild = GetChildItem(hitemDrag)) != NULL) {
    TransferItem(hFirstChild, hNewItem);  // recursively transfer all the items
    DeleteItem(hFirstChild);
  }
  return true;
}

void CMyTreeCtrl::OnButtonUp()
{
  if (m_bDragging) {
    ASSERT(m_pimagelist != NULL);
    m_pimagelist->DragLeave(this);
    m_pimagelist->EndDrag();
    delete m_pimagelist;
    m_pimagelist = NULL;
    HTREEITEM parent = GetParentItem(m_hitemDrag);

    if (m_hitemDrag != m_hitemDrop &&
	!IsLeafNode(m_hitemDrop) &&
	!IsChildNodeOf(m_hitemDrop, m_hitemDrag) &&
	parent != m_hitemDrop) {
      TransferItem(m_hitemDrag, m_hitemDrop);
      DeleteItem(m_hitemDrag);
      while (parent != NULL && !ItemHasChildren(parent)) {
	HTREEITEM grandParent = GetParentItem(parent);
	DeleteItem(parent);
	parent = grandParent;
      }
    } else
      MessageBeep(0);

    ReleaseCapture();
    m_bDragging = FALSE;
    SelectDropTarget(NULL);
  }
}

void CMyTreeCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
  OnButtonUp();
  CTreeCtrl::OnLButtonUp(nFlags, point);
}


void CMyTreeCtrl::OnBeginDrag(LPNMHDR , LRESULT *)
{
  CPoint      ptAction;
  UINT        nFlags;

  GetCursorPos(&ptAction);
  ScreenToClient(&ptAction);
  ASSERT(!m_bDragging);
  m_bDragging = TRUE;
  m_hitemDrag = HitTest(ptAction, &nFlags);
  m_hitemDrop = NULL;

  ASSERT(m_pimagelist == NULL);
  m_pimagelist = CreateDragImage(m_hitemDrag);
  m_pimagelist->DragShowNolock(TRUE);
  m_pimagelist->SetDragCursorImage(0, CPoint(0, 0));
  m_pimagelist->BeginDrag(0, CPoint(0,0));
  m_pimagelist->DragMove(ptAction);
  m_pimagelist->DragEnter(this, ptAction);
  SetCapture();
}
