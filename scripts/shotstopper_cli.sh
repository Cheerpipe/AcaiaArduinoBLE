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
# Written for bash 3.2 (the system bash on macOS): no associative arrays,
# no ${var,,} case conversion.

SS_CLI_KEYS="port arch speed host token flags build_dir output_dir"
SS_CLI_SECRET_KEYS="token"
# Extra compiler flags offered when the prompt for --flags is answered with Enter.
SS_CLI_DEFAULT_FLAGS='-Werror=deprecated-copy -DSHOT_STOPPER_ENABLE_REMOTE_CN9=1 -DSHOT_STOPPER_ENABLE_BUZZER=2'
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
    token) printf 'SHOTSTOPPER_OTA_TOKEN' ;;
    flags) printf 'SHOTSTOPPER_FLAGS' ;;
    build_dir) printf 'SHOTSTOPPER_BUILD_DIR_OVERRIDE' ;;
    output_dir) printf 'SHOTSTOPPER_OUTPUT_DIR' ;;
  esac
}

ss_flag_help() {
  cat <<'EOF'
Parámetros con nombre (largo y corto):
  -p, --port <ruta>        Puerto serial, por ejemplo /dev/cu.usbmodem2101
  -a, --arch <arq>         n8r4 | n16r8
  -s, --speed <baudios>    Velocidad del monitor serie, por ejemplo 115200
  -H, --host <ip|nombre>   Dirección del controlador para OTA
  -t, --token <clave>      Token OTA (la contraseña del punto de acceso)
  -f, --flags "<flags>"    Flags extra de compilación (una sola cadena)
  -b, --build-dir <ruta>   Carpeta de compilación (solo static_report)
  -o, --output-dir <ruta>  Carpeta de reportes (solo static_report)
  -h, --help               Muestra esta ayuda

Ningún script rellena en silencio lo que falte. Cada parámetro se toma del flag,
de su variable de entorno, del archivo .shotstopper en la raíz del repositorio,
o se pregunta. En la pregunta, Enter acepta el valor sugerido entre corchetes.
Tras una ejecución correcta los valores usados quedan guardados.
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
      -t|--token) key="token" ;;
      -f|--flags) key="flags" ;;
      -b|--build-dir) key="build_dir" ;;
      -o|--output-dir) key="output_dir" ;;
      -h|--help|help)
        SS_CLI_HELP_REQUESTED=1
        return 0
        ;;
      -*)
        printf 'Opción desconocida: %s\n\n' "$1" >&2
        ss_flag_help >&2
        return 2
        ;;
      *)
        printf 'Argumento posicional no admitido: %s\n' "$1" >&2
        printf 'Los scripts ahora usan parámetros con nombre.\n' >&2
        printf 'Por ejemplo: --port /dev/cu.usbmodem2101 --arch n16r8 --speed 115200\n\n' >&2
        ss_flag_help >&2
        return 2
        ;;
    esac
    shift
    if [[ $# -eq 0 ]]; then
      printf 'Falta el valor de --%s.\n' "$key" >&2
      return 2
    fi
    ss_put "$key" "$1" "cli"
    shift
  done
  return 0
}

ss_cli_apply_env() {
  local key name value
  for key in $SS_CLI_KEYS; do
    ss_is_set "$key" && continue
    name="$(ss_env_name "$key")"
    [[ -n "$name" ]] || continue
    eval "value=\${$name-}"
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
    ss_is_set "$key" && continue
    ss_put "$key" "$value" "store"
  done < "$SS_CLI_STORE"
  return 0
}

ss_display_value() {
  local key="$1" value
  value="$(ss_get "$key")"
  if ss_is_secret "$key"; then
    if [[ -n "$value" ]]; then
      printf '********'
    else
      printf '(vacío)'
    fi
    return 0
  fi
  if [[ -z "$value" ]]; then
    printf '(vacío)'
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
  [[ -n "$line_cli" ]] && printf 'Desde CLI:               %s\n' "${line_cli# }"
  [[ -n "$line_env" ]] && printf 'Desde entorno:           %s\n' "${line_env# }"
  [[ -n "$line_store" ]] && printf 'Guardados (.shotstopper):%s\n' "$line_store"
  if [[ -n "$missing" ]]; then
    printf 'Faltan:                  %s\n' "$(printf '%s' "$missing" | tr ' ' ',' | sed 's/,/, /g')"
  fi
  return 0
}

ss_can_prompt() {
  [[ -z "${SHOTSTOPPER_NONINTERACTIVE:-}" ]] &&
    [[ -r /dev/tty ]] && [[ -w /dev/tty ]]
}

ss_prompt_text() {
  case "$1" in
    port) printf 'Puerto serial' ;;
    arch) printf 'Arquitectura (n8r4 o n16r8)' ;;
    speed) printf 'Velocidad del monitor' ;;
    host) printf 'Dirección del controlador (IP o nombre)' ;;
    token) printf 'Token OTA = contraseña del punto de acceso' ;;
    flags) printf 'Flags extra de compilación' ;;
    build_dir) printf 'Carpeta de compilación' ;;
    output_dir) printf 'Carpeta de reportes' ;;
    *) printf '%s' "$1" ;;
  esac
}

