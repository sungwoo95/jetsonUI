
// jetsonUIDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "jetsonUI.h"
#include "jetsonUIDlg.h"
#include "afxdialogex.h"

#include <opencv2/imgcodecs.hpp>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	// 젯슨 tcp_server.cpp::sendFrame 프로토콜과 동일 (little-endian, 84바이트)
	// [uint32 width][uint32 height][uint64 frameNumber][uint32 payloadBytes]
	// [uint64 captureUs][uint64 serviceTotalUs]
	// [double h2dMs][double cudaMs][double d2hMs][double totalMs]
	// [uint32 format][uint32 reserved][double encodeMs]
	constexpr int kHeaderBytes = 84;
	constexpr UINT32 kMaxPayloadBytes = 100 * 1024 * 1024;
	constexpr UINT32 kFormatRaw8 = 0;
	constexpr UINT32 kFormatJpeg = 1;

	constexpr UINT WM_APP_FRAME_RECEIVED = WM_APP + 1;
	constexpr UINT WM_APP_STATUS = WM_APP + 2;
	// WM_APP_STATUS wParam: 0=상태텍스트, 1=스레드 종료, 2=로그, 3=연결 수립

	// UI -> 젯슨 제어 명령 (젯슨 tcp_server.h Command와 동일)
	constexpr BYTE kCmdStartGrab = 1;
	constexpr BYTE kCmdStopGrab = 2;

	// 상단 컨트롤 영역 높이 (픽셀)
	constexpr int kToolbarHeight = 40;

	// 오른쪽 정보 열(파이프라인 + 로그) 레이아웃
	constexpr int kSideColumnMinWidth = 230;
	constexpr int kPipelineHeight = 275;	// 파이프라인 리스트 높이 (헤더+9단계+구분+FPS = 12행)

	// 오른쪽 열의 시작 x 좌표 (이미지와 정보 열의 경계)
	int sideColumnLeft(int clientWidth)
	{
		int sideWidth = std::max(kSideColumnMinWidth, (clientWidth * 35) / 100);
		return std::max(0, clientWidth - sideWidth);
	}

	double elapsedMs(std::chrono::steady_clock::time_point begin)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
	}

	bool recvAll(SOCKET sock, BYTE* buffer, int bytes)
	{
		int received = 0;
		while (received < bytes)
		{
			int chunk = recv(sock, reinterpret_cast<char*>(buffer) + received, bytes - received, 0);
			if (chunk <= 0)
			{
				return false;
			}
			received += chunk;
		}
		return true;
	}

	// JPEG을 8bit grayscale(행 4바이트 정렬) 버퍼로 디코딩 (OpenCV cv::imdecode)
	// IMREAD_GRAYSCALE로 libjpeg-turbo(SIMD) 경로를 사용하며, DIB stride에 맞춰 행 단위 복사.
	bool decodeJpegToGray(const BYTE* data, UINT32 bytes, UINT32 width, UINT32 height,
		int strideBytes, std::vector<BYTE>& out)
	{
		// 입력 버퍼를 복사 없이 감싸는 1xN Mat (imdecode가 내부적으로 파싱)
		cv::Mat encoded(1, static_cast<int>(bytes), CV_8UC1, const_cast<BYTE*>(data));
		cv::Mat gray = cv::imdecode(encoded, cv::IMREAD_GRAYSCALE);
		if (gray.empty() || gray.cols != static_cast<int>(width) || gray.rows != static_cast<int>(height))
		{
			return false;
		}

		out.resize(static_cast<size_t>(strideBytes) * height);
		for (UINT32 y = 0; y < height; ++y)
		{
			memcpy(out.data() + static_cast<size_t>(y) * strideBytes, gray.ptr(y), width);
		}
		return true;
	}
}


// 응용 프로그램 정보에 사용되는 CAboutDlg 대화 상자입니다.

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원입니다.

// 구현입니다.
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CjetsonUIDlg 대화 상자



