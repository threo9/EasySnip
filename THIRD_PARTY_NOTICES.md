# Third-party notices

## Qt 5.14.2

EasySnip dynamically links the Qt Core, GUI, Widgets, Network, Concurrent, PrintSupport and Test modules supplied by the configured Qt 5.14.2 SDK. The deployment script copies the SDK's license files into the package. Qt source and applicable terms are available from the Qt archive.

[Qt 5.14.2 source archive](https://download.qt.io/archive/qt/5.14/5.14.2/)

## ZXing-C++ 1.4.0

Source was obtained from the upstream v1.4.0 release archive. The vendored `core/src` files retain their upstream copyright and SPDX identifiers. `LICENSE`, `LICENSE.ZXing`, `NOTICE`, author information and the upstream Qt-related license files are retained in `third_party/zxing`.

[ZXing-C++ v1.4.0 upstream source](https://github.com/zxing-cpp/zxing-cpp/tree/v1.4.0)

The decoder and encoder are compiled into EasySnip. Encoders are used by the built-in deterministic barcode regression tests.

## MinGW runtime

The portable package contains the GCC/libstdc++/winpthreads runtime DLLs required by the selected MinGW 7.3 toolchain. Consult the GCC Runtime Library Exception and the MinGW-w64 license texts supplied with that toolchain.

## Windows Runtime ABI references

The small interface declarations in `src/share.cpp` follow the public Microsoft Windows SDK ABI for DataTransfer and Storage.Streams. They enable the existing MinGW toolchain to call the Windows Runtime provided by Windows 11.

[Microsoft Windows metadata source](https://github.com/microsoft/win32metadata), [desktop share UI integration](https://learn.microsoft.com/en-us/windows/apps/develop/ui/display-ui-objects)
