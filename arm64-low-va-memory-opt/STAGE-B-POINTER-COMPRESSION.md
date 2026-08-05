# Stage B — 포인터 4바이트화 (Compressed Pointers)

> ⚠️ **처음 보는 사람은 `HANDOVER.md`를 먼저 읽을 것.** 이 문서는 시간순 작업 기록이라 진입점으로
> 적합하지 않다. 현재 상태·수치 요약과 재발견 비용이 큰 기술적 사실은 HANDOVER에 정리돼 있다.

작성: 2026-07-28 야간 작업 기록. 선행 문서: `arm64-low-va-spec.md`, `PHASE-A1-RESULTS.md`.

## 전제 변경 사항 (중요)

**런타임 LOWVA 할당 코드는 전부 revert 되었다.** `qemu-user -R 0x100000000`이 게스트 가상 주소
공간 전체(glibc malloc 아레나·스레드 스택 포함)를 4GB로 제한하므로 런타임 수정이 불필요하다는
것이 확인되었기 때문이다. 소스 트리는 upstream 상태이며, Stage B 작업만 올라가 있다.

**표준 테스트 환경 (이하 "저VA 환경"):**
```bash
R=/home/clamp/Work/dotnet/rootfs/arm64.tizen
CR=$(pwd)/artifacts/bin/testhost/net11.0-linux-Release-arm64/shared/Microsoft.NETCore.App/11.0.0
env DOTNET_GCRegionRange=40000000 DOTNET_ValidateCompressedPtr=1 \
  qemu-aarch64 -R 0x100000000 -L $R $R/lib64/ld-linux-aarch64.so.1 \
    --library-path $R/lib64:$R/usr/lib64:$CR $CR/corerun -c $CR <app.dll>
```
`-R`과 `GCRegionRange` 제한은 **둘 다** 필요하다. `-R`만 주면 GC 기본 예약(약 61GiB)이 4GB에
못 들어가 `0x8007000E`로 기동 실패한다.

---

## B0 — 완료 ✅ (구현·검증 완료)

**목적:** 레이아웃을 바꾸기 전에 "압축 포인터가 담아야 할 모든 주소가 4GB 아래"라는 전제가
실제로 성립하는지 증명한다.

**추가된 것:**
| 파일 | 내용 |
|---|---|
| `src/coreclr/inc/compressedptr.h` (신규) | `CompressedPtr<T>` 저장 타입(4바이트, zero-extend), `AddressFitsInCompressedPtr()`, `static_assert(sizeof==4)` |
| `src/coreclr/vm/compressedptrvalidate.cpp` (신규) | `ValidateCompressedPtrInvariant()` — GC 힙 경계와 대표 MethodTable 주소를 검사·보고 |
| `src/coreclr/inc/clrconfigvalues.h` | `DOTNET_ValidateCompressedPtr` = 0(off) / 1(보고) / 2(보고 후 fail fast) |
| `src/coreclr/vm/ceemain.cpp` | `SystemDomain::Init()` 직후 검증 호출 (config 없으면 no-op) |
| `src/coreclr/vm/CMakeLists.txt` | 새 소스 등록 |

**검증 결과 (arm64, QEMU):**

`-R` 없음 → 위반을 정확히 검출:
```
g_lowest_address    0x0000729adec00000  ABOVE 4GB
MT System.Object    0x000072aa38034c70  ABOVE 4GB
FAILED - ... a 32 bit pointer representation is not usable in this process.
```

`-R 4GB` + `GCRegionRange=1GB` → 통과:
```
g_lowest_address    0x00000000b4000000  ok
g_highest_address   0x00000000f4000000  ok
MT System.Object    0x0000000060044c70  ok
MT System.String    0x0000000061c9fc38  ok
OK - all probed addresses fit in 32 bits
```

→ **B1/B2의 전제가 실측으로 성립함이 증명되었다.** 기본 빌드는 config가 없으면 무영향.

---

## B1 — 객체 헤더의 MethodTable 포인터 4바이트화 (착수, VM 컴파일 통과)

### 진행 상태 요약

| 항목 | 상태 |
|---|---|
| `FEATURE_COMPRESSED_MT` 플래그 도입 (기본 **off**) | ✅ |
| `Object`의 MT 저장을 4바이트로 (접근자 시그니처 유지) | ✅ |
| 압축 슬롯용 write barrier `ErectWriteBarrierForCompressedMT` | ✅ |
| GC 마크 비트(`MARKED_BIT`) / `GetGCSafeMethodTable()` 압축 대응 | ✅ |
| arm64 `asmconstants.h` 배열 오프셋 시프트 대응 | ✅ |
| **플래그 ON 시 VM 네이티브 빌드 (arm64)** | ✅ **0 에러 통과** |
| 플래그 ON 런타임 기동 | ❌ 관리/네이티브 레이아웃 불일치에서 중단 (아래) |
| JIT 코드젠 / 어셈블리 / crossgen2 | ❌ 미착수 |

**플래그 OFF 기본 빌드는 무영향**이며, 저VA 환경 회귀 테스트(8스레드 스트레스, 무결성 오류 0)
통과를 확인했다. 실험용 `#define`은 제거되어 트리는 기본 상태로 빌드된다.

### 플래그 ON 에러 표면의 실제 추이 (핵심 데이터)

접근자 시그니처를 유지하는 전략이 유효했음이 수치로 확인되었다:

| 단계 | 에러 수 | 위치 |
|---|---|---|
| 최초 플래그 ON | **86** | 전부 `vm/object.h`, `vm/object.inl` |
| object.h/inl 정리 후 | **2** | `vm/arm64/asmconstants.h` (배열 오프셋 static assert) |
| asmconstants 대응 후 | **1** | `vm/stubhelpers.cpp` (`GetMethodTablePtr()` 계약) |
| stubhelpers 대응 후 | **0** | — VM 컴파일 통과 |

→ `GetMethodTable()` 1133개 호출부는 **한 곳도 수정하지 않았다.**

### B1a 완료 ✅ — 압축 MT 슬롯으로 런타임 동작 확인 (2026-07-29)

레이아웃 충돌의 원인이 규명되었다: `GetOffsetOfFirstField() = sizeof(Object)`이므로 MT를 4바이트로
줄이면 관리 측 필드가 오프셋 4에서 시작하는데, C++ 미러 구조체는 첫 `OBJECTREF`(8바이트)를 정렬
때문에 8에 배치한다.

**핵심 함의: 참조가 8바이트인 동안에는 MT만 줄여도 그 4바이트가 절감이 아니라 패딩이 된다.**
JVM의 compressed oops가 klass 포인터와 참조를 함께 압축하는 이유와 같다.

그래서 B1을 둘로 나눴다:
- **B1a (완료)** — 4바이트 MT 슬롯 + 명시적 4바이트 패딩(`Object::m_mtPadding`). `sizeof(Object)`가
  8로 유지되어 **나머지 레이아웃이 전부 불변**(미러 구조체, binder 오프셋, JIT, 어셈블리).
  패딩이 항상 0이므로 **기존 8바이트 MT 로드도 그대로 유효**하다(리틀엔디언 하위 4바이트가 MT).
  → JIT/어셈블리 수정 없이 압축 슬롯 기계장치만 독립 검증 가능.
- **B1b/B2** — 참조를 4바이트로 줄인 뒤 패딩 제거. **실제 절감은 여기서 발생.**

**검증 결과 (arm64 Checked, 저VA 환경):**

| 항목 | 결과 |
|---|---|
| 빌드 | 0 에러 |
| 런타임 부팅 + 관리 코드 실행 | ✅ |
| `lowvatest` | PASS, GC 주소 20/20 4GB 아래 |
| `lowvastress` 8스레드/2058MB/gen2 32회 | **무결성 오류 0, PASS** |
| 메모리 절감 | **0 (의도됨 — 패딩 때문)** |

추가 수정: `asmconstants.h`의 `OFFSETOF__Array__m_Length`는 패딩 덕에 0x8 유지(B2에서 0x4로).
`stubhelpers.cpp`의 `GetMethodTablePtr()` 사용처는 4바이트 로드 후 확장하도록 수정.

### 설계 노트: `PTR_MethodTable` 타입 자체를 바꾸지 않는 이유

`PTR_MethodTable`은 **저장과 사용을 겸하는** 타입이다(필드·지역변수·파라미터·반환타입). 폭을 줄이면
(1) 모든 사용처에 인코딩/디코딩이 번지고, (2) raw `MethodTable*`를 주고받는 1133곳과 암묵 변환으로
뒤섞이며, (3) `DPTR`의 DAC 계약(호스트가 타깃 프로세스 주소를 담음)이 깨진다.

실측이 이를 뒷받침한다: **접근자 시그니처를 유지하고 저장만 바꾼 결과 에러 86 → 0**, 호출부 1133곳
무수정. 대신 B3에서는 **필드 선언 전용 저장 타입**(`CompressedPtr<T>`)을 도입하는 것이 맞다 —
사용처를 건드리지 않고 타입 안전성을 얻는 방식이다.

### (B1a 이전) 최초 실행 실패 로그 — 레이아웃 불일치