CjetsonUIDlg::CjetsonUIDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_JETSONUI_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CjetsonUIDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CjetsonUIDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_BN_CLICKED(IDC_BTN_CONNECT, &CjetsonUIDlg::OnBnClickedConnect)
	ON_BN_CLICKED(IDC_BTN_GRAB, &CjetsonUIDlg::OnBnClickedGrab)
	ON_MESSAGE(WM_APP_FRAME_RECEIVED, &CjetsonUIDlg::OnFrameReceived)
	ON_MESSAGE(WM_APP_STATUS, &CjetsonUIDlg::OnStatusMessage)
END_MESSAGE_MAP()


// CjetsonUIDlg 메시지 처리기

BOOL CjetsonUIDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 시스템 메뉴에 "정보..." 메뉴 항목을 추가합니다.

	// IDM_ABOUTBOX는 시스템 명령 범위에 있어야 합니다.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 이 대화 상자의 아이콘을 설정합니다.  응용 프로그램의 주 창이 대화 상자가 아닐 경우에는
	//  프레임워크가 이 작업을 자동으로 수행합니다.
	SetIcon(m_hIcon, TRUE);			// 큰 아이콘을 설정합니다.
	SetIcon(m_hIcon, FALSE);		// 작은 아이콘을 설정합니다.

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		AfxMessageBox(_T("WSAStartup failed"));
	}

	// 상단 컨트롤 동적 생성
	CFont* font = GetFont();

	m_hostEdit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
		CRect(10, 9, 150, 32), this, IDC_HOST_EDIT);
	m_hostEdit.SetFont(font);
	m_hostEdit.SetWindowText(_T("192.168.20.10"));

	m_portEdit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER,
		CRect(158, 9, 218, 32), this, IDC_PORT_EDIT);
	m_portEdit.SetFont(font);
	m_portEdit.SetWindowText(_T("5000"));

	m_connectButton.Create(_T("Connect"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		CRect(226, 8, 320, 33), this, IDC_BTN_CONNECT);
	m_connectButton.SetFont(font);

	m_grabButton.Create(_T("Start Grab"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		CRect(328, 8, 432, 33), this, IDC_BTN_GRAB);
	m_grabButton.SetFont(font);
	m_grabButton.EnableWindow(FALSE);	// 연결 전에는 비활성

	m_statusText.Create(_T("Ready."), WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
		CRect(442, 8, 1200, 33), this, IDC_STATUS_TEXT);
	m_statusText.SetFont(font);

	// 파이프라인 리스트박스 (이미지 오른쪽 상단, 위치는 OnSize에서 결정)
	// 현재/평균 열 정렬을 위해 고정폭 폰트 사용
	m_monoFont.CreatePointFont(90, _T("Consolas"));
	m_pipelineList.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
		CRect(0, 0, 0, 0), this, IDC_PIPELINE_LIST);
	m_pipelineList.SetFont(&m_monoFont);
	m_pipelineList.AddString(_T("Pipeline: (no frame yet)"));

	// 로그 리스트박스 (파이프라인 아래, 위치는 OnSize에서 결정)
	m_logList.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
		CRect(0, 0, 0, 0), this, IDC_LOG_LIST);
	m_logList.SetFont(font);
	m_logList.AddString(_T("[System] Initialized."));

	return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void CjetsonUIDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

void CjetsonUIDlg::OnDestroy()
{
	// 수신 스레드가 recv에서 블로킹 중이면 소켓을 닫아 깨움
	SOCKET sock = m_socket.exchange(INVALID_SOCKET);
	if (sock != INVALID_SOCKET)
	{
		closesocket(sock);
	}
	joinReceiveThread();
	WSACleanup();

	CDialogEx::OnDestroy();
}

void CjetsonUIDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);

	// 레이아웃: 이미지(왼쪽) | 오른쪽 열 = 파이프라인(위) + 로그(아래)
	if (m_logList.GetSafeHwnd() != nullptr && m_pipelineList.GetSafeHwnd() != nullptr
		&& cy > kToolbarHeight + 100)
	{
		int left = sideColumnLeft(cx) + 5;
		int top = kToolbarHeight + 5;
		int pipelineBottom = std::min(top + kPipelineHeight, cy - 40);

		m_pipelineList.SetWindowPos(nullptr, left, top, cx - left - 5, pipelineBottom - top, SWP_NOZORDER);
		m_logList.SetWindowPos(nullptr, left, pipelineBottom + 5, cx - left - 5, cy - pipelineBottom - 10, SWP_NOZORDER);
	}

	Invalidate();
}

void CjetsonUIDlg::joinReceiveThread()
{
	if (m_recvThread.joinable())
	{
		m_recvThread.join();
	}
}

void CjetsonUIDlg::OnBnClickedConnect()
{
	// 연결 중이면 Disconnect로 동작: 소켓을 닫아 수신 스레드를 깨우고
	// 스레드 종료 알림(wParam=1)에서 버튼 상태가 복원된다.
	if (m_socket.load() != INVALID_SOCKET)
	{
		m_connectButton.EnableWindow(FALSE);
		SOCKET s = m_socket.exchange(INVALID_SOCKET);
		if (s != INVALID_SOCKET)
		{
			closesocket(s);
		}
		return;
	}

	joinReceiveThread();

	CString hostText, portText;
	m_hostEdit.GetWindowText(hostText);
	m_portEdit.GetWindowText(portText);
	int port = _ttoi(portText);
	if (hostText.IsEmpty() || port <= 0 || port > 65535)
	{
		AfxMessageBox(_T("Invalid host or port."));
		return;
	}

	m_connectButton.EnableWindow(FALSE);

	// FPS/이동평균 상태 초기화 (새 세션은 처음부터 측정)
	m_haveFrameTime = false;
	m_fps = 0.0;
	m_displayedFrames = 0;
	for (int i = 0; i < PS_COUNT; ++i)
	{
		m_avg[i].reset();
	}

	CStringA hostA(hostText);
	m_recvThread = std::thread(&CjetsonUIDlg::receiveLoop, this, hostA, port);
}

void CjetsonUIDlg::OnBnClickedGrab()
{
	SOCKET s = m_socket.load();
	if (s == INVALID_SOCKET)
	{
		return;
	}

	// 젯슨으로 1바이트 제어 명령 송신 (수신과 독립적인 방향이라 UI 스레드에서 안전)
	BYTE cmd = m_grabbing ? kCmdStopGrab : kCmdStartGrab;
	if (send(s, reinterpret_cast<const char*>(&cmd), 1, 0) != 1)
	{
		m_logList.AddString(_T("[Error] Failed to send grab command."));
		return;
	}

	m_grabbing = !m_grabbing;
	m_grabButton.SetWindowText(m_grabbing ? _T("Stop Grab") : _T("Start Grab"));
	m_logList.AddString(m_grabbing ? _T("[System] Grab start requested.") : _T("[System] Grab stop requested."));
	m_logList.SetTopIndex(m_logList.GetCount() - 1);
}

void CjetsonUIDlg::postStatus(const CString& text)
{
	// UI 스레드에서 해제하는 힙 문자열로 전달
	PostMessage(WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(new CString(text)));
}

void CjetsonUIDlg::postLog(const CString& text)
{
	// UI 스레드에서 해제하는 힙 문자열로 전달
	PostMessage(WM_APP_STATUS, 2, reinterpret_cast<LPARAM>(new CString(text)));
}

