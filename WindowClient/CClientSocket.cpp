// CClientSocket.cpp: 구현 파일
#include "pch.h"
#include "WindowClient.h"
#include "CClientSocket.h"
#include "WindowClientDlg.h"

CClientSocket::CClientSocket() {}
CClientSocket::~CClientSocket() {}

// ==============================================================================
// 🌟 네트워크 예외 처리 함수
// ==============================================================================
/**
 * @brief 서버와의 연결이 끊어졌을 때 호출되는 콜백 함수
 * @details 리눅스 서버가 종료되거나 네트워크가 단절되었을 때,
 * UI를 즉시 '오프라인' 상태로 전환하여 프로그램이 뻗는(Crash) 현상을 막습니다.
 */
void CClientSocket::OnClose(int nErrorCode) {
    CWindowClientDlg* pDlg = (CWindowClientDlg*)AfxGetMainWnd();
    if (pDlg) {
        pDlg->m_listLog.AddString(_T("❌ 서버와의 연결이 끊어졌습니다."));
        pDlg->m_listLog.SetCurSel(pDlg->m_listLog.GetCount() - 1);
        pDlg->m_stStatus.SetWindowText(_T("상태: 오프라인 (서버 끊김)"));
        pDlg->GetDlgItem(IDC_BTN_CONNECT)->SetWindowText(_T("서버 접속"));
    }
    CSocket::OnClose(nErrorCode);
}

// ==============================================================================
// 🌟 데이터 파싱 및 윈도우 제어 함수
// ==============================================================================
bool CClientSocket::ParseSensorData(CString strData, SensorData& data) {
    return (_stscanf_s((LPCTSTR)strData, _T("P:%f R:%f L:%d"), &data.pitch, &data.roll, &data.lux) == 3);
}

/**
 * @brief 🌟 핵심 기술: OS 레벨 활성 창(PID) 타겟팅 제어
 * @details 카카오톡 등 배경 프로그램에서 오작동하는 것을 방지하기 위해,
 * 현재 포커싱된 최상단 창(Foreground Window)의 제목을 분석하여 타겟 앱 여부를 판별합니다.
 */
CString CClientSocket::GetActiveWindowInfo(bool& bIsTargetActive) {
    HWND hWnd = ::GetForegroundWindow();
    bIsTargetActive = false;

    // 자기 자신이 최상단이거나 창이 없으면 제어 무시
    if (hWnd == NULL || hWnd == AfxGetMainWnd()->GetSafeHwnd()) {
        return _T("Other");
    }

    TCHAR szTitle[256] = { 0 };
    ::GetWindowText(hWnd, szTitle, 256);
    CString strTitle(szTitle);

    // 타겟 애플리케이션 명확히 판별
    if (strTitle.Find(_T("PowerPoint")) != -1 || strTitle.Find(_T("슬라이드 쇼")) != -1) {
        bIsTargetActive = true;
        return _T("PowerPoint");
    }
    else if (strTitle.Find(_T("YouTube")) != -1 || strTitle.Find(_T("유튜브")) != -1) {
        bIsTargetActive = true;
        return _T("YouTube");
    }

    return _T("Other"); // 타겟 창이 아닐 경우
}

/**
 * @brief 판별된 앱에 맞게 OS 키보드 이벤트를 발생시키고 서버 DB에 로그 전송
 */
void CClientSocket::ProcessGesture(int direction, const SensorData& data) {
    CWindowClientDlg* pDlg = (CWindowClientDlg*)AfxGetMainWnd();
    if (!pDlg) return;

    int nCheckedID = pDlg->GetCheckedRadioButton(IDC_RAD_AUTO, IDC_RAD_YT);
    bool bIsAuto = (nCheckedID == IDC_RAD_AUTO);
    bool bIsYT = (nCheckedID == IDC_RAD_YT);

    bool bIsTargetActive = false;
    CString currentApp = GetActiveWindowInfo(bIsTargetActive); // 활성창 검사

    CString names[] = { _T("NONE"), _T("NEXT"), _T("PREV") };
    CString status = _T("OK");

    // 활성화된 타겟 앱이 있을 때만 하드웨어 키보드 이벤트 발생
    if (bIsTargetActive) {
        bool bPlayYouTube = bIsYT || (bIsAuto && currentApp == _T("YouTube"));

        if (bPlayYouTube && (direction == 1 || direction == 2)) {
            // YouTube: Shift + N/P 조합키 전송
            BYTE targetKey = (direction == 1) ? 'N' : 'P';
            keybd_event(VK_SHIFT, 0, 0, 0);
            keybd_event(targetKey, 0, 0, 0);
            keybd_event(targetKey, 0, KEYEVENTF_KEYUP, 0);
            keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);

            Sleep(100);
            currentApp = _T("YouTube");
        }
        else {
            // PowerPoint: Page Down/Up 전송
            UINT vKeys[] = { 0, VK_NEXT, VK_PRIOR };
            BYTE scanCode = (BYTE)MapVirtualKey(vKeys[direction], 0);
            keybd_event(vKeys[direction], scanCode, 0, 0);
            keybd_event(vKeys[direction], scanCode, KEYEVENTF_KEYUP, 0);

            currentApp = _T("PowerPoint");
        }
    }
    else {
        status = _T("FAIL"); // 타겟 앱이 아니면 실패(FAIL)로 DB에 기록
    }

    // 결과를 리눅스 서버 DB에 저장하기 위해 전송 (LOG|방향|상태|앱|R:각도|L:조도)
    CString dbMsg;
    dbMsg.Format(_T("LOG|%s|%s|%s|R:%.2f|L:%d\n"),
        (LPCTSTR)names[direction], (LPCTSTR)status, (LPCTSTR)currentApp, data.roll, data.lux);
    CT2CA pszConverted(dbMsg);
    Send((LPCSTR)pszConverted, (int)strlen((LPCSTR)pszConverted));

    // UI 리스트 업데이트
    CString uiMsg;
    uiMsg.Format(status == _T("FAIL") ? _T("❌ %s (실패: 활성창 없음)") : _T("✅ %s (%s)"),
        (LPCTSTR)names[direction], (LPCTSTR)currentApp);

    pDlg->m_listLog.AddString(uiMsg);
    pDlg->m_listLog.SetCurSel(pDlg->m_listLog.GetCount() - 1);
    pDlg->m_stMainState.SetWindowText(uiMsg);
}