```
Consistency check failed: Managed class field offset does not match unmanaged class field offset
man: 0x4, unman: 0x8, Class: System.Reflection.RuntimeAssembly, Name: _ModuleResolve
    File: src/coreclr/vm/binder.cpp:674
```

객체 헤더가 4바이트 줄어 **관리 측 필드 오프셋이 4로 이동**했는데, `object.h`의 네이티브 미러
구조체(`AssemblyBaseObject` 등)와 `binder.cpp`의 교차 검증이 8을 기대한다. 즉 **B1의 남은 핵심은
"관리/네이티브 객체 레이아웃 합의"** 이며, 구체적 대상은:

1. `vm/object.h`의 `*BaseObject` 미러 구조체 전체 (헤더 축소에 따른 오프셋 재정렬)
2. `binder.cpp`의 필드 오프셋 교차 검증 테이블
3. CoreLib 측 하드코딩 오프셋/`ARRAYBASE_SIZE`류 가정 (계량: 48곳)
4. 그 다음이 JIT 코드젠(객체→MT 4바이트 zero-extend 로드), arm64 어셈블리 12파일, crossgen2/R2R

### 변경 지점 계량 (실측 grep, `src/coreclr` 기준)

객체마다 1개씩 존재하므로 객체당 절감이 가장 큰 항목이다(8→4 바이트).
`MIN_OBJECT_SIZE`도 24→20으로 줄어들 여지가 생긴다(정렬 재검토 필요).

### 변경 지점 계량 (실측 grep, `src/coreclr` 기준)

| 항목 | 수 | 비고 |
|---|---|---|
| `m_pMethTab` 직접 접근 | 34 | 접근자 우회 지점 — 전부 손봐야 함 |
| `GetMethodTable()` 호출 | 1133 | **접근자 시그니처를 `MethodTable*` 반환으로 유지하면 무영향** |
| `MIN_OBJECT_SIZE`/`OBJECT_BASESIZE`/`ARRAYBASE_SIZE` | 48 | 레이아웃 시프트 영향 |
| arm64 어셈블리 중 MethodTable 언급 파일 | 12 | 손으로 고쳐야 함 |
| GC의 `method_table()` | 33 | 객체 크기 계산 경로 |

### 코드에서 확인된 구체적 난관 (설계 시 반드시 반영)

1. **`MARKED_BIT` (object.h:163~)** — GC가 `m_pMethTab`의 **최하위 비트를 마크 비트로 사용**한다
   (`GetGCSafeMethodTable()` 경로). 4바이트로 좁혀도 이 비트 트릭은 유지되어야 한다.
2. **`ErectWriteBarrierForMT(&m_pMethTab, pMT)`** (`SetMethodTableForUOHObject`) — collectible
   타입용 write barrier가 `MethodTable**`를 받는다. 압축 슬롯의 주소를 넘길 수 없으므로 별도
   경로가 필요하다.
3. **`GetMethodTablePtr()`** — `DPTR(PTR_MethodTable)`를 반환한다. 즉 호출자가 8바이트 슬롯
   포인터를 전제한다. 압축 시 이 API의 계약이 깨지므로 호출자 전수 조사가 필요하다.
4. **JIT 코드젠** — 객체에서 MT를 읽는 모든 코드(타입 체크, 캐스팅, 가상 디스패치, 배열 공변성
   저장 검사)가 8바이트 로드를 낸다. 4바이트 zero-extend 로드로 바꿔야 하며, **crossgen2/R2R가
   내는 코드도 동일하게 맞춰야 한다**(런타임과 AOT 이미지의 레이아웃 합의).
5. **어셈블리 헬퍼(12파일)** — 할당 경로(`AllocSlow.S`)가 MT를 객체에 써 넣고, 캐스팅/인터페이스
   디스패치(`CachedInterfaceDispatchCoreCLR.S`)가 MT를 읽는다.
6. **객체 레이아웃 시프트** — `OBJECT_SIZE`가 8→4가 되면 모든 필드 오프셋이 이동한다. VM·JIT·
   CoreLib(문자열/배열 데이터 오프셋)·GC가 동일한 레이아웃에 합의해야 한다.

### 권장 실행 순서 (B1)

1. `FEATURE_COMPRESSED_MT` 컴파일 플래그 신설(기본 off). 기본 빌드 무영향 보장.
2. `Object`의 저장만 `CompressedPtr<MethodTable>`로 교체하되 **`GetMethodTable()`/`SetMethodTable()`
   시그니처는 유지** → 1133개 호출부를 건드리지 않는다.
3. 위 난관 1~3(마크 비트, write barrier, `GetMethodTablePtr`)을 압축 슬롯 기준으로 재설계.
4. GC의 `method_table()` 경로 정렬(33곳).
5. JIT: 객체→MT 로드를 4바이트로. 먼저 **인터프리터/최소 경로로 부팅**시켜 VM 단독 검증 후 JIT 착수.
6. arm64 어셈블리 12파일.
7. crossgen2/R2R 정렬. **R2R를 끄고(JIT 전용) 먼저 통과**시킨 뒤 R2R를 맞추는 순서를 권장.

**검증:** 단계마다 저VA 환경에서 `lowvatest`(핀 주소 확인) → `lowvastress`(8스레드 무결성) →
CoreCLR 테스트 스위트 순으로 올린다.

---

## 🔖 현재 진행 상태 / 다음 작업 (2026-07-29 갱신, 새 세션 시작점)

### 빌드/테스트 표준 명령

**스위치는 하나다.** `FeatureCompressedMTFields` MSBuild 속성이 네이티브 CMake 정의와 CoreLib
C# 심볼을 **동시에** 구동한다(아래 "스위치 동기화" 절). `-cmakeargs`로 CMake 옵션을 직접 주지 말 것.

```bash
cd /work/dotnet/new/runtime
# 빌드 (Checked 필수 - assert가 레이아웃 불일치를 잡아준다)
ROOTFS_DIR=/home/clamp/Work/dotnet/rootfs/arm64.tizen \
  ./build.sh clr.runtime+clr.corelib+clr.nativecorelib -arch arm64 -rc checked --cross \
  /p:FeatureCompressedMTFields=true

# 기본(OFF) 구성: 속성을 빼면 된다. CMake 캐시의 stale ON은 자동으로 OFF로 덮어써진다.
ROOTFS_DIR=... ./build.sh clr.runtime+clr.corelib+clr.nativecorelib -arch arm64 -rc checked --cross

# testhost 갱신 (네이티브는 반드시 복사. System.Private.CoreLib.dll은 하드링크라 자동 반영됨)
B=artifacts/bin/coreclr/linux.arm64.Checked
CR=artifacts/bin/testhost/net11.0-linux-Release-arm64/shared/Microsoft.NETCore.App/11.0.0
for f in libcoreclr.so libclrgc.so corerun; do cp -f "$B/$f" "$CR/$f"; done

# 실행 (저VA 환경: -R 과 GCRegionRange 둘 다 필요)
arm64-low-va-memory-opt/tests/run-lowva.sh <app.dll> [NAME=value ...] [app args ...]
```

테스트 앱 **소스는 `arm64-low-va-memory-opt/tests/`에 보관**(세션 scratchpad는 휘발성이므로 이전됨).
각 디렉터리에서 `/work/dotnet/new/runtime/dotnet.sh build -c Release`로 빌드한다. 같은 디렉터리의
빈 `Directory.Build.props`/`.targets`가 레포 빌드 규약(분석기·라이선스 헤더)을 차단하므로 순수
SDK 빌드가 된다.

| 앱 | 무엇을 보는가 |
|---|---|
| `mtstress` | 객체 헤더의 MT 포인터를 읽는 경로 — 캐스팅/인터페이스·가상 디스패치/제네릭/리플렉션 |
| `mirrorstress` | **CoreLib 관리 미러의 모든 소비자** — statics(GC/non-GC/thread/generic), `IsClassInited`, `ExposedClassObject`, parent chain, `ValueType.Equals` 고속경로, `Stream` override 캐시, enum/delegate 분류, `Nullable<T>` unbox·배열 요소 타입 |
| `lowvastress` | 8스레드 GC 무결성 + 메모리 수치 |
| `lowvatest` | GC 힙 주소가 4GB 아래인지 |
| `sizemeasure` | shape별 인스턴스 실측 바이트 |
| `heapcensus` | 라이브 힙 구성비 프로파일 생성 (B2 절감 실측용, 아래 참조) |

진단 스위치:

| 스위치 | 내용 |
|---|---|
| `DOTNET_ValidateCompressedPtr=1` | 기동 시 4GB 불변식 + **MethodTable 레이아웃**(헤더 크기 / `m_pPerInstInfo` 오프셋 / 압축 여부) 보고. 레이아웃 불일치를 눈으로 확인하는 가장 빠른 수단 |
| `DOTNET_CompressedPtrHeapCensus=1` | 전체(gen2) GC마다 **라이브 힙 구성비와 B1/B2 절감 실측** 보고. **아무 앱에나 붙일 수 있다** — 실제 Tizen 앱에 그대로 걸면 된다 |

