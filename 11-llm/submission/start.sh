#!/usr/bin/env bash
set -euo pipefail

: "${HELLOHPC_MODEL_PATH:?HELLOHPC_MODEL_PATH is set by the platform}"
: "${HELLOHPC_SERVICE_BASE_URL:?HELLOHPC_SERVICE_BASE_URL is set by the platform}"
: "${HELLOHPC_MODEL_ID:?HELLOHPC_MODEL_ID is set by the platform}"
: "${HELLOHPC_CASE_ID:?HELLOHPC_CASE_ID is set by the platform}"
: "${HELLOHPC_STAGE:?HELLOHPC_STAGE is set by the platform}"

# Replace this block with a framework-specific background launch. The service
# must keep running after this script exits and bind only to the supplied
# loopback endpoint. Redirect stdout/stderr to HELLOHPC_SERVICE_LOG.
printf '%s\n' "start.sh is still the starter template" >&2
exit 2
