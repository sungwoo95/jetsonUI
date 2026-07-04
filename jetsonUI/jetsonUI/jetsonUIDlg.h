
// jetsonUIDlg.h: 헤더 파일
//

#pragma once

#include <atomic>
#include <chrono>
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

	// 젯슨 파이프라인 단계별 소요 시간 (헤더로 수신)
	UINT64 captureUs = 0;       // 카메라 캡처 (us)
	UINT64 serviceTotalUs = 0;  // 캡처+GPU 처리 전체 (us)
	double h2dMs = 0.0;         // Host -> Device 복사 (ms)
	double cudaMs = 0.0;        // CUDA 커널 (ms)
	double d2hMs = 0.0;         // Device -> Host 복사 (ms)
	double totalMs = 0.0;       // GPU 파이프라인 전체 (ms)
	UINT32 format = 0;          // 0=Raw8, 1=Jpeg
	double encodeMs = 0.0;      // 젯슨 JPEG 인코딩 (ms)
	UINT32 payloadBytes = 0;    // 실제 전송된 페이로드 크기

	// PC 측 측정 시간
	double transferMs = 0.0;    // 픽셀 데이터 수신(네트워크 전송) (ms)
	double decodeMs = 0.0;      // JPEG 디코딩 (ms)
	double copyMs = 0.0;        // DIB 정렬 버퍼 복사 (ms)
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
		IDC_PIPELINE_LIST,
	};
	CEdit m_hostEdit;
	CEdit m_portEdit;
	CButton m_connectButton;
	CStatic m_statusText;
	CListBox m_logList;
	CListBox m_pipelineList;	// 이미지 오른쪽: 파이프라인 단계별 시간

	// 마지막 렌더링(StretchDIBits) 소요 시간 (UI 스레드에서만 접근)
	double m_lastRenderMs = 0.0;

	// 수신 스레드 상태
	std::thread m_recvThread;
	std::atomic<SOCKET> m_socket{ INVALID_SOCKET };

	// 표시할 프레임 (UI 스레드에서 갱신)
	std::mutex m_frameMutex;
	ReceivedFrame m_frame;
	bool m_hasFrame = false;

	// FPS 측정 (UI 스레드에서 수신 간격 기준, 지수이동평균으로 평활화)
	std::chrono::steady_clock::time_point m_lastFrameTime;
	bool m_haveFrameTime = false;
	double m_fps = 0.0;
	UINT64 m_displayedFrames = 0;

	void receiveLoop(CStringA host, int port);
	void postStatus(const CString& text);
	void postLog(const CString& text);
	void joinReceiveThread();
	void drawFrame(CDC& dc, const CRect& area);
	void updateTimingDisplay();

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