> ⚠️ 빌드 대기 시 `ps | grep "eng/common/build.sh"` 패턴은 **자기 자신을 매칭**해 무한 루프가 된다.
> `grep -qE "[n]inja -j"` 대괄호 트릭이나 로그의 `Build succeeded|Build FAILED` 마커로 판정할 것.

### 플래그 (기본 OFF - 기본 빌드 무영향, 실측 확인)

| 옵션 | 켜는 방법 | 내용 | 상태 |
|---|---|---|---|
| `FeatureCompressedMT` | `/p:FeatureCompressedMT=true` | 객체 헤더의 MT 슬롯 4B (+ 패딩) | ✅ 동작 검증 완료 |
| `FeatureCompressedMTFields` | `/p:FeatureCompressedMTFields=true` | MethodTable 내부 포인터 필드 4B | ✅ **동작 검증 완료 (2026-07-29)** |
| `FeatureCompressedRefs` | `/p:FeatureCompressedRefs=true` | 힙 참조 접근을 4B로 (저장 폭은 아직 8B) | ✅ **부분 완료, 동작 검증** — 참조 접근 47/53 좁혀짐. 아래 "B2 1차 착수" 참조 |

### ✅ 스위치 동기화 (완료) — 단일 MSBuild 속성

이전에는 CMake 옵션과 CoreLib 정의를 손으로 함께 켜야 했고, 빠뜨리면 컴파일은 통과하고 기동 중
SIGSEGV가 났다. 레포에 이미 있던 선례(`FeatureInterpreter`)와 같은 방식으로 정리했다:

| 파일 | 역할 |
|---|---|
| `src/coreclr/runtime.proj` | 속성 → `-cmakeargs -DFEATURE_COMPRESSED_MT_FIELDS=ON/OFF` |
| `src/coreclr/clr.featuredefines.props` | 속성 → CoreLib `DefineConstants` |
| `src/coreclr/clrdefinitions.cmake` | CMake 옵션 정의 + "손으로 주지 말 것" 주석 |

**값을 항상 명시적으로 넘긴다**(조건부로 인자를 생략하지 않는다)는 점이 핵심이다. CMake는 옵션을
캐시하므로, 조건부로만 넘기면 이전 빌드의 `ON`이 캐시에 남아 CoreLib만 OFF가 되는 **반대 방향의
불일치**가 생긴다. 실측 검증:

| 빌드 명령 | CMake 캐시 | CoreLib 미러 | 테스트 |
|---|---|---|---|
| `/p:FeatureCompressedMTFields=true` | `ON` | 압축 | mtstress / mirrorstress / lowvastress PASS |
| (속성 없음, 직전이 ON이었음) | **`OFF`로 자동 전환** | 비압축 | mtstress / mirrorstress PASS |

### ✅ B3(일괄 MethodTable 압축) 완료 — 4개 레이아웃 계약 층 모두 정렬

`m_pParentMethodTable` + `m_pModule` + `m_pAuxiliaryData` 3개를 4B로 압축하려면 넷을 동시에
맞춰야 했고, 넷 다 맞췄다:

| # | 계약 층 | 상태 |
|---|---|---|
| 1 | C++ `MethodTable` 구조체 (`vm/methodtable.h`) | ✅ |
| 2 | `SIZEOF__MethodTable_` = **vtable 시작 오프셋** (`methodtable.h`) | ✅ |
| 3 | arm64 `asmconstants.h` `OFFSETOF__MethodTable__m_pPerInstInfo` (0x38→0x30) | ✅ |
| 4 | **CoreLib 관리 미러** (`RuntimeHelpers.CoreCLR.cs`) | ✅ **완료** |

**JIT은 무수정.** vtable 오프셋을 런타임에 EE에 질의하고(`vm/jitinterface.cpp` `getMethodVTableOffset`),
JIT 소스에는 MethodTable 필드 오프셋 하드코딩이 없다(`grep offsetof(MethodTable` → 0건).
arm64 어셈블리도 `asmconstants.h`의 `m_pPerInstInfo` 하나만 걸렸고 `ASMCONSTANTS_C_ASSERT`가
그 정합성을 컴파일 타임에 보증한다.

#### 층 4의 구현 (이번 작업)

압축 시 필드는 `0x10/0x14/0x18`(각 4B)에 놓이고, 뒤따르는 8B 정렬 union이 `0x20`으로 재정렬되므로
**그 이후 전부 12바이트가 아니라 8바이트 내려간다**(4B 패딩이 생김). 실측 오프셋:

| 필드 | 기본 | 압축 |
|---|---|---|
| `ParentMethodTable` | 0x10 (+debug 8) | 0x10, **4B** |
| `Module` | 0x18 | 0x14, **4B** |
| `AuxiliaryData` | 0x20 | **0x18**, 4B |
| `m_pEEClass`/`m_pCanonMT` union | 0x28 | 0x20 (4B 패딩 후) |
| `ElementType`/`PerInstInfo` | 0x30 | **0x28** |
| `InterfaceMap` | 0x38 | **0x30** |
| vtable 시작 = `SIZEOF__MethodTable_` | 0x40(=72, debug) | **0x40−8 = 64 (debug)** |

- `src/coreclr/System.Private.CoreLib/System.Private.CoreLib.csproj` — `FeatureCompressedMTFields`
  MSBuild 속성 → `FEATURE_COMPRESSED_MT_FIELDS` C# 심볼. **CMake 옵션은 관리 코드에 도달하지 않으므로
  이 두 스위치는 별개이며, 함께 켜는 것이 호출자 책임이다**(아래 "남은 위험" 참조).
- `RuntimeHelpers.CoreCLR.cs` — `ParentMethodTable`/`AuxiliaryData`를 4바이트 `private uint` 필드 +
  zero-extend 프로퍼티로 교체. **호출부는 한 곳도 수정하지 않았다** — 실측상 이 두 멤버는 관리
  코드 전체에서 **읽기 전용**으로만 쓰이고(대입·주소 취득 없음, 총 28개 사용처) 필드→프로퍼티
  치환이 소스 호환이기 때문이다. 오프셋 상수 3개(`AuxiliaryDataOffset`/`ElementTypeOffset`/
  `InterfaceMapOffset`)도 조건부로 분기시켰다.
- `vm/compressedptrvalidate.cpp` — 진단에 MethodTable 레이아웃 보고를 추가(아래 실측치의 출처).

#### 검증 결과 (arm64 Checked, 저VA 환경, qemu -R 4GB)

| 앱 | 압축 ON | 기본 OFF |
|---|---|---|
| `mtstress` | PASS (errors 0) | PASS |
| `mtstress` + `ReadyToRun=0` | PASS | — |
| `mtstress` + `TieredCompilation=0` | PASS | — |
| `mirrorstress` | PASS (errors 0) | PASS |
| `lowvatest` | PASS, 주소 20/20 4GB 아래 | — |
| `lowvastress` 8스레드/2058MB | **무결성 오류 0, PASS** | 무결성 오류 0, PASS |
| `sizemeasure` | 정상 (객체당 절감 0 — 예상대로, B2 사안) |  |

**착수 전 상태와의 대비(반증 근거):** 같은 네이티브 바이너리 + 미정렬 CoreLib에서는
`mtstress`가 `StaticsHelpers.GetGCStaticBase`에서 SIGSEGV(EXIT=139)했다. 미러 정렬 후 EXIT=100.

#### 실절감: MethodTable당 8바이트 (실측)

`DOTNET_ValidateCompressedPtr=1` 보고, 동일 앱·동일 환경:

```
OFF: MethodTable header  72 bytes, m_pPerInstInfo@0x38  (full width fields)
ON : MethodTable header  64 bytes, m_pPerInstInfo@0x30  (compressed fields)
```

Checked(`_DEBUG`) 72→64B, Release 64→56B. **포인터 3개(24B)를 12B로 줄였지만 정렬 패딩이 4B를
되돌려 실절감은 8B**다.

⚠️ **이 8B는 타입당이며 로더 힙에만 영향한다.** 앱이 N개 타입(제네릭 인스턴스화 포함)을 로드하면
8N 바이트, 즉 수천 타입에서 수십 KB 수준이다. `lowvastress`의 `VmRSS`/`committed` 수치는 실행 간
편차(gen2 GC 20~33회)가 이 크기를 훨씬 넘으므로 **이 워크로드로는 측정되지 않는다.**
객체당 절감은 여전히 0이며 **물리 메모리 절감의 본체는 B2**라는 결론은 그대로다.

### ✅ CoreCLR 테스트 스위트 — qemu 저VA에서 실행 가능 (2026-07-29 확인)

지금까지 검증은 직접 쓴 앱 6개뿐이었다. **upstream CoreCLR 런타임 테스트를 저VA/qemu 환경에서
그대로 돌릴 수 있음을 확인했고, JIT 코드젠 기본기 스위트(`JIT/CodeGenBringUpTests`) 641개가
압축 구성에서 전부 통과했다.**

