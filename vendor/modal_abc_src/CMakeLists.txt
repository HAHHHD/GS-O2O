cmake_minimum_required(VERSION 3.5.0)


project(abc)


include(CMakeParseArguments)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
# Generate compilation database compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# Default c++ standard used unless otherwise specified in target_compile_features.
set(CMAKE_CXX_STANDARD 17 CACHE STRING "the C++ standard to use for this project")
set(CMAKE_CXX_STANDARD_REQUIRED ON)




########## for RePlace #########

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dcimg_use_jpeg=1 -O3 -g -ggdb")
set(CMAKE_CXX_FLAGS_DEBUG "-g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set(THREADS_PREFER_PTHREAD_FLAG ON)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

set(REPLACE_HOME ${PROJECT_SOURCE_DIR} )
set(DEFLIB_HOME
${PROJECT_SOURCE_DIR}/src/replace/module/def
)

set(LEFLIB_HOME
  ${PROJECT_SOURCE_DIR}/src/replace/module/lef
)
set(FLUTE_HOME
  ${PROJECT_SOURCE_DIR}/src/replace/module/flute
)
set(OPENSTA_HOME
  ${PROJECT_SOURCE_DIR}/src/replace/module/OpenSTA
)
set(CIMG_HOME
  ${PROJECT_SOURCE_DIR}/src/replace/module/CImg
)
set(EIGEN_HOME
  ${PROJECT_SOURCE_DIR}/src/replace/module/eigen-git-mirror 
)

add_subdirectory(${DEFLIB_HOME})
add_subdirectory(${LEFLIB_HOME})
add_subdirectory(${FLUTE_HOME})
add_subdirectory(${OPENSTA_HOME})

set(ENV{TYPE_CALL} library)




## configure settings
find_package(GoogleHash)
find_package(TCL REQUIRED)

find_package(ZLIB REQUIRED)
find_package(SWIG REQUIRED)
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# for cimg
find_package(X11 REQUIRED)
find_package(JPEG REQUIRED)

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

# for multi threads
find_package(OpenMP REQUIRED)


############################################################
# include directories 
############################################################
include_directories(
  ${X11_INCLUDE_DIR}
  ${Boost_INCLUDE_DIR}

  ${EIGEN_HOME}
  ${CIMG_HOME}

  ${DEFLIB_HOME}/def
  ${DEFLIB_HOME}/defzlib
  ${DEFLIB_HOME}/cdef
  ${DEFLIB_HOME}/cdefzlib

  ${LEFLIB_HOME}/lef
  ${LEFLIB_HOME}/lefzlib
  ${LEFLIB_HOME}/clef
  ${LEFLIB_HOME}/clefzlib

  ${FLUTE_HOME}/src
 
  ${OPENSTA_HOME}/app
  ${OPENSTA_HOME}/dcalc
  ${OPENSTA_HOME}/graph
  ${OPENSTA_HOME}/liberty
  ${OPENSTA_HOME}/network
  ${OPENSTA_HOME}/parasitics
  ${OPENSTA_HOME}/sdc
  ${OPENSTA_HOME}/sdf
  ${OPENSTA_HOME}/search
  ${OPENSTA_HOME}/tcl
  ${OPENSTA_HOME}/util
  ${OPENSTA_HOME}/verilog

)

############################################################
# Create a library 
############################################################
# add_library( replacelib  SHARED
#   ${REPLACE_SRC}
# )

# set_target_properties( replacelib 
#   PROPERTIES OUTPUT_NAME "replace"
# )

###########################################################
# Create an executable
###########################################################
# add_custom_command(OUTPUT ${REPLACE_HOME}/src/replace_wrap.cpp
#   COMMAND ${SWIG_EXECUTABLE} -tcl8 -c++ -o ${REPLACE_HOME}/src/replace_wrap.cpp ${REPLACE_HOME}/src/replace.i   
#   WORKING_DIRECTORY ${REPLACE_HOME}
#   DEPENDS ${REPLACE_SWIG_FILES} src/replace_external.h src/replace_external.cpp
# )

# add_executable( replace
#   ${REPLACE_SRC}
# )

# add_dependencies( replace sta )
# add_dependencies( replace flute )
# add_dependencies( replace def )
# add_dependencies( replace lef )

# #add_dependencies( replacelib sta )
# #add_dependencies( replacelib flute )
# #add_dependencies( replacelib def )
# #add_dependencies( replacelib lef )

# target_link_libraries( replace
#   PUBLIC
#   OpenSTA
#   flute
#   def
#   defzlib
#   cdef
#   cdefzlib
#   lef
#   lefzlib
#   clef
#   clefzlib

#   ${ZLIB_LIBRARIES}
#   ${X11_LIBRARIES}
#   ${JPEG_LIBRARIES}
#   ${TCL_LIB}

#   ${OpenMP_CXX_LIBRARIES}
#   Threads::Threads
#   )

########## for RePlace #########


function(addprefix var prefix)
    foreach( s ${ARGN} )
        list(APPEND tmp "-I${s}")
    endforeach()
    set(${var} ${tmp} PARENT_SCOPE)
endfunction()

# filter out flags that are not appropriate for the compiler being used
function(target_compile_options_filtered target visibility)
    foreach( flag ${ARGN} )
        if( flag MATCHES "^-D.*" )
            target_compile_options( ${target} ${visibility} ${flag} )
        else()
            check_c_compiler_flag( ${flag} C_COMPILER_SUPPORTS__${flag} )
            if( C_COMPILER_SUPPORTS__${flag} )
                target_compile_options( ${target} ${visibility} $<$<COMPILE_LANGUAGE:C>:${flag}> )
            endif()

            check_cxx_compiler_flag( ${flag} CXX_COMPILER_SUPPORTS__${flag} )
            if( CXX_COMPILER_SUPPORTS__${flag} )
                target_compile_options( ${target} ${visibility} $<$<COMPILE_LANGUAGE:CXX>:${flag}>  )
            endif()
        endif()
    endforeach()
endfunction()


if(READLINE_FOUND MATCHES TRUE)
    addprefix(ABC_READLINE_INCLUDES_FLAGS "-I" ${READLINE_INCLUDE})
    string(REPLACE ";" " " ABC_READLINE_INCLUDES_FLAGS "${ABC_READLINE_INCLUDES_FLAGS}")
    list(APPEND ABC_READLINE_FLAGS "ABC_READLINE_INCLUDES=${ABC_READLINE_INCLUDES_FLAGS}")

    string(REPLACE ";" " " ABC_READLINE_LIBRARIES_FLAGS "${READLINE_LIBRARIES}")
    list(APPEND ABC_READLINE_FLAGS "ABC_READLINE_LIBRARIES=${ABC_READLINE_LIBRARIES_FLAGS}")
elseif(READLINE_FOUND MATCHES FALSE)
    list(APPEND ABC_READLINE_FLAGS "ABC_USE_NO_READLINE=1")
endif()

if(ABC_USE_NAMESPACE)
    set(ABC_USE_NAMESPACE_FLAGS "ABC_USE_NAMESPACE=${ABC_USE_NAMESPACE}")
endif()

if( APPLE )
    set(make_env ${CMAKE_COMMAND} -E env SDKROOT=${CMAKE_OSX_SYSROOT})
endif()

# run make to extract compiler options, linker options and list of source files
execute_process(
  COMMAND
    ${make_env}
    make
        ${ABC_READLINE_FLAGS}
        ${ABC_USE_NAMESPACE_FLAGS}
        ARCHFLAGS_EXE=${CMAKE_CURRENT_BINARY_DIR}/abc_arch_flags_program.exe
        ABC_MAKE_NO_DEPS=1
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        LD=${CMAKE_CXX_COMPILER}
        cmake_info
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE MAKE_OUTPUT
)

# extract options from make output
function(extract_var SEPARATOR DEST_VARIABLE MAKE_OUTPUT)
  string(REGEX MATCH "${SEPARATOR} .* ${SEPARATOR}" TMP "${MAKE_OUTPUT}")
  string(REGEX REPLACE "${SEPARATOR} (.*) ${SEPARATOR}" "\\1" TMP "${TMP}")

  separate_arguments(TMP)

  set(${DEST_VARIABLE} ${TMP} PARENT_SCOPE)
endfunction()

extract_var(SEPARATOR_SRC ABC_SRC ${MAKE_OUTPUT})
extract_var(SEPARATOR_LIBS ABC_LIBS ${MAKE_OUTPUT})
extract_var(SEPARATOR_CFLAGS ABC_CFLAGS ${MAKE_OUTPUT})
extract_var(SEPARATOR_CXXFLAGS ABC_CXXFLAGS ${MAKE_OUTPUT})

if(ABC_USE_NAMESPACE)
    set_source_files_properties(${ABC_SRC} PROPERTIES LANGUAGE CXX)
endif()

function(abc_properties target visibility)
    target_include_directories(${target} ${visibility} ${CMAKE_CURRENT_SOURCE_DIR}/src )
    target_compile_options_filtered(${target} ${visibility} ${ABC_CFLAGS} ${ABC_CXXFLAGS} -Wno-unused-but-set-variable )
    target_link_libraries(${target} ${visibility} ${ABC_LIBS})
endfunction()

set(ABC_MAIN_SRC src/base/main/main.c)
list(REMOVE_ITEM ABC_SRC ${ABC_MAIN_SRC})

add_library(libabc EXCLUDE_FROM_ALL ${ABC_SRC})



add_dependencies( libabc sta )
add_dependencies( libabc flute )
add_dependencies( libabc def )
add_dependencies( libabc lef )

target_link_libraries( libabc
  PUBLIC
  OpenSTA
  flute
  def
  defzlib
  cdef
  cdefzlib
  lef
  lefzlib
  clef
  clefzlib

  ${ZLIB_LIBRARIES}
  ${X11_LIBRARIES}
  ${JPEG_LIBRARIES}
  ${TCL_LIB}

  ${OpenMP_CXX_LIBRARIES}
  Threads::Threads
  )



abc_properties(libabc PUBLIC)
set_property(TARGET libabc PROPERTY OUTPUT_NAME abc)

add_executable(abc ${ABC_MAIN_SRC})
target_link_libraries(abc PRIVATE libabc)


add_dependencies( abc sta )
add_dependencies( abc flute )
add_dependencies( abc def )
add_dependencies( abc lef )

target_link_libraries( abc
  PUBLIC
  OpenSTA
  flute
  def
  defzlib
  cdef
  cdefzlib
  lef
  lefzlib
  clef
  clefzlib

  ${ZLIB_LIBRARIES}
  ${X11_LIBRARIES}
  ${JPEG_LIBRARIES}
  ${TCL_LIB}

  ${OpenMP_CXX_LIBRARIES}
  Threads::Threads
  )


abc_properties(abc PRIVATE)

add_library(libabc-pic EXCLUDE_FROM_ALL ${ABC_SRC})


add_dependencies( libabc-pic sta )
add_dependencies( libabc-pic flute )
add_dependencies( libabc-pic def )
add_dependencies( libabc-pic lef )

target_link_libraries( libabc-pic
  PUBLIC
  OpenSTA
  flute
  def
  defzlib
  cdef
  cdefzlib
  lef
  lefzlib
  clef
  clefzlib

  ${ZLIB_LIBRARIES}
  ${X11_LIBRARIES}
  ${JPEG_LIBRARIES}
  ${TCL_LIB}

  ${OpenMP_CXX_LIBRARIES}
  Threads::Threads
  )



abc_properties(libabc-pic PUBLIC)
set_property(TARGET libabc-pic PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET libabc-pic PROPERTY OUTPUT_NAME abc-pic)

if(NOT DEFINED ABC_SKIP_TESTS)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
    googletest
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # Specify the commit you depend on and update it regularly.
    URL "https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip"
    )
    FetchContent_MakeAvailable(googletest)
    include(GoogleTest)
    add_subdirectory(test)
endif()