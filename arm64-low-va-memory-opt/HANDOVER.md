# ARM64 메모리 최적화 (포인터 압축) — 인수인계

> **이 폴더에서 가장 먼저 읽는 문서.** 다른 문서와의 관계:
>
> | 문서 | 성격 | 주의 |
> |---|---|---|
> | **HANDOVER.md** (이 문서) | 현재 상태·수치·기술적 사실 | 최신 |
> | `RESTORE.md` | **저장소·브랜치·디바이스 상태 복원 안내** | `/work/dotnet/new`가 삭제된 뒤 **여기서 시작** |
> | `measurements/2026-08-04-tizen-app-census/` | 실제 앱 census 원본 데이터 + 재측정 스크립트 | §4-3b의 근거 |
> | `STAGE-B-POINTER-COMPRESSION.md` | Stage B 시간순 작업 기록 (52KB) | 상세 근거. 진입점으로는 부적합 |
> | `PHASE-A1-RESULTS.md` | Phase A1a 구현·측정 결과 | 유효 |
> | `arm64-low-va-spec.md` | 착수 전 명세 | 일부 판단이 실측으로 **정정됨** (§2-2, §5-6b) |
> | `arm64-low-va-plan.md` | 착수 전 구현 계획 | 일부 판단이 **정정됨** (§2-3, §5-6b) |

## 왜 이 작업을 하는가

대상은 메모리가 제한된 Tizen ARM64 기기다. ARM64에서 CoreCLR은 모든 객체 참조와 `MethodTable`
포인터를 8바이트로 **저장**하는데 실제로는 프로세스가 수백 MB만 쓴다. 가상 주소를 하위 4GB에
가두면 이 포인터들을 4바이트로 저장하고 사용 시 확장할 수 있어, 힙의 포인터 비중만큼 메모리를
줄일 수 있다. "느려도 메모리를 최대한 줄인다"가 우선순위다.

---

## ⚠️ 먼저 알아야 할 3가지

### 1. 지금까지 실제 메모리 절감은 거의 없다

| 구분 | 상태 |
|---|---|
| **객체당 절감** | **0 바이트** (모든 shape, 실측) |
| **타입당 절감** | **8 바이트** (로더 힙, 실측) — 수천 타입이면 수십 KB |
| **RSS 절감** | **측정되지 않음** — 위 수십 KB는 실행 간 편차보다 작다 |
| 투영 절감 (미구현) | 라이브 힙의 **2.8% ~ 22%** (워크로드 의존) |

완료된 것은 **"절감을 만드는 단계의 전제와 기계장치"** 다. 절감 자체는 전부 마지막 단계(저장 폭
축소)에서 나온다. **"이미 메모리가 줄었다"고 가정하면 안 된다.**

### 2. Phase A 코드는 트리에 없다 (전부 revert)

Phase A1a(GC 저주소 예약)는 구현·측정까지 마친 뒤 **전부 revert**했다. `qemu-user -R`이 게스트 VA
전체를 4GB로 제한해 주므로 개발/테스트에는 런타임 수정이 불필요하다는 것이 확인됐기 때문이다.
`grep -rn "GCLowVirtualAddress" src/` → 0건. **측정 결과는 지식으로 남고 코드는 없다.**
실기기에서는 다시 필요하다.

### 3. 검증은 QEMU + 실기기 양쪽에서 되어 있다 (RSS만 미측정)

**2026-08-03: 실기기(rpi4, Tizen 11.0 aarch64) 검증 완료.** 압축 빌드가 실기기에서
`CodeGenBringUpTests` 641개를 전부 통과한다 — 상세는 §3-3.

계획서는 `qemu-user`를 "주소 불변식 신뢰 불가 / RSS 측정 불가"로 규정했다. 그 판단은 여전히
유효하지만 **질문별로 역할이 다르다**:

| 질문 | qemu `-R` | 실기기 |
|---|---|---|
| "모든 주소가 4GB 아래일 때 런타임이 정상 동작하는가" | ✅ 불변식을 *강제*하므로 타당 | ✅ **shim으로 강제해 검증 완료** |
| "실제 커널이 런타임 할당을 저주소에 놓아 주는가" | ❌ QEMU가 배치를 결정 | ✅ **측정 완료 — 놓을 수 있다** (§3-3) |
| "RSS가 줄었는가" | ❌ 불가 | ⏳ **아직 안 함** (애초에 현재 절감이 타입당 8B뿐이라 노이즈 이하) |

즉 남은 공백은 **RSS 실측**과 **I3(코드 힙) 불변식**뿐이다.

---

## 1. 프로젝트 구조와 판단 근거

### 1-1. ARM64 메모리 오버헤드의 근원 (명세 §2)

| # | 항목 | 32bit 대비 | 근거 |
|---|---|---|---|
| 1 | 객체 참조/`MethodTable` 포인터 저장 = 8B | 2배 | `vm/object.h:130`, `vm/methodtable.h` |
| 2 | 객체 헤더 4B `m_alignpad` 死공간, `MIN_OBJECT_SIZE` 24 vs 12 | +패딩 | `vm/object.h:85-106`, `vm/syncblk.h` |
| 3 | GC 부기 테이블(card/brick/mark/seg-mapping)이 커버 범위에 비례 | 범위 의존 | `gc/gcpriv.h`, `gc/gc.cpp` |

### 1-2. 설계를 지배하는 사실 (명세 §3)

- **"주소 공간 제한"과 "포인터 폭 축소"는 별개다.** 주소를 4GB에 가둬도 저장 폭은 8B → 객체당
  메모리는 줄지 않는다. → **Phase A는 그 자체로 물리 절감 수단이 아니다.**
- compressed references 모드는 현재 런타임에 **없다.** 과거 담당했던 self-relative 32bit 포인터
  machinery(`RelativePointer` 등)는 crossgen1 제거와 함께 삭제됨(commit `5a5b1045ab2`). 잔존은
  R2R/NativeAOT **디스크 포맷** 리더뿐. → **재사용 가능한 upstream 패치는 존재하지 않는다.**
- 그러나 주소 공간 제한은 compressed refs의 **전제조건**이므로 단계적 진행 시 버려지는 작업이 없다.

### 1-3. 외부 `.so` 전략 — 전략 2 채택 (명세 §7)

| | 내용 | 판정 |
|---|---|---|
| 전략 1 | `personality(ADDR_LIMIT_32BIT)`로 프로세스 전역 제한 | **비권장** — 스택·TLS·vDSO·glibc·자식 프로세스가 모두 4GB에 눌려 실패/단편화 |
| 전략 2 | **런타임 영역만 저주소** | ✅ **채택** |

핵심 통찰: 관리 참조는 오직 GC 힙/frozen/static을, `MethodTable` 포인터는 loader heap을 가리킨다 —
전부 런타임이 배치 가능. **관리 참조가 `.so`를 직접 가리키는 일은 없다.** interop 경계의 네이티브
주소는 `IntPtr` = full width 8B 슬롯에 저장되므로 고주소여도 무방.

> 부수 사실: **4GB에 모아도 jump stub은 사라지지 않는다** (`bl` ±128MB ≪ 4GB). 코드 도달성은 기존
> 1GB 슬랩이 이미 처리하므로 본 최적화의 목표가 아니다.

---

## 2. Phase A — 저주소 arena (A1a 완료·revert / A1b·A2 미착수)

### 2-1. A1a — GC 저주소 예약 (구현→측정→revert)

| | |
|---|---|
| 대상 파일 | `gc/unix/gcenv.unix.cpp` (`VirtualReserveInner`/`VirtualReserve`) |
| config | `DOTNET_GCLowVirtualAddress` = `0` 비활성 / `1` preferred(폴백 허용) / `2` strict(진단용) |
| 구현 | 하위 4GB 내 bump-hint 2-pass 탐색, `MAP_FIXED_NOREPLACE`(Linux 4.17+), 구버전 커널의 advisory 힌트는 결과 range check로 처리 |
| 게이트 | `HOST_64BIT`만 (아키텍처 중립) → **linux-x64에서 그대로 검증 가능**했다 |
| ARM64 제약 | Linux ARM64는 `MAP_32BIT` 미지원 → 주소 힌트 기반 구현이 유일 |

**필수 동반 설정 (중요 발견):** 현대 CoreCLR GC는 regions 방식으로 **단일 연속 범위**를 예약하며
기본값이 약 **61 GiB**였다. 4GB에 들어갈 수 없으므로 저주소 모드는 반드시 `DOTNET_GCRegionRange`
제한과 함께 써야 한다. 제한 없이 strict를 켜면
`GC heap initialization failed with error 0x8007000E`로 기동 실패한다.

### 2-2. A1a 측정 결과 (linux-x64) — 기대와 달랐다

**불변식:** `GCHandle.AddrOfPinnedObject()`로 20회 probe (gen0/gen1/LOH 혼합)

| 설정 | 결과 |
|---|---|
| 기본(비활성) | 20/20 **고주소** (`0x7d9f…`) |
| `=1` + 3GB range | **20/20 4GB 아래** |
| `=2` strict + 3GB range | **20/20 4GB 아래**, 폴백 0 |

**기능:** 8스레드 GC 스트레스(총 2058MB, 전 크기 클래스 16B~3MB) 무결성 오류 **0** — 3개 모드 전부
PASS. R2R 이미지 생성 후 `DOTNET_ReadyToRun=1/0` 양쪽 PASS. 성능 168–189ms로 모드 간 유의차 없음.

**메모리:** 동일 워크로드, `/proc/self/status`

| 설정 | VmPeak (가상) | VmHWM (물리) |
|---|---|---|
| 기본 (61GiB range, 고주소) | **65.0 GB** | 401 MB |
| `GCRegionRange=3GB` 만 | **6.84 GB** | 357 MB |
| `GCRegionRange=3GB` + 저주소 strict | **6.56 GB** | 392 MB |

**정직한 평가:**

1. 가상 주소는 65 GB → 6.6 GB (~90% 감소). 단 **절감의 대부분은 `GCRegionRange` 제한에서 오고**
   저주소 배치 자체의 기여는 작다 (6.84→6.56).
2. **물리 메모리는 개선되지 않았다** (357–392 MB, 노이즈 범위). GC 부기 테이블은 예약 범위가 아니라
   **실제 사용량에 비례해 lazy commit**되므로 예약을 줄여도 RSS가 줄지 않는다.
3. → 명세 §8의 "Phase A 효과 = GC 부기 예약 감소"는 **물리 절감이 아니라 가상 주소 절감**으로
   정정되어야 한다.

**로드맵에 주는 의미:** Phase A의 가치는 **(a) Phase B 전제 불변식 확립**, **(b) 거대 VA 예약 회피**
(엄격한 overcommit/`RLIMIT_AS` 환경에서 기동 실패 방지)로 재정의된다. **직접적 RSS 절감 수단이
아니다.** Phase B Go/No-Go 판단 시 "A에서 이미 메모리가 줄었다"는 가정을 쓰면 안 된다.

### 2-3. `qemu-user -R` 발견 (초기 판단 정정)

처음에 "OS/에뮬레이터 레벨로 VA를 32bit로 제한하는 방법은 없다"고 판단했으나 **틀렸다.**
`personality(ADDR_LIMIT_32BIT)`·`setarch --32bit`·Docker cgroup·`ulimit -v`만 시험하고
**qemu-user의 `-R`(`QEMU_RESERVED_VA`)을 확인하지 않았다.**

측정 결과 (`DOTNET_GCLowVirtualAddress` **미설정**, 즉 저주소 코드 OFF):

- GC 힙 probe 20/20 4GB 아래
- 큰 익명 매핑 **전부** 4GB 아래. **glibc malloc 아레나(63MB 단위)까지 포함**되며 최상단이 정확히
  `0x100000000`에서 끝난다.

| | QEMU 테스트 | 실기기(rpi4 arm64) |
|---|---|---|
| `qemu -R` | ✅ 완전한 32bit VA (malloc 포함) | ❌ 해당 없음 |
| 런타임 LOWVA 코드 | 불필요 | 필요(또는 커널 레벨 대응) |