```bash
# 1) Core_Root 레이아웃 (IsXUnitLogCheckerSupported=false 가 필수 - 아래 참조)
ROOTFS_DIR=/home/clamp/Work/dotnet/rootfs/arm64.tizen \
  ./src/tests/build.sh -arch arm64 -checked -cross -generatelayoutonly \
  /p:IsXUnitLogCheckerSupported=false

# 2) 테스트 트리 빌드 (-priority1 필수: CodeGenBringUpTests는 CLRTestPriority=1)
ROOTFS_DIR=/home/clamp/Work/dotnet/rootfs/arm64.tizen \
  ./src/tests/build.sh -arch arm64 -checked -cross -priority1 -skipnative -skipgeneratelayout \
  -tree JIT/CodeGenBringUpTests /p:IsXUnitLogCheckerSupported=false

# 3) 실행 (merged runner 단위)
B=artifacts/tests/coreclr/linux.arm64.Checked/JIT/CodeGenBringUpTests
for c in d do r ro; do
  arm64-low-va-memory-opt/tests/run-lowva-coreclrtest.sh $B/JIT.CodeGenBringUpTests_$c
done
```

| 구성 | 통과 | 실패 | exit |
|---|---|---|---|
| `JIT.CodeGenBringUpTests_d` (debug/non-opt) | 160 | 0 | 100 |
| `_do` (debug/opt) | 160 | 0 | 100 |
| `_r` (release/non-opt) | 161 | 0 | 100 |
| `_ro` (release/opt) | 160 | 0 | 100 |

**161개가 5.3초.** merged runner가 프로세스 기동 비용을 분산시켜 qemu에서도 빠르다.

**음성 대조:** 같은 바이너리를 `-R` 없이 돌리면 `inc/compressedptr.h:58`의 assert가 정확히 걸린다
("Address does not fit in a compressed pointer"). 641개 통과가 우연이 아니라 **유효한 4GB 불변식
아래의 결과**임을 확인한 것이다.

#### 가능하게 만든 두 가지 발견 (다음 세션이 반드시 알아야 함)

1. **`XUnitLogChecker`가 블로커다.** 레이아웃 생성이 여기서 실패한다 — NativeAOT로 발행되는데
   Tizen rootfs에 `crtbeginS.o`/`libgcc`가 NativeAOT 링크 라인이 찾는 경로에 없다. CI 로그
   후처리 전용 도구이므로 `/p:IsXUnitLogCheckerSupported=false`로 끄면 된다
   (`Directory.Build.props:253`에서 정의되는 속성).
2. **개별 테스트 dll은 직접 실행되지 않는다.** `corerun Add1_d.dll` → "Entry point not found".
   현재 테스트는 `Main`이 없는 xunit `[Fact]` 라이브러리이고, 같은 디렉터리의
   `JIT.<Area>_{d,do,r,ro}.dll`이 **생성된 `Main`을 가진 merged runner**다. 또한 **작업 디렉터리가
   merged 디렉터리여야 한다**(runner가 옆의 테스트 dll들을 로드한다).

#### 확장 시 미검증 제약 (낙관하지 말 것)

전체 `src/tests`는 4802개 프로젝트(JIT 2801, GC 696, Loader 439, baseservices 365 …). 실행 시간은
위 비율대로면 병목이 아니고 **빌드가 지배적**이다(645개에 약 10분). 확인하지 않은 것 3가지:

1. **네이티브 테스트 컴포넌트를 `-skipnative`로 건너뛰었다.** Interop 등은 네이티브 빌드가 필요하고,
   그건 런타임과 같은 CMake 경로라 아마 되겠지만(런타임 크로스 빌드는 성공하므로) **검증 안 했다.**
2. **4GB VA 제한이 GC 스트레스 테스트에 걸릴 수 있다.** `GCRegionRange=1GB`로 돌리므로 크게
   할당하는 테스트는 OOM 소지가 있다.
3. qemu 타임아웃 — 장시간 테스트는 조정이 필요할 수 있다.

다음으로 값어치가 큰 트리: **`Loader`**(타입 로딩 — MethodTable 레이아웃 직결)와 **`GC`**.

### 남은 위험 / 알려진 공백

1. ~~두 스위치의 동기화가 수동이다.~~ → **해결됨** (위 "스위치 동기화" 절). 남은 잔여 위험은
   누군가 `-cmakeargs`로 CMake 옵션을 직접 주는 경우뿐이며, `clrdefinitions.cmake`에 하지 말라는
   주석을 남겼다. 기동 시 fail fast 검증은 여전히 없다(관리 미러의 기대 오프셋을 네이티브가 알
   방법이 없어 별도 채널이 필요하다). `DOTNET_ValidateCompressedPtr=1`의 레이아웃 보고가 확인 수단.
2. **cDAC 데이터 디스크립터가 미정렬.** `vm/datadescriptor/datadescriptor.inc`가
   `MethodTable.{Module,ParentMethodTable,AuxiliaryData}`를 `T_POINTER`(8B)로 선언한다. 오프셋은
   `offsetof`에서 오므로 맞지만 **폭이 틀리다.** 압축 구성에서 SOS/덤프 분석이 이 필드들을 오독한다.
   런타임 실행에는 영향 없음. 고치려면 `src/native/managed/cdac/` 리더 측도 함께 손대야 한다.
3. **레거시 DAC(`libmscordaccore`)**는 같은 접근자(`GetParentMethodTable()` 등)를 통과하므로
   빌드·오프셋은 맞지만 압축 구성에서의 의미 검증은 하지 않았다.
4. **실기기(rpi4)에서는 `qemu -R`이 없다** — 4GB 불변식을 만들 다른 수단(Phase A의 LOWVA 코드
   또는 커널 레벨)이 필요하다. 현재 검증은 전부 QEMU 한정이다.
5. ~~정식 CoreCLR 테스트 스위트 미실시.~~ → **경로 확보** (위 "CoreCLR 테스트 스위트" 절).
   `JIT/CodeGenBringUpTests` 641개 통과. 단 아직 **JIT 코드젠 기본기 한 트리뿐**이며 `Loader`/`GC`/
   `Interop`은 미실행이다.

### 커밋되지 않은 변경 (main 브랜치, 커밋 안 함)

네이티브: `clrdefinitions.cmake`, `runtime.proj`, `clr.featuredefines.props`,
`inc/compressedptr.h`(신규), `vm/compressedptrvalidate.cpp`(신규), `vm/methodtable.h`,
`vm/methodtable.cpp`, `vm/methodtablebuilder.cpp`, `vm/generics.cpp`, `vm/class.cpp`,
`vm/object.h`, `vm/object.inl`, `vm/gchelpers.cpp`, `vm/stubhelpers.cpp`, `vm/ceemain.cpp`,
`vm/gcenv.ee.cpp`, `vm/CMakeLists.txt`, `vm/arm64/asmconstants.h`, `inc/clrconfigvalues.h`

관리: `System.Private.CoreLib/src/System/Runtime/CompilerServices/RuntimeHelpers.CoreCLR.cs`

문서/테스트(추적 대상 아님): `arm64-low-va-memory-opt/`

---

## 📋 압축 인벤토리 — 지금까지 바꾼 것 / 앞으로 바꿀 것 (2026-07-29)

### 세 가지 서로 다른 불변식 (이 구분이 우선순위를 결정한다)

압축 가능성은 "그 포인터가 무엇을 가리키는가"에 달렸고, **4GB 아래라는 보장은 대상별로 별개**다:

| # | 불변식 | 대상 | 현재 상태 |
|---|---|---|---|
| I1 | **GC 힙** < 4GB | 객체 참조(필드·배열 요소·statics) | ✅ B0에서 실측 증명, `ValidateCompressedPtr`가 상시 검사 |
| I2 | **로더 힙** < 4GB | MethodTable, EEClass, MethodDesc, aux data | ✅ B0에서 실측 증명 (대표 MT 주소 probe) |
| I3 | **코드 힙(PCODE)** < 4GB | vtable 슬롯, 프리코드, 스텁 | ❌ **증명 안 됨.** `ValidateCompressedPtr`가 probe하지 않는다. QEMU `-R`에서는 성립하지만 실기기에서는 PAL의 코드용 1GB 슬랩 배치에 달려 있다(Phase A1b 미착수) |

→ **I3 대상(vtable 슬롯 등)은 착수 전에 validator 확장이 선행되어야 한다.** 지금 손대면 QEMU에서만
동작하고 실기기에서 조용히 깨진다.

### ✅ 지금까지 압축한 것

| 대상 | 크기 | 플래그 | 불변식 | 실절감 | 검증 |
|---|---|---|---|---|---|
| `Object::m_pMethTab` (객체 헤더의 MT 슬롯) | 8→4B **+ 4B 패딩** | `FeatureCompressedMT` | I2 | **0** (패딩이 되돌림, 의도됨) | lowvatest/lowvastress |
| `MethodTable::m_pParentMethodTable` | 8→4B | `FeatureCompressedMTFields` | I2 | 타입당 | mtstress/mirrorstress/BringUpTests 641 |
| `MethodTable::m_pModule` | 8→4B | 〃 | I2 | 합계 | 〃 |
| `MethodTable::m_pAuxiliaryData` | 8→4B | 〃 | I2 | **8B** | 〃 |

