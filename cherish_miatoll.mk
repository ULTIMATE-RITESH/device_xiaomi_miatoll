#
# Copyright (C) 2021-2022 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/cherish/config/common_full_phone.mk)

# Inherit from miatoll device
$(call inherit-product, device/xiaomi/miatoll/miatoll.mk)

# Boot animation
TARGET_BOOT_ANIMATION_RES := 1080

# CherishOS stuff.
TARGET_FACE_UNLOCK_SUPPORTED := true
TARGET_SUPPORTS_GOOGLE_RECORDER := true
TARGET_INCLUDE_LIVE_WALLPAPERS := true
TARGET_SUPPORTS_QUICK_TAP := true
TARGET_USES_AOSP_RECOVERY := true

# CherishOS Build.
WITH_GMS := true
TARGET_GAPPS_ARCH := arm64
#CHERISH_VANILLA := true
#TARGET_USES_PICO_GAPPS := true
USE_PIXEL_CHARGING := true
TARGET_INCLUDE_CARRIER_SETTINGS := true

# Nuke AudioFX
TARGET_EXCLUDES_AUDIOFX := true

# CherishOS props
CHERISH_BUILD_TYPE=OFFICIAL
CHERISH_MAINTAINER := RITESHROKX
CHERISH_CHIPSET := SD720G
CHERISH_BATTERY := 5020mAh
CHERISH_DISPLAY := 1080x2400

PRODUCT_NAME := cherish_miatoll
PRODUCT_DEVICE := miatoll
PRODUCT_MANUFACTURER := Xiaomi
PRODUCT_BRAND := Redmi
PRODUCT_MODEL := SM6250

PRODUCT_GMS_CLIENTID_BASE := android-xiaomi

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRIVATE_BUILD_DESC="miatoll_global-user 12 SKQ1.211019.001 V14.0.5.0.SJZMIXM release-keys"

BUILD_FINGERPRINT := Redmi/miatoll_global/miatoll:12/RKQ1.211019.001/V14.0.5.0.SJZMIXM:user/release-keys