→ **이것이 A1a 코드를 revert한 이유이자, Stage B에 유리한 이유다.** `-R` 환경에서는 malloc까지
32bit에 들어가므로 "압축 대상을 GC 힙·loader heap으로 한정" 제약이 QEMU 테스트에서는 사라진다.
단 실기기에서는 다시 성립하므로 압축 대상은 런타임이 배치를 제어하는 영역으로 한정하는 것이 안전하다.

### 2-4. A1b / A2 / Phase C — 미착수

| 단계 | 목표 | 대상 코드 |
|---|---|---|
| **A1b** | loader/코드 힙(PAL) 저주소화 → `MethodTable` 포인터까지 불변식 확장 | `pal/src/map/virtual.cpp` — `VIRTUALReserveMemory`(:560), `TryReserveInitialMemory`(:1609), **"4GB 아래를 피하라" 휴리스틱(:1670-1674)을 저VA 모드에서 반전**. **메커니즘은 실기기에서 검증됨** — 저주소 힌트와 `MAP_FIXED_NOREPLACE` 둘 다 동작한다(§3-3) |
| **A2** | JIT 절대주소 적재를 `movz`+3×`movk`(4명령) → 1~2명령 | ARM64용 `FitsInAddrBase`/`AddrNeedsReloc` 신설 (`jit/gentree.cpp`, `gentree.h:3380`). AMD64·RISCV64 구현 참고. 적재부(`codegenarm64.cpp:2199-2256`)는 상위 0 halfword를 이미 건너뛰므로 관건은 JIT가 reloc 대신 짧은 인코딩을 고르게 하는 것 |
| **Phase C** (선택) | 네이티브 interop 저주소 트램폴린 | 코드는 thunk(`ldr x16,=target; br x16`, ELF PLT와 동형)로 해결 가능. **데이터 포인터는 핸들 테이블 필요(고비용)** |

**A2는 물리 절감이 유효한 유일한 Phase A 항목이다** — 커밋되는 코드가 실제로 줄어든다 (주로 non-AOT).

**Phase C의 필수 제약:** 64bit 프로세스에서 `IntPtr.Size == 8`은 공개 관찰 가능 값이며 blittable
레이아웃·마샬링·`Marshal.SizeOf`가 이를 전제한다. → **네이티브 포인터 값은 8B 유지**, 트램폴린으로
"저주소 도달성"만 확보. 저장 폭 축소는 하지 않는다. 네이티브 포인터는 interop 표면에만 있어 개수가
적으므로 메모리 이득이 작다 → **선택 항목.**

---

## 3. Stage B — 포인터 압축 (진행 중)

### 3-1. 완료된 것

| # | 항목 | 무엇 | 검증 |
|---|---|---|---|
| **B0** | 불변식 증명 | `CompressedPtr<T>` 저장 타입 + 4GB 검사기 | GC 힙 경계·대표 MT 주소 probe |
| **B1a** | 객체 헤더 MT 슬롯 4B | 4B 슬롯 + **명시적 4B 패딩**(`Object::m_mtPadding`) → `sizeof(Object)` 8 유지 → 나머지 레이아웃 전부 불변 | lowvastress 무결성 0 |
| **B3** | MethodTable 필드 3개 4B | `m_pParentMethodTable`/`m_pModule`/`m_pAuxiliaryData` | 4개 계약 층 정렬, 641 테스트 |
| — | 스위치 동기화 | 단일 MSBuild 속성이 CMake 정의 + CoreLib C# 심볼 동시 구동 | ON/OFF 전환 실측 |
| — | 힙 census | `DOTNET_CompressedPtrHeapCensus` | §4 수치 |
| — | 테스트 스위트 경로 | `CodeGenBringUpTests` 641개를 qemu 저VA에서 실행 | 641/641 |
| **B2-1** | Zero Extension 전제 실증 (§5-2) | 참조 슬롯 단위 검사 | 242k 슬롯 위반 0 |
| **B2-2** | JIT 접근 폭 축소 | 힙 참조 load/store 4B (저장 폭은 8B 유지) | 47/53 좁혀짐, 641 통과 |

**B1a 설계 요점:** 참조가 8B인 동안에는 MT만 줄여도 그 4B가 절감이 아니라 **패딩**이 된다(JVM의
compressed oops가 klass 포인터와 참조를 함께 압축하는 이유와 같다). 패딩을 명시적으로 두어
`sizeof(Object)`를 8로 유지하면 미러 구조체·binder 오프셋·JIT·어셈블리가 전부 불변이라 압축 슬롯
기계장치만 독립 검증할 수 있다. **패딩 제거는 B2와 함께.**

**B3의 4개 레이아웃 계약 층** (하나만 놓쳐도 기동 중 SIGSEGV):

1. C++ `MethodTable` 구조체 (`vm/methodtable.h`)
2. `SIZEOF__MethodTable_` = **vtable 시작 오프셋**
3. arm64 `asmconstants.h` `OFFSETOF__MethodTable__m_pPerInstInfo` (0x38→0x30)
4. **CoreLib 관리 미러** (`RuntimeHelpers.CoreCLR.cs`) ← 가장 예상 밖이었고 실제 크래시 원인

**JIT은 무수정.** vtable 오프셋을 런타임에 EE에 질의하고(`getMethodVTableOffset`), JIT 소스에
MethodTable 필드 오프셋 하드코딩이 없다(`grep offsetof(MethodTable` → 0건).

**접근자 시그니처 유지 전략의 효과 (실측):** 저장만 바꾸고 `GetMethodTable()` 시그니처를 유지한
결과 에러 86 → 0, 호출부 **1133곳 무수정**. CoreLib 미러도 읽기 전용 사용처 28곳을 필드→프로퍼티
치환으로 무수정 통과.

### 3-2. 남은 작업

| 단계 | 내용 | 동작 보존? | 상태 |
|---|---|---|---|
| **3** | write barrier의 참조 저장 4B화 — arm64는 `vm/arm64/patchedcode.S`의 `stlr x15, [x14]` 등 소수 명령 + GC shadow(`str x15, [x12]`) | ✅ 독립 검증 가능 | ✅ **완료** (2026-08-04, §4-6) |
| **3b** | 배열 인덱스 shape의 **접근 폭만** 4B화. ⚠️ **스케일 8→4는 3b가 아니라 4단계다** — 스케일은 요소 stride(=레이아웃)이므로 슬롯이 8B인 동안 바꾸면 잘못된 요소를 가리킨다(§5-4b) | ✅ 단 스케일 불변 조건에서만 | 미착수 |
| **4** | GC 스캔 stride + **shape별 저장 폭 축소** (VM 레이아웃 + CGCDesc) + **배열 스케일 8→4**. **절감은 여기서 나온다** | ❌ R2R 무효화 | 미착수 |
| 4순위 | 남은 로더 힙 포인터 (`aux::m_pLoaderModule`이 가장 쉬움 — 접근자 2개뿐) | ✅ | 미착수 |

### 3-3. ✅ 실기기 검증 (2026-08-03) — rpi4 / Tizen 11.0 aarch64

`tizen-device-flash-dude` 스킬로 rpi4를 재이미징한 뒤 실기기에서 검증했다.

| 항목 | 값 |
|---|---|
| OS | **Tizen 11.0 / Unified 11.0.0 (aarch64)**, build `20260802.230107` |
| 커널 | `6.12.80-arm64-rpi4-v8` |
| **glibc** | **2.40 — 우리 arm64 rootfs와 일치** (§5-6의 버전 정합성 위험 해소) |
| RAM | 3779 MB |

#### 결과

| 구성 | 앱 6개 | `CodeGenBringUpTests` | `Loader` | `GC` | 4GB 불변식 |
|---|---|---|---|---|---|
| **OFF, shim 없음** (기준선) | PASS | **641/641** | **390 / 2 fail** | **104 / 1 fail** (5회 반복 전부 동일) | ❌ 성립 안 함 (정상) |
| **OFF + shim** (대조군) | — | **161/161** | — | — | ✅ OK |
| **ON(MTFields+Refs) + shim** | PASS | **641/641** | **390 / 2 fail** | **104 / 1 fail** (6회 중 5회, 아래 미판정 항목 참조) | ✅ OK |
| ON, shim 없음 | `compressedptr.h:58` assert로 즉시 중단 | — | — | — | ❌ |

**핵심은 OFF와 ON+shim이 실패 목록까지 완전히 동일하다는 것이다** — 압축이 Loader/GC에 회귀를
만들지 않는다. 그리고 OFF+shim 대조군도 통과하므로 ON의 통과가 "shim이 무언가를 감춰서"가 아니다.
shim 없는 ON은 조용히 깨지지 않고 **정확한 진단과 함께 즉시 멈춘다** — QEMU에서 했던 음성 대조가
실기기에서 동일하게 재현된다.

##### 공통 실패 3건은 전부 환경 요인이며 압축과 무관하다

| 실패 | 원인 |
|---|---|
| `Loader/AssemblyDependencyResolver/AssemblyDependencyResolverTests` | hostpolicy 필요 — `-skipnative`로 빌드에서 제외됨 |
| `Loader/NativeLibs/FromNativePaths` | 네이티브 라이브러리 필요 — 동일 |
| `GC/API/Frozen` | **우리 하네스가 `DOTNET_GCRegionRange=1GB`로 고정**해서 OOM. 이 테스트는 그보다 큰 영역이 필요하다 |

`GC/API/Frozen`은 OFF 빌드로 조건을 분리해 확정했다:

| 구성 | `GCRegionRange=1GB` | `3GB` | 미설정 |
|---|---|---|---|
| OFF, shim 없음 | OOM (101) | **PASS** | **PASS** |
| ON + shim | OOM (101) | 런타임 초기화 실패 — **shim 용량 한계**(아래) | — |

→ 양쪽 구성에서 동일하게 실패하므로 **압축 회귀가 아니다.** 다만 이 조사에서 shim의 상한이 드러났다.

##### ⏳ 미판정: GC 트리의 간헐 실패 1건 (ON 6회 중 1회, 이름 미상)

ON+shim 실행 한 번이 **103 / 2 fail**로 나왔다(나머지는 전부 104 / 1). `Frozen` 외에 실패가 하나
더 있었는데 **그 이름을 로그 truncate(`head -3`)로 유실했고 이후 재현되지 않았다.**

반복 실험 결과 (`tests/gc-flaky-compare.sh`):

| 구성 | 실행 | `104/1` (Frozen만) | `103/2` (이름 미상) |
|---|---|---|---|
| ON + shim | 6회 | 5 | **1** |
| OFF | 5회 | 5 | 0 |

**ON에서만 관측됐지만 이것은 통계적으로 의미 없는 차이다.** 실패율이 양쪽 동일하게 1/6이라고
가정해도 OFF 5회가 전부 깨끗할 확률이 `(5/6)^5 ≈ 40%`다. 지금 데이터는 "압축 관련 경합"과
"환경/타이밍 요인"을 **구분하지 못한다.**

> ⚠️ 진행 중 두 번 오판했다. 기록해 둔다:
> 1. 이 차이를 "shim 폴백 차단 수정의 결과"로 해석했으나 **틀렸다** — 문제의 실행과 그 전후가 모두
>    같은 수정판 shim이었다. 시간적 인접성으로 인과를 붙인 오류.
> 2. ON에서만 나왔다는 사실을 압축 혐의로 읽으려 했으나, 위 확률 계산이 그것을 지지하지 않는다.

**판정하려면 양쪽 20회 수준의 반복이 필요하다**(회당 약 7분 → 구성당 약 2.5시간). 그리고 재발 시
**반드시 실패 이름과 전체 로그를 남겨야** 한다 — 이번에 유일하게 되돌릴 수 없었던 손실이 그것이다.
후보 가설은 §7의 미확인 항목인 `Interlocked`/volatile 참조의 4B/8B CAS 혼재.

