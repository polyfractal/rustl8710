// You should create a copy of this file in your flash loader project
// and configure it as described below

// when this macro is non-zero, your FlashInit function should accept
// extra 'argc' and 'argv' arguments as specified by the function
// prototype in 'flash_loader.h'
#define USE_ARGC_ARGV 1

// You can customize the memory reserved for passing arguments to FlashInit
// through argc and argv.
#if USE_ARGC_ARGV
// This specifies the maximum allowed number of arguments in argv
#define MAX_ARGS 5
// This specifies the maximum combined size of the arguments, including
// a trailing null for each argument
#define MAX_ARG_SIZE 64
#endif

// If this is true (non-zero), the parameter designating the code destination
// in flash operations will be a 'void *', otherwise it will be a uint32_t.
// Targets where void * is smaller than a code pointer should set this to 0.
#define CODE_ADDR_AS_VOID_PTR 1
