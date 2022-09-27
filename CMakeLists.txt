if(CONFIG_LCZ_LWM2M_UTIL)
    zephyr_include_directories(include)
    zephyr_sources(source/lcz_lwm2m_util.c)
endif()

if(CONFIG_LCZ_LWM2M_UTIL_FWK_BROADCAST_ON_CREATE)
    add_fwk_msgcode_file(${CMAKE_CURRENT_SOURCE_DIR}/framework/msg_codes.h)
endif()