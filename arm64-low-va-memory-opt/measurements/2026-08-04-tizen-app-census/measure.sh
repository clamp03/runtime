#!/bin/bash
# FINAL measurement: forced blocking gen2 GC every 10s (config value is HEX: 2710),
# census level 1 (true live set). Take the LAST sample as the plateau and the
# second-to-last to prove it settled.
OUT=/tmp/census_final; mkdir -p "$OUT"
OBSERVE=110
APPS="VisualSample ChannelList System_info.Tizen.Mobile Settings.Tizen.Mobile AppCommon.Tizen.Mobile ApplicationControl.Tizen.Mobile Puzzle.Tizen.Mobile Xamarin.Hello.F_HUB.Tizen MovieLibrary MediaHubSample FirstScreen"

for A in $APPS; do
  APP="org.tizen.example.$A"
  SHORT=$(echo "$A" | sed 's/\.Tizen.*$//')
  echo "=================== $SHORT ==================="
  for k in $APPS; do sdb shell "app_launcher -k org.tizen.example.$k" >/dev/null 2>&1; done
  sdb shell "pkill -9 -f dotnet-hydra-loader; pkill -9 -f process-pool" >/dev/null 2>&1
  sleep 3
  ok=0; for i in $(seq 1 45); do sdb shell "ps -ef" 2>/dev/null | grep -q "process-pool" && { ok=1; break; }; sleep 1; done
  [ $ok -eq 0 ] && { echo "  pool never ready, skip"; echo; continue; }
  sdb shell "rm -f /tmp/census.*.txt /tmp/censusdbg.*.txt" >/dev/null 2>&1

  L=$(sdb shell "app_launcher -s $APP" 2>&1 | tr -d '\r' | tail -1)
  PID=$(echo "$L" | grep -oE 'pid = [0-9]+' | grep -oE '[0-9]+')
  echo "  pid=$PID"
  [ -z "$PID" ] && { echo "  launch failed: $L"; echo; continue; }
  sleep "$OBSERVE"

  echo "  memps: $(sdb shell "memps -v" 2>/dev/null | tr -d '\r' | grep "/${SHORT}" | head -1)"
  sdb pull "/tmp/census.${PID}.txt" "$OUT/${SHORT}.txt" >/dev/null 2>&1
  if [ -f "$OUT/${SHORT}.txt" ]; then
    echo "  samples: $(grep -c CompressedPtrHeapCensus "$OUT/${SHORT}.txt")"
    echo "  --- last 3 live-set samples ---"
    grep "live objects" "$OUT/${SHORT}.txt" | tail -3 | sed 's/^/    /'
    echo "  --- plateau TOTAL row ---"
    grep "^  TOTAL" "$OUT/${SHORT}.txt" | tail -1 | sed 's/^/  /'
  else
    echo "  no census file"
  fi
  echo
done
