file( GLOB NEOVIM_SOURCES *.c )

foreach(sfile ${NEOVIM_SOURCES})
  get_filename_component(f ${sfile} NAME)
  if(${f} MATCHES "^(regexp_nfa.c|farsi.c|arabic.c)$")
    list(APPEND to_remove ${sfile})
  endif()
endforeach()

list(REMOVE_ITEM NEOVIM_SOURCES ${to_remove})
list(APPEND NEOVIM_SOURCES "${PROJECT_BINARY_DIR}/config/auto/pathdef.c")


add_executable (vim ${NEOVIM_SOURCES})

list(APPEND LINK_LIBS m termcap uv rt pthread)
list(APPEND LINK_LIBS selinux)

target_link_libraries (vim ${LINK_LIBS}) 
include_directories ("${PROJECT_SOURCE_DIR}/src/proto") 
