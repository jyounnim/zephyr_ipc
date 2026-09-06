# Lab 01 트러블슈팅 - APPCPU LED가 켜지지 않는 문제

## 증상

PROCPU LED(로그 포함)는 처음부터 정상 동작. APPCPU LED만 계속 켜지지 않음. GPIO 핀을 여러 번 바꿔보고(GPIO42 → GPIO18 → GPIO2), 점멸 주기도 바꿔보고, LED/저항을 물리적으로 서로 바꿔 껴봐도 항상 같은 결과: **PROCPU가 구동하는 핀만 토글되고, APPCPU가 구동하는 핀은 어떤 핀이든 전혀 토글되지 않음.**

## 배제한 원인들 (순서대로)

1. **LED/저항 등 부품 불량** — 배제. 같은 LED+저항 세트를 GPIO2(PROCPU)와 GPIO42/GPIO18(APPCPU) 사이에서 물리적으로 바꿔 껴봐도, 항상 PROCPU 쪽 핀에 연결됐을 때만 토글됨.
2. **GPIO42 핀 자체의 문제, 또는 `&gpio1`(핀 32 이상) 컨트롤러의 로컬 인덱스 계산 실수** — 배제. PROCPU가 GPIO18(`&gpio0`)로 토글을 시도했을 때도, GPIO2를 GPIO18로 바꿔서 시도했을 때도 전부 정상 동작. 즉 핀 번호/컨트롤러 자체는 문제가 아니었음. (참고: `&gpio1`의 로컬 인덱스 = 물리 GPIO 번호 - 32 라는 계산법 자체는 맞는 것으로 확인됨 — 예: GPIO42 → `<&gpio1 10 ...>`)
3. **빌드 캐시/스테일 아티팩트** — 배제. `rm -rf build` 후 완전 클린 빌드, 그리고 `esptool erase_flash`로 플래시 전체 삭제 후 재플래시까지 해봤지만 동일한 증상.
4. **빌드/플래시 로그 자체의 이상** — 배제. `west build`/`west flash` 로그를 확인한 결과 procpu(`0x00000000`)와 appcpu/remote(`0x002c0000`) 양쪽 이미지 모두 에러 없이 정상적으로 빌드되고, 서로 다른 플래시 주소에 정상적으로 기록됨.
5. **Kconfig 차이(로깅 등)** — 배제. `CONFIG_LOG`/`CONFIG_LOG_MODE_IMMEDIATE`를 추가해봐도 변화 없음.

## 실제 원인

**PROCPU와 APPCPU 양쪽 이미지에서 `&ipm0`(ESP32 소프트 IPM 메일박스)가 활성화(`status = "okay"`)돼 있지 않으면, APPCPU 코어 자체가 reset에서 풀리지 않습니다.** GPIO 핀 번호나 배선과는 완전히 무관한, 이 보드/Zephyr 조합의 코어 기동 방식 자체의 특성입니다.

이 프로젝트의 다른 랩(`06_AHT20_BMP280_MultiSensor`, IPM을 실제로 사용하는 멀티센서 랩)에서는 애초에 IPM을 쓰고 있었기 때문에 이 문제 자체가 드러나지 않았습니다. Lab 01은 IPC가 전혀 없는 "hello world" 랩으로 설계됐던 터라, IPM을 켤 이유가 없다고 판단해서 처음엔 빼놨었는데, 그게 바로 APPCPU가 죽어있던 이유였습니다.

### 어떻게 좁혔나

1. procpu/appcpu 각각의 코드가 완전히 같은 패턴(`gpio-leds` devicetree 노드 + `gpio_pin_configure_dt`/`gpio_pin_set_dt`)을 쓰는데 한쪽만 실패 → 코드 로직 자체의 버그가 아니라 "그 코드가 애초에 실행되고 있는가"를 의심하기 시작
2. PROCPU로 같은 핀(GPIO18)을 구동해보는 실험으로 "핀은 멀쩡하다"를 확인 → 문제가 APPCPU 실행 여부로 좁혀짐
3. 이미 실기 검증된 `06_AHT20_BMP280_MultiSensor` 랩과 Lab 01의 설정 차이를 다시 대조 → 유일하게 남은 실질적 차이가 IPM(`&ipm0`, `CONFIG_IPM`, `CONFIG_ESP32_SOFT_IPM`) 활성화 여부였음
4. IPM을 (실제로 쓰지 않고 활성화만) 추가해서 재현 테스트 → APPCPU LED가 즉시 정상 동작 시작 → 원인 확정

## 해결 / 최종 설정

이 랩(그리고 이 시리즈의 앞으로 모든 랩)의 `prj.conf`와 오버레이에는, 실제 IPC 사용 여부와 무관하게 아래가 기본으로 들어갑니다:

**procpu/appcpu 양쪽 `prj.conf`:**
```
CONFIG_IPM=y
CONFIG_ESP32_SOFT_IPM=y
```

**procpu/appcpu 양쪽 오버레이:**
```
&ipm0 {
	status = "okay";
};
```

## 후속 확인 필요 사항

- Lab 02부터는 MBOX(`mbox.h`) API로 넘어갈 예정인데, **MBOX를 쓸 때도 이 IPM 요구사항이 그대로 필요한지, 아니면 MBOX 활성화 자체가 같은 역할(APPCPU 기동)을 하는지는 아직 확인되지 않았습니다.** Lab 02 착수 시 반드시 재확인이 필요합니다 — 어쩌면 IPM과 MBOX를 동시에 켜야 할 수도 있습니다.
