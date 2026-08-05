# 구현 계획서 — ARM64 저(低)-주소 메모리 최적화

> 명세서: `arm64-low-va-spec.md`. 구현 착수 예정: 다음 주.
> 대상 런타임: CoreCLR (Tizen `corerun`/crossgen2).
>
> ⚠️ **착수 전 문서이며 일부 판단이 실측으로 정정되었다.** 현재 상태와 정정 사항은
> `HANDOVER.md`를 볼 것. 특히 (1) `qemu-user -R`은 여기 적힌 것보다 유용하다 — VA 전체를 4GB로
> **강제**하므로 "저VA 환경에서 런타임이 동작하는가"의 기능 검증에는 타당하다(주소 배치 판정·RSS
> 측정에는 여전히 부적합). (2) A1a는 구현·측정 후 **revert**되어 트리에 없다. (3) 148행의
> `DOTNET_ARM64LowVA`는 폐기된 이름이다(68행 참조).

## 측정 환경 (Measurement Environment) — 하이브리드 (확정)

본 최적화의 측정 대상이 **가상 주소 배치 그 자체**이므로 환경 충실도가 결과 타당성을 좌우한다.
역할을 명확히 분리한다.

### 역할 분담
- **QEMU (내부 루프):** 빌드·기능 회귀·JIT 코드 크기·객체 레이아웃 검증. 빠른 반복에 사용.
- **실기기 rpi4/arm64 (판정 근거):** **RSS·GC 부기 절감·주소 불변식 수치는 실기기에서 산정.**
  Phase B Go/No-Go는 **반드시 실기기 수치**로 판단한다 (QEMU RSS 위에 세우면 게이트가 무의미).

### `qemu-user` vs `qemu-system` (중요)
- `qemu-user`(qemu-aarch64 + binfmt): 게스트 `mmap`을 QEMU가 가로채 **QEMU 자신의 주소 공간에서
  배치를 결정** → 게스트가 보는 VA는 타깃 커널 배치가 아니라 QEMU의 산물.
  - 위험: 저주소 arena가 "공짜로 성공"으로 보이는 **거짓 양성**, 또는 QEMU 배치 정책 충돌로 인한
    **거짓 음성**. 또한 `/proc/self/smaps_rollup` 에뮬레이션 커버리지가 보장되지 않아 주 측정
    수단이 무효화될 수 있음.
  - → **주소 불변식·RSS 측정에 사용 금지.** 기능/정적 지표에만 사용.
- `qemu-system`: 실제 게스트 커널 구동 → VA 배치 충실. 구축·속도 비용 큼. **실기기(rpi4 arm64)
  확보가 가능하므로 기본 계획에서는 사용하지 않는다** — 실기기 장애 시 폴백으로만 보류.
- 참고: 본 리포의 문서화된 QEMU 용도는 `qemu-user-static`+binfmt를 통한 **rootfs 구성**뿐이며
  (`docs/workflow/building/libraries/cross-building.md:11`,
  `docs/workflow/requirements/linux-requirements.md:95-99`), 테스트·측정 환경으로는 문서화되어 있지 않다.

### 항목별 적합성
| 측정 항목 | qemu-user | qemu-system | 실기기 |
|---|---|---|---|
| 기능 회귀(테스트 통과) | 충분 | 충분 | 충분 |
| JIT 코드 크기(`movz/movk` 수, 코드 바이트) | 충분(정적·결정적) | 충분 | 충분 |
| 객체당 오버헤드(레이아웃) | 충분(컴파일 타임) | 충분 | 충분 |
| GC 부기 예약 크기 | 배치 왜곡 시 오염 | 충분 | 충분 |
| **주소 불변식(전부 하위 4GB)** | **신뢰 불가** | 충분 | 충분 |
| **RSS 실측** | **불가** | 근사 | **유일 신뢰** |
| 단편화·메모리 압박 하 거동 | 불가 | 근사 | 충분 |
| ASLR·페이지 크기 거동 | 불가 | 충분 | 충분 |
| 성능 회귀 | 불가(캐시/TLB 미모델링) | 불가 | 충분 |

### 실기기 선행 조건 (A0에 포함)
1. **arm64 Tizen 이미지 bring-up** — 현 나이트리 하네스는 **armel(32bit)** 기준이므로 arm64
   측정에는 rpi4 arm64 이미지 확보가 선행 작업(`tizen-device-flash-dude`,
   `dotnet-runtime-tizen-upstream` 스킬 활용).
2. **장시간 측정 시 스케줄 무력화** — 기기 자체 리부트 타이머(22:20)와 나이트리 cron(22:30)을
   측정 전 비활성화, 완료 후 복원 (`/work/dotnet/CI/armel-ci/device-selfreboot.sh`,
   `install-cron.sh`; `armel-nightly-ci` 스킬). 미이행 시 측정이 중간에 깨진다.

## 단계 로드맵