- MethodTable 헤더 **72→64B (Checked)**, 64→56B (Release). 포인터 3개 24B→12B지만 뒤따르는 8B 정렬
  union이 4B를 패딩으로 되돌려 실절감 8B.
- **객체당 절감은 아직 0이다.** 위 두 항목 모두 로더 힙만 줄인다.

### ❌ 앞으로 바꿀 것 — 절감 크기 순

절감의 본체는 **I1(객체 참조)** 이고, 그것이 전부 B2다. 나머지(I2/I3)는 타입 수에 비례할 뿐
객체 수에 비례하지 않아 효과가 한 자릿수 KB~수십 KB에 머문다.

#### 1순위 — B2b: 인스턴스 필드 참조 (I1)

| 항목 | 값 |
|---|---|
| 라이브 힙 점유 | **31~43%** (census 실측) |
| 절감률 | **33.3%** |
| 난이도 | 최고 — JIT 타입 모델에 축 추가 필요 |

census 실측상 **참조 배열보다 점유·절감 모두 크다.** 문서가 원래 B2a(배열)를 먼저로 잡았던 것은
shape별 비율만 본 판단이었고, 실제 힙 구성비로는 순위가 뒤바뀐다.

#### 2순위 — B2a: 참조 배열 요소 (I1)

| 항목 | 값 |
|---|---|
| 라이브 힙 점유 | **28.8~30.3%** |
| 절감률 | **25~49%** (요소 수가 많을수록 큼) |
| 난이도 | 최고이나 접근 경로는 B2b보다 좁다(`GT_INDEX_ADDR` 중심) |

**1·2순위 공통 블로커:** `jit/typelist.h:57`의 `TYP_REF` 크기가 하나뿐이라 "레지스터에서 8B / 힙
슬롯에서 4B"를 표현할 수 없다. `Span<T>`/`Unsafe.Add`의 `sizeof(T)` 의미까지 갈라진다.
상세는 아래 "B2 착수 범위 산정" 절.

#### 3순위 — B2c: static 필드 / 핸들 테이블 (I1)

`DynamicStaticsInfo::m_pGCStatics`/`m_pNonGCStatics`는 **하위 비트를 초기화 플래그로 사용하는
`TADDR`** 이다(`ISCLASSNOTINITEDMASK`). 폭을 줄이면 그 인코딩을 재설계해야 한다. 점유는 작다.

#### 4순위 — B3 확장: 남은 로더 힙 포인터 (I2, 저위험)

JIT 가시성이 낮아 B1a/B3와 같은 "접근자 시그니처 유지" 전략이 통할 가능성이 높은 것들:

| 대상 | 크기 | 비고 |
|---|---|---|
| `MethodTableAuxiliaryData::m_pLoaderModule` | 8B/타입 | 순수 로더 힙, 접근자 2개(`GetLoaderModule`/`SetLoaderModule`)뿐 — **가장 쉬운 다음 항목** |
| `MethodTable::m_pEEClass`/`m_pCanonMT` union | 8B/타입 | 하위 1비트를 태그로 사용(`UNION_MASK`) → 4B에서도 가능하나 `TADDR` 산술 전수 확인 필요 |
| `MethodTable::m_pPerInstInfo`/`m_ElementTypeHnd` union | 8B/타입 | **arm64 어셈블리가 읽는다**(`asmconstants.h`) + JIT이 배열 요소 타입 핸들을 읽음 → I2지만 JIT 가시 |
| `MethodDescChunk::MethodTable`, `Next` | 8B×2/청크 | 로더 힙. CoreLib 관리 미러에도 존재(`RuntimeHelpers.CoreCLR.cs`) → 미러 동반 수정 |
| `MethodTableAuxiliaryData::m_hExposedClassObject` | 8B/타입 | OBJECTHANDLE = 핸들 테이블 주소. 핸들 테이블이 4GB 아래인지 **별도 확인 필요** |
| aux data의 debug 전용 포인터 3개 | 24B/타입 (debug만) | Release에 영향 없음 |

#### 압축 불가 / 인코딩 재설계가 선행되어야 하는 것

| 대상 | 이유 |
|---|---|
| `MethodTable::m_encodedNullableUnboxData` | 64bit에서 **상위 32비트에 value field 크기를 패킹**한다(`(size << 32) \| offset`, `methodtable.h` `SetNullableDetails`). 슬롯 전체 8B가 이미 쓰이고 있어 폭을 줄일 수 없다. `m_pInterfaceMap`과 union이므로 이 슬롯은 사실상 압축 대상 제외 |
| vtable 슬롯 (PCODE) | **I3 미증명.** 게다가 JIT이 가상 디스패치에서 직접 읽는다. validator 확장이 선행 조건 |
| vtable indirection 포인터 | 8B × ⌈virtuals/8⌉. 부모와 청크를 공유하며 JIT이 2단 로드로 읽는다 → JIT 동반 변경 |
| interop 경계의 네이티브 주소 | 명세서 §7대로 `IntPtr` full width 슬롯 유지가 설계다 |

#### 아직 측정되지 않은 것 (다음 도구 후보)

GC 힙은 census로 실측하게 됐지만 **로더 힙 총량은 측정 도구가 없다.** 그래서 4순위 항목들의
합계 절감이 "수십 KB"라는 것도 구조적 추정일 뿐 실측이 아니다. census와 같은 방식으로 로더 힙
census를 붙이는 것이 4순위 착수의 정당화 수단이 될 것이다.

---

## 📊 B2 Go/No-Go 데이터 — 라이브 힙 census 실측 (2026-07-29)

문서가 B2 착수의 선행 조건으로 지목했던 "대상 워크로드의 객체 구성비"를 **추정이 아니라 실측**으로
답할 수 있게 됐다. `DOTNET_CompressedPtrHeapCensus=1`을 붙이면 전체(gen2) GC마다 라이브 힙을
순회해 shape별 구성비와 B1/B2 투영 절감을 보고한다.

- 구현: `vm/compressedptrvalidate.cpp`의 `ReportCompressedPtrHeapCensus()`, 훅은
  `GCToEEInterface::DiagGCEnd`(EE가 이미 정지돼 있고 힙이 순회 가능한 지점 — 프로파일러 힙 워크와
  동일한 문맥). 참조 슬롯 수는 GC 자신의 서술자 `CGCDesc::GetNumPointers`로 세므로 **B2가 실제로
  좁힐 슬롯만** 계산한다(collectible 타입의 합성 LoaderAllocator 참조는 제외).
- 투영 모델은 `sizemeasure`와 **동일**하다(`B1 = align8(size-4)`,
  `B1+B2 = align8(size-4-4*refs)`). MIN_OBJECT_SIZE가 헤더와 함께 줄어든다고 가정하며, 그 가정에
  의존하는 양을 별도 줄로 함께 보고한다.
- **아무 앱에나 붙일 수 있다.** 실제 Tizen 앱에 환경변수만 걸면 그 앱의 답이 나온다.

### 측정 결과 (arm64 Checked, 저VA 환경)

| 워크로드 | 라이브 바이트 | B1 절감 | **B1+B2 절감** |
|---|---|---|---|
| `heapcensus startup` — 프레임워크 기저 힙만 | 42 KB | 2.5% | **18.3%** |
| `lowvastress` 정상 상태 | 114 KB | 2.2% | **16.1%** |
| `heapcensus collections` — 참조 리치 | 2.2 MB | 0.0% | **22.0%** |
| `heapcensus mixed` — 앱 코드 유사 | 3.9 MB | 1.6% | **20.4%** |
| `heapcensus arrays` | 0.7 MB | 0.1% | 12.8% |

> `arrays` 프로파일의 12.8%가 낮은 것은 배열 때문이 아니다. 이 프로파일은 `object[16]` 1250개와
> 그것이 가리키는 **빈 `object` 20000개**를 만드는데, 후자가 힙의 66.7%를 차지하고 절감이 0%다.
> 즉 "참조 배열이 있어도 그 끝에 달린 잎 객체가 값 없는 헤더뿐이면 전체 이득이 희석된다"는 사례이며,
> 배열 자체는 표 아래 shape별 수치대로 42.7% 절감한다.
| `heapcensus text` — 문자열/버퍼 위주 | 3.4 MB | 0.0% | **2.8%** |

shape별로 보면 (mixed 프로파일):

| shape | 힙 점유 | B1+B2 절감 |
|---|---|---|
| 참조를 가진 객체 | 31.0% | **33.3%** |
| 참조 배열 | 28.8% | **29.5%** |
| 참조 없는 객체 | 18.6% | 0% |
| 문자열 | 12.0% | 13.2% (B1 몫) |
| 값 배열 | 9.7% | 0% |

### 결론 (B2 착수 판단의 근거)

1. **B2의 절감은 워크로드에 따라 2.8% ~ 22%로 8배 차이가 난다.** 문자열·바이트 버퍼가 지배하는
   워크로드에서는 3% 수준이므로 B2의 비용이 정당화되지 않는다. **실제 앱에서 이 수치를 먼저 재야
   한다** — 이제 도구가 있으므로 그것이 첫 단계다.
