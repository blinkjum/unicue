﻿/************************************************************************/
/*                                                                      */
/* Unicue 1.2                                                           */
/* A tool to convert file from ansi code-page to Unicode                */
/*                                                                      */
/* Author:  kuyur (kuyur@kuyur.info)                                    */
/* Published under GPLv3                                                */
/* http://www.gnu.org/licenses/gpl-3.0.en.html                          */
/*                                                                      */
/* Project URL: http://github.com/kuyur/unicue                          */
/*                                                                      */
/************************************************************************/

#include "stdafx.h"
#include <Shlwapi.h>
#include "..\common\winfile.h"
#include "..\common\utils.h"
#include "..\common\win32helper.h"
#include "..\common\wtlhelper.h"
#include "resource.h"
#include "aboutdlg.h"
#include "SettingDlg.h"
#include "MainDlg.h"

CMainDlg::CMainDlg()
    :m_bNeedConvert(TRUE), m_RawStringLength(0), m_StringLength(0), m_UnicodeLength(0),
    m_ConfigPath(L""), m_FilePathName(L""), m_CodeStatus(L""), m_StringCodeType(L"Local Codepage"),
    m_bCueFile(FALSE), m_bTransferString(FALSE), m_context(NULL)
{
    m_RawString = NULL;
    m_String = NULL;
    m_UnicodeString = NULL;
    m_ConfigPath += GetProcessFolder();
    m_ConfigPath += L"config-unicue.xml";

    SetDefault(m_Config);
    // TODO Loading config file in Constructor is not a good choice.
    // Load config file...
    // Because TiXml does not support wchar_t file name,
    // use Win32 File Api to load xml file.
    CWinFile file(m_ConfigPath, CWinFile::modeRead | CWinFile::shareDenyWrite);
    if (!file.open())
        SaveConfigFile(m_ConfigPath, m_Config);
    else
    {
        UINT fileLength = file.length();
        char *fileBuffer = new char[fileLength+1];
        memset((void*)fileBuffer, 0, fileLength+1);
        file.seek(0, CWinFile::begin);
        file.read(fileBuffer, fileLength);
        file.close();

        TiXmlDocument *doc = new TiXmlDocument;
        doc->Parse(fileBuffer, NULL, TIXML_ENCODING_UTF8);
        if (doc->Error() || !LoadConfigFile(doc, m_Config))
        {
            ::DeleteFile(m_ConfigPath);
            SetDefault(m_Config);
            SaveConfigFile(m_ConfigPath, m_Config);
        }

        delete []fileBuffer;
        fileBuffer = NULL;
        delete doc;
    }
    // set local here
    switch (m_Config.Lang)
    {
    case EN:
        SetThreadLocalSettings(LANG_ENGLISH, SUBLANG_ENGLISH_US);
        break;
    case CHN:
        SetThreadLocalSettings(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
        break;
    case CHT:
        SetThreadLocalSettings(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL);
        break;
    case JPN:
        SetThreadLocalSettings(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN);
        break;
    default:
        SetThreadLocalSettings(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
    }
}

CMainDlg::~CMainDlg()
{
    if (m_RawString)
        delete []m_RawString;
    if (m_UnicodeString)
        delete []m_UnicodeString;
    if (m_context)
    {
        m_context->finalize();
        delete m_context;
    }
}

BOOL CMainDlg::PreTranslateMessage(MSG* pMsg)
{
    return CWindow::IsDialogMessage(pMsg);
}

BOOL CMainDlg::OnIdle()
{
    return FALSE;
}

LRESULT CMainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // center the dialog on the screen
    CenterWindow();
    // init menu
    CMenu menu;
    menu.LoadMenu(IDR_MENU1);
    CWindow::SetMenu(menu);
    // popup menu
    m_popupMenu.LoadMenu(IDR_MENU_POPUP);
    // set icons
    HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME_BIG, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
    SetIcon(hIcon, TRUE);
    HICON hIconSmall = AtlLoadIconImage(IDR_MAINFRAME_LITTLE, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
    SetIcon(hIconSmall, FALSE);

    // register object for message filtering and idle updates
    CMessageLoop* pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != NULL);
    pLoop->AddMessageFilter(this);
    pLoop->AddIdleHandler(this);

    UIAddChildWindowContainer(m_hWnd);

    // always on top
    SetDialogPos();

    // DDX
    DoDataExchange(FALSE);

    // init C4 Context and load charmaps
    m_context = new CC4Context(std::wstring(m_Config.MapConfName), GetProcessFolder());
    if (!m_context->init())
        MessageBox(getString(IDS_FAILEDTOLOAD), _T("Unicue"), MB_OK);

    // add encode items
    CComboBox &theCombo = (CComboBox)GetDlgItem(IDC_COMBO_SELECTCODE);
    std::list<std::wstring> &encodeList = m_context->getEncodesNameList();
    std::list<std::wstring>::iterator iter;
    theCombo.InsertString(-1, getString(IDS_LOCALPAGE));
    for (iter = encodeList.begin(); iter != encodeList.end(); iter++)
        theCombo.InsertString(-1, iter->c_str());
    theCombo.SetCurSel(0);

    // when called from command line
    LPWSTR *szArglist;
    int nArgs;
    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if ((NULL != szArglist) && (nArgs >= 2) && (wcslen(szArglist[1]) >= 1))
    {
        // 0 is execution path
        WTL::CString filePath(szArglist[1]);
        if (filePath.GetAt(0) == _T('\"'))
            filePath.Delete(0);
        if (filePath.GetAt(filePath.GetLength() - 1) == _T('\"'))
            filePath.Delete(filePath.GetLength() -1);
        m_FilePathName = filePath;
        WTL::CString &ExtensionName = filePath.Right(filePath.GetLength() - filePath.ReverseFind('.') - 1);
        ExtensionName.MakeLower();
        WTL::CString &FileName = filePath.Right(filePath.GetLength() - filePath.ReverseFind('\\') - 1);
        if ((ExtensionName == _T("tak")) || (ExtensionName == _T("flac")) || (ExtensionName == _T("ape")))
        {
            if (m_Config.AcceptDragAudioFile)
            {
                if (ExtensionName==_T("flac"))
                    ExtractFlacInternalCue(FileName);
                else if ((ExtensionName==_T("tak"))||(ExtensionName==_T("ape")))
                    ExtractTakInternalCue(FileName);
            }
            else
            {
                if (DealFile())
                {
                    if (m_Config.AutoFixTTA) FixTTACue();
                    if (m_Config.AutoFixCue) FixCue();
                }
            }
        }
        else
        {
            if (DealFile())
            {
                if (m_Config.AutoFixTTA) FixTTACue();
                if (m_Config.AutoFixCue) FixCue();
            }
        }
    }
    LocalFree(szArglist);

    return TRUE;
}

BOOL CMainDlg::SetDialogPos()
{
    RECT rc;
    GetWindowRect(&rc);

    if (m_Config.AlwaysOnTop)
        return SetWindowPos(HWND_TOPMOST, &rc, SWP_NOMOVE|SWP_NOSIZE);
    else
        return SetWindowPos(HWND_NOTOPMOST, &rc, SWP_NOMOVE|SWP_NOSIZE);
}


BOOL CMainDlg::DealFile()
{
    if (m_FilePathName.IsEmpty()) return FALSE;

    m_bCueFile=FALSE;
    WTL::CString extendName = m_FilePathName.Right(m_FilePathName.GetLength() - m_FilePathName.ReverseFind('.') - 1);
    extendName.MakeLower();
    if (extendName == _T("cue"))
        m_bCueFile = TRUE;

    CWinFile openFile(m_FilePathName, CWinFile::modeRead | CWinFile::shareDenyWrite);
    if (!openFile.open())
    {
        MessageBox(getString(IDS_OPENFAILED), L"Unicue", MB_OK);
        return FALSE;
    }
    m_bNeedConvert = TRUE;
    if (m_RawString)
    {
        delete []m_RawString;
        m_RawString = NULL;
        m_String = NULL;
    }
    if (m_UnicodeString)
    {
        delete []m_UnicodeString;
        m_UnicodeString = NULL;
    }
    m_RawStringLength = openFile.length();
    m_RawString = new char[m_RawStringLength + 1];
    openFile.seek(0, CWinFile::begin);
    if (openFile.read(m_RawString, m_RawStringLength) == (DWORD)-1)
    {
        MessageBox(getString(IDS_READFAILED), L"Unicue", MB_OK);
    }
    openFile.close();
    m_RawString[m_RawStringLength] = '\0';
    m_String = m_RawString;
    m_StringLength = m_RawStringLength;

    CComboBox &theCombo  = (CComboBox)GetDlgItem(IDC_COMBO_SELECTCODE);
    CStatic   &theStatic = (CStatic)GetDlgItem(IDC_STATIC_STAT);
    m_CodeStatus = getString(IDS_UNKNOWNCODE);
    CEdit &LeftEdit  = (CEdit)GetDlgItem(IDC_EDIT_ANSI);
    CEdit &RightEdit = (CEdit)GetDlgItem(IDC_EDIT_UNICODE);

    // Unicode(little-endian)
    if (((unsigned char)m_RawString[0] == 0xFF) && ((unsigned char)m_RawString[1] == 0xFE))
    {
        m_CodeStatus = _T("UTF-16 (little endian)");
        m_bNeedConvert = FALSE;
        m_StringCodeType = CC4EncodeUTF16::_getName().c_str();
        int nIndex = theCombo.FindStringExact(0, m_StringCodeType);
        theCombo.SetCurSel(nIndex);
        m_String = m_RawString + 2; // 真正的起始地址
        m_StringLength = m_RawStringLength - 2; // 真正的长度
        if ((m_RawStringLength%2) != 0)
        {
            MessageBox(getString(IDS_CORRUPTFILE));
            return FALSE;
        }
        m_UnicodeLength = m_StringLength>>1;
        m_UnicodeString = new wchar_t[m_UnicodeLength+1];
        memcpy((void*)m_UnicodeString, m_String, m_StringLength);
        m_UnicodeString[m_UnicodeLength] = '\0';
    }
    // Unicode(big-endian)
    if (((unsigned char)m_RawString[0] == 0xFE) && ((unsigned char)m_RawString[1] == 0xFF))
    {
        m_CodeStatus = _T("UTF-16 (big endian)");
        m_bNeedConvert = FALSE;
        m_StringCodeType = CC4EncodeUTF16::_getName().c_str();
        int nIndex = theCombo.FindStringExact(0, m_StringCodeType);
        theCombo.SetCurSel(nIndex);
        m_String = m_RawString + 2; // 真正的起始地址
        m_StringLength = m_RawStringLength - 2; // 真正的长度
        if ((m_RawStringLength&1) != 0)
        {
            MessageBox(getString(IDS_CORRUPTFILE));
            return FALSE;
        }
        m_UnicodeLength = m_StringLength>>1;
        m_UnicodeString = new wchar_t[m_UnicodeLength + 1];
        memcpy((void*)m_UnicodeString, m_String, m_StringLength);
        m_UnicodeString[m_UnicodeLength] = '\0';
        // 调整高低位顺序
        convertBEtoLE(m_UnicodeString, m_UnicodeLength);
    }
    // UTF-8(with BOM)
    if (((unsigned char)m_RawString[0] == 0xEF) &&
        ((unsigned char)m_RawString[1] == 0xBB) &&
        ((unsigned char)m_RawString[2] == 0xBF))
    {
        m_CodeStatus = _T("UTF-8 (with BOM)");
        m_bNeedConvert = FALSE;
        m_StringCodeType = CC4EncodeUTF8::_getName().c_str();
        int nIndex = theCombo.FindStringExact(0, m_StringCodeType);
        theCombo.SetCurSel(nIndex);
        m_String = m_RawString + 3; // 真正的起始地址
        m_StringLength = m_RawStringLength - 3; // 真正的长度
    }

    if (!m_bNeedConvert)
    {
        theStatic.SetWindowText(getString(IDS_FILEDETECTRESULT) + m_CodeStatus + _T("\n\n") + getString(IDS_FILEPATH) + m_FilePathName);
        if (m_StringCodeType == CC4EncodeUTF16::_getName().c_str())
        {
            RightEdit.SetWindowText(m_UnicodeString);
            LeftEdit.SetWindowText(_T(""));
        }
        if (m_StringCodeType == CC4EncodeUTF8::_getName().c_str())
        {
            RightEdit.SetWindowText(CC4EncodeUTF8::convert2unicode(m_String,m_StringLength).c_str());
            LeftEdit.SetWindowText(_T(""));
        }
    }
    else
    {
        // 检测编码
        if (m_Config.AutoCheckCode)
        {
            const CC4Encode *encode = m_context->getMostPossibleEncode(m_String);
            if (encode)
            {
                m_StringCodeType = encode->getName().c_str();
                int nIndex = theCombo.FindStringExact(0, m_StringCodeType);
                theCombo.SetCurSel(nIndex);
                m_CodeStatus = encode->getName().c_str();
            }
            else
            {
                getLBText(theCombo, 0, m_StringCodeType);
                theCombo.SetCurSel(0);
                m_CodeStatus = getString(IDS_UNKNOWNCODE);
            }
        }
        else
            m_CodeStatus = getString(IDS_DETECTDISABLED);

        theStatic.SetWindowText(getString(IDS_FILEDETECTRESULT) + m_CodeStatus + _T("\n\n") + getString(IDS_FILEPATH) + m_FilePathName);

        // 左
        /*
        _locale_t locale = _create_locale(LC_ALL, "Japan");
        size_t requiredSize = _mbstowcs_l(NULL, m_String, 0, locale);
        wchar_t *localString = new wchar_t[requiredSize + 1];
        size_t result = _mbstowcs_l(localString, m_String, requiredSize + 1, locale);
        */
        std::wstring &localString = msConvert(m_String);
        LeftEdit.SetWindowText(localString.c_str());

        // 右
        const CC4Encode *encode = m_context->getEncode(std::wstring(m_StringCodeType));
        if (encode)
            RightEdit.SetWindowText(encode->wconvertText(m_String, m_StringLength).c_str());
        else
            RightEdit.SetWindowText(localString.c_str());
    }

    return TRUE;
}

LRESULT CMainDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    SaveConfigFile(m_ConfigPath, m_Config);
    // unregister message filtering and idle updates
    CMessageLoop* pLoop = _Module.GetMessageLoop();
    ATLASSERT(pLoop != NULL);
    pLoop->RemoveMessageFilter(this);
    pLoop->RemoveIdleHandler(this);

    return 0;
}

