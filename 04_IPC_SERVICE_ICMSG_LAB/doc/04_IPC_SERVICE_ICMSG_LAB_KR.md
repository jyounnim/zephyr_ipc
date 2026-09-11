# Lab 04 - IPC Service (icmsg backend): 표준 프레임워크로 Lab 03을 다시 만들기

## 1. 이 랩의 목적

Lab 03에서는 MBOX + 우리가 직접 만든 공유 구조체만으로 IPC를 구현했고, 그 결과 "알림 개수는 지킬 수 있어도 슬롯 하나짜리 공유 메모리로는 중간 데이터가 손실된다"는 근본적인 한계를 직접 확인했습니다.

이번 Lab 04는 Lab 03과 **똑같은 실험**(procpu가 증가하는 순번을 계속 보내고, appcpu가 그대로 돌려보냄)을 이번엔 Zephyr의 표준 `IPC Service` 프레임워크(`ipc_service.h`)의 **icmsg backend**로 다시 구현합니다. 목표는 두 가지입니다:

- `IPC Service`의 **instance/endpoint 개념**과 **콜백 기반 API**(`bound`/`received`/`error`)에 익숙해지기
- Lab 03에서 겪은 데이터 손실 문제를 **icmsg가 프레임워크 차원에서 어떻게 해결**하는지 직접 확인하기 (icmsg는 슬롯 하나가 아니라 진짜 큐잉되는 링버퍼이기 때문에, procpu가 보낸 값 중 어느 하나도 중간에 사라지지 않아야 합니다)

이번에도 버튼/배선 추가는 없습니다. LED 2개(PROCPU=GPIO2, APPCPU=GPIO42)만 그대로 사용합니다.

## 2. 공유 메모리를 어디에 둘 것인가 - `shm0` 발견

icmsg backend는 Lab 02/03의 MBOX와 달리, **자기만의 별도 공유 메모리 영역**이 필요합니다 (MBOX는 여기서도 여전히 "새 데이터 있음"을 알리는 신호용으로만 쓰이고, 실제 메시지 바이트는 이 별도 영역의 링버퍼를 통해 오갑니다). Lab 03이 재사용한 `ipmmem0`는 1KB밖에 안 되고, 이미 `&ipm0`/`&mbox0` 드라이버가 자기 용도로 예약해둔 것이라 새 용도로 쓰기엔 조금 애매합니다.

그런데 ESP32-S3의 SoC devicetree(`esp32s3_common.dtsi`)를 다시 살펴보니, `ipmmem0` 바로 다음에 이런 노드가 **이미 존재**했습니다:

```c
ipmmem0: memory@3fce5000 {
    compatible = "mmio-sram";
    reg = <0x3fce5000 0x400>;   /* 1KB - Lab 02/03이 사용 */
};

shm0: memory@3fce5400 {
    compatible = "mmio-sram";
    reg = <0x3fce5400 0x4000>;  /* 16KB - 지금까지 어떤 랩도 안 쓴 블록! */
};
```

`shm0`("shared memory 0")는 `ipmmem0`와 완전히 똑같은 방식으로 선언된, **16KB짜리 별도 블록**입니다. 이름부터 "범용 공유 메모리"라는 뜻이고, `ipmmem0`처럼 `&ipm0`/`&mbox0` 레지스터(`0x3fce9400`) 바로 앞에 딱 붙어있는 위치도 SoC 포팅 담당자가 "코어 간 공유용으로 따로 떼어둔 블록"이라는 의도를 강하게 뒷받침합니다. (참고로 원조 ESP32에도 `esp32_common.dtsi`에 `ipmmem0`/`shm0`가 정확히 같은 패턴으로 있는 것도 확인했습니다 - SoC 포팅 담당자가 의도적으로 넣어둔 관례로 보입니다.)

