# ESP32 Wi-Fi 설정

`wifi_setup/secrets.h`의 설정으로 ced에 접속합니다. 비밀번호 파일은 Git에서 제외합니다.

보드: ESP32 Dev Module (`esp32:esp32:esp32`), 시리얼: 115200bps.
전원을 켜면 자동 접속하고, 연결되면 IP와 신호 세기를 출력합니다.

PowerShell에서 프로젝트 폴더를 연 뒤:

```powershell
.\.tools\arduino-cli.exe compile --fqbn esp32:esp32:esp32 wifi_setup
.\.tools\arduino-cli.exe upload --fqbn esp32:esp32:esp32 -p COM6 wifi_setup
.\.tools\arduino-cli.exe monitor -p COM6 --config baudrate=115200
```

출력의 IP로 `ping -n 20 실제IP`를 실행합니다. 라즈베리파이는 `ping -c 20 실제IP`를 사용합니다.
PC와 ESP32는 같은 LAN에 있어야 합니다. 응답이 없으면 IP 변경과 공유기의 기기 간 통신 차단 설정을 확인합니다.
USB 충전기로 전원을 공급해도 자동으로 연결됩니다. 재부팅하면 DHCP IP가 바뀔 수 있습니다.

재부팅 후 IP를 다시 확인하려면 프로젝트 폴더에서 실행합니다:

```powershell
.\monitor.ps1 -Reset -Seconds 20
```

설치된 ESP32 보드 패키지: 3.3.12.
실물 확인: ESP32-D0WD-V3 revision v3.1, COM6 업로드 및 기록 데이터 검증 성공.
ced 접속 IP: `10.251.46.117` (DHCP 주소), RSSI -26 dBm.
PC에서 ping 20회 전송, 20회 응답, 손실 0% 확인.
