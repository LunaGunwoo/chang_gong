# USB 직접 모터 제어

Wi-Fi 연결 없이 USB 시리얼 명령으로 DRV8833 A채널을 제어합니다.
배선은 기존 도면과 같습니다: GPIO 25 → AIN1, GPIO 26 → AIN2, GPIO 33 → STBY.
VM에는 외부 5V를 공급하고 전원 모듈·드라이버·ESP32 GND를 공통 연결합니다.
모터는 AO1·AO2에 연결하며 프로펠러는 제거한 상태로 시험합니다.

부팅하면 정지 상태로 기다립니다. USB가 연결된 상태에서 프로젝트 루트에서 실행합니다.
다른 시리얼 모니터는 닫아 둡니다.

```powershell
.\motor_serial\run.ps1
```

30% 출력으로 1초, 60% 출력으로 1초 회전한 뒤 STBY를 LOW로 내려 자유 감속합니다.
실행 명령은 한 번만 보내며 단계별 로그와 정지 완료를 검사합니다.
명령 로그와 실제 회전·속도 변화는 별개이므로 움직임은 눈으로 확인합니다.

시리얼 모니터(115200bps)에서는 `r`을 입력하면 한 번 실행하고 `s`를 입력하면 즉시 출력을 차단합니다.
실행 중 추가 `r`은 무시합니다. 모니터를 닫은 상태에서는 다음 명령으로 정지시킬 수도 있습니다.

```powershell
.\motor_serial\run.ps1 -Stop
```

PC 연결이 끊겨도 진행 중인 2초 동작은 ESP32에서 마무리하고 정지합니다.
전원만 연결하면 정지 상태로 대기하며 자동 반복하지 않습니다.
출력 비율과 시간은 스케치 상단 상수로 조정합니다.

컴파일·업로드:

```powershell
.\.tools\arduino-cli.exe compile --fqbn esp32:esp32:esp32 --build-path build/motor_serial motor_serial
.\.tools\arduino-cli.exe upload --fqbn esp32:esp32:esp32 -p COM6 --input-dir build/motor_serial motor_serial
```

업로드는 기존 ping 펌웨어를 이 USB 제어 펌웨어로 교체합니다.
전원을 끈 상태에서 전원 모듈의 C01 USB 플러그를 PC로 옮기며 ESP32 5V 핀은 비워 둡니다.
자세한 전원 배선은 루트 SVG와 `motor_ping/README.md`를 따릅니다.
