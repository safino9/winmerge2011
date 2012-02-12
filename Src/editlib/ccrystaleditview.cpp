///////////////////////////////////////////////////////////////////////////
//  File:       ccrystaleditview.cpp
//  Version:    1.2.0.5
//  Created:    29-Dec-1998
//
//  Copyright:  Stcherbatchenko Andrei
//  E-mail:     windfall@gmx.de
//
//  Implementation of the CCrystalEditView class, a part of the Crystal Edit -
//  syntax coloring text editor.
//
//  You are free to use or modify this code to the following restrictions:
//  - Acknowledge me somewhere in your about box, simple "Parts of code by.."
//  will be enough. If you can't (or don't want to), contact me personally.
//  - LEAVE THIS HEADER INTACT
////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////
//  21-Feb-99
//      Paul Selormey, James R. Twine:
//  +   FEATURE: description for Undo/Redo actions
//  +   FEATURE: multiple MSVC-like bookmarks
//  +   FEATURE: 'Disable backspace at beginning of line' option
//  +   FEATURE: 'Disable drag-n-drop editing' option
//
//  +   FEATURE: Auto indent
//  +   FIX: ResetView() was overriden to provide cleanup
////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////
//  19-Jul-99
//      Ferdinand Prantl:
//  +   FEATURE: support for autoindenting brackets and parentheses
//  +   FEATURE: menu options, view and window
//  +   FEATURE: SDI+MDI versions with help
//  +   FEATURE: extended registry support for saving settings
//  +   FEATURE: some other things I've forgotten ...
//  27-Jul-99
//  +   FIX: treating groups in regular expressions corrected
//  +   FIX: autocomplete corrected
//
//  ... it's being edited very rapidly so sorry for non-commented
//        and maybe "ugly" code ...
////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////
//	??-Aug-99
//		Sven Wiegand (search for "//BEGIN SW" to find my changes):
//	+ FEATURE: "Go to last change" (sets the cursor on the position where
//			the user did his last edit actions
//	+ FEATURE: Support for incremental search in CCrystalTextView
////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////
//	24-Oct-99
//		Sven Wiegand
//	+ FEATURE: Supporting [Return] to exit incremental-search-mode
//		     (see OnChar())
////////////////////////////////////////////////////////////////////////////

/**
 * @file  ccrystaleditview.cpp
 *
 * @brief Implementation of the CCrystalEditView class
 */
// ID line follows -- this is updated by SVN
// $Id$


#include "StdAfx.h"
#include <MyCom.h>
#include "LanguageSelect.h"
#include "editcmd.h"
#include "ccrystaleditview.h"
#include "ccrystaltextbuffer.h"
#include "ceditreplacedlg.h"
#include "SettingStore.h"
#include "string_util.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static const unsigned int MAX_TAB_LEN = 64;  // Same as in CrystalViewText.cpp

#define DRAG_BORDER_X       5
#define DRAG_BORDER_Y       5

/////////////////////////////////////////////////////////////////////////////
// CCrystalEditView

CCrystalEditView::CCrystalEditView(size_t ZeroInit)
: CCrystalTextView(ZeroInit)
, m_bAutoIndent(TRUE)
{
}

CCrystalEditView::~CCrystalEditView()
{
}

bool CCrystalEditView::DoSetTextType(TextDefinition *def)
{
	m_CurSourceDef = def;
	SetAutoIndent((def->flags & SRCOPT_AUTOINDENT) != 0);
	return CCrystalTextView::DoSetTextType(def);
}

/////////////////////////////////////////////////////////////////////////////
// CCrystalEditView message handlers

void CCrystalEditView::ResetView()
{
	m_bOvrMode = false;
	m_bLastReplace = FALSE;
	CCrystalTextView::ResetView();
}

bool CCrystalEditView::QueryEditable()
{
	return m_pTextBuffer && !m_pTextBuffer->GetReadOnly();
}

BOOL CCrystalEditView::DeleteCurrentSelection()
{
  if (IsSelection ())
    {
      POINT ptSelStart, ptSelEnd;
      GetSelection (ptSelStart, ptSelEnd);

      POINT ptCursorPos = ptSelStart;
      ASSERT_VALIDTEXTPOS (ptCursorPos);
      SetAnchor (ptCursorPos);
      SetSelection (ptCursorPos, ptCursorPos);
      SetCursorPos (ptCursorPos);
      EnsureVisible (ptCursorPos);

      // [JRT]:
      m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_DELSEL);
      return TRUE;
    }
  return FALSE;
}

void CCrystalEditView::Paste()
{
	if (!QueryEditable())
		return;
	if (!OpenClipboard())
		return;
	HGLOBAL hData = GetClipboardData(CF_UNICODETEXT);
	if (LPCTSTR pszText = reinterpret_cast<LPTSTR>(GlobalLock(hData)))
	{
		SIZE_T cbData = GlobalSize(hData);
		int cchText = cbData / sizeof(TCHAR) - 1;

		if (cchText <= 0)
			cchText = 0;
		// If in doubt, assume zero-terminated string
		else if (!IsClipboardFormatAvailable(RegisterClipboardFormat(_T("WinMergeClipboard"))))
			cchText = _tcslen(pszText);

		m_pTextBuffer->BeginUndoGroup();

		POINT ptCursorPos;
		if (IsSelection ())
		{
			POINT ptSelStart, ptSelEnd;
			GetSelection(ptSelStart, ptSelEnd);
			ptCursorPos = ptSelStart;
			// [JRT]:
			m_pTextBuffer->DeleteText(this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_PASTE);
		}
		else
		{
			ptCursorPos = GetCursorPos();
		}
		ASSERT_VALIDTEXTPOS(ptCursorPos);

		int x, y;
		m_pTextBuffer->InsertText(this, ptCursorPos.y, ptCursorPos.x, pszText, cchText, y, x, CE_ACTION_PASTE);  //  [JRT]

		ptCursorPos.x = x;
		ptCursorPos.y = y;
		ASSERT_VALIDTEXTPOS (ptCursorPos);
		SetAnchor(ptCursorPos);
		SetSelection(ptCursorPos, ptCursorPos);
		SetCursorPos(ptCursorPos);
		EnsureVisible(ptCursorPos);

		m_pTextBuffer->FlushUndoGroup(this);
		GlobalUnlock(hData);
	}
	CloseClipboard();
}

void CCrystalEditView::Cut()
{
  if (!QueryEditable())
    return;
  if (!IsSelection())
    return;

  POINT ptSelStart, ptSelEnd;
  GetSelection (ptSelStart, ptSelEnd);
  String text;
  GetText (ptSelStart, ptSelEnd, text);
  PutToClipboard (text.c_str(), text.size());

  POINT ptCursorPos = ptSelStart;
  ASSERT_VALIDTEXTPOS (ptCursorPos);
  SetAnchor (ptCursorPos);
  SetSelection (ptCursorPos, ptCursorPos);
  SetCursorPos (ptCursorPos);
  EnsureVisible (ptCursorPos);

  m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_CUT);  // [JRT]

}

