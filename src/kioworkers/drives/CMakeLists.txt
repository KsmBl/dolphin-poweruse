# The drives:/ worker, listing everything Solid can mount
#
# SPDX-License-Identifier: GPL-2.0-or-later

kcoreaddons_add_plugin(kio_drives INSTALL_NAMESPACE "kf6/kio")

target_sources(kio_drives PRIVATE kio_drives.cpp)

target_link_libraries(kio_drives
    KF6::KIOCore
    KF6::I18n
    KF6::Solid
)