**참고로 밝혀두자면**: 현재 mainline Zephyr에서 `shm0`를 실제로 사용하는 기존 샘플/테스트는 찾지 못했습니다. 그래서 "이미 검증된 예제를 그대로 따라한 것"이 아니라, "`ipmmem0`와 동일한 선언 방식이고, 이 시리즈에서 이미 그 옆 블록(`ipmmem0`)을 실기로 검증했다"는 근거로 안전하다고 추론해서 설계한 것이며, **실제로 DevKitC-1 보드에서 이 설계 그대로 정상 동작하는 것을 확인했습니다** (9절 참고).

이 16KB를 절반씩(8KB, 8KB) 나눠서 icmsg의 tx/rx 링버퍼로 씁니다 (Nordic 공식 예제는 방향당 2KB만 쓰는 걸 확인했으니, 8KB면 상당히 여유 있는 크기입니다).

## 3. devicetree 설계

procpu 오버레이:

```c
reserved-memory {
    #address-cells = <1>;
    #size-cells = <1>;

    icmsg_p2a: memory@3fce5400 { reg = <0x3fce5400 0x2000>; }; /* procpu -> appcpu */
    icmsg_a2p: memory@3fce7400 { reg = <0x3fce7400 0x2000>; }; /* appcpu -> procpu */
};

ipc0: ipc0 {
    compatible = "zephyr,ipc-icmsg";
    dcache-alignment = <0>;
    tx-region = <&icmsg_p2a>;
    rx-region = <&icmsg_a2p>;
    mboxes = <&mbox0 0>, <&mbox0 0>;
    mbox-names = "tx", "rx";
    status = "okay";
};
```

appcpu 오버레이는 `tx-region`/`rx-region`을 정확히 반대로 지정합니다(procpu의 tx가 appcpu 입장에선 rx가 되므로). 이 외에는 동일합니다.

몇 가지 설계 근거:

- **`dcache-alignment = <0>`**: 이 값은 공식 binding 문서에 필수(required) 항목으로 나와 있어서 반드시 지정해야 합니다. `0`으로 둔 이유는 "이 메모리 영역이 캐시되지 않는다"는 뜻인데, Lab 02/03에서 바로 이 옆 블록(`ipmmem0`)을 캐시 관리(flush/invalidate) 없이 `volatile` 포인터로만 읽고 써도 실기에서 정상 동작했으므로, 같은 SRAM 영역에 속한 `shm0`도 캐시되지 않는다고 추론했습니다.
- **`mboxes = <&mbox0 0>, <&mbox0 0>;`**: Lab 02/03에서 이미 확인했듯, ESP32 MBOX 하드웨어는 방향당 채널이 1개뿐이라 tx/rx 모두 채널 0을 가리킵니다 - icmsg도 내부적으로는 우리가 Lab 02/03에서 직접 했던 것과 똑같은 `mbox_send_dt()`/`mbox_register_callback_dt()` 패턴을 쓰므로 그대로 적용됩니다.
- **`&mbox0`만 활성화, `&ipm0`는 비활성화**: 이 시리즈의 기존 정책을 그대로 따릅니다.
- icmsg는 **단일 엔드포인트 전용**입니다(`CONFIG_IPC_SERVICE_BACKEND_ICMSG`의 Kconfig 설명: "single endpoint implementation based on circular packet buffer") - 그래서 이번 랩은 인스턴스당 엔드포인트를 딱 하나만 등록합니다. 여러 엔드포인트가 필요하면 Lab 05의 rpmsg나 `icmsg_me` 같은 다른 backend가 필요합니다.

## 4. 준비물 / 배선

- ESP32-S3-DevKitC-1 보드, USB 케이블 1개
- 추가 배선 없음 (Lab 01/02/03과 동일한 LED 2개만 사용)

## 5. 디렉터리 구조