# Suggestion shown in [brackets] and used when the user presses Enter.
ss_prompt_suggestion() {
  local detected arch_s
  case "$1" in
    port)
      if declare -f shotstopper_detect_ports >/dev/null 2>&1; then
        detected="$(shotstopper_detect_ports | { IFS= read -r first || true; printf '%s' "${first:-}"; })"
        if [[ -n "$detected" ]]; then
          printf '%s' "$detected"
          return 0
        fi
      fi
      printf '%s' '/dev/cu.usbmodem2101'
      ;;
    arch) printf 'n16r8' ;;
    speed) printf '115200' ;;
    host) printf '192.168.4.1' ;;
    token) printf 'Micra1234' ;;
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
          printf 'Puertos USB detectados: %s\n' "${detected% }" > /dev/tty
        else
          printf 'No se detectó ningún /dev/cu.usbmodem<número> conectado.\n' > /dev/tty
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
          ss_cli_die "Velocidad inválida: '$value' (se esperan solo dígitos)."
          return 1
          ;;
      esac
      ;;
    host)
      if [[ -z "$value" ]]; then
        ss_cli_die "El parámetro --host no puede quedar vacío."
        return 1
      fi
      case "$value" in
        *[[:space:]]*)
          ss_cli_die "Host inválido: '$value' (no puede contener espacios)."
          return 1
          ;;
      esac
      ;;
    port|token|build_dir|output_dir)
      if [[ -z "$value" ]]; then
        ss_cli_die "El parámetro --$(printf '%s' "$key" | tr '_' '-') no puede quedar vacío."
        return 1
      fi
      ;;
  esac
  return 0
}

# ss_cli_resolve "<required keys>" ["<optional keys to ask once>"]
# Loads env + store, prints the summary, prompts for what is missing and
# validates the result. Fails with exit code 2 when it cannot prompt.
ss_cli_resolve() {
  local required="$1" optional="${2:-}"
  local wanted="$required $optional"
  local key missing=""

  ss_cli_apply_env
  ss_cli_load_store

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
        printf 'Lectura interrumpida.\n' >&2
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
      printf '\nNo hay terminal interactiva para pedir: %s\n' "$missing" >&2
      printf 'Pásalos como flags o guárdalos en %s.\n' "$SS_CLI_STORE" >&2
      case " $missing " in
        *' host '*)
          printf 'La IP aparece en Admin/Diagnostic del Web UI; en SoftAP es 192.168.4.1.\n' >&2
          ;;
      esac
      return 2
    fi
    for key in $missing; do
      ss_prompt_one "$key" || {
        printf 'Lectura interrumpida.\n' >&2
        return 2
      }
    done
  fi

  for key in $required; do
    ss_validate_key "$key" || return 2
  done
  return 0
}

ss_cli_save() {
  local key value tmp
  tmp="${TMPDIR:-/tmp}/shotstopper.XXXXXX"
  tmp="$(mktemp "$tmp")" || return 0
  chmod 600 "$tmp" 2>/dev/null || true
  {
    printf '# Valores recordados por los scripts de Shot Stopper.\n'
    printf '# Archivo local, ignorado por git. Contiene el token OTA.\n'
    for key in $SS_CLI_KEYS; do
      ss_is_transient "$key" && continue
      ss_is_set "$key" || continue
      value="$(ss_get "$key")"
      # A newline in a stored value would split into a second "key=value" line
      # on the next load and could clobber another key, including the token.
      case "$value" in
        *$'\n'*) continue ;;
      esac
      printf '%s=%s\n' "$key" "$value"
    done
  } >> "$tmp"
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
      # token is deliberately absent: a command line is world-readable through
      # `ps` for as long as the child runs, which for an OTA is minutes. Use
      # ss_cli_export_secrets instead.
      flags) SS_CLI_FORWARD+=(--flags "$(ss_get flags)") ;;
      build_dir) SS_CLI_FORWARD+=(--build-dir "$(ss_get build_dir)") ;;
      output_dir) SS_CLI_FORWARD+=(--output-dir "$(ss_get output_dir)") ;;
    esac
  done
}

# Hands secrets to a child process through the environment, which other users
# cannot read, rather than through argv, which they can.
ss_cli_export_secrets() {
  if ss_is_set token; then
    SHOTSTOPPER_OTA_TOKEN="$(ss_get token)"
    export SHOTSTOPPER_OTA_TOKEN
  fi
}

ss_cli_reset