LRESULT CMainDlg::OnAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    CAboutDlg dlg;
    dlg.DoModal();
    return 0;
}

LRESULT CMainDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    // TODO: Add validation code 
    CloseDialog(wID);
    return 0;
}

LRESULT CMainDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    CloseDialog(wID);
    return 0;
}

void CMainDlg::CloseDialog(int nVal)
{
    DestroyWindow();
    ::PostQuitMessage(nVal);
}

LRESULT CMainDlg::OnFileExit(WORD, WORD wID, HWND, BOOL&)
{
    CloseDialog(wID);
    return 0;
}

LRESULT CMainDlg::OnFileOpen(WORD, WORD, HWND, BOOL&)
{
    CFileDialog openFile(TRUE, _T("*.txt"), NULL, OFN_EXTENSIONDIFFERENT|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST,
        _T("text file(*.txt;*.cue;*.log)\0*.txt;*.cue;*.log\0txt file(*.txt)\0*.txt\0cue file(*.cue)\0*.cue\0log file(*.log)\0*.log\0All Files (*.*)\0*.*\0\0"));
    if (openFile.DoModal() == IDOK)
    {
        m_FilePathName = openFile.m_szFileName;
        WTL::CString &ExtensionName = m_FilePathName.Right(m_FilePathName.GetLength() - m_FilePathName.ReverseFind('.') - 1);
        ExtensionName.MakeLower();
        WTL::CString &FileName = m_FilePathName.Right(m_FilePathName.GetLength() - m_FilePathName.ReverseFind('\\') - 1);
        if ((ExtensionName == _T("tak"))  ||
            (ExtensionName == _T("flac")) ||
            (ExtensionName == _T("ape")))
        {
            if (m_Config.AcceptDragAudioFile)
            {
                if (ExtensionName == _T("flac"))
                    ExtractFlacInternalCue(FileName);
                else
                    ExtractTakInternalCue(FileName);
            }
            else
            {
                if (DealFile())
                {
                    if (m_Config.AutoFixTTA) FixTTACue();
                    if (m_Config.AutoFixCue) FixCue();
                }
            }
        }
        else
        {
            if (DealFile())
            {
                if (m_Config.AutoFixTTA) FixTTACue();
                if (m_Config.AutoFixCue) FixCue();
            }
        }
    }
    return 0;
}