```bash
arm64-low-va-memory-opt/tests/gc-flaky-compare.sh on  20
arm64-low-va-memory-opt/tests/gc-flaky-compare.sh off 20
```

> `off` 모드는 호스트 `artifacts/bin/coreclr/linux.arm64.Checked`가 **OFF 빌드**라고 가정한다.
> 확인: `strings -a <testhost>/System.Private.CoreLib.dll | grep -c _parentMethodTableCompressed`
> → `0`이어야 OFF. `on` 모드는 `/home/clamp/Work/dotnet/on-build-backup/`의 보관본을 쓴다.
>
> ⚠️ **2026-08-04 현재 호스트 artifacts는 ON 빌드다**(3단계 검증 때문). 즉 `off` 모드를 지금 그대로
> 돌리면 **ON 빌드를 "off"로 라벨링해 측정**한다. `off` 비교 전에 반드시 플래그 없이 재빌드할 것.
>
> ⚠️ **`strings`는 호스트에서만 쓸 수 있다 — 디바이스(Tizen)에는 `strings`가 없다.** 디바이스에서
> ON/OFF를 확인하려면 파일 크기(`stat -c%s`)를 호스트와 비교하거나 `ValidateCompressedPtr=1`의
> MethodTable 레이아웃 보고를 쓴다. 디바이스에서 `strings | grep -c`를 돌리면 빈 파이프 때문에
> 조용히 `0`이 나와 **OFF로 오판하게 된다**(이번에 실제로 겪었다).

##### ⚠️ shim의 용량 상한 — 저주소 창은 약 3.75GB뿐

`GCRegionRange=3GB`를 주면 `LOWVASHIM_VERBOSE=1`이 `no low space for 3145732 KB`를 찍는다.
`[0x10000000, 4GB)` 창에 3GB GC 영역이 다른 매핑과 함께 들어가지 못한다.

**초기 구현은 이때 고주소로 폴백했고, 압축 빌드가 그 포인터를 잘라 segfault가 났다.** 조용한 손상은
최악이므로 기본 동작을 **실패(`MAP_FAILED`/`ENOMEM`)** 로 바꿨다 — segfault(139) → 초기화 실패(255).
옛 동작이 필요하면 `LOWVASHIM_ALLOW_HIGH=1`.

##### 🔧 하네스 결함: `CORE_ROOT`를 export해야 한다

Loader 첫 실행은 **352 통과 / 40 실패**였는데 실패 40건 전부
`/corerun: No such file or directory`였다. **out-of-process 테스트**(Loader가 생성된 `.sh`로 자식
프로세스를 띄운다)가 `$CORE_ROOT/corerun`을 직접 호출하는데, `corerun -c <dir>`만 주고 환경변수를
export하지 않아 `/corerun`으로 해석된 것이다. export한 뒤 **352 → 390**.

`run-lowva-coreclrtest.sh`(QEMU)에도 같은 결함이 있어 함께 고쳤다 — `CodeGenBringUpTests`에는
out-of-process 테스트가 없어서 드러나지 않았을 뿐이다.

> 부수 확인: ON+shim에서 out-of-process 자식들도 통과했다 → **`LD_PRELOAD`가 자식 프로세스로
> 상속**되어 자식의 압축 런타임도 저주소를 얻는다. 상속되지 않았다면 자식이 assert로 죽었을 것이다.

측정된 실제 주소:

```
shim 없음:  GC heap 0xffff0c000000–0xffff4c000000   MT 0xffff5d144c70    → FAILED
shim 적용:  GC heap 0x0000000050400000–0x90000000   MT 0x0000000010054c70 → OK
```

참조 슬롯 단위(`DOTNET_CompressedPtrHeapCensus=2`)로도: shim 없으면 **140,488개 전부** 4GB 위,
shim 적용하면 **전부 32비트에 맞고 위반 0**.

#### 🔑 커널이 무엇을 허용하는가 — `tests/vaprobe`로 직접 측정

이것이 이번 검증의 가장 재사용 가치가 큰 산출물이다. A1a/A1b를 설계하기 전에 반드시 알아야 하는 값:

| 수단 | 결과 |
|---|---|
| 기동 시 기존 매핑 16개 | **전부 4GB 위** (stack `0xfffff270f000`) — 이미 매핑된 것은 못 옮긴다 |
| 평범한 `mmap(NULL)` — 지금 GC가 하는 것 | `0xffff80460000` → **4GB 위** |
| 저주소 **힌트** `mmap(hint=0x40000000)` | `0x40000000` → **✅ 존중됨** |
| **`MAP_FIXED_NOREPLACE`** 0x4/0x8/0xc0000000 | **✅ 전부 성공** |
| `personality(ADDR_LIMIT_32BIT)` | 커널이 받아들이지만 **효과 없음** (여전히 4GB 위) |

→ **프로세스 전역 제한은 arm64에서 불가능하다** (명세 §7의 "전략 1 비권장"이 실측으로 확인됨).
**런타임이 자기 예약을 명시적으로 배치하는 것만 유효하고, 그것은 실제로 가능하다.**
즉 A1a(GC)/A1b(PAL loader·code heap)가 유일한 길이며 메커니즘은 검증됐다.

#### `tests/lowvashim` — 디바이스판 `qemu -R`

`MAP_FIXED_NOREPLACE`가 동작하므로, `mmap`을 가로채 주소 무관 익명 예약을
`[0x10000000, 4GB)`로 몰아주는 `LD_PRELOAD` shim을 만들었다.

```bash
LD_PRELOAD=/opt/vatest/liblowvashim.so corerun ...
LOWVASHIM_VERBOSE=1   # 몰아준 예약을 stderr로 로그
LOWVASHIM_BASE=0x10000000
```

**shim은 테스트 하네스이고 제품 메커니즘이 아니다.** 가치는 A1a/A1b 코드를 쓰기 **전에**
"저주소에서 런타임이 정상 동작하는가"를 분리 검증한 것이다 — 이제 그 질문은 답이 나와 있고
"예약을 저주소에 놓는 방법"만 남는다. 다루지 않는 것: 이미 매핑된 실행 파일·`.so`·스택(압축 대상이
아니므로 무관), `MAP_FIXED` 요청(호출자가 주소를 지정한 것), **I3 — `PROT_EXEC` 매핑도 몰지만 그
불변식은 검증되지 않았다.**

---

## 4. 개선 수치 (실측 vs 투영 구분)

### 4-1. 실측 — MethodTable 크기 (타입당, 로더 힙)

`DOTNET_ValidateCompressedPtr=1`, 동일 앱·환경:

```
OFF: MethodTable header  72 bytes, m_pPerInstInfo@0x38  (full width fields)
ON : MethodTable header  64 bytes, m_pPerInstInfo@0x30  (compressed fields)
```

Checked 72→64B, Release 64→56B. 포인터 3개 24B→12B지만 뒤따르는 8B 정렬 union이 4B를 패딩으로
되돌려 **실절감 8B**. 로더 힙만 영향하며 객체 수가 아니라 **타입 수에 비례**한다.

### 4-2. 실측 — 객체당 절감: 모든 shape에서 0%

`sizemeasure` (`GC.GetAllocatedBytesForCurrentThread()` 전후 차, 배치 20000):

| shape | 현재(실측) | B1 투영 | B1+B2 투영 |
|---|---|---|---|
| `object` | 24 | 24 (0%) | 24 (0%) |
| `class{1 ref}` | 24 | 24 (0%) | **16 (33.3%)** |
| `class{4 refs}` | 48 | 48 (0%) | **32 (33.3%)** |
| `class{2ref+int+long}` | 48 | 48 (0%) | **40 (16.7%)** |
| `class{4 ints}` | 32 | 32 (0%) | 32 (0%) |
| `object[16]` | 152 | 152 (0%) | **88 (42.1%)** |
| `int[16]` | 88 | 88 (0%) | 88 (0%) |
| `object[256]` | 2072 | 2072 (0%) | **1048 (49.4%)** |
| `string(16)` | 56 | 56 (0%) | 56 (0%) |

**B1 단독은 모든 shape에서 0%다.** 4B를 아껴도 8B 정렬로 반올림되며 되돌아온다.

### 4-3. 투영 — 라이브 힙 census (B2 Go/No-Go 근거)

`DOTNET_CompressedPtrHeapCensus=1`, arm64 Checked, 저VA:

| 워크로드 | 라이브 바이트 | 객체 수 | B1 | **B1+B2** |
|---|---|---|---|---|
| `heapcensus startup` — 프레임워크 기저 힙만 | 42,264 | 446 | 2.5% | **18.3%** |
| `lowvastress` 정상 상태 | 114,328 | 1,283 | 2.2% | **16.1%** |
| `heapcensus collections` — 참조 리치 | 2,231,352 | 40,453 | 0.0% | **22.0%** |
| `heapcensus mixed` — 앱 코드 유사 | 3,889,360 | 75,635 | 1.6% | **20.4%** |
| `heapcensus arrays` | 722,208 | 21,695 | 0.1% | 12.8% |
| `heapcensus text` — 문자열/버퍼 위주 | 3,370,840 | 22,457 | 0.0% | **2.8%** |

shape별 (mixed):

| shape | 힙 점유 | B1+B2 절감 |
|---|---|---|
| 참조를 가진 객체 | 31.0% | **33.3%** |
| 참조 배열 | 28.8% | **29.5%** |
| 참조 없는 객체 | 18.6% | 0% |
| 문자열 | 12.0% | 13.2% |
| 값 배열 | 9.7% | 0% |

**결론 4개:**

1. B2 절감은 워크로드에 따라 **2.8% ~ 22%, 8배 차이**. 문자열/버퍼 지배 워크로드에서는 3% 수준이라
   B2 비용이 정당화되지 않는다. → **실제 앱을 먼저 재야 한다.**
2. 프레임워크 기저 힙만으로도 18.3% → 작은 앱일수록 상대 이득이 크다.
3. 절감 관점 우선순위는 **참조 필드 > 참조 배열**. 원래 계획(배열 먼저)은 shape별 비율만 본
   판단이었고 실제 힙에서는 필드가 점유·절감 모두 크다. 단 위험도는 배열이 낮다.
4. "참조 없는 객체 + 값 배열"이 mixed에서 28%를 먹고 절감 0% → **손댈 수 없는 상한이 존재**한다.

### 4-3b. ✅ 실측 — 실제 Tizen 앱 11개, plateau 검증 완료 (2026-08-04, rpi4 aarch64) — B2 Go/No-Go 확정

위 4-3의 수치는 전부 합성 워크로드(heapcensus 도구)였다. 여기서는 `/work/dotnet/apps`의 실제 샘플
앱 **11개 전부**를 **경로 A**(census를 포크에 포팅 → `libcoreclr.so` 단독 교체, NI 우회)로 측정했다.
방법과 함정은 §실행 C 및 아래 "계측 결함 5개" 참조.

**측정 조건:** 앱마다 hydra pool을 재활용해 콜드 스타트 → 실행 → **10초 간격 강제 blocking gen2 GC를
10~11회** → 각 GC 직후 census. **마지막 3개 샘플이 바이트 단위로 동일한 것을 plateau 근거로 삼았다**
(11개 앱 전부 충족). 라이브 힙은 gen2 GC 직후의 진짜 라이브셋이다.