void CjetsonUIDlg::receiveLoop(CStringA host, int port)
{
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		postStatus(_T("socket() failed."));
		PostMessage(WM_APP_STATUS, 1, 0);
		return;
	}
	m_socket = sock;

	sockaddr_in addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<u_short>(port));
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
	{
		postStatus(_T("Invalid IP address."));
		closesocket(m_socket.exchange(INVALID_SOCKET));
		PostMessage(WM_APP_STATUS, 1, 0);
		return;
	}

	CString msg;
	msg.Format(_T("Connecting to %hs:%d ..."), (LPCSTR)host, port);
	postStatus(msg);

	if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
	{
		msg.Format(_T("Connect failed (WSA error %d)."), WSAGetLastError());
		postStatus(msg);
		postLog(msg);
		SOCKET s = m_socket.exchange(INVALID_SOCKET);
		if (s != INVALID_SOCKET) closesocket(s);
		PostMessage(WM_APP_STATUS, 1, 0);
		return;
	}

	msg.Format(_T("[%02d:%02d:%02d] Connected to %hs:%d"), 0, 0, 0, (LPCSTR)host, port);
	postStatus(_T("Connected. Press Start Grab."));
	postLog(msg);
	PostMessage(WM_APP_STATUS, 3, 0);	// 연결 수립: Disconnect/Start Grab 버튼 활성화

	for (;;)
	{
		BYTE header[kHeaderBytes];
		if (!recvAll(sock, header, kHeaderBytes))
		{
			postLog(_T("[Error] Connection lost while receiving header."));
			break;
		}

		UINT32 width, height, payloadBytes, format;
		UINT64 frameNumber, captureUs, serviceTotalUs;
		double h2dMs, cudaMs, d2hMs, totalMs, encodeMs;
		memcpy(&width, header + 0, 4);
		memcpy(&height, header + 4, 4);
		memcpy(&frameNumber, header + 8, 8);
		memcpy(&payloadBytes, header + 16, 4);
		memcpy(&captureUs, header + 20, 8);
		memcpy(&serviceTotalUs, header + 28, 8);
		memcpy(&h2dMs, header + 36, 8);
		memcpy(&cudaMs, header + 44, 8);
		memcpy(&d2hMs, header + 52, 8);
		memcpy(&totalMs, header + 60, 8);
		memcpy(&format, header + 68, 4);
		memcpy(&encodeMs, header + 76, 8);

		bool validPayload = (format == kFormatRaw8)
			? (payloadBytes == width * height)
			: (format == kFormatJpeg && payloadBytes > 0);
		if (width == 0 || height == 0 || !validPayload || payloadBytes > kMaxPayloadBytes)
		{
			msg.Format(_T("[Error] Invalid header (w=%u h=%u fmt=%u payload=%u). Disconnecting."), width, height, format, payloadBytes);
			postStatus(msg);
			postLog(msg);
			break;
		}

		// 헤더 수신 로그
		UINT32 totalRecvBytes = kHeaderBytes + payloadBytes;
		msg.Format(_T("[Frame #%llu] Header received: %u x %u, expecting %u bytes (header: %d)"),
			frameNumber, width, height, payloadBytes, kHeaderBytes);
		postLog(msg);

		// 픽셀 데이터 수신 (소요 시간 = 네트워크 전송 시간 근사치)
		std::vector<BYTE> raw(payloadBytes);
		auto transferBegin = std::chrono::steady_clock::now();
		if (!recvAll(sock, raw.data(), static_cast<int>(payloadBytes)))
		{
			msg.Format(_T("[Error] Connection lost while receiving pixel data for frame #%llu"), frameNumber);
			postLog(msg);
			break;
		}
		double transferMs = elapsedMs(transferBegin);

		// 수신 완료 로그
		msg.Format(_T("[Frame #%llu] Received: header(%d) + pixels(%u) = total %u bytes"),
			frameNumber, kHeaderBytes, payloadBytes, totalRecvBytes);
		postLog(msg);

		auto* frame = new ReceivedFrame();
		frame->width = width;
		frame->height = height;
		frame->frameNumber = frameNumber;
		frame->captureUs = captureUs;
		frame->serviceTotalUs = serviceTotalUs;
		frame->h2dMs = h2dMs;
		frame->cudaMs = cudaMs;
		frame->d2hMs = d2hMs;
		frame->totalMs = totalMs;
		frame->format = format;
		frame->encodeMs = (format == kFormatJpeg) ? encodeMs : 0.0;
		frame->payloadBytes = payloadBytes;
		frame->transferMs = transferMs;
		frame->strideBytes = (width + 3) & ~3u;

		if (format == kFormatJpeg)
		{
			// JPEG 디코딩 (수신 스레드에서 수행해 UI 스레드 부하 방지)
			auto decodeBegin = std::chrono::steady_clock::now();
			if (!decodeJpegToGray(raw.data(), payloadBytes, width, height,
				frame->strideBytes, frame->pixels))
			{
				msg.Format(_T("[Error] JPEG decode failed for frame #%llu (%u bytes). Skipping."), frameNumber, payloadBytes);
				postLog(msg);
				delete frame;
				continue;
			}
			frame->decodeMs = elapsedMs(decodeBegin);
		}
		else
		{
			auto copyBegin = std::chrono::steady_clock::now();
			frame->pixels.resize(static_cast<size_t>(frame->strideBytes) * height);
			for (UINT32 y = 0; y < height; ++y)
			{
				memcpy(frame->pixels.data() + static_cast<size_t>(y) * frame->strideBytes,
					raw.data() + static_cast<size_t>(y) * width, width);
			}
			frame->copyMs = elapsedMs(copyBegin);
		}

		PostMessage(WM_APP_FRAME_RECEIVED, 0, reinterpret_cast<LPARAM>(frame));
	}

	SOCKET s = m_socket.exchange(INVALID_SOCKET);
	if (s != INVALID_SOCKET)
	{
		closesocket(s);
		postStatus(_T("Disconnected."));
		postLog(_T("[System] Connection closed."));
	}
	PostMessage(WM_APP_STATUS, 1, 0);	// wParam=1: 스레드 종료 알림 (버튼 재활성화)
}

