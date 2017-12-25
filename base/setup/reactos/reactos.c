/*
 *  ReactOS applications
 *  Copyright (C) 2004-2008 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS GUI first stage setup application
 * FILE:        base/setup/reactos/reactos.c
 * PROGRAMMERS: Matthias Kupfer
 *              Dmitry Chapyshev (dmitry@reactos.org)
 */

#include "reactos.h"
#include "resource.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS ******************************************************************/

HANDLE ProcessHeap;
BOOLEAN IsUnattendedSetup = FALSE;
SETUPDATA SetupData;


/* FUNCTIONS ****************************************************************/

LONG LoadGenentry(HINF hinf,PCTSTR name,PGENENTRY *gen,PINFCONTEXT context);

static VOID
CenterWindow(HWND hWnd)
{
    HWND hWndParent;
    RECT rcParent;
    RECT rcWindow;

    hWndParent = GetParent(hWnd);
    if (hWndParent == NULL)
        hWndParent = GetDesktopWindow();

    GetWindowRect(hWndParent, &rcParent);
    GetWindowRect(hWnd, &rcWindow);

    SetWindowPos(hWnd,
                 HWND_TOP,
                 ((rcParent.right - rcParent.left) - (rcWindow.right - rcWindow.left)) / 2,
                 ((rcParent.bottom - rcParent.top) - (rcWindow.bottom - rcWindow.top)) / 2,
                 0,
                 0,
                 SWP_NOSIZE);
}

static HFONT
CreateTitleFont(VOID)
{
    NONCLIENTMETRICS ncm;
    LOGFONT LogFont;
    HDC hdc;
    INT FontSize;
    HFONT hFont;

    ncm.cbSize = sizeof(NONCLIENTMETRICS);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &ncm, 0);

    LogFont = ncm.lfMessageFont;
    LogFont.lfWeight = FW_BOLD;
    _tcscpy(LogFont.lfFaceName, _T("MS Shell Dlg"));

    hdc = GetDC(NULL);
    FontSize = 12;
    LogFont.lfHeight = 0 - GetDeviceCaps (hdc, LOGPIXELSY) * FontSize / 72;
    hFont = CreateFontIndirect(&LogFont);
    ReleaseDC(NULL, hdc);

    return hFont;
}

INT DisplayError(
    IN HWND hParentWnd OPTIONAL,
    IN UINT uIDTitle,
    IN UINT uIDMessage)
{
    WCHAR message[512], caption[64];

    LoadStringW(SetupData.hInstance, uIDMessage, message, ARRAYSIZE(message));
    LoadStringW(SetupData.hInstance, uIDTitle, caption, ARRAYSIZE(caption));

    return MessageBoxW(hParentWnd, message, caption, MB_OK | MB_ICONERROR);
}

static INT_PTR CALLBACK
StartDlgProc(
    IN HWND hwndDlg,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    PSETUPDATA pSetupData;

    /* Retrieve pointer to the global setup data */
    pSetupData = (PSETUPDATA)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            /* Save pointer to the global setup data */
            pSetupData = (PSETUPDATA)((LPPROPSHEETPAGE)lParam)->lParam;
            SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (DWORD_PTR)pSetupData);

            /* Center the wizard window */
            CenterWindow(GetParent(hwndDlg));

            /* Set title font */
            SendDlgItemMessage(hwndDlg,
                               IDC_STARTTITLE,
                               WM_SETFONT,
                               (WPARAM)pSetupData->hTitleFont,
                               (LPARAM)TRUE);
            break;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_NEXT);
                    break;

                default:
                    break;
            }
        }
        break;

        default:
            break;

    }

    return FALSE;
}

