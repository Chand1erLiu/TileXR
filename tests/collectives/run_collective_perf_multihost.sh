#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage:
  TILEXR_MULTIHOST_PEERS="rank,host,ip,device;rank,host,ip,device" \
  run_collective_perf_multihost.sh profile_dir bin_dir [extra tilexr_collective_perf args...]

Example:
  TILEXR_MULTIHOST_PEERS="0,root@141.62.24.62,141.62.24.62,0;1,root@141.62.24.70,141.62.24.70,0" \
  TILEXR_COMM_ID=141.62.24.62:10067 \
  bash run_collective_perf_multihost.sh /home/l00929943/TileXR/run/prof/collectives-2host \
    /home/l00929943/TileXR/build-profile-950/tests/collectives \
    --op allgather --min-bytes 4096 --max-bytes 4096 --iters 2 --warmup-iters 1 \
    --datatype int32 --check 0 --profile 1 --profile-sample-every 1 --profile-ai-prompt 1

Each peer entry is rank,ssh_target,host_ip,device_id. The first entry is used as rank0/server.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

profile_dir="${1:?profile_dir required}"
bin_dir="${2:?bin_dir required}"
shift 2

peers_spec="${TILEXR_MULTIHOST_PEERS:?TILEXR_MULTIHOST_PEERS is required}"
comm_id="${TILEXR_COMM_ID:-141.62.24.62:10067}"
timeout_sec="${TILEXR_COLLECTIVES_RUN_TIMEOUT_SEC:-600}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
helper="${script_dir}/tilexr_collective_profile_report.py"
rank_size=0
warmup_iters=5
measured_iters=20
profile_sample_every=1
profile_ai_prompt=0

is_true_bool() {
  [[ "${1:-}" == "1" || "${1:-}" == "true" || "${1:-}" == "yes" ]]
}