LRESULT CjetsonUIDlg::OnFrameReceived(WPARAM /*wParam*/, LPARAM lParam)
{
	ReceivedFrame* frame = reinterpret_cast<ReceivedFrame*>(lParam);
	if (frame == nullptr)
	{
		return 0;
	}

	{
		std::lock_guard<std::mutex> lock(m_frameMutex);
		m_frame = std::move(*frame);
		m_hasFrame = true;
	}
	delete frame;

	// FPS: 이전 프레임과의 수신 간격으로 계산, EMA(0.2)로 평활화
	auto now = std::chrono::steady_clock::now();
	if (m_haveFrameTime)
	{
		double deltaMs = std::chrono::duration<double, std::milli>(now - m_lastFrameTime).count();
		if (deltaMs > 0.0)
		{
			double inst = 1000.0 / deltaMs;
			m_fps = (m_fps > 0.0) ? (m_fps * 0.8 + inst * 0.2) : inst;
		}
	}
	m_lastFrameTime = now;
	m_haveFrameTime = true;
	++m_displayedFrames;

	CString status;
	status.Format(_T("Frame #%llu  %u x %u  |  %.1f fps  (displayed %llu)"),
		m_frame.frameNumber, m_frame.width, m_frame.height, m_fps, m_displayedFrames);
	m_statusText.SetWindowText(status);

	// 프레임별 타이밍 로그 (스트리밍 시 무한 증가 방지: 최근 200개만 유지)
	CString log;
	log.Format(_T("[#%llu] %s %u bytes | cap=%.1f enc=%.1f xfer=%.1f dec=%.1f | %.1ffps"),
		m_frame.frameNumber,
		(m_frame.format == kFormatJpeg) ? _T("JPEG") : _T("RAW"),
		m_frame.payloadBytes,
		m_frame.captureUs / 1000.0,
		m_frame.encodeMs, m_frame.transferMs, m_frame.decodeMs, m_fps);
	m_logList.AddString(log);
	while (m_logList.GetCount() > 200)
	{
		m_logList.DeleteString(0);
	}
	m_logList.SetTopIndex(m_logList.GetCount() - 1);

	// 이동평균 갱신 (프레임당 1회. Render는 직전 프레임 값이라 1프레임 지연 — 무시 가능)
	double e2e = m_frame.serviceTotalUs / 1000.0 + m_frame.encodeMs
		+ m_frame.transferMs + m_frame.decodeMs + m_frame.copyMs + m_lastRenderMs;
	m_avg[PS_CAPTURE].add(m_frame.captureUs / 1000.0);
	m_avg[PS_H2D].add(m_frame.h2dMs);
	m_avg[PS_CUDA].add(m_frame.cudaMs);
	m_avg[PS_D2H].add(m_frame.d2hMs);
	m_avg[PS_ENCODE].add(m_frame.encodeMs);
	m_avg[PS_TRANSFER].add(m_frame.transferMs);
	m_avg[PS_DECODE].add(m_frame.decodeMs);
	m_avg[PS_RENDER].add(m_lastRenderMs);
	m_avg[PS_E2E].add(e2e);

	updateTimingDisplay();

	Invalidate(FALSE);
	return 0;
}