| 앱 | 라이브 힙(KB) | 객체 수 | B1 | **B1+B2** | 절감(KB) | PSS(MB) | **절감/PSS** | 힙/PSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| VisualSample | 421 | 4,299 | 1.2% | 24.0% | 101 | 49.4 | **0.20%** | 0.83% |
| MovieLibrary | 429 | 4,391 | 1.2% | 25.0% | 107 | 42.8 | **0.25%** | 0.98% |
| MediaHubSample | 503 | 5,343 | 1.1% | 24.8% | 125 | 34.1 | **0.36%** | 1.44% |
| Xamarin.Hello.F_HUB | 530 | 6,011 | 1.2% | 22.8% | 121 | 40.0 | **0.30%** | 1.29% |
| System_info | 599 | 7,025 | 1.2% | 23.3% | 140 | 41.9 | **0.33%** | 1.40% |
| ApplicationControl | 924 | 11,942 | 0.9% | 24.8% | 229 | 47.8 | **0.47%** | 1.89% |
| ChannelList | 991 | 10,870 | 0.8% | 26.3% | 261 | 36.8 | **0.69%** | 2.63% |
| AppCommon | 1,072 | 14,400 | 0.8% | 25.5% | 273 | 45.3 | **0.59%** | 2.31% |
| Settings | 1,155 | 15,380 | 0.8% | 25.5% | 294 | 45.7 | **0.63%** | 2.47% |
| Puzzle | 1,388 | 18,887 | 0.7% | 25.7% | 357 | 47.0 | **0.74%** | 2.89% |
| FirstScreen | 1,674 | 19,591 | 0.7% | 27.8% | 465 | 52.3 | **0.87%** | 3.13% |

산식: 절감 = 라이브 힙 × (B1+B2)%. 절감/PSS = 절감 ÷ PSS. PSS는 `memps -v` field[6].

| 지표 | 범위 (중앙값) |
|---|---|
| B1 단독 | 0.7% ~ 1.2% — **거의 0. 이미 구현된 B1a/B3만으로는 절감이 없다** |
| **B1+B2** | **22.8% ~ 27.8%** (25.0%) — 합성 워크로드 투영(18~22%)과 일치·소폭 상회 |
| **절감/PSS** | **0.20% ~ 0.87%** (0.47%) |
| 절감/P(DATA) (가장 유리한 분모) | 0.29% ~ 1.23% (0.73%) |
| **라이브 힙/PSS** | **0.83% ~ 3.13%** (1.89%) ← **B2의 물리적 상한** |

**결론 5개:**

1. **B2 절감률 자체는 실제 앱에서 재현된다** (22.8~27.8%). 메커니즘·투영은 옳았다.
2. **그러나 관리 라이브 힙이 앱 PSS의 0.83~3.13%밖에 안 된다.** GUI 위주 Tizen 앱은 실제 콘텐츠가
   TBM/DALI 네이티브 그래픽 버퍼와 JIT/NI 코드에 있고, 관리 힙에는 골격(뷰모델·리스트·바인딩)만
   남는다. MovieLibrary는 `GEM(PSS)=77 MB`로 그래픽 사용이 가장 많은데도 관리 힙은 429 KB뿐이다.
3. **→ `B2가 관리 힙을 100% 없앤다 해도`(물리적으로 불가능) 상한이 PSS의 3.13%다.** 현실적
   25%로는 **0.20~0.87%**. 절감 절대량은 최대 465 KB(FirstScreen)다.
4. **손익분기:** 절감이 PSS의 1%가 되려면 라이브 힙 ≈ **1.8 MB**(거의 도달), 2%면 **3.6 MB**
   (측정 최대의 2.1배), 5%면 **9 MB**(5.4배)가 필요하다. `DOTNET_GCHeapHardLimit=400 MB`(런처 강제)
   대비 힙이 0.4% 수준이라 앱들은 관리 힙 압력이 사실상 없다 — 2.5 GB tmpfs를 채워 메모리 압력을
   가해도 자연 gen2 GC가 유도되지 않았다.
5. **⚠️ No-Go — 이 앱 스펙트럼에서는.** 4단계(저장 폭 실축소)는 R2R 무효화 + `WriteBarriers.S`
   두 벌 + CGCDesc/GC stride + 배열 스케일까지 건드리는 고위험 대공사인데(§3-2, §7-5b/5c),
   얻는 것이 PSS의 0.5% 안팎이다. **비용/효과가 성립하지 않는다.**
   판단을 뒤집을 수 있는 유일한 조건은 **라이브 힙이 3~9 MB인 실제 프로덕션 앱**이며, 이번 11개
   샘플에는 그런 앱이 없다. → 다음 액션은 "4단계 착수"가 아니라 **"무거운 프로덕션 앱 확보 후
   재측정"** 이다. 재측정 인프라는 이제 전부 갖춰져 있어 앱만 있으면 앱당 약 2분이면 된다.

> 보정 고려 사항 — 어느 쪽도 결론을 바꾸지 않는다:
> - **분모**: PSS(34~52 MB) 대신 가장 유리한 P(DATA) private-dirty(18~38 MB)를 써도 0.29~1.23%.
> - **`DOTNET_ReadyToRun=0`**: 측정은 JIT 전용으로 했다. JIT 코드가 private dirty를 부풀려 분모를
>   키우므로 프로덕션(NI 사용) 대비 **불리한** 방향이다. 보정하면 비율이 다소 오르지만 1% 미만.
> - **frozen object heap 미포함**: `DiagWalkHeap`은 gen2→gen1→gen0→LOH→POH를 걷지만 FOH는 빼므로
>   절감을 약간 **과소** 평가한다.

#### ⚠️ 계측 결함 5개 — 전부 "조용히 잘못된 수치"를 냈다 (재발 방지 필수)

이 측정은 **다섯 번 연속으로 그럴듯하지만 틀린 결과**를 냈다. 어느 것도 에러를 내지 않았다.

1. **Tizen `hydra` 프로세스 풀 + fork COW.** 런처는 `dotnet-hydra-loader`로 CoreCLR을 미리 fork해
   워커 풀(`process-pool <hydra`)을 유지하고, `app_launcher`가 앱을 요청하면 그 워커를 앱으로
   specialize한다(`launcher/NativeLauncher/hydra/hydra_main.cc`의 `precreate`에서
   `CoreRuntime::initialize(..., prefork=true)`로 EE를 먼저 띄운다). 자식은 부모 힙을 COW로
   물려받으므로, **앱이 자기 상태를 할당하기 전에 census를 찍으면 서로 다른 앱들이 바이트 단위로
   동일한 값**(2,672 objects / 320,728 bytes)을 낸다. 실제로 5개 앱이 그렇게 나왔다.
   → 기준점은 `coreclr_initialize_after_fork` → `CorHost2::InitializeAfterFork`(`vm/corhost.cpp`)다.
   측정 사이에는 `pkill -9 -f dotnet-hydra-loader; pkill -9 -f process-pool` 후 **`process-pool`이
   다시 뜰 때까지 기다려야** 한다(안 기다리면 `launch failed`). 앱도 `-k`로 확실히 죽여야 한다
   (살아 있으면 옛 `libcoreclr.so`를 쓰는 프로세스에 재접속된다).
2. **`census` 출력에 PID가 없으면 귀속이 불가능하다.** 여러 CoreCLR 프로세스가 동시에 살아 있고
   `s_seq`/`s_firstTick` 같은 static은 fork로 상속된다. **PID별 파일**(`/tmp/census.<pid>.txt`)로
   나누고 seq·경과시간을 함께 찍어야 시계열로 읽을 수 있다.
3. **`FinalizerThread::WaitForFinalizerEvent`는 주기 tick이 아니다.** `WAIT_TIMEOUT`에서
   `g_TriggerHeapDump`가 FALSE면 `break`로 내부 `while(1)`을 계속 돌 뿐 반환하지 않는다. 따라서
   `FinalizerThreadWorker`의 루프 몸통은 **finalization이 신호될 때만** 돈다.
   반대로 timeout 경로는 선행 `event->Wait(2000)`이 타임아웃해야 도달하는데, 바쁜 NUI/DALI 앱은
   2초보다 자주 finalization을 신호하므로 **거기에 도달하지 못한다**.
   → **두 곳 모두에서 호출해야 한다.** 한 곳만 쓰면 다른 쪽 워크로드에서 샘플이 0이 되는데,
   에러 없이 조용히 그렇게 된다(양방향으로 각각 관측했다).
4. **`DiagGCEnd`에서 GC 락을 잡는 API를 부르면 자기 데드락.** 교차검증용으로
   `IGCHeap::GetTotalBytesInUse()`를 넣었더니(그 함수는 `enter_spin_lock(&gc_lock)`을 한다)
   첫 gen2 GC에서 `dotnet-hydra-loader`가 `futex_wait_queue`에 걸려 **모든 앱 실행이
   `launch failed`** 가 됐다. dlog에는 `Start coreclr_prepare_before_fork`만 찍히고 Finish가 없다.
   힙 **걷기**(`DiagWalkHeap`)는 안전하지만 GC 상태 조회 API는 부르면 안 된다.
   복구는 백업 `libcoreclr.so` 복원 + pool kill.
5. **🔑 `DOTNET_*` 숫자 config 값은 16진수로 파싱된다.** `...ForceAfterMs=15000`은 `0x15000` =
   **86016 ms**로 읽힌다. 강제 GC가 15초가 아니라 86초마다 발동했고, 그 결과를 한동안 "우연한
   자연 GC"로 오해했다. 10초를 원하면 `2710`, 15초는 `3A98`로 써야 한다.
   → **새 숫자 knob을 넣으면 런타임이 실제로 파싱한 값을 먼저 찍어서 확인할 것.**

> 진단 방법: `MaybeForceCensusGC` 진입마다 `/tmp/censusdbg.<pid>.txt`에
> `interval/count/start/now/stopFork`를 찍게 하자 결함 5가 즉시 드러났다. 추론으로 6 사이클을
> 태우기 전에 이걸 먼저 했어야 했다.

### 4-4. 실측 — Zero Extension 전제 (§5-2, 설계 전체의 근거)

`DOTNET_CompressedPtrHeapCensus=2`:

| 워크로드 | 검사한 슬롯 | 4GB 초과 | 4B 정렬 위반 |
|---|---|---|---|
| startup | 309 | **0** | 0 |
| mixed | 140,466 | **0** | 0 |
| collections | 100,314 | **0** | 0 |
| lowvastress | 1,100 | **0** | 0 |

> `DiagWalkObject2`는 non-null 참조만 보고한다(mixed는 157,980 슬롯 중 140,466).

### 4-5. 실측 — B2 2단계 커버리지

`mtstress`, JitDump 계측:

| | 건수 | shape |
|---|---|---|
| **좁혀짐** | **47 / 53** | contained LEA load 23 / CNS_INT load 14 / LCL_VAR load 10 |
| full width 유지 | 6 | 스택 store 3 (`notHeap=1`) / 배열 인덱스 load 3 (`hasIndex=1`) |

남은 6건은 **의도적 제외**(스택은 8B 유지, 배열 인덱스는 스케일 변경이 3b 사안).

### 4-6. 실측 — 테스트 (QEMU + 실기기)

**B2 2단계 (2026-08-03):**

| 대상 | ON (양 플래그) | OFF (기본) |
|---|---|---|
| **QEMU** `JIT/CodeGenBringUpTests` × 4구성 | **641 / 0 failed**, exit 100 | 641 통과 |
| **QEMU** 앱 6개 | PASS | PASS |
| **실기기** `CodeGenBringUpTests` × 4구성 | **641 / 0 failed** (+ shim) | **641 통과** |
| **실기기** `Loader` | **390 / 2 fail** (+ shim) | **390 / 2 fail** |
| **실기기** `GC` | **104 / 1 fail** (+ shim, 6회 중 5회) | **104 / 1 fail** (5회 반복 전부 동일) |
| **실기기** 앱 6개 | PASS (+ shim) | PASS |

**B2 3단계 (2026-08-04) — write barrier 4B 저장:**

| 대상 | ON (MTFields+Refs+WB 3단계) | 판정 |
|---|---|---|
| **QEMU** `JIT/CodeGenBringUpTests` × 4구성 | **641 / 0 failed**, exit 100 | ✅ |
| **QEMU** 앱 6개 | PASS (mtstress / mirrorstress / lowvatest / lowvastress / refload / sizemeasure) | ✅ |
| **실기기** `CodeGenBringUpTests` × 4구성 | **641 / 0 failed** (+ shim) | ✅ |
| **실기기** `Loader` | **390 / 2 fail** (+ shim) | ✅ 2단계와 동일 |
| **실기기** `GC` | **104 / 1 fail** (+ shim) | ✅ 2단계와 동일 |
| **실기기** 앱 6개 | PASS (+ shim, `ValidateCompressedPtr=1`) | ✅ |

