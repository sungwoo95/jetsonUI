
// jetsonUIDlg.h: 헤더 파일
//

#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

// 젯슨에서 수신한 한 프레임 (8bit grayscale, 행은 4바이트 정렬)
struct ReceivedFrame
{
	UINT32 width = 0;
	UINT32 height = 0;
	UINT64 frameNumber = 0;
	int strideBytes = 0;
	std::vector<BYTE> pixels;
};

// CjetsonUIDlg 대화 상자
class CjetsonUIDlg : public CDialogEx
{
// 생성입니다.
public:
	CjetsonUIDlg(CWnd* pParent = nullptr);	// 표준 생성자입니다.

// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_JETSONUI_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 지원입니다.


// 구현입니다.
protected:
	HICON m_hIcon;

	// 동적 생성 컨트롤
	enum
	{
		IDC_HOST_EDIT = 2001,
		IDC_PORT_EDIT,
		IDC_BTN_CONNECT,
		IDC_STATUS_TEXT,
		IDC_LOG_LIST,
	};
	CEdit m_hostEdit;
	CEdit m_portEdit;
	CButton m_connectButton;
	CStatic m_statusText;
	CListBox m_logList;

	// 수신 스레드 상태
	std::thread m_recvThread;
	std::atomic<SOCKET> m_socket{ INVALID_SOCKET };

	// 표시할 프레임 (UI 스레드에서 갱신)
	std::mutex m_frameMutex;
	ReceivedFrame m_frame;
	bool m_hasFrame = false;

	void receiveLoop(CStringA host, int port);
	void postStatus(const CString& text);
	void postLog(const CString& text);
	void joinReceiveThread();
	void drawFrame(CDC& dc, const CRect& area);

	virtual void OnOK() {}	// Enter 키로 대화 상자가 닫히지 않도록 함

	// 생성된 메시지 맵 함수
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnBnClickedConnect();
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg LRESULT OnFrameReceived(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnStatusMessage(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()
};
