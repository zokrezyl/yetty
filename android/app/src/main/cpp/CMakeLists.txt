# Android-specific CMakeLists.txt
# This file is used by Gradle's externalNativeBuild

cmake_minimum_required(VERSION 3.20)
project(yetty-android)

# Force Android build
set(YETTY_ANDROID ON CACHE BOOL "Building for Android" FORCE)

# Include the main yetty CMakeLists.txt
# The path goes up from android/app/src/main/cpp to the root
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ${CMAKE_CURRENT_BINARY_DIR}/yetty)
