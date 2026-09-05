#!/usr/bin/env bash
# Shared resilient OTA client. Source after shotstopper_cli.sh (bash 3.2).

SS_OTA_UPLOAD_ATTEMPTS=3
SS_OTA_COMMIT_ATTEMPTS=2

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
  # method, path, optional binary file, timeout seconds
  local method="$1" path="$2" payload="$3" timeout="$4"
  local args
  args=(--show-error --config "$SS_OTA_CURL_CONFIG" --connect-timeout 10
        --max-time "$timeout" --output "$SS_OTA_BODY_FILE"
        --write-out '%{http_code}' --request "$method")
  : > "$SS_OTA_BODY_FILE"
  if [[ -n "$payload" ]]; then
    # The ESP HTTP server is deliberately small. Removing Expect avoids a
    # needless extra round trip before the request body starts flowing.
    args+=(--progress-bar --header 'Content-Type: application/octet-stream'
           --header 'Expect:' --data-binary "@$payload")
  else
    args+=(--silent)
  fi
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
  if [[ -z "$SS_OTA_IMAGE_VERSION" ]]; then
    # --no-check skipped the local image tag, so there is nothing to compare
    # version/packed against. Accept any staged image of the requested arch;
    # the post-reboot confirmation still validates the result.
    [[ -n "$(ss_ota_field staged.version)" ]] &&
        [[ "$(ss_ota_field staged.arch)" == "$SS_OTA_IMAGE_ARCH" ]]
    return
  fi
  [[ "$(ss_ota_field staged.arch)" == "$SS_OTA_IMAGE_ARCH" ]] &&
      [[ "$(ss_ota_field staged.version)" == "$SS_OTA_IMAGE_VERSION" ]] &&
      [[ "$(ss_ota_field staged.packed)" == "$SS_OTA_IMAGE_PACKED" ]]
}

ss_ota_transient_failure() {
  # curl failure, a broken upload, or temporary server/network overload.
  local curl_exit="$1" http_status="$2" error="$3"
  [[ "$curl_exit" != "0" ]] && return 0
  case "$http_status" in
    408|429|500|502|503|504) return 0 ;;
    400) [[ "$error" == "RECEIVE_FAILED" ]] && return 0 ;;
  esac
  return 1
}

ss_ota_backoff() {
  local attempt="$1" max="$2"
  local seconds=$((attempt * attempt))
  printf 'Transient OTA failure; retrying in %ss (attempt %s/%s).\n' \
      "$seconds" "$((attempt + 1))" "$max" >&2
  sleep "$seconds"
}

ss_ota_upload() {
  local attempt=1
  while (( attempt <= SS_OTA_UPLOAD_ATTEMPTS )); do
    printf 'Uploading attempt %s/%s...\n' "$attempt" "$SS_OTA_UPLOAD_ATTEMPTS"
    if ss_ota_request POST /api/v1/ota "$SS_OTA_IMAGE" 900 &&
        [[ "$SS_OTA_HTTP_STATUS" == "200" ]]; then
      return 0
    fi

    local curl_exit="$SS_OTA_CURL_EXIT" http_status="$SS_OTA_HTTP_STATUS"
    local error="$(ss_ota_field error)"
    printf 'Upload attempt %s ended with curl=%s HTTP=%s%s.\n' \
        "$attempt" "$curl_exit" "${http_status:-no-response}" \
        "${error:+ code=$error}" >&2

    # If the response itself was lost after verification, avoid sending the
    # image again. The controller verifies the same image identity before it
    # can stage it; compare all of its stable fields before continuing.
    if ss_ota_status && ss_ota_staged_matches_image; then
      echo 'The controller verified the image; its upload response was lost.' >&2
      return 0
    fi

    if (( attempt < SS_OTA_UPLOAD_ATTEMPTS )) &&
        ss_ota_transient_failure "$curl_exit" "$http_status" "$error"; then
      # A failed legacy POST discards its partial slot before returning. Only
      # restart it after the device confirms that it is idle and safe.
      if [[ "$(ss_ota_field state)" == "idle" ]] &&
          [[ "$(ss_ota_field safe)" == "true" ]]; then
        ss_ota_backoff "$attempt" "$SS_OTA_UPLOAD_ATTEMPTS"
        attempt=$((attempt + 1))
        continue
      fi
    fi
    echo 'Upload did not reach a verified state. Running firmware was not touched.' >&2
    return 1
  done
  return 1
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
    if (( attempt < SS_OTA_COMMIT_ATTEMPTS )) &&
        ss_ota_transient_failure "$curl_exit" "$http_status" "$error" &&
        ss_ota_staged_matches_image; then
      ss_ota_backoff "$attempt" "$SS_OTA_COMMIT_ATTEMPTS"
      attempt=$((attempt + 1))
      continue
    fi
    return 1
  done
  return 1
}

ss_ota_cleanup() {
  rm -f "${SS_OTA_CURL_CONFIG:-}" "${SS_OTA_BODY_FILE:-}"
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
    # The build step already verified this image (./scripts/bo-idf or an
    # explicit --no-check). Keep only the arch so the staged-image match can
    # degrade to arch-only in ss_ota_staged_matches_image.
    SS_OTA_IMAGE_ARCH="$arch"
    SS_OTA_IMAGE_VERSION=""
    SS_OTA_IMAGE_PACKED=""
    echo "Skipping local image verification (--no-check)."
  else
    local tag_json
    echo 'Local image identity:'
    tag_json="$(node ./scripts/image_tag.js "$SS_OTA_IMAGE" --expect-arch "$arch" --json)" || return $?
    SS_OTA_IMAGE_ARCH="$(node -e 'process.stdout.write(JSON.parse(process.argv[1]).arch)' "$tag_json")"
    SS_OTA_IMAGE_VERSION="$(node -e 'process.stdout.write(JSON.parse(process.argv[1]).version)' "$tag_json")"
    SS_OTA_IMAGE_PACKED="$(node -e 'process.stdout.write(String(JSON.parse(process.argv[1]).packed))' "$tag_json")"
  fi

  SS_OTA_CURL_CONFIG="$(mktemp "${TMPDIR:-/tmp}/shotstopper-ota.XXXXXX")"
  chmod 600 "$SS_OTA_CURL_CONFIG"
  SS_OTA_BODY_FILE="$(mktemp "${TMPDIR:-/tmp}/shotstopper-ota-body.XXXXXX")"
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

  local size
  size="$(wc -c < "$SS_OTA_IMAGE" | tr -d ' ')"
  printf 'Uploading %s (%s KiB)...\n' "$SS_OTA_IMAGE" "$((size / 1024))"
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
