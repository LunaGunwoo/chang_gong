# DRV8833 A채널 모터 ping 테스트

ESP32에 ping 한 번을 보내면 **정회전 PWM 30%로 1초 → 60%로 1초 → 출력 차단**을 실행합니다.
전원을 켜면 정지한 상태로 기다리며, 테스트 도중 받은 ping은 예약하지 않고 무시합니다.
PWM 비율은 실제 회전수 비율을 의미하지 않습니다. 최종 정지는 브레이크가 아닌 자유 감속입니다.

## 배선

프로젝트 루트의 `아틀란티스_보트_점퍼선배선도.svg`를 기준으로 합니다.
실물의 핀 글씨를 확인하고, 전원을 끈 상태에서 연결합니다.

| 도면 전선 | 연결 |
| --- | --- |
| W10 | ESP32 GPIO 25 → DRV8833 AIN1 |
| W11 | ESP32 GPIO 26 → DRV8833 AIN2 |
| W12 | ESP32 GPIO 33 → DRV8833 STBY |
| W01 | 전원 모듈 5V 출력 1 → DRV8833 VM |
| W02 | 전원 모듈 GND 출력 1 → DRV8833 GND |
| W05·W06 | 전원 모듈 GND와 ESP32 GND를 빵판 GND 레일로 공통 연결 |
| W14·W15 | DRV8833 AO1·AO2 → 5V DC 모터 두 선 |
| W16·W17 | 미사용 B채널 BIN1·BIN2 → GND |

BO1·BO2·NC는 비워 둡니다. 모터 전원과 접지는 전원 모듈로 직접 연결하고,
빵판에는 5V를 공급하지 않습니다. ESP32 5V 핀도 비워 둡니다.
ESP32는 도면의 C01 USB 케이블로 전원을 받습니다.
드라이버 STBY는 이 테스트에서 DRV8833의 nSLEEP 기능(LOW 비활성, HIGH 활성)으로 취급합니다.

**프로펠러를 제거한 상태**에서 테스트합니다. 정회전 방향은 모터 선 연결에 따라 달라집니다.
전원 모듈의 여러 출력은 총전류 한도를 공유합니다.
이 펌웨어는 DC 모터만 제어하며, 서보 GPIO 27과 IMU는 초기화하지 않습니다.

## 컴파일·업로드

프로젝트 루트 PowerShell에서 실행합니다. Wi-Fi 설정은 `wifi_setup/secrets.h`를 재사용합니다.
ESP32 Arduino 코어 3.3.12와 기본 LEDC 기능을 사용하며 추가 라이브러리는 필요 없습니다.

```powershell
.\.tools\arduino-cli.exe compile --fqbn esp32:esp32:esp32 --build-path build/motor_ping motor_ping
```

업로드 전 전원을 끄고 C01의 USB-A 플러그를 전원 모듈에서 PC로 옮깁니다.
ESP32 5V 핀은 비워 둔 채 W05·W06의 공통 GND를 유지합니다.
다른 시리얼 모니터를 닫은 후 업로드합니다(COM6은 실제 연결 포트로 변경 가능).

```powershell
.\.tools\arduino-cli.exe upload --fqbn esp32:esp32:esp32 -p COM6 --input-dir build/motor_ping motor_ping
.\monitor.ps1 -Reset -Seconds 20
```

업로드하면 실행 중이던 서보 테스트가 이 모터 테스트 펌웨어로 교체됩니다.
`READY IP=...`에 출력된 현재 IP를 확인합니다. DHCP IP는 달라질 수 있습니다.
외부 전원으로 옮길 때도 전원을 끈 상태에서 C01 플러그를 옮기고 다시 켭니다.

## 실행

PC와 ESP32가 같은 LAN에 연결된 상태에서 실행합니다. 다음 IP는 이전에 사용한 주소이므로
현재 `READY` 로그의 IP가 다르면 바꿔 입력합니다. **이 ping 자체가 모터 실행 명령입니다.**

```powershell
ping -n 1 10.251.46.117
```

정상 로그는 아래 순서입니다. 테스트 완료 후 STBY=0으로 출력이 차단됩니다.

```text
MOTOR START #1
MOTOR LOW duty=77/255 (30%)
MOTOR HIGH duty=153/255 (60%)
MOTOR DONE #1 STBY=0
```

다시 한 번 보내면 같은 동작을 실행합니다. 연속 ping에서는 약 2초의 실행 중 들어온 요청은 무시합니다.
Wi-Fi 연결 해제를 감지하면 진행 중인 테스트를 중단하고 출력을 끕니다.
연결이 복구되어도 자동으로 재실행하지 않으며 새 ping을 기다립니다.
PWM 또는 초기화 오류가 발생하면 `ERROR`를 출력하고 STBY를 LOW로 유지합니다.

## 검증

코드 작성 시 컴파일과 테스트 스크립트 문법을 확인합니다. 보드 업로드와 실기 검사는 별도로 진행합니다.
프로펠러를 제거하고 모터를 고정한 후 시리얼 모니터를 닫고 실행합니다.
이 스크립트는 실제 모터 테스트를 세 번 실행하며, 시리얼 상태와 ping 응답을 검사합니다.

```powershell
.\motor_ping\test.ps1 -Address 10.251.46.117 -Port COM6
```

- 부팅 후에는 모터가 돌지 않아야 합니다.
- 단일 ping에 LOW → HIGH → DONE이 한 번씩 출력되어야 합니다.
- 실행 중 추가 ping에 응답하되 테스트는 한 번만 실행하고, 끝난 뒤 추가 실행이 없어야 합니다.
- 다음 단일 ping에는 새 테스트가 한 번 실행되어야 하며 재부팅이 없어야 합니다.
- 실행 중 Wi-Fi를 끊으면 `MOTOR ABORT`와 함께 출력이 차단되고 복구 후 정지 상태로 대기해야 합니다.
- 모터가 실제 회전하고 출력 증가 시 속도가 변하는지는 눈으로 확인합니다. 로그만으로 회전은 검증할 수 없습니다.

모터가 돌지 않으면 전원을 끄고 VM 5V, 공통 GND, STBY와 AIN1·AIN2, AO1·AO2를 확인합니다.
낮은 PWM에서는 모터가 출발하지 않을 수 있습니다. 계속 멈춘 상태로 구동하지 말고 부하와 전원을 확인합니다.
`motor_ping.ino` 상단의 `LOW_PERCENT`, `HIGH_PERCENT`, `RUN_MS`로 출력과 시간을 조정할 수 있습니다.

제어 기준: [TI DRV8833 데이터시트](https://www.ti.com/lit/ds/symlink/drv8833.pdf).
