set(VCPKG_POLICY_ALLOW_RESTRICTED_HEADERS enabled)

vcpkg_find_acquire_program(PERL)
set(ENV{PERL} "${PERL}")

vcpkg_find_acquire_program(PKGCONFIG)
set(ENV{PKG_CONFIG} "${PKGCONFIG}")

vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO besser82/libxcrypt
        REF "174c24d6e87aeae631bc0a7bb1ba983cf8def4de"
        SHA512 86596b2b8800ef6be20e4d4ed2f8969eac8dfad79064b7fe65ce418cf1365e6fea32e7a72dd51d2782337e874527c4e83a6f21dd326801012cc393bf94d397c0
)

vcpkg_make_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        AUTORECONF
        OPTIONS "--disable-werror"
)
vcpkg_make_install()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSING" "${SOURCE_PATH}/COPYING.LIB")