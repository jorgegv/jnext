#pragma output REGISTER_SP = 0xFFFE
#pragma output CRT_STACK_SIZE = 500

#pragma output CRT_ORG_CODE = 0xC000

#pragma output CRT_ENABLE_CLOSE       = 0          // don't bother closing files on exit

#pragma output CLIB_STDIO_HEAP_SIZE   = 0          // no stdio heap (no files)
#pragma output CLIB_EXIT_STACK_SIZE   = 0          // no atexit() functions

#pragma output CLIB_FOPEN_MAX         = -1         // no FILE* list
#pragma output CLIB_OPEN_MAX          = -1         // no fd table