LRESULT CMainDlg::OnPopupUTF8(WORD, WORD, HWND, BOOL&)
{
    m_Config.OutputEncoding = O_UTF_8;
    return 0;
}

LRESULT CMainDlg::OnPopupUTF8NoBom(WORD, WORD, HWND, BOOL&)
{
    m_Config.OutputEncoding = O_UTF_8_NOBOM;
    return 0;
}

LRESULT CMainDlg::OnPopupUTF16LE(WORD, WORD, HWND, BOOL&)
{
    m_Config.OutputEncoding = O_UTF_16_LE;
    return 0;
}

LRESULT CMainDlg::OnPopupUTF16BE(WORD, WORD, HWND, BOOL&)
{
    m_Config.OutputEncoding = O_UTF_16_BE;
    return 0;
}

LRESULT CMainDlg::OnFileSave(WORD, WORD, HWND, BOOL&)
{
    CFileDialog saveFile(FALSE, _T("*.txt"), NULL, OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST,
        _T("text file(*.txt;*.cue;*.log)\0*.txt;*.cue;*.log\0txt file(*.txt)\0*.txt\0cue file(*.cue)\0*.cue\0log file(*.log)\0*.log\0All Files (*.*)\0*.*\0\0"));
    if (saveFile.DoModal() == IDOK)
    {
        CWinFile file(saveFile.m_szFileName, CWinFile::modeCreate|CWinFile::modeWrite|CWinFile::shareExclusive);
        if (!file.open())
        {
            MessageBox(getString(IDS_WRITEFAILED), _T("Unicue"), MB_OK);
            return 0;
        }
        WTL::CString UnicodeStr;
        getWindowText(GetDlgItem(IDC_EDIT_UNICODE), UnicodeStr);
        std::string &utf8str = CC4EncodeUTF16::convert2utf8((LPCTSTR)UnicodeStr, UnicodeStr.GetLength());
        file.write(CC4Encode::UTF_8_BOM, 3);
        file.write(utf8str.c_str(), utf8str.length());
        file.close();
    }

    return 0;
}

