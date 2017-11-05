# This should just work on macOS, or anywhere with clang installed
# As far as I can tell, --std=c89 does nothing on clang, so I stick to 
# --std==c99 for this version.

cc=clang

echo wb_alloc_test.c
${cc} -x c --std=c99 -Wall -Wno-unused-variable wb_alloc_test.c -o wb_alloc_test

echo wb_alloc_test_cpp.cpp
${cc} -x c++ --std=c++11 -Wall -Wno-unused-variable wb_alloc_test_cpp.cpp -o wb_alloc_test_cpp

echo ""


