
// WindowClientDlg.h: 헤더 파일
//

#pragma once
#include "CClientSocket.h"

// CWindowClientDlg 대화 상자
class CWindowClientDlg : public CDialogEx
{
// 생성입니다.
public:
	CWindowClientDlg(CWnd* pParent = nullptr);	// 표준 생성자입니다.

	CClientSocket m_ClientSocket;
	CPoint m_ptPrev;    // 선을 그을 때 직전의 좌표를 기억
	BOOL m_bIsFirst;    // 첫 번째 데이터인지 확인용 (처음부터 선이 튀지 않게)
	float m_oldRoll;   // 직전의 Roll 값을 저장할 변수
	float m_oldPitch;  // 상/하 움직임 비교용 변수 추가
	ULONGLONG m_lastGestureTime;


// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_WINDOWCLIENT_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 지원입니다.


// 구현입니다.
protected:
	HICON m_hIcon;

	// 생성된 메시지 맵 함수
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
//	CStatic m_picCanvas;
//	CStatic m_picCanvas;
	CSliderCtrl m_slSense;
	CProgressCtrl m_progLux;
	CListBox m_listLog;
	CStatic m_stStatus;
	CButton m_chkControl;
	CStatic m_stMainState;
	CStatic m_stPos;
	CStatic m_picCanvas;
	int m_nPPTCount;
	int m_nYTCount;
	int m_nFailCount;
	afx_msg void OnBnClickedBtnConnect();
	afx_msg void OnBnClickedBtnClear();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
};