```
04_IPC_SERVICE_ICMSG_LAB/
├── doc/
│   └── 04_IPC_SERVICE_ICMSG_LAB_KR.md   (이 문서)
└── lab/
    ├── CMakeLists.txt
    ├── prj.conf
    ├── sample.yaml
    ├── sysbuild.cmake
    ├── sysbuild.conf
    ├── boards/
    │   └── esp32s3_devkitc_esp32s3_procpu.overlay
    ├── src/
    │   ├── common.h                      (메시지 구조체 정의)
    │   └── main.c                        (procpu 애플리케이션)
    └── remote/
        ├── CMakeLists.txt
        ├── prj.conf
        ├── boards/
        │   └── esp32s3_devkitc_esp32s3_appcpu.overlay
        └── src/
            ├── common.h                  (procpu 쪽과 완전히 동일한 사본)
            └── main.c                    (appcpu 애플리케이션)
```

## 6. 빌드 & 플래시

```bash
cd 04_IPC_SERVICE_ICMSG_LAB/lab

west build -p always --sysbuild -b esp32s3_devkitc/esp32s3/procpu .
west flash
```

빌드 후 시리얼 터미널(115200bps)로 procpu 로그를 확인하세요.

## 7. 코드 동작 설명

### 7.1 `common.h`

```c
struct app_msg {
    uint32_t seq;
};
```

procpu → appcpu, appcpu → procpu 양방향 모두 이 하나의 구조체(4바이트)만 주고받습니다. `src/`와 `remote/src/`에 동일한 내용으로 중복 배치되어 있습니다(이유는 Lab 03의 `shared_mem.h`와 동일 - 서로 다른 보드용으로 따로 빌드되는 완전히 별개의 Zephyr 애플리케이션이라서).

### 7.2 procpu (`src/main.c`)

1. `DEVICE_DT_GET(DT_NODELABEL(ipc0))`으로 icmsg 인스턴스를 가져와 `ipc_service_open_instance()`로 엽니다
2. `ipc_service_register_endpoint()`로 엔드포인트 하나를 등록합니다. 콜백 3개를 지정합니다:
   - `bound`: 양쪽이 서로 연결을 확인하면 호출됨 - 세마포어를 올려서 `main()`이 대기 상태에서 풀려나게 함
   - `received`: appcpu가 되돌려준 메시지가 올 때마다 호출 - `LOG_INF("recv reply seq=...")`로 기록
   - `error`: icmsg 자체 오류 발생 시 호출 (정상 동작 시엔 호출되지 않아야 함)
3. bind될 때까지 기다린 뒤, 메인 루프에서:
   - GPIO2 LED를 500ms마다 토글 (기존과 동일)
   - 200ms마다 `ipc_service_send()`로 새 순번을 전송. **`-ENOMEM`이 반환되면**(icmsg 링버퍼가 꽉 찬 경우) 그 값을 버리지 않고 다음 tick에 **같은 값으로 재시도**합니다 - Lab 03의 공유 슬롯처럼 값을 그냥 덮어써서 잃어버리는 대신, "보낼 자리가 날 때까지 기다렸다가 반드시 보낸다"는 태도입니다

### 7.3 appcpu (`remote/src/main.c`)

- `received` 콜백 안에서 **바로** 받은 값을 그대로 `ipc_service_send()`로 되돌려 보냅니다 (별도 메인 루프로 미루지 않음)
- 콜백 안에서 바로 `ipc_service_send()`를 호출해도 안전한 이유: Zephyr의 icmsg 구현(`subsys/ipc/ipc_service/lib/icmsg.c`)을 확인한 결과, 멀티스레딩이 켜져 있을 때(이 프로젝트의 모든 랩이 그렇습니다) `received` 콜백은 **워크큐 스레드 컨텍스트**에서 호출되고, 이 콜백을 호출하는 동안 어떤 락도 잡고 있지 않습니다 - 그래서 콜백 안에서 다시 `ipc_service_send()`를 호출해도 데드락이 발생하지 않습니다
- LED는 평소 200ms 하트비트, 메시지를 되돌려 보낼 때마다 짧은 더블 플래시 - 이건 순전히 눈으로 보기 위한 연출이라, 플래시 타이밍이 겹쳐서 몇 번이 뭉쳐 보이더라도 그게 곧 "메시지 손실"을 의미하지는 않습니다 (진짜 증거는 procpu 로그 쪽입니다)

## 8. 정상 동작 확인 체크리스트

