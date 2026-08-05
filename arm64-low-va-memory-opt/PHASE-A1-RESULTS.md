# Phase A1a 구현 및 측정 결과 (linux-x64, 2026-07-28)

명세서 `arm64-low-va-spec.md` / 계획서 `arm64-low-va-plan.md`의 Phase A1a(GC 저주소 예약)를
구현하고 개발 머신(linux-x64)에서 검증·측정한 결과. **디바이스 없이 얻은 수치**이며, RSS 최종
산정은 계획대로 rpi4 arm64 실기기에서 다시 받아야 한다.

## 정정 (중요): QEMU에는 VA를 제한하는 기능이 있다 — `qemu-user -R`

처음에 "OS/에뮬레이터 레벨로 가상 주소를 32bit로 제한하는 방법은 없다"고 판단했으나 **틀렸다.**
`personality(ADDR_LIMIT_32BIT)`·`setarch --32bit`·Docker cgroup·`ulimit -v`만 시험하고
**qemu-user 자체의 `-R`(`QEMU_RESERVED_VA`) 옵션을 확인하지 않았다.**

```bash
# 게스트 가상 주소 공간을 4GB로 제한. 런타임 코드 변경 불필요.
env DOTNET_GCRegionRange=40000000 \
  qemu-aarch64 -R 0x100000000 -L $ROOTFS $ROOTFS/lib64/ld-linux-aarch64.so.1 \
    --library-path $ROOTFS/lib64:$ROOTFS/usr/lib64:$CORE_ROOT \
    $CORE_ROOT/corerun -c $CORE_ROOT app.dll
```

측정 결과 (`DOTNET_LowVirtualAddress` **미설정**, 즉 저주소 코드 OFF):
- GC 힙 probe 20/20 **4GB 아래** (`0xb6803b60` … `0xc5000058`)
- 큰 익명 매핑 **전부** 4GB 아래. **glibc malloc 아레나(63MB 단위)까지 포함**되며 최상단이
  정확히 `0x100000000`에서 끝난다.
- `-R` 없이 `GCRegionRange`만 제한하면 고주소, `-R`만 주고 GC 범위를 제한하지 않으면 기본
  61GiB 예약이 4GB에 못 들어가 `0x8007000E`(OOM)로 기동 실패 → **둘을 함께** 써야 한다.

### 이 발견이 바꾸는 것

| | QEMU 테스트 | 실기기(rpi4 arm64) |
|---|---|---|
| `qemu -R` | ✅ 완전한 32bit VA (malloc 포함) | ❌ 해당 없음 |
| 런타임 LOWVA 코드 | 불필요 | 필요(또는 커널 레벨 대응) |

- **QEMU 기반 개발·테스트에는 LOWVA 코드가 필요 없다.** `-R`이 더 완전하다(런타임이 제어할 수
  없는 malloc·스레드 스택까지 커버).
- **Stage B(포인터 4바이트화)에 유리하다.** `-R` 환경에서는 malloc 메모리까지 32bit에 들어가므로
  "압축 대상을 GC 힙·loader heap으로 한정해야 한다"는 제약이 QEMU 테스트에서는 사라진다.
- LOWVA 코드는 **실기기 경로용으로 유지**하되 기본 비활성으로 둔다. 되돌리기는
  `grep -rn "LOWVA:" src/` 지점 제거 + `minipal/lowvamem.*` 삭제 + CMakeLists 1줄.
- 참고: Docker·`personality`·`ulimit -v`는 여전히 배치를 바꾸지 못한다(측정 환경으로만 유용).

## 구현

- 파일: `src/coreclr/gc/unix/gcenv.unix.cpp`
- config: `DOTNET_GCLowVirtualAddress` = `0`(기본/비활성) / `1`(preferred, 폴백 허용) /
  `2`(strict, 폴백 없음 — 불변식 검증용)
- 하위 4GB 내 bump-hint 2-pass 탐색. `MAP_FIXED_NOREPLACE`(Linux 4.17+) 사용, 구버전 커널의
  advisory 힌트 동작은 결과 range check로 처리. 첫 폴백 시 stderr 1회 경고.
- `HOST_64BIT`로만 감쌈(아키텍처 중립) → linux-x64에서 그대로 검증 가능.

빌드: `./build.sh clr.runtime -rc checked` (NativeAOT 불필요, 제외)

## 필수 동반 설정 (중요 발견)