void CCrystalEditView::OnEditDelete()
{
  if (!QueryEditable())
    return;

  POINT ptSelStart, ptSelEnd;
  GetSelection (ptSelStart, ptSelEnd);
  if (ptSelStart == ptSelEnd)
    {
      if (ptSelEnd.x == GetLineLength (ptSelEnd.y))
        {
          if (ptSelEnd.y == GetLineCount () - 1)
            return;
          ptSelEnd.y++;
          ptSelEnd.x = 0;
        }
      else 
        {
          ptSelEnd.x++;
        }
    }

  POINT ptCursorPos = ptSelStart;
  ASSERT_VALIDTEXTPOS (ptCursorPos);
  SetAnchor (ptCursorPos);
  SetSelection (ptCursorPos, ptCursorPos);
  SetCursorPos (ptCursorPos);
  EnsureVisible (ptCursorPos);

  m_pTextBuffer->DeleteText(this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_DELETE);   // [JRT]
  UpdateCaret();
}

void CCrystalEditView::OnChar(WPARAM nChar)
{
  if ((::GetAsyncKeyState (VK_LBUTTON) & 0x8000) != 0 ||
        (::GetAsyncKeyState (VK_RBUTTON) & 0x8000) != 0)
    return;

  if (nChar == VK_RETURN)
    {
      if (m_bOvrMode)
        {
          POINT ptCursorPos = GetCursorPos ();
          ASSERT_VALIDTEXTPOS (ptCursorPos);
          if (ptCursorPos.y < GetLineCount () - 1)
            {
              ptCursorPos.x = 0;
              ptCursorPos.y++;

              ASSERT_VALIDTEXTPOS (ptCursorPos);
              SetSelection (ptCursorPos, ptCursorPos);
              SetAnchor (ptCursorPos);
              SetCursorPos (ptCursorPos);
              EnsureVisible (ptCursorPos);
              return;
            }
        }

      m_pTextBuffer->BeginUndoGroup(m_bMergeUndo);
	  m_bMergeUndo = false;

      if (QueryEditable ())
        {
          POINT ptCursorPos;
          if (IsSelection ())
            {
              POINT ptSelStart, ptSelEnd;
              GetSelection (ptSelStart, ptSelEnd);
        
              ptCursorPos = ptSelStart;
              /*SetAnchor (ptCursorPos);
              SetSelection (ptCursorPos, ptCursorPos);
              SetCursorPos (ptCursorPos);
              EnsureVisible (ptCursorPos);*/
        
              // [JRT]:
              m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_TYPING);
            }
          else
            ptCursorPos = GetCursorPos ();
          ASSERT_VALIDTEXTPOS (ptCursorPos);
          LPCTSTR pszText = m_pTextBuffer->GetDefaultEol();
          int cchText = _tcslen(pszText);

          int x, y;
          m_pTextBuffer->InsertText (this, ptCursorPos.y, ptCursorPos.x, pszText, cchText, y, x, CE_ACTION_TYPING);  //  [JRT]

          ptCursorPos.x = x;
          ptCursorPos.y = y;
          ASSERT_VALIDTEXTPOS (ptCursorPos);
          SetSelection (ptCursorPos, ptCursorPos);
          SetAnchor (ptCursorPos);
          SetCursorPos (ptCursorPos);
          EnsureVisible (ptCursorPos);
        }

      m_pTextBuffer->FlushUndoGroup (this);
      return;
    }
  // Accept control characters other than [\t\n\r] through Alt-Numpad
  if (nChar > 31
  || GetKeyState(VK_CONTROL) >= 0 &&
      (nChar != 27 || GetKeyState(VK_ESCAPE) >= 0) &&
      nChar != 9 && nChar != 10 && nChar != 13)
    {
      if (QueryEditable ())
        {
          m_pTextBuffer->BeginUndoGroup (m_bMergeUndo);

          POINT ptSelStart, ptSelEnd;
          GetSelection (ptSelStart, ptSelEnd);
          POINT ptCursorPos;
          if (ptSelStart != ptSelEnd)
            {
              ptCursorPos = ptSelStart;
              if (IsSelection ())
                {
                  POINT ptSelStart, ptSelEnd;
                  GetSelection (ptSelStart, ptSelEnd);
            
                  /*SetAnchor (ptCursorPos);
                  SetSelection (ptCursorPos, ptCursorPos);
                  SetCursorPos (ptCursorPos);
                  EnsureVisible (ptCursorPos);*/
            
                  // [JRT]:
                  m_pTextBuffer->DeleteText(this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_TYPING);
                }
            }
          else
            {
              ptCursorPos = GetCursorPos();
              if (m_bOvrMode && ptCursorPos.x < GetLineLength(ptCursorPos.y))
                m_pTextBuffer->DeleteText(this, ptCursorPos.y, ptCursorPos.x, ptCursorPos.y, ptCursorPos.x + 1, CE_ACTION_TYPING);     // [JRT]

            }

          ASSERT_VALIDTEXTPOS (ptCursorPos);

          TCHAR pszText[2];
          pszText[0] = (TCHAR) nChar;
          pszText[1] = 0;

          int x, y;
          m_pTextBuffer->InsertText (this, ptCursorPos.y, ptCursorPos.x, pszText, 1, y, x, CE_ACTION_TYPING);    // [JRT]

          ptCursorPos.x = x;
          ptCursorPos.y = y;
          ASSERT_VALIDTEXTPOS (ptCursorPos);
          SetSelection (ptCursorPos, ptCursorPos);
          SetAnchor (ptCursorPos);
          SetCursorPos (ptCursorPos);
          EnsureVisible (ptCursorPos);

		  m_bMergeUndo = true;
          m_pTextBuffer->FlushUndoGroup (this);
        }
    }
}

void CCrystalEditView::OnEditDeleteBack()
{
	if (!QueryEditable())
		return;

	POINT ptCursorPos = GetCursorPos();
	POINT ptCurrentCursorPos = ptCursorPos;
	if (IsSelection())
	{
		OnEditDelete();
	}
	else if (ptCursorPos.x > 0)		// If At Start Of Line
	{
		--ptCursorPos.x;			// Decrement Position
	}
	else if (ptCursorPos.y > 0)		// If Previous Lines Available
	{
		--ptCursorPos.y;		// Decrement To Previous Line
		ptCursorPos.x = GetLineLength(ptCursorPos.y); // Set Cursor To End Of Previous Line
	}
	if (ptCurrentCursorPos != ptCursorPos)
	{
		ASSERT_VALIDTEXTPOS(ptCursorPos);
		SetAnchor(ptCursorPos);
		SetSelection(ptCursorPos, ptCursorPos);
		SetCursorPos(ptCursorPos);
		EnsureVisible(ptCursorPos);
		m_pTextBuffer->DeleteText(this, ptCursorPos.y, ptCursorPos.x,
			ptCurrentCursorPos.y, ptCurrentCursorPos.x, CE_ACTION_BACKSPACE);  // [JRT]
	}
}

