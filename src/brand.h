#pragma once
#include "local_config.h"
#define VP_BRAND "vietphuong esp"
#define VP_FACEBOOK "https://www.facebook.com/leviethoaiphuong"
#define VP_AP_PREFIX "vietphuong esp"
// Temporary setup hotspot only; this shared default is not administrator
// authentication.
#define VP_AP_PASSWORD "vietphuongesp"
#define VP_SETUP_IP "192.168.4.1"
#define VP_CLOUD_BASE FIRMWARE_UPDATE_BASE_URL
#if defined(PRINTSERVER_PROFILE_HP)
#define VP_PROFILE "hp"
#elif defined(PRINTSERVER_PROFILE_CANON)
#define VP_PROFILE "canon"
#else
#define VP_PROFILE "all"
#endif
