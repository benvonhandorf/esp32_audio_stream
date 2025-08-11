# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/benvh/esp/v5.4/esp-idf/components/bootloader/subproject"
  "/home/benvh/projects/esp32_audio_stream/pdm-idf-test/build/bootloader"
  "/home/benvh/projects/esp32_audio_stream/pdm-idf-test/build/bootloader-prefix"
  "/home/benvh/projects/esp32_audio_stream/pdm-idf-test/build/bootloader-prefix/tmp"
  "/home/benvh/projects/esp32_audio_stream/pdm-idf-test/build/bootloader-prefix/src/bootloader-stamp"
  "/home/benvh/projects/esp32_audio_stream/pdm-idf-test/build/bootloader-prefix/src"
  "/home/benvh/projects/esp32_audio_stream/pdm-idf-test/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/benvh/projects/esp32_audio_stream/pdm-idf-test/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/benvh/projects/esp32_audio_stream/pdm-idf-test/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
