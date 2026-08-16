#[=======================================================================[.rst:
FindSQLite3
-----------

Find the SQLite3 database library headers and libraries.

Imported Targets
^^^^^^^^^^^^^^^^

``SQLite::SQLite3``
  The SQLite3 library, if found.

Result Variables
^^^^^^^^^^^^^^^^

``SQLite3_FOUND``
  True if SQLite3 was found.
``SQLite3_INCLUDE_DIRS``
  The directory containing sqlite3.h.
``SQLite3_LIBRARIES``
  The libraries to link against.
``SQLite3_VERSION``
  The version of SQLite3 found.
#]=======================================================================]

find_path(SQLite3_INCLUDE_DIR
    NAMES sqlite3.h
    PATH_SUFFIXES include
)

find_library(SQLite3_LIBRARY
    NAMES sqlite3 sqlite3_static
    PATH_SUFFIXES lib
)

if(SQLite3_INCLUDE_DIR AND EXISTS "${SQLite3_INCLUDE_DIR}/sqlite3.h")
    file(STRINGS "${SQLite3_INCLUDE_DIR}/sqlite3.h" _sqlite3_version_lines
        REGEX "#define[ \t]+SQLITE_VERSION[ \t]+\"[0-9.]+\"")
    if(_sqlite3_version_lines MATCHES "#define[ \t]+SQLITE_VERSION[ \t]+\"([0-9.]+)\"")
        set(SQLite3_VERSION "${CMAKE_MATCH_1}")
    endif()
    unset(_sqlite3_version_lines)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SQLite3
    REQUIRED_VARS SQLite3_LIBRARY SQLite3_INCLUDE_DIR
    VERSION_VAR SQLite3_VERSION
)

if(SQLite3_FOUND)
    set(SQLite3_INCLUDE_DIRS "${SQLite3_INCLUDE_DIR}")
    set(SQLite3_LIBRARIES "${SQLite3_LIBRARY}")

    if(NOT TARGET SQLite::SQLite3)
        add_library(SQLite::SQLite3 UNKNOWN IMPORTED)
        set_target_properties(SQLite::SQLite3 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIR}"
            IMPORTED_LOCATION "${SQLite3_LIBRARY}"
        )
    endif()
endif()

mark_as_advanced(SQLite3_INCLUDE_DIR SQLite3_LIBRARY)
