#!/usr/bin/env bash
# Shared named-flag front end for the Shot Stopper developer scripts.
# Source from other scripts; do not execute this file directly.
#
# No script silently fills missing values. Every value comes from, in order:
#   1. a named flag on the command line
#   2. the matching environment variable
#   3. the hidden store file .shotstopper at the repository root
#   4. an interactive prompt for the keys the command requires
#      (Enter accepts the suggestion shown in brackets)
#
# A required serial port whose device node is missing is treated like an
# unset key: the scripts prompt and suggest the first live USB-CDC path.
#
# After a successful resolve the values used are merged into .shotstopper.
# The device password is never loaded, suggested, or persisted (use env/CLI
# each run).
#
# Written for bash 3.2 (the system bash on macOS): no associative arrays,
# no ${var,,} case conversion.

SS_CLI_KEYS="port arch speed host password flags build_dir output_dir"
SS_CLI_SECRET_KEYS="password"
# Extra compiler flags offered when the prompt for --flags is answered with Enter.
SS_CLI_DEFAULT_FLAGS='-Werror=deprecated-copy -DSHOT_STOPPER_ENABLE_BUZZER=1'
# Per-run path overrides. Persisting them would let a stale build_dir silently
# point an analysis at the wrong architecture, so they never touch the store.
SS_CLI_TRANSIENT_KEYS="build_dir output_dir"

if [[ -z "${SS_CLI_ROOT:-}" ]]; then
  SS_CLI_ROOT="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
fi
SS_CLI_STORE="$SS_CLI_ROOT/.shotstopper"

ss_upper() {
  printf '%s' "$1" | tr '[:lower:]' '[:upper:]'
}

ss_is_secret() {
  case " $SS_CLI_SECRET_KEYS " in
    *" $1 "*) return 0 ;;
  esac
  return 1
}

ss_is_transient() {
  case " $SS_CLI_TRANSIENT_KEYS " in
    *" $1 "*) return 0 ;;
  esac
  return 1
}

ss_set() {
  eval "SS_$(ss_upper "$1")=\$2"
}

ss_get() {
  eval "printf '%s' \"\${SS_$(ss_upper "$1")-}\""
}

ss_origin_set() {
  eval "SS_ORIGIN_$(ss_upper "$1")=\$2"
}

ss_origin() {
  eval "printf '%s' \"\${SS_ORIGIN_$(ss_upper "$1")-}\""
}

# A key counts as set when it has an origin, so an explicitly empty --flags
# is distinguishable from "never provided".
ss_is_set() {
  [[ -n "$(ss_origin "$1")" ]]
}

ss_put() {
  ss_set "$1" "$2"
  ss_origin_set "$1" "$3"
}

ss_cli_forget() {
  ss_set "$1" ""
  ss_origin_set "$1" ""
}

# True when the path is a present device node (not merely a non-empty string).
ss_port_exists() {
  [[ -n "$1" && -e "$1" ]]
}

ss_cli_reset() {
  local key
  for key in $SS_CLI_KEYS; do
    ss_set "$key" ""
    ss_origin_set "$key" ""
  done
}

ss_env_name() {
  case "$1" in
    port) printf 'SHOTSTOPPER_PORT' ;;
    arch) printf 'SHOTSTOPPER_ARCH' ;;
    speed) printf 'SHOTSTOPPER_SPEED' ;;
    host) printf 'SHOTSTOPPER_HOST' ;;
    password) printf 'SHOTSTOPPER_DEVICE_PASSWORD' ;;
    flags) printf 'SHOTSTOPPER_FLAGS' ;;
    build_dir) printf 'SHOTSTOPPER_BUILD_DIR_OVERRIDE' ;;
    output_dir) printf 'SHOTSTOPPER_OUTPUT_DIR' ;;
  esac
}

ss_flag_help() {
  cat <<'EOF'
Named parameters (long and short):
  -p, --port <path>        Serial port, e.g. /dev/cu.usbmodem2101
  -a, --arch <arch>        n8r4 | n16r8
  -s, --speed <baud>       Serial monitor baud rate, e.g. 115200
  -H, --host <ip|name>     Controller address for OTA
  -t, --password <pw>      Device password (never persisted)
  -f, --flags "<flags>"    Extra compile flags (single string)
  -b, --build-dir <path>   Build directory (static / static-idf only)
  -o, --output-dir <path>  Reports directory (static / static-idf only)
  -h, --help               Show this help

No script silently fills in missing values. Each parameter comes from the flag,
its environment variable, the .shotstopper file at the repository root, or a
prompt. At the prompt, Enter accepts the suggested value in brackets.
A missing or non-existent --port is prompted like the device password, with the
first detected USB-CDC device suggested.
After resolving parameters, the values used are saved to .shotstopper
(the device password is never saved or suggested).
EOF
}

