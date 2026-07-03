# JetsonUI

Jetson 보드에서 캡처한 HDR 이미지와 GPU 파이프라인 성능 시간을 실시간으로 표시하는 Windows UI 애플리케이션입니다.

## 기능

- 젯슨 보드로부터 TCP를 통해 HDR 이미지 수신
- 8-bit grayscale PGM 이미지 디스플레이
- CUDA 처리 시간 메트릭 표시:
  - Host to Device Copy (h2dMs)
  - Kernel Execution (cudaMs)
  - Device to Host Copy (d2hMs)
  - Total Pipeline (totalMs)
- 프레임 정보 표시 (해상도, 프레임 번호, gain ratio)

## 환경

- Windows (Visual Studio 2019+)
- .NET Framework / MFC

## 빌드

```bash
cd jetsonUI
msbuild jetsonUI.sln /p:Configuration=Release
```

## 사용법

1. Jetson 보드에서 `hdr_main` 실행
2. 이 애플리케이션 실행
3. 연결 설정:
   - 호스트: `192.168.20.10`
   - 포트: `5000`

## 연관 프로젝트

- [HdrCameraProject](https://github.com/your-username/HdrCameraProject) - Jetson 보드의 HDR 캡처/처리 엔진
