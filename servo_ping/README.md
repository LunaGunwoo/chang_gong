# MG996R ping 동작 테스트 (GPIO 27)

PC에서 ESP32에 실제 IPv4 ping을 보내면 서보가 중앙 → 한쪽 → 중앙으로 한 번 움직입니다.
초기 설정은 약 90° → 45° → 90°를 의도한 1500µs → 1250µs → 1500µs입니다.
각 위치에서 600ms씩 기다리므로 한 동작은 약 1.2초입니다.
동작 중 받은 추가 ping은 무시하고, ping 응답은 계속 보냅니다.

## 배선

전원을 끈 상태에서 연결합니다. 보드의 **GPIO 27 / IO27** 표시를 사용합니다(커넥터의 27번째 핀이라는 뜻이 아닙니다).

| 연결 대상 | 연결 위치 |
| --- | --- |
| MG996R 신호선 (보통 주황/노랑) | ESP32 GPIO 27 |
| MG996R 전원선 (보통 빨강) | 외부 5~6V 전원 + |
| MG996R GND (보통 갈색/검정) | 외부 전원 − |
| ESP32 GND | 외부 전원 −와 공통 연결 |
| ESP32 전원 | 기존 USB |

서보 전원을 ESP32의 3.3V/GPIO에서 공급하지 않습니다. 외부 전원 +는 서보에 연결하고,
ESP32에는 GND만 공통 연결합니다. 전원은 모터가 움직일 때도 전압을 유지할 수 있어야 합니다.
첫 테스트는 혼에 부하를 걸지 않고 움직일 공간을 확보한 상태로 합니다.
일반 위치 제어형 MG996R 기준이며 연속 회전형은 이 코드로 각도를 지정할 수 없습니다.

## 컴파일 및 업로드

프로젝트 루트(`esp32-wifi-setup`)에서 실행합니다.
기존 `wifi_setup/secrets.h`를 상대 경로로 재사용하므로 두 디렉터리를 함께 유지합니다.
추가 라이브러리는 필요 없으며 ESP32 Arduino 코어 3.3.12를 사용합니다.

```powershell
.\.tools\arduino-cli.exe compile --fqbn esp32:esp32:esp32 --build-path build/servo_ping servo_ping
.\.tools\arduino-cli.exe upload --fqbn esp32:esp32:esp32 -p COM6 --input-dir build/servo_ping servo_ping
.\monitor.ps1 -Reset -Seconds 30
```

업로드 전 COM6을 사용 중인 시리얼 모니터를 닫습니다. 다른 보드를 연결했다면 포트 값을 바꿉니다.
업로드하면 보드에서 실행 중인 Wi-Fi 테스트 펌웨어는 이 테스트 펌웨어로 교체됩니다.
부팅 시 중앙으로 이동하며, `READY IP=...`가 출력되면 ping을 보낼 수 있습니다.
DHCP 주소는 바뀔 수 있으므로 로그에 나온 현재 IP를 사용합니다.

## 동작 확인

PC와 ESP32를 같은 LAN에 연결합니다. 모니터를 한 PowerShell 창에서 실행합니다.

```powershell
.\monitor.ps1 -Seconds 60
```

다른 창에서 `실제IP`를 현재 ESP32 IP로 바꿔 실행합니다.

```powershell
ping -4 -n 1 실제IP
```

정상 결과는 ping 응답 1회와 서보 왕복 1회입니다. 시리얼에는 다음 순서로 출력됩니다.

```text
SERVO START #1 pulse=1250 us
SERVO RETURN
SERVO DONE #1
```

연속 전송도 확인합니다.

```powershell
ping -4 -n 10 실제IP
```

Windows 기본 간격은 동작 시간보다 짧으므로 모든 ping이 각각 왕복을 만들지는 않습니다.
전송을 멈추면 진행 중인 왕복만 마무리하고 중앙에서 멈춰야 합니다. PWM은 중앙 위치를 유지합니다.
정지 후 단일 ping을 다시 보내면 새 왕복을 시작해야 합니다.

단일 ping과 동작 중 중복 요청 처리를 자동 확인하려면 모니터를 닫고 실행합니다.
이 명령은 실제 모터를 움직이며, 결과는 시리얼 로그와 ping 응답을 기준으로 판정합니다.

```powershell
.\servo_ping\test.ps1 -Address 실제IP -Port COM6
```

추가 확인 항목:

- 부팅 후 ping 없이 자동 왕복이 시작되지 않는지 확인합니다(초기 중앙 이동은 정상).
- Wi-Fi를 끊었다가 복구하면 다시 `READY IP=...`가 나오고 새 IP로 동작하는지 확인합니다.
- ping 응답만 있고 모터가 안 움직이면 `SERVO START` 로그, 공통 GND, GPIO 번호, 외부 전원을 확인합니다.
- 재부팅이나 떨림이 반복되면 외부 전원 용량과 배선 접촉 상태를 확인합니다.

## 위치 보정

`servo_ping.ino` 상단의 `CENTER_US`, `AWAY_US`, `HOLD_MS`로 위치와 대기 시간을 조정합니다.
실제 각도는 개체마다 다르며 펄스 폭만으로 정확한 45°/90°를 보장하지 않습니다.
처음에는 기본값에서 작은 폭으로 조정하고 기계적 끝까지 밀어붙이지 않습니다.
초기화나 PWM 출력이 실패하면 `ERROR`를 출력하고 PWM을 해제합니다.

구현은 ESP32의 기본 LEDC PWM과 lwIP raw ICMP 수신 콜백을 사용합니다.
콜백은 패킷 길이·종류·목적지·ICMP 체크섬을 검사하고, 패킷을 소비하지 않아 정상 ping 응답을 유지합니다.
서보 이동은 Arduino `loop()`에서 처리하므로 수신 콜백에서 모터 대기를 하지 않습니다.

참고: [ESP32 LEDC API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/ledc.html)

## 보드 검증 기록

COM6의 ESP32-D0WD-V3에 업로드하고 `test.ps1`을 실행해 통과했습니다.
ping 7회 응답과 시리얼 기준 왕복 완료 3회를 확인했으며, 동작 중 추가 요청은 예약되지 않았습니다.
최초 버전의 수신 버퍼 헤더 복원 실패로 발생하던 재부팅은 원본 버퍼를 변경하지 않도록 수정했습니다.
물리적인 서보 움직임과 실제 각도는 사용자가 눈으로 확인해야 합니다.
