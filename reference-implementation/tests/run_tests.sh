#!/bin/sh

THISDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

SIZES=(0 128 512 513 $((512*256)))
LOSSES=(0 10)
DELAYS=(0 5 10)
JITTER=(0 5)
ERRORS=(0 2 5)


function test_blackbox() {
    err_count=0
    test_count=0
    for s in "${SIZES[@]}"; do
        for l in "${LOSSES[@]}"; do
            for d in "${DELAYS[@]}"; do
                for j in "${JITTER[@]}"; do
                    for e in "${ERRORS[@]}"; do
                        test_count=$((test_count+1))
                        if ! INFILESIZ=$s LOSS=$l DELAY=$d JITTER=$j ERRRATE=$e "$THISDIR/exec_test.sh"; then
                            err_count=$((err_count+1))
                        fi
                    done
                done
            done
        done
    done
    echo "Black box fail rate: $err_count/$test_count"
}

function test_whitebox() {
    make
}

test_whitebox
test_blackbox
