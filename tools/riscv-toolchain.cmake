# RISC-V Toolchain Configuration for MilkV Duo
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

# Toolchain paths
set(TOOLCHAIN_PREFIX "${CMAKE_CURRENT_LIST_DIR}/host-tools/gcc/riscv64-linux-musl-x86_64")

# Compilers
set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}/bin/riscv64-unknown-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}/bin/riscv64-unknown-linux-musl-g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}/bin/riscv64-unknown-linux-musl-gcc")
set(CMAKE_AR           "${TOOLCHAIN_PREFIX}/bin/riscv64-unknown-linux-musl-ar")
set(CMAKE_LINKER       "${TOOLCHAIN_PREFIX}/bin/riscv64-unknown-linux-musl-ld")
set(CMAKE_NM           "${TOOLCHAIN_PREFIX}/bin/riscv64-unknown-linux-musl-nm")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}/bin/riscv64-unknown-linux-musl-objcopy")
set(CMAKE_OBJDUMP      "${TOOLCHAIN_PREFIX}/bin/riscv64-unknown-linux-musl-objdump")
set(CMAKE_STRIP        "${TOOLCHAIN_PREFIX}/bin/riscv64-unknown-linux-musl-strip")
set(CMAKE_RANLIB       "${TOOLCHAIN_PREFIX}/bin/riscv64-unknown-linux-musl-ranlib")

# Sysroot
set(CMAKE_SYSROOT "${TOOLCHAIN_PREFIX}/sysroot")
set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_PREFIX}/sysroot")

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Architecture-specific flags
set(ARCH_FLAGS "-mcpu=c906fdv -march=rv64imafdcv0p7xthead -mcmodel=medany -mabi=lp64d")
set(CMAKE_C_FLAGS_INIT "${ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARCH_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${ARCH_FLAGS}")

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS_INIT "-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64")

