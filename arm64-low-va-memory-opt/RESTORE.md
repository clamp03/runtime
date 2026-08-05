# 복원 안내 — 이 작업 상태를 다시 만드는 방법

> `/work/dotnet/new` 폴더는 삭제될 예정이므로, 이 문서가 **작업 상태를 되살리는 유일한 진입점**이다.
> 기술적 내용·수치·판단은 `HANDOVER.md`에 있다. 이 문서는 "무엇이 어디에 있고 어떻게 되돌리는가"만 다룬다.

작성: 2026-08-04. 대상: ARM64 메모리 최적화(포인터 압축) 작업, Stage B + 실제 Tizen 앱 census 측정.

---

## 1. 저장소 두 개 — 브랜치와 기준 커밋

| 역할 | 원격 | 브랜치 | 기준(base) 커밋 |
|---|---|---|---|
| **upstream dotnet/runtime 작업 트리** | `git@github.com:clamp03/runtime.git` | `arm64-lowva-stage-b-20260804` | `e9d093ab7068dcd456f5a07b9edf8810ae6940ff` (main, 2026-07-22) |
| **Samsung 플랫폼 포크 (net8.0)** | `git@github.sec.samsung.net:dheon-jung/runtime.git` | `arm64-lowva-tizen-census-20260804` | `45f8714b5ee894aee88e0646b2a04f7fdf9bef53` (release/8.0-tizen, 2026-08-04) |

```bash
# upstream 작업 트리 복원
git clone git@github.com:clamp03/runtime.git runtime
cd runtime && git checkout arm64-lowva-stage-b-20260804

# 플랫폼 포크 복원
git clone git@github.sec.samsung.net:dheon-jung/runtime.git runtime.sec
cd runtime.sec && git checkout arm64-lowva-tizen-census-20260804
```

**주의:** 원래 두 트리는 **커밋되지 않은 작업 상태**였다(upstream 25 수정 + 2 신규, 포크 7 수정 + 2 신규).
폴더 삭제에 대비해 그것을 그대로 커밋한 것이므로, 이 브랜치의 diff = 당시 working tree 그대로다.

### 함께 필요한 저장소 (수정하지 않았음 — 그냥 다시 clone하면 된다)

| 용도 | 원격 | 브랜치 |
|---|---|---|
| Tizen dotnet-launcher 소스 (hydra/prefork 구조 파악에 필수) | `git@github.sec.samsung.net:dotnet/launcher.git` | `tizen` |

---

## 2. 두 트리에 무엇이 들어 있나

### upstream (`clamp03/runtime`, 브랜치 `arm64-lowva-stage-b-20260804`)

Stage B 포인터 압축 구현 + 문서 + 테스트 자산. 상세 파일 목록은 `HANDOVER.md` §8.

- **신규**: `src/coreclr/inc/compressedptr.h`, `src/coreclr/vm/compressedptrvalidate.cpp`
- **JIT**: `instr.h`, `codegen.h`, `codegenarmarch.cpp`, `codegenarm64.cpp`, `jitconfigvalues.h`
- **VM**: `methodtable.h/.cpp`, `object.h/.inl`, `arm64/asmconstants.h`, `arm64/patchedcode.S/.asm`(write barrier 4B) 외
- **관리**: `RuntimeHelpers.CoreCLR.cs` (MethodTable 미러)
- **빌드 스위치**: `clrdefinitions.cmake`, `runtime.proj`, `clr.featuredefines.props`
- **`arm64-low-va-memory-opt/`** ← 문서·테스트·측정 데이터 전부. 원래 git 추적 대상이 아니었으나 이번에 포함시켰다.
  - `HANDOVER.md` — **가장 먼저 읽을 문서**
  - `STAGE-B-POINTER-COMPRESSION.md` — 시간순 상세 기록
  - `measurements/2026-08-04-tizen-app-census/` — 실제 앱 census 원본 11개 + plateau 검증 + 재사용 스크립트
  - `tests/` — 헬퍼 앱 소스(`Program.cs`/`.csproj`), `lowvashim`(LD_PRELOAD 저VA shim), `vaprobe`, 실행 스크립트
  - `.gitignore` — `bin/`·`obj/`·컴파일된 디바이스 바이너리 제외(재생성 가능)

### 포크 (`dheon-jung/runtime`, 브랜치 `arm64-lowva-tizen-census-20260804`)

실제 Tizen 앱에서 힙 census를 재기 위한 **최소 이식**. 레이아웃을 바꾸지 않으므로 압축 기능(B1/B2)은 들어 있지 **않다**.

