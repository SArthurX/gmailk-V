#!/bin/bash

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
echo "script_dir: ${script_dir}"

milkv_debug=0
milkv_chip="CV181X"
milkv_arch="riscv64"

host_tools_git="https://github.com/milkv-duo/host-tools.git"
host_tools=host-tools
sys_inc="include/system"
tdl_inc="include/tdl"

arch_cflags="-mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d"
arch_ldflags="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64"
toolchain_dir=${host_tools}/gcc/riscv64-linux-musl-x86_64
sys_lib="libs/system/musl_riscv64"
tdl_lib="libs/tdl/cv181x_riscv64"
export TOOLCHAIN_PREFIX=${toolchain_dir}/bin/riscv64-unknown-linux-musl-
if [ "${milkv_debug}" -eq 1 ]; then
	debug_cflags="-g -O0"
else
	debug_cflags="-O3 -DNDEBUG"
fi
export CC="${TOOLCHAIN_PREFIX}gcc"
export CFLAGS="${arch_cflags} ${debug_cflags} -I${sys_inc} -I${tdl_inc}"
export LDFLAGS="${arch_ldflags} -L${sys_lib} -L${tdl_lib}"
export CHIP="${milkv_chip}"
export COMMON_DIR="common"

function print_info()    { printf "\e[1;92m%s\e[0m\n" "$1"; }
print_info "Environment is ready."