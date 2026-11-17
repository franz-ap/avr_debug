#include "avr8-stub.h"
#include <avr/interrupt.h>

#ifndef AVR_DEBUGGER_H_
#define AVR_DEBUGGER_H_


#ifdef __cplusplus
extern "C" {
#endif

#if defined(PLATFORMIO) && defined(__PLATFORMIO_BUILD_DEBUG__)
    #ifndef __DEBUG__
        #define __DEBUG__
    #endif
#endif

#if defined(__DEBUG__)
    #define DBG_EXEC(x) x
#else
    #define DBG_EXEC(x)
#endif

#define dbg_breakpoint() { \
    DBG_EXEC(breakpoint()); \
    }

#define dbg_init()       { \
    DBG_EXEC(debug_init()); \
    }

#define dbg_message(x) { \
    DBG_EXEC(debug_message(x)); \
    }

#if (AVR8_LOCAL_WATCH_SUPPORT == 1)
/* Set a write watchpoint on a variable, same as "watch var" in GDB.
 * var - the variable (lvalue) to watch; its address and size are passed
 *       to debug_watch(). */
#define dbg_watch(var)   { \
    DBG_EXEC(debug_watch(&(var), sizeof(var))); \
    }

/* Remove the watchpoint set by dbg_watch(var).
 * var - the variable whose watchpoint should be removed. */
#define dbg_unwatch(var) { \
    DBG_EXEC(debug_unwatch(&(var))); \
    }
#else
/* Local watchpoints disabled (AVR8_LOCAL_WATCH_SUPPORT == 0) - do nothing. */
#define dbg_watch(var)   { }
#define dbg_unwatch(var) { }
#endif

#define dbg_start()      { \
    DBG_EXEC(debug_init()); \
    DBG_EXEC(sei()); \
    DBG_EXEC(breakpoint()); \
    }

#ifdef __cplusplus
}
#endif


#endif /* AVR_DEBUGGER_H_ */