| 파일 | 내용 |
|---|---|
| `src/coreclr/vm/compressedptrvalidate.cpp` (신규) | census 본체. shape별 집계 + B1/B1+B2 투영. 출력은 `/tmp/census.<pid>.txt` |
| `src/coreclr/vm/gcenv.ee.cpp` | `DiagGCEnd`에서 census 호출 (`extern "C"` 링키지 필수 — 이유는 파일 주석) |
| `src/coreclr/vm/finalizerthread.cpp/.h` | N초 주기 강제 blocking gen2 GC + `HasStopRequestedForFork()` 접근자 |
| `src/coreclr/vm/corhost.cpp` | `InitializeAfterFork`에서 census 타이머 리셋 (hydra fork 기준점) |
| `src/coreclr/inc/clrconfigvalues.h` | `CompressedPtrHeapCensus`, `CompressedPtrHeapCensusForceAfterMs` |
| `src/coreclr/vm/CMakeLists.txt` | 신규 파일 등록 |
| `packaging/coreclr.spec` | `BuildRequires: tar`, `xz` 추가 (aarch64 GBS 빌드용) |
| `.github/gbs.conf` | GBS 프로필. `buildroot`가 `../GBS-ROOT/runtime`을 가리킨다 |

> 진단용 잔여물: `finalizerthread.cpp`의 `MaybeForceCensusGC()`가 진입마다
> `/tmp/censusdbg.<pid>.txt`에 가드 상태를 기록한다. 계측을 신뢰하려면 남겨두는 게 좋지만,
> 제품 빌드에는 불필요하므로 필요시 지워도 된다.

---

## 3. 재현 절차 — 실제 Tizen 앱 census 측정

전체 맥락은 `HANDOVER.md` §실행 C(경로 A)와 §4-3b. 요약:

```bash
# 1) GBS 컨테이너 (호스트에서 직접 GBS 설치 불가)
tail -f /dev/null | docker start -i gbs &        # stdin을 잡아둬야 컨테이너가 안 죽는다
docker exec -u clamp gbs bash -lc 'gbs --version'

# 2) aarch64 빌드 (약 11분)
docker exec -u clamp gbs bash -lc \
  'cd <포크경로> && gbs -c .github/gbs.conf build -A aarch64 -P Tizen-Unified --include'

# 3) libcoreclr.so만 추출 → 디바이스 교체 (RPM 설치·NI 전부 불필요)
rpm2cpio <buildroot>/local/repos/Tizen_Unified/aarch64/RPMS/coreclr-8.0.11-0.aarch64.rpm | cpio -idm
sdb push .../libcoreclr.so /usr/share/dotnet.tizen/netcoreapp/libcoreclr.so
sdb shell "chsmack -a _ /usr/share/dotnet.tizen/netcoreapp/libcoreclr.so"   # 빠뜨리면 조용히 실패

# 4) census 활성화 (런처가 이 파일을 putenv한다)
sdb shell 'cat > /usr/share/dotnet.tizen/lib/coreclr_env.list << EOF
DOTNET_CompressedPtrHeapCensus=1
DOTNET_ReadyToRun=0
DOTNET_CompressedPtrHeapCensusForceAfterMs=2710
EOF'
sdb shell "chsmack -a _ /usr/share/dotnet.tizen/lib/coreclr_env.list"

# 5) 측정 (앱당 약 2분)
arm64-low-va-memory-opt/measurements/2026-08-04-tizen-app-census/measure.sh
```

### ⚠️ 반드시 알아야 할 함정 5개

`HANDOVER.md` §4-3b의 "계측 결함 5개" 절에 전문이 있다. 요약:

1. **`DOTNET_*` 숫자 config는 16진수다.** `15000` → `0x15000` = 86016 ms. 10초는 `2710`.
2. **`DiagGCEnd`에서 GC 락을 잡는 API를 부르면 자기 데드락.** `GetTotalBytesInUse()`가 그렇다
   → 런처가 `futex_wait_queue`에 걸려 모든 앱이 `launch failed`. 힙 걷기는 안전하다.
   (`GetLastGCGenerationSize()`는 락 없는 배열 읽기라 안전 — LOH/POH 크기 측정에 쓸 수 있다.)
3. **Tizen hydra 프로세스 풀 + fork COW.** 앱은 미리 fork된 워커에서 specialize되므로, 앱이
   자기 상태를 할당하기 전에 census를 찍으면 **서로 다른 앱이 바이트 단위로 동일한 값**을 낸다.
   측정 사이에 `pkill -9 -f dotnet-hydra-loader; pkill -9 -f process-pool` 후 **`process-pool`이
   다시 뜰 때까지 기다려야** 한다. 앱도 `-k`로 확실히 죽여야 옛 `.so`를 안 쓴다.
4. **census 출력에 PID를 반드시 넣어라.** static은 fork로 상속되고 여러 프로세스가 동시에 쓴다.
5. **finalizer thread는 주기 tick이 아니다.** `WaitForFinalizerEvent`의 timeout 경로와
   `FinalizerThreadWorker` 루프 몸통 **양쪽 모두**에서 호출해야 한다(각각 다른 워크로드를 놓친다).

