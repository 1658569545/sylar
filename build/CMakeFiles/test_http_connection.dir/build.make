# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.22

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Produce verbose output by default.
VERBOSE = 1

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/xx/code/C++/sylar-master

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/xx/code/C++/sylar-master/build

# Include any dependencies generated for this target.
include CMakeFiles/test_http_connection.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/test_http_connection.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/test_http_connection.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/test_http_connection.dir/flags.make

CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.o: CMakeFiles/test_http_connection.dir/flags.make
CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.o: ../tests/test_http_connection.cc
CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.o: CMakeFiles/test_http_connection.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/xx/code/C++/sylar-master/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.o"
	/usr/bin/g++ $(CXX_DEFINES) -D__FILE__=\"tests/test_http_connection.cc\" $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.o -MF CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.o.d -o CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.o -c /home/xx/code/C++/sylar-master/tests/test_http_connection.cc

CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.i"
	/usr/bin/g++ $(CXX_DEFINES) -D__FILE__=\"tests/test_http_connection.cc\" $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/xx/code/C++/sylar-master/tests/test_http_connection.cc > CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.i

CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.s"
	/usr/bin/g++ $(CXX_DEFINES) -D__FILE__=\"tests/test_http_connection.cc\" $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/xx/code/C++/sylar-master/tests/test_http_connection.cc -o CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.s

# Object files for target test_http_connection
test_http_connection_OBJECTS = \
"CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.o"

# External object files for target test_http_connection
test_http_connection_EXTERNAL_OBJECTS =

../bin/test_http_connection: CMakeFiles/test_http_connection.dir/tests/test_http_connection.cc.o
../bin/test_http_connection: CMakeFiles/test_http_connection.dir/build.make
../bin/test_http_connection: ../lib/libsylar.so
../bin/test_http_connection: /usr/lib/x86_64-linux-gnu/libz.so
../bin/test_http_connection: /usr/lib/x86_64-linux-gnu/libssl.so
../bin/test_http_connection: /usr/lib/x86_64-linux-gnu/libcrypto.so
../bin/test_http_connection: /usr/lib/x86_64-linux-gnu/libprotobuf.so
../bin/test_http_connection: CMakeFiles/test_http_connection.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/xx/code/C++/sylar-master/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable ../bin/test_http_connection"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/test_http_connection.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/test_http_connection.dir/build: ../bin/test_http_connection
.PHONY : CMakeFiles/test_http_connection.dir/build

CMakeFiles/test_http_connection.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/test_http_connection.dir/cmake_clean.cmake
.PHONY : CMakeFiles/test_http_connection.dir/clean

CMakeFiles/test_http_connection.dir/depend:
	cd /home/xx/code/C++/sylar-master/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/xx/code/C++/sylar-master /home/xx/code/C++/sylar-master /home/xx/code/C++/sylar-master/build /home/xx/code/C++/sylar-master/build /home/xx/code/C++/sylar-master/build/CMakeFiles/test_http_connection.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/test_http_connection.dir/depend