void CCrystalEditView::OnEditTab()
{
	if (!QueryEditable())
		return;

	POINT ptSelStart, ptSelEnd;
	GetSelection(ptSelStart, ptSelEnd);
	POINT ptCursorPos = GetCursorPos();
	ASSERT_VALIDTEXTPOS(ptCursorPos);
	// If we have more than one line selected, tabify sel lines
	const bool bTabify = ptSelStart.y != ptSelEnd.y;
	const int nTabSize = GetTabSize();
	int nChars = nTabSize;
	if (!bTabify)
		nChars -= CalculateActualOffset(ptCursorPos.y, ptCursorPos.x) % nTabSize;
	ASSERT(nChars > 0 && nChars <= nTabSize);

	TCHAR pszText[MAX_TAB_LEN + 1];
	// If inserting tabs, then initialize the text to a tab.
	if (m_pTextBuffer->GetInsertTabs())
	{
		pszText[0] = '\t';
		pszText[1] = '\0';
	}
	else //...otherwise, built whitespace depending on the location and tab stops
	{
		for (int i = 0; i < nChars; i++)
			pszText[i] = ' ';
		pszText[nChars] = '\0';
	}

	// Indent selected lines (multiple lines selected)
	if (bTabify)
	{
		m_pTextBuffer->BeginUndoGroup();
		int nStartLine = ptSelStart.y;
		int nEndLine = ptSelEnd.y;
		ptSelStart.x = 0;
		if (ptSelEnd.x == 0)
		{
			// Do not indent empty line.
			--nEndLine;
		}
		else if (ptSelEnd.y == GetLineCount() - 1)
		{
			ptSelEnd.x = GetLineLength(ptSelEnd.y);
		}
		else
		{
			ptSelEnd.x = 0;
			++ptSelEnd.y;
		}
		SetSelection(ptSelStart, ptSelEnd);
		SetCursorPos(ptSelEnd);
		EnsureVisible(ptSelEnd);

		//  Shift selection to right
		m_bHorzScrollBarLocked = TRUE;
		for (int i = nStartLine ; i <= nEndLine ; ++i)
		{
			int x, y;
			m_pTextBuffer->InsertText(this, i, 0, pszText, _tcslen(pszText), y, x, CE_ACTION_INDENT);  //  [JRT]
		}
		m_bHorzScrollBarLocked = FALSE;
		RecalcHorzScrollBar();
		m_pTextBuffer->FlushUndoGroup(this);
		return;
	}

  // Overwrite mode, replace next char with tab/spaces
  if (m_bOvrMode)
    {
      POINT ptCursorPos = GetCursorPos ();
      ASSERT_VALIDTEXTPOS (ptCursorPos);

      int nLineLength = GetLineLength (ptCursorPos.y);
      LPCTSTR pszLineChars = GetLineChars (ptCursorPos.y);
		
      // Not end of line
      if (ptCursorPos.x < nLineLength)
        {
          while (nChars > 0)
            {
              if (ptCursorPos.x == nLineLength)
                break;
              if (pszLineChars[ptCursorPos.x] == _T ('\t'))
                {
                  ptCursorPos.x++;
                  break;
                }
              ptCursorPos.x++;
              nChars--;
            }
          ASSERT (ptCursorPos.x <= nLineLength);
          ASSERT_VALIDTEXTPOS (ptCursorPos);

          SetSelection (ptCursorPos, ptCursorPos);
          SetAnchor (ptCursorPos);
          SetCursorPos (ptCursorPos);
          EnsureVisible (ptCursorPos);
          return;
        }
    }

  m_pTextBuffer->BeginUndoGroup ();

  int x, y;	// For cursor position

  // Text selected, no overwrite mode, replace sel with tab
  if (IsSelection ())
    {
      POINT ptSelStart, ptSelEnd;
      GetSelection (ptSelStart, ptSelEnd);

      /*SetAnchor (ptCursorPos);
      SetSelection (ptCursorPos, ptCursorPos);
      SetCursorPos (ptCursorPos);
      EnsureVisible (ptCursorPos);*/

      // [JRT]:
      m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_TYPING);
      m_pTextBuffer->InsertText( this, ptSelStart.y, ptSelStart.x, pszText, _tcslen(pszText), y, x, CE_ACTION_TYPING );
    }
  // No selection, add tab
  else
    {
      m_pTextBuffer->InsertText (this, ptCursorPos.y, ptCursorPos.x, pszText, _tcslen(pszText), y, x, CE_ACTION_TYPING);  //  [JRT]
    }

	ptCursorPos.x = x;
	ptCursorPos.y = y;
	ASSERT_VALIDTEXTPOS(ptCursorPos);
	SetSelection(ptCursorPos, ptCursorPos);
	SetAnchor(ptCursorPos);
	SetCursorPos(ptCursorPos);
	EnsureVisible(ptCursorPos);

	m_pTextBuffer->FlushUndoGroup(this);
}

void CCrystalEditView::OnEditUntab()
{
	if (!QueryEditable())
		return;

	POINT ptSelStart, ptSelEnd;
	GetSelection(ptSelStart, ptSelEnd);
	// If we have more than one line selected, tabify sel lines
	const bool bTabify = ptSelStart.y != ptSelEnd.y;

	if (bTabify)
	{
		m_pTextBuffer->BeginUndoGroup();
		int nStartLine = ptSelStart.y;
		int nEndLine = ptSelEnd.y;
		ptSelStart.x = 0;
		if (ptSelEnd.x == 0)
		{
			--nEndLine;
		}
		else if (ptSelEnd.y == GetLineCount() - 1)
		{
			ptSelEnd.x = GetLineLength(ptSelEnd.y);
		}
		else
		{
			ptSelEnd.x = 0;
			++ptSelEnd.y;
		}
		SetSelection(ptSelStart, ptSelEnd);
		SetCursorPos(ptSelEnd);
		EnsureVisible(ptSelEnd);

		//  Shift selection to left
		m_bHorzScrollBarLocked = TRUE;
		for (int i = nStartLine ; i <= nEndLine ; ++i)
        {
			int nLength = GetLineLength(i);
			if (nLength > 0)
			{
				LPCTSTR pszChars = GetLineChars(i);
				int nPos = 0, nOffset = 0;
				while (nPos < nLength)
				{
					if (pszChars[nPos] == _T(' '))
					{
						nPos++;
						if (++nOffset >= GetTabSize())
							break;
					}
					else
					{
						if (pszChars[nPos] == _T('\t'))
							nPos++;
						break;
					}
				}
				if (nPos > 0)
					m_pTextBuffer->DeleteText(this, i, 0, i, nPos, CE_ACTION_INDENT);  // [JRT]
			}
		}
		m_bHorzScrollBarLocked = FALSE;
		RecalcHorzScrollBar();
		m_pTextBuffer->FlushUndoGroup(this);
	}
  else
    {
      POINT ptCursorPos = GetCursorPos ();
      ASSERT_VALIDTEXTPOS (ptCursorPos);
      if (ptCursorPos.x > 0)
        {
          int nTabSize = GetTabSize ();
          int nOffset = CalculateActualOffset (ptCursorPos.y, ptCursorPos.x);
          int nNewOffset = nOffset / nTabSize * nTabSize;
          if (nOffset == nNewOffset && nNewOffset > 0)
            nNewOffset -= nTabSize;
          ASSERT (nNewOffset >= 0);

          LPCTSTR pszChars = GetLineChars(ptCursorPos.y);
          int nCurrentOffset = 0;
          int i = 0;
          while (nCurrentOffset < nNewOffset)
            {
              if (pszChars[i] == _T ('\t'))
                nCurrentOffset = nCurrentOffset / nTabSize * nTabSize + nTabSize;
              else
                nCurrentOffset++;
              ++i;
            }

          ASSERT (nCurrentOffset == nNewOffset);

          ptCursorPos.x = i;
          ASSERT_VALIDTEXTPOS (ptCursorPos);
          SetSelection (ptCursorPos, ptCursorPos);
          SetAnchor (ptCursorPos);
          SetCursorPos (ptCursorPos);
          EnsureVisible (ptCursorPos);
        }
    }
}