void CjetsonUIDlg::updateTimingDisplay()
{
	if (!m_hasFrame || m_pipelineList.GetSafeHwnd() == nullptr)
	{
		return;
	}

	// 캡처->렌더링 전체 시간: 두 장비의 시계는 동기화되어 있지 않으므로
	// 타임스탬프 비교 대신 각 구간 측정치를 더해 근사
	// (serviceTotal = Capture + GPU 파이프라인 전체를 포함)
	double e2eNow = m_frame.serviceTotalUs / 1000.0 + m_frame.encodeMs
		+ m_frame.transferMs + m_frame.decodeMs + m_frame.copyMs + m_lastRenderMs;

	// 3열 표: 단계 / 현재 프레임(now) / 최근 N프레임 이동평균(avg)
	struct Row { const TCHAR* label; double now; int stage; };
	Row rows[] = {
		{ _T("Capture"),  m_frame.captureUs / 1000.0, PS_CAPTURE },
		{ _T("H2D"),      m_frame.h2dMs,              PS_H2D },
		{ _T("CUDA"),     m_frame.cudaMs,             PS_CUDA },
		{ _T("D2H"),      m_frame.d2hMs,              PS_D2H },
		{ _T("Encode"),   m_frame.encodeMs,           PS_ENCODE },
		{ _T("Transfer"), m_frame.transferMs,         PS_TRANSFER },
		{ _T("Decode"),   m_frame.decodeMs,           PS_DECODE },
		{ _T("Render"),   m_lastRenderMs,             PS_RENDER },
		{ _T("E2E"),      e2eNow,                     PS_E2E },
	};

	m_pipelineList.SetRedraw(FALSE);
	m_pipelineList.ResetContent();

	CString header;
	header.Format(_T("%-8s %8s %8s"), _T("Stage"), _T("now"), _T("avg"));
	m_pipelineList.AddString(header);

	CString line;
	for (const Row& r : rows)
	{
		line.Format(_T("%-8s %8.1f %8.1f"), r.label, r.now, m_avg[r.stage].mean());
		m_pipelineList.AddString(line);
	}
	m_pipelineList.AddString(_T("-------------------------"));
	line.Format(_T("%-8s %8.1f"), _T("FPS"), m_fps);	// FPS는 평활된 단일 값
	m_pipelineList.AddString(line);

	m_pipelineList.SetRedraw(TRUE);
	m_pipelineList.Invalidate();
}

LRESULT CjetsonUIDlg::OnStatusMessage(WPARAM wParam, LPARAM lParam)
{
	CString* text = reinterpret_cast<CString*>(lParam);
	if (text != nullptr)
	{
		if (wParam == 2)
		{
			// 로그 메시지 추가
			m_logList.AddString(*text);
			// 최신 항목으로 스크롤
			int count = m_logList.GetCount();
			if (count > 0)
			{
				m_logList.SetTopIndex(count - 1);
			}
		}
		else
		{
			// 상태 텍스트 업데이트
			m_statusText.SetWindowText(*text);
		}
		delete text;
	}
	if (wParam == 1)
	{
		// 수신 스레드 종료: 연결 전 상태로 복원
		m_connectButton.SetWindowText(_T("Connect"));
		m_connectButton.EnableWindow(TRUE);
		m_grabbing = false;
		m_grabButton.SetWindowText(_T("Start Grab"));
		m_grabButton.EnableWindow(FALSE);
	}
	else if (wParam == 3)
	{
		// 연결 수립: Disconnect 토글 + 그랩 버튼 활성화
		m_connectButton.SetWindowText(_T("Disconnect"));
		m_connectButton.EnableWindow(TRUE);
		m_grabbing = false;
		m_grabButton.SetWindowText(_T("Start Grab"));
		m_grabButton.EnableWindow(TRUE);
	}
	return 0;
}

