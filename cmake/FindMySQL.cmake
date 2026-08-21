#[=======================================================================[.rst:
FindMySQL
---------

Find the MySQL / MariaDB database library headers and libraries.

Imported Targets
^^^^^^^^^^^^^^^^

``MySQL::MySQL``
  The MySQL/MariaDB client library target, if found.

Result Variables
^^^^^^^^^^^^^^^^

``MySQL_FOUND``
  True if MySQL/MariaDB was found.
``MySQL_INCLUDE_DIRS``
  The directory containing mysql.h.
``MySQL_LIBRARIES``
  The libraries to link against.
``MySQL_VERSION``
  The version of MySQL/MariaDB found.
#]=======================================================================]

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_MYSQL QUIET mysqlclient libmariadb mariadb)
endif()

find_path(MySQL_INCLUDE_DIR
    NAMES mysql.h mariadb/mysql.h mysql/mysql.h
    HINTS
        ${PC_MYSQL_INCLUDE_DIRS}
        "$ENV{HOME}/.local/include"
        "$ENV{HOME}/.local/include/mariadb"
        "$ENV{HOME}/.local/usr/include"
        "$ENV{HOME}/.local/usr/include/mariadb"
        "$ENV{HOME}/.local/usr/include/mysql"
    PATH_SUFFIXES
        mariadb
        mysql
        include
        include/mariadb
        include/mysql
)

find_library(MySQL_LIBRARY
    NAMES
        libmariadb.so.3
        libmariadb.so
        libmysqlclient.so
        mariadb
        mysqlclient
        mariadbclient
        libmariadb
        libmysqlclient
        libmariadbclient
        mysqlclient_r
    HINTS
        ${PC_MYSQL_LIBRARY_DIRS}
        /usr/lib/x86_64-linux-gnu
        /usr/lib
        /usr/local/lib
        "$ENV{HOME}/.local/lib"
        "$ENV{HOME}/.local/usr/lib/x86_64-linux-gnu"
        "$ENV{HOME}/.local/usr/lib"
    PATH_SUFFIXES
        lib
        lib64
        x86_64-linux-gnu
)

find_package(OpenSSL QUIET)
find_package(ZLIB QUIET)

if(MySQL_INCLUDE_DIR)
    set(_version_file "")
    if(EXISTS "${MySQL_INCLUDE_DIR}/mariadb_version.h")
        set(_version_file "${MySQL_INCLUDE_DIR}/mariadb_version.h")
    elseif(EXISTS "${MySQL_INCLUDE_DIR}/mysql_version.h")
        set(_version_file "${MySQL_INCLUDE_DIR}/mysql_version.h")
    elseif(EXISTS "${MySQL_INCLUDE_DIR}/mariadb/mariadb_version.h")
        set(_version_file "${MySQL_INCLUDE_DIR}/mariadb/mariadb_version.h")
    elseif(EXISTS "${MySQL_INCLUDE_DIR}/mysql/mysql_version.h")
        set(_version_file "${MySQL_INCLUDE_DIR}/mysql/mysql_version.h")
    endif()

    if(_version_file)
        file(STRINGS "${_version_file}" _mysql_version_lines
            REGEX "#define[ \t]+(MARIADB_CLIENT_VERSION_STR|MYSQL_SERVER_VERSION|LIBMARIADB_VERSION)[ \t]+\"[0-9.]+\"")
        if(_mysql_version_lines MATCHES "#define[ \t]+[A-Za-z0-9_]+[ \t]+\"([0-9.]+)\"")
            set(MySQL_VERSION "${CMAKE_MATCH_1}")
        endif()
        unset(_mysql_version_lines)
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MySQL
    REQUIRED_VARS MySQL_LIBRARY MySQL_INCLUDE_DIR
    VERSION_VAR MySQL_VERSION
)

if(MySQL_FOUND)
    set(MySQL_INCLUDE_DIRS "${MySQL_INCLUDE_DIR}")
    set(MySQL_LIBRARIES "${MySQL_LIBRARY}")
    if(OPENSSL_FOUND)
        list(APPEND MySQL_LIBRARIES ${OPENSSL_LIBRARIES})
    endif()
    if(ZLIB_FOUND)
        list(APPEND MySQL_LIBRARIES ${ZLIB_LIBRARIES})
    endif()

    if(NOT TARGET MySQL::MySQL)
        add_library(MySQL::MySQL UNKNOWN IMPORTED)
        set_target_properties(MySQL::MySQL PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${MySQL_INCLUDE_DIR}"
            IMPORTED_LOCATION "${MySQL_LIBRARY}"
        )
        if(OPENSSL_FOUND)
            set_property(TARGET MySQL::MySQL APPEND PROPERTY INTERFACE_LINK_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
        endif()
        if(ZLIB_FOUND)
            set_property(TARGET MySQL::MySQL APPEND PROPERTY INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)
        endif()
    endif()
endif()

mark_as_advanced(MySQL_INCLUDE_DIR MySQL_LIBRARY)