실패 목록이 2단계·OFF와 완전히 동일하다(환경 요인 3건) → write barrier 축소에 회귀 없음.

#### 🔑 어셈블리 변경의 "실제 반영" 검증법 — objdump 없이 (재사용 가치 높음)

`libcoreclr.so`는 **stripped**여서 심볼이 없고, 호스트 `objdump`는 aarch64를 모른다
(`can't disassemble for architecture UNKNOWN`). 3단계 검증 중 이 때문에 디스어셈블이 막혔다.
**해결: A64 명령을 직접 인코딩해 바이트 패턴을 세면 된다.** 명령이 고정 4바이트이므로 정확하다.

```python
# STLR: 64bit 0xC89FFC00 | Rn<<5 | Rt   /  32bit 0x889FFC00 | Rn<<5 | Rt
# STR (imm, offset 0): 64bit 0xF9000000 | Rn<<5 | Rt  /  32bit 0xB9000000 | Rn<<5 | Rt
# LDR (imm, offset 0): 64bit 0xF9400000 | Rn<<5 | Rt  /  32bit 0xB9400000 | Rn<<5 | Rt
# little-endian 4바이트로 바꿔 open(so,'rb').read().count(...) 한다.
```

3단계 실측 (신규 빌드 vs `on-build-backup`의 2단계 ON 빌드):

| 명령 | 2단계 빌드 | 3단계 빌드 | 해석 |
|---|---|---|---|
| `stlr x15,[x14]` | 11 | **1** | 10개 변종이 전부 바뀜 (남은 1은 `WriteBarriers.S`, 아래) |
| `stlr w15,[x14]` | 0 | **10** | 매크로 공유 그대로 10개 |
| shadow `str x15,[x12]` → `str w15,[x12]` | 10 → — | — → 10 | **`WRITE_BARRIER_CHECK`가 이 Checked 빌드에서 실제로 켜져 있음이 이걸로 증명된다** |
| `str x15,[x14]` (CheckedWB 힙 밖) | 8 | 8 | 의도대로 **8B 유지** |

즉 "변경이 반영됐다"를 테스트 전에 독립적으로 확정했다. §5-8-1의 교훈을 이 방법으로 이행한다.

Loader/GC의 공통 실패 3건은 전부 환경 요인(`-skipnative` 2건, 하네스의 `GCRegionRange=1GB` 1건)이며
**OFF와 ON+shim의 실패 목록이 동일**하다 → 압축 회귀 없음. 상세는 §3-3.

⏳ 단 GC 트리에 **미판정 간헐 실패 1건**이 있다(ON 6회 중 1회, 이름 미상). OFF 5회는 전부 깨끗했으나
그 차이는 통계적으로 유의하지 않다 — §3-3.

161개 테스트가 qemu에서 **5.3초** (merged runner가 프로세스 기동 비용을 분산).

**음성 대조 (양쪽에서 재현):** 같은 바이너리를 QEMU에서 `-R` 없이, 또는 실기기에서 shim 없이
돌리면 `compressedptr.h:58` assert가 정확히 걸린다 → 641개 통과가 유효한 4GB 불변식 아래의
결과임을 확인. 실기기에서는 **OFF+shim 대조군도 통과**하므로 shim이 무언가를 감춘 것이 아니다.

> ⏳ **RSS는 아직 비교하지 않았다.** `lowvastress`에서 ON `VmRSS 82MB` / OFF `182MB`로 보였지만
> GC 타이밍 편차일 수 있고, 현재 절감은 타입당 8B(수십 KB)뿐이라 이 차이는 다른 원인일 가능성이
> 크다. 단정하지 말 것 — 동일 조건 반복 측정이 필요하다.

---

## 5. 재발견에 며칠 걸리는 기술적 사실

### 5-1. 압축 가능성을 결정하는 **3개의 서로 다른 불변식**

| | 불변식 | 대상 | QEMU | 실기기 |
|---|---|---|---|---|
| **I1** | GC 힙 < 4GB | 객체 참조 (필드·배열·statics) | ✅ 증명 | ✅ **증명** (shim, §3-3) |
| **I2** | 로더 힙 < 4GB | MethodTable, EEClass, MethodDesc, aux data | ✅ 증명 | ✅ **증명** (shim, §3-3) |
| **I3** | **코드 힙(PCODE) < 4GB** | vtable 슬롯, 프리코드, 스텁 | ❌ | ❌ **증명 안 됨** |

`ValidateCompressedPtr`는 코드 주소를 probe하지 않는다. shim이 `PROT_EXEC` 매핑도 저주소로 몰지만
그것을 **검증한 적이 없고**, 제품 경로에서는 PAL의 코드용 1GB 슬랩 배치에 달려 있다(A1b 미착수).
**지금 vtable을 건드리면 검증되지 않은 불변식에 의존하게 된다** — validator를 코드 주소까지
확장하는 것이 선행 조건이다.

### 5-2. Zero Extension 성질 → 단계 분할이 가능한 이유

**Zero Extension 전제 = "힙에 저장된 모든 객체 참조 값의 상위 32비트가 0"이다.** 압축은 하위
4바이트만 잘라 저장하고, 복원은 그 4바이트를 64비트 레지스터로 로드하는 것뿐이다 — 하드웨어가 상위
32비트를 0으로 채우므로(zero extension) 원래 값이 그대로 복원된다. `inc/compressedptr.h`의 `Get()`이
캐스트 한 줄인 이유다.

> JVM의 compressed oops는 `base + (compressed << 3)`으로 base 덧셈과 shift가 필요하지만, 우리는
> 압축 범위가 0에서 시작하므로 둘 다 없다. → **압축형과 전체 폭 값의 숫자가 동일**하다.
>
> ⚠️ 자연법칙이 아니라 **환경이 강제한 것**이다. `qemu -R 0x100000000`이 만들어 준다. 실기기에서는
> 다시 만들어야 한다(§2-4 A1b).

값이 동일하고 리틀엔디언이므로 4바이트 압축형은 8바이트 슬롯의 하위 절반 그 자체다:

| 접근 폭 → 슬롯 폭 | 결과 |
|---|---|
| 8B → 8B | 정상 |
| **4B → 8B** | **정상** (읽기=하위 절반 / 쓰기 후 상위 0 유지) |
| 4B → 4B | 정상 |
| 8B → 4B | **오류** |

불건전한 조합이 하나뿐 → **접근 폭을 먼저, 저장 폭을 나중에 shape별로**가 성립하고 4B/8B 슬롯이
공존해도 안전하다. 스택·레지스터가 8B로 유지되므로 **GC info·스택워킹·EH를 건드리지 않는다**
(JIT이 GC 참조 지역변수를 프롤로그에서 0 초기화하므로 스택 슬롯 상위 절반도 0).

`ref object`/`Span<object>`의 byref가 스택/힙 어느 쪽도 가리킬 수 있다는 모호성도 이걸로 무해해진다.

> 이 성질이 **초기 판단("JIT `TYP_REF` 크기가 하나뿐이라 한 번에 다 해야 한다")을 뒤집었다.**

### 5-3. 🔑 `EA_GCREF`는 플래그가 아니라 **특수값**이다

여기서 3번 실패했다. `emit.cpp` `emitAllocInstr`:

```cpp
if (EA_IS_GCREF(opsz)) { id->idGCref(GCT_GCREF); id->idOpSize(EA_PTRSIZE); }
```

**GC 플래그가 붙으면 operand size가 무조건 8로 강제되고 크기 비트가 버려진다.**
`EA_GCREF = EA_GCREF_FLG | EA_PTRSIZE` 정의를 보고 "직교한다"고 판단한 것이 틀렸다. 결과:

1. 명령은 8바이트로 나가고(좁혀지지 않음),
2. `emitIns_R_R_I`는 이미 4바이트 기준으로 `imm = 8 >> 2 = 2`로 스케일했으므로,
3. 실효 오프셋이 `2 × 8 = 0x10`이 되어 **필드 오프셋이 배가되고 힙이 손상된다**.

**해결:** GC 플래그를 **버리고** 순수 `EA_4BYTE`로 낸다. 레지스터 GC 여부는 emit attr이 아니라
트리 타입에서 온다 — `genProduceReg` → `gcMarkRegPtrVal(reg, tree->TypeGet())`.

### 5-4. arm64는 접근 폭이 스케일 인덱스 주소 모드를 결정한다

`emitIns_R_R_R_Ext`의 `assert((shiftAmount == scale) || (shiftAmount == 0))`이고
그 위에서 `scale = (size == EA_8BYTE) ? 3 : 2`로 **접근 폭에서** 유도된다
(`jit/emitarm64.cpp:7507,7546`). 즉 4B 접근은 `lsl #2` 또는 `lsl #0`만 인코딩할 수 있다.
즉시 오프셋은 emitter가 재스케일하므로 무관.

### 5-4b. 🔑 스케일은 "접근 폭"이 아니라 "요소 stride"다 — 3b/4단계 분리의 근거

**스케일 8→4는 배열 요소 stride를 바꾸는 것, 즉 레이아웃 변경이다.** 슬롯이 아직 8B인 동안
스케일을 4로 바꾸면 요소 *i*의 주소가 `base + i*4`가 되어 **다른 요소를 읽고 쓴다**. 컴파일도 되고
assert도 안 걸리며 index 0에서는 정상 동작하므로 **조용히 힙을 손상시킨다.**

| stride(레이아웃) | 접근 폭 | 필요한 shift | 가능? |
|---|---|---|---|
| 8B | 8B | 3 | ✅ 현재 |
| 8B | **4B** | **3** | ⚠️ 스케일 인덱스 주소 모드로는 **불가**(§5-4) → 주소를 별도 명령으로 만들어야 함 |
| 4B | 4B | 2 | ✅ 4단계 (stride와 함께) |

→ **"스케일 8→4"는 3b가 아니라 4단계 항목이다.** 3b가 할 수 있는 것은 *스케일을 8로 둔 채* 접근
폭만 4B로 만드는 것뿐이다. 그것이 가능한지는 emitter가 어떤 형태를 내는지에 달려 있다:

`emitter::emitInsLoadStoreOp`(`jit/emitarm64.cpp:14963`)의 contained-LEA + index 분기 4가지:

| 조건 | 생성 코드 | 스케일 8 + 4B 접근 |
|---|---|---|
| `offset != 0` (배열의 정상 경로 — 헤더가 있어 dataOffset≠0) | `add tmp, base, index, lsl #3` → `ldr/str w, [tmp, #offset]` | ✅ **안전**. 주소 계산이 별 명령이라 접근 폭과 무관 |
| `offset != 0` large offset | `emitIns_R_R_R_I(ins, attr, ..., lsl, LSL)` | ⚠️ 미확인 — 같은 제약일 가능성 |
| `offset == 0 && lsl > 0` | `emitIns_R_R_R_Ext(..., lsl)` | ❌ §5-4 assert에 걸린다 |
| `offset == 0 && lsl == 0` | `add`/`_Ext` (스케일 없음) | ✅ 무관 |

→ 따라서 3b는 "`genIndirAddrDependsOnAccessSize`에서 `HasIndex()` 차단을 없앤다"가 **아니라**
"**스케일 인덱스 주소 모드를 쓰는 하위 경우만** 계속 차단한다"로 좁혀야 한다. 현재의 통짜 차단
(`addr->isContained() && indir->HasIndex()`)은 보수적이지만 **옳다** — 성급히 풀면 assert 또는
잘못된 주소가 된다.