parse_profile_args() {
  local args=("$@")
  local i
  for ((i = 0; i < ${#args[@]}; i++)); do
    case "${args[$i]}" in
      --warmup-iters)
        if (( i + 1 < ${#args[@]} )); then warmup_iters="${args[$((i + 1))]}"; fi
        ;;
      --iters)
        if (( i + 1 < ${#args[@]} )); then measured_iters="${args[$((i + 1))]}"; fi
        ;;
      --profile-sample-every)
        if (( i + 1 < ${#args[@]} )); then profile_sample_every="${args[$((i + 1))]}"; fi
        ;;
      --profile-ai-prompt)
        if (( i + 1 < ${#args[@]} )); then profile_ai_prompt="${args[$((i + 1))]}"; fi
        ;;
    esac
  done
}

parse_profile_args "$@"

IFS=';' read -r -a peers <<< "${peers_spec}"
rank_size="${#peers[@]}"
if (( rank_size < 2 )); then
  echo "ERROR: TILEXR_MULTIHOST_PEERS must contain at least two peers" >&2
  exit 1
fi

mkdir -p "${profile_dir}"

ssh_pids=()
logs=()
targets=()
ranks=()

cleanup() {
  local pid
  for pid in "${ssh_pids[@]:-}"; do
    kill "${pid}" 2>/dev/null || true
  done
}
trap cleanup INT TERM

copy_rank_profile() {
  local target="$1"
  local rank="$2"
  local dest="${profile_dir}/rank${rank}"
  mkdir -p "${dest}"

  if command -v rsync >/dev/null 2>&1 &&
     ssh -o BatchMode=yes "${target}" "command -v rsync >/dev/null 2>&1"; then
    rsync -a -e "ssh -o BatchMode=yes" "${target}:${profile_dir}/rank${rank}/" "${dest}/"
    return
  fi

  ssh -o BatchMode=yes "${target}" bash -s -- "${profile_dir}" "${rank}" <<'REMOTE' | tar -xf - -C "${dest}"
set -euo pipefail
profile_dir="$1"
rank="$2"
cd "${profile_dir}/rank${rank}"
tar -cf - .
REMOTE
}

launch_rank() {
  local rank="$1"
  local target="$2"
  local host_ip="$3"
  local device_id="$4"
  local host_label="${target#*@}"
  shift 4
  local log="${profile_dir}/multihost_rank${rank}.log"
  logs+=("${log}")
  targets+=("${target}")
  ranks+=("${rank}")
  if [[ -z "${host_label}" || "${host_label}" == "${target}" ]]; then
    host_label="${host_ip}"
  fi

  ssh -o BatchMode=yes "${target}" bash -s -- \
    "${rank_size}" "${rank}" "${device_id}" "${comm_id}" "${profile_dir}" "${bin_dir}" "${host_ip}" "${host_label}" "$@" >"${log}" 2>&1 <<'REMOTE' &
set -euo pipefail
rank_size="$1"
rank="$2"
device_id="$3"
comm_id="$4"
profile_dir="$5"
bin_dir="$6"
host_ip="$7"
host_label="$8"
shift 8

cd /home/l00929943/TileXR
set +u
source /root/anaconda3/etc/profile.d/conda.sh 2>/dev/null || true
conda activate pt311 2>/dev/null || true
source /usr/local/Ascend/cann/set_env.sh
set -u
export LD_LIBRARY_PATH=/usr/local/Ascend/driver/lib64/driver:$(pwd)/build-profile-950/src/collectives:$(pwd)/build-profile-950/src/comm:${LD_LIBRARY_PATH:-}
export ASCEND_PROCESS_LOG_PATH="${profile_dir}/plog/rank${rank}"
export ASCEND_GLOBAL_LOG_LEVEL="${ASCEND_GLOBAL_LOG_LEVEL:-3}"
mkdir -p "${ASCEND_PROCESS_LOG_PATH}"
export TILEXR_COMM_ID="${comm_id}"
export TILEXR_PROFILE_HOST="${host_label}"
export TILEXR_PROFILE_HOST_IP="${host_ip}"

"${bin_dir}/tilexr_collective_perf" \
  --rank-size "${rank_size}" \
  --rank "${rank}" \
  --device-id "${device_id}" \
  --comm-mode socket \
  --profile-dir "${profile_dir}" \
  "$@"
REMOTE
  ssh_pids+=("$!")
}

for peer in "${peers[@]}"; do
  IFS=',' read -r rank target host_ip device_id <<< "${peer}"
  if [[ -z "${rank:-}" || -z "${target:-}" || -z "${host_ip:-}" || -z "${device_id:-}" ]]; then
    echo "ERROR: invalid peer entry '${peer}', expected rank,target,ip,device" >&2
    exit 1
  fi
  launch_rank "${rank}" "${target}" "${host_ip}" "${device_id}" "$@"
done

sleep "${timeout_sec}" >/dev/null 2>&1 &
watchdog_pid="$!"

completed=0
while (( completed < rank_size )); do
  if wait -n; then
    if ! kill -0 "${watchdog_pid}" 2>/dev/null; then
      echo "ERROR: timed out after ${timeout_sec}s" >&2
      cleanup
      exit 124
    fi
    completed=$((completed + 1))
  else
    rc="$?"
    echo "ERROR: remote rank failed with ${rc}" >&2
    for log in "${logs[@]}"; do
      echo "===== ${log} =====" >&2
      tail -n 120 "${log}" >&2 || true
    done
    cleanup
    exit "${rc}"
  fi
done

kill "${watchdog_pid}" 2>/dev/null || true
wait "${watchdog_pid}" 2>/dev/null || true
trap - INT TERM

for i in "${!targets[@]}"; do
  target="${targets[$i]}"
  rank="${ranks[$i]}"
  copy_rank_profile "${target}" "${rank}"
done

prompt_args=()
if is_true_bool "${profile_ai_prompt}"; then
  prompt_args+=(--emit-ai-prompt)
fi

python3 "${helper}" "${profile_dir}" \
  --warmup-iters "${warmup_iters}" \
  --iters "${measured_iters}" \
  --profile-sample-every "${profile_sample_every}" \
  "${prompt_args[@]}"
