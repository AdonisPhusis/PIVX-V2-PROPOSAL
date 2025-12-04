dnl Copyright (c) 2013-2015 The Bitcoin Core developers
dnl Copyright (c) 2025 The PIV2 developers
dnl Distributed under the MIT software license, see the accompanying
dnl file COPYING or http://www.opensource.org/licenses/mit-license.php.

AC_DEFUN([BITCOIN_FIND_BDB48],[
  AC_ARG_VAR([BDB_CFLAGS], [C compiler flags for BerkeleyDB, bypasses autodetection])
  AC_ARG_VAR([BDB_LIBS], [Linker flags for BerkeleyDB, bypasses autodetection])

  if test "x$BDB_CFLAGS" = "x" && test "x$BDB_LIBS" = "x"; then
    dnl Check for BDB 4.8 in /usr/local/BerkeleyDB.4.8 (compiled from source)
    if test -f "/usr/local/BerkeleyDB.4.8/include/db_cxx.h"; then
      AC_MSG_NOTICE([Found BerkeleyDB 4.8 in /usr/local/BerkeleyDB.4.8])
      BDB_CFLAGS="-I/usr/local/BerkeleyDB.4.8/include"
      BDB_LIBS="-L/usr/local/BerkeleyDB.4.8/lib -ldb_cxx-4.8"
    fi
  fi

  if test "x$BDB_CFLAGS" = "x"; then
    AC_MSG_CHECKING([for Berkeley DB C++ headers])
    dnl Check standard locations
    for searchpath in "" "db4/" "db5/" "db5.3/"; do
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
        #include <${searchpath}db_cxx.h>
      ]],[[
        #if !((DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 8) || DB_VERSION_MAJOR > 4)
          #error "failed to find bdb 4.8+"
        #endif
      ]])],[
        if test "x$searchpath" != "x"; then
          BDB_CPPFLAGS="-I/usr/include/${searchpath}"
        fi
        AC_MSG_RESULT([found])
        bdb_found=yes
        break
      ],[])
    done

    if test "x$bdb_found" != "xyes"; then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([libdb_cxx headers missing, ]AC_PACKAGE_NAME[ requires this library for wallet functionality (--disable-wallet to disable wallet functionality)])
    fi

    dnl Check if it's exactly 4.8
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
      #include <db_cxx.h>
    ]],[[
      #if !(DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR == 8)
        #error "not bdb 4.8"
      #endif
    ]])],[
      bdb48=yes
    ],[
      bdb48=no
    ])

    if test "x$bdb48" != "xyes"; then
      AC_ARG_WITH([incompatible-bdb],[AS_HELP_STRING([--with-incompatible-bdb], [allow using a bdb version other than 4.8])],[
        AC_MSG_WARN([Found Berkeley DB other than 4.8; wallets opened by this build will not be portable!])
      ],[
        AC_MSG_ERROR([Found Berkeley DB other than 4.8, required for portable wallets (--with-incompatible-bdb to ignore or --disable-wallet to disable wallet functionality)])
      ])
    fi
  else
    BDB_CPPFLAGS=${BDB_CFLAGS}
  fi
  AC_SUBST(BDB_CPPFLAGS)

  if test "x$BDB_LIBS" = "x"; then
    dnl Search for library
    for searchlib in db_cxx-4.8 db_cxx-5.3 db_cxx db4_cxx; do
      AC_CHECK_LIB([$searchlib],[main],[
        BDB_LIBS="-l${searchlib}"
        break
      ])
    done
    if test "x$BDB_LIBS" = "x"; then
      AC_MSG_ERROR([libdb_cxx missing, ]AC_PACKAGE_NAME[ requires this library for wallet functionality (--disable-wallet to disable wallet functionality)])
    fi
  fi
  AC_SUBST(BDB_LIBS)
])