LRESULT CMainDlg::OnFileOption(WORD, WORD, HWND, BOOL&)
{
    CSettingDlg dlg(m_Config);
    if (dlg.DoModal() == IDOK)
    {
        m_Config = dlg.m_Config;
    }
    return 0;
}

LRESULT CMainDlg::OnDropFiles(UINT, WPARAM wParam, LPARAM, BOOL&)
{
    HDROP hDrop = (HDROP)wParam;
    int nFileCount = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, MAX_PATH);
    if (!m_bTransferString)
    {
        if (nFileCount == 1)
        {
            TCHAR szFileName[MAX_PATH + 1] = {0};
            DragQueryFile(hDrop, 0, szFileName, MAX_PATH);
            m_FilePathName = szFileName;
            WTL::CString &ExtensionName = m_FilePathName.Right(m_FilePathName.GetLength() - m_FilePathName.ReverseFind('.') - 1);
            ExtensionName.MakeLower();
            WTL::CString &FileName = m_FilePathName.Right(m_FilePathName.GetLength() - m_FilePathName.ReverseFind('\\') - 1);
            if ((ExtensionName == L"tak")  ||
                (ExtensionName == L"flac") ||
                (ExtensionName == L"ape"))
            {
                if (m_Config.AcceptDragAudioFile)
                {
                    if (ExtensionName == L"flac")
                        ExtractFlacInternalCue(FileName);
                    else
                        ExtractTakInternalCue(FileName);
                }
                else
                {
                    if (DealFile())
                    {
                        if (m_Config.AutoFixTTA) FixTTACue();
                        if (m_Config.AutoFixCue) FixCue();
                    }
                }
            }
            else
            {
                if (DealFile())
                {
                    if (m_Config.AutoFixTTA) FixTTACue();
                    if (m_Config.AutoFixCue) FixCue();
                }
            }
        }
        else
            MessageBox(getString(IDS_ONLYONEFILEALLOW), _T("Unicue"), MB_OK);
    }
    else
    {
        // 抓取文件名
        WTL::CString LeftStr;
        for (int i = 0; i < nFileCount; i++)
        {
            TCHAR szFileName[MAX_PATH+1];
            ::DragQueryFile(hDrop, i, szFileName, MAX_PATH);
            TCHAR *pdest = wcsrchr(szFileName, L'\\');
            if (pdest)
            {
                pdest++;
                LeftStr += pdest;
                LeftStr += _T("\x0D\x0A");
            }
        }

        GetDlgItem(IDC_EDIT_ANSI).SetWindowText(LeftStr);
    }

    ::DragFinish(hDrop);
    return 0;
}

LRESULT CMainDlg::OnCbnSelchangeComboSelectcode(WORD, WORD, HWND, BOOL&)
{
    CComboBox &theCombo = (CComboBox)GetDlgItem(IDC_COMBO_SELECTCODE);

    if (m_bTransferString)
    {
        getWindowText(theCombo, m_StringCodeType);

        // 左
        WTL::CString LeftStr;
        getWindowText(GetDlgItem(IDC_EDIT_ANSI), LeftStr);
        std::string &LeftAnsiStr = msConvertBack(LeftStr);
        // 右
        const CC4Encode *encode = m_context->getEncode(std::wstring(m_StringCodeType));
        if (encode)
            GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(encode->wconvertText(LeftAnsiStr.c_str(),LeftAnsiStr.length()).c_str());
        else
            GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(LeftStr);
        return 0;
    }

    if (m_bNeedConvert)
    {
        getWindowText(theCombo, m_StringCodeType);
        const CC4Encode *encode = m_context->getEncode(std::wstring(m_StringCodeType));
        if (encode)
            GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(encode->wconvertText(m_String, m_StringLength).c_str());
        else
            GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(msConvert(m_String).c_str());
        if (m_Config.AutoFixTTA) FixTTACue();
        if (m_Config.AutoFixCue) FixCue();
    }

    return 0;
}

LRESULT CMainDlg::OnBnClickedButtonDo(WORD, WORD, HWND, BOOL&)
{
    // 只有转换字符串时才有效
    if (m_bTransferString)
    {
        // 左
        WTL::CString LeftStr;
        getWindowText(GetDlgItem(IDC_EDIT_ANSI), LeftStr);
        std::string &LeftAnsiStr = msConvertBack(LeftStr);

        CComboBox &theCombo  =(CComboBox)GetDlgItem(IDC_COMBO_SELECTCODE);
        CStatic   &theStatic =(CStatic)GetDlgItem(IDC_STATIC_STAT);
        getWindowText(theCombo, m_StringCodeType);
        m_CodeStatus = getString(IDS_UNKNOWNCODE);

        // 检测编码
        if (m_Config.AutoCheckCode)
        {
            const CC4Encode *encode = m_context->getMostPossibleEncode(LeftAnsiStr);
            if (encode)
            {
                m_StringCodeType = encode->getName().c_str();
                int nIndex = theCombo.FindStringExact(0, m_StringCodeType);
                theCombo.SetCurSel(nIndex);
                m_CodeStatus = encode->getName().c_str();
            } else {
                getLBText(theCombo, 0, m_StringCodeType);
                theCombo.SetCurSel(0);
                m_CodeStatus = getString(IDS_UNKNOWNCODE);
            }
        }
        else
            m_CodeStatus = getString(IDS_DETECTDISABLED);

        theStatic.SetWindowText(getString(IDS_STRDETECTRESULT) + m_CodeStatus);

        //右
        const CC4Encode *encode = m_context->getEncode(std::wstring(m_StringCodeType));
        if (encode)
            GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(encode->wconvertText(LeftAnsiStr.c_str(), LeftAnsiStr.length()).c_str());
        else
            GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(LeftStr);
    }
    return 0;
}