void CCrystalEditView::OnEditSwitchOvrmode()
{
	m_bOvrMode = !m_bOvrMode;
	UpdateCaret();
}

HRESULT CCrystalEditView::QueryInterface(REFIID iid, void **ppv)
{
    static const QITAB rgqit[] = 
    {   
        QITABENT(CCrystalEditView, IDropSource),
        QITABENT(CCrystalEditView, IDataObject),
        QITABENT(CCrystalEditView, IDropTarget),
        { 0 }
    };
    return QISearch(this, rgqit, iid, ppv);
}

ULONG CCrystalEditView::AddRef()
{
	return 1;
}

ULONG CCrystalEditView::Release()
{
	return 1;
}

HRESULT CCrystalEditView::DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	HRESULT hr = CMyFormatEtc(CF_UNICODETEXT).QueryGetData(pDataObj);
	if (hr == S_OK)
	{
		POINT ptClient = { pt.x, pt.y };
		ScreenToClient(&ptClient);
		ShowDropIndicator(ptClient);
		if (*pdwEffect != DROPEFFECT_COPY)
			*pdwEffect = grfKeyState & MK_CONTROL ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
	}
	else
	{
		HideDropIndicator();
		*pdwEffect = DROPEFFECT_NONE;
	}
	return hr;
}

HRESULT CCrystalEditView::DragLeave()
{
	HideDropIndicator();
	return S_OK;
}

HRESULT CCrystalEditView::DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	if (QueryEditable() && !GetDisableDragAndDrop())
	{
		POINT ptClient = { pt.x, pt.y };
		ScreenToClient(&ptClient);
		DoDragScroll(ptClient);
		ShowDropIndicator(ptClient);
		if (*pdwEffect != DROPEFFECT_COPY)
			*pdwEffect = grfKeyState & MK_CONTROL ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
	}
	else
	{
		HideDropIndicator();
		*pdwEffect = DROPEFFECT_NONE;
	}
	return S_OK;
}

HRESULT CCrystalEditView::Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
{
	HideDropIndicator();
	CMyStgMedium stgmedium;
	POINT ptClient = { pt.x, pt.y };
	ScreenToClient(&ptClient);
	if (QueryEditable() && !GetDisableDragAndDrop() &&
		CMyFormatEtc(CF_UNICODETEXT).GetData(pDataObj, &stgmedium) == S_OK &&
		DoDropText(stgmedium.hGlobal, ptClient))
	{
		if (*pdwEffect != DROPEFFECT_COPY)
			*pdwEffect = grfKeyState & MK_CONTROL ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
	}
	else
	{
		*pdwEffect = DROPEFFECT_NONE;
	}
	return S_OK;
}

void CCrystalEditView::DoDragScroll(const POINT & point)
{
  RECT rcClientRect;
  GetClientRect(&rcClientRect);
  if (point.y < rcClientRect.top + DRAG_BORDER_Y)
    {
      HideDropIndicator ();
      ScrollUp();
      UpdateWindow();
      ShowDropIndicator(point);
    }
  else if (point.y >= rcClientRect.bottom - DRAG_BORDER_Y)
    {
      HideDropIndicator();
      ScrollDown();
      UpdateWindow();
      ShowDropIndicator(point);
    }
  else if (point.x < rcClientRect.left + GetMarginWidth () + DRAG_BORDER_X)
    {
      HideDropIndicator();
      ScrollLeft();
      UpdateWindow();
      ShowDropIndicator(point);
    }
  else if (point.x >= rcClientRect.right - DRAG_BORDER_X)
    {
      HideDropIndicator();
      ScrollRight();
      UpdateWindow();
      ShowDropIndicator(point);
    }
}

BOOL CCrystalEditView::DoDropText(HGLOBAL hData, const POINT & ptClient)
{
  if (hData == NULL)
    return FALSE;

  POINT ptDropPos = ClientToText (ptClient);
  if (IsDraggingText () && IsInsideSelection (ptDropPos))
    {
      SetAnchor (ptDropPos);
      SetSelection (ptDropPos, ptDropPos);
      SetCursorPos (ptDropPos);
      EnsureVisible (ptDropPos);
      return FALSE;
    }

  SIZE_T cbData = ::GlobalSize(hData);
  int cchText = cbData / sizeof(TCHAR) - 1;
  if (cchText < 0)
    return FALSE;
  LPTSTR pszText = (LPTSTR)::GlobalLock (hData);
  if (pszText == NULL)
    return FALSE;

  // Open the undo group
  // When we drag from the same panel, it is already open, so do nothing
  // (we could test m_pTextBuffer->m_bUndoGroup if it were not a protected member)
  BOOL bGroupFlag = FALSE;
  if (! IsDraggingText())
    {
      m_pTextBuffer->BeginUndoGroup ();
      bGroupFlag = TRUE;
    } 

  int x, y;
  m_pTextBuffer->InsertText(this, ptDropPos.y, ptDropPos.x, pszText, cchText, y, x, CE_ACTION_DRAGDROP);  //   [JRT]

  POINT ptCurPos = { x, y };
  ASSERT_VALIDTEXTPOS (ptCurPos);
  SetAnchor(ptDropPos);
  SetSelection(ptDropPos, ptCurPos);
  SetCursorPos(ptCurPos);
  EnsureVisible(ptCurPos);

  if (bGroupFlag)
    m_pTextBuffer->FlushUndoGroup (this);

  ::GlobalUnlock (hData);
  return TRUE;
}

void CCrystalEditView::ShowDropIndicator(const POINT & point)
{
  if (!m_bDropPosVisible)
    {
      HideCursor();
      m_ptSavedCaretPos = GetCursorPos();
      m_bDropPosVisible = TRUE;
      ::CreateCaret(m_hWnd, (HBITMAP) 1, 2, GetLineHeight());
    }
  m_ptDropPos = ClientToText(point);
  // NB: m_ptDropPos.x is index into char array, which is uncomparable to m_nOffsetChar.
  POINT ptCaretPos = TextToClient(m_ptDropPos);
  if (ptCaretPos.x >= GetMarginWidth())
    {
      SetCaretPos(ptCaretPos.x, ptCaretPos.y);
      ShowCaret();
    }
  else
    {
      HideCaret ();
    }
}