현대 CoreCLR GC는 **regions** 방식으로 **단일 연속 범위**를 예약하며, 기본값은 이 머신에서
**약 61 GiB**였다. 4GB에 들어갈 수 없으므로 저주소 모드는 반드시 GC 범위 제한과 함께 써야 한다:

```
DOTNET_GCRegionRange=C0000000   # 3GB. 4GB 천장 아래에 들어가는 값
DOTNET_GCLowVirtualAddress=2
```

`GCRegionRange`를 제한하지 않고 strict 모드를 켜면 61 GiB 예약이 실패하고
`GC heap initialization failed with error 0x8007000E`로 런타임이 시작하지 못한다
(설계상 strict는 진단 모드이므로 의도된 동작이며, 이 실험이 병목을 드러냈다).

## 검증 결과 — 불변식 성립

`GCHandle.AddrOfPinnedObject()`로 실제 GC 힙 주소를 20회 probe (gen0/gen2/LOH 크기 혼합):

| 설정 | 결과 |
|---|---|
| 기본(비활성) | 20/20 **고주소** (`0x7d9f…`) |
| `=1` + 3GB range | **20/20 4GB 아래** (`0x22c04330`…`0x31400058`) |
| `=2` strict + 3GB range | **20/20 4GB 아래**, 폴백 0 |

## 기능 검증 — 통과

- **GC 스트레스**: 8스레드, 총 2058MB 할당, gen0 39–44 / gen1 39–43 / gen2 22–27회,
  전 크기 클래스(16B~3MB) 혼합, 무결성 검사 **오류 0** — 3개 모드 전부 PASS.
- **R2R**: crossgen2로 R2R 이미지 생성 후 `DOTNET_ReadyToRun=1`/`0` 양쪽 실행 — 둘 다 PASS,
  주소 전부 4GB 아래. **JIT·R2R 경로 모두 정상.**
- 성능: 스트레스 소요 168–189ms로 모드 간 유의미한 차이 없음.

## 메모리 측정 — 핵심 결과 (기대와 다름)

동일 워크로드(`lowvastress`), `/proc/self/status`:

| 설정 | VmPeak (가상) | VmHWM (물리 피크) |
|---|---|---|
| 기본 (61GiB range, 고주소) | **65.0 GB** | 401 MB |
| `GCRegionRange=3GB` 만 | **6.84 GB** | 357 MB |
| `GCRegionRange=3GB` + 저주소 strict | **6.56 GB** | 392 MB |

**결론 (정직한 평가):**

1. **가상 주소 사용량은 65 GB → 6.6 GB로 약 90% 감소.** 단, 이 절감의 **대부분은
   `GCRegionRange` 제한에서 오고**, 저주소 배치 자체의 추가 기여는 작다(6.84→6.56 GB).
2. **물리 메모리(RSS/HWM)는 개선되지 않았다** (357–401 MB, 실행 간 노이즈 범위 내).
   GC 부기 테이블(card/brick/mark)은 예약 범위가 아니라 **실제 사용량에 비례해 lazy commit**
   되므로, 예약을 줄여도 RSS가 줄지 않는다.
3. → **명세서 §8의 "Phase A 효과: GC 부기 예약 감소"는 물리 메모리 절감이 아니라
   가상 주소 절감으로 수정되어야 한다.**

## 이 결과가 로드맵에 주는 의미

- Phase A의 가치는 재정의된다: **(a) Phase B(compressed references)의 전제 불변식 확립**,
  **(b) 거대 VA 예약 회피**(엄격한 overcommit/`RLIMIT_AS` 환경에서 기동 실패를 막음).
  **직접적인 RSS 절감 수단은 아니다.**
- 물리 메모리 절감은 결국 **Phase B(객체 참조 4바이트 저장)** 와 **Phase A2(JIT 코드 크기
  단축 → 코드 커밋 감소)** 에서 와야 한다. A2는 커밋되는 코드가 실제로 줄어들므로 물리 절감
  효과가 유효하다.
- Phase B Go/No-Go 판단 시, "A에서 이미 메모리가 줄었다"는 가정을 쓰면 안 된다.

## arm64 컴파일 검증 — 통과 (단, 전체 빌드는 rootfs가 막고 있음)

- 변경 파일은 arm64로 **정상 컴파일**됨: `ninja gc/unix/CMakeFiles/gc_pal.dir/gcenv.unix.cpp.o`
  → ELF 64-bit aarch64 오브젝트에 `DOTNET_GCLowVirtualAddress` 문자열 포함 확인.
  (코드가 `mmap`/`uintptr_t`만 쓰므로 아키텍처 의존성 없음)