SS_CLI_HELP_REQUESTED=0

ss_cli_die() {
  printf '%s\n' "$1" >&2
}

# Parses named flags. Positional arguments are rejected with a migration hint
# because these scripts used to take them.
ss_cli_parse() {
  local key value
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --*=*)
        key="${1%%=*}"
        value="${1#*=}"
        set -- "$key" "$value" "${@:2}"
        continue
        ;;
      -p|--port) key="port" ;;
      -a|--arch) key="arch" ;;
      -s|--speed) key="speed" ;;
      -H|--host) key="host" ;;
      -t|--password|--token) key="password" ;;
      -f|--flags) key="flags" ;;
      -b|--build-dir) key="build_dir" ;;
      -o|--output-dir) key="output_dir" ;;
      -h|--help|help)
        SS_CLI_HELP_REQUESTED=1
        return 0
        ;;
      -*)
        printf 'Unknown option: %s\n\n' "$1" >&2
        ss_flag_help >&2
        return 2
        ;;
      *)
        printf 'Positional argument not allowed: %s\n' "$1" >&2
        printf 'Scripts now use named parameters.\n' >&2
        printf 'For example: --port /dev/cu.usbmodem2101 --arch n16r8 --speed 115200\n\n' >&2
        ss_flag_help >&2
        return 2
        ;;
    esac
    shift
    if [[ $# -eq 0 ]]; then
      printf 'Missing value for --%s.\n' "$key" >&2
      return 2
    fi
    value="$1"
    shift
    # --flags is one logical string, but people often pass several -D/-W tokens
    # unquoted. Swallow consecutive compiler tokens so the second -D is not
    # treated as an unknown script option.
    if [[ "$key" == "flags" ]]; then
      while [[ $# -gt 0 ]]; do
        case "$1" in
          -D*|-W*|-I*)
            value="$value $1"
            shift
            ;;
          [A-Za-z_]*=*)
            value="$value $1"
            shift
            ;;
          *) break ;;
        esac
      done
    fi
    ss_put "$key" "$value" "cli"
  done
  return 0
}

# g++ treats a bare MACRO=value token as a linker input. Prefix -D so
# SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=1 is a define, not a missing file.
ss_normalize_compiler_flags() {
  local raw="$1" out="" tok
  # Word-splitting is intentional: extra_flags is a space-separated list.
  # shellcheck disable=SC2086
  set -- $raw
  for tok in "$@"; do
    case "$tok" in
      [A-Za-z_]*=*) tok="-D$tok" ;;
    esac
    out="$out $tok"
  done
  printf '%s' "${out# }"
}

ss_cli_apply_env() {
  local key name value
  for key in $SS_CLI_KEYS; do
    ss_is_set "$key" && continue
    name="$(ss_env_name "$key")"
    [[ -n "$name" ]] || continue
    eval "value=\${$name-}"
    # Legacy alias: older docs/scripts used SHOTSTOPPER_OTA_TOKEN.
    if [[ -z "$value" && "$key" == "password" ]]; then
      value="${SHOTSTOPPER_OTA_TOKEN-}"
    fi
    # Only an exported, non-empty value counts; an empty export is ignored so
    # a stale `export SHOTSTOPPER_HOST=` cannot silently win over the store.
    [[ -n "$value" ]] || continue
    ss_put "$key" "$value" "env"
  done
}

ss_cli_load_store() {
  [[ -f "$SS_CLI_STORE" ]] || return 0
  local line key value
  while IFS= read -r line || [[ -n "$line" ]]; do
    case "$line" in
      ''|'#'*) continue ;;
      *'='*) ;;
      *) continue ;;
    esac
    key="${line%%=*}"
    value="${line#*=}"
    case " $SS_CLI_KEYS " in
      *" $key "*) ;;
      *) continue ;;
    esac
    ss_is_transient "$key" && continue
    ss_is_secret "$key" && continue
    ss_is_set "$key" && continue
    ss_put "$key" "$value" "store"
  done < "$SS_CLI_STORE"
  return 0
}

