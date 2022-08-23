if(CONFIG_LCZ_LWM2M_UTIL)
    zephyr_include_directories(include)
    zephyr_sources(source/lcz_lwm2m_util.c)
endif()