> 이 사실이 최초 3b 계획의 핵심 동작(`lea->gtScale = 4`)을 **무효화했다.** 또한
> `genLeaInstruction`(`codegenarmarch.cpp:4232`)은 **contained가 아닌** 독립 `GT_LEA`용이라
> 배열 요소 *접근* 경로가 아니다 — 변경 지점 자체가 틀렸다.

### 5-5. 압축 불가 / 인코딩 재설계가 선행되어야 하는 것

| 대상 | 이유 |
|---|---|
| `MethodTable::m_encodedNullableUnboxData` | 64bit에서 **상위 32비트에 value field 크기를 패킹**(`(size << 32) \| offset`). 8B가 이미 다 쓰였다. `m_pInterfaceMap`과 union이라 이 슬롯은 사실상 제외 |
| `DynamicStaticsInfo::m_pGCStatics/m_pNonGCStatics` | 하위 비트를 초기화 플래그로 쓰는 `TADDR` (`ISCLASSNOTINITEDMASK`) |
| vtable 슬롯 / indirection | I3 미증명 + JIT이 가상 디스패치에서 직접 읽음 |
| interop 경계 네이티브 주소 | 명세 §7 / Phase C 제약대로 `IntPtr` full width 유지가 설계 |

### 5-6. rootfs / 실기기 함정

| 사실 | 내용 |
|---|---|
| **Ubuntu rootfs는 만들어지지 않는다** | 사내 프록시(`10.112.1.184:8080`)가 TLS를 인터셉트하는데 chroot 안에 사내 CA가 없어 apt가 전부 실패. **`build-rootfs.sh`는 이 실패를 무시한다**(909–912행이 exit code 미검사) → rootfs가 잘 만들어진 것처럼 보이지만 `/usr/include`가 빈다 |
| Tizen rootfs는 성공 | `tizen-fetch.sh`가 평문 HTTP(`download.tizen.org`)를 쓰므로 TLS 인터셉션에 걸리지 않는다 |
| ~~glibc 버전 정합성~~ → **해소** | arm64 rootfs는 Tizen 11.0 / glibc 2.40. rpi4를 Tizen 11.0 aarch64(`20260802.230107`)로 재이미징해 **디바이스도 glibc 2.40**이 되었고 바이너리가 그대로 로드된다. 원 위험: 바이너리는 빌드 시점보다 **낮은** glibc에서 로드되지 않으므로, 디바이스를 구 이미지(Tizen 9.0/glibc 2.30)로 되돌리면 다시 문제가 된다 |
| ⚠️ **정적 링크 불가** | rootfs에 `crtbeginT.o`/`libgcc.a`가 없어 `-static`이 실패한다. 또 clang이 `lib/gcc`를 찾는데 Tizen은 `lib64/gcc`라 `--gcc-toolchain`만으로는 링크가 안 된다. 디바이스용 작은 네이티브 도구는 이렇게 빌드한다: `G=$ROOTFS_DIR/usr/lib64/gcc/aarch64-tizen-linux-gnu/14.2.0; clang-21 --target=aarch64-linux-gnu --sysroot=$ROOTFS_DIR -B$G -L$G -L$ROOTFS_DIR/usr/lib64 ...` (동적 링크. 디바이스 glibc가 일치하므로 문제없다) |
| ⚠️ **디바이스 파티션 여유** | `/` 1.2 GB, `/opt` 1.3 GB, `/tmp` 1.9 GB(**tmpfs = RAM**). Core_Root(358 MB)+테스트를 올릴 곳은 **`/opt/usr/home/owner/media` (55 GB)** 다 |
| ⚠️ **장시간 측정 전 스케줄 무력화** | rpi4는 22:20 자체 리부트 타이머 + 22:30 나이트리 cron이 있다. 측정 전 `device-selfreboot.sh disable` + `install-cron.sh --remove`, 완료 후 복원. 미이행 시 측정이 중간에 깨진다 |

### 5-6b. 낡은 문서 표기 (혼동 주의)

`arm64-low-va-spec.md:41`과 `arm64-low-va-plan.md:148`에 남아 있는 **`DOTNET_ARM64LowVA`는 폐기된
이름**이다. 실제 구현 config는 `DOTNET_GCLowVirtualAddress`였고(그리고 그 코드는 지금 revert되어
없다), 아키텍처 게이트도 없앴다(`HOST_64BIT`만). `plan.md:68`에 개명 경위가 적혀 있다.

### 5-7. CoreCLR 테스트 스위트를 qemu에서 돌리기 위한 두 가지

1. **`XUnitLogChecker`가 블로커.** NativeAOT로 발행되는데 Tizen rootfs에 `crtbeginS.o`/`libgcc`가
   없다. CI 로그 후처리 전용이므로 `/p:IsXUnitLogCheckerSupported=false`로 끈다.
2. **개별 테스트 dll은 실행되지 않는다** ("Entry point not found"). xunit `[Fact]` 라이브러리이고,
   같은 디렉터리의 `JIT.<Area>_{d,do,r,ro}.dll`이 생성된 `Main`을 가진 merged runner다.
   **작업 디렉터리도 merged 디렉터리여야** 한다.

### 5-8. 프로세스 교훈

1. **코드젠 변경은 JitDisasm으로 실제 반영을 확인한 뒤에만 테스트 결과를 해석한다.** no-op 상태의
   "641개 통과"를 실제로 한 번 보고했다가 정정했다.
2. **shape별 이등분은 리빌드가 아니라 config로.** `JitCompressedRefsMask`를 만든 뒤 원인 분리가
   한 번의 실행으로 끝났다. 그 전까지 25분 빌드를 반복했다.
3. `ps | grep "eng/common/build.sh"`는 **자기 자신을 매칭**해 무한 루프가 된다. 로그의
   `Build succeeded|Build FAILED` 마커로 판정.

---

## 6. 환경 — 빌드와 실행

### 표준 빌드 (Checked 필수 — assert가 레이아웃 불일치를 잡아준다)

```bash
cd /work/dotnet/new/runtime
ROOTFS_DIR=/home/clamp/Work/dotnet/rootfs/arm64.tizen \
  ./build.sh clr.runtime+clr.corelib+clr.nativecorelib -arch arm64 -rc checked --cross \
  /p:FeatureCompressedMTFields=true /p:FeatureCompressedRefs=true

B=artifacts/bin/coreclr/linux.arm64.Checked
for CR in artifacts/bin/testhost/net11.0-linux-Release-arm64/shared/Microsoft.NETCore.App/11.0.0 \
          artifacts/tests/coreclr/linux.arm64.Checked/Tests/Core_Root; do
  for f in libcoreclr.so libclrgc.so libclrjit.so corerun; do cp -f "$B/$f" "$CR/$f"; done
done
```

기본(OFF)으로 되돌릴 때는 속성을 빼면 된다 — CMake 캐시의 stale ON이 자동으로 OFF로 덮어써진다.

### ⏱️ JIT만 고칠 때는 25분 → 2분

```bash
cd artifacts/obj/coreclr/linux.arm64.Checked && ninja libclrjit.so
cp artifacts/obj/coreclr/linux.arm64.Checked/jit/libclrjit.so <CORE_ROOT>/libclrjit.so
```

> ❌ **`cmake -D... .`를 직접 부르지 말 것.** 크로스 빌드 캐시의 arch 변수가 유실되어
> `Arch is . Only arm, arm64, ...`로 트리가 빌드 불가가 된다. 복구는 `build.sh` 재실행.

### 실행 A — QEMU 저VA 환경 (`-R`과 `GCRegionRange` 둘 다 필요)

```bash
arm64-low-va-memory-opt/tests/run-lowva.sh <app.dll> [NAME=value ...] [app args ...]
arm64-low-va-memory-opt/tests/run-lowva-coreclrtest.sh <merged-test-dir> [NAME=value ...]
```

### 실행 B — 실기기 (rpi4, sdb)

```bash
T=arm64-low-va-memory-opt/tests

# 1) 런타임 + 앱 배포 (/opt/vatest, 251 MB). --runtime-only 로 빠른 재배포
$T/deploy-device.sh

# 2) 앱 실행
$T/run-device.sh mtstress DOTNET_ValidateCompressedPtr=1

# 3) 저VA shim 빌드·배포 (압축 빌드를 실기기에서 돌릴 때 필수 — §3-3)
R=$ROOTFS_DIR; G=$R/usr/lib64/gcc/aarch64-tizen-linux-gnu/14.2.0
(cd $T/lowvashim && clang-21 --target=aarch64-linux-gnu --sysroot=$R -B$G -L$G -L$R/usr/lib64 \
   -O1 -shared -fPIC -o liblowvashim.so lowvashim.c -ldl)
sdb push $T/lowvashim/liblowvashim.so /opt/vatest/liblowvashim.so

# 4) CoreCLR 테스트 스위트 — media 파티션(55 GB)에 올린다
M=/opt/usr/home/owner/media/coreclr; B=artifacts/tests/coreclr/linux.arm64.Checked
sdb shell "mkdir -p $M/Core_Root $M/tests"
sdb push $B/Tests/Core_Root $M/Core_Root                       # 358 MB, ~2분 (큰 파일이라 3.2 MB/s)
for c in d do r ro; do \
  sdb push $B/JIT/CodeGenBringUpTests/JIT.CodeGenBringUpTests_$c $M/tests/JIT.CodeGenBringUpTests_$c; done
sdb shell "chmod +x $M/Core_Root/corerun"

# 5) 실행 (--shim 은 압축 빌드에 필수)
for c in d do r ro; do $T/run-device-coreclrtest.sh JIT.CodeGenBringUpTests_$c --shim; done
$T/run-device-coreclrtest.sh Loader/Loader --shim     # 중첩 경로: <tree>/<merged dir>
$T/run-device-coreclrtest.sh GC/GC     --shim
```

### 실행 C — ✅ 실제 Tizen 앱에 GBS aarch64 빌드로 붙이기 (경로 A, 2026-08-04 검증 완료)

§7-7의 "aarch64 GBS 빌드 미검증" 공백을 이번에 메웠다. `dotnet-runtime-tizen-platform` 스킬대로
`gbs build -A aarch64`가 원리상 동작한다는 §7-7의 예상은 맞았지만, **armv7l과 달리 실제로 거쳐야
했던 5개의 개별 실패**가 있었다 — 전부 재사용 가치가 크므로 기록한다.

1. **GBS buildroot에 `tar`가 없다.** `%prep`의 `tar -xof -`가 `/bin/tar: No such file or directory`로
   즉시 실패한다. Tizen 저장소의 `tar` 패키지는 `gnutar` 바이너리만 설치하고 `/usr/bin/tar` 심볼릭
   링크를 만들지 않는다. **buildroot마다** (`--clean`으로 재생성될 때마다) 다음이 필요하다:
   ```bash
   ln -s gnutar <buildroot>/usr/bin/tar   # 예: .../BUILD-ROOTS/scratch.aarch64.0/usr/bin/tar
   ```
   coreclr.spec에 `BuildRequires: tar`를 추가해도 이 심볼릭 링크는 자동으로 안 생긴다 — **수동
   또는 buildroot 초기화 후처리 스크립트로 매번 걸어야 한다.**
2. **`xz-utils`는 Tizen 저장소에 없다.** 패키지명은 `xz`다(`nothing provides xz-utils` 에러로 확인).
3. **`FEATURE_COMPRESSED_*` 없이 census만 포팅해도 CoreLib 관리 미러 문제는 없다** — census는 레이아웃을
   안 바꾸므로 `RuntimeHelpers.CoreCLR.cs` 수정이 전혀 필요 없다. 순수 VM 코드 3개 파일
   (`compressedptrvalidate.cpp` 신규, `gcenv.ee.cpp`/`finalizerthread.cpp`/`corhost.cpp` 배선)만으로
   충분하다.
