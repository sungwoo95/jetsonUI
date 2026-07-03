
// jetsonUIDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "jetsonUI.h"
#include "jetsonUIDlg.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	// 젯슨 tcp_server.cpp::sendImage 프로토콜과 동일 (little-endian)
	// uint32 width, uint32 height, uint64 frameNumber, uint32 payloadBytes
	constexpr int kHeaderBytes = 20;
	constexpr UINT32 kMaxPayloadBytes = 100 * 1024 * 1024;

	constexpr UINT WM_APP_FRAME_RECEIVED = WM_APP + 1;
	constexpr UINT WM_APP_STATUS = WM_APP + 2;

	// 상단 컨트롤 영역 높이 (픽셀)
	constexpr int kToolbarHeight = 40;

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

	m_statusText.Create(_T("Ready."), WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
		CRect(330, 8, 1200, 33), this, IDC_STATUS_TEXT);
	m_statusText.SetFont(font);

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

	CStringA hostA(hostText);
	m_recvThread = std::thread(&CjetsonUIDlg::receiveLoop, this, hostA, port);
}

void CjetsonUIDlg::postStatus(const CString& text)
{
	// UI 스레드에서 해제하는 힙 문자열로 전달
	PostMessage(WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(new CString(text)));
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
		SOCKET s = m_socket.exchange(INVALID_SOCKET);
		if (s != INVALID_SOCKET) closesocket(s);
		PostMessage(WM_APP_STATUS, 1, 0);
		return;
	}

	postStatus(_T("Connected. Waiting for image ..."));

	for (;;)
	{
		BYTE header[kHeaderBytes];
		if (!recvAll(sock, header, kHeaderBytes))
		{
			break;
		}

		UINT32 width, height, payloadBytes;
		UINT64 frameNumber;
		memcpy(&width, header + 0, 4);
		memcpy(&height, header + 4, 4);
		memcpy(&frameNumber, header + 8, 8);
		memcpy(&payloadBytes, header + 16, 4);

		if (width == 0 || height == 0 || payloadBytes != width * height || payloadBytes > kMaxPayloadBytes)
		{
			msg.Format(_T("Invalid header (w=%u h=%u payload=%u). Disconnecting."), width, height, payloadBytes);
			postStatus(msg);
			break;
		}

		// DIB 규격에 맞게 행을 4바이트 정렬하여 수신 버퍼에 복사
		std::vector<BYTE> raw(payloadBytes);
		if (!recvAll(sock, raw.data(), static_cast<int>(payloadBytes)))
		{
			postStatus(_T("Connection lost while receiving pixel data."));
			break;
		}

		auto* frame = new ReceivedFrame();
		frame->width = width;
		frame->height = height;
		frame->frameNumber = frameNumber;
		frame->strideBytes = (width + 3) & ~3u;
		frame->pixels.resize(static_cast<size_t>(frame->strideBytes) * height);
		for (UINT32 y = 0; y < height; ++y)
		{
			memcpy(frame->pixels.data() + static_cast<size_t>(y) * frame->strideBytes,
				raw.data() + static_cast<size_t>(y) * width, width);
		}

		PostMessage(WM_APP_FRAME_RECEIVED, 0, reinterpret_cast<LPARAM>(frame));
	}

	SOCKET s = m_socket.exchange(INVALID_SOCKET);
	if (s != INVALID_SOCKET)
	{
		closesocket(s);
		postStatus(_T("Disconnected."));
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

	CString status;
	status.Format(_T("Frame #%llu received: %u x %u (%u bytes)"),
		m_frame.frameNumber, m_frame.width, m_frame.height, m_frame.width * m_frame.height);
	m_statusText.SetWindowText(status);

	Invalidate(FALSE);
	return 0;
}

LRESULT CjetsonUIDlg::OnStatusMessage(WPARAM wParam, LPARAM lParam)
{
	CString* text = reinterpret_cast<CString*>(lParam);
	if (text != nullptr)
	{
		m_statusText.SetWindowText(*text);
		delete text;
	}
	if (wParam == 1)
	{
		m_connectButton.EnableWindow(TRUE);
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
	double scale = min(
		static_cast<double>(area.Width()) / m_frame.width,
		static_cast<double>(area.Height()) / m_frame.height);
	int destW = max(1, static_cast<int>(m_frame.width * scale));
	int destH = max(1, static_cast<int>(m_frame.height * scale));
	int destX = area.left + (area.Width() - destW) / 2;
	int destY = area.top + (area.Height() - destH) / 2;

	dc.FillSolidRect(area, RGB(30, 30, 30));
	SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
	SetBrushOrgEx(dc.GetSafeHdc(), 0, 0, nullptr);
	StretchDIBits(dc.GetSafeHdc(),
		destX, destY, destW, destH,
		0, 0, m_frame.width, m_frame.height,
		m_frame.pixels.data(),
		reinterpret_cast<const BITMAPINFO*>(&bmi),
		DIB_RGB_COLORS, SRCCOPY);
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
		CRect imageArea(client.left, client.top + kToolbarHeight, client.right, client.bottom);
		drawFrame(dc, imageArea);
	}
}

// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서
//  이 함수를 호출합니다.
HCURSOR CjetsonUIDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}
