// WindowClientDlg.cpp: 구현 파일
#include "pch.h"
#include "framework.h"
#include "WindowClient.h"
#include "WindowClientDlg.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ==============================================================================
// 🌟 CAboutDlg 대화 상자 (프로그램 정보 창)
// ==============================================================================
class CAboutDlg : public CDialogEx {
public:
	CAboutDlg() : CDialogEx(IDD_ABOUTBOX) {}
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif
protected:
	virtual void DoDataExchange(CDataExchange* pDX) { CDialogEx::DoDataExchange(pDX); }
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

// ==============================================================================
// 🌟 CWindowClientDlg 생성자 및 데이터 바인딩
// ==============================================================================
/**
 * @brief 다이얼로그 생성자: 변수 초기화 및 리소스 연결
 */
CWindowClientDlg::CWindowClientDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_WINDOWCLIENT_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	// 도화지 및 제스처 판정용 초기값 설정
	m_bIsFirst = TRUE;
	m_ptPrev = CPoint(0, 0);
	m_oldRoll = 0.0f;
	m_oldPitch = 0.0f;
	m_lastGestureTime = 0;

	// 리눅스 서버 DB로부터 동기화될 통계 변수 초기화
	m_nPPTCount = 0;
	m_nYTCount = 0;
	m_nFailCount = 0;
}

void CWindowClientDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SLIDER_SENSE, m_slSense);
	DDX_Control(pDX, IDC_PROG_LUX, m_progLux);
	DDX_Control(pDX, IDC_LIST_LOG, m_listLog);
	DDX_Control(pDX, IDC_ST_STATUS, m_stStatus);
	DDX_Control(pDX, IDC_CHK_CONTROL_ON, m_chkControl);
	DDX_Control(pDX, IDC_ST_MAIN_STATE, m_stMainState);
	DDX_Control(pDX, IDC_ST_POS, m_stPos);
	DDX_Control(pDX, IDC_PIC_CANVAS, m_picCanvas);
}

// 메시지 맵: 이벤트와 함수를 연결
BEGIN_MESSAGE_MAP(CWindowClientDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BTN_CONNECT, &CWindowClientDlg::OnBnClickedBtnConnect)
	ON_BN_CLICKED(IDC_BTN_CLEAR, &CWindowClientDlg::OnBnClickedBtnClear)
	ON_WM_TIMER() // 🌟 타이머 이벤트 연결
END_MESSAGE_MAP()

// ==============================================================================
// 🌟 초기화 및 시스템 이벤트
// ==============================================================================
BOOL CWindowClientDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);

	// 1. 슬라이더 및 프로그레스 바 범위 설정
	m_slSense.SetRange(0, 100);
	m_slSense.SetPos(50);
	m_progLux.SetRange(0, 1023);
	m_progLux.SetPos(0);

	// 2. 초기 화면 텍스트 및 모드 설정
	m_listLog.AddString(_T("🚀 스마트 제스처 시스템이 시작되었습니다."));
	m_stStatus.SetWindowText(_T("상태: 오프라인 (자동 접속 중...)"));
	m_stMainState.SetWindowText(_T("대기 중"));
	CheckRadioButton(IDC_RAD_AUTO, IDC_RAD_YT, IDC_RAD_AUTO);

	// 3. 🌟 Auto-Recovery (좀비 모드) 타이머 기동
	// 서버 연결이 끊어지더라도 3초 주기로 스스로 재접속을 시도하여 무중단 환경(가용성)을 구축합니다.
	SetTimer(1, 3000, NULL);

	return TRUE;
}

/**
 * @brief 타이머 콜백 함수 (Auto-Recovery 기능의 핵심)
 */
void CWindowClientDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1) // 1번 타이머: 자동 재접속 감시
	{
		// 소켓이 연결되어 있지 않은(INVALID) 상태일 때만 백그라운드에서 접속 시도
		if (m_ClientSocket.m_hSocket == INVALID_SOCKET)
		{
			if (m_ClientSocket.Create())
			{
				if (m_ClientSocket.Connect(_T("192.168.56.114"), 8080))
				{
					m_listLog.AddString(_T("✅ 서버 자동 연결 성공!"));
					m_stStatus.SetWindowText(_T("상태: 서버 연결됨"));
					GetDlgItem(IDC_BTN_CONNECT)->SetWindowText(_T("접속 종료"));
				}
				else
				{
					m_ClientSocket.Close();
					m_stStatus.SetWindowText(_T("상태: 서버 찾는 중..."));
				}
			}
		}
	}
	CDialogEx::OnTimer(nIDEvent);
}