// ==============================================================================
// 🌟 네트워크 수신 메인 처리 함수
// ==============================================================================
void CClientSocket::OnReceive(int nErrorCode) {
    char szBuffer[1024];
    ::ZeroMemory(szBuffer, sizeof(szBuffer));

    int nRead = Receive(szBuffer, sizeof(szBuffer) - 1);

    // 🌟 핵심 방어: 수신 바이트가 0 이하면 서버가 죽은 것이므로 유령 소켓 진입 차단!
    if (nRead <= 0) {
        OnClose(0);
        return;
    }
    szBuffer[nRead] = '\0';

    CWindowClientDlg* pDlg = (CWindowClientDlg*)AfxGetMainWnd();
    if (!pDlg) return;

    CString strData(szBuffer);
    static SensorData lastData = { 0.0f, 0.0f, 0 };

    // [패턴 1] DB 연동 통계 데이터 (STAT) 수신
    if (strData.Find(_T("STAT|")) == 0) {
        int ppt, yt, fail;
        if (_stscanf_s((LPCTSTR)strData, _T("STAT|%d|%d|%d"), &ppt, &yt, &fail) == 3) {
            pDlg->m_nPPTCount = ppt;
            pDlg->m_nYTCount = yt;
            pDlg->m_nFailCount = fail;

            int total = ppt + yt + fail;
            float rate = (total > 0) ? ((float)(ppt + yt) / total) * 100.0f : 0.0f;

            CString strStat;
            strStat.Format(_T("📊 DB 연동 통계 | PPT: %d회 | YouTube: %d회 | 허공(실패): %d회 | 🎯 성공률: %.1f%%"),
                ppt, yt, fail, rate);
            pDlg->SetWindowText(strStat);
        }
        return;
    }

    // [패턴 2] 서버 명령 (CMD) 수신
    if (strData.Find(_T("CMD|NEXT")) != -1) {
        if (pDlg->m_chkControl.GetCheck() == BST_CHECKED) ProcessGesture(1, lastData);
        return;
    }
    else if (strData.Find(_T("CMD|PREV")) != -1) {
        if (pDlg->m_chkControl.GetCheck() == BST_CHECKED) ProcessGesture(2, lastData);
        return;
    }

    // [패턴 3] 일반 센서 데이터 파싱
    SensorData data;
    if (!ParseSensorData(strData, data)) {
        CSocket::OnReceive(nErrorCode);
        return;
    }
    lastData = data;

    pDlg->m_progLux.SetPos(data.lux);
    CString strPos;
    strPos.Format(_T("R:%.2f, P:%.2f"), data.roll, data.pitch);
    pDlg->m_stPos.SetWindowText(strPos);

    // 🌟 부가 기능: MFC GDI를 이용한 실시간 제스처 시각화 (도화지 그리기)
    CWnd* pCanvas = pDlg->GetDlgItem(IDC_PIC_CANVAS);
    if (pCanvas != nullptr)
    {
        CRect rect;
        pCanvas->GetClientRect(&rect);
        CClientDC dc(pCanvas); // 캔버스 영역에 대한 디바이스 컨텍스트 획득

        float ratioX = (data.roll + 90.0f) / 180.0f;
        float ratioY = (data.pitch + 90.0f) / 180.0f;

        // 클리핑(Clipping): 좌표가 캔버스를 벗어나지 않도록 방어
        int x = max(0, min((int)(ratioX * rect.Width()), rect.Width()));
        int y = max(0, min((int)(ratioY * rect.Height()), rect.Height()));

        if (pDlg->m_bIsFirst) {
            pDlg->m_ptPrev = CPoint(x, y);
            pDlg->m_bIsFirst = FALSE;
        }

        // 펜 객체 생성 및 그리기
        CPen pen(PS_SOLID, 2, RGB(0, 0, 0));
        CPen* pOldPen = dc.SelectObject(&pen);

        dc.MoveTo(pDlg->m_ptPrev);
        dc.LineTo(x, y);

        dc.SelectObject(pOldPen); // 메모리 누수 방지를 위한 기존 펜 반환
        pDlg->m_ptPrev = CPoint(x, y);
    }

    CSocket::OnReceive(nErrorCode);
}