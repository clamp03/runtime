# 명세서 — ARM64 저(低)-주소 메모리 최적화 (Low-Address / 32-bit VA Confinement)

> 대상 런타임: CoreCLR (Tizen `corerun`/crossgen2). Mono는 범위 밖.
> 상태: 구현 착수 전 기준 산출물. 구현 계획은 `arm64-low-va-plan.md` 참조.
>
> ⚠️ **착수 전 문서이며 일부 판단이 실측으로 정정되었다.** 현재 상태와 정정 사항은
> `HANDOVER.md`를 볼 것. 특히 §4의 config 이름 `DOTNET_ARM64LowVA`는 폐기됐고(실제는
> `DOTNET_GCLowVirtualAddress`, 그리고 그 코드는 revert됨), §8의 "Phase A 효과"는 물리 절감이
> 아니라 가상 주소 절감으로 정정됐다(§8에 정정 주석 있음).

## 1. 개요 및 목적

ARM64는 64bit 가상 주소 공간에서 동작하며, CoreCLR 내부의 주소 처리도 64bit 폭을 전제한다.
제한된 메모리에서 가벼운 앱만 실행하는 대상 기기에서는, 가상 주소를 하위 32bit(4GB)에 가둬
런타임 내부 주소 처리를 32bit로 전환함으로써 64bit로 인한 메모리 오버헤드를 줄이는 것을 목표로 한다.
"느리게 작동하더라도 메모리를 최대한 줄이는" 방향을 우선한다.

## 2. ARM64 메모리 오버헤드의 실제 근원 (조사 결과)

| # | 항목 | 32bit 대비 | 근거 |
|---|---|---|---|
| 1 | 객체 참조/`MethodTable` 포인터 저장 = 8바이트 | 2배 | `vm/object.h:130`, `vm/common.h:143`, `vm/methodtable.h:3987-4031` |
| 2 | 객체 헤더 4바이트 `m_alignpad` 死공간, `MIN_OBJECT_SIZE` 24 vs 12 | +패딩 | `vm/object.h:85-106`, `vm/syncblk.h:730-744` |
| 3 | GC 부기 테이블(card/brick/mark/seg-mapping)이 커버 주소 범위에 비례 | 범위 의존 | `gc/gcpriv.h:6771`, `gc/gcinternal.h:1888,2665`, `gc/gc.cpp:2649-2711` |

## 3. 핵심 타당성 판정 (설계 전체를 지배하는 사실)

- **"주소 공간 제한"과 "포인터 폭 축소"는 별개다.** 주소를 4GB에 가둬도 저장 폭은 8바이트
  그대로 → **객체당 메모리는 줄지 않는다.** 가장 큰 항목(객체당)을 줄이려면 참조/포인터를
  4바이트로 **저장**하는 *compressed references*가 필요하다.
- **compressed references 모드는 현재 런타임에 없다.** 과거 이를 담당하던 self-relative
  32bit 포인터 machinery(`RelativePointer`/`RelativeFixupPointer`)는 crossgen1(`FEATURE_PREJIT`)
  제거와 함께 삭제됨(commit `5a5b1045ab2`). 현재 잔존은 R2R/NativeAOT **디스크 포맷** 리더뿐
  (`nativeaot/.../MethodTable.cs:1340`). → upstream이 "ARM64용으로 적용한 재사용 가능한
  32bit-포인터 런타임 패치"는 **존재하지 않는다.**
- 따라서 compressed refs는 사실상 **신규 런타임 모드** 신설이며 GC·JIT·VM·interop 전반을
  건드리는 초대형·고위험 작업이다.
- **그러나 주소 공간 제한은 compressed refs의 전제조건이다.** compressed refs가 성립하려면
  "모든 관리 객체 참조 대상 ∈ 하위 4GB"가 보장되어야 하고, 그 보장을 만드는 것이 주소 공간
  제한이다. → 단계적 진행 시 버려지는 작업 없이 누적된다.

## 4. 설계 결정 (확정)

- **야심 수준: 단계적** — Phase A(주소 공간 제한) → 측정/평가 → Phase B(compressed refs) 타당성 재판정.
- **외부 네이티브 라이브러리 전략: 전략 2 (런타임 영역만 저주소)** — §7 근거 참조.
- **격리 원칙:** 모든 변경은 `ARM64` + config 스위치(`DOTNET_ARM64LowVA`) 뒤에 격리하여
  기본 경로 및 타 플랫폼에 무영향.

## 5. As-Is: 현재 런타임 메커니즘