static INT_PTR CALLBACK
TypeDlgProc(
    IN HWND hwndDlg,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    PSETUPDATA pSetupData;

    /* Retrieve pointer to the global setup data */
    pSetupData = (PSETUPDATA)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            /* Save pointer to the global setup data */
            pSetupData = (PSETUPDATA)((LPPROPSHEETPAGE)lParam)->lParam;
            SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (DWORD_PTR)pSetupData);

            /* Check the 'install' radio button */
            CheckDlgButton(hwndDlg, IDC_INSTALL, BST_CHECKED);

            /* Disable the 'update' radio button and text */
            EnableWindow(GetDlgItem(hwndDlg, IDC_UPDATE), FALSE);
            EnableWindow(GetDlgItem(hwndDlg, IDC_UPDATETEXT), FALSE);
            break;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_NEXT | PSWIZB_BACK);
                    break;

                case PSN_QUERYCANCEL:
                    SetWindowLongPtr(hwndDlg,
                                     DWLP_MSGRESULT,
                                     MessageBox(GetParent(hwndDlg),
                                                pSetupData->szAbortMessage,
                                                pSetupData->szAbortTitle,
                                                MB_YESNO | MB_ICONQUESTION) != IDYES);
                    return TRUE;

                case PSN_WIZNEXT: // set the selected data
                    pSetupData->RepairUpdateFlag = !(SendMessage(GetDlgItem(hwndDlg, IDC_INSTALL),
                                                                 BM_GETCHECK,
                                                                 (WPARAM) 0,
                                                                 (LPARAM) 0) == BST_CHECKED);
                    return TRUE;

                default:
                    break;
            }
        }
        break;

        default:
            break;

    }
    return FALSE;
}

#define MAX_LIST_COLUMNS (IDS_LIST_COLUMN_LAST - IDS_LIST_COLUMN_FIRST + 1)
static const UINT column_ids[MAX_LIST_COLUMNS] = {IDS_LIST_COLUMN_FIRST, IDS_LIST_COLUMN_FIRST + 1, IDS_LIST_COLUMN_FIRST + 2};
static const INT  column_widths[MAX_LIST_COLUMNS] = {200, 150, 150};
static const INT  column_alignment[MAX_LIST_COLUMNS] = {LVCFMT_LEFT, LVCFMT_LEFT, LVCFMT_LEFT};

static INT_PTR CALLBACK
UpgradeRepairDlgProc(
    IN HWND hwndDlg,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    PSETUPDATA pSetupData;
    HWND hList;

    /* Retrieve pointer to the global setup data */
    pSetupData = (PSETUPDATA)GetWindowLongPtrW(hwndDlg, GWL_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            /* Save pointer to the global setup data */
            pSetupData = (PSETUPDATA)((LPPROPSHEETPAGE)lParam)->lParam;
            SetWindowLongPtrW(hwndDlg, GWL_USERDATA, (DWORD_PTR)pSetupData);

            hList = GetDlgItem(hwndDlg, IDC_LIST1);

            CreateListViewColumns(pSetupData->hInstance,
                                  hList,
                                  column_ids,
                                  column_widths,
                                  column_alignment,
                                  MAX_LIST_COLUMNS);

            break;
        }

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_NEXT | PSWIZB_BACK);
                    break;

                case PSN_QUERYCANCEL:
                    SetWindowLongPtrW(hwndDlg,
                                     DWL_MSGRESULT,
                                     MessageBox(GetParent(hwndDlg),
                                                pSetupData->szAbortMessage,
                                                pSetupData->szAbortTitle,
                                                MB_YESNO | MB_ICONQUESTION) != IDYES);
                    return TRUE;

                case PSN_WIZNEXT: // set the selected data
                    pSetupData->RepairUpdateFlag = !(SendMessageW(GetDlgItem(hwndDlg, IDC_INSTALL),
                                                                 BM_GETCHECK,
                                                                 (WPARAM) 0,
                                                                 (LPARAM) 0) == BST_CHECKED);
                    return TRUE;

                default:
                    break;
            }
        }
        break;

        default:
            break;

    }
    return FALSE;
}