4. **`gcenv.ee.cpp`는 두 번 컴파일된다 — 한 번은 `namespace standalone` 안에서.**
   `gcenv.ee.standalone.cpp`가 `#include "gcenv.ee.cpp"`를 `namespace standalone { ... }`로 감싸
   `singlefilehost`(Corehost.Static)용으로 재사용한다. 그 안에서 선언한 `extern` 함수는
   `standalone::함수이름`으로 링크되므로, `compressedptrvalidate.cpp`처럼 **네임스페이스 밖에서
   정의된** 함수를 호출하려면 선언과 정의 양쪽에 `extern "C"`를 붙여야 한다(안 그러면
   `ld.lld: undefined symbol: standalone::함수이름`로 singlefilehost 링크만 실패 — 본체
   `libcoreclr.so`는 이미 성공해 있어 헷갈리기 쉽다).
5. **fork 없이 순수 헤더 문제도 있다.** `<cstdio>`가 이 빌드 환경(Tizen aarch64 clang)에서 없다 —
   `<stdio.h>`로 바꾸면 된다. 새 VM `.cpp` 파일을 추가할 때는 C++ 표준 헤더보다 C 헤더를
   우선 시도해 볼 것.

**결과:** 5번의 실패(위 1,2,4,5 + `MethodTable::ContainsGCPointers`가 이 포크(8.0.11)에는
`ContainsPointers`/`ContainsPointersOrCollectible`이라는 다른 이름으로 존재해서 난 컴파일 에러)를
전부 고친 뒤 **10번째 시도에서 GBS aarch64 빌드가 성공**했다(`coreclr-8.0.11-0.aarch64.rpm` 등
15개 RPM 전부 `Wrote:`). §7-7의 공백은 이제 해소— **GBS `-A aarch64`는 armv7l과 별개의, 그러나
극복 가능한 함정 집합을 갖고 있을 뿐 원리적으로 막혀 있지 않다.**

#### 🔑 재사용 가치 큰 사실 두 가지 (경로 A 전체를 성립시키는 근거)

**(a) `coreclr_env.list`로 앱에 런타임 env를 주입할 수 있다 — 런처 재빌드 불필요.**
`/usr/share/dotnet.tizen/lib/coreclr_env.list`에 `NAME=value` 한 줄씩 쓰고
`chsmack -a _`로 라벨링하면, `launcher/NativeLauncher/launcher/lib/core_runtime.cc`의
`setEnvFromFile()`이 `putenv()`로 주입한다. **앱 재시작(껐다 켬)만으로 새 값이 반영된다** — 단,
`dotnet-hydra-loader`가 이미 오래 떠 있는 워커를 재사용하면 그 워커가 **precreate 때 읽은 값**이
이후 fork되는 모든 앱에 굳어 있을 수 있다(위 census 타이밍 함정과 같은 원인). 확실히 하려면
`sdb shell "pkill -9 -f dotnet-hydra-loader; pkill -9 -f process-pool"`로 워커 풀을 죽여 재생성을
유도한다(즉시 respawn됨).

**(b) 레이아웃을 바꾸지 않는 변경이라면 `libcoreclr.so` 단독 교체로 GBS/RPM 5종 설치를 전부
건너뛸 수 있다.** `/usr/share/dotnet.tizen/netcoreapp/libcoreclr.so`만 백업 후 교체하고
`chsmack -a _`를 잊지 않으면 된다. 단 **R2R/NI가 이 절차와 맞물리면 무효화될 수 있으므로**
(크로스겐된 이미지가 새 런타임 빌드와 레이아웃/ABI가 다를 가능성), `System.Private.CoreLib.dll`도
`.dll.Backup`(NI 되기 전 IL 원본, 4.5MB)으로 되돌리고 `coreclr_env.list`에 `DOTNET_ReadyToRun=0`을
추가해 전부 JIT 경로로 강제하는 것이 안전하다. `dotnettool --ni-*` 계열은 이 경로에서 전부
생략 가능하다. 이번 5개 앱 census 측정 전체가 이 방식(GBS로 만든 `libcoreclr.so` + non-NI CoreLib +
JIT 강제)만으로 이뤄졌다 — RPM 설치도, NI 재생성도 필요 없었다.

**(c) Tizen `hydra` 프로세스 풀이 시간 기반 native 로직의 기준점을 오염시킨다.** 자세한 내용은
§4-3b의 "측정 중 발견한 함정" 참조. 요지: 프로세스 시작 후 N초 뒤에 무언가를 하는 로직을
libcoreclr.so에 넣을 때는 EE 시작 시각이 아니라 `coreclr_initialize_after_fork`
(`CorHost2::InitializeAfterFork`, `vm/corhost.cpp`)를 기준점으로 삼아야 hydra 풀 재사용에 안전하다.

> **⏱️ 작은 파일이 많은 트리는 반드시 tar로 보낼 것 — 165배 차이가 난다.**
> `sdb push`는 파일당 오버헤드가 커서 Loader 트리(81 MB, 파일 수천 개)가 **117 KB/s로 10분**
> 걸렸다. tar로 묶으면 **19 MB/s로 1초**다:
> ```bash
> (cd $B && tar czf /tmp/gc-tree.tgz GC)
> sdb push /tmp/gc-tree.tgz /opt/usr/home/owner/media/gc-tree.tgz
> sdb shell "cd $M/tests && tar xzf /opt/usr/home/owner/media/gc-tree.tgz"
> ```
> 추출 시 나오는 `time stamp ... is N s in the future` 경고는 무해하다 — **rpi4에는 RTC가 없어**
> 디바이스 시계가 뒤처져 있다.

> `sdb push <dir> <dest>`는 디렉터리 **내용**을 `<dest>` 직하에 넣는다(`<dest>/<dirname>/`이 아니다).
> `CodeGenBringUpTests`는 merged runner 디렉터리만 밀면 된다(각 4.4 MB, 개별 테스트 디렉터리 42 MB는
> 불필요). 반면 **`Loader`/`GC`는 트리 전체를 밀어야 한다** — merged runner가 형제 디렉터리의
> out-of-process 테스트 스크립트를 실행한다.
>
> 구성을 바꿀 때는 `Core_Root`의 `libcoreclr.so`/`libclrjit.so`/`libclrgc.so`/`corerun`/
> `System.Private.CoreLib.dll` 5개만 덮어쓰면 된다.
>
> ⚠️ **`tests/vaprobe`와 `tests/lowvashim`은 네이티브라 정적 링크가 안 된다** — 위 `-B$G -L$G`
> 형태를 쓸 것(§5-6).

### 플래그 / 진단 스위치

| 속성 (모두 기본 OFF) | 내용 | 상태 |
|---|---|---|
| `FeatureCompressedMT` | 객체 헤더 MT 슬롯 4B (+명시적 패딩) | ✅ |
| `FeatureCompressedMTFields` | MethodTable 내부 포인터 3개 4B | ✅ |
| `FeatureCompressedRefs` | 힙 참조 **접근** 4B (저장 폭은 8B) | ✅ 부분 |

| 스위치 | 용도 |
|---|---|
| `DOTNET_ValidateCompressedPtr=1` | 기동 시 4GB 불변식 + MethodTable 레이아웃 보고 |
| `DOTNET_CompressedPtrHeapCensus=1` | 전체 GC마다 라이브 힙 구성비 + 절감 투영. **실제 앱에 그대로 붙일 수 있다** |
| `DOTNET_CompressedPtrHeapCensus=2` | 위 + 모든 참조 슬롯 값이 32비트에 맞는지 검사 |
| `DOTNET_JitCompressedRefsMask` | 좁힐 접근 shape 비트마스크 (기본 `0x1F`). 회귀 시 이등분용 |

### 테스트 자산 (`arm64-low-va-memory-opt/tests/`)

| 앱 | 무엇을 보는가 |
|---|---|
| `mtstress` | 객체 헤더 MT 읽는 경로 — 캐스팅/디스패치/제네릭/리플렉션 |
| `mirrorstress` | CoreLib 관리 미러의 **모든** 소비자 (statics 4종, aux data, parent chain, ValueType, Stream, enum/delegate, Nullable) |
| `lowvastress` | 8스레드 GC 무결성 |
| `lowvatest` | GC 힙 주소가 4GB 아래인지 |
| `sizemeasure` | shape별 인스턴스 실측 바이트 |
| `heapcensus` | 라이브 힙 프로파일 (startup/collections/text/arrays/mixed) |
| `refload` | 코드젠 프로브 — 참조 접근 3 shape를 JitDisasm으로 확인 |

관리 앱 빌드: 각 디렉터리에서 `/work/dotnet/new/runtime/dotnet.sh build -c Release`. 빈
`Directory.Build.props`/`.targets`가 레포 빌드 규약(분석기·헤더)을 차단한다.

네이티브 도구 (실기기 전용, C):

| 도구 | 무엇을 하는가 |
|---|---|
| `vaprobe` | 커널이 저주소 배치를 허용하는지 직접 측정 — 힌트 / `MAP_FIXED_NOREPLACE` / `personality` (§3-3) |
| `lowvashim` | `LD_PRELOAD` 저VA shim — 디바이스판 `qemu -R` (§3-3) |

### upstream 테스트 스위트

```bash
ROOTFS_DIR=... ./src/tests/build.sh -arch arm64 -checked -cross -generatelayoutonly \
  /p:IsXUnitLogCheckerSupported=false
# 트리 단위로 빌드 (-tree 는 한 번에 하나)
for tree in JIT/CodeGenBringUpTests Loader GC; do
  ROOTFS_DIR=... ./src/tests/build.sh -arch arm64 -checked -cross -priority1 -skipnative \
    -skipgeneratelayout -tree $tree /p:IsXUnitLogCheckerSupported=false
done
```

빌드된 트리 (2026-08-03 기준):

| 트리 | 프로젝트 | merged runner | 트리 크기 |
|---|---|---|---|
| `JIT/CodeGenBringUpTests` | 645 | 4개 (`JIT.CodeGenBringUpTests_{d,do,r,ro}`) | 42 MB |
| `Loader` | 717 | **1개** (`Loader/Loader/Loader.dll`, 366 dll) | 81 MB |
| `GC` | 696 | **1개** (`GC/GC/GC.dll`, 59 dll) | 57 MB |

> JIT 트리만 구성별(`_d/_do/_r/_ro`) merged runner가 4개다 — 그건 개별 csproj 이름에서 오는 것이고
> `Loader`/`GC`는 트리당 하나씩만 생긴다. merged assembly에 포함되지 않은 테스트는
> **out-of-process**로 형제 디렉터리의 생성된 `.sh`를 통해 실행되므로 트리 전체가 필요하다.

---

## 7. 알려진 공백 / 위험

1. **cDAC 데이터 디스크립터 미정렬.** `vm/datadescriptor/datadescriptor.inc`가
   `MethodTable.{Module,ParentMethodTable,AuxiliaryData}`를 `T_POINTER`(8B)로 선언. 오프셋은 맞지만
   **폭이 틀리다** → 압축 구성에서 SOS/덤프가 오독. 런타임 실행에는 영향 없음. 고치려면
   `src/native/managed/cdac/` 리더도 함께.
2. **레거시 DAC**는 같은 접근자를 통과하므로 빌드·오프셋은 맞지만 의미 검증은 안 했다.
3. ~~실기기 검증 전무.~~ → **해소** (§3-3). 남은 것은 두 가지뿐:
   - **제품 경로가 없다.** 실기기 검증은 `LD_PRELOAD` shim으로 불변식을 강제한 결과다. 배포
     가능한 형태로는 **A1a(GC) + A1b(PAL loader/code heap)** 구현이 필요하다. 메커니즘(저주소 힌트,
     `MAP_FIXED_NOREPLACE`)은 실기기에서 동작을 확인했다.
   - **I3(코드 주소) 미증명** — validator가 코드 주소를 probe하지 않는다. vtable 압축의 선행 조건.