void CCrystalEditView::HideDropIndicator()
{
  if (m_bDropPosVisible)
    {
      SetCursorPos (m_ptSavedCaretPos);
      ShowCursor ();
      m_bDropPosVisible = FALSE;
    }
}

DWORD CCrystalEditView::GetDropEffect()
{
	return DROPEFFECT_COPY | DROPEFFECT_MOVE;
}

void CCrystalEditView::OnDropSource(DWORD de)
{
  if (!IsDraggingText ())
    return;

  ASSERT_VALIDTEXTPOS (m_ptDraggedTextBegin);
  ASSERT_VALIDTEXTPOS (m_ptDraggedTextEnd);

  if (de == DROPEFFECT_MOVE)
    {
      m_pTextBuffer->DeleteText (this, m_ptDraggedTextBegin.y, m_ptDraggedTextBegin.x, m_ptDraggedTextEnd.y,
                                 m_ptDraggedTextEnd.x, CE_ACTION_DRAGDROP);     // [JRT]

    }
}

void CCrystalEditView::UpdateView(CCrystalTextView * pSource, CUpdateContext * pContext, DWORD dwFlags, int nLineIndex)
{
  CCrystalTextView::UpdateView(pSource, pContext, dwFlags, nLineIndex);

  if (m_bSelectionPushed && pContext != NULL)
    {
      pContext->RecalcPoint (m_ptSavedSelStart);
      pContext->RecalcPoint (m_ptSavedSelEnd);
      ASSERT_VALIDTEXTPOS (m_ptSavedSelStart);
      ASSERT_VALIDTEXTPOS (m_ptSavedSelEnd);
    }
  if (m_bDropPosVisible )
    {
      pContext->RecalcPoint (m_ptSavedCaretPos);
      ASSERT_VALIDTEXTPOS (m_ptSavedCaretPos);
    }
}

void CCrystalEditView::OnEditReplace()
{
	if (!QueryEditable())
		return;

	CEditReplaceDlg dlg(this);
	DWORD dwFlags = m_bLastReplace ? m_dwLastReplaceFlags :
		SettingStore.GetProfileInt(_T("Editor"), _T("ReplaceFlags"), 0);

	dlg.m_bMatchCase = (dwFlags & FIND_MATCH_CASE) != 0;
	dlg.m_bWholeWord = (dwFlags & FIND_WHOLE_WORD) != 0;
	dlg.m_bRegExp = (dwFlags & FIND_REGEXP) != 0;
	dlg.m_bNoWrap = (dwFlags & FIND_NO_WRAP) != 0;
	dlg.m_sText = m_strLastFindWhat;

	if (IsSelection())
	{
		GetSelection(m_ptSavedSelStart, m_ptSavedSelEnd);
		m_bSelectionPushed = TRUE;

		dlg.SetScope(TRUE);       //  Replace in current selection
		dlg.m_ptCurrentPos = m_ptSavedSelStart;
		dlg.m_bEnableScopeSelection = TRUE;
		dlg.m_ptBlockBegin = m_ptSavedSelStart;
		dlg.m_ptBlockEnd = m_ptSavedSelEnd;

		// If the selection is in one line, copy text to dialog
		if (m_ptSavedSelStart.y == m_ptSavedSelEnd.y)
			GetText(m_ptSavedSelStart, m_ptSavedSelEnd, dlg.m_sText);
	}
	else
	{
		dlg.SetScope(FALSE);      // Set scope when no selection
		dlg.m_ptCurrentPos = GetCursorPos();
		dlg.m_bEnableScopeSelection = FALSE;

		POINT ptCursorPos = GetCursorPos();
		POINT ptStart = WordToLeft(ptCursorPos);
		POINT ptEnd = WordToRight(ptCursorPos);
		if (IsValidTextPos(ptStart) && IsValidTextPos(ptEnd) && ptStart != ptEnd)
			GetText(ptStart, ptEnd, dlg.m_sText);
	}

	//  Execute Replace dialog
	LanguageSelect.DoModal(dlg);

	if (dlg.m_bConfirmed)
	{
		//  Save Replace parameters for 'F3' command
		m_bLastReplace = TRUE;
		m_strLastFindWhat = dlg.m_sText;
		m_dwLastReplaceFlags = 0;
		if (dlg.m_bMatchCase)
			m_dwLastReplaceFlags |= FIND_MATCH_CASE;
		if (dlg.m_bWholeWord)
			m_dwLastReplaceFlags |= FIND_WHOLE_WORD;
		if (dlg.m_bRegExp)
			m_dwLastReplaceFlags |= FIND_REGEXP;
		if (dlg.m_bNoWrap)
			m_dwLastReplaceFlags |= FIND_NO_WRAP;
		//  Save search parameters to registry
		SettingStore.WriteProfileInt(_T("Editor"), _T("ReplaceFlags"), m_dwLastReplaceFlags);
	}
	//  Restore selection
	if (m_bSelectionPushed)
	{
		SetSelection (m_ptSavedSelStart, m_ptSavedSelEnd);
		m_bSelectionPushed = FALSE;
	}
}

/**
 * @brief Replace selected text.
 * This function replaces selected text in the editor pane with given text.
 * @param [in] pszNewText The text replacing selected text.
 * @param [in] cchNewText Length of the replacing text.
 * @param [in] dwFlags Additional modifier flags:
 * - FIND_REGEXP: use the regular expression.
 * @return TRUE if succeeded.
 */
BOOL CCrystalEditView::ReplaceSelection(LPCTSTR pszNewText, int cchNewText, DWORD dwFlags)
{
  if (!cchNewText)
    return DeleteCurrentSelection();
  ASSERT(pszNewText);

  m_pTextBuffer->BeginUndoGroup();

  POINT ptCursorPos;
  if (IsSelection ())
    {
      POINT ptSelStart, ptSelEnd;
      GetSelection (ptSelStart, ptSelEnd);

      ptCursorPos = ptSelStart;

      m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_REPLACE);
    }
  else
    ptCursorPos = GetCursorPos ();
  ASSERT_VALIDTEXTPOS (ptCursorPos);

  int x = 0;
  int y = 0;
  m_pTextBuffer->InsertText(this, ptCursorPos.y, ptCursorPos.x, pszNewText, cchNewText, y, x, CE_ACTION_REPLACE);  //  [JRT]
  m_nLastReplaceLen = cchNewText;

  POINT ptEndOfBlock = { x, y };
  ASSERT_VALIDTEXTPOS(ptCursorPos);
  ASSERT_VALIDTEXTPOS(ptEndOfBlock);
  SetAnchor(ptEndOfBlock);
  SetSelection(ptCursorPos, ptEndOfBlock);
  SetCursorPos(ptEndOfBlock);

  m_pTextBuffer->FlushUndoGroup(this);

  return TRUE;
}

BOOL CCrystalEditView::DoEditUndo()
{
  if (m_pTextBuffer != NULL && m_pTextBuffer->CanUndo())
    {
      POINT ptCursorPos;
      if (m_pTextBuffer->Undo(this, ptCursorPos))
        {
          ASSERT_VALIDTEXTPOS(ptCursorPos);
          SetAnchor(ptCursorPos);
          SetSelection(ptCursorPos, ptCursorPos);
          SetCursorPos(ptCursorPos);
          EnsureVisible(ptCursorPos);
          return TRUE;
        }
    }
  return FALSE;
}

