#!/bin/bash
# Usage: ./switch_workload.sh r0
# Valid values: r0, r1, r2, r3

ROUND=$1
if [[ ! "$ROUND" =~ ^r[0-3]$ ]]; then
    echo "Usage: $0 <r0|r1|r2|r3>"
    exit 1
fi

SRC_BASE="/home/jz/workload"
DST_BASE="/home/jz/In-Switch-FS-Metadata/netfetch-private/mdtestcpp/workload"
SUFFIX="31981446_32000000_10_4_0.9_3.out"

echo "Switching to workload ${ROUND}..."

cp "${SRC_BASE}/workloads_${ROUND}/access_file_${SUFFIX}"       "${DST_BASE}/workloads/"
cp "${SRC_BASE}/caches_${ROUND}/freq_${SUFFIX}"                 "${DST_BASE}/caches/"
cp "${SRC_BASE}/caches_${ROUND}/bottleneck_file_16_${SUFFIX}"   "${DST_BASE}/caches/"
cp "${SRC_BASE}/caches_${ROUND}/bottleneck_file_128_${SUFFIX}"  "${DST_BASE}/caches/"

echo "Done."