LRESULT CMainDlg::OnBnClickedButtonSave(WORD, WORD, HWND, BOOL&)
{
    CWinFile file(m_FilePathName, CWinFile::modeCreate|CWinFile::modeWrite|CWinFile::shareExclusive);
    if (!file.open())
    {
        MessageBox(getString(IDS_WRITEFAILED), _T("Unicue"), MB_OK);
        return 0;
    }
    WTL::CString UnicodeStr;
    getWindowText(GetDlgItem(IDC_EDIT_UNICODE), UnicodeStr);

    switch (m_Config.OutputEncoding)
    {
    case O_UTF_8_NOBOM:
        {
            std::string &utf8str = CC4EncodeUTF16::convert2utf8((LPCTSTR)UnicodeStr, UnicodeStr.GetLength());
            file.write(utf8str.c_str(), utf8str.length());
        }
        break;
    case O_UTF_16_LE:
        file.write(CC4Encode::LITTLEENDIAN_BOM, 2);
        file.write((const char*)(LPCTSTR)UnicodeStr, UnicodeStr.GetLength()*sizeof(wchar_t));
        break;
    case O_UTF_16_BE:
        file.write(CC4Encode::BIGENDIAN_BOM, 2);
        for (int i = 0; i < UnicodeStr.GetLength(); ++i)
        {
            wchar_t chr = UnicodeStr.GetAt(i);
            file.write(((char*)(&chr)) + 1, 1);
            file.write((char*)(&chr), 1);
        }
        break;
    case O_UTF_8:
    default:
        {
            std::string &utf8str = CC4EncodeUTF16::convert2utf8((LPCTSTR)UnicodeStr, UnicodeStr.GetLength());
            file.write(CC4Encode::UTF_8_BOM, 3);
            file.write(utf8str.c_str(), utf8str.length());
        }
    }

    file.close();

    return 0;
}

LRESULT CMainDlg::OnBnClickedButtonSaveas(WORD, WORD, HWND, BOOL&)
{
    int position = m_FilePathName.ReverseFind('.');
    WTL::CString &FileType = m_FilePathName.Right(m_FilePathName.GetLength() - position);
    WTL::CString &FilePath = m_FilePathName.Left(position);
    FilePath += m_Config.TemplateStr;
    FilePath += FileType;

    CWinFile file(FilePath, CWinFile::modeCreate|CWinFile::modeWrite|CWinFile::shareExclusive);
    if (!file.open())
    {
        MessageBox(getString(IDS_WRITEFAILED), _T("Unicue"), MB_OK);
        return 0;
    }
    WTL::CString UnicodeStr;
    getWindowText(GetDlgItem(IDC_EDIT_UNICODE), UnicodeStr);
    switch (m_Config.OutputEncoding)
    {
    case O_UTF_8_NOBOM:
        {
            std::string &utf8str = CC4EncodeUTF16::convert2utf8((LPCTSTR)UnicodeStr, UnicodeStr.GetLength());
            file.write(utf8str.c_str(), utf8str.length());
        }
        break;
    case O_UTF_16_LE:
        file.write(CC4Encode::LITTLEENDIAN_BOM, 2);
        file.write((const char*)(LPCTSTR)UnicodeStr, UnicodeStr.GetLength()*sizeof(wchar_t));
        break;
    case O_UTF_16_BE:
        file.write(CC4Encode::BIGENDIAN_BOM, 2);
        for (int i = 0; i < UnicodeStr.GetLength(); ++i)
        {
            wchar_t chr = UnicodeStr.GetAt(i);
            file.write(((char*)(&chr)) + 1, 1);
            file.write((char*)(&chr), 1);
        }
        break;
    case O_UTF_8:
    default:
        {
            std::string &utf8str = CC4EncodeUTF16::convert2utf8((LPCTSTR)UnicodeStr, UnicodeStr.GetLength());
            file.write(CC4Encode::UTF_8_BOM, 3);
            file.write(utf8str.c_str(), utf8str.length());
        }
    }
    file.close();

    return 0;
}

LRESULT CMainDlg::OnBnClickedCheckAutocheckcode(WORD, WORD, HWND, BOOL&)
{
    m_Config.AutoCheckCode = !m_Config.AutoCheckCode;
    return 0;
}

LRESULT CMainDlg::OnBnClickedCheckAlwaysontop(WORD, WORD, HWND, BOOL&)
{
    m_Config.AlwaysOnTop = !m_Config.AlwaysOnTop;
    SetDialogPos();
    return 0;
}

LRESULT CMainDlg::OnBnClickedButtonTransferstring(WORD, WORD, HWND, BOOL&)
{
    m_bTransferString = !m_bTransferString;
    if (m_bTransferString)
    {
        GetDlgItem(IDC_BUTTON_TRANSFERSTRING).SetWindowText(getString(IDS_FILEMODE));
        GetDlgItem(IDC_BUTTON_SAVE).EnableWindow(FALSE);
        GetDlgItem(IDC_BUTTON_SAVEAS).EnableWindow(FALSE);
        GetDlgItem(IDC_BUTTON_SELECTSAVECODE).EnableWindow(FALSE);
        GetDlgItem(IDC_BUTTON_DO).EnableWindow(TRUE);
        GetDlgItem(IDC_STATIC_STAT).SetWindowText(getString(IDS_STRDETECTRESULT));
        UIEnable(IDM_FILE_OPEN, FALSE);
    }
    else
    {
        GetDlgItem(IDC_BUTTON_TRANSFERSTRING).SetWindowText(getString(IDS_STRINGMODE));
        GetDlgItem(IDC_BUTTON_SAVE).EnableWindow(TRUE);
        GetDlgItem(IDC_BUTTON_SAVEAS).EnableWindow(TRUE);
        GetDlgItem(IDC_BUTTON_SELECTSAVECODE).EnableWindow(TRUE);
        GetDlgItem(IDC_BUTTON_DO).EnableWindow(FALSE);
        GetDlgItem(IDC_STATIC_STAT).SetWindowText(getString(IDS_FILEDETECTRESULT) + _T("\n\n") + getString(IDS_FILEPATH));
        // 恢复
        UIEnable(IDM_FILE_OPEN, TRUE);
        GetDlgItem(IDC_EDIT_ANSI).SetWindowText(_T(""));
        GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(_T(""));
        m_FilePathName = _T("");
        m_bNeedConvert = FALSE;
        /*
        if (DealFile())
        {
            if (m_Config.AutoFixTTA) FixTTACue();
            if (m_Config.AutoFixCue) FixCue();
        }
        */
    }

    return 0;
}

