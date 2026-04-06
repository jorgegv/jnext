# ZOT TZX/TAP player by Salvatore Sanfilippo (antirez) — MIT License
# https://github.com/antirez/ZOT
add_library(zot_tzx STATIC tzx.c)
target_include_directories(zot_tzx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