# Reads one key from the store file without touching session state.
ss_cli_store_value() {
  local want="$1" line key value
  [[ -f "$SS_CLI_STORE" ]] || return 1
  while IFS= read -r line || [[ -n "$line" ]]; do
    case "$line" in
      ''|'#'*) continue ;;
      *'='*) ;;
      *) continue ;;
    esac
    key="${line%%=*}"
    [[ "$key" == "$want" ]] || continue
    value="${line#*=}"
    printf '%s' "$value"
    return 0
  done < "$SS_CLI_STORE"
  return 1
}

ss_display_value() {
  local key="$1" value
  value="$(ss_get "$key")"
  if ss_is_secret "$key"; then
    if [[ -n "$value" ]]; then
      printf '********'
    else
      printf '(empty)'
    fi
    return 0
  fi
  if [[ -z "$value" ]]; then
    printf '(empty)'
  else
    printf '%s' "$value"
  fi
}

# Prints which values come from where, and which ones are still missing.
ss_cli_summary() {
  local wanted="$1" missing="$2"
  local key line_cli="" line_env="" line_store=""
  for key in $wanted; do
    ss_is_set "$key" || continue
    case "$(ss_origin "$key")" in
      cli) line_cli="$line_cli $key=$(ss_display_value "$key")" ;;
      env) line_env="$line_env $key=$(ss_display_value "$key")" ;;
      store) line_store="$line_store $key=$(ss_display_value "$key")" ;;
    esac
  done
  [[ -n "$line_cli" ]] && printf 'From CLI:                %s\n' "${line_cli# }"
  [[ -n "$line_env" ]] && printf 'From environment:        %s\n' "${line_env# }"
  [[ -n "$line_store" ]] && printf 'Stored (.shotstopper):  %s\n' "$line_store"
  if [[ -n "$missing" ]]; then
    printf 'Missing:                 %s\n' "$(printf '%s' "$missing" | tr ' ' ',' | sed 's/,/, /g')"
  fi
  return 0
}

ss_can_prompt() {
  [[ -z "${SHOTSTOPPER_NONINTERACTIVE:-}" ]] &&
    [[ -r /dev/tty ]] && [[ -w /dev/tty ]]
}

ss_prompt_text() {
  case "$1" in
    port) printf 'Serial port' ;;
    arch) printf 'Architecture (n8r4 or n16r8)' ;;
    speed) printf 'Monitor baud rate' ;;
    host) printf 'Controller address (IP or hostname)' ;;
    password) printf 'Device password' ;;
    flags) printf 'Extra compile flags' ;;
    build_dir) printf 'Build directory' ;;
    output_dir) printf 'Reports directory' ;;
    *) printf '%s' "$1" ;;
  esac
}

# Suggestion shown in [brackets] and used when the user presses Enter.
ss_prompt_suggestion() {
  local key="$1" current detected arch_s
  ss_is_secret "$key" && return 0

  # Port prompts always prefer a live USB-CDC device over a stale saved path.
  if [[ "$key" == "port" ]]; then
    if declare -f shotstopper_detect_ports >/dev/null 2>&1; then
      detected="$(shotstopper_detect_ports | { IFS= read -r first || true; printf '%s' "${first:-}"; })"
      if [[ -n "$detected" ]]; then
        printf '%s' "$detected"
        return 0
      fi
    fi
    current="$(ss_get port)"
    if ss_port_exists "$current"; then
      printf '%s' "$current"
      return 0
    fi
    current="$(ss_cli_store_value port 2>/dev/null || true)"
    if ss_port_exists "$current"; then
      printf '%s' "$current"
      return 0
    fi
    printf '%s' '/dev/cu.usbmodem2101'
    return 0
  fi

  current="$(ss_get "$key")"
  if [[ -n "$current" ]]; then
    printf '%s' "$current"
    return 0
  fi
  current="$(ss_cli_store_value "$key" 2>/dev/null || true)"
  if [[ -n "$current" ]]; then
    printf '%s' "$current"
    return 0
  fi
  case "$key" in
    arch) printf 'n16r8' ;;
    speed) printf '115200' ;;
    host) printf '192.168.4.1' ;;
    password) return 0 ;;
    flags) printf '%s' "$SS_CLI_DEFAULT_FLAGS" ;;
    build_dir)
      arch_s="$(ss_get arch)"
      [[ -n "$arch_s" ]] || arch_s="n16r8"
      case "$arch_s" in
        n8r4|esp32s3-n8r4) printf 'build/n8r4' ;;
        *) printf 'build/n16r8' ;;
      esac
      ;;
    output_dir) printf 'reports/static-analysis' ;;
    *) printf '' ;;
  esac
}

