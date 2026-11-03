#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
${CXX:-g++} -std=c++17 -Wall -Wextra -Wpedantic -g -fsanitize=${SANITIZERS:-address,undefined} -fno-omit-frame-pointer -Ilib/InhCore/src lib/InhCore/src/Glagol.cpp test/core_test.cpp -o build/core_test
ASAN_OPTIONS=detect_leaks=1 ./build/core_test
${CXX:-g++} -std=c++17 -Ilib/InhCore/src lib/InhCore/src/Glagol.cpp tools/check_examples.cpp -o build/check_examples
./build/check_examples examples/*.inh
${CXX:-g++} -std=c++17 -Ilib/InhCore/src test/image_test.cpp -o build/image_test
./build/image_test assets/default-sleep.ginh
${CXX:-g++} -std=c++17 -Ilib/InhCore/src lib/InhCore/src/Glagol.cpp test/memory_profile.cpp -o build/memory_profile
./build/memory_profile examples/01_geometry.inh
${CXX:-g++} -std=c++17 -Wall -Wextra -Wpedantic -fsanitize=${SANITIZERS:-address,undefined} -Ilib/InhCore/src test/battery_test.cpp -o build/battery_test
ASAN_OPTIONS=detect_leaks=1 ./build/battery_test
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Wpedantic -fsanitize=${SANITIZERS:-address,undefined} -Ilib/InhCore/src test/usb_host.cpp -o build/usb_host
# RSS must be measured without ASan's deliberate freed-block quarantine.
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Wpedantic -Ilib/InhCore/src test/usb_host.cpp -o build/usb_host_profile
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Wpedantic -fsanitize=${SANITIZERS:-address,undefined} -Ilib/InhCore/src test/book_check.cpp -o build/book_check
