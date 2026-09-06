# Lab 01 - Hello Dual-Core (ESP32-S3 AMP 스캐폴딩, IPC 없음)

## 1. 이 랩의 목적

ESP32-S3 Zephyr Dual-Core IPC 시리즈의 첫 번째 랩입니다. 이 랩에는 **실제 IPC(데이터 주고받기)가 전혀 없습니다** - 목적은 딱 하나, ESP32-S3의 두 코어(PROCPU/APPCPU) 위에서 **완전히 독립된 두 개의 Zephyr 이미지**를 동시에 빌드/플래시하고, 둘 다 정상적으로 살아 있는지 확인하는 방법을 익히는 것입니다.

- `west build --sysbuild` 한 번으로 두 이미지를 함께 빌드하는 흐름 익히기
- **PROCPU(core0)**: UART 콘솔이 되므로, LED 점멸 + `LOG_INF()` 로그 두 가지로 생존 확인
- **APPCPU(core1)**: **UART 콘솔이 아직 지원되지 않음** - LED 점멸만으로 생존을 확인해야 하는 이 플랫폼의 실무적 제약을 직접 체감
- 두 LED가 서로 다른 주기로 깜빡이도록 해서, 두 코어가 서로 아무 통신 없이 각자 독립적으로 돌고 있다는 것을 눈으로 확인 (Lab 02부터 이 둘이 실제로 통신을 시작합니다)

## 2. 왜 APPCPU는 로그가 안 나오나?

ESP32-S3용 Zephyr 보드 포트는 **AMP(Asymmetric Multiprocessing)만 지원**합니다 - 두 코어가 하나의 OS 이미지를 공유하는 SMP가 아니라, `esp32s3_devkitc/esp32s3/procpu`와 `esp32s3_devkitc/esp32s3/appcpu`라는 **완전히 별도의 빌드 타겟/이미지**로 동작합니다. 그리고 Zephyr 공식 문서에 명시된 대로, **PROCPU만 UART 콘솔(printk/logging)을 지원**하고 APPCPU 쪽은 아직 지원되지 않습니다.

그래서 이 랩(그리고 이후 랩들)에서 APPCPU 쪽 동작을 확인하는 방법은 두 가지뿐입니다:
1. GPIO(LED 등)로 직접 신호를 내보내서 눈으로 확인
2. IPC로 PROCPU에 데이터를 보내고, PROCPU가 대신 로그로 출력 (Lab 02부터)

이 랩은 그중 1번만 사용합니다.

## 3. 중요: IPM을 켜야 APPCPU가 켜집니다

이 랩은 코어 사이에 아무 데이터도 주고받지 않는데도, `prj.conf`와 오버레이에 `CONFIG_IPM` / `CONFIG_ESP32_SOFT_IPM` / `&ipm0`가 **양쪽 이미지 모두에 들어있습니다.** 실기에서 확인된 이 보드의 특성 때문입니다:

> **PROCPU와 APPCPU 양쪽 이미지에서 `&ipm0`(ESP32 소프트 IPM 메일박스)가 활성화돼 있지 않으면, APPCPU가 reset 상태에서 아예 풀리지 않습니다.** GPIO도, 다른 어떤 코드도 실행되지 않습니다 - 핀 번호나 배선과는 무관합니다.

즉 이 랩처럼 IPC를 전혀 안 쓰는 순수 "hello world" 성격의 랩이라도, **APPCPU를 깨우는 용도로만** IPM을 켜둬야 합니다. 이 시리즈의 앞으로 모든 랩에서 이 설정은 기본으로 유지됩니다. (이 사실이 어떻게 밝혀졌는지는 `01_HELLO_DUALCORE_LAB_TROUBLESHOOTING_KR.md`에 정리했습니다.)

## 4. 준비물 / 배선

| 부품 | 수량 |
| --- | --- |
| ESP32-S3-DevKitC-1 | 1 |
| LED | 2개 (색을 다르게 하면 구분하기 더 편함) |
| 저항 (220~330Ω) | 2개 |
| 브레드보드 + 점퍼선 | 약간 |

| 신호 | GPIO | 용도 |
| --- | --- | --- |
| PROCPU 하트비트 LED | **GPIO2** | LED 애노드(+) - 저항 - GPIO2, 캐소드(-) - GND |
| APPCPU 하트비트 LED | **GPIO42** | LED 애노드(+) - 저항 - GPIO42, 캐소드(-) - GND |

