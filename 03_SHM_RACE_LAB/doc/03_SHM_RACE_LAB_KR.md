# Lab 03 - 공유 메모리(ipmmem0 재사용) + MBOX 알림, 그리고 레이스 컨디션 체감하기

## 1. 이 랩의 목적

Lab 02에서는 MBOX를 "신호만 전달하는 초인종"으로만 썼습니다(페이로드 없음). 이번 Lab 03은 여기서 한 걸음 더 나갑니다:

- procpu와 appcpu가 **실제 데이터(작은 구조체)를 공유 SRAM에 직접 쓰고 읽는** 것을 해봅니다 (`ipc_service.h` 같은 프레임워크 없이, MBOX + 원시 포인터만으로)
- 그 과정에서 **아무 프레임워크도 없이 직접 만들면 반드시 마주치는 레이스 컨디션**을 실기에서 직접 관찰합니다
- `atomic_t`로 "알림 개수를 잃어버리지 않는 것"은 고칠 수 있지만, "공유 메모리 슬롯 하나에만 최신값을 계속 덮어쓰는 방식"은 **알림 개수와 무관하게 중간값 손실이 근본적으로 남는다**는 것까지 확인합니다 → 이게 왜 Lab 04/05의 `IPC Service`(icmsg/rpmsg) 같은 진짜 메시지 큐 프레임워크가 필요한지에 대한 이유가 됩니다

이번 랩은 버튼도, 새 배선도 없습니다. Lab 01/02와 똑같은 LED 2개(PROCPU=GPIO2, APPCPU=GPIO42)만 그대로 씁니다 - 관찰은 대부분 **procpu의 시리얼 로그** 하나로 충분합니다 (appcpu는 여전히 UART 콘솔을 지원하지 않습니다. 이유는 `01_HELLO_DUALCORE_LAB` 문서 참고).

## 2. 왜 새 `reserved-memory`를 만들지 않고 `ipmmem0`를 재사용해도 안전한가

ESP32-S3의 SoC devicetree(`esp32s3_common.dtsi`)에는 이미 이런 노드가 있습니다:

```c
ipmmem0: memory@3fce5000 {
    compatible = "mmio-sram";
    reg = <0x3fce5000 0x400>;   /* 1024바이트 */
};

ipm0: ipm@3fce9400 {
    compatible = "espressif,esp32-ipm";
    shared-memory = <&ipmmem0>;
    shared-memory-size = <0x400>;
    ...
};

mbox0: mbox@3fce9408 {
    compatible = "espressif,mbox-esp32";
    shared-memory = <&ipmmem0>;
    shared-memory-size = <0x400>;
    ...
};
```

즉 `ipmmem0`는 `&ipm0`/`&mbox0` 드라이버가 **메시지 페이로드를 코어 간에 복사할 때 쓰려고 이미 예약해둔** 1KB SRAM 블록입니다. "이미 다른 목적으로 예약된 메모리를 우리 마음대로 또 써도 되는가?"라는 질문이 자연히 생기는데, Zephyr의 실제 ESP32 MBOX 드라이버 소스(`drivers/mbox/mbox_esp32.c`)를 확인해서 답을 검증했습니다:

```c
if (msg != NULL && msg->data != NULL) {
    uint8_t *dest = (dev_data->other_core_id == 0) ? dev_data->shm.pro_cpu_shm
                                                    : dev_data->shm.app_cpu_shm;
    memcpy(dest, msg->data, msg->size);
}
```

**드라이버는 `msg`가 `NULL`이거나 `msg->data`가 `NULL`일 때는 이 공유메모리 버퍼를 단 1바이트도 읽거나 쓰지 않습니다.** Lab 02와 이번 랩 모두 `mbox_send_dt(&tx_channel, NULL)`처럼 **항상 NULL 페이로드로만** 호출하므로, 드라이버의 `memcpy`는 절대 실행되지 않고, 이 1KB 전체가 우리 애플리케이션 용도로 완전히 비어있게 됩니다. 그래서 새 `reserved-memory` 영역을 devicetree에 추가로 정의하지 않고, 이미 있는 `ipmmem0`를 그대로 재사용합니다.

추가로, `mbox0` 드라이버 내부는 이 1KB를 방향(코어)별로 절반씩(`pro_cpu_shm`/`app_cpu_shm`, 각 512바이트) 나눠서 관리한다는 것도 확인했습니다. 이번 랩도 같은 방식으로 절반씩 나눠 씁니다(대칭성을 위한 선택일 뿐, 기능적으로 꼭 그래야 하는 건 아닙니다 - `src/shared_mem.h` 참고).

## 3. 준비물 / 배선