BOOL CCrystalEditView::DoEditRedo()
{
  if (m_pTextBuffer != NULL && m_pTextBuffer->CanRedo())
    {
      POINT ptCursorPos;
      if (m_pTextBuffer->Redo(this, ptCursorPos))
        {
          ASSERT_VALIDTEXTPOS(ptCursorPos);
          SetAnchor(ptCursorPos);
          SetSelection(ptCursorPos, ptCursorPos);
          SetCursorPos(ptCursorPos);
          EnsureVisible(ptCursorPos);
          return TRUE;
        }
    }
  return FALSE;
}

static bool isopenbrace(TCHAR c)
{
	return c == _T ('{') || c == _T ('(') || c == _T ('[') || c == _T ('<');
}

static bool isclosebrace(TCHAR c)
{
	return c == _T ('}') || c == _T ('}') || c == _T (']') || c == _T ('>');
}

static bool isopenbrace(LPCTSTR s)
{
	return s[1] == _T ('\0') && isopenbrace (*s);
}

static bool isclosebrace(LPCTSTR s)
{
	return s[1] == _T ('\0') && isclosebrace (*s);
}

int bracetype(TCHAR c)
{
	static const TCHAR braces[] = _T("{}()[]<>");
	LPCTSTR pos = _tcschr(braces, c);
	return pos ? pos - braces + 1 : 0;
}

static int bracetype(LPCTSTR s)
{
	if (s[1])
		return 0;
	return bracetype(*s);
}

void CCrystalEditView::OnEditOperation(int nAction, LPCTSTR pszText)
{
  if (m_bAutoIndent)
    {
      //  Analyse last action...
      if (nAction == CE_ACTION_TYPING && _tcscmp(pszText, _T("\r\n")) == 0 && !m_bOvrMode)
        {
          //  Enter stroke!
          POINT ptCursorPos = GetCursorPos();
          ASSERT(ptCursorPos.y > 0);

          //  Take indentation from the previos line
          int nLength = m_pTextBuffer->GetLineLength(ptCursorPos.y - 1);
          LPCTSTR pszLineChars = m_pTextBuffer->GetLineChars(ptCursorPos.y - 1);
          int nPos = 0;
          while (nPos < nLength && xisspace(pszLineChars[nPos]))
            nPos++;

          if (nPos > 0)
            {
              if ((GetFlags () & SRCOPT_BRACEGNU) && isclosebrace (pszLineChars[nLength - 1]) && ptCursorPos.y > 0 && nPos && nPos == nLength - 1)
                {
                  if (pszLineChars[nPos - 1] == _T ('\t'))
                    {
                      nPos--;
                    }
                  else
                    {
                      int nTabSize = GetTabSize (),
                        nDelta = nTabSize - nPos % nTabSize;
                      if (!nDelta)
                        {
                          nDelta = nTabSize;
                        }
                      nPos -= nDelta;
                      if (nPos < 0)
                        {
                          nPos = 0;
                        }
                    }
                }
              //  Insert part of the previos line
              TCHAR *pszInsertStr;
              if ((GetFlags () & (SRCOPT_BRACEGNU|SRCOPT_BRACEANSI)) && isopenbrace (pszLineChars[nLength - 1]))
                {
                  if (m_pTextBuffer->GetInsertTabs())
                    {
                      pszInsertStr = (TCHAR *) _alloca (sizeof (TCHAR) * (nPos + 2));
                      _tcsncpy (pszInsertStr, pszLineChars, nPos);
                      pszInsertStr[nPos++] = _T ('\t');
                    }
                  else
                    {
                      int nTabSize = GetTabSize ();
                      int nChars = nTabSize - nPos % nTabSize;
                      pszInsertStr = (TCHAR *) _alloca (sizeof (TCHAR) * (nPos + nChars + 1));
                      _tcsncpy (pszInsertStr, pszLineChars, nPos);
                      while (nChars--)
                        {
                          pszInsertStr[nPos++] = _T (' ');
                        }
                    }
                }
              else
                {
                  pszInsertStr = (TCHAR *) _alloca (sizeof (TCHAR) * (nPos + 1));
                  _tcsncpy (pszInsertStr, pszLineChars, nPos);
                }
              pszInsertStr[nPos] = 0;

              // m_pTextBuffer->BeginUndoGroup ();
              int x, y;
              m_pTextBuffer->InsertText (NULL, ptCursorPos.y, ptCursorPos.x,
                                         pszInsertStr, nPos, y, x, CE_ACTION_AUTOINDENT);
			  POINT pt = { x, y };
              SetCursorPos (pt);
              SetSelection (pt, pt);
              SetAnchor (pt);
              EnsureVisible (pt);
              // m_pTextBuffer->FlushUndoGroup (this);
            }
          else
            {
              //  Insert part of the previos line
              TCHAR *pszInsertStr;
              if ((GetFlags () & (SRCOPT_BRACEGNU|SRCOPT_BRACEANSI)) && isopenbrace (pszLineChars[nLength - 1]))
                {
                  if (m_pTextBuffer->GetInsertTabs())
                    {
                      pszInsertStr = (TCHAR *) _alloca (sizeof (TCHAR) * 2);
                      pszInsertStr[nPos++] = _T ('\t');
                    }
                  else
                    {
                      int nTabSize = GetTabSize ();
                      int nChars = nTabSize - nPos % nTabSize;
                      pszInsertStr = (TCHAR *) _alloca (sizeof (TCHAR) * (nChars + 1));
                      while (nChars--)
                        {
                          pszInsertStr[nPos++] = _T (' ');
                        }
                    }
                  pszInsertStr[nPos] = 0;

                  // m_pTextBuffer->BeginUndoGroup ();
                  int x, y;
                  m_pTextBuffer->InsertText (NULL, ptCursorPos.y, ptCursorPos.x,
                                             pszInsertStr, nPos, y, x, CE_ACTION_AUTOINDENT);
				  POINT pt = { x, y };
                  SetCursorPos (pt);
                  SetSelection (pt, pt);
                  SetAnchor (pt);
                  EnsureVisible (pt);
                  // m_pTextBuffer->FlushUndoGroup (this);
                }
            }
        }
      else if (nAction == CE_ACTION_TYPING && (GetFlags () & SRCOPT_FNBRACE) && bracetype (pszText) == 3)
        {
          //  Enter stroke!
          POINT ptCursorPos = GetCursorPos();
          LPCTSTR pszChars = m_pTextBuffer->GetLineChars(ptCursorPos.y);
          if (ptCursorPos.x > 1 && xisalnum (pszChars[ptCursorPos.x - 2]))
            {
              LPTSTR pszInsertStr = (TCHAR *) _alloca (sizeof (TCHAR) * 2);
              *pszInsertStr = _T (' ');
              pszInsertStr[1] = _T ('\0');
              // m_pTextBuffer->BeginUndoGroup ();
              int x, y;
              m_pTextBuffer->InsertText (NULL, ptCursorPos.y, ptCursorPos.x - 1,
                                         pszInsertStr, 1, y, x, CE_ACTION_AUTOINDENT);
              ptCursorPos.x = x + 1;
              ptCursorPos.y = y;
              SetCursorPos (ptCursorPos);
              SetSelection (ptCursorPos, ptCursorPos);
              SetAnchor (ptCursorPos);
              EnsureVisible (ptCursorPos);
              // m_pTextBuffer->FlushUndoGroup (this);
            }
        }
      else if (nAction == CE_ACTION_TYPING && (GetFlags () & SRCOPT_BRACEGNU) && isopenbrace (pszText))
        {
          //  Enter stroke!
          POINT ptCursorPos = GetCursorPos ();

          //  Take indentation from the previos line
          int nLength = m_pTextBuffer->GetLineLength (ptCursorPos.y);
          LPCTSTR pszLineChars = m_pTextBuffer->GetLineChars (ptCursorPos.y );
          int nPos = 0;
          while (nPos < nLength && xisspace (pszLineChars[nPos]))
            nPos++;
          if (nPos == nLength - 1)
            {
              TCHAR *pszInsertStr;
              if (m_pTextBuffer->GetInsertTabs())
                {
                  pszInsertStr = (TCHAR *) _alloca (sizeof (TCHAR) * 2);
                  *pszInsertStr = _T ('\t');
                  nPos = 1;
                }
              else
                {
                  int nTabSize = GetTabSize ();
                  int nChars = nTabSize - nPos % nTabSize;
                  pszInsertStr = (TCHAR *) _alloca (sizeof (TCHAR) * (nChars + 1));
                  nPos = 0;
                  while (nChars--)
                    {
                      pszInsertStr[nPos++] = _T (' ');
                    }
                }
              pszInsertStr[nPos] = 0;

              // m_pTextBuffer->BeginUndoGroup ();
              int x, y;
              m_pTextBuffer->InsertText (NULL, ptCursorPos.y, ptCursorPos.x - 1,
                                         pszInsertStr, nPos, y, x, CE_ACTION_AUTOINDENT);
			  POINT pt = { x + 1, y };
              SetCursorPos (pt);
              SetSelection (pt, pt);
              SetAnchor (pt);
              EnsureVisible (pt);
              // m_pTextBuffer->FlushUndoGroup (this);
            }
        }
      else if (nAction == CE_ACTION_TYPING && (GetFlags () & (SRCOPT_BRACEGNU|SRCOPT_BRACEANSI)) && isclosebrace (pszText))
        {
          //  Enter stroke!
          POINT ptCursorPos = GetCursorPos ();

          //  Take indentation from the previos line
          int nLength = m_pTextBuffer->GetLineLength (ptCursorPos.y);
          LPCTSTR pszLineChars = m_pTextBuffer->GetLineChars (ptCursorPos.y );
          int nPos = 0;
          while (nPos < nLength && xisspace (pszLineChars[nPos]))
            nPos++;
          if (ptCursorPos.y > 0 && nPos && nPos == nLength - 1)
            {
              if (pszLineChars[nPos - 1] == _T ('\t'))
                {
                  nPos = 1;
                }
              else
                {
                  int nTabSize = GetTabSize ();
                  nPos = nTabSize - (ptCursorPos.x - 1) % nTabSize;
                  if (!nPos)
                    {
                      nPos = nTabSize;
                    }
                  if (nPos > nLength - 1)
                    {
                      nPos = nLength - 1;
                    }
                }
              // m_pTextBuffer->BeginUndoGroup ();
              m_pTextBuffer->DeleteText (NULL, ptCursorPos.y, ptCursorPos.x - nPos - 1,
                ptCursorPos.y, ptCursorPos.x - 1, CE_ACTION_AUTOINDENT);
              ptCursorPos.x -= nPos;
              SetCursorPos (ptCursorPos);
              SetSelection (ptCursorPos, ptCursorPos);
              SetAnchor (ptCursorPos);
              EnsureVisible (ptCursorPos);
              // m_pTextBuffer->FlushUndoGroup (this);
            }
        }
    }
}