2. **프레임워크 자체의 기저 힙만으로도 18.3%다.** 앱 코드가 아무리 값 타입 위주여도 최소한
   프레임워크 몫은 있으므로, 작은 앱일수록 상대 이득이 크다는 뜻이다.
3. **문서의 B2 착수 순서(B2a 배열 먼저)는 데이터와 어긋난다.** `sizemeasure`의 shape별 비율
   (배열 42~49% vs 필드 33%)만 보면 배열이 커 보이지만, 실제 힙에서는 **"참조를 가진 객체"가
   점유(31~43%)와 절감(33.3%) 모두에서 참조 배열보다 크다.** 즉 절감 관점에서는 **B2b(인스턴스
   필드)가 B2a(배열)보다 우선순위가 높다.** 다만 접근 경로의 좁음(위험도)은 여전히 배열이 유리하며,
   이 트레이드오프는 실제 앱 측정 결과를 보고 결정해야 한다.
4. **"참조 없는 객체" + "값 배열"이 mixed에서 28%를 먹고 절감 0%다.** B2가 어떤 형태로 완성되든
   힙의 1/4~1/3은 손댈 수 없다는 상한이 존재한다.

---

## 🔬 B2 1차 착수 (2026-07-29) — 1단계 완료, 2단계 미해결

### 1단계 ✅ — Zero Extension 전제 실증 (`DOTNET_CompressedPtrHeapCensus=2`)

census 워크에 참조 슬롯 단위 검사를 추가했다(`ValidateReferenceSlotCallback`, `DiagWalkObject2`가
슬롯 값과 슬롯 주소를 함께 준다).

| 워크로드 | 검사한 슬롯 | 4GB 초과 | 4B 정렬 위반 |
|---|---|---|---|
| startup (프레임워크 기저) | 309 | **0** | 0 |
| mixed | 140,466 | **0** | 0 |
| collections | 100,314 | **0** | 0 |
| lowvastress (8스레드) | 1,100 | **0** | 0 |

> `DiagWalkObject2`는 **non-null 참조만** 보고하므로 검사 수가 총 슬롯 수보다 적다(mixed는
> 157,980 중 140,466 — 나머지는 null).

### 이 사실이 바꾸는 설계 판단

문서는 B2의 최대 난관을 "`jit/typelist.h`의 `TYP_REF` 크기가 하나뿐"으로 봤다. 그것보다 중요한
사실은 **모든 참조 값의 상위 32비트가 0**이라는 것이다. 리틀엔디언에서:

| 접근 폭 → 슬롯 폭 | 결과 |
|---|---|
| 8B → 8B | 정상 |
| **4B → 8B** | **정상** (읽기=하위 절반, 쓰기 후 상위 0 유지) |
| 4B → 4B | 정상 |
| 8B → 4B | **오류** |

불건전한 조합이 하나뿐이므로 **"접근 폭을 먼저 4B로, 저장 폭은 나중에 shape별로"** 라는 순서가
가능하고, 4B 슬롯과 8B 슬롯이 공존해도 안전하다. 또한 스택·레지스터 슬롯이 8B로 유지되므로
**GC info·스택워킹·EH를 건드리지 않는다**(JIT은 GC 참조 지역변수를 프롤로그에서 0으로 초기화하므로
스택 슬롯의 상위 절반도 0이다).

`ref object`/`Span<object>`의 byref가 스택 슬롯(8B)과 힙 슬롯(4B) 어느 쪽도 가리킬 수 있다는
모호성도 이 성질로 무해해진다.

### 2단계 ✅ — JIT 접근 폭 축소 (부분 완료, 동작 검증)

**결과: `mtstress`의 참조 접근 53건 중 47건이 4바이트로 좁혀지고, 641개 테스트 스위트 + 앱 5개가
전부 통과한다.** 저장 폭은 여전히 8바이트다(설계대로).

| | |
|---|---|
| 플래그 | `/p:FeatureCompressedRefs=true` (기본 OFF) |
| 좁혀지는 지점 | `genCodeForIndir`(load) / `genCodeForStoreInd` 비-barrier 경로(store) |
| 좁힘 판정 | `emitNarrowGCRefAccess` (`jit/instr.h`) + `genNarrowGCRefAccessForIndir` (`jit/codegen.h`) |
| shape 선택 | `DOTNET_JitCompressedRefsMask` (기본 `0x1F` = 안전 확인된 전부) |

실측 커버리지 (`mtstress`, `DOTNET_JitDump=*` 계측):

| | 건수 | shape |
|---|---|---|
| **좁혀짐** | **47** | contained LEA load 23 / CNS_INT load 14 / LCL_VAR load 10 |
| full width 유지 | 6 | 스택 store 3 (`notHeap=1`) / 배열 인덱스 load 3 (`hasIndex=1`) |

full width로 남은 6건은 **의도적 제외**다. 스택은 8바이트로 유지되어야 하고(GC가 8바이트로 스캔),
배열 인덱스는 스케일 변경이 4단계 사안이다. 힙 store가 거의 안 보이는 이유는 대부분
write barrier 헬퍼를 타기 때문이며 그게 3단계다.

#### 🔑 근본 원인: `EA_GCREF`는 플래그가 아니라 **특수값**이다

여기서 막혀 세 번 실패했다. 원인은 `emit.cpp` `emitAllocInstr`:

```cpp
if (EA_IS_GCREF(opsz)) { id->idGCref(GCT_GCREF); id->idOpSize(EA_PTRSIZE); }
```

**GC 플래그가 붙으면 operand size가 무조건 `EA_PTRSIZE`(8)로 강제되고 크기 비트가 버려진다.**
`EA_GCREF = EA_GCREF_FLG | EA_PTRSIZE`라는 정의를 보고 "크기와 GC-ness가 직교한다"고 판단했는데
틀렸다. 그 결과 `EA_GCREF_FLG | EA_4BYTE`는:

1. 명령은 **8바이트로** 나가고 (좁혀지지 않음),
2. 그런데 `emitIns_R_R_I`는 이미 4바이트 기준으로 `imm = 8 >> 2 = 2`로 스케일해 두었으므로,
3. 실효 오프셋이 `2 × 8 = 0x10`이 되어 **필드 오프셋이 배가되고 힙이 손상된다**
   (`ldr x0, [x0, #0x10]`, 원래는 `#0x08`).

**해결:** GC 플래그를 **버리고** 순수 `EA_4BYTE`로 낸다. 레지스터의 GC 여부는 emit attr이 아니라
트리 타입에서 오므로 안전하다 — `genProduceReg`가 `gcMarkRegPtrVal(reg, tree->TypeGet())`를 호출한다
(`codegenlinear.cpp`). 이후 `ldr w0, [x0, #0x08]`로 정확히 나온다.

#### 진행 기록 (같은 실수를 반복하지 않기 위해)

| 시도 | 가드 | 결과 |
|---|---|---|
| 1 | 없음 | `assert(shiftAmount == scale)` — `emitarm64.cpp:7546`. arm64는 접근 폭이 스케일 인덱스 주소 모드의 shift를 결정한다 |
| 2 | contained 주소 전부 제외 | 테스트 전부 통과했지만 **JitDisasm 확인 결과 no-op**. arm64 lowering이 거의 모든 주소를 contained LEA로 감싼다 |
| 3 | 인덱스+스택만 제외 | 객체 헤더 손상 (`m_alignpad == 0`). 위 근본 원인 |
| 4 | 3 + GC 플래그 제거 | ✅ 47/53 좁혀짐, 전부 통과 |

**교훈 두 가지:**

1. **코드젠 변경은 JitDisasm으로 실제 반영을 확인한 뒤에만 테스트 결과를 해석한다.** 시도 2에서
   no-op 상태의 "641개 통과"를 믿을 뻔했다.
2. **shape별 이등분은 리빌드가 아니라 config로 한다.** `JitCompressedRefsMask`를 만든 뒤 원인
   분리가 한 번의 실행으로 끝났다. 그 전까지 25분 빌드를 반복하며 낭비했다.

#### ⚠️ 반복 비용: JIT만 빌드하기 (다음 세션 필수)

`./build.sh clr.runtime` 전체는 약 25분, JIT만은 약 2분이다:

```bash
cd artifacts/obj/coreclr/linux.arm64.Checked && ninja libclrjit.so
cp artifacts/obj/coreclr/linux.arm64.Checked/jit/libclrjit.so <CORE_ROOT>/libclrjit.so
```

> ❌ **`cmake -D... .`를 직접 부르지 말 것.** 크로스 빌드 캐시의 arch 변수가 유실되어
> `Arch is . Only arm, arm64, ...`로 트리가 빌드 불가 상태가 된다. 복구는 `build.sh`를 다시 도는 것.
> 플래그를 바꿔야 할 때만 `build.sh`를 쓰고, 소스만 고칠 때는 위 ninja 경로를 쓴다.

### 3단계 이후 (미착수)