static INT_PTR CALLBACK
DeviceDlgProc(
    IN HWND hwndDlg,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    PSETUPDATA pSetupData;
    LONG i;
    LRESULT tindex;
    HWND hList;

    /* Retrieve pointer to the global setup data */
    pSetupData = (PSETUPDATA)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            /* Save pointer to the global setup data */
            pSetupData = (PSETUPDATA)((LPPROPSHEETPAGE)lParam)->lParam;
            SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (DWORD_PTR)pSetupData);

            hList = GetDlgItem(hwndDlg, IDC_COMPUTER);

            for (i=0; i < pSetupData->CompCount; i++)
            {
                tindex = SendMessage(hList, CB_ADDSTRING, (WPARAM) 0, (LPARAM) pSetupData->pComputers[i].Value);
                SendMessage(hList, CB_SETITEMDATA, tindex, i);
            }
            SendMessage(hList, CB_SETCURSEL, 0, 0); // set first as default

            hList = GetDlgItem(hwndDlg, IDC_DISPLAY);

            for (i=0; i < pSetupData->DispCount; i++)
            {
                tindex = SendMessage(hList, CB_ADDSTRING, (WPARAM) 0, (LPARAM) pSetupData->pDisplays[i].Value);
                SendMessage(hList, CB_SETITEMDATA, tindex, i);
            }
            SendMessage(hList, CB_SETCURSEL, 0, 0); // set first as default

            hList = GetDlgItem(hwndDlg, IDC_KEYBOARD);

            for (i=0; i < pSetupData->KeybCount; i++)
            {
                tindex = SendMessage(hList,CB_ADDSTRING,(WPARAM)0,(LPARAM)pSetupData->pKeyboards[i].Value);
                SendMessage(hList,CB_SETITEMDATA,tindex,i);
            }
            SendMessage(hList,CB_SETCURSEL,0,0); // set first as default
            break;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_NEXT | PSWIZB_BACK);
                    break;

                case PSN_QUERYCANCEL:
                    SetWindowLongPtr(hwndDlg,
                                     DWLP_MSGRESULT,
                                     MessageBox(GetParent(hwndDlg),
                                                pSetupData->szAbortMessage,
                                                pSetupData->szAbortTitle,
                                             MB_YESNO | MB_ICONQUESTION) != IDYES);
                    return TRUE;

                case PSN_WIZNEXT: // set the selected data
                {
                    hList = GetDlgItem(hwndDlg, IDC_COMPUTER); 

                    tindex = SendMessage(hList, CB_GETCURSEL, (WPARAM) 0, (LPARAM) 0);
                    if (tindex != CB_ERR)
                    {
                        pSetupData->SelectedComputer = SendMessage(hList,
                                                                   CB_GETITEMDATA,
                                                                   (WPARAM) tindex,
                                                                   (LPARAM) 0);
                    }

                    hList = GetDlgItem(hwndDlg, IDC_DISPLAY);

                    tindex = SendMessage(hList, CB_GETCURSEL, (WPARAM) 0, (LPARAM) 0);
                    if (tindex != CB_ERR)
                    {
                        pSetupData->SelectedDisplay = SendMessage(hList,
                                                                  CB_GETITEMDATA,
                                                                  (WPARAM) tindex,
                                                                  (LPARAM) 0);
                    }

                    hList =GetDlgItem(hwndDlg, IDC_KEYBOARD);

                    tindex = SendMessage(hList, CB_GETCURSEL, (WPARAM) 0, (LPARAM) 0);
                    if (tindex != CB_ERR)
                    {
                        pSetupData->SelectedKeyboard = SendMessage(hList,
                                                                   CB_GETITEMDATA,
                                                                   (WPARAM) tindex,
                                                                   (LPARAM) 0);
                    }
                    return TRUE;
                }

                default:
                    break;
            }
        }
        break;

        default:
            break;

    }
    return FALSE;
}

static INT_PTR CALLBACK
SummaryDlgProc(
    IN HWND hwndDlg,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    PSETUPDATA pSetupData;

    /* Retrieve pointer to the global setup data */
    pSetupData = (PSETUPDATA)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            /* Save pointer to the global setup data */
            pSetupData = (PSETUPDATA)((LPPROPSHEETPAGE)lParam)->lParam;
            SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (DWORD_PTR)pSetupData);
            break;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE: 
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_NEXT | PSWIZB_BACK);
                    break;

                case PSN_QUERYCANCEL:
                    SetWindowLongPtr(hwndDlg,
                                     DWLP_MSGRESULT,
                                     MessageBox(GetParent(hwndDlg),
                                                pSetupData->szAbortMessage,
                                                pSetupData->szAbortTitle,
                                                MB_YESNO | MB_ICONQUESTION) != IDYES);
                    return TRUE;
                default:
                    break;
            }
        }
        break;

        default:
            break;
    }

    return FALSE;
}