4. **RSS 실측 미실시.** 현재 절감이 타입당 8B라 노이즈 이하이므로 4단계 이후에 의미가 생긴다.
5. **`Interlocked`/volatile 참조 연산** — 4B CAS와 8B CAS 혼재. 상위가 0이므로 정확성은 유지되나
   확인하지 않았다. (참고: LL/SC 배타 모니터는 캐시라인 granule이라 4B 저장이 끼면 재시도가 되므로
   손상이 아니라 정상 동작이다. LSE `cas*`도 단일 원자 RMW라 창이 없다. 단 실증은 안 했다.)
5b. ⚠️ **write barrier 구현이 두 벌 있다 — 4단계에서 반드시 함께 봐야 한다.**
   `runtime/arm64/WriteBarriers.S`(NativeAOT와 공유, `RhpAssignRefArm64`/`RhpCheckedAssignRefArm64`)가
   `libcoreclr.so`에 **링크돼 있고** `stlr x15,[x14]`(8B)와 `UPDATE_GC_SHADOW`의 `str \refReg`(8B)를
   그대로 갖고 있다. 3단계는 이것을 바꾸지 않았다.
   **3단계에서 문제가 없는 이유:** `inc/jithelpers.h:170-171`은 `CORINFO_HELP_ASSIGN_REF`를
   `RhpAssignRef`로 정적 바인딩하지만, `vm/threads.cpp:1107`이 이를 런타임에
   `JIT_WriteBarrier`로, `:1117`이 `CORINFO_HELP_CHECKED_ASSIGN_REF`를 `JIT_CheckedWriteBarrier`로
   **재바인딩**한다(arm64 명시). 즉 CoreCLR arm64의 실제 경로는 `patchedcode.S`이고 3단계 변경은
   라이브다. `WriteBarriers.S`는 사실상 NativeAOT 전용(범위 밖).
   **4단계 위험:** 슬롯이 4B가 되면 이 8B 저장이 인접 필드를 덮는다. 재바인딩이 항상 일어나는지,
   그리고 이 심볼을 직접 호출하는 경로가 없는지 확인한 뒤 함께 축소하거나 `#error`로 막을 것.
5c. **shadow 힙의 INVALIDGCVALUE 저장은 8B로 남겨 뒀다** (`str x17,[x12]`). 지금은 무해하다 —
   shadow는 commit 시 0이고 `testGCShadow`(`gc/gc.cpp:4392`)가 8B로 비교하는데 `movz/movk`가
   만드는 값의 상위 32비트가 0이라 정합한다. **4단계에서는 인접 슬롯을 덮으므로 함께 축소해야 한다.**
6. 테스트 커버리지: `CodeGenBringUpTests` + `Loader` + `GC` 3트리(약 1,135개). **`Interop` 미실행**
   (네이티브 컴포넌트가 필요하므로 `-skipnative` 상태로는 의미가 없다).
7. ~~**실행 가능한 Tizen 앱은 이미 준비돼 있다**... 실제 병목은 포크의 aarch64 GBS 빌드 미검증~~
   → ✅ **해소 (2026-08-04)**. `gbs build -A aarch64`가 armv7l과 별개의 함정(들)을 거쳐 성공했다
   (tar 심볼릭 링크, `xz` 패키지명, `extern "C"` 네임스페이스, `<stdio.h>`, `ContainsPointers` API
   이름 — §실행 C 참조). 스킬의 검증 범위가 이제 aarch64까지 확장됐다.
8. **`-skipnative`로 네이티브 테스트 컴포넌트를 건너뛰었다** — Loader 실패 2건이 이 때문이다
   (`AssemblyDependencyResolver`=hostpolicy, `NativeLibs/FromNativePaths`). 커버리지를 넓히려면
   네이티브 테스트 빌드가 Tizen rootfs에서 되는지부터 확인해야 한다.
9. **shim의 저주소 창은 약 3.75 GB가 상한이다.** `GCRegionRange`를 3 GB로 올리면 들어가지 못한다
   (§3-3). 크게 할당하는 GC 테스트를 돌리려면 A1a/A1b 제품 구현이 필요하다 — shim으로는 한계가 있다.
10. ⏳ **GC 트리 간헐 실패 1건이 미판정이다** (ON 6회 중 1회, 이름 미상 — §3-3). OFF 5회는 깨끗했지만
   `(5/6)^5≈40%`이므로 **유의한 차이가 아니다.** 판정에는 양쪽 20회 수준의 반복이 필요하다.
11. **4단계에서 R2R이 무효화된다** — 8B 접근이 구워진 이미지가 좁혀진 슬롯을 읽는다. 그 시점에 R2R을
   끄고 시작할 것.
12. **디바이스는 armel CI와 SD카드를 공유한다.** rpi4는 나이트리 armel CI(22:30 cron)의 타깃이기도
   하다. aarch64 작업은 **별도 SD카드**로 하고 작업 후 armel 카드로 되돌리는 것이 가장 깨끗한
   격리다(카드가 분리돼 있으므로 서로 오염되지 않는다). 카드를 되돌리면 cron은 손댈 필요가 없다.

---

## 8. 변경된 파일 (커밋 안 됨, main 브랜치)

25파일 수정 + 2파일 신규. **Phase A 코드는 없다(revert).**

**현재 빌드 상태 (2026-08-04):** 호스트 `artifacts`와 디바이스 `/opt/vatest/fx`·
`media/coreclr/Core_Root`가 **모두 ON 빌드**(MTFields+Refs, 3단계 write barrier 포함)다.
디바이스에 `liblowvashim.so`도 올라가 있다. `JIT/CodeGenBringUpTests` 테스트 트리는 재빌드됐다.
OFF 비교가 필요하면 플래그 없이 재빌드해야 한다(§3-3의 경고 참조).

| 영역 | 파일 |
|---|---|
| 신규 | `inc/compressedptr.h` (저장 타입 + 검사기 선언), `vm/compressedptrvalidate.cpp` (불변식 검사 + 힙 census) |
| **어셈블리 (3단계)** | `vm/arm64/patchedcode.S`, `vm/arm64/patchedcode.asm` — write barrier 참조 저장 4B (`FEATURE_COMPRESSED_REFS` 게이트) |
| 빌드/플래그 | `clrdefinitions.cmake`, `runtime.proj`, `clr.featuredefines.props`, `inc/clrconfigvalues.h` |
| VM | `methodtable.h/.cpp`, `methodtablebuilder.cpp`, `generics.cpp`, `class.cpp`, `object.h/.inl`, `gchelpers.cpp`, `stubhelpers.cpp`, `ceemain.cpp`, `gcenv.ee.cpp`, `arm64/asmconstants.h`, `vm/CMakeLists.txt` |
| JIT | `instr.h` (`emitNarrowGCRefAccess`), `codegen.h` (shape 판정 + 계측), `codegenarmarch.cpp` (load), `codegenarm64.cpp` (store), `jitconfigvalues.h` (마스크) |
| 관리 | `System.Private.CoreLib/src/.../RuntimeHelpers.CoreCLR.cs` (MethodTable 미러) |
| 문서/테스트 | `arm64-low-va-memory-opt/` (추적 대상 아님). 실기기용 신규: `tests/vaprobe/`, `tests/lowvashim/`, `tests/deploy-device.sh`, `tests/run-device.sh`, `tests/run-device-coreclrtest.sh` |

**디바이스(aarch64 SD카드)에 올라간 것** — 총 약 626 MB:

| 경로 | 파티션 | 내용 |
|---|---|---|
| `/opt/vatest` | `/dev/mmcblk0p3` | 런타임 + 앱 251 MB, `liblowvashim.so`, `vaprobe` |
| `/opt/usr/home/owner/media/coreclr` | `/dev/mmcblk0p5` | Core_Root + 테스트 트리 375 MB |

**둘 다 SD카드의 파티션이므로 카드를 뺐다 다시 꽂으면 그대로 남아 있다** — 리부트도, armel 카드로
교체했다 되돌리는 것도 데이터를 지우지 않는다. 재배포가 필요한 경우는 **카드를 재이미징할 때**
(`dude download`가 파티션을 덮어쓴다)뿐이다.

> `/tmp`만 예외다 — 1.9 GB **tmpfs(RAM)** 이므로 리부트에 사라진다. 배포물을 여기 두지 말 것.

---

## 9. 다음에 할 일 (우선순위)

0. ⏳ **GC 간헐 실패 판정** — ON/OFF 각 20회 반복(§3-3). 구성당 약 2.5시간이고 결론이 나오지
   않을 수도 있으므로, 그 이상의 우선순위 작업을 먼저 진행해도 된다.
1. ~~**B2 3단계**~~ → ✅ **완료** (QEMU + 실기기 양쪽, §4-6).
2. ~~**실제 Tizen 앱에 `DOTNET_CompressedPtrHeapCensus=1`.** B2 전체의 Go/No-Go가 여기서 갈린다~~
   → ✅ **완료 (2026-08-04)** — 실제 앱 **11개 전부**, plateau 검증까지. 상세 §4-3b.
   **결론: No-Go.** B1+B2 절감률은 실제 앱에서도 재현되지만(22.8~27.8%), 관리 라이브 힙이
   421 KB~1.67 MB로 **앱 PSS의 0.83~3.13%**밖에 안 되어 절감/PSS가 **0.20~0.87%**(중앙값 0.47%,
   최대 절감 465 KB)에 그친다. **B2가 관리 힙을 100% 없애도 상한이 PSS의 3.13%다.**
1. ⭐ **NEW 최우선 — 무거운 프로덕션 앱으로 재측정.** 위 No-Go를 뒤집을 수 있는 유일한 조건이며,
   그것만 남았다. 손익분기: 절감이 PSS의 2%가 되려면 라이브 힙 ≈ **3.6 MB**(측정 최대의 2.1배),
   5%면 **9 MB**(5.4배)다. 샘플 앱 11개에는 그런 앱이 없다.
   **인프라는 전부 갖춰져 있어 앱만 확보하면 앱당 약 2분이다** — §실행 C의 경로 A + `§4-3b`의
   측정 조건(10초 간격 강제 gen2 GC, 마지막 3샘플 동일 = plateau)을 그대로 쓰면 된다.
   대상 후보: 큰 데이터 모델·깊은 객체 그래프를 가진 실제 제품 앱(대량 리스트/미디어 라이브러리,
   웹뷰 기반 앱, 장시간 상주 서비스). **이것이 안 나오면 4단계는 착수하지 않는 것이 맞다.**
3. B2 **3b** / **4단계** — ⏸️ **보류 (우선순위 강등).** 위 2번 결과 때문이다. 4단계는 R2R 무효화 +
   `WriteBarriers.S` 두 벌(§7-5b) + INVALIDGCVALUE(§7-5c) + CGCDesc/GC stride + 배열 스케일 8→4를
   동시에 건드리는 고위험 대공사인데, 얻는 것이 현재 워크로드에서 PSS의 0.5% 안팎이다.
   **비용/효과가 성립하지 않는다.** 위 1번(무거운 앱)에서 3~9 MB 힙이 확인되면 그때 재개한다.
   (기술적 준비 상태는 유지: 3b는 배열 인덱스 shape의 **접근 폭만** 4B화이고 **스케일 8→4는 4단계**다
   — §5-4b. 3b의 검증 가치는 낮아 건너뛰고 4단계에서 배열 shape을 함께 처리해도 된다.)
4. **A1a/A1b 구현** — shim을 제품 경로로 대체. 메커니즘은 §3-3에서 확인됐으므로 남은 것은
   "런타임 어디에서 예약하는가"뿐이다. A1b는 `MethodTable`을 저주소로 만들어 I2를 제품에서 성립시킨다.
5. ~~`Loader` + `GC` 테스트 트리~~ → **완료** (§3-3, QEMU). 다음 확장 후보는 **네이티브 테스트 컴포넌트**
   (`-skipnative` 해제)와 `Interop` 트리 — 둘 다 Tizen rootfs에서 네이티브 테스트 빌드가 되는지
   확인이 선행이다.
6. **I3 증명** — validator를 코드 주소(vtable 슬롯/프리코드/스텁)까지 확장. vtable 압축의 선행 조건.