| 단계 | 내용 |
|---|---|
| 3 | write barrier의 참조 저장을 4B로. arm64는 `vm/arm64/patchedcode.S`의 `stlr x15, [x14]` 등 소수 명령 + GC shadow 갱신(`str x15, [x12]`) |
| 3b | 배열 인덱스 shape: `GT_LEA` 스케일 8→4를 접근 폭과 함께 바꾼다 (현재 유일하게 남은 load 제외 항목) |
| 4 | GC 스캔 stride, 그리고 shape별 저장 폭 축소 (VM 레이아웃 + CGCDesc) |

3단계까지는 저장 폭이 8B이므로 **여전히 동작 보존이고 독립 검증 가능**하다. 절감은 4단계부터 나온다.

### 현재 플래그 상태

`FeatureCompressedRefs`는 기본 OFF이며, **ON/OFF 양쪽 모두 QEMU·실기기에서 검증된 상태**다:

| 구성 | 앱 6개 | BringUpTests | Loader | GC |
|---|---|---|---|---|
| **QEMU** `FeatureCompressedMTFields=true` (Refs OFF) | PASS | 641 PASS | — | — |
| **QEMU** `+ FeatureCompressedRefs=true` (마스크 기본 0x1F) | PASS | 641 PASS | — | — |
| **실기기** OFF (shim 없음, 기준선) | PASS | **641 PASS** | **390 / 2 fail** | **104 / 1 fail** (5회 반복 동일) |
| **실기기** ON 양 플래그 + `lowvashim` | PASS | **641 PASS** | **390 / 2 fail** | **104 / 1 fail** (6회 중 5회) |

Loader/GC 공통 실패 3건은 전부 환경 요인이고 **OFF와 ON의 실패 목록이 동일**하다 → 압축 회귀 없음.
(`-skipnative` 2건 + 하네스의 `GCRegionRange=1GB` 1건. 상세는 `HANDOVER.md` §3-3.)

⏳ **미판정 1건:** GC 트리에서 `Frozen` 외 간헐 실패가 **ON 6회 중 1회** 관측됐고 이름은 로그
truncate로 유실됐다. **OFF 5회는 전부 깨끗했지만 그 차이는 통계적으로 유의하지 않다** — 실패율이
양쪽 1/6로 같아도 OFF 5회가 깨끗할 확률이 `(5/6)^5≈40%`다. 판정에는 양쪽 20회 수준의 반복이
필요하다. 상세와 진행 중 했던 두 번의 오판 기록은 `HANDOVER.md` §3-3.

### ✅ 실기기 검증 (2026-08-03) — rpi4 / Tizen 11.0 aarch64

상세는 `HANDOVER.md` §3-3. 요점만:

- 디바이스: Tizen 11.0 aarch64 (`20260802.230107`), 커널 6.12.80, **glibc 2.40 — rootfs와 일치**.
- 실기기에는 `qemu -R`이 없다. `tests/vaprobe`로 커널이 무엇을 허용하는지 직접 재보니:
  평범한 `mmap(NULL)`은 `0xffff80460000`(4GB 위), **저주소 힌트와 `MAP_FIXED_NOREPLACE`는 동작**,
  `personality(ADDR_LIMIT_32BIT)`는 **받아들여지지만 효과 없음**.
  → 프로세스 전역 제한은 불가능하고 **런타임이 자기 예약을 배치하는 것만 유효**하다(= A1a/A1b).
- 그래서 `mmap`을 가로채 `[0x10000000, 4GB)`로 몰아주는 `LD_PRELOAD` shim
  (`tests/lowvashim`)을 만들었다 — **디바이스판 `qemu -R`**. 이걸로 압축 빌드가 실기기에서
  641개를 전부 통과했다.
- **음성 대조가 실기기에서도 재현된다:** shim 없이 ON을 돌리면 `compressedptr.h:58` assert가
  정확히 걸린다. 그리고 **OFF+shim 대조군도 통과**하므로 shim이 무언가를 감춘 것이 아니다.
- **`Loader`/`GC` 트리 추가 실행에서 하네스 결함 하나와 shim 한계 하나가 드러났다** (둘 다 수정):
  - `CORE_ROOT`를 **export**해야 한다. out-of-process 테스트가 생성된 `.sh`에서
    `$CORE_ROOT/corerun`을 직접 호출하므로, `corerun -c <dir>`만으로는 `/corerun`으로 해석돼
    40건이 실패했다(352→390). QEMU 러너도 같은 결함이라 함께 고쳤다.
  - shim의 저주소 창은 **약 3.75GB 상한**이다. `GCRegionRange=3GB`는 들어가지 못하고, 초기
    구현은 이때 고주소로 폴백해 **압축 빌드가 포인터를 잘라 segfault**했다. 조용한 손상이므로
    기본 동작을 실패로 바꿨다(`LOWVASHIM_ALLOW_HIGH=1`로 옛 동작).

```
shim 없음:  GC heap 0xffff0c000000–0xffff4c000000   MT 0xffff5d144c70    → FAILED
shim 적용:  GC heap 0x0000000050400000–0x90000000   MT 0x0000000010054c70 → OK
참조 슬롯:  shim 없음 140,488개 전부 4GB 위 / shim 적용 전부 32비트에 맞음, 위반 0
```

> shim은 **테스트 하네스이고 제품 메커니즘이 아니다.** 제품 경로는 A1a(GC)+A1b(PAL) 구현이다.
> shim의 가치는 그 코드를 쓰기 전에 "저주소에서 런타임이 정상 동작하는가"를 분리 검증한 것.

---

## B2 착수 범위 산정 — 초기 판단과 그 정정

> ⚠️ **이 절의 원래 결론("JIT `TYP_REF` 크기가 하나뿐이라 착수 불가")은 실제 착수 결과로 정정되었다.**
> 진짜 걸림돌은 타입 크기 표가 아니라 `EA_GCREF`가 특수값이라는 emitter 내부 사실이었고, 그것은
> 우회 가능했다. 위 "B2 1차 착수" 절이 최신 상태다. 아래는 착수 전 산정 자료로 남긴다.

### (초기 판단) JIT에는 `TYP_REF`의 크기가 **하나뿐**이다

```
src/coreclr/jit/typelist.h:57
DEF_TP(REF, "ref", TYP_REF, PS, GCS, GCS, PST, PS, VTR_INT, ...)
                            ^^ pointer size
```

이 하나의 값이 **지역변수·레지스터·GC info·힙 슬롯 전부**에 쓰인다. 본 문서의 B2 원칙("힙 저장만
압축, 실행 중 값은 8바이트")을 구현하려면 JIT 타입 모델에 **"좁혀진 힙 슬롯" 개념을 새로 도입**해야
한다 — 호출부 N곳을 고치는 일이 아니라 타입 시스템에 축을 하나 추가하는 일이다.

이것이 B1/B3와 B2가 **질적으로 다른** 이유다. B1/B3는 저장 위치의 폭만 바뀌고 접근자 시그니처를
유지할 수 있어서 호출부 1133곳이 무영향이었다(에러 86→0). B2에는 그런 은닉 지점이 없다.

### 변경 표면 계량 (실측 grep)

| 영역 | 수 | 성격 |
|---|---|---|
| JIT 중 `TYP_REF` 언급 파일 | 63 | 타입 모델 축 추가의 파급 |
| JIT 배열 요소 크기(`elemSize`/`GetElemSize`) | 123 | 인덱싱 스케일 8→4 |
| GC 참조 슬롯 순회(`go_through_object` 계열) | 34 | 마킹·relocation |
| GC 카드 마킹(`card_of`/`set_card`) | 13 | write barrier 대상 폭 |
| CoreLib 중 `Unsafe.Add`/`Unsafe.As<>` 사용 파일 | 147 | `Span<T>`/byref 산술이 `sizeof(T)=8` 전제 |
| CoreLib `RawArrayData`/`ArrayBase` 참조 | 18 | 배열 데이터 오프셋 |

### 가장 위험한 지점: `Span<T>` / byref 산술

`Span<object>`는 배열 요소에 대한 byref를 만들고 `Unsafe.Add(ref, i)`로 8바이트 스케일 이동한다.
요소가 4바이트가 되면 **제네릭 코드에서 `sizeof(T)`가 "참조를 로컬로 담을 때 8, 배열 요소로 담을 때
4"로 갈라진다.** 이는 JIT 내부만의 문제가 아니라 **관리 코드의 의미까지 바꾼다**(`MemoryMarshal`,
`fixed`, `Array.Copy`, `TypedReference`).

### 권장 진행 방식

1. **먼저 실제 Tizen 앱에 `DOTNET_CompressedPtrHeapCensus=1`을 걸어 수치를 얻는다.** 3%면 B2를
   접고, 20%면 아래로 진행한다. 이 한 단계가 가장 값싸고 결정적이다.
2. 진행 결정 시, JIT 타입 모델의 "좁혀진 힙 슬롯" 설계를 **먼저 문서로** 확정한다. 코드부터
   시작하면 `Span<T>` 지점에서 막힌다.
