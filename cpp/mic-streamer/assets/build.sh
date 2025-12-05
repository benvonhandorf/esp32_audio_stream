#!/bin/bash

## Convert raw audio data to C header files

RAW_FILES=(*.raw)

for raw in "${RAW_FILES[@]}"; do
    base_name=${raw%.*}

    echo "Converting $raw"

    eval "xxd -i $raw > $base_name.h"
done