static INT_PTR CALLBACK
ProcessDlgProc(
    IN HWND hwndDlg,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    PSETUPDATA pSetupData;

    /* Retrieve pointer to the global setup data */
    pSetupData = (PSETUPDATA)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            /* Save pointer to the global setup data */
            pSetupData = (PSETUPDATA)((LPPROPSHEETPAGE)lParam)->lParam;
            SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (DWORD_PTR)pSetupData);
            break;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE:
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_NEXT);
                   // disable all buttons during installation process
                   // PropSheet_SetWizButtons(GetParent(hwndDlg), 0 );
                   break;
                case PSN_QUERYCANCEL:
                    SetWindowLongPtr(hwndDlg,
                                     DWLP_MSGRESULT,
                                     MessageBox(GetParent(hwndDlg),
                                                pSetupData->szAbortMessage,
                                                pSetupData->szAbortTitle,
                                                MB_YESNO | MB_ICONQUESTION) != IDYES);
                    return TRUE;
                default:
                   break;
            }
        }
        break;

        default:
            break;

    }

    return FALSE;
}

static INT_PTR CALLBACK
RestartDlgProc(
    IN HWND hwndDlg,
    IN UINT uMsg,
    IN WPARAM wParam,
    IN LPARAM lParam)
{
    PSETUPDATA pSetupData;

    /* Retrieve pointer to the global setup data */
    pSetupData = (PSETUPDATA)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            /* Save pointer to the global setup data */
            pSetupData = (PSETUPDATA)((LPPROPSHEETPAGE)lParam)->lParam;
            SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (DWORD_PTR)pSetupData);

            /* Set title font */
            /*SendDlgItemMessage(hwndDlg,
                                 IDC_STARTTITLE,
                                 WM_SETFONT,
                                 (WPARAM)hTitleFont,
                                 (LPARAM)TRUE);*/
            break;

        case WM_TIMER:
        {
            INT Position;
            HWND hWndProgress;

            hWndProgress = GetDlgItem(hwndDlg, IDC_RESTART_PROGRESS);
            Position = SendMessage(hWndProgress, PBM_GETPOS, 0, 0);
            if (Position == 300)
            {
                KillTimer(hwndDlg, 1);
                PropSheet_PressButton(GetParent(hwndDlg), PSBTN_FINISH);
            }
            else
            {
                SendMessage(hWndProgress, PBM_SETPOS, Position + 1, 0);
            }
            return TRUE;
        }

        case WM_DESTROY:
            return TRUE;

        case WM_NOTIFY:
        {
            LPNMHDR lpnm = (LPNMHDR)lParam;

            switch (lpnm->code)
            {
                case PSN_SETACTIVE: // Only "Finish" for closing the App
                    PropSheet_SetWizButtons(GetParent(hwndDlg), PSWIZB_FINISH);
                    SendDlgItemMessage(hwndDlg, IDC_RESTART_PROGRESS, PBM_SETRANGE, 0, MAKELPARAM(0, 300));
                    SendDlgItemMessage(hwndDlg, IDC_RESTART_PROGRESS, PBM_SETPOS, 0, 0);
                    SetTimer(hwndDlg, 1, 50, NULL);
                    break;

                default:
                    break;
            }
        }
        break;

        default:
            break;

    }

    return FALSE;
}