3. 검증 경로는 인터프리터(`FEATURE_INTERPRETER`, 이 빌드에 이미 켜져 있음) 우선 — JIT 코드젠 전에
   VM·GC·CoreLib 합의만 단독 검증할 수 있다. B1a에서 "JIT 없이 먼저 부팅"이 통했던 것과 같은 전략.
4. R2R은 끄고 시작한다(런타임과 AOT 이미지가 같은 레이아웃에 합의해야 하므로 변수를 하나 줄인다).

## 📊 절감량 실측 (2026-07-29) — B1 단독은 **0%**, 전부 B2에서 나온다

`GC.GetAllocatedBytesForCurrentThread()` 전후 차이로 인스턴스당 실제 바이트를 측정
(배치 20000개, arm64/QEMU). 투영값은 8바이트 정렬 반올림을 반영.

| shape | 현재 | B1 (MT 4B) | B1+B2 (refs 4B) |
|---|---|---|---|
| `object` | 24 | 24 (0%) | 24 (0%) |
| `class{1 ref}` | 24 | 24 (0%) | **16 (33%)** |
| `class{4 refs}` | 48 | 48 (0%) | **32 (33%)** |
| `class{2ref+int+long}` | 48 | 48 (0%) | **40 (17%)** |
| `class{4 ints}` | 32 | 32 (0%) | 32 (0%) |
| `object[16]` | 152 | 152 (0%) | **88 (42%)** |
| `int[16]` | 88 | 88 (0%) | 88 (0%) |
| `object[256]` | 2072 | 2072 (0%) | **1048 (49%)** |
| `string(16)` | 56 | 56 (0%) | 56 (0%) |

### 결론 (프로젝트 계획을 바꾸는 수준)

1. **B1 단독의 절감은 모든 shape에서 0%다.** MT에서 4바이트를 아껴도 객체가 8바이트 정렬로
   반올림되면서 그대로 되돌아간다(예: 24 → 20 → 24). B1a에서 넣은 패딩을 제거해도 마찬가지다.
   → **B1은 그 자체로 가치가 없고, B2의 동반 변경으로서만 의미가 있다.** 둘은 함께 가야 한다.
2. **절감은 전적으로 B2(참조 4바이트화)에서 발생하며 33~49%로 크다.** 참조가 많을수록 커진다.
3. **값 타입 위주 데이터(`int[]`, `string`, 값 필드 클래스)는 0%다.** 대상 워크로드가 문자열/
   바이트 버퍼 위주라면 전체 이득이 작을 수 있으므로, 실기기 워크로드의 객체 구성비를 확인해야 한다.

측정 코드: `scratchpad/sizemeasure/`. 재현은 저VA 환경 표준 명령으로.

## B2 — 객체 참조(필드·배열 요소) 4바이트화 (다음 작업, 실제 절감의 전부)

### 원칙: 힙 저장만 압축, 실행 중 값은 8바이트

스택/레지스터의 참조는 GC info·예외 처리·스택워킹과 얽혀 있으므로 **8바이트를 유지**한다.
압축은 **힙에 저장되는 순간에만** 적용한다(store 시 narrow, load 시 zero-extend).
이렇게 하면 JIT 변경이 load/store 경계로 국한되고 GC info 포맷을 건드리지 않아도 된다.

### 권장 단계 (작은 것부터, 각 단계마다 저VA 환경에서 검증)

- **B2a — 참조 배열(`object[]`)만.** 절감이 가장 크고(42~49%) 접근 경로가 `GT_INDEX` 하나로
  좁다. 걸리는 것: JIT 배열 인덱싱 스케일 8→4, 배열 요소 write barrier, GC의 배열 스캔,
  `ARRAYBASE_SIZE`/`GetDataPtrOffset`, CoreLib `Array`/`Span` 경로.
- **B2b — 인스턴스 필드.** `FieldDesc` 오프셋 계산과 `MethodTableBuilder::PlaceInstanceFields`의
  정렬 로직이 4바이트 참조를 인지해야 한다. **여기서 B1a의 패딩을 제거**해 MT 절감도 함께 실현된다.
- **B2c — static 필드 / 핸들 테이블.**

### 각 단계 공통 체크리스트

1. GC: 마킹·relocation·**write barrier**(카드 마킹 대상 폭), `promote_object` 계열
2. JIT: 참조 load/store zero-extend, 배열 인덱싱 스케일, write barrier 헬퍼 시그니처
3. arm64 어셈블리 12파일 + `asmconstants.h`(`OFFSETOF__Array__m_Length` 0x8 → 0x4)
4. CoreLib: `Unsafe`, `Array`, `Span`, `fixed`
5. crossgen2/R2R: 런타임과 동일 레이아웃 합의 (**R2R 끄고 JIT 전용으로 먼저 통과** 권장)
6. interop 경계: 네이티브로 넘어가는 참조는 8바이트로 확장

### 선행 확인 사항

실측상 값 타입 데이터는 절감이 0이므로, **대상 Tizen 워크로드의 객체 구성비**(참조 리치 vs
문자열·버퍼 위주)를 먼저 확인해야 B2의 큰 비용이 정당화된다.

## B3 — VM 내부 포인터 확대 적용 (1차 완료 ✅)

`FEATURE_COMPRESSED_MT_FIELDS`로 `m_pParentMethodTable` + `m_pModule` + `m_pAuxiliaryData` 3개를
4B로 압축했다. **동작 검증 완료, MethodTable당 8B 절감(실측).** 상세는 위 "🔖 현재 진행 상태" 절.

예상대로 JIT 가시성이 낮아 B1/B2보다 위험이 낮았다 — JIT 무수정, arm64 어셈블리는 상수 1개.
가장 비쌌던 것은 예상 밖의 **CoreLib 관리 미러**였다(네이티브 레이아웃을 관리 코드가 복제).

남은 B3 후보: vtable 슬롯, `MethodDescChunk`/`EEClass`의 포인터 필드, 인터페이스 맵 엔트리.
단 절감이 타입 수에 비례할 뿐 객체 수에 비례하지 않으므로 **물리 효과는 B2보다 한참 작다.**

> 참고: `-R` 환경에서는 glibc malloc 메모리도 4GB 아래이므로, 앞서 우려했던 "malloc 포인터는
> 압축 불가" 제약이 **QEMU 테스트 환경에서는 없다**. 단 실기기에서는 다시 성립하므로, B3 대상은
> 런타임이 배치를 제어하는 영역으로 한정하는 것이 안전하다.

---

## 현재 트리 상태

- LOWVA 할당 코드: **전부 revert 완료** (`git status`에 흔적 없음)
- Stage B0: 신규 2파일 + 기존 3파일 소폭 수정. arm64 빌드 성공, 기본 동작 무영향
- **B1a: 완료** — 4B MT 슬롯 + 패딩. 절감 0(의도됨), 레이아웃 불변
- **B3(MethodTable 필드 3개): 완료** — 4개 계약 층 정렬, 타입당 8B 절감
- **스위치 동기화: 완료** — 단일 MSBuild 속성이 네이티브·CoreLib 양쪽 구동, 캐시 stale 방지
- **힙 census 진단: 완료** — `DOTNET_CompressedPtrHeapCensus=1`. B2 절감을 임의 앱에서 실측
- **CoreCLR 테스트 스위트: 경로 확보** — `JIT/CodeGenBringUpTests` 641개 통과(qemu 저VA, 압축 ON)
- **B2 1·2단계: 완료** — 참조 슬롯 전제 실증 + JIT 접근 폭 축소(47/53 좁혀짐). 3·3b·4단계 남음
- **실기기 검증: 완료 (2026-08-03)** — rpi4/Tizen 11.0 aarch64에서 압축 빌드가 641개 통과.
  `tests/vaprobe`로 커널 배치 측정, `tests/lowvashim`으로 저VA 강제(위 절)
- 두 플래그 모두 기본 OFF이며 **OFF 구성 회귀 테스트 통과 확인**(mtstress/mirrorstress/lowvastress
  + census 수치가 ON과 동일함을 교차 확인, 실기기 OFF 기준선 641개 통과)
- 커밋하지 않음 (main 브랜치)

### 다음 세션 권장 순서

1. **실제 Tizen 앱에 `DOTNET_CompressedPtrHeapCensus=1`**. B2 전체의 Go/No-Go가 여기서 갈린다
   (2.8% vs 22%). 가장 값싸고 결정적. **이제 실기기에서 바로 붙일 수 있다.**
2. **B2 3단계(write barrier) → 3b(배열 스케일) → 4단계(저장 폭).** 3단계까지는 동작 보존이라
   QEMU·실기기 양쪽에서 독립 검증 가능하다.
3. **A1a/A1b 구현** — `lowvashim`을 제품 경로로 대체. 메커니즘(저주소 힌트,
   `MAP_FIXED_NOREPLACE`)은 실기기에서 확인됐으므로 남은 것은 "런타임 어디에서 예약하는가"뿐이다.
4. **`Loader` + `GC` 테스트 트리 실행.** 검증 폭을 넓히는 가장 값싼 수단. 실기기 media
   파티션(55 GB)에 여유가 충분하다.
5. **I3 증명** — validator를 코드 주소까지 확장. vtable 압축의 선행 조건.
