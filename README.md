# OnePlus Nord (avicii) NeverSettle Kernel Source

<img src="https://raw.githubusercontent.com/sreeshankark/sreeshankark/main/images/ns_logo.png" width=100 height=100 align="left" />  


<img src="https://raw.githubusercontent.com/sreeshankark/sreeshankark/main/images/oneplus_logo.png" align="right"/>

OnePlus Nord (codename:- avicii) is the first upper mid-range non-flagship smartphone from OnePlus Technology Co., Ltd. released in the year 2020.

The phone is available in 2 variants based on region: AC2001 (India) & AC2003 (Europe & Global)
> Task :ksu-module:buildCMakeRelWithDebInfo[armeabi-v7a]
C/C++: ninja: Entering directory `/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/armeabi-v7a'
C/C++: /home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/armeabi-v7a/_deps/soundtouch-src/source/SoundTouch/PeakFinder.cpp:139:11: warning: variable 'peaklevel' set but not used [-Wunused-but-set-variable]
C/C++:   139 |     float peaklevel;
C/C++:       |           ^
C/C++: 1 warning generated.
C/C++: /home/niftox/Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ --target=armv7-none-linux-androideabi31 --sysroot=/home/niftox/Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/sysroot  -I/home/niftox/avcrt/ksu-module/src/main/cpp/src -I/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/armeabi-v7a/_deps/soundtouch-src/include -I/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/armeabi-v7a/_deps/nlohmann_json-src/include -g -DANDROID -fdata-sections -ffunction-sections -funwind-tables -fstack-protector-strong -no-canonical-prefixes -D_FORTIFY_SOURCE=2 -march=armv7-a -mthumb -Wformat -Werror=format-security  -std=c++17 -fexceptions -frtti -O3 -fvisibility=hidden -O2 -g -DNDEBUG -fPIE -Wall -Wextra -Wno-unused-parameter -O2 -std=c++17 -MD -MT CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o -MF CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o.d -o CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o -c /home/niftox/avcrt/ksu-module/src/main/cpp/daemon/avcrtd.cpp
C/C++: /home/niftox/avcrt/ksu-module/src/main/cpp/daemon/avcrtd.cpp:18:10: fatal error: 'tinyalsa/asoundlib.h' file not found
C/C++:    18 | #include <tinyalsa/asoundlib.h>
C/C++:       |          ^~~~~~~~~~~~~~~~~~~~~~
C/C++: 1 error generated.

> Task :ksu-module:buildCMakeRelWithDebInfo[armeabi-v7a] FAILED

> Task :ksu-module:buildCMakeRelWithDebInfo[arm64-v8a]
C/C++: ninja: Entering directory `/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/arm64-v8a'
C/C++: /home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/arm64-v8a/_deps/soundtouch-src/source/SoundTouch/PeakFinder.cpp:139:11: warning: variable 'peaklevel' set but not used [-Wunused-but-set-variable]
C/C++:   139 |     float peaklevel;
C/C++:       |           ^
C/C++: 1 warning generated.
C/C++: /home/niftox/Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ --target=aarch64-none-linux-android31 --sysroot=/home/niftox/Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/sysroot  -I/home/niftox/avcrt/ksu-module/src/main/cpp/src -I/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/arm64-v8a/_deps/soundtouch-src/include -I/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/arm64-v8a/_deps/nlohmann_json-src/include -g -DANDROID -fdata-sections -ffunction-sections -funwind-tables -fstack-protector-strong -no-canonical-prefixes -D_FORTIFY_SOURCE=2 -Wformat -Werror=format-security  -std=c++17 -fexceptions -frtti -O3 -fvisibility=hidden -O2 -g -DNDEBUG -fPIE -Wall -Wextra -Wno-unused-parameter -O2 -std=c++17 -MD -MT CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o -MF CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o.d -o CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o -c /home/niftox/avcrt/ksu-module/src/main/cpp/daemon/avcrtd.cpp
C/C++: /home/niftox/avcrt/ksu-module/src/main/cpp/daemon/avcrtd.cpp:18:10: fatal error: 'tinyalsa/asoundlib.h' file not found
C/C++:    18 | #include <tinyalsa/asoundlib.h>
C/C++:       |          ^~~~~~~~~~~~~~~~~~~~~~
C/C++: 1 error generated.