void CCrystalEditView::OnEditLowerCase()
{
  if (IsSelection ())
    {
      POINT ptCursorPos = GetCursorPos ();
      POINT ptSelStart, ptSelEnd;
      GetSelection (ptSelStart, ptSelEnd);
      String text;
      GetText (ptSelStart, ptSelEnd, text);
	  CharLower(&text.front());

      m_pTextBuffer->BeginUndoGroup ();

      if (IsSelection ())
        {
          POINT ptSelStart, ptSelEnd;
          GetSelection (ptSelStart, ptSelEnd);
    
          ptCursorPos = ptSelStart;
          /*SetAnchor (ptCursorPos);
          SetSelection (ptCursorPos, ptCursorPos);
          SetCursorPos (ptCursorPos);
          EnsureVisible (ptCursorPos);*/
    
          // [JRT]:
          m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_LOWERCASE);
        }

      int x, y;
      m_pTextBuffer->InsertText (this, ptSelStart.y, ptSelStart.x, text.c_str(), text.size(), y, x, CE_ACTION_LOWERCASE);

      ASSERT_VALIDTEXTPOS (ptCursorPos);
      SetAnchor (ptCursorPos);
      SetSelection (ptSelStart, ptSelEnd);
      SetCursorPos (ptCursorPos);
      EnsureVisible (ptCursorPos);

      m_pTextBuffer->FlushUndoGroup (this);
    }
}

void CCrystalEditView::OnEditUpperCase()
{
  if (IsSelection ())
    {
      POINT ptCursorPos = GetCursorPos ();
      POINT ptSelStart, ptSelEnd;
      GetSelection (ptSelStart, ptSelEnd);
      String text;
      GetText (ptSelStart, ptSelEnd, text);
      CharUpper(&text.front());

      m_pTextBuffer->BeginUndoGroup ();

      if (IsSelection ())
        {
          POINT ptSelStart, ptSelEnd;
          GetSelection (ptSelStart, ptSelEnd);
    
          ptCursorPos = ptSelStart;
          /*SetAnchor (ptCursorPos);
          SetSelection (ptCursorPos, ptCursorPos);
          SetCursorPos (ptCursorPos);
          EnsureVisible (ptCursorPos);*/
    
          // [JRT]:
          m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_UPPERCASE);
        }

      int x, y;
      m_pTextBuffer->InsertText (this, ptSelStart.y, ptSelStart.x, text.c_str(), text.size(), y, x, CE_ACTION_UPPERCASE);

      ASSERT_VALIDTEXTPOS (ptCursorPos);
      SetAnchor (ptCursorPos);
      SetSelection (ptSelStart, ptSelEnd);
      SetCursorPos (ptCursorPos);
      EnsureVisible (ptCursorPos);

      m_pTextBuffer->FlushUndoGroup (this);
    }
}