LRESULT CMainDlg::OnBnClickedButtonSelectOutputCode(WORD, WORD, HWND, BOOL&)
{
    CMenuHandle hMenu;
    hMenu = m_popupMenu.GetSubMenu(0);
    CheckMenuItem(hMenu, IDM_UTF_8_WITH_BOM, MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_UTF_8_WITHOUT_BOM, MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_UTF_16_LITTLE_ENDIAN, MF_UNCHECKED);
    CheckMenuItem(hMenu, IDM_UTF_16_BIG_ENDIAN, MF_UNCHECKED);
    switch (m_Config.OutputEncoding)
    {
    case O_UTF_8:
        CheckMenuItem(hMenu, IDM_UTF_8_WITH_BOM, MF_CHECKED);
        break;
    case O_UTF_8_NOBOM:
        CheckMenuItem(hMenu, IDM_UTF_8_WITHOUT_BOM, MF_CHECKED);
        break;
    case O_UTF_16_LE:
        CheckMenuItem(hMenu, IDM_UTF_16_LITTLE_ENDIAN, MF_CHECKED);
        break;
    case O_UTF_16_BE:
        CheckMenuItem(hMenu, IDM_UTF_16_BIG_ENDIAN, MF_CHECKED);
        break;
    default:
        CheckMenuItem(hMenu, IDM_UTF_8_WITH_BOM, MF_CHECKED);
    }
    CPoint point;
    GetCursorPos(&point);
    hMenu.TrackPopupMenu(TPM_LEFTALIGN|TPM_LEFTBUTTON, point.x, point.y, this->m_hWnd);

    return 0;
}

BOOL CMainDlg::ExtractTakInternalCue(WTL::CString AudioFileName)
{
    m_CodeStatus = _T("UTF-8 (Internal Cue File)");
    m_bNeedConvert = FALSE;
    m_StringCodeType = CC4EncodeUTF8::_getName().c_str();
    int nIndex = ((CComboBox)GetDlgItem(IDC_COMBO_SELECTCODE)).FindStringExact(0, m_StringCodeType);
    ((CComboBox)GetDlgItem(IDC_COMBO_SELECTCODE)).SetCurSel(nIndex);

    WTL::CString statusText = getString(IDS_FILEDETECTRESULT) + m_CodeStatus + _T("\n\n") + getString(IDS_FILEPATH) + m_FilePathName;
    GetDlgItem(IDC_STATIC_STAT).SetWindowText(statusText);
    GetDlgItem(IDC_EDIT_ANSI).SetWindowText(_T(""));
    GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(_T(""));

    if (m_RawString)
    {
        delete []m_RawString;
        m_RawString = NULL;
        m_String = NULL;
    }
    if (m_UnicodeString)
    {
        delete []m_UnicodeString;
        m_UnicodeString = NULL;
    }

    if (m_FilePathName.IsEmpty())
        return FALSE;

    CWinFile OpenFile(m_FilePathName, CWinFile::modeRead | CWinFile::shareDenyWrite);
    if (!OpenFile.open())
    {
        MessageBox(getString(IDS_OPENFAILED), _T("Unicue"), MB_OK);
        return FALSE;
    }

    m_FilePathName += _T(".cue");
    statusText += _T(".cue");
    GetDlgItem(IDC_STATIC_STAT).SetWindowText(statusText);

    if (OpenFile.length() < 20480) // 小于20K，文档太小了
    {
        OpenFile.close();
        return FALSE;
    }
    unsigned char Buffer[20480] = {0}; //20k的缓冲区
    OpenFile.seek(-20480, CWinFile::end);
    OpenFile.read((char*)Buffer, 20480);
    OpenFile.close();

    //查找 Cuesheet 标记,自动机模型,大小写不敏感
    int state = 0, BeginPos = 0, EndPos = 0, Length = 0;
    for (int i = 0; i < 20480; ++i)
    {
        if ((Buffer[i] >= 0x41) && (Buffer[i] <= 0x5A))
            Buffer[i] = Buffer[i] + 0x20;

        switch (Buffer[i])
        {
        case 'c':
            state = 1;      // C
            break;
        case 'u':
            if (state == 1)
                state = 2;  // Cu
            else
                state = 0;
            break;
        case 'e':
            switch (state)
            {
            case 2:
                state = 3;  // Cue
                break;
            case 5:
                state = 6;  // Cueshe
                break;
            case 6:
                state = 7;  // Cueshee
                break;
            default:
                state = 0;
            }
            break;
        case 's':
            if (state == 3)
                state = 4;  // Cues
            else
                state = 0;
            break;
        case 'h':
            if (state == 4)
                state = 5;  // Cuesh
            else
                state = 0;
            break;
        case 't':
            if (state == 7)
            {
                state = 8;  // Cuesheet
            }
            else
                state = 0;
            break;
        default:
            state = 0;
        }
        if (state == 8)
        {
            BeginPos = i + 2;
            break;
        }
    }

    if (BeginPos == 0)
        return FALSE;

    // 查找终止符 0D 0A ? 00 00 00 00 00 00 （连续六个终止符以上）
    state = 0;
    for (int i = BeginPos; i < 20480; ++i)
    {
        switch (Buffer[i])
        {
        case '\0':
            state++;
            break;
        default:
            state = 0;
        }
        if (state == 6)
        {
            EndPos = i - 6; //指向0D 0A后的第一个字符
            break;
        }
    }

    if (EndPos <= 1)
        return FALSE;

    if ((Buffer[EndPos - 2] == '\x0D') && (Buffer[EndPos - 1] == '\x0A'))
        EndPos--;

    Length = EndPos - BeginPos + 1;
    if (Length <= 10) //too short
        return FALSE;

    m_RawStringLength = Length;
    m_RawString = new char[m_RawStringLength + 1];
    memcpy(m_RawString, Buffer + BeginPos, m_RawStringLength);
    m_RawString[m_RawStringLength]='\0';
    m_String = m_RawString;
    m_StringLength = m_RawStringLength;

    GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(CC4EncodeUTF8::convert2unicode(m_String,m_StringLength).c_str());

    FixInternalCue(AudioFileName);

    return TRUE;
}