ss_prompt_hint() {
  local detected
  case "$1" in
    port)
      if declare -f shotstopper_detect_ports >/dev/null 2>&1; then
        detected="$(shotstopper_detect_ports | tr '\n' ' ')" || detected=""
        if [[ -n "$detected" ]]; then
          printf 'Detected USB ports: %s\n' "${detected% }" > /dev/tty
          printf 'Press Enter to use the suggested port, or type another path.\n' > /dev/tty
        else
          printf 'No USB CDC device detected (/dev/cu.usbmodem* or /dev/ttyACM*).\n' > /dev/tty
        fi
      fi
      ;;
    arch)
      printf 'n8r4 = 8 MB flash / 4 MB PSRAM; n16r8 = 16 MB flash / 8 MB PSRAM\n' > /dev/tty
      ;;
  esac
}

ss_prompt_one() {
  local key="$1" prompt value suggestion
  prompt="$(ss_prompt_text "$key")"
  suggestion="$(ss_prompt_suggestion "$key")"
  ss_prompt_hint "$key"
  if [[ -n "$suggestion" ]]; then
    printf '%s [%s]: ' "$prompt" "$suggestion" > /dev/tty
  else
    printf '%s: ' "$prompt" > /dev/tty
  fi
  if ss_is_secret "$key"; then
    IFS= read -r -s value < /dev/tty || return 1
    printf '\n' > /dev/tty
  else
    IFS= read -r value < /dev/tty || return 1
  fi
  if [[ -z "$value" && -n "$suggestion" ]]; then
    value="$suggestion"
  fi
  ss_put "$key" "$value" "prompt"
  return 0
}

ss_validate_key() {
  local key="$1" value
  value="$(ss_get "$key")"
  case "$key" in
    arch)
      # shotstopper_resolve_board prints its own diagnostics.
      shotstopper_resolve_board "$value" || return 1
      ;;
    speed)
      case "$value" in
        ''|*[!0-9]*)
          ss_cli_die "Invalid baud rate: '$value' (digits only)."
          return 1
          ;;
      esac
      ;;
    host)
      if [[ -z "$value" ]]; then
        ss_cli_die "Parameter --host cannot be empty."
        return 1
      fi
      case "$value" in
        *[[:space:]]*)
          ss_cli_die "Invalid host: '$value' (must not contain spaces)."
          return 1
          ;;
      esac
      ;;
    port)
      if [[ -z "$value" ]]; then
        ss_cli_die "Parameter --port cannot be empty."
        return 1
      fi
      if ! ss_port_exists "$value"; then
        ss_cli_die "Serial port $value does not exist."
        return 1
      fi
      ;;
    password|build_dir|output_dir)
      if [[ -z "$value" ]]; then
        ss_cli_die "Parameter --$(printf '%s' "$key" | tr '_' '-') cannot be empty."
        return 1
      fi
      ;;
    flags)
      if [[ -n "$value" ]]; then
        ss_put flags "$(ss_normalize_compiler_flags "$value")" "$(ss_origin flags)"
      fi
      ;;
  esac
  return 0
}

# ss_cli_resolve "<required keys>" ["<optional keys to ask once>"]
# Loads env + store, prints the summary, prompts for what is missing and
# validates the result. Fails with exit code 2 when it cannot prompt.
#
# A required --port whose device node is missing (stale .shotstopper entry,
# vanished USB path, or a bad CLI/env value) is forgotten and prompted the
# same way as an unset password: suggest the first live USB-CDC port, accept
# Enter, then persist the chosen path.
ss_cli_resolve() {
  local required="$1" optional="${2:-}"
  local wanted="$required $optional"
  local key missing="" port_val

  ss_cli_apply_env
  ss_cli_load_store

  # Drop unusable ports before the missing-key scan so flash/monitor prompt
  # early instead of failing after a long build.
  if [[ " $required " == *" port "* ]] && ss_is_set port; then
    port_val="$(ss_get port)"
    if ! ss_port_exists "$port_val"; then
      if [[ -n "$port_val" ]]; then
        printf 'Serial port %s does not exist.\n' "$port_val" >&2
      fi
      ss_cli_forget port
    fi
  fi

  for key in $required; do
    ss_is_set "$key" || missing="$missing $key"
  done
  missing="${missing# }"

  # Optional keys that were never provided stay empty rather than blocking a
  # non-interactive run. Prompting for them only happens when a TTY is there.
  local optional_missing=""
  for key in $optional; do
    ss_is_set "$key" && continue
    optional_missing="$optional_missing $key"
  done
  optional_missing="${optional_missing# }"

  ss_cli_summary "$wanted" "$missing"

  if [[ -n "$optional_missing" ]] && ss_can_prompt; then
    for key in $optional_missing; do
      ss_prompt_one "$key" || {
        printf 'Input interrupted.\n' >&2
        return 2
      }
    done
  elif [[ -n "$optional_missing" ]]; then
    for key in $optional_missing; do
      ss_put "$key" "" "default"
    done
  fi

  if [[ -n "$missing" ]]; then
    if ! ss_can_prompt; then
      printf '\nNo interactive terminal to prompt for: %s\n' "$missing" >&2
      printf 'Pass them as flags or save them in %s.\n' "$SS_CLI_STORE" >&2
      case " $missing " in
        *' host '*)
          printf 'The IP appears under Admin/Diagnostic in the Web UI; on SoftAP it is 192.168.4.1.\n' >&2
          ;;
        *' port '*)
          printf 'Plug in the ESP32-S3 over USB and pass --port /dev/cu.usbmodem… (macOS) or /dev/ttyACM… (Linux).\n' >&2
          ;;
      esac
      return 2
    fi
    for key in $missing; do
      ss_prompt_one "$key" || {
        printf 'Input interrupted.\n' >&2
        return 2
      }
    done
  fi

  for key in $wanted; do
    ss_is_set "$key" || continue
    while ! ss_validate_key "$key"; do
      # Wrong path typed at the prompt: ask again instead of aborting the run.
      if [[ "$key" == "port" ]] && ss_can_prompt; then
        ss_cli_forget port
        ss_prompt_one port || {
          printf 'Input interrupted.\n' >&2
          return 2
        }
        continue
      fi
      return 2
    done
  done
  ss_cli_save
  return 0
}