// ==============================================================================
// 🌟 버튼 이벤트 핸들러
// ==============================================================================
void CWindowClientDlg::OnBnClickedBtnConnect()
{
	CString strBtnText;
	GetDlgItemText(IDC_BTN_CONNECT, strBtnText);

	// CASE [1]: 사용자가 수동으로 연결을 끊는 경우
	if (strBtnText == _T("접속 종료")) {
		KillTimer(1); // 🌟 수동 종료 시 타이머를 꺼서 쓸데없는 재접속을 방지 (리소스 최적화)

		if (m_ClientSocket.m_hSocket != INVALID_SOCKET) {
			// 🌟 핵심: Graceful Shutdown 적용
			// 무턱대고 Close()로 끊지 않고, TCP FIN 패킷을 보내 서버에게 정중히 이별을 통보합니다.
			m_ClientSocket.ShutDown(2); // 2 == SD_BOTH (송수신 모두 차단)
			m_ClientSocket.Close();
		}

		m_listLog.AddString(_T("사용자가 연결을 종료했습니다."));
		m_listLog.SetCurSel(m_listLog.GetCount() - 1);
		m_stStatus.SetWindowText(_T("상태: 오프라인 (수동 종료)"));

		GetDlgItem(IDC_BTN_CONNECT)->SetWindowText(_T("서버 접속"));
		return;
	}

	// CASE [2]: 수동으로 접속을 시도하는 경우
	GetDlgItem(IDC_BTN_CONNECT)->EnableWindow(FALSE); // 중복 클릭 방지

	if (m_ClientSocket.m_hSocket != INVALID_SOCKET) {
		m_ClientSocket.ShutDown(2); // 기존 소켓을 닫을 때도 예의를 갖춤
		m_ClientSocket.Close();
	}

	if (m_ClientSocket.Create())
	{
		m_listLog.AddString(_T("서버 접속 시도..."));
		if (m_ClientSocket.Connect(_T("192.168.56.114"), 8080))
		{
			m_listLog.AddString(_T("✅ 서버 수동 연결 성공!"));
			m_stStatus.SetWindowText(_T("상태: 서버 연결됨"));

			GetDlgItem(IDC_BTN_CONNECT)->SetWindowText(_T("접속 종료"));
			SetTimer(1, 3000, NULL); // 접속 성공 시 감시 타이머 다시 기동
		}
		else
		{
			AfxMessageBox(_T("서버 접속 실패! 리눅스 서버를 확인하세요."));
			m_stStatus.SetWindowText(_T("상태: 접속 실패"));
			m_ClientSocket.Close();
			SetTimer(1, 3000, NULL); // 실패해도 3초 뒤 다시 시도하도록 타이머는 유지
		}
	}

	GetDlgItem(IDC_BTN_CONNECT)->EnableWindow(TRUE);
	m_listLog.SetCurSel(m_listLog.GetCount() - 1);
}

void CWindowClientDlg::OnBnClickedBtnClear()
{
	// 도화지(캔버스) 하얀색으로 덮어 초기화
	CRect rect;
	m_picCanvas.GetClientRect(&rect);
	CClientDC dc(&m_picCanvas);
	dc.FillSolidRect(rect, RGB(255, 255, 255));
	m_bIsFirst = TRUE;
}

// ==============================================================================
// 🌟 그래픽 및 시스템 아이콘 처리 (MFC 기본 구조)
// ==============================================================================
void CWindowClientDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this);
		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect; GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
		// 화면 갱신 시 캔버스를 하얗게 유지
		CRect rect;
		m_picCanvas.GetClientRect(&rect);
		CClientDC dc(&m_picCanvas);
		dc.FillSolidRect(rect, RGB(255, 255, 255));
	}
}

HCURSOR CWindowClientDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CWindowClientDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX) { CAboutDlg dlgAbout; dlgAbout.DoModal(); }
	else { CDialogEx::OnSysCommand(nID, lParam); }
}