BOOL LoadSetupData(
    IN OUT PSETUPDATA pSetupData)
{
    BOOL ret = TRUE;
    INFCONTEXT InfContext;
    TCHAR tmp[10];
    //TCHAR szValue[MAX_PATH];
    DWORD LineLength;
    LONG Count;

    // get language list
    pSetupData->LangCount = SetupGetLineCount(pSetupData->SetupInf, _T("Language"));
    if (pSetupData->LangCount > 0)
    {
        pSetupData->pLanguages = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LANG) * pSetupData->LangCount);
        if (pSetupData->pLanguages == NULL)
        {
            ret = FALSE;
            goto done;
        }

        Count = 0;
        if (SetupFindFirstLine(pSetupData->SetupInf, _T("Language"), NULL, &InfContext))
        {
            do
            {
                SetupGetStringField(&InfContext,
                                    0,
                                    pSetupData->pLanguages[Count].LangId,
                                    sizeof(pSetupData->pLanguages[Count].LangId) / sizeof(TCHAR),
                                    &LineLength);

                SetupGetStringField(&InfContext,
                                    1,
                                    pSetupData->pLanguages[Count].LangName,
                                    sizeof(pSetupData->pLanguages[Count].LangName) / sizeof(TCHAR),
                                    &LineLength);
                ++Count;
            }
            while (SetupFindNextLine(&InfContext, &InfContext) && Count < pSetupData->LangCount);
        }
    }

    // get keyboard layout list
    pSetupData->KbLayoutCount = SetupGetLineCount(pSetupData->SetupInf, _T("KeyboardLayout"));
    if (pSetupData->KbLayoutCount > 0)
    {
        pSetupData->pKbLayouts = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(KBLAYOUT) * pSetupData->KbLayoutCount);
        if (pSetupData->pKbLayouts == NULL)
        {
            ret = FALSE;
            goto done;
        }

        Count = 0;
        if (SetupFindFirstLine(pSetupData->SetupInf, _T("KeyboardLayout"), NULL, &InfContext))
        {
            do
            {
                SetupGetStringField(&InfContext,
                                    0,
                                    pSetupData->pKbLayouts[Count].LayoutId,
                                    sizeof(pSetupData->pKbLayouts[Count].LayoutId) / sizeof(TCHAR),
                                    &LineLength);

                SetupGetStringField(&InfContext,
                                    1,
                                    pSetupData->pKbLayouts[Count].LayoutName,
                                    sizeof(pSetupData->pKbLayouts[Count].LayoutName) / sizeof(TCHAR),
                                    &LineLength);
                ++Count;
            }
            while (SetupFindNextLine(&InfContext, &InfContext) && Count < pSetupData->KbLayoutCount);
        }
    }

    // get default for keyboard and language
    pSetupData->DefaultKBLayout = -1;
    pSetupData->DefaultLang = -1;

    // TODO: get defaults from underlaying running system
    if (SetupFindFirstLine(pSetupData->SetupInf, _T("NLS"), _T("DefaultLayout"), &InfContext))
    {
        SetupGetStringField(&InfContext, 1, tmp, sizeof(tmp) / sizeof(TCHAR), &LineLength);
        for (Count = 0; Count < pSetupData->KbLayoutCount; Count++)
        {
            if (_tcscmp(tmp, pSetupData->pKbLayouts[Count].LayoutId) == 0)
            {
                pSetupData->DefaultKBLayout = Count;
                break;
            }
        }
    }

    if (SetupFindFirstLine(pSetupData->SetupInf, _T("NLS"), _T("DefaultLanguage"), &InfContext))
    {
        SetupGetStringField(&InfContext, 1, tmp, sizeof(tmp) / sizeof(TCHAR), &LineLength);
        for (Count = 0; Count < pSetupData->LangCount; Count++)
        {
            if (_tcscmp(tmp, pSetupData->pLanguages[Count].LangId) == 0)
            {
                pSetupData->DefaultLang = Count;
                break;
            }
        }
    }

    // get computers list
    pSetupData->CompCount = LoadGenentry(pSetupData->SetupInf,_T("Computer"),&pSetupData->pComputers,&InfContext);

    // get display list
    pSetupData->DispCount = LoadGenentry(pSetupData->SetupInf,_T("Display"),&pSetupData->pDisplays,&InfContext);

    // get keyboard list
    pSetupData->KeybCount = LoadGenentry(pSetupData->SetupInf, _T("Keyboard"),&pSetupData->pKeyboards,&InfContext);

    // get install directory
    if (SetupFindFirstLine(pSetupData->SetupInf, _T("SetupData"), _T("DefaultPath"), &InfContext))
    {
        SetupGetStringField(&InfContext,
                            1,
                            pSetupData->USetupData.InstallationDirectory,
                            sizeof(pSetupData->USetupData.InstallationDirectory) / sizeof(TCHAR),
                            &LineLength);
    }

done:
    if (ret == FALSE)
    {
        if (pSetupData->pKbLayouts != NULL)
        {
            HeapFree(GetProcessHeap(), 0, pSetupData->pKbLayouts);
            pSetupData->pKbLayouts = NULL;
        }

        if (pSetupData->pLanguages != NULL)
        {
            HeapFree(GetProcessHeap(), 0, pSetupData->pLanguages);
            pSetupData->pLanguages = NULL;
        }
    }

    return ret;
}

