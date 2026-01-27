/*
   Copyright (C) 2020 The LineageOS Project.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
   ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
   BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
   BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
   OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
   IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <cstring>

#include <android-base/properties.h>
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include <android-base/file.h>

using android::base::GetProperty;
using android::base::ReadFileToString;

static constexpr const char* const kPropSources[] = {
    "",
    "odm.",
    "system.",
    "system_ext.",
    "vendor.",
    "vendor_dlkm.",
};
static constexpr size_t kPropSourcesCount = sizeof(kPropSources) / sizeof(kPropSources[0]);

static void property_override(const char* prop, size_t prop_len,
                              const char* value, size_t value_len,
                              bool add = true)
{
    auto pi = const_cast<prop_info*>(__system_property_find(prop));
    if (pi) {
        __system_property_update(pi, value, value_len);
    } else if (add) {
        __system_property_add(prop, prop_len, value, value_len);
    }
}

static inline void property_override(const char* prop, const char* value, bool add = true)
{
    property_override(prop, strlen(prop), value, strlen(value), add);
}

static void set_ro_build_prop(const char* prop, const char* value)
{
    char prop_name[PROP_NAME_MAX];
    const size_t value_len = strlen(value);

    for (size_t i = 0; i < kPropSourcesCount; ++i) {
        int len = snprintf(prop_name, sizeof(prop_name), "ro.%sbuild.%s",
                           kPropSources[i], prop);
        if (len > 0 && static_cast<size_t>(len) < sizeof(prop_name)) {
            property_override(prop_name, len, value, value_len, i == 0);
        }
    }
}

static void set_ro_product_prop(const char* prop, const char* value)
{
    char prop_name[PROP_NAME_MAX];
    const size_t value_len = strlen(value);

    for (size_t i = 0; i < kPropSourcesCount; ++i) {
        int len = snprintf(prop_name, sizeof(prop_name), "ro.product.%s%s",
                           kPropSources[i], prop);
        if (len > 0 && static_cast<size_t>(len) < sizeof(prop_name)) {
            property_override(prop_name, len, value, value_len, false);
        }
    }
}

static void parse_cmdline_display(const char* cmdline)
{
    const char* key = "msm_drm.dsi_display0=";
    const char* pos = strstr(cmdline, key);
    if (!pos) return;

    pos += strlen(key);

    if (strncmp(pos, "dsi_ss_fhd_eb_f10_cmd_display:", 30) == 0) {
        property_override("ro.product.display0", "eb");
    } else if (strncmp(pos, "dsi_ss_fhd_ea_f10_cmd_display:", 30) == 0) {
        property_override("ro.product.display0", "ea");
    }
}

void vendor_load_properties()
{
    const std::string region = GetProperty("ro.boot.hwc", "GLOBAL");
    const std::string hw_rev = GetProperty("ro.boot.hwversion", "UNKNOWN");

    const char* model;
    const char* device;
    const char* mod_device = nullptr;

    if (region == "GLOBAL") {
        model = "Mi 9T";
        device = "davinci";
        mod_device = "davinci_global";
    } else if (region == "CN") {
        model = "Redmi K20";
        device = "davinci";
    } else if (region == "INDIA") {
        model = "Redmi K20";
        device = "davinciin";
        mod_device = "davinciin_in_global";
    } else {
        model = "Mi 9T";
        device = "davinci";
    }

    set_ro_product_prop("device", device);
    set_ro_product_prop("model", model);

    if (mod_device) {
        property_override("ro.product.mod_device", mod_device);
    }

    property_override("ro.boot.product.hardware.sku", device);
    property_override("ro.boot.hardware.revision", hw_rev.c_str());

    std::string cmdline;
    if (ReadFileToString("/proc/cmdline", &cmdline)) {
        parse_cmdline_display(cmdline.c_str());
    }
}