- ESP32-S3-DevKitC-1 보드, USB 케이블 1개
- **추가 배선 없음** - Lab 01/02와 완전히 동일한 온보드 LED 2개만 사용:
  - PROCPU 하트비트 LED: GPIO2 (`&gpio0`)
  - APPCPU 하트비트 LED: GPIO42 (`&gpio1`, 로컬 인덱스 42-32=10)
- (LED를 직접 배선해야 한다면 Lab 01 문서의 배선 설명을 그대로 따라하시면 됩니다 - 이 랩은 그 부분을 반복 설명하지 않습니다)

## 4. 디렉터리 구조

```
03_SHM_RACE_LAB/
├── doc/
│   └── 03_SHM_RACE_LAB_KR.md          (이 문서)
└── lab/
    ├── CMakeLists.txt                  (procpu 앱)
    ├── prj.conf                        (procpu Kconfig)
    ├── sample.yaml
    ├── sysbuild.cmake                  (appcpu 이미지를 함께 빌드하도록 등록)
    ├── sysbuild.conf                   (MCUboot 비활성화)
    ├── boards/
    │   └── esp32s3_devkitc_esp32s3_procpu.overlay
    ├── src/
    │   ├── shared_mem.h                 (공유 메모리 레이아웃 정의)
    │   └── main.c                       (procpu 애플리케이션)
    └── remote/                          (appcpu 앱 - procpu와 완전히 별개의 Zephyr 애플리케이션)
        ├── CMakeLists.txt
        ├── prj.conf
        ├── boards/
        │   └── esp32s3_devkitc_esp32s3_appcpu.overlay
        └── src/
            ├── shared_mem.h             (procpu 쪽과 내용이 완전히 동일한 사본 - 아래 6절 참고)
            └── main.c                   (appcpu 애플리케이션)
```

## 5. 빌드 & 플래시

Lab 01/02와 동일하게 `--sysbuild`로 procpu/appcpu 두 이미지를 한 번에 빌드/플래시합니다.

```bash
cd 03_SHM_RACE_LAB/lab

west build -p always --sysbuild -b esp32s3_devkitc/esp32s3/procpu .
west flash
```

- `-p always`: 이전 빌드 캐시를 완전히 지우고 새로 빌드 (다른 랩과 헷갈리지 않도록)
- `--sysbuild`: `sysbuild.cmake`에 등록된 `remote/`(appcpu) 앱도 함께 빌드
- `west flash`: procpu(`0x00000000`)와 appcpu(`0x002c0000`) 이미지를 각자의 주소에 플래시

빌드 후 시리얼 터미널(115200bps)을 열어 procpu의 로그를 확인하세요.

## 6. 코드 동작 설명

### 6.1 `shared_mem.h` - 공유 메모리 레이아웃

```c
#define SHM_BASE (DT_REG_ADDR(DT_NODELABEL(ipmmem0)))
#define SHM_SIZE (DT_REG_SIZE(DT_NODELABEL(ipmmem0)))
#define SHM_HALF_SIZE (SHM_SIZE / 2)

#define SHM_P2A_ADDR (SHM_BASE)                 /* procpu -> appcpu */
#define SHM_A2P_ADDR (SHM_BASE + SHM_HALF_SIZE) /* appcpu -> procpu */

struct p2a_payload { uint32_t seq; };
struct a2p_payload { uint32_t drained_count; uint32_t last_seen_seq; };
```

- `DT_NODELABEL(ipmmem0)`으로 devicetree에 **이미 있는** 노드의 주소/크기를 그대로 읽어옵니다. 이 랩을 위해 새로 추가한 devicetree 노드는 없습니다 (오버레이에는 `mbox-consumer`와 `&mbox0 { status = "okay"; };`만 있습니다 - Lab 02와 동일).
- 이 헤더 파일은 `src/`와 `remote/src/`에 **완전히 동일한 내용으로 중복** 배치되어 있습니다. procpu와 appcpu는 서로 다른 보드용으로 따로 빌드되는 완전히 별개의 Zephyr 애플리케이션(각자 `CMakeLists.txt`/`project()`)이라, 공유 include 경로를 새로 구성하기보다 작은 헤더 하나를 손으로 동기화하는 쪽이 더 간단하다고 판단했습니다. **이 파일을 수정할 때는 두 군데 다 고쳐야 합니다.**

### 6.2 procpu (`src/main.c`) - 빠르게 계속 보내는 쪽

메인 루프에서 매번 `k_uptime_get()` 기반으로(블로킹 없이) 세 가지를 확인합니다:

