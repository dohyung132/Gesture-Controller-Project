각속도 기반 윈도우 운영체제 컨트롤러
Gyro & Illumination Sensor Based Windows OS Gesture Controller

자이로 센서와 조도 센서를 활용하여 물리적 접촉 없이 손동작만으로 Windows 환경(PPT, YouTube 등)을 제어하는 비접촉 인터페이스 시스템입니다.


주요 기능 (Key Features)

가상 클러치 (Virtual Clutch): 조도 센서를 활용하여 사용자가 의도적으로 센서를 가렸을 때만 제스처를 인식하도록 설계하여 오작동을 방지했습니다.

지능형 앱 감지: Windows API(GetForegroundWindow)를 통해 현재 활성화된 창을 실시간으로 분석, 앱에 맞는 최적의 단축키를 자동으로 매핑합니다.

실시간 데이터 보정: 시스템 기동 시 초기 데이터를 수집하여 센서 고유의 오차를 소프트웨어적으로 자동 보정(Zero-point Calibration)합니다.

통합 모니터링: 모든 제스처 이력과 성공 여부를 MySQL DB에 기록하여 시스템 신뢰도를 관리합니다.


시스템 구조 (System Architecture)

본 프로젝트는 하드웨어, 리눅스 서버, 윈도우 클라이언트의 3계층 구조로 이루어져 있습니다.

Hardware (Arduino): MPU6050(자이로) 및 조도 센서 데이터 수집 (10ms 주기)

Server (Linux): 수신된 Raw 데이터를 분석하여 제스처 판정 및 DB 로그 적재

Client (Windows): TCP/IP 소켓 통신을 통해 수신된 명령을 바탕으로 OS 이벤트(키보드 입력) 실행


기술 스택 (Tech Stack)
구분,내용
Language,"C, C++"
Hardware,"Arduino Uno, MPU6050 (Gyro), Photoresistor (CdS)"
OS,"Linux (Ubuntu), Windows 10/11"
Database,MySQL
Protocol,"Serial Communication, TCP/IP Socket"

문제 해결 (Troubleshooting)

리바운드 오작동 방지: 제스처 완료 후 손을 원위치로 돌릴 때 발생하는 중복 입력을 막기 위해 1초간의 Throttling(쿨다운 타임) 로직을 구현했습니다.

통신 신뢰성 확보: 데이터 유실 방지를 위해 시작(STX)과 끝(ETX) 문자를 포함한 고유 통신 프로토콜을 설계하여 데이터 무결성을 확보했습니다.

이도형 (Dohyung Lee)