// flac文件结构
// http://flac.sourceforge.net/format.html
BOOL CMainDlg::ExtractFlacInternalCue(WTL::CString AudioFileName)
{
    m_CodeStatus=_T("UTF-8 (Internal Cue File)");
    m_bNeedConvert=FALSE;
    m_StringCodeType=CC4EncodeUTF8::_getName().c_str();
    int nIndex = ((CComboBox)GetDlgItem(IDC_COMBO_SELECTCODE)).FindStringExact(0, m_StringCodeType);
    ((CComboBox)GetDlgItem(IDC_COMBO_SELECTCODE)).SetCurSel(nIndex);

    WTL::CString statusText = getString(IDS_FILEDETECTRESULT) + m_CodeStatus + _T("\n\n") + getString(IDS_FILEPATH) + m_FilePathName;
    GetDlgItem(IDC_STATIC_STAT).SetWindowText(statusText);
    GetDlgItem(IDC_EDIT_ANSI).SetWindowText(_T(""));
    GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(_T(""));

    if (m_RawString)
    {
        delete []m_RawString;
        m_RawString=NULL;
        m_String=NULL;
    }
    if (m_UnicodeString)
    {
        delete []m_UnicodeString;
        m_UnicodeString=NULL;
    }

    if (m_FilePathName.IsEmpty())
        return FALSE;

    CWinFile OpenFile(m_FilePathName, CWinFile::modeRead | CWinFile::shareDenyWrite);
    if (!OpenFile.open())
    {
        MessageBox(getString(IDS_OPENFAILED), _T("Unicue"), MB_OK);
        return FALSE;
    }

    m_FilePathName += _T(".cue");
    statusText += _T(".cue");
    GetDlgItem(IDC_STATIC_STAT).SetWindowText(statusText);

    if (OpenFile.length() < 1048576) // 小于1M，文档太小了
    {
        OpenFile.close();
        return FALSE;
    }

    unsigned char Header[5];
    memset(Header,0,5);
    UINT64 position=0;
    // 4个字节的头部
    OpenFile.seek(0, CWinFile::begin);
    OpenFile.read((char*)Header,4);
    if (strcmp((char*)Header,"fLaC")!=0)
    {
        //MessageBox(_T("Not real flac file!"));
        return FALSE;
    }

    unsigned char chr;
    unsigned char *Buffer=NULL;
    UINT Length;
    //4个字节的METADATA_BLOCK_HEADER
    do 
    {
        OpenFile.read((char*)Header,4);
        //解析
        memcpy(&chr,Header,1);
        //检查最高位是否为1
        if ((chr&0x80)==0x80)
        {
            //最后一个METADATA_BLOCK
            if ((chr&0x7F)==0x04)//是VORBIS_COMMENT
            {
                //读取BLOCK长度
                Length=Header[1]*0x10000+Header[2]*0x100+Header[3];
                //申请空间
                Buffer=new unsigned char[Length+1];
                //读取BLOCK DATA
                OpenFile.read((char*)Buffer,Length);
                Buffer[Length]='\0';
            }
            break;
        }
        else
        {
            //不是最后一个METADATA_BLOCK
            if ((chr&0x7F)==0x04)//是VORBIS_COMMENT
            {
                //读取BLOCK长度
                Length=Header[1]*0x10000+Header[2]*0x100+Header[3];
                //申请空间
                Buffer=new unsigned char[Length+1];
                //读取BLOCK DATA
                OpenFile.read((char*)Buffer,Length);
                Buffer[Length]='\0';
                break;
            }
            else //不是VORBIS_COMMENT
            {
                //读取BLOCK长度
                Length=Header[1]*0x10000+Header[2]*0x100+Header[3];
                //移动文件指针
                OpenFile.seek(Length, CWinFile::current);
                position = OpenFile.getPosition();
            }
        }
    } while(position<=1048576);

    OpenFile.close();
    if (!Buffer)
        return FALSE;

    //查找 Cuesheet 标记,自动机模型,大小写不敏感
    int state=0,BeginPos=0,EndPos=0;
    for (UINT i=0;i<Length;++i)
    {
        if ((Buffer[i]>=0x41)&&(Buffer[i]<=0x5A))
            Buffer[i]=Buffer[i]+0x20;

        switch (Buffer[i])
        {
        case 'c':
            state=1;      //C
            break;
        case 'u':
            if (state==1)
                state=2;  //Cu
            else
                state=0;
            break;
        case 'e':
            switch (state)
            {
            case 2:
                state=3;  //Cue
                break;
            case 5:
                state=6;  //Cueshe
                break;
            case 6:
                state=7;  //Cueshee
                break;
            default:
                state=0;
            }
            break;
        case 's':
            if (state==3)
                state=4;  //Cues
            else
                state=0;
            break;
        case 'h':
            if (state==4)
                state=5;  //Cuesh
            else
                state=0;
            break;
        case 't':
            if (state==7)
            {
                state=8;  //Cuesheet
            }
            else
                state=0;
            break;
        default:
            state=0;
        }
        if (state==8)
        {
            BeginPos=i+2;
            break;
        }
    }
    if (BeginPos==0)
    {
        delete []Buffer;
        return FALSE;
    }
    //查找终止符 0D 0A ? 00 00 00（连续3个终止符以上）
    state=0;
    for (int i=BeginPos;i<20480;++i)
    {
        switch (Buffer[i])
        {
        case '\0':
            state++;
            break;
        default:
            state=0;
        }
        if (state==3)
        {
            EndPos=i-3; //指向0D 0A后的第一个字符
            break;
        }
    }

    if (EndPos<=1)
    {
        delete []Buffer;
        return FALSE;
    }

    if ((Buffer[EndPos-2]=='\x0D')&&(Buffer[EndPos-1]=='\x0A'))
        EndPos--;

    int CueLength=EndPos-BeginPos+1;
    if (CueLength<=10) //too short
    {
        delete []Buffer;
        return FALSE;
    }

    m_RawStringLength=CueLength;
    m_RawString=new char[m_RawStringLength+1];
    memcpy(m_RawString,Buffer+BeginPos,m_RawStringLength);
    m_RawString[m_RawStringLength]='\0';
    m_String=m_RawString;
    m_StringLength=m_RawStringLength;
    delete []Buffer;

    GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(CC4EncodeUTF8::convert2unicode(m_String,m_StringLength).c_str());
    FixInternalCue(AudioFileName);

    return TRUE;
}