- GPIO2/GPIO42 모두 스트래핑 핀(0/3/45/46), USB-JTAG 핀(19/20), SPI Flash/PSRAM 영역(26~37)과 겹치지 않는 자유 GPIO로 골랐습니다. 이 IPC 시리즈에서 앞으로도 "PROCPU 하트비트=GPIO2 / APPCPU 하트비트=GPIO42" 관례로 재사용할 예정입니다.
- GPIO42는 `&gpio1`(핀 32 이상) 컨트롤러 소속으로, 오버레이에서는 로컬 인덱스 42-32=10으로 씁니다. ESP32-S3의 5핀 외부 JTAG(MTMS 등)과 겹치는 핀이지만, DevKitC-1은 USB-JTAG(GPIO19/20)을 쓰므로 이 랩에서 문제없이 일반 GPIO로 쓸 수 있습니다.

## 5. 디렉터리 구조

```
01_HELLO_DUALCORE_LAB/
├── doc/
│   ├── 01_HELLO_DUALCORE_LAB_KR.md              (이 문서)
│   ├── 01_HELLO_DUALCORE_LAB_EN.md
│   ├── 01_HELLO_DUALCORE_LAB_TROUBLESHOOTING_KR.md
│   └── 01_HELLO_DUALCORE_LAB_TROUBLESHOOTING_EN.md
└── lab/
    ├── CMakeLists.txt        # core0 (procpu) 앱
    ├── prj.conf              # core0 설정
    ├── sample.yaml
    ├── sysbuild.cmake        # sysbuild가 core1(remote/)도 같이 빌드하도록 지정
    ├── sysbuild.conf         # MCUboot 비활성화
    ├── boards/
    │   └── esp32s3_devkitc_esp32s3_procpu.overlay
    ├── src/
    │   └── main.c            # core0 소스
    └── remote/                # core1 (appcpu) 앱 - 완전히 독립된 서브 애플리케이션
        ├── CMakeLists.txt
        ├── prj.conf
        ├── boards/
        │   └── esp32s3_devkitc_esp32s3_appcpu.overlay
        └── src/
            └── main.c        # core1 소스
```

AMP 구조라서 `remote/` 아래가 사실상 **완전히 별개의 Zephyr 애플리케이션**입니다 (자기 CMakeLists.txt/prj.conf/overlay/src를 따로 가짐). `sysbuild.cmake`가 이 둘을 하나의 `west build` 명령으로 묶어주는 역할을 합니다.

## 6. 빌드 & 플래시

```bash
cd 01_HELLO_DUALCORE_LAB/lab

# 두 이미지(procpu + appcpu) 동시 빌드
west build -p always --sysbuild -b esp32s3_devkitc/esp32s3/procpu .

# 플래시 (두 이미지 모두 한 번에 올라감)
west flash
```

- `--sysbuild`를 빼먹으면 procpu 이미지만 빌드되고 `remote/`는 무시되니 주의하세요.
- Espressif 보드는 `--sysbuild` 사용 시 기본적으로 MCUboot도 같이 빌드하려고 하는데, 이 랩은 OTA와 무관한 단순 2-이미지 AMP 빌드라서 `sysbuild.conf`에서 `SB_CONFIG_BOOTLOADER_NONE=y`로 꺼뒀습니다.
- PROCPU 쪽 로그를 보려면 평소처럼 시리얼 터미널(115200bps)을 여세요. APPCPU 쪽은 위에서 설명한 대로 콘솔 자체가 없습니다.

## 7. 정상 동작 확인 체크리스트

- [ ] GPIO2에 연결한 LED가 약 0.5초 간격(500ms ON/500ms OFF)으로 깜빡인다 (PROCPU)
- [ ] GPIO42에 연결한 LED가 약 0.2초 간격(200ms ON/200ms OFF)으로 깜빡인다 (APPCPU) - PROCPU LED보다 눈에 띄게 빠름
- [ ] 시리얼 터미널에 `01_HELLO_DUALCORE_LAB (core0/procpu) starting` 로그가 뜬 뒤, `procpu heartbeat - LED ON/OFF`가 0.5초 간격으로 계속 찍힌다
- [ ] 두 LED가 서로 동기화되지 않고 각자 다른 리듬으로 움직인다 - 이게 "두 코어가 서로 통신 없이 완전히 독립적으로 실행 중"이라는 걸 보여주는 이 랩의 핵심 확인 포인트

두 LED가 모두 정상적으로 깜빡이면 이 랩은 성공입니다. 다음 Lab 02부터 이 두 코어가 MBOX를 통해 실제로 신호를 주고받기 시작합니다.
