# Vendored upstream ZXing-C++ v1.4.0, Apache-2.0. See zxing/NOTICE.
# Writers are included for deterministic QR/barcode round-trip self-tests.
DEFINES += ZXING_BUILD_READERS ZXING_BUILD_WRITERS
SOURCES += $$files($$PWD/zxing/core/src/*.cpp, true)
HEADERS += $$files($$PWD/zxing/core/src/*.h, true)
