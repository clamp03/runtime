# 실제 Tizen 앱 11개 힙 census 원본 데이터 (2026-08-04, rpi4 aarch64)

B2(포인터 압축) Go/No-Go 판정의 근거 데이터. 분석과 결론은 **`../../HANDOVER.md` §4-3b**.

## 파일

| 파일 | 내용 |
|---|---|
| `<App>.txt` | 앱별 census 시계열 원본. 강제 blocking gen2 GC 10초 간격 10~11회 |
| `plateau-verification-FirstScreen.txt` | plateau 검증용 장시간(123초) 12샘플 |
| `measurement-run.log` | 측정 실행 로그 (memps PSS 포함) |
| `measure.sh` | 측정 스크립트 (재사용 가능) |

## 읽는 방법

각 블록 헤더: `pid=<pid> seq=<n> t=+<ms>`.
`TOTAL` 행의 마지막 두 열이 `B1+B2` 바이트와 절감률이다.

**plateau 판정: 마지막 3개 샘플의 live bytes가 동일한지 확인한다** (11개 앱 전부 충족).
앞쪽 샘플에는 fork 직후의 프레임워크 기저값(2,672 objects / 320,728 bytes)이 섞여 있을 수 있다 —
Tizen hydra pool worker에서 COW로 물려받은 것이므로 앱의 값이 아니다.

## 재측정 방법

`../../HANDOVER.md` §실행 C(경로 A: `libcoreclr.so` 단독 교체)로 census 빌드를 올린 뒤:

```
DOTNET_CompressedPtrHeapCensus=1
DOTNET_ReadyToRun=0
DOTNET_CompressedPtrHeapCensusForceAfterMs=2710   # ⚠️ HEX! = 10000ms
```
을 `/usr/share/dotnet.tizen/lib/coreclr_env.list`에 넣고 `measure.sh` 실행.

⚠️ **`DOTNET_*` 숫자 config는 16진수로 파싱된다** — `15000`은 0x15000 = 86016ms가 된다.
그 외 계측 함정 5개는 HANDOVER §4-3b의 "계측 결함 5개" 절에 전부 기록돼 있다.