void CCrystalEditView::OnEditSwapCase()
{
  if (IsSelection ())
    {
      POINT ptCursorPos = GetCursorPos ();
      POINT ptSelStart, ptSelEnd;
      GetSelection (ptSelStart, ptSelEnd);
      String text;
      GetText (ptSelStart, ptSelEnd, text);
      int nLen = text.size();
      LPTSTR pszText = &text.front();
      while (*pszText)
        *pszText++ = (TCHAR)(_istlower (*pszText) ? _totupper (*pszText) : _totlower (*pszText));

      m_pTextBuffer->BeginUndoGroup ();

      if (IsSelection ())
        {
          POINT ptSelStart, ptSelEnd;
          GetSelection (ptSelStart, ptSelEnd);
    
          ptCursorPos = ptSelStart;
          /*SetAnchor (ptCursorPos);
          SetSelection (ptCursorPos, ptCursorPos);
          SetCursorPos (ptCursorPos);
          EnsureVisible (ptCursorPos);*/
    
          // [JRT]:
          m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_SWAPCASE);
        }

      int x, y;
      m_pTextBuffer->InsertText (this, ptSelStart.y, ptSelStart.x, text.c_str(), text.size(), y, x, CE_ACTION_SWAPCASE);

      ASSERT_VALIDTEXTPOS (ptCursorPos);
      SetAnchor (ptCursorPos);
      SetSelection (ptSelStart, ptSelEnd);
      SetCursorPos (ptCursorPos);
      EnsureVisible (ptCursorPos);

      m_pTextBuffer->FlushUndoGroup (this);
    }
}

void CCrystalEditView::OnEditCapitalize()
{
  if (IsSelection ())
    {
      POINT ptCursorPos = GetCursorPos ();
      POINT ptSelStart, ptSelEnd;
      GetSelection (ptSelStart, ptSelEnd);
      String text;
      GetText (ptSelStart, ptSelEnd, text);
      int nLen = text.size();
      LPTSTR pszText = &text.front();
      bool bCapitalize = true;
      while (*pszText)
        {
          if (xisspace (*pszText))
            bCapitalize = true;
          else if (_istalpha (*pszText))
            if (bCapitalize)
              {
                *pszText = (TCHAR)_totupper (*pszText);
                bCapitalize = false;
              }
            else
              *pszText = (TCHAR)_totlower (*pszText);
          pszText++;
        }

	  m_pTextBuffer->BeginUndoGroup ();

      if (IsSelection ())
        {
          POINT ptSelStart, ptSelEnd;
          GetSelection (ptSelStart, ptSelEnd);
    
          ptCursorPos = ptSelStart;
          /*SetAnchor (ptCursorPos);
          SetSelection (ptCursorPos, ptCursorPos);
          SetCursorPos (ptCursorPos);
          EnsureVisible (ptCursorPos);*/
    
          // [JRT]:
          m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_CAPITALIZE);
        }

      int x, y;
      m_pTextBuffer->InsertText (this, ptSelStart.y, ptSelStart.x, text.c_str(), text.size(), y, x, CE_ACTION_CAPITALIZE);

      ASSERT_VALIDTEXTPOS (ptCursorPos);
      SetAnchor (ptCursorPos);
      SetSelection (ptSelStart, ptSelEnd);
      SetCursorPos (ptCursorPos);
      EnsureVisible (ptCursorPos);

      m_pTextBuffer->FlushUndoGroup (this);
    }
}

void CCrystalEditView::OnEditSentence()
{
  if (IsSelection ())
    {
      POINT ptCursorPos = GetCursorPos ();
      POINT ptSelStart, ptSelEnd;
      GetSelection (ptSelStart, ptSelEnd);
      String text;
      GetText (ptSelStart, ptSelEnd, text);
      int nLen = text.size();
      LPTSTR pszText = &text.front();
      bool bCapitalize = true;
      while (*pszText)
        {
          if (!xisspace (*pszText))
            if (*pszText == _T ('.'))
              {
                if (pszText[1] && !_istdigit (pszText[1]))
                  bCapitalize = true;
              }
            else if (_istalpha (*pszText))
              if (bCapitalize)
                {
                  *pszText = (TCHAR)_totupper (*pszText);
                  bCapitalize = false;
                }
              else
                *pszText = (TCHAR)_totlower (*pszText);
          pszText++;
        }

      m_pTextBuffer->BeginUndoGroup ();

      if (IsSelection ())
        {
          POINT ptSelStart, ptSelEnd;
          GetSelection (ptSelStart, ptSelEnd);
    
          ptCursorPos = ptSelStart;
          /*SetAnchor (ptCursorPos);
          SetSelection (ptCursorPos, ptCursorPos);
          SetCursorPos (ptCursorPos);
          EnsureVisible (ptCursorPos);*/
    
          // [JRT]:
          m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_SENTENCIZE);
        }

      int x, y;
      m_pTextBuffer->InsertText (this, ptSelStart.y, ptSelStart.x, text.c_str(), text.size(), y, x, CE_ACTION_SENTENCIZE);

      ASSERT_VALIDTEXTPOS (ptCursorPos);
      SetAnchor (ptCursorPos);
      SetSelection (ptSelStart, ptSelEnd);
      SetCursorPos (ptCursorPos);
      EnsureVisible (ptCursorPos);

      m_pTextBuffer->FlushUndoGroup (this);
    }
}

//BEGIN SW
void CCrystalEditView::OnEditGotoLastChange()
{
	POINT ptLastChange = m_pTextBuffer->GetLastChangePos();
	if( ptLastChange.x < 0 || ptLastChange.y < 0 )
		return;

	// goto last change
	SetCursorPos( ptLastChange );
	SetSelection( ptLastChange, ptLastChange );
	EnsureVisible( ptLastChange );
}
//END SW

void CCrystalEditView::OnEditDeleteWord()
{
  if (!IsSelection())
    MoveWordRight(TRUE);
  if (IsSelection())
    {
      POINT ptSelStart, ptSelEnd;
      GetSelection (ptSelStart, ptSelEnd);

      m_pTextBuffer->BeginUndoGroup ();

      m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_DELETE);

      ASSERT_VALIDTEXTPOS (ptSelStart);
      SetAnchor (ptSelStart);
      SetSelection (ptSelStart, ptSelStart);
      SetCursorPos (ptSelStart);
      EnsureVisible (ptSelStart);

      m_pTextBuffer->FlushUndoGroup (this);
    }
}

void CCrystalEditView::OnEditDeleteWordBack()
{
  if (!IsSelection())
    MoveWordLeft(TRUE);
  if (IsSelection())
    {
      POINT ptSelStart, ptSelEnd;
      GetSelection (ptSelStart, ptSelEnd);

      m_pTextBuffer->BeginUndoGroup ();

      m_pTextBuffer->DeleteText (this, ptSelStart.y, ptSelStart.x, ptSelEnd.y, ptSelEnd.x, CE_ACTION_DELETE);

      ASSERT_VALIDTEXTPOS (ptSelStart);
      SetAnchor (ptSelStart);
      SetSelection (ptSelStart, ptSelStart);
      SetCursorPos (ptSelStart);
      EnsureVisible (ptSelStart);

      m_pTextBuffer->FlushUndoGroup (this);
    }
}
