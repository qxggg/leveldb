# CMake generated Testfile for 
# Source directory: /Users/xingeqi/CLionProjects/leveldb
# Build directory: /Users/xingeqi/CLionProjects/leveldb/cmake-build-debug
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(leveldb_tests "/Users/xingeqi/CLionProjects/leveldb/cmake-build-debug/leveldb_tests")
set_tests_properties(leveldb_tests PROPERTIES  _BACKTRACE_TRIPLES "/Users/xingeqi/CLionProjects/leveldb/CMakeLists.txt;368;add_test;/Users/xingeqi/CLionProjects/leveldb/CMakeLists.txt;0;")
add_test(c_test "/Users/xingeqi/CLionProjects/leveldb/cmake-build-debug/c_test")
set_tests_properties(c_test PROPERTIES  _BACKTRACE_TRIPLES "/Users/xingeqi/CLionProjects/leveldb/CMakeLists.txt;394;add_test;/Users/xingeqi/CLionProjects/leveldb/CMakeLists.txt;397;leveldb_test;/Users/xingeqi/CLionProjects/leveldb/CMakeLists.txt;0;")
add_test(env_posix_test "/Users/xingeqi/CLionProjects/leveldb/cmake-build-debug/env_posix_test")
set_tests_properties(env_posix_test PROPERTIES  _BACKTRACE_TRIPLES "/Users/xingeqi/CLionProjects/leveldb/CMakeLists.txt;394;add_test;/Users/xingeqi/CLionProjects/leveldb/CMakeLists.txt;405;leveldb_test;/Users/xingeqi/CLionProjects/leveldb/CMakeLists.txt;0;")
subdirs("third_party/googletest")
subdirs("third_party/benchmark")
