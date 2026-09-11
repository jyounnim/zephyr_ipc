# Lab 02 - MBOX 도어벨 (신호 전용, 양방향)

## 1. 이 랩의 목적

이 시리즈에서 두 코어가 처음으로 실제 통신을 시작하는 랩입니다. Zephyr의 **MBOX API**(`mbox.h`)를 데이터 없이 순수 "초인종(도어벨)" 신호로만 사용합니다 - `mbox_send_dt()`에 실제 데이터 대신 `NULL`을 넘겨서, "눌렀다"는 사실 자체만 상대 코어에 전달합니다.

- **PROCPU → APPCPU**: PROCPU에 연결된 보드 내장 BOOT 버튼을 누르면 APPCPU의 도어벨이 울립니다 (LED로 확인)
- **APPCPU → PROCPU**: APPCPU가 3초마다 자체적으로 PROCPU의 도어벨을 울립니다 (로그로 확인)
- 두 코어 모두 Lab 01의 하트비트 LED 점멸도 그대로 유지합니다

## 2. `&ipm0` 없이 `&mbox0`만으로 충분합니다

Lab 01에서 확인한 사실: **PROCPU/APPCPU 양쪽에서 `&ipm0`가 켜져 있지 않으면 APPCPU가 reset에서 풀리지 않는다.**

그런데 ESP32-S3의 devicetree를 보면 `&ipm0`와 `&mbox0`는 완전히 다른 두 하드웨어가 아니라, **정확히 같은 레지스터 블록(인접 주소)과 같은 공유메모리, 같은 인터럽트 소스(FROM_CPU_INTR0/1)를 가리키는, 같은 물리 하드웨어에 대한 두 개의 서로 다른 Zephyr 드라이버 바인딩**입니다. 즉 하나의 "코어 간 인터럽트/메일박스" 하드웨어를, 구식 API(`ipm.h`/`&ipm0`)로 볼 수도 있고 신식 API(`mbox.h`/`&mbox0`)로 볼 수도 있는 구조입니다.

실기로 확인한 결과: **`&mbox0`만 활성화해도(`&ipm0`는 꺼둔 채로) APPCPU가 정상적으로 reset에서 풀리고 동작합니다.** 즉 APPCPU를 깨우는 데 필요한 건 `ipm0`라는 특정 드라이버가 아니라, 이 공유 하드웨어 자체가 (어느 바인딩을 통해서든) 활성화되는 것입니다.

이 발견에 따라 이 시리즈의 정책을 다음과 같이 정리합니다: **Dual-core/IPC를 쓰는 랩은 `ipm0`와 `mbox0` 중, 그 랩이 실제로 가르치는 API에 해당하는 쪽 하나만 켜면 됩니다.** 둘을 동시에 켜는 것은 같은 인터럽트 소스를 두 드라이버가 동시에 요구하게 되므로 피합니다. 이 랩은 MBOX API를 다루므로 `&mbox0`만 켭니다.

## 3. 준비물 / 배선

| 부품 | 수량 |
| --- | --- |
| ESP32-S3-DevKitC-1 | 1 |
| LED | 2개 (Lab 01과 동일하게 재사용) |
| 저항 (220~330Ω) | 2개 |
| 브레드보드 + 점퍼선 | 약간 |

| 신호 | GPIO | 용도 |
| --- | --- | --- |
| PROCPU 하트비트 LED | **GPIO2** | Lab 01과 동일 |
| APPCPU 하트비트 LED | **GPIO42** | Lab 01과 동일 |
| PROCPU 트리거 버튼 | **GPIO0 (BOOT 버튼)** | **추가 배선 불필요** - DevKitC-1 보드에 이미 내장된 BOOT 버튼을 그대로 사용 (Zephyr 보드 파일이 `sw0` alias로 이미 노출해 둠) |

이번 랩은 Lab 01의 LED 배선을 그대로 재사용하고, 버튼은 보드에 이미 있는 것을 쓰므로 **추가 배선이 전혀 없습니다.**

## 4. 디렉터리 구조

