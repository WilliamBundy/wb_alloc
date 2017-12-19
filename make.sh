# This file builds the regular and C++ versions of the tests.
# gcc -ansi -pedantic is as close as I can get to "true" C89/C90 standards

cc=gcc

echo wb_alloc_test.c
${cc} -x c -ansi -Wall -pedantic -Wno-format -Wno-unused-variable wb_alloc_test.c -o wb_alloc_test

echo wb_alloc_test_cpp.cpp
${cc} -x c++ --std=c++98 -Wall -Wno-unused-variable wb_alloc_test_cpp.cpp -o wb_alloc_test_cpp

echo ""


