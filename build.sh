TC_DIR=$HOME/tc/
export ARCH=arm64
export SUBARCH=arm64
export CONFIG_FILE="avicii_defconfig debugfs.config"
export BRAND_SHOW_FLAG=oneplus
export CCACHE=$(command -v ccache)
export PATH="/home/niftiox/avcrt-dev/clang-r547379/bin:$PATH"
if [ -n "$CCACHE" ]; then
	export CC="$CCACHE clang"
else
	export CC="clang"
fi
export CLANG_TRIPLE="aarch64-linux-gnu-"
export CROSS_COMPILE="aarch64-linux-gnu-"
export CROSS_COMPILE_ARM32="arm-linux-gnueabi-"
export LLVM=1
export LLVM_IAS=1
export DTC_EXT=/bin/dtc
make -s ARCH=arm64 O=out $CONFIG_FILE -j$(nproc --all)
make ARCH=arm64 O=out -j$(nproc --all)