| 영역 | 현재 동작 | 근거 |
|---|---|---|
| 실행 코드(JIT/loader heap) | 시작 시 libcoreclr 근처 **1GB 슬랩**(ARM64) 예약, 그 안에서 할당 (rel28/rel21 도달성 목적) | `pal/src/include/pal/virtual.h:187-194`, `pal/src/map/virtual.cpp:1609-1759` |
| 범위 제한 예약 API | `ClrVirtualAllocWithinRange` / `ExecutableAllocator::ReserveWithinRange` — **코드 전용** | `utilcode/util.cpp:374-531`, `utilcode/executableallocator.cpp:680-725`, `pal/src/map/virtual.cpp:818-866` |
| GC 힙/일반 데이터 | `mmap(nullptr, …)` — **주소 무제약** | `gc/unix/gcenv.unix.cpp:385` |
| 저주소 정책 | 오히려 **"4GB 아래를 피하라"** 휴리스틱 존재 | `pal/src/map/virtual.cpp:1670-1674` |
| `MAP_32BIT`/`personality` | **어디에도 없음** (grep 무결과) | — |
| JIT 절대주소 적재(ARM64) | 임의 64bit = 최대 `movz`+3×`movk` **4명령**; 32bit 보장 시 **1~2명령** | `jit/codegenarm64.cpp:2199-2256` |
| JIT 저주소 최적화 | ARM64용 `FitsInAddrBase`/`AddrNeedsReloc` **미구현**(AMD64/RISCV64만) | `jit/gentree.cpp:20205-20284`, `gentree.h:3380` |
| 호출(`bl`) | 항상 rel28(±128MB) 가정, 초과 시 VM이 jump stub 삽입 | `jit/instr.cpp:1884-1893`, `vm/jitinterface.cpp:12213-12298` |

핵심 시사점:
- **GC/데이터 경로가 공백** — 저주소 제한 시 신규 코드 필요.
- **4GB에 모아도 jump stub은 안 사라짐** (`bl` ±128MB ≪ 4GB). 코드 도달성은 기존 1GB 슬랩이
  이미 처리하므로 본 최적화의 목표가 아니다.

## 6. To-Be 아키텍처

**불변식(Invariant):** 런타임이 제어하는 모든 할당 — GC 힙, loader heap(=`MethodTable`),
frozen/static 세그먼트, JIT 코드 — 을 **하위 4GB 연속 "low arena"** 에 배치한다.
관리 객체 그래프는 100% 런타임 제어 하에 있으므로 이 불변식이 성립 가능하다.

- **Phase A (저장 폭 8바이트 유지):** low arena 확보 → GC 부기 축소 + JIT 코드 크기 단축.
  객체당 메모리는 불변. 중간 이득 + Phase B 토대.
- **Phase B (저장 폭 4바이트):** A의 불변식 위에서 참조/MT 포인터를 4바이트 저장 후
  zero-extend. 객체당 메모리 대폭 절감. 초대형·고위험, 별도 상세 설계 필요.

## 7. 외부 네이티브 라이브러리(.so) 트레이드오프 (사용자 우려 사항)

리눅스 커널은 `.so`를 ASLR에 따라 높은 VA에 로드하며 런타임이 위치를 강제할 수 없다.

**전략 1 — 프로세스 전역 제한 (`personality(ADDR_LIMIT_32BIT)`):** 로더가 올리는 `.so`까지
32bit VA에 강제. 단순하나 스택·TLS·vDSO·glibc·자식 프로세스가 모두 4GB에 눌려 로드 실패/
충돌/단편화 위험. 고주소 가정 라이브러리와 비호환. → **비권장** (측정용 실험 스위치로만).

**전략 2 — 런타임 영역만 저주소 (권장, 채택):**
- **핵심 통찰:** 관리 참조는 오직 GC 힙/frozen/static을, `MethodTable` 포인터는 loader heap을
  가리킨다 — 전부 런타임이 저주소 arena에 배치 가능. 관리 참조가 `.so`를 직접 가리키는 일은 없다.
- interop 경계의 네이티브 주소(P/Invoke 함수 포인터, delegate, 콜백)는 `IntPtr`/`nint`
  = **full-width 8바이트 슬롯**에 저장되므로 고주소여도 무방.
- 결론: "커널이 라이브러리를 어디 올릴지 모른다"는 우려는 런타임 arena 설계에서 자연히 해소.
  A/B 모두 `.so`를 저주소로 옮길 필요가 없다.

## 8. 예상 효과 / 리스크

> **측정으로 수정됨 (2026-07-28, linux-x64):** Phase A1a 구현·측정 결과 **물리 메모리(RSS)는
> 개선되지 않았다.** GC 부기 테이블은 예약 범위가 아니라 실제 사용량에 비례해 lazy commit되므로
> 예약 축소가 RSS로 이어지지 않는다. 가상 주소는 65GB→6.6GB로 약 90% 감소했으나 그 대부분은
> `GCRegionRange` 제한에서 오며 저주소 배치 자체의 기여는 작다. 상세: `PHASE-A1-RESULTS.md`.
> → **Phase A의 가치는 (a) Phase B 전제 불변식 확립, (b) 거대 VA 예약 회피로 재정의된다.**
> 물리 절감은 Phase B(참조 4바이트 저장)와 A2(코드 크기 축소 → 코드 커밋 감소)에서 와야 한다.

- **Phase A 효과:** ~~GC 부기 예약 감소~~ → **가상 주소 사용량 감소**(물리 절감 아님) +
  JIT 코드 크기 감소(A2, 주로 non-AOT). 객체당 8바이트 불변.
- **Phase A 리스크(중):** 저주소 예약 실패/단편화, ASLR 완화 보안 영향, "avoid <4GB" 반전 누출
  (→ ARM64+config 게이트로 격리).
- **Phase B 효과:** 객체당 메모리 대폭 절감(최대 이득).
- **Phase B 리스크(초고):** GC·JIT·VM·interop·DAC 광범위 개조, upstream 미지원, 유지비 큼.
