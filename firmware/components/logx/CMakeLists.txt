idf_component_register(SRCS "logx.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board esp_timer)

# msg[] and line[] are both LOGX_LINE_MAX (160 B): a full-length msg embedded
# behind the "[level][ts] tag: " prefix can make snprintf's worst-case bound
# exceed line[]'s size. snprintf never overflows — it just truncates the
# tail of an overlong line, which is an accepted limit of a fixed-size log
# ring — so this is not a real bug, just IDF's default -Werror=all catching
# GCC's conservative static bound.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format-truncation)