LONG LoadGenentry(HINF hinf,PCTSTR name,PGENENTRY *gen,PINFCONTEXT context)
{
    LONG TotalCount;
    DWORD LineLength;

    TotalCount = SetupGetLineCount(hinf, name);
    if (TotalCount > 0)
    {
        *gen = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GENENTRY) * TotalCount);
        if (*gen != NULL)
        {
            if (SetupFindFirstLine(hinf, name, NULL, context))
            {
                LONG Count = 0;
                do
                {
                    SetupGetStringField(context,
                                        0,
                                        (*gen)[Count].Id,
                                        sizeof((*gen)[Count].Id) / sizeof(TCHAR),
                                        &LineLength);

                    SetupGetStringField(context,
                                        1,
                                        (*gen)[Count].Value,
                                        sizeof((*gen)[Count].Value) / sizeof(TCHAR),
                                        &LineLength);
                    ++Count;
                }
                while (SetupFindNextLine(context, context) && Count < TotalCount);
            }
        }
        else return 0;
    }
    return TotalCount;
}

/*
 * Attempts to convert a pure NT file path into a corresponding Win32 path.
 * Adapted from GetInstallSourceWin32() in dll/win32/syssetup/wizard.c
 */
BOOL
ConvertNtPathToWin32Path(
    OUT PWSTR pwszPath,
    IN DWORD cchPathMax,
    IN PCWSTR pwszNTPath)
{
    WCHAR wszDrives[512];
    WCHAR wszNTPath[512]; // MAX_PATH ?
    DWORD cchDrives;
    PWCHAR pwszDrive;

    *pwszPath = UNICODE_NULL;

    cchDrives = GetLogicalDriveStringsW(_countof(wszDrives) - 1, wszDrives);
    if (cchDrives == 0 || cchDrives >= _countof(wszDrives))
    {
        /* Buffer too small or failure */
        DPRINT1("GetLogicalDriveStringsW failed\n");
        return FALSE;
    }

    for (pwszDrive = wszDrives; *pwszDrive; pwszDrive += wcslen(pwszDrive) + 1)
    {
        /* Retrieve the NT path corresponding to the current Win32 DOS path */
        pwszDrive[2] = UNICODE_NULL; // Temporarily remove the backslash
        QueryDosDeviceW(pwszDrive, wszNTPath, _countof(wszNTPath));
        pwszDrive[2] = L'\\';        // Restore the backslash

        wcscat(wszNTPath, L"\\");    // Concat a backslash

        DPRINT1("Testing '%S' --> '%S'\n", pwszDrive, wszNTPath);

        /* Check whether the NT path corresponds to the NT installation source path */
        if (!_wcsnicmp(wszNTPath, pwszNTPath, wcslen(wszNTPath)))
        {
            /* Found it! */
            wsprintf(pwszPath, L"%s%s", // cchPathMax
                     pwszDrive, pwszNTPath + wcslen(wszNTPath));
            DPRINT1("ConvertNtPathToWin32Path: %S\n", pwszPath);
            return TRUE;
        }
    }

    return FALSE;
}

/* Used to enable and disable the shutdown privilege */
/* static */ BOOL
EnablePrivilege(LPCWSTR lpszPrivilegeName, BOOL bEnablePrivilege)
{
    BOOL   Success;
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;

    Success = OpenProcessToken(GetCurrentProcess(),
                               TOKEN_ADJUST_PRIVILEGES,
                               &hToken);
    if (!Success) return Success;

    Success = LookupPrivilegeValueW(NULL,
                                    lpszPrivilegeName,
                                    &tp.Privileges[0].Luid);
    if (!Success) goto Quit;

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = (bEnablePrivilege ? SE_PRIVILEGE_ENABLED : 0);

    Success = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);

Quit:
    CloseHandle(hToken);
    return Success;
}

