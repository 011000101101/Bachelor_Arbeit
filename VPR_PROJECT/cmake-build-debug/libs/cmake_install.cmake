# Install script for directory: /cygdrive/c/Users/01100/Desktop/Bachelor_Arbeit/VPR_PROJECT/libs

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/cygdrive/c/Users/01100/Desktop/Bachelor_Arbeit/VPR_PROJECT")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/cygdrive/c/Users/01100/Desktop/Bachelor_Arbeit/VPR_PROJECT/cmake-build-debug/libs/libarchfpga/cmake_install.cmake")
  include("/cygdrive/c/Users/01100/Desktop/Bachelor_Arbeit/VPR_PROJECT/cmake-build-debug/libs/libvtrutil/cmake_install.cmake")
  include("/cygdrive/c/Users/01100/Desktop/Bachelor_Arbeit/VPR_PROJECT/cmake-build-debug/libs/liblog/cmake_install.cmake")
  include("/cygdrive/c/Users/01100/Desktop/Bachelor_Arbeit/VPR_PROJECT/cmake-build-debug/libs/libpugiutil/cmake_install.cmake")
  include("/cygdrive/c/Users/01100/Desktop/Bachelor_Arbeit/VPR_PROJECT/cmake-build-debug/libs/libeasygl/cmake_install.cmake")
  include("/cygdrive/c/Users/01100/Desktop/Bachelor_Arbeit/VPR_PROJECT/cmake-build-debug/libs/librtlnumber/cmake_install.cmake")
  include("/cygdrive/c/Users/01100/Desktop/Bachelor_Arbeit/VPR_PROJECT/cmake-build-debug/libs/EXTERNAL/cmake_install.cmake")

endif()