- [ ] GPIO2 LED가 0.5초 간격으로 계속 깜빡인다
- [ ] GPIO42 LED가 평소 0.2초 간격으로 깜빡이다가, 메시지를 되돌려 보낼 때마다 짧은 더블 플래시를 보여준다
- [ ] 시리얼 터미널에 `04_IPC_SERVICE_ICMSG_LAB (core0/procpu) starting`, 이어서 `endpoint bound` 로그가 뜬다 (bind가 안 되면 여기서 멈춥니다 - devicetree 설정을 다시 확인해주세요)
- [ ] `sent seq=N (total_sent=N)` 로그가 약 200ms 간격으로 계속 증가하며 출력된다
- [ ] `recv reply seq=N (total_received=N, ...)` 로그도 뒤따라 출력된다
- [ ] **가장 중요한 확인**: `recv reply seq=` 값이 Lab 03처럼 건너뛰지 않고 **1,2,3,4,5...처럼 순서대로 하나도 빠짐없이** 이어진다 (Lab 03과 정반대 결과가 나와야 정상입니다)
- [ ] `icmsg error:` 로그나 `ipc_service_send() failed` 에러 로그가 뜨지 않는다 (`send buffer full, retrying` 경고는 뜰 수도 있고 안 뜰 수도 있습니다 - 떠도 그 다음 줄에서 같은 값이 재시도되어 성공하면 정상입니다)

## 9. 실기 검증 결과

실제 DevKitC-1 보드에서 위 체크리스트 전체를 확인했습니다. 실행 후 약 55초 경과 시점의 실제 로그입니다:

```
[00:00:55.444,000] <inf> app_procpu: recv reply seq=268 (total_received=268, total_sent=268)
[00:00:55.444,000] <inf> app_procpu: sent seq=268 (total_sent=268)
[00:00:55.650,000] <inf> app_procpu: recv reply seq=269 (total_received=269, total_sent=269)
[00:00:55.650,000] <inf> app_procpu: sent seq=269 (total_sent=269)
[00:00:55.856,000] <inf> app_procpu: recv reply seq=270 (total_received=270, total_sent=270)
[00:00:55.856,000] <inf> app_procpu: sent seq=270 (total_sent=270)
...
[00:00:58.537,000] <inf> app_procpu: recv reply seq=283 (total_received=283, total_sent=283)
[00:00:58.537,000] <inf> app_procpu: sent seq=283 (total_sent=283)
```

Lab 03과 정확히 대비되는 결과가 그대로 나타납니다:

- **`recv reply seq=`가 268, 269, 270, 271... 처럼 단 하나도 빠짐없이 순서대로 이어집니다.** Lab 03에서 `last_seen_seq`가 445 → 446 → 448처럼 건너뛰던 것과 정반대입니다.
- **`total_received`가 항상 `total_sent`와 정확히 같습니다** (`recv reply seq=268 (total_received=268, ..., total_sent=268)`). Lab 03에서는 `drained_count`(261)와 `last_seen_seq`(455)처럼 두 숫자가 계속 벌어졌던 것과 달리, 여기서는 "보낸 개수"와 "받은 개수"가 한 번도 어긋나지 않습니다.
- `icmsg error:` 로그나 `send buffer full` 경고도 이 구간에서 전혀 발생하지 않았습니다 - 200ms 전송 주기가 icmsg 링버퍼 용량(8KB) 안에서 충분히 여유롭게 처리된다는 뜻입니다.

`shm0`(16KB, mainline Zephyr에 기존 사용 예제가 없던 블록)를 icmsg의 tx/rx 영역으로 재사용한 설계와, `dcache-alignment = <0>` 설정 모두 실기에서 문제없이 동작하는 것을 이걸로 확인했습니다.

## 다음 단계

같은 request-response 구조를 icmsg 대신 `rpmsg` backend(OpenAMP 기반, 정식 RPMsg 프로토콜)로 교체해서 지연시간과 코드 복잡도를 비교하는 것이 Lab 05의 내용입니다.