void CjetsonUIDlg::drawFrame(CDC& dc, const CRect& area)
{
	std::lock_guard<std::mutex> lock(m_frameMutex);

	if (!m_hasFrame || area.Width() <= 0 || area.Height() <= 0)
	{
		dc.FillSolidRect(area, RGB(30, 30, 30));
		return;
	}

	// 8bit grayscale DIB (팔레트 256단계)
	struct
	{
		BITMAPINFOHEADER header;
		RGBQUAD palette[256];
	} bmi;
	ZeroMemory(&bmi, sizeof(bmi));
	bmi.header.biSize = sizeof(BITMAPINFOHEADER);
	bmi.header.biWidth = static_cast<LONG>(m_frame.width);
	bmi.header.biHeight = -static_cast<LONG>(m_frame.height);	// top-down
	bmi.header.biPlanes = 1;
	bmi.header.biBitCount = 8;
	bmi.header.biCompression = BI_RGB;
	for (int i = 0; i < 256; ++i)
	{
		bmi.palette[i].rgbRed = bmi.palette[i].rgbGreen = bmi.palette[i].rgbBlue = static_cast<BYTE>(i);
	}

	// 종횡비를 유지하며 영역에 맞춤
	double scale = std::min(
		static_cast<double>(area.Width()) / m_frame.width,
		static_cast<double>(area.Height()) / m_frame.height);
	int destW = std::max(1, static_cast<int>(m_frame.width * scale));
	int destH = std::max(1, static_cast<int>(m_frame.height * scale));
	int destX = area.left + (area.Width() - destW) / 2;
	int destY = area.top + (area.Height() - destH) / 2;

	auto renderBegin = std::chrono::steady_clock::now();
	dc.FillSolidRect(area, RGB(30, 30, 30));
	SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
	SetBrushOrgEx(dc.GetSafeHdc(), 0, 0, nullptr);
	StretchDIBits(dc.GetSafeHdc(),
		destX, destY, destW, destH,
		0, 0, m_frame.width, m_frame.height,
		m_frame.pixels.data(),
		reinterpret_cast<const BITMAPINFO*>(&bmi),
		DIB_RGB_COLORS, SRCCOPY);
	GdiFlush();	// 배칭된 GDI 명령을 실행시켜 렌더링 시간을 실제에 가깝게 측정
	m_lastRenderMs = elapsedMs(renderBegin);
}

// 대화 상자에 최소화 단추를 추가할 경우 아이콘을 그리려면
//  아래 코드가 필요합니다.  문서/뷰 모델을 사용하는 MFC 애플리케이션의 경우에는
//  프레임워크에서 이 작업을 자동으로 수행합니다.

void CjetsonUIDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 그리기를 위한 디바이스 컨텍스트입니다.

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 클라이언트 사각형에서 아이콘을 가운데에 맞춥니다.
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 아이콘을 그립니다.
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CPaintDC dc(this);

		CRect client;
		GetClientRect(&client);
		// 이미지 영역: 툴바 아래 왼쪽 (오른쪽 열은 파이프라인 + 로그)
		CRect imageArea(client.left, client.top + kToolbarHeight,
			client.left + sideColumnLeft(client.Width()), client.bottom);
		drawFrame(dc, imageArea);

		// 렌더링 시간이 확정된 후 파이프라인 표시 갱신
		updateTimingDisplay();
	}
}

// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서
//  이 함수를 호출합니다.
HCURSOR CjetsonUIDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}