1. **GPIO2 하트비트**: 500ms마다 토글 (Lab 01/02와 동일)
2. **200ms마다 전송**: `total_sent`를 1 증가시키고 `SHM_P2A_ADDR`에 써넣은 뒤, `mbox_send_dt(&tx_channel, NULL)`로 appcpu의 초인종을 울림 (페이로드는 여전히 NULL - 실제 데이터는 공유메모리로, "새 데이터 있음" 신호만 MBOX로, 라는 이번 랩의 핵심 설계). 보낼 때마다 `LOG_INF("sent seq=%u ...")`로 기록
3. **appcpu의 응답 수신**: appcpu가 초인종을 울릴 때마다(ISR 콜백에서) `atomic_inc(&reply_pending)`으로 카운트만 올려두고, 메인 루프에서 `while (atomic_get(&reply_pending) > 0)`로 밀린 응답을 전부 드레인하면서 `SHM_A2P_ADDR`을 읽어 `LOG_INF("appcpu reply: drained_count=... last_seen_seq=... backlog=...")`로 기록

### 6.3 appcpu (`remote/src/main.c`) - 일부러 느리게 처리하는 쪽

- **GPIO42 하트비트**: 평소엔 200ms 간격으로 토글 (Lab 01/02와 동일)
- procpu의 초인종이 울리면(ISR 콜백) `atomic_inc(&pending)`으로 대기 개수만 올려둠
- 메인 루프는 **`PROCESS_PERIOD_MS`(350ms)에 한 번씩만** 대기열에서 하나를 꺼내 "처리"함:
  - `SHM_P2A_ADDR`에서 **지금 이 순간** 들어있는 `seq` 값을 읽음 (주의: procpu가 이 초인종을 울린 "그 순간"의 값이 아니라, appcpu가 실제로 읽는 "지금 이 순간"의 값입니다 - procpu는 그 사이에도 계속 새 값을 덮어쓰고 있었을 수 있습니다)
  - `SHM_A2P_ADDR`에 `{drained_count, last_seen_seq}`를 써넣고 procpu에게 초인종을 울려 응답
  - LED로 짧은 더블 플래시(80ms 간격, 4번 엣지) 반응을 보여준 뒤 평소 하트비트로 복귀
- **`PROCESS_PERIOD_MS`(350ms)를 procpu의 `SEND_PERIOD_MS`(200ms)보다 일부러 더 느리게** 잡아둔 것이 이 랩의 핵심 장치입니다 - appcpu가 procpu를 따라잡지 못하게 만들어서, 아래 7절의 현상이 실제로 관찰되게 합니다.

## 7. 레이스 컨디션: 무엇이, 왜 벌어지는가

procpu는 200ms마다 하나씩 보내는데 appcpu는 350ms마다 하나씩만 처리하므로, 시간이 지날수록 "밀린 초인종 개수"(`atomic_t pending`)가 계속 쌓입니다. 여기서 확인할 수 있는 두 가지는 서로 다른 문제입니다:

- **`drained_count` (알림 "개수"는 안 잃어버립니다)**: `pending`이 일반 `bool`/`int` 변수가 아니라 `atomic_t`이기 때문에, ISR이 아무리 빠르게 여러 번 겹쳐서 `atomic_inc`를 호출해도 그 증가분은 절대 사라지지 않습니다. appcpu가 느긋하게라도 계속 `atomic_dec`으로 하나씩 꺼내 처리하는 한, `drained_count`는 언젠가 반드시 `total_sent`를 다 따라잡습니다 (물론 계속 뒤처진 채로요 - `backlog = total_sent - drained_count`가 procpu 로그에 매번 출력됩니다).
- **`last_seen_seq` (그러나 "값"은 잃어버립니다)**: `SHM_P2A_ADDR`는 슬롯이 딱 하나뿐이라 procpu가 새 값을 쓸 때마다 이전 값은 그냥 사라집니다. appcpu가 350ms에 한 번씩만 읽으러 오는 동안 procpu는 이미 그 슬롯을 여러 번 덮어썼을 수 있으므로, `last_seen_seq`는 `drained_count`와 나란히 1,2,3,4...로 증가하지 않고 **듬성듬성 건너뜁니다** (예: 1 → 4 → 7 → 9 ...). 즉 appcpu는 "초인종이 울린 횟수"는 정확히 세지만, "그 초인종들이 각각 어떤 값을 갖고 있었는지"는 이미 상당수 잃어버린 상태입니다.

procpu 시리얼 로그에서 대략 이런 패턴을 보시게 됩니다 (정확한 숫자는 타이밍에 따라 달라집니다):

```
[00:00:01.200] sent seq=6 (total_sent=6)
[00:00:01.400] sent seq=7 (total_sent=7)
[00:00:01.410] appcpu reply: drained_count=3 last_seen_seq=7 (procpu total_sent=7, backlog=4)
[00:00:01.600] sent seq=8 (total_sent=8)
```

