#!/usr/bin/env bash
# The web API (docs/API.md) of the PACKAGED desktop app, with its demo robot: the packaged app
# has its own trimmed Java, so this also catches a module it lacks (jdk.httpserver).
#   web-api.sh "<path to the packaged app's binary>"
set -u
app="$1"
log="$(mktemp)"
SCRAPER_DEMO="setup=done api=on" "$app" > "$log" 2>&1 &
pid=$!
trap 'kill $pid 2>/dev/null; taskkill //IM "Scraper Control.exe" //F >/dev/null 2>&1' EXIT

api() {   # api METHOD PATH [PIN] -> "<http code> <body>"
  curl -s -m 60 -o /tmp/web-api-body -w "%{http_code}" -X "$1" -H "Authorization: Bearer ${3-4321}" "http://127.0.0.1:8080$2"
  echo " $(cat /tmp/web-api-body)"
}
fail=0
check() {   # check NAME "<answer>" PATTERN
  if [[ "$2" == $3 ]]; then echo "ok   $1"; else echo "FAIL $1: $2"; fail=1; fi
}

for _ in $(seq 60); do curl -s -o /dev/null "http://127.0.0.1:8080/" && break; sleep 2; done
check "serves"            "$(api GET /api/status)"            '200 {"ok":true,*'
check "no PIN"            "$(api GET /api/status '')"         '401 *'
check "status read now"   "$(api GET '/api/status?fresh')"    '200 {"ok":true,*"planning":*'
check "nothing to stop"   "$(api POST /api/stop)"             '502 {"ok":false,"fault":"Busy",*'
check "route A starts"    "$(api POST /api/route/A)"          '200 {"ok":true,*'
check "unknown call"      "$(api GET /api/route/A)"           '404 *'
if [ $fail -ne 0 ]; then echo "--- app output"; tail -40 "$log"; fi
exit $fail