void CMainDlg::FixCue()
{
    if (!m_bCueFile) return;

    FixTTACue();

    WTL::CString cueContent;
    getWindowText(GetDlgItem(IDC_EDIT_UNICODE), cueContent);

    int BeginPos = cueContent.Find(_T("FILE \""));
    if (BeginPos == -1)
    {
        if (!m_Config.CloseCuePrompt) MessageBox(getString(IDS_CORRUPTCUE));
        return;
    }
    int EndPos = cueContent.Find(_T("\" WAVE"));
    if (EndPos == -1)
    {
        if (!m_Config.CloseCuePrompt) MessageBox(getString(IDS_CORRUPTCUE));
        return;
    }
    BeginPos += 6;
    if (BeginPos >= EndPos)
    {
        if (!m_Config.CloseCuePrompt) MessageBox(getString(IDS_CORRUPTCUE));
        return;
    }

    // 依据文档路径：m_FilePathName查找音频文件
    WTL::CString &audioFileName = cueContent.Mid(BeginPos, EndPos - BeginPos); // 音频文件名
    WTL::CString &audioFilePath = m_FilePathName.Left(m_FilePathName.ReverseFind(L'\\'));
    audioFilePath += _T('\\');
    audioFilePath += audioFileName;

    if (PathFileExists(audioFileName)) return; // no need to fix

    // 替换扩展名查找
    int pos = audioFileName.ReverseFind(L'.');
    int extensionLength = 0;
    WTL::CString audioFileNameFound(_T(""));
    if (-1 != pos)
    {
        extensionLength += audioFileName.GetLength() - pos; // contain .
        audioFileNameFound += audioFileName.Left(pos);
    }
    else
        audioFileNameFound += audioFileName;

    const static wchar_t* FORMAT[12] =
    {
        L".ape",
        L".flac",
        L".tta",
        L".tak",
        L".wv",
        L".m4a",
        L".wma",
        L".wav",
        L".mac",
        L".fla",
        L".wave",
        L".mp3"
    };

    for (int i = 0; i < 12; ++i)
    {
        RemoveFromEnd(audioFilePath, extensionLength);
        const wchar_t *format = FORMAT[i];
        audioFilePath += format;
        if (PathFileExists(audioFilePath))
        {
            audioFileNameFound += format;
            cueContent.Replace(audioFileName, audioFileNameFound);
            GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(cueContent);
            return;
        }
        extensionLength = wcslen(format);
    }
    
    // also guess from cue file name
    WTL::CString audioFilePathImplicit(m_FilePathName);
    WTL::CString &audioFileNameImplicit = m_FilePathName.Right(m_FilePathName.GetLength() - m_FilePathName.ReverseFind(L'\\') - 1);
    //For first time, length is 4 (.cue)
    extensionLength = 4;
    RemoveFromEnd(audioFileNameImplicit, extensionLength);

    for (int i = 0; i < 12; ++i)
    {
        RemoveFromEnd(audioFilePathImplicit, extensionLength);
        const wchar_t *format = FORMAT[i];
        audioFilePathImplicit += format;
        if (PathFileExists(audioFilePathImplicit))
        {
            audioFileNameImplicit += format;
            cueContent.Replace(audioFileName, audioFileNameImplicit);
            GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(cueContent);
            return;
        }
        extensionLength = wcslen(format);
    }

    // 最后还是没找到
    return;
}

void CMainDlg::FixInternalCue(WTL::CString AudioFileName)
{
    WTL::CString CueString;
    getWindowText(GetDlgItem(IDC_EDIT_UNICODE), CueString);
    int BeginPos = CueString.Find(_T("FILE \""));
    if (BeginPos == -1)
    {
        if (!m_Config.CloseCuePrompt) MessageBox(getString(IDS_CORRUPTCUE));
        return;
    }
    int EndPos = CueString.Find(_T("\" WAVE"));
    if (EndPos == -1)
    {
        if (!m_Config.CloseCuePrompt) MessageBox(getString(IDS_CORRUPTCUE));
        return;
    }
    BeginPos += 6;
    if (BeginPos >= EndPos)
    {
        if (!m_Config.CloseCuePrompt) MessageBox(getString(IDS_CORRUPTCUE));
        return;
    }

    WTL::CString &OldFileName = CueString.Mid(BeginPos,EndPos-BeginPos); // 音频文件名
    CueString.Replace(OldFileName, AudioFileName);
    GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(CueString);
}

void CMainDlg::FixTTACue()
{
    if (!m_bCueFile)
        return;

    WTL::CString cueString;
    getWindowText(GetDlgItem(IDC_EDIT_UNICODE), cueString);
    cueString.MakeLower();

    int pos = cueString.Find(_T("the true audio"));
    if (pos <= 0) return;
    getWindowText(GetDlgItem(IDC_EDIT_UNICODE), cueString);
    WTL::CString &NewCueString = cueString.Left(pos) + _T("WAVE") + cueString.Right(cueString.GetLength() - pos - 14);
    GetDlgItem(IDC_EDIT_UNICODE).SetWindowText(NewCueString);
}