ss_cli_save() {
  local key value tmp
  tmp="${TMPDIR:-/tmp}/shotstopper.XXXXXX"
  tmp="$(mktemp "$tmp")" || return 0
  chmod 600 "$tmp" 2>/dev/null || true
  {
    printf '# Values remembered by the Shot Stopper scripts.\n'
    printf '# Local file, ignored by git. Does not store the device password.\n'
    for key in $SS_CLI_KEYS; do
      ss_is_transient "$key" && continue
      ss_is_secret "$key" && continue
      if ss_is_set "$key"; then
        value="$(ss_get "$key")"
      else
        value="$(ss_cli_store_value "$key" 2>/dev/null || true)"
      fi
      [[ -n "$value" ]] || continue
      # A newline in a stored value would split into a second "key=value" line
      # on the next load and could clobber another key.
      case "$value" in
        *$'\n'*) continue ;;
      esac
      printf '%s=%s\n' "$key" "$value"
    done
  } > "$tmp"
  mv -f "$tmp" "$SS_CLI_STORE" 2>/dev/null || rm -f "$tmp"
  chmod 600 "$SS_CLI_STORE" 2>/dev/null || true
  return 0
}

# Re-emits the resolved values as flags so wrapper scripts can call children
# without triggering a second round of prompts.
#
# Callers must expand this as ${SS_CLI_FORWARD[@]+"${SS_CLI_FORWARD[@]}"}:
# bash 3.2 treats "${array[@]}" on an empty array as an unbound variable, which
# under `set -u` aborts the wrapper instead of running the child with no flags.
ss_cli_flags_for() {
  local key
  SS_CLI_FORWARD=()
  for key in "$@"; do
    ss_is_set "$key" || continue
    case "$key" in
      port) SS_CLI_FORWARD+=(--port "$(ss_get port)") ;;
      arch) SS_CLI_FORWARD+=(--arch "$(ss_get arch)") ;;
      speed) SS_CLI_FORWARD+=(--speed "$(ss_get speed)") ;;
      host) SS_CLI_FORWARD+=(--host "$(ss_get host)") ;;
      # password is deliberately absent: a command line is world-readable
      # through `ps` for as long as the child runs, which for an OTA is
      # minutes. Use ss_cli_export_secrets instead.
      flags) SS_CLI_FORWARD+=(--flags "$(ss_get flags)") ;;
      build_dir) SS_CLI_FORWARD+=(--build-dir "$(ss_get build_dir)") ;;
      output_dir) SS_CLI_FORWARD+=(--output-dir "$(ss_get output_dir)") ;;
    esac
  done
}

# Hands secrets to a child process through the environment, which other users
# cannot read, rather than through argv, which they can.
ss_cli_export_secrets() {
  if ss_is_set password; then
    SHOTSTOPPER_DEVICE_PASSWORD="$(ss_get password)"
    export SHOTSTOPPER_DEVICE_PASSWORD
  fi
}

ss_cli_reset
