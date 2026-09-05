#!/usr/bin/env bash
# Exercises retry/reconciliation policy without a controller or curl.
set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
# shellcheck source=scripts/shotstopper_ota.sh
source "$repo_root/scripts/shotstopper_ota.sh"

body_file="$(mktemp "${TMPDIR:-/tmp}/shotstopper-ota-cli-test.XXXXXX")"
image_file="$(mktemp "${TMPDIR:-/tmp}/shotstopper-ota-cli-image.XXXXXX")"
chunk_file="$(mktemp "${TMPDIR:-/tmp}/shotstopper-ota-cli-chunk.XXXXXX")"
session_file="$(mktemp "${TMPDIR:-/tmp}/shotstopper-ota-cli-session.XXXXXX")"
trap 'rm -f "$body_file" "$image_file" "$chunk_file" "$session_file"' EXIT
printf 'test' > "$image_file"
SS_OTA_BODY_FILE="$body_file"
SS_OTA_SESSION_BODY="$session_file"
SS_OTA_CHUNK_FILE="$chunk_file"
SS_OTA_IMAGE_ARCH=n16r8
SS_OTA_IMAGE_VERSION=1.2.3
SS_OTA_IMAGE_PACKED=16908291
SS_OTA_IMAGE="$image_file"
SS_OTA_IMAGE_SIZE=4
SS_OTA_IMAGE_SHA256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
SS_OTA_TRANSFER_ID=0123456789abcdef0123456789abcdef0123
SS_OTA_CHUNK_BYTES=2
SS_OTA_RANGE_ATTEMPTS=3
SS_OTA_COMMIT_ATTEMPTS=2

failures=0
check() {
  if ! "$@"; then
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
  fi
}

mock_mode=""
mock_patches=0
mock_offset=0
ss_ota_request() {
  local method="$1" path="$2"
  if [[ "$method" == "POST" && "$path" == "/api/v1/ota/session" ]]; then
    printf '{"state":"receiving","transferId":"%s","sha256":"%s","nextOffset":%s,"chunkBytes":2}' "$SS_OTA_TRANSFER_ID" "$SS_OTA_IMAGE_SHA256" "$mock_offset" > "$SS_OTA_BODY_FILE"
    SS_OTA_CURL_EXIT=0; SS_OTA_HTTP_STATUS=200; return 0
  fi
  if [[ "$method" == "PATCH" && "$path" == "/api/v1/ota" ]]; then
    mock_patches=$((mock_patches + 1))
    if [[ "$mock_mode" == "retry" && "$mock_patches" == "1" ]]; then
      mock_offset=2
      printf '{}' > "$SS_OTA_BODY_FILE"
      SS_OTA_CURL_EXIT=56
      SS_OTA_HTTP_STATUS=000
      return 1
    fi
    if [[ "$mock_mode" == "safety" ]]; then
      printf '{"error":"SAFETY_LOST"}' > "$SS_OTA_BODY_FILE"
      SS_OTA_CURL_EXIT=0
      SS_OTA_HTTP_STATUS=409
      return 0
    fi
    mock_offset=$((mock_offset + 2))
    if (( mock_offset >= 4 )); then
      printf '{"state":"staged","transferId":"%s","sha256":"%s","nextOffset":4,"staged":{"arch":"n16r8","version":"1.2.3","packed":16908291}}' "$SS_OTA_TRANSFER_ID" "$SS_OTA_IMAGE_SHA256" > "$SS_OTA_BODY_FILE"
    else
      printf '{"state":"receiving","transferId":"%s","sha256":"%s","nextOffset":%s}' "$SS_OTA_TRANSFER_ID" "$SS_OTA_IMAGE_SHA256" "$mock_offset" > "$SS_OTA_BODY_FILE"
    fi
    SS_OTA_CURL_EXIT=0
    SS_OTA_HTTP_STATUS=200
    return 0
  fi
  if [[ "$method" == "POST" && "$path" == "/api/v1/ota/flash" ]]; then
    printf '{}' > "$SS_OTA_BODY_FILE"
    SS_OTA_CURL_EXIT=56
    SS_OTA_HTTP_STATUS=000
    return 1
  fi
  if [[ "$method" == "GET" && "$path" == "/api/v1/ota/session" ]]; then
    printf '{"state":"receiving","transferId":"%s","sha256":"%s","nextOffset":%s}' "$SS_OTA_TRANSFER_ID" "$SS_OTA_IMAGE_SHA256" "$mock_offset" > "$SS_OTA_BODY_FILE"
    SS_OTA_CURL_EXIT=0; SS_OTA_HTTP_STATUS=200; return 0
  fi
  if [[ "$method" == "GET" && "$path" == "/api/v1/ota" ]]; then
    if [[ "$mock_mode" == "lost-commit" ]]; then
      printf '{"state":"committed","restartPending":true}' > "$SS_OTA_BODY_FILE"
    else
      printf '{"state":"idle","safe":true}' > "$SS_OTA_BODY_FILE"
    fi
    SS_OTA_CURL_EXIT=0
    SS_OTA_HTTP_STATUS=200
    return 0
  fi
  return 1
}

# Avoid real backoff in the policy test.
ss_ota_backoff() { :; }

mock_mode=retry
mock_patches=0
mock_offset=0
ss_ota_upload
check test "$mock_patches" -eq 2

mock_mode=safety
mock_patches=0
mock_offset=0
if ss_ota_upload; then
  echo 'FAIL: SAFETY_LOST was retried/accepted' >&2
  failures=$((failures + 1))
fi
check test "$mock_patches" -eq 1

mock_mode=lost-commit
ss_ota_commit

if (( failures != 0 )); then
  exit 1
fi
echo 'OTA CLI resilience: retry and reconciliation policy OK'