### arm64 전체 크로스 빌드 — Tizen rootfs로 해결 (성공)

```bash
sudo ROOTFS_DIR=/home/clamp/Work/dotnet/rootfs/arm64.tizen \
     ./eng/common/cross/build-rootfs.sh arm64 tizen
ROOTFS_DIR=/home/clamp/Work/dotnet/rootfs/arm64.tizen \
     ./build.sh clr.runtime -arch arm64 -rc checked --cross
```

→ **Build succeeded (0 error / 0 warning).** 산출물 확인:
`libcoreclr.so`(8.8MB), `libclrgc.so`, `corerun` — 모두 `ELF 64-bit LSB shared object, ARM aarch64`,
`DOTNET_GCLowVirtualAddress` 문자열 포함(변경 반영 확인).

rootfs: **Tizen 11.0 / glibc 2.40 / 커널 헤더 6.6** — 이전 블로커였던
`MEMBARRIER_CMD_PRIVATE_EXPEDITED` 존재.

### Ubuntu rootfs는 사내 프록시 TLS 때문에 만들어지지 않는다 (원인 규명)

`build-rootfs.sh arm64 [codename]`으로 만든 Ubuntu rootfs는 **debootstrap은 성공하지만
dev 패키지가 설치되지 않아** `/usr/include`가 빈 상태로 끝난다(≈136–165MB). 실패 원인:

```
Err:1 https://ports.ubuntu.com jammy InRelease
  Certificate verification failed: The certificate is NOT trusted. [IP: 10.112.1.184 8080]
E: Unable to locate package symlinks / libomp5 / ...
```

사내 프록시(`10.112.1.184:8080`)가 TLS를 인터셉트하는데 **chroot 안에는 사내 CA
(`/usr/local/share/ca-certificates/SRnD_Web_Proxy_*.crt`)가 없어** HTTPS 검증이 실패한다.
따라서 패키지 인덱스를 못 받고 dev 패키지 설치가 전부 실패한다.

- **`build-rootfs.sh`는 이 실패를 무시한다** — 909–912행의 `chroot ... apt-get` 호출들이
  exit code를 검사하지 않아, 실패해도 스크립트는 정상 종료한다. 그래서 rootfs가
  잘 만들어진 것처럼 보인다. (xenial/jammy 모두 동일하게 실패 — 배포판 EOL 문제가 아니다.)
- **Tizen rootfs가 성공한 이유:** `tizen-fetch.sh`는 `http://download.tizen.org`(평문 HTTP)를
  쓰므로 TLS 인터셉션에 걸리지 않는다.
- 굳이 Ubuntu rootfs가 필요하면 apt 실행 전에 사내 CA를 rootfs에 복사하고
  `update-ca-certificates`를 돌려야 한다.

### ⚠️ 남은 확인 사항: Tizen 버전 정합성

새 arm64 rootfs는 **Tizen 11.0 (glibc 2.40)** 인데, 기존 armel rootfs는
**Tizen 9.0 (glibc 2.30)** 이다. 바이너리는 빌드 시점보다 낮은 glibc에서 실행되지 않으므로,
rpi4에 올릴 arm64 이미지가 Tizen 9라면 2.40으로 빌드한 바이너리는 **로드되지 않는다.**
bring-up 시 디바이스 이미지 버전에 맞춰 rootfs를 다시 받아야 한다(`tizen-fetch.sh`의 대상 스냅샷 조정).

## 남은 작업

- **arm64 rootfs 재생성** (위 ⚠️ 항목, 실기기 측정의 선행 조건)
- **A1b**: loader/코드 힙(PAL 경로) 저주소화 — `MethodTable` 포인터까지 불변식 확장(Phase B 전제).
  현재 PAL은 코드용 1GB 슬랩을 별도 관리하며 `virtual.cpp:1670-1674`에 "4GB 아래를 피하라"
  휴리스틱이 있어 이를 저VA 모드에서 반전해야 한다.
- **A2**: ARM64 `FitsInAddrBase`/짧은 주소 인코딩 신설 → `movz/movk` 4→2 명령.
- arm64 크로스 빌드 컴파일 확인 (rootfs는 `/work/dotnet/rootfs/arm64`에 존재).
- 실기기 RSS·부기 산정(rpi4 arm64 bring-up 이후).
- 기본 경로 무영향 회귀 확인을 위한 정식 테스트 스위트 실행(아직 미실시).