> Task :ksu-module:buildCMakeRelWithDebInfo[arm64-v8a] FAILED

FAILURE: Build completed with 2 failures.

1: Task failed with an exception.
-----------
* What went wrong:
Execution failed for task ':ksu-module:buildCMakeRelWithDebInfo[armeabi-v7a]'.
> com.android.ide.common.process.ProcessException: ninja: Entering directory `/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/armeabi-v7a'
  [1/25] Building CXX object CMakeFiles/avcrtd.dir/src/avcrt_log.cpp.o
  [2/25] Building CXX object CMakeFiles/avcrtfx.dir/src/avcrt_log.cpp.o
  [3/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/PeakFinder.cpp.o
  /home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/armeabi-v7a/_deps/soundtouch-src/source/SoundTouch/PeakFinder.cpp:139:11: warning: variable 'peaklevel' set but not used [-Wunused-but-set-variable]
    139 |     float peaklevel;
        |           ^
  1 warning generated.
  [4/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/cpu_detect_x86.cpp.o
  [5/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/AAFilter.cpp.o
  [6/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/RateTransposer.cpp.o
  [7/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/FIFOSampleBuffer.cpp.o
  [8/25] Building CXX object CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o
  FAILED: CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o 
  /home/niftox/Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ --target=armv7-none-linux-androideabi31 --sysroot=/home/niftox/Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/sysroot  -I/home/niftox/avcrt/ksu-module/src/main/cpp/src -I/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/armeabi-v7a/_deps/soundtouch-src/include -I/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/armeabi-v7a/_deps/nlohmann_json-src/include -g -DANDROID -fdata-sections -ffunction-sections -funwind-tables -fstack-protector-strong -no-canonical-prefixes -D_FORTIFY_SOURCE=2 -march=armv7-a -mthumb -Wformat -Werror=format-security  -std=c++17 -fexceptions -frtti -O3 -fvisibility=hidden -O2 -g -DNDEBUG -fPIE -Wall -Wextra -Wno-unused-parameter -O2 -std=c++17 -MD -MT CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o -MF CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o.d -o CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o -c /home/niftox/avcrt/ksu-module/src/main/cpp/daemon/avcrtd.cpp
  /home/niftox/avcrt/ksu-module/src/main/cpp/daemon/avcrtd.cpp:18:10: fatal error: 'tinyalsa/asoundlib.h' file not found
     18 | #include <tinyalsa/asoundlib.h>
        |          ^~~~~~~~~~~~~~~~~~~~~~
  1 error generated.
  [9/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/InterpolateShannon.cpp.o
  [10/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/SoundTouch.cpp.o
  [11/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/mmx_optimized.cpp.o
  [12/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/FIRFilter.cpp.o
  [13/25] Building CXX object CMakeFiles/avcrtfx.dir/src/avcrt_effect.cpp.o
  [14/25] Building CXX object CMakeFiles/avcrtd.dir/src/avcrt_engine.cpp.o
  [15/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/sse_optimized.cpp.o
  [16/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/InterpolateCubic.cpp.o
  [17/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/TDStretch.cpp.o
  [18/25] Building CXX object CMakeFiles/avcrtfx.dir/src/avcrt_engine.cpp.o
  [19/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/InterpolateLinear.cpp.o
  [20/25] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/BPMDetect.cpp.o
  [21/25] Building CXX object CMakeFiles/avcrtfx.dir/src/avcrt_config.cpp.o
  [22/25] Building CXX object CMakeFiles/avcrtd.dir/src/avcrt_config.cpp.o
  ninja: build stopped: subcommand failed.
  
  C++ build system [build] failed while executing:
      /home/niftox/Android/Sdk/cmake/3.22.1/bin/ninja \
        -C \
        /home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/armeabi-v7a \
        avcrtd \
        avcrtfx
    from /home/niftox/avcrt/ksu-module

* Try:
> Run with --stacktrace option to get the stack trace.
> Run with --info or --debug option to get more log output.
> Run with --scan to get full insights.
> Get more help at https://help.gradle.org.
==============================================================================

2: Task failed with an exception.
-----------
* What went wrong:
Execution failed for task ':ksu-module:buildCMakeRelWithDebInfo[arm64-v8a]'.
> com.android.ide.common.process.ProcessException: ninja: Entering directory `/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/arm64-v8a'
  [1/29] Building CXX object CMakeFiles/avcrtd.dir/src/avcrt_log.cpp.o
  [2/29] Building CXX object CMakeFiles/zygisk_avcrt.dir/src/avcrt_log.cpp.o
  [3/29] Building CXX object CMakeFiles/avcrtfx.dir/src/avcrt_log.cpp.o
  [4/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/PeakFinder.cpp.o
  /home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/arm64-v8a/_deps/soundtouch-src/source/SoundTouch/PeakFinder.cpp:139:11: warning: variable 'peaklevel' set but not used [-Wunused-but-set-variable]
    139 |     float peaklevel;
        |           ^
  1 warning generated.
  [5/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/mmx_optimized.cpp.o
  [6/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/InterpolateLinear.cpp.o
  [7/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/FIRFilter.cpp.o
  [8/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/InterpolateShannon.cpp.o
  [9/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/cpu_detect_x86.cpp.o
  [10/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/InterpolateCubic.cpp.o
  [11/29] Building CXX object CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o
  FAILED: CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o 
  /home/niftox/Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ --target=aarch64-none-linux-android31 --sysroot=/home/niftox/Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/sysroot  -I/home/niftox/avcrt/ksu-module/src/main/cpp/src -I/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/arm64-v8a/_deps/soundtouch-src/include -I/home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/arm64-v8a/_deps/nlohmann_json-src/include -g -DANDROID -fdata-sections -ffunction-sections -funwind-tables -fstack-protector-strong -no-canonical-prefixes -D_FORTIFY_SOURCE=2 -Wformat -Werror=format-security  -std=c++17 -fexceptions -frtti -O3 -fvisibility=hidden -O2 -g -DNDEBUG -fPIE -Wall -Wextra -Wno-unused-parameter -O2 -std=c++17 -MD -MT CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o -MF CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o.d -o CMakeFiles/avcrtd.dir/daemon/avcrtd.cpp.o -c /home/niftox/avcrt/ksu-module/src/main/cpp/daemon/avcrtd.cpp
  /home/niftox/avcrt/ksu-module/src/main/cpp/daemon/avcrtd.cpp:18:10: fatal error: 'tinyalsa/asoundlib.h' file not found
     18 | #include <tinyalsa/asoundlib.h>
        |          ^~~~~~~~~~~~~~~~~~~~~~
  1 error generated.
  [12/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/AAFilter.cpp.o
  [13/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/FIFOSampleBuffer.cpp.o
  [14/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/RateTransposer.cpp.o
  [15/29] Building CXX object CMakeFiles/zygisk_avcrt.dir/zygisk/zygisk_avcrt.cpp.o
  [16/29] Building CXX object CMakeFiles/avcrtfx.dir/src/avcrt_effect.cpp.o
  [17/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/sse_optimized.cpp.o
  [18/29] Building CXX object CMakeFiles/avcrtd.dir/src/avcrt_engine.cpp.o
  [19/29] Building CXX object CMakeFiles/zygisk_avcrt.dir/src/avcrt_engine.cpp.o
  [20/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/SoundTouch.cpp.o
  [21/29] Building CXX object CMakeFiles/avcrtfx.dir/src/avcrt_engine.cpp.o
  [22/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/TDStretch.cpp.o
  [23/29] Building CXX object _deps/soundtouch-build/CMakeFiles/SoundTouch.dir/source/SoundTouch/BPMDetect.cpp.o
  [24/29] Building CXX object CMakeFiles/avcrtfx.dir/src/avcrt_config.cpp.o
  [25/29] Building CXX object CMakeFiles/avcrtd.dir/src/avcrt_config.cpp.o
  ninja: build stopped: subcommand failed.
  
  C++ build system [build] failed while executing:
      /home/niftox/Android/Sdk/cmake/3.22.1/bin/ninja \
        -C \
        /home/niftox/avcrt/ksu-module/.cxx/RelWithDebInfo/h3l6c6h2/arm64-v8a \
        avcrtd \
        avcrtfx \
        zygisk_avcrt
    from /home/niftox/avcrt/ksu-module

* Try:
> Run with --stacktrace option to get the stack trace.
> Run with --info or --debug option to get more log output.
> Run with --scan to get full insights.
> Get more help at https://help.gradle.org.
==============================================================================

BUILD FAILED in 11s
270 actionable tasks: 118 executed, 152 from cache
Configuration cache entry stored.
<p align="center">
<img src="https://raw.githubusercontent.com/sreeshankark/sreeshankark/main/images/oneplus_avicii.png" width=500 height=500 />
</p>

| Features | Specification |
| ------------- | ------------- |
| Chipset |  Qualcomm Snapdragon 765G 5G (SM7250-AB) |
| CPU  | Octa-core (1x2.4GHz Single-Core Kryo 475 Prime, 1x2.2GHz Single-Core Kryo 475 Gold & 6x1.8GHz Hexa-Core Kryo 475 Silver)  |
| GPU  | Qualcomm Adreno 620 (625Mhz) |
| RAM  | 6GB/8GB/12GB (LPDDR4X) |
| Shipped OS | Android 10 (Oxygen OS 10) |
| Storage | 64GB/128GB/256GB (UFS2.1) |
| SIM | Hybrid Dual SIM (Nano-SIM, dual stand-by) |
| MicroSD | Not Supported |
| Battery | 4115 mAh (non-removable), Warp Charge 30T fast charging (5V/6A) support |
| Dimensions | 158.3x73.3x8.2 (mm) |
| Weight | 184g |
| Display | 2400x1080, 408ppi, 20:9, 90Hz, Fluid AMOLED |
| Rear Camera (Main) | 48MP (Sony IMX586), 0.8 µm/48M; 1.6 µm (4 in 1)/12M, f/1.75, OIS, EIS, Multi Autofocus (PDAF+CAF) |
| Rear Camera (Ultra wide angle) | 8MP, f/2.25, 119° (FOV) |
| Rear Camera (Depth) | 5MP, f/2.4 |
| Rear Camera (Macro) | 2MP, f/2.4 |
| Flash | Dual LED Flash |
| Front Camera (Main) | 32MP (Sony IMX616), 0.8 µm/32M; 1.6 µm (4 in 1)/8M, f/2.45, EIS, Fixed focus |
| Front Camera (Ultra Wide Angle) | 8MP, f/2.45, 105° (FOV) |
| Fingerprint | Goodix (In-display, optical) |
| Sensors | Accelerometer, Electronic compass, Gyroscope, Ambient light sensor, Proximity sensor, Sensor Core |
| Extras | Bluetooth 5.1 (support aptX & aptX HD & LDAC & AAC), NFC enabled |

### ⚠️ This kernel is not allowed to be included in ROM builds without the [developer's](https://t.me/sksanju0910) approval.

## Kernel Details

| Parameter | Value |
| ---------- | ---------- |
| NS version | 3.4 |
| Linux Kernel version | 4.19.325 |
| KernelSU-Next version | 1.1.1 |
| KernelSU-Next version code | 12851 |
| SUSFS version | 1.5.9 |

## Kernel Features
(1) Supports 5V-6A Fast Charging 

(2) Supports OPlusExtras
- Touchboost
- KCAL
- HBM
- FSync
- Adrenoboost
- Force Fast Charge

(3) Compiled using Clang-LLVM 20.0.0

(4) Supports Boeffla Wakelock blocker

(5) Using power efficient workqueues

(6) Supports Schedhorizon CPU governor

(7) Global timeout for wakelock

(8) Supports Wireguard VPN

(9) Supports KernelSU-Next & SUSFS

(10) Supports WPA3 SAE WiFi authentication

(11) Supports all NetHunter Features

(12) Support GSM voice change ( mute mic )

(13) Support QCACLD-3.0 Packet Injection ( if not, apply a different patch )

## Credits
- [Avicii kerne source (Kimocoder)](https://github.com/kimocoder/android_kernel_oneplus_avicii): Good job bro
- [Avicii kerne source (Kimocoder)](https://github.com/kimocoder/android_kernel_oneplus_avicii): Good job bro
- [Linux Kernel Organisation](https://kernel.org): For the development of base kernel
- [weishu](https://github.com/tiann): For the development of KernelSU
- [Rifat Azad](https://github.com/rifsxd): For the development of KernelSU-Next
- [simonpunk](https://gitlab.com/simonpunk): For the development of SUSFS4KSU
- [osm0sis](https://github.com/osm0sis): For the development of AnyKernel3