`drained_count=3`인데 `last_seen_seq=7`인 것을 보시면, appcpu가 3번째로 처리한 항목이 실제로는 (진작에 사라진) 4, 5, 6이 아니라 그 시점에 남아있던 최신값 7을 읽었다는 뜻입니다. **알림은 3번 정확히 처리했지만(개수는 안 틀렸지만), 그 사이의 실제 데이터 4/5/6은 영영 사라졌습니다.**

이게 바로 "프레임워크 없이 공유 메모리 + 초인종만으로 IPC를 직접 만들면 반드시 겪는" 근본적인 한계입니다. `atomic_t`는 "몇 번 알림이 왔는지"는 정확하게 지켜주지만, "각 알림이 어떤 데이터를 담고 있었는지"까지 지켜주지는 못합니다 - 슬롯이 하나뿐이니까요. 이걸 해결하려면 진짜 메시지 큐(각 메시지를 자기 슬롯에 보관, 링버퍼 등)가 필요한데, 그게 바로 Lab 04/05에서 다룰 `IPC Service`(`icmsg`/`rpmsg` backend)가 프레임워크 차원에서 이미 해결해주는 문제입니다.

## 8. 정상 동작 확인 체크리스트

- [ ] GPIO2 LED가 0.5초 간격으로 계속 깜빡인다 (PROCPU 하트비트)
- [ ] GPIO42 LED가 평소 0.2초 간격으로 계속 깜빡이다가, 주기적으로 빠른 더블 플래시(80ms 간격 반짝임 2번)를 보여준다 (APPCPU가 backlog를 하나 처리할 때마다)
- [ ] 시리얼 터미널에 `03_SHM_RACE_LAB (core0/procpu) starting`과 `reusing ipmmem0 at 0x3fce5000, size 1024 bytes (half=512)` 로그가 뜬다
- [ ] `sent seq=N (total_sent=N)` 로그가 약 200ms 간격으로 계속 증가하며 출력된다
- [ ] `appcpu reply: drained_count=... last_seen_seq=... backlog=...` 로그가 약 350ms 간격으로 출력되고, **시간이 지날수록 `backlog` 값이 서서히 커진다**
- [ ] `last_seen_seq` 값이 `drained_count`처럼 1씩 증가하지 않고 **군데군데 건너뛰는 것**을 로그에서 직접 확인할 수 있다 (7절에서 설명한 레이스 현상)

## 9. 실기 검증 결과

실제 DevKitC-1 보드에서 위 체크리스트 전체를 확인했습니다. 약 1분 30초 경과 시점의 실제 로그 일부입니다:

```
[00:01:30.226,000] <inf> app_procpu: appcpu reply: drained_count=255 last_seen_seq=445 (procpu total_sent=445, backlog=190)
[00:01:30.398,000] <inf> app_procpu: sent seq=446 (total_sent=446)
[00:01:30.580,000] <inf> app_procpu: appcpu reply: drained_count=256 last_seen_seq=446 (procpu total_sent=446, backlog=190)
[00:01:30.600,000] <inf> app_procpu: sent seq=447 (total_sent=447)
[00:01:30.802,000] <inf> app_procpu: sent seq=448 (total_sent=448)
[00:01:30.933,000] <inf> app_procpu: appcpu reply: drained_count=257 last_seen_seq=448 (procpu total_sent=448, backlog=191)
```

여기서 정확히 이 문서가 설명한 두 가지가 그대로 나타납니다:

- **개수는 보존됨**: `drained_count`가 255 → 256 → 257로 정확히 1씩 늘어납니다. `atomic_t`가 알림을 하나도 잃어버리지 않았다는 증거입니다.
- **값은 손실됨**: `last_seen_seq`는 445 → 446 → 448로, **447이 통째로 빠져 있습니다**. procpu가 447을 보낸 직후 곧바로 448을 덮어써버려서, appcpu가 그 슬롯을 읽으러 왔을 때는 이미 447이 사라지고 448만 남아있었기 때문입니다.
- **backlog는 계속 누적됨**: `backlog`가 190 → 191로 계속 늘어나는 것도 보이는데, 이는 appcpu의 처리 속도(350ms)가 procpu의 전송 속도(200ms)보다 근본적으로 느리기 때문에 나타나는, 이 랩에서 의도한 정상적인 현상입니다.

## 다음 단계

이 랩에서 확인한 "알림 개수는 안전하게 셀 수 있지만, 공유 슬롯 하나만으로는 그 사이 데이터가 손실된다"는 한계는 Lab 04(`IPC Service` - `icmsg` backend)에서 표준 프레임워크가 어떻게 해결하는지 이어서 다룹니다.
