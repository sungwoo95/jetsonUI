
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
	double stageCopyMs = 0.0;   // 입력 스테이징 복사: 카메라 버퍼 -> pinned (ms)
	double outCopyMs = 0.0;     // 출력 복사: pinned -> 결과 버퍼 (ms)
	UINT32 format = 0;          // 0=Raw8, 1=Jpeg
	double encodeMs = 0.0;      // 젯슨 JPEG 인코딩 (ms)
	UINT32 payloadBytes = 0;    // 실제 전송된 페이로드 크기

	// PC 측 측정 시간
	double transferMs = 0.0;    // 픽셀 데이터 수신(네트워크 전송) (ms)
	double decodeMs = 0.0;      // JPEG 디코딩 (ms)
	double copyMs = 0.0;        // DIB 정렬 버퍼 복사 (ms)
};

// 최근 N프레임 이동평균 (링 버퍼 + running sum)
class RollingMean
{
public:
	void add(double v)
	{
		if (count_ < kWindow) { buf_[head_] = v; sum_ += v; ++count_; }
		else { sum_ -= buf_[head_]; buf_[head_] = v; sum_ += v; }
		head_ = (head_ + 1) % kWindow;
	}
	void reset() { count_ = 0; head_ = 0; sum_ = 0.0; }
	double mean() const { return count_ > 0 ? sum_ / count_ : 0.0; }

private:
	static const int kWindow = 60;
	double buf_[kWindow] = {};
	int count_ = 0;
	int head_ = 0;
	double sum_ = 0.0;
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
		IDC_BTN_GRAB,
		IDC_STATUS_TEXT,
		IDC_LOG_LIST,
		IDC_PIPELINE_LIST,
	};
	CEdit m_hostEdit;
	CEdit m_portEdit;
	CButton m_connectButton;	// Connect <-> Disconnect 토글
	CButton m_grabButton;		// Start Grab <-> Stop Grab 토글 (연결 중에만 활성)
	bool m_grabbing = false;
	bool m_logFirstFrame = false;	// 그랩 시작 후 첫 프레임의 수신 상세를 1회 로그
	CStatic m_statusText;
	CListBox m_logList;
	CListBox m_pipelineList;	// 이미지 오른쪽: 파이프라인 단계별 시간 (현재/평균)
	CFont m_monoFont;			// 파이프라인 표 정렬용 고정폭 폰트

	// 파이프라인 단계 인덱스 (현재값/이동평균 공용). FPS는 단일 평활값이라 제외.
	// PS_INCOPY/PS_OUTCOPY: GPU 파이프라인 내부의 입력/출력 CPU 복사.
	// PS_GPUOTHER: GPU total에서 위 단계들을 뺀 잔여(submit/sync 등, ~0.5ms)
	// -> 표 합계가 E2E와 맞도록 유지.
	enum { PS_CAPTURE, PS_INCOPY, PS_H2D, PS_CUDA, PS_D2H, PS_OUTCOPY,
		PS_GPUOTHER, PS_ENCODE, PS_TRANSFER, PS_DECODE, PS_RENDER, PS_E2E, PS_COUNT };
	RollingMean m_avg[PS_COUNT];	// 단계별 최근 N프레임 이동평균

	// 마지막 렌더링(StretchDIBits) 소요 시간 (UI 스레드에서만 접근)
	double m_lastRenderMs = 0.0;

	// 표시 크기로 리사이즈된 프레임 버퍼 (drawFrame에서 재사용)
	std::vector<BYTE> m_scaled;

	// 더블 버퍼: 프레임 수신 시 1회만 축소 렌더링하고 OnPaint는 BitBlt만 수행
	CDC m_backDC;
	CBitmap m_backBmp;
	CBitmap* m_backOld = nullptr;
	CSize m_backSize{ 0, 0 };
	bool m_backValid = false;
	CRect m_imageArea{ 0, 0, 0, 0 };	// 레이아웃이 정한 이미지 표시 영역

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
	int m_warmupRemaining = 0;	// 그랩 시작 후 측정에서 제외할 남은 프레임 수(cold-start outlier)

	void receiveLoop(CStringA host, int port);
	void postStatus(const CString& text);
	void postLog(const CString& text);
	void joinReceiveThread();
	void drawFrame(CDC& dc, const CRect& area);
	void renderFrameToBackBuffer();
	void updateTimingDisplay();
	void layoutControls();

	virtual void OnOK() {}	// Enter 키로 대화 상자가 닫히지 않도록 함

	// 생성된 메시지 맵 함수
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnBnClickedConnect();
	afx_msg void OnBnClickedGrab();
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg LRESULT OnFrameReceived(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnStatusMessage(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()
};