### GBS aarch64 빌드 함정

- **빌드 루트에 `tar`가 없다.** Tizen `tar` 패키지는 `gnutar`만 설치한다. buildroot마다
  `ln -s gnutar <buildroot>/usr/bin/tar` 를 걸어야 `%prep`이 통과한다.
  (`--clean`으로 buildroot를 재생성하면 다시 걸어야 한다.)
- `xz-utils`는 없다 → 패키지명은 `xz`.
- `<cstdio>`가 없다 → `<stdio.h>`를 써라.
- 이 포크(8.0.11)의 `MethodTable` API는 `ContainsPointers()` / `ContainsPointersOrCollectible()`이다
  (upstream의 `ContainsGCPointers*`가 아니다).
- `gcenv.ee.cpp`는 `gcenv.ee.standalone.cpp`가 `namespace standalone` 안에서 한 번 더 include한다
  → 거기서 호출하는 외부 함수는 선언·정의 양쪽에 `extern "C"`가 필요하다.

---

## 4. 디바이스(rpi4) 상태 — 세션 종료 시점

작업 후 **원본으로 완전 복원**해 두었다. 다음 세션에서 그대로 시작할 수 있다.

| 항목 | 상태 |
|---|---|
| `libcoreclr.so` | 원본 복원(체크섬 일치 확인). 백업은 `libcoreclr.so.backup.1785809841`로 남겨둠 |
| `System.Private.CoreLib.dll` | NI 복원 13,592,064 B, `.Backup`(IL 4.5 MB) 재생성 |
| NI 전체 | 518/518, Missing 0, CoreLib = R2R image |
| `coreclr_env.list` | 제거 |
| 샘플 앱 11개 | 설치된 상태로 유지 (`/work/dotnet/apps`의 `.tpk`, `ins.sh`) |
| armel CI 22:30 cron | 재설치 완료 |

> 이 디바이스는 **aarch64 전용 SD카드**로 작업했다. armel CI와 카드를 공유하므로, 카드를 바꿔 끼우면
> 서로 오염되지 않는다(`HANDOVER.md` §7-12). aarch64 카드에는 `/opt/vatest`(런타임+앱+shim, 251 MB)와
> `/opt/usr/home/owner/media/coreclr`(Core_Root+테스트, 375 MB)가 그대로 남아 있다.

---

## 5. 결론과 다음 할 일

**B2(포인터 압축) 판정: No-Go** — 근거는 `HANDOVER.md` §4-3b.

실제 앱 11개 plateau 실측: B1+B2 절감률은 22.8~27.8%로 투영과 일치하지만, 관리 라이브 힙이
421 KB~1.67 MB로 **앱 PSS의 0.83~3.13%**뿐이어서 절감/PSS가 **0.20~0.87%**(중앙값 0.47%,
최대 465 KB)에 그친다. **관리 힙을 100% 없애도 상한이 3.13%다.**

**다음 최우선 작업(§9 항목 1): 무거운 프로덕션 앱으로 재측정.** 판정을 뒤집을 수 있는 유일한 조건이다.
손익분기는 라이브 힙 **3.6 MB**(절감/PSS 2%) / **9 MB**(5%)이고, 인프라는 전부 갖춰져 있어
앱만 확보하면 앱당 약 2분이다. 이것이 안 나오면 4단계(저장 폭 실축소)는 착수하지 않는 것이 맞다.

### 미측정으로 남은 것

- **committed GC heap 크기** — live set만 쟀다. `GetMemoryInfo()`의 `totalCommittedBytes`로
  얻을 수 있다(GC 밖에서 호출). 절감 추정치는 안 바뀌지만 "관리 힙이 앱의 몇 %인가" 서술이 정확해진다.
- **LOH/POH 크기** — census가 LOH를 포함해 걷지만 세대별로 분리하지 않는다.
  `GetLastGCGenerationSize(3)`=LOH, `(4)`=POH (`gc/gc.h`의 `loh_generation=3`, `poh_generation=4`).
  락을 잡지 않아 `DiagGCEnd`에서 안전하다.
  현재까지의 간접 증거로는 **LOH는 사실상 비어 있다** — `arrays of values` 버킷 총량이
  11개 앱 전부에서 LOH 임계값(85,000 B)보다 작아 산술적으로 불가능하다. Tizen 로그는 dlog(네이티브,
  커널 ring buffer 256 KiB)를 타므로 관리 힙에 안 쌓인다.
- **RSS 실측** — 현재 절감이 타입당 8 B뿐이라 노이즈 이하. 4단계 이후에 의미가 생긴다.
- `HANDOVER.md` §7의 나머지 공백(cDAC 폭 불일치, I3 코드 주소 증명, GC 간헐 실패 1건 미판정 등).