### Phase A0 — 계측 기반 마련 (코드 변경 없음)
대표 Tizen 앱으로 baseline 메모리 프로파일 수집: 객체당 오버헤드, GC 부기 예약량, JIT 코드
크기, RSS. 이후 모든 단계의 이득을 이 baseline과 비교한다.
- 도구: `/proc/<pid>/smaps_rollup`, EventPipe(GC 이벤트), 코드 힙 사용량.
- **환경:** 위 "측정 환경" 정책을 따른다. RSS·주소 불변식 baseline은 **실기기에서** 취득하며,
  실기기 선행 조건(arm64 이미지 bring-up, 스케줄 무력화)을 A0의 작업 항목으로 포함한다.

### Phase A1 — 저주소 arena 예약
목표: GC/데이터/loader 예약을 하위 4GB로 유도.

**config (구현 확정):** `DOTNET_GCLowVirtualAddress`
- `0`/미설정 — 비활성(기본). 기존 동작 그대로.
- `1` — preferred: 저주소 우선, 실패 시 무제약 예약으로 폴백(첫 폴백 시 stderr 경고).
- `2` — strict: 폴백 없이 예약 실패 처리. **불변식 검증용 진단 모드**(GC는 예약 실패를 OOM으로
  취급하므로 프로덕션용 아님).

> 초기 스케치의 `DOTNET_ARM64LowVA`에서 이름을 변경했다. 메커니즘이 OS 추상화 계층에만 있어
> **아키텍처 중립**이며, ARM64로 못 박지 않으면 **linux-x64 개발 머신에서 그대로 검증** 가능하다
> (디바이스·QEMU 없이 실제 검증). 따라서 arch 게이트 없이 `HOST_64BIT`로만 감싼다.

**세부 단계:** A1a = GC 예약 경로(구현 완료), A1b = loader/코드 힙(PAL) 경로(예정).
- **수정 대상(대표):**
  - `pal/src/map/virtual.cpp` — `VIRTUALReserveMemory`(:560) 저주소 힌트,
    `TryReserveInitialMemory`(:1609)의 "avoid <4GB" 휴리스틱(:1670-1674)을 저VA 모드에서 반전.
  - `gc/unix/gcenv.unix.cpp:375-429` — `VirtualReserveInner`/`VirtualReserve`에 저주소 arena
    내 주소 힌트 + 필요 시 `MAP_FIXED_NOREPLACE` 재시도. (GC는 이 관문만 사용)
  - loader/frozen/static 힙이 동일 arena에서 예약되는지 확인.
- **재사용:** 기존 범위-제한 예약 패턴(`ExecutableMemoryAllocator`,
  `ClrVirtualAllocWithinRange` `utilcode/util.cpp:374-531`)을 데이터/GC 경로로 확장.
- **주의:** ARM64 Linux는 `MAP_32BIT` 미지원 → 주소 힌트/`MAP_FIXED_NOREPLACE` 기반 구현.

### Phase A2 — JIT 코드 크기 단축
목표: 런타임 주소가 4GB에 든다는 보장을 JIT가 활용해 직접 임베드 핸들(`IAT_VALUE`)을
`movz`+`movk` 2명령으로 낮춤(현재 최대 4명령).
- **수정 대상:** ARM64용 `GenTreeIntConCommon::FitsInAddrBase`/`AddrNeedsReloc` 신설
  (`jit/gentree.cpp`, `jit/gentree.h:3380`). AMD64(`gentree.cpp:20205-20246`)와
  **RISCV64**(`lowerriscv64.cpp:1182`, `codegenriscv64.cpp:1313`, `emitriscv64.cpp`) 구현 참고.
  적재부(`codegenarm64.cpp:2199-2256`)는 상위 0 halfword를 이미 건너뛰므로, 관건은 JIT가
  reloc 대신 짧은 인코딩을 고르게 하는 것.
- **범위:** JIT-time known 절대주소 한정. AOT(R2R/`dotnettool NI`)는 이미 `adrp`/`add`(2명령)
  또는 간접 셀 사용 → A2 이득 작음. **JIT 코드 우선.**

### Phase A-측정 & Phase B 판정
- A0 대비 GC 부기·코드 크기·RSS 절감 정량화 + 기능/성능 회귀 확인.
- 측정치(객체당 예상 절감 × 실제 객체 수 vs 구현·유지비·위험)로 compressed refs Go/No-Go.
  No-Go여도 A 이득은 보존.

### Phase B — Compressed References (별도 상세 설계서 대상)
전제: A로 "모든 관리 참조 대상 ∈ 하위 4GB" 보장. 영향 범위:
- 객체 레이아웃: `object.h:85-106` `OBJECT_SIZE`/`MIN_OBJECT_SIZE`/헤더 매크로 재정의.
- GC: 참조 read/write·barrier·마킹·relocation이 4바이트 참조 처리.
- JIT: 참조 load/store zero-extend, MT 접근 폭 변경.
- VM: `OBJECTREF`(`common.h:143`), MT 필드(`methodtable.h`) 폭 가정 수정.
- interop/unsafe/DAC/진단 레이아웃 인지.
- 개념 참고: R2R/NativeAOT 32bit relative 포맷(`nativeaot/.../MethodTable.cs:1340`, ILCompiler 노드).