int WINAPI
_tWinMain(HINSTANCE hInst,
          HINSTANCE hPrevInstance,
          LPTSTR lpszCmdLine,
          int nCmdShow)
{
    NTSTATUS Status;
    ULONG Error;
    PROPSHEETHEADER psh;
    HPROPSHEETPAGE ahpsp[8];
    PROPSHEETPAGE psp = {0};
    UINT nPages = 0;

    ProcessHeap = GetProcessHeap();

    /* Initialize global unicode strings */
    RtlInitUnicodeString(&SetupData.USetupData.SourcePath, NULL);
    RtlInitUnicodeString(&SetupData.USetupData.SourceRootPath, NULL);
    RtlInitUnicodeString(&SetupData.USetupData.SourceRootDir, NULL);
    // RtlInitUnicodeString(&InstallPath, NULL);
    RtlInitUnicodeString(&SetupData.USetupData.DestinationPath, NULL);
    RtlInitUnicodeString(&SetupData.USetupData.DestinationArcPath, NULL);
    RtlInitUnicodeString(&SetupData.USetupData.DestinationRootPath, NULL);
    RtlInitUnicodeString(&SetupData.USetupData.SystemRootPath, NULL);

    /* Get the source path and source root path */
    //
    // NOTE: Sometimes the source path may not be in SystemRoot !!
    // (and this is the case when using the 1st-stage GUI setup!)
    //
    Status = GetSourcePaths(&SetupData.USetupData.SourcePath,
                            &SetupData.USetupData.SourceRootPath,
                            &SetupData.USetupData.SourceRootDir);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("GetSourcePaths() failed (Status 0x%08lx)", Status);
        // MUIDisplayError(ERROR_NO_SOURCE_DRIVE, Ir, POPUP_WAIT_ENTER);
        MessageBoxW(NULL, L"GetSourcePaths failed!", L"Error", MB_ICONERROR);
        goto Quit;
    }
    DPRINT1("SourcePath: '%wZ'\n", &SetupData.USetupData.SourcePath);
    DPRINT1("SourceRootPath: '%wZ'\n", &SetupData.USetupData.SourceRootPath);
    DPRINT1("SourceRootDir: '%wZ'\n", &SetupData.USetupData.SourceRootDir);

    /* Load 'txtsetup.sif' from the installation media */
    Error = LoadSetupInf(&SetupData.SetupInf, &SetupData.USetupData);
    if (Error != ERROR_SUCCESS)
    {
        // MUIDisplayError(Error, Ir, POPUP_WAIT_ENTER);
        DisplayError(NULL, IDS_CAPTION, IDS_NO_TXTSETUP_SIF);
        goto Quit;
    }

    /* Load extra setup data (HW lists etc...) */
    if (!LoadSetupData(&SetupData))
        goto Quit;

    SetupData.hInstance = hInst;

    CheckUnattendedSetup(&SetupData.USetupData);
    SetupData.bUnattend = IsUnattendedSetup;

    LoadStringW(hInst, IDS_ABORTSETUP, SetupData.szAbortMessage, ARRAYSIZE(SetupData.szAbortMessage));
    LoadStringW(hInst, IDS_ABORTSETUP2, SetupData.szAbortTitle, ARRAYSIZE(SetupData.szAbortTitle));

    /* Create title font */
    SetupData.hTitleFont = CreateTitleFont();

    if (!SetupData.bUnattend)
    {
        /* Create the Start page, until setup is working */
        // NOTE: What does "until setup is working" mean??
        psp.dwSize = sizeof(PROPSHEETPAGE);
        psp.dwFlags = PSP_DEFAULT | PSP_HIDEHEADER;
        psp.hInstance = hInst;
        psp.lParam = (LPARAM)&SetupData;
        psp.pfnDlgProc = StartDlgProc;
        psp.pszTemplate = MAKEINTRESOURCE(IDD_STARTPAGE);
        ahpsp[nPages++] = CreatePropertySheetPage(&psp);

        /* Create the install type selection page */
        psp.dwSize = sizeof(PROPSHEETPAGE);
        psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
        psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_TYPETITLE);
        psp.pszHeaderSubTitle = MAKEINTRESOURCE(IDS_TYPESUBTITLE);
        psp.hInstance = hInst;
        psp.lParam = (LPARAM)&SetupData;
        psp.pfnDlgProc = TypeDlgProc;
        psp.pszTemplate = MAKEINTRESOURCE(IDD_TYPEPAGE);
        ahpsp[nPages++] = CreatePropertySheetPage(&psp);

        /* Create the upgrade/repair selection page */
        psp.dwSize = sizeof(PROPSHEETPAGE);
        psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
        psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_TYPETITLE);
        psp.pszHeaderSubTitle = MAKEINTRESOURCE(IDS_TYPESUBTITLE);
        psp.hInstance = hInst;
        psp.lParam = (LPARAM)&SetupData;
        psp.pfnDlgProc = UpgradeRepairDlgProc;
        psp.pszTemplate = MAKEINTRESOURCE(IDD_UPDATEREPAIRPAGE);
        ahpsp[nPages++] = CreatePropertySheetPage(&psp);

        /* Create the device settings page */
        psp.dwSize = sizeof(PROPSHEETPAGE);
        psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
        psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_DEVICETITLE);
        psp.pszHeaderSubTitle = MAKEINTRESOURCE(IDS_DEVICESUBTITLE);
        psp.hInstance = hInst;
        psp.lParam = (LPARAM)&SetupData;
        psp.pfnDlgProc = DeviceDlgProc;
        psp.pszTemplate = MAKEINTRESOURCE(IDD_DEVICEPAGE);
        ahpsp[nPages++] = CreatePropertySheetPage(&psp);

        /* Create the install device settings page / boot method / install directory */
        psp.dwSize = sizeof(PROPSHEETPAGE);
        psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
        psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_DRIVETITLE);
        psp.pszHeaderSubTitle = MAKEINTRESOURCE(IDS_DRIVESUBTITLE);
        psp.hInstance = hInst;
        psp.lParam = (LPARAM)&SetupData;
        psp.pfnDlgProc = DriveDlgProc;
        psp.pszTemplate = MAKEINTRESOURCE(IDD_DRIVEPAGE);
        ahpsp[nPages++] = CreatePropertySheetPage(&psp);

        /* Create the summary page */
        psp.dwSize = sizeof(PROPSHEETPAGE);
        psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
        psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_SUMMARYTITLE);
        psp.pszHeaderSubTitle = MAKEINTRESOURCE(IDS_SUMMARYSUBTITLE);
        psp.hInstance = hInst;
        psp.lParam = (LPARAM)&SetupData;
        psp.pfnDlgProc = SummaryDlgProc;
        psp.pszTemplate = MAKEINTRESOURCE(IDD_SUMMARYPAGE);
        ahpsp[nPages++] = CreatePropertySheetPage(&psp);
    }

    /* Create the installation progress page */
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE;
    psp.pszHeaderTitle = MAKEINTRESOURCE(IDS_PROCESSTITLE);
    psp.pszHeaderSubTitle = MAKEINTRESOURCE(IDS_PROCESSSUBTITLE);
    psp.hInstance = hInst;
    psp.lParam = (LPARAM)&SetupData;
    psp.pfnDlgProc = ProcessDlgProc;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_PROCESSPAGE);
    ahpsp[nPages++] = CreatePropertySheetPage(&psp);

    /* Create the finish-and-reboot page */
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_DEFAULT | PSP_HIDEHEADER;
    psp.hInstance = hInst;
    psp.lParam = (LPARAM)&SetupData;
    psp.pfnDlgProc = RestartDlgProc;
    psp.pszTemplate = MAKEINTRESOURCE(IDD_RESTARTPAGE);
    ahpsp[nPages++] = CreatePropertySheetPage(&psp);

    /* Create the property sheet */
    psh.dwSize = sizeof(PROPSHEETHEADER);
    psh.dwFlags = PSH_WIZARD97 | PSH_WATERMARK | PSH_HEADER;
    psh.hInstance = hInst;
    psh.hwndParent = NULL;
    psh.nPages = nPages;
    psh.nStartPage = 0;
    psh.phpage = ahpsp;
    psh.pszbmWatermark = MAKEINTRESOURCE(IDB_WATERMARK);
    psh.pszbmHeader = MAKEINTRESOURCE(IDB_HEADER);

    /* Display the wizard */
    PropertySheet(&psh);

    if (SetupData.hTitleFont)
        DeleteObject(SetupData.hTitleFont);

    SetupCloseInfFile(SetupData.SetupInf);

Quit:

#if 0 // NOTE: Disabled for testing purposes only!
    EnablePrivilege(SE_SHUTDOWN_NAME, TRUE);
    ExitWindowsEx(EWX_REBOOT, 0);
    EnablePrivilege(SE_SHUTDOWN_NAME, FALSE);
#endif

    return 0;
}

/* EOF */
