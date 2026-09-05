#!/usr/bin/env bash
# Shared resilient OTA client. Source after shotstopper_cli.sh (bash 3.2).

SS_OTA_RANGE_ATTEMPTS=3
SS_OTA_COMMIT_ATTEMPTS=2
SS_OTA_CHUNK_BYTES=65536

ss_ota_field() {
  node -e '
const fs = require("fs"); let data = {};
try { data = JSON.parse(fs.readFileSync(process.argv[1], "utf8")); } catch (_) {}
const value = process.argv[2].split(".").reduce(
  (node, key) => node == null ? undefined : node[key], data);
process.stdout.write(value == null ? "" : String(value));
' "$SS_OTA_BODY_FILE" "$1"
}

ss_ota_request() {
  # method, path, optional payload file, timeout seconds, extra curl headers
  local method="$1" path="$2" payload="$3" timeout="$4"
  shift 4
  local args
  args=(--show-error --config "$SS_OTA_CURL_CONFIG" --connect-timeout 10
        --max-time "$timeout" --output "$SS_OTA_BODY_FILE"
        --write-out '%{http_code}' --request "$method")
  : > "$SS_OTA_BODY_FILE"
  if [[ -n "$payload" ]]; then
    # The ESP HTTP server is deliberately small. Removing Expect avoids a
    # needless extra round trip before the request body starts flowing.
    args+=(--progress-bar --header "Content-Type: ${SS_OTA_CONTENT_TYPE:-application/octet-stream}"
           --header 'Expect:' --data-binary "@$payload")
  else
    args+=(--silent)
  fi
  while (( $# > 0 )); do
    args+=(--header "$1")
    shift
  done
  SS_OTA_HTTP_STATUS=""
  set +e
  SS_OTA_HTTP_STATUS="$(curl "${args[@]}" "$SS_OTA_BASE$path")"
  SS_OTA_CURL_EXIT=$?
  set -e
  [[ "$SS_OTA_CURL_EXIT" == "0" ]]
}

ss_ota_status() {
  ss_ota_request GET /api/v1/ota "" 20 && [[ "$SS_OTA_HTTP_STATUS" == "200" ]]
}

ss_ota_staged_matches_image() {
  [[ "$(ss_ota_field state)" == "staged" ]] || return 1
  [[ "$(ss_ota_field transferId)" == "$SS_OTA_TRANSFER_ID" ]] &&
      [[ "$(ss_ota_field sha256)" == "$SS_OTA_IMAGE_SHA256" ]] &&
      [[ "$(ss_ota_field staged.arch)" == "$SS_OTA_IMAGE_ARCH" ]] &&
      [[ "$(ss_ota_field staged.version)" == "$SS_OTA_IMAGE_VERSION" ]] &&
      [[ "$(ss_ota_field staged.packed)" == "$SS_OTA_IMAGE_PACKED" ]]
}

ss_ota_backoff() {
  local attempt="$1" max="$2"
  local seconds=$((attempt * attempt))
  printf 'Transient OTA failure; retrying in %ss (attempt %s/%s).\n' \
      "$seconds" "$((attempt + 1))" "$max" >&2
  sleep "$seconds"
}

ss_ota_upload() {
  SS_OTA_CONTENT_TYPE=application/json
  if ! ss_ota_request POST /api/v1/ota/session "$SS_OTA_SESSION_BODY" 30 ||
      [[ "$SS_OTA_HTTP_STATUS" != "200" ]]; then
    echo 'The controller refused the OTA session.' >&2
    return 1
  fi
  SS_OTA_CONTENT_TYPE=application/octet-stream
  local offset="$(ss_ota_field nextOffset)"
  [[ "$offset" =~ ^[0-9]+$ ]] || return 1
  while (( offset < SS_OTA_IMAGE_SIZE )); do
    local end=$((offset + SS_OTA_CHUNK_BYTES))
    (( end > SS_OTA_IMAGE_SIZE )) && end=$SS_OTA_IMAGE_SIZE
    local length=$((end - offset)) attempt=1
    dd if="$SS_OTA_IMAGE" of="$SS_OTA_CHUNK_FILE" bs=1 skip="$offset" count="$length" 2>/dev/null
    while :; do
      if ss_ota_request PATCH /api/v1/ota "$SS_OTA_CHUNK_FILE" 90 \
          "X-OTA-Transfer: $SS_OTA_TRANSFER_ID" "X-OTA-Offset: $offset" \
          "X-OTA-Length: $SS_OTA_IMAGE_SIZE" \
          "Content-Range: bytes $offset-$((end - 1))/$SS_OTA_IMAGE_SIZE" &&
          [[ "$SS_OTA_HTTP_STATUS" =~ ^(200|208)$ ]]; then
        local next="$(ss_ota_field nextOffset)"
        [[ "$next" =~ ^[0-9]+$ ]] && (( next > offset )) || return 1
        offset="$next"
        printf 'Uploaded %s / %s KiB\n' "$((offset / 1024))" "$((SS_OTA_IMAGE_SIZE / 1024))"
        break
      fi
      case "$(ss_ota_field error)" in
        SAFETY_LOST|OTA_SHA256_MISMATCH|OTA_SESSION_IDENTITY_MISMATCH|OTA_INVALID_RANGE)
          return 1 ;;
      esac
      # A lost response is reconciled before any retry.  Only this exact,
      # idempotent range can be repeated, never the entire image.
      if ss_ota_request GET /api/v1/ota/session "" 20 &&
          [[ "$(ss_ota_field transferId)" == "$SS_OTA_TRANSFER_ID" ]] &&
          [[ "$(ss_ota_field sha256)" == "$SS_OTA_IMAGE_SHA256" ]]; then
        local next="$(ss_ota_field nextOffset)"
        if [[ "$next" =~ ^[0-9]+$ ]] && (( next > offset )); then offset="$next"; break; fi
      fi
      (( attempt < SS_OTA_RANGE_ATTEMPTS )) || return 1
      ss_ota_backoff "$attempt" "$SS_OTA_RANGE_ATTEMPTS"
      attempt=$((attempt + 1))
    done
  done
  ss_ota_staged_matches_image
}

ss_ota_commit() {
  local attempt=1
  while (( attempt <= SS_OTA_COMMIT_ATTEMPTS )); do
    if ss_ota_request POST /api/v1/ota/flash "" 30 &&
        [[ "$SS_OTA_HTTP_STATUS" == "202" ]]; then
      return 0
    fi
    local curl_exit="$SS_OTA_CURL_EXIT" http_status="$SS_OTA_HTTP_STATUS"
    local error="$(ss_ota_field error)"
    printf 'Commit attempt %s ended with curl=%s HTTP=%s%s.\n' \
        "$attempt" "$curl_exit" "${http_status:-no-response}" \
        "${error:+ code=$error}" >&2

    # POST /flash is not blindly replayed: first reconcile a lost 202.
    if ss_ota_status && ([[ "$(ss_ota_field restartPending)" == "true" ]] ||
        [[ "$(ss_ota_field state)" == "committed" ]]); then
      echo 'The controller accepted the commit; its response was lost.' >&2
      return 0
    fi
    if (( attempt < SS_OTA_COMMIT_ATTEMPTS )) && ss_ota_staged_matches_image; then
      ss_ota_backoff "$attempt" "$SS_OTA_COMMIT_ATTEMPTS"
      attempt=$((attempt + 1))
      continue
    fi
    return 1
  done
  return 1
}

ss_ota_cleanup() {
  rm -f "${SS_OTA_CURL_CONFIG:-}" "${SS_OTA_BODY_FILE:-}" "${SS_OTA_SESSION_BODY:-}" "${SS_OTA_CHUNK_FILE:-}"
}

ss_ota_run() {
  # image, requested arch, host, password, force, skip local image check
  SS_OTA_IMAGE="$1"
  local arch="$2" host="$3" password="$4" force="$5" skip_local_check="${6:-0}"
  SS_OTA_BASE="http://$host"

  for tool in curl node; do
    command -v "$tool" >/dev/null 2>&1 || {
      echo "$tool not found on PATH." >&2
      return 127
    }
  done

  if [[ "$skip_local_check" == "1" ]]; then
    echo '--no-check cannot perform resumable OTA: the SHA-256, architecture, and version are required.' >&2
    return 2
  else
    local tag_json
    echo 'Local image identity:'
    tag_json="$(node ./scripts/image_tag.js "$SS_OTA_IMAGE" --expect-arch "$arch" --json)" || return $?
    SS_OTA_IMAGE_ARCH="$(node -e 'process.stdout.write(JSON.parse(process.argv[1]).arch)' "$tag_json")"
    SS_OTA_IMAGE_VERSION="$(node -e 'process.stdout.write(JSON.parse(process.argv[1]).version)' "$tag_json")"
    SS_OTA_IMAGE_PACKED="$(node -e 'process.stdout.write(String(JSON.parse(process.argv[1]).packed))' "$tag_json")"
  fi

  SS_OTA_IMAGE_SIZE="$(wc -c < "$SS_OTA_IMAGE" | tr -d ' ')"
  SS_OTA_IMAGE_SHA256="$(shasum -a 256 "$SS_OTA_IMAGE" | awk '{print $1}')"
  SS_OTA_TRANSFER_ID="$(node -e 'const c=require("crypto");process.stdout.write(c.randomBytes(18).toString("hex"))')"

  SS_OTA_CURL_CONFIG="$(mktemp "${TMPDIR:-/tmp}/shotstopper-ota.XXXXXX")"
  chmod 600 "$SS_OTA_CURL_CONFIG"
  SS_OTA_BODY_FILE="$(mktemp "${TMPDIR:-/tmp}/shotstopper-ota-body.XXXXXX")"
  SS_OTA_SESSION_BODY="$(mktemp "${TMPDIR:-/tmp}/shotstopper-ota-session.XXXXXX")"
  SS_OTA_CHUNK_FILE="$(mktemp "${TMPDIR:-/tmp}/shotstopper-ota-chunk.XXXXXX")"
  node -e 'process.stdout.write(JSON.stringify({size:Number(process.argv[1]),sha256:process.argv[2],arch:process.argv[3],version:process.argv[4],transferId:process.argv[5]}))' \
      "$SS_OTA_IMAGE_SIZE" "$SS_OTA_IMAGE_SHA256" "$SS_OTA_IMAGE_ARCH" "$SS_OTA_IMAGE_VERSION" "$SS_OTA_TRANSFER_ID" > "$SS_OTA_SESSION_BODY"
  password="${password//\\/\\\\}"
  password="${password//\"/\\\"}"
  printf 'header = "X-Device-Password: %s"\n' "$password" > "$SS_OTA_CURL_CONFIG"
  unset password

  echo
  echo "Querying $SS_OTA_BASE ..."
  if ! ss_ota_status; then
    echo "Could not reach the controller at $SS_OTA_BASE." >&2
    echo 'Check the IP (Admin or Diagnostic in the Web UI; SoftAP is 192.168.4.1).' >&2
    return 1
  fi
  if [[ "$(ss_ota_field available)" != "true" ]]; then
    echo 'This controller has no second application partition.' >&2
    echo 'Update it over USB instead.' >&2
    return 1
  fi
  printf 'Controller: %s · %s\n' "$(ss_ota_field running.version)" \
      "$(ss_ota_field running.arch)"
  if [[ "$(ss_ota_field safe)" != "true" ]]; then
    printf 'Machine is busy (%s). Wait until Ready.\n' "$(ss_ota_field lockReason)" >&2
    return 1
  fi

  printf 'Uploading %s (%s KiB, SHA-256 %s)…\n' "$SS_OTA_IMAGE" "$((SS_OTA_IMAGE_SIZE / 1024))" "$SS_OTA_IMAGE_SHA256"
  ss_ota_upload || return 1

  local staged_version staged_arch
  staged_version="$(ss_ota_field staged.version)"
  staged_arch="$(ss_ota_field staged.arch)"
  if [[ -z "$staged_version" ]]; then
    echo 'Controller did not report a verified image.' >&2
    return 1
  fi
  printf '\nVerified image: %s · %s\n' "$staged_version" "$staged_arch"

  if [[ "$force" != "1" ]] && ss_can_prompt; then
    local answer
    printf 'Commit and reboot the controller? [y/N]: ' > /dev/tty
    IFS= read -r answer < /dev/tty || answer=""
    case "$answer" in
      y|Y|yes|YES) ;;
      *)
        echo 'Cancelled. Discarding the image on the controller.'
        ss_ota_request POST /api/v1/ota/abort "" 20 || true
        return 1
        ;;
    esac
  fi

  echo 'Committing...'
  ss_ota_commit || {
    echo 'Could not confirm the boot partition change.' >&2
    return 1
  }

  if [[ "$force" == "1" ]]; then
    echo 'Waiting for reboot and OTA confirmation (up to 4 minutes)...'
    local deadline=$((SECONDS + 240))
    while (( SECONDS < deadline )); do
      if ss_ota_status &&
          [[ "$(ss_ota_field running.version)" == "$staged_version" ]] &&
          [[ "$(ss_ota_field running.arch)" == "$staged_arch" ]] &&
          [[ "$(ss_ota_field restartPending)" == "false" ]] &&
          [[ "$(ss_ota_field confirmed)" == "true" ]]; then
        echo 'OTA confirmed by the rebooted firmware.'
        return 0
      fi
      sleep 2
    done
    echo 'The image was committed, but was not confirmed within 4 minutes.' >&2
    echo 'Check the controller network connection and OTA status.' >&2
    return 1
  fi

  cat <<'EOF'

Done. The controller reboots once the machine is free.
The new version confirms itself by serving its Web UI; if it cannot, the
bootloader rolls back to the previous version.
EOF
  return 0
}