### Phase C — 네이티브 interop 저주소 트램폴린 (선택, 측정 후 판단)
목표: "모든 포인터 32bit" 야심을 네이티브 경계까지 확장. Phase B가 **런타임 제어 포인터**를
32bit화한다면, C는 **OS/네이티브 제어 포인터**(`.so` 로드 위치, 네이티브 `malloc`, 커널 반환
주소, P/Invoke 함수 포인터)를 하위 4GB에서 도달 가능하게 만든다. 커널은 `.so`를 ASLR로
고주소에 올리며 위치를 강제할 수 없으므로 **간접 계층**으로 해결한다.

- **경로별 해법 (반드시 구분):**
  - **함수 호출(코드) — 트램폴린으로 완결.** 하위 4GB 코드 영역에 thunk를 심고, 관리 측은
    thunk의 32bit 주소만 저장. thunk가 실제 고주소로 점프: `ldr x16, =target ; br x16`.
    ELF PLT와 동형 구조 → CoreCLR 기존 스텁 기계(precode/stub/jump stub) 재사용·확장.
    비용: 호출당 간접 점프 1회 + thunk 테이블(항목당 ~16B) 저메모리 상주.
  - **데이터 포인터 — 핸들 테이블 필요(고비용).** 데이터는 트램폴린으로 우회 불가. 32bit
    인덱스→실제 64bit 주소 매핑 테이블을 두고 역참조마다 조회. 네이티브 `malloc` 버퍼,
    `unsafe`/`fixed` raw 포인터 등이 해당. 복잡도·비용 큼 → 이득이 확인될 때만.

- **필수 제약 — `IntPtr` ABI:** 64bit 프로세스에서 `IntPtr.Size == 8`은 공개 관찰 가능 값이며
  blittable 구조체 레이아웃·마샬링·`Marshal.SizeOf`가 이를 전제. 네이티브 포인터의 **저장 폭**을
  4바이트로 바꾸면 네이티브 ABI가 깨진다. → 권고: **네이티브 포인터 값은 8바이트 유지**,
  트램폴린으로 "저주소 도달성"만 확보. 저장 폭 축소는 하지 않음.

- **우선순위 근거:** 네이티브 포인터는 interop 표면에만 존재해 **개수가 적다** → 32bit화의
  메모리 이득은 작고 복잡도·위험만 큼. Phase C의 가치는 "메모리 절감"보다 **주소 공간 일관성/
  도달성 보장**에 있음. 따라서 Phase C는 **선택 항목**이며 B의 측정 결과와 interop 특성(호출
  빈도·데이터 포인터 비중)을 보고 착수 여부를 판단한다.

## 검증 방법

- **빌드/기능 회귀:** ARM64 크로스 빌드 후 `corerun`으로 라이브러리·CoreCLR 테스트 통과
  (`/.github/copilot-instructions.md`, `docs/workflow/testing/coreclr/testing.md`).
  실기기는 Tizen armel/arm64 하네스(`dotnet-runtime-tizen-upstream` 스킬).
- **메모리 계측 [실기기]:** `smaps_rollup` RSS, EventPipe GC 이벤트, `DOTNET_GCHeapHardLimit`
  조합에서 저주소 arena 점유 확인. A 전/후 동일 워크로드 비교. QEMU 수치는 판정 근거로 쓰지 않음.
- **주소 불변식 검증 [실기기 / 대체: qemu-system]:** 디버그 assert로 "모든 GC 세그먼트/loader
  heap/MT ∈ 하위 4GB" 확인 (B 전제조건 사전 검증). `qemu-user`는 신뢰 불가.
- **JIT 코드 크기 [QEMU 가능]:** `DOTNET_JitDisasm`으로 `movz/movk` 시퀀스 길이/총 코드 바이트
  비교. 정적·결정적 지표이므로 QEMU에서 취득해도 유효.
- **interop 안전성:** P/Invoke·delegate 콜백이 고주소 `.so`와 정상 동작하는지 회귀(전략 2 검증).
- **Phase C 트램폴린(해당 시):** 저주소 thunk를 통한 P/Invoke 호출이 고주소 `.so` 함수로
  정확히 도달하는지, thunk 주소가 32bit에 드는지, `IntPtr.Size == 8` 및 blittable 마샬링이
  불변인지 검증.
- **회귀 격리:** ARM64 + `DOTNET_ARM64LowVA` 뒤에 격리, 기본/타 플랫폼 무영향 보장.

## 미해결/후속 확인 사항
- 대표 워크로드에서 GC 부기 절감의 실제 크기(측정 전 미지).
- 저주소 arena 초기 크기/성장 정책(단편화 vs 예약 낭비 트레이드오프).
- Tizen 커널의 저주소 mmap 힌트 수용성 및 ASLR 완화의 보안 정책 적합성.
- ~~rpi4 arm64 Tizen 이미지 bring-up 가능 여부~~ → **가능 확인됨.** 실제 bring-up은 A0 착수 시
  진행 (지금 미리 준비할 필요 없음). `qemu-system` 대체안은 불필요.