```
02_MBOX_DOORBELL_LAB/
├── doc/
│   ├── 02_MBOX_DOORBELL_LAB_KR.md   (이 문서)
│   └── 02_MBOX_DOORBELL_LAB_EN.md
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

## 5. 빌드 & 플래시

```bash
cd 02_MBOX_DOORBELL_LAB/lab

# 두 이미지(procpu + appcpu) 동시 빌드
west build -p always --sysbuild -b esp32s3_devkitc/esp32s3/procpu .

# 플래시 (두 이미지 모두 한 번에 올라감)
west flash
```

- `--sysbuild`를 빼먹으면 procpu 이미지만 빌드되고 `remote/`는 무시되니 주의하세요.
- PROCPU 쪽 로그를 보려면 평소처럼 시리얼 터미널(115200bps)을 여세요. APPCPU 쪽은 콘솔 자체가 없습니다 (Lab 01 참고).

## 6. 코드 동작 설명

**PROCPU (`src/main.c`)**
- GPIO2 LED를 500ms 간격으로 계속 점멸 (Lab 01과 동일한 하트비트)
- BOOT 버튼(GPIO0)을 20ms마다 폴링하고, 눌리는 순간(edge)을 감지하면 `mbox_send_dt(&tx_channel, NULL)`로 APPCPU에 도어벨 신호만 전송 (데이터 없음)
- `mbox_register_callback_dt()`로 APPCPU에서 오는 도어벨을 항상 수신 대기 - 수신될 때마다 카운트를 증가시키고 로그 출력

**APPCPU (`remote/src/main.c`)**
- GPIO42 LED를 평소 200ms 간격으로 점멸 (Lab 01과 동일한 하트비트)
- PROCPU에서 오는 도어벨을 수신하면, 평소 점멸을 잠시 멈추고 80ms 간격의 빠른 트리플 플래시(6번 토글)로 반응한 뒤 평소 점멸로 복귀
- 이와 별개로 3초마다 자체적으로 `mbox_send_dt(&tx_channel, NULL)`로 PROCPU에 도어벨 신호를 전송 (콘솔이 없으므로 이 동작은 PROCPU 쪽 로그로만 확인 가능)

**devicetree (`boards/*.overlay`)**
- 두 이미지 모두 `&mbox0 { status = "okay"; };`로 MBOX 하드웨어를 활성화
- 각 이미지에 `mbox-consumer`라는 노드를 추가해 `mboxes`/`mbox-names` 프로퍼티로 "tx"/"rx" 채널을 지정 (Zephyr 공식 MBOX 샘플과 동일한 패턴, `compatible = "vnd,mbox-consumer"`는 Zephyr 자체에 이미 내장된 테스트용 바인딩)
- ESP32의 MBOX 하드웨어는 방향당 채널이 1개뿐이라 tx/rx 모두 `&mbox0 0`을 가리킵니다 - 실제 방향은 채널 번호가 아니라 "어느 코어에서 실행 중인 이미지인가"로 결정됩니다

## 7. 정상 동작 확인 체크리스트

- [ ] GPIO2 LED가 0.5초 간격으로 계속 깜빡인다 (PROCPU 하트비트)
- [ ] GPIO42 LED가 0.2초 간격으로 계속 깜빡인다 (APPCPU 하트비트)
- [ ] 시리얼 터미널에 `02_MBOX_DOORBELL_LAB (core0/procpu) starting` 로그가 뜬다
- [ ] **BOOT 버튼**을 누르면 시리얼에 `button pressed - ringing appcpu's doorbell` 로그가 뜨고, 곧이어 **APPCPU LED가 빠르게 3번 깜빡인 뒤(트리플 플래시)** 다시 평소 속도로 돌아온다
- [ ] 버튼을 누르지 않아도 **3초에 한 번씩** 시리얼에 `doorbell from appcpu received (count=N)` 로그가 계속 올라온다 (N이 계속 증가)

이 랩이 정상 동작하면, 두 코어가 서로 신호(초인종)만으로 통신하는 가장 기본적인 IPC 패턴을 완성한 것입니다. 다음 Lab 03부터는 신호뿐 아니라 실제 데이터를 공유메모리를 통해 주고받기 시작합니다.
