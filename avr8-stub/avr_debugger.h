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

#if AVR8_LOCAL_WATCH_SUPPORT >= 2
/* Convenience function: Set a write watchpoint on a variable, same as "watch var" in GDB.
 * var - the variable (lvalue) to watch; its address and size are passed
 *       to debug_watch().
 * Returns 1 if the watchpoint was set (or an existing watchpoint on the
 *           same address was updated)
 *         0 if no free watchpoint slot is available, the parameters are invalid,
 *           debug_init() was not called or watchpoints are not supported
 *           in the current mode.
 */
#define dbg_watch(var)   { \
    DBG_EXEC(debug_watch(&(var), sizeof(var))); \
    }

/* Convenience function: Remove the watchpoint set by dbg_watch(var).
 * var - the variable whose watchpoint should be removed.
 * Returns 1 if a watchpoint on this address was found and removed
 *         0 if no such watchpoint existed, debug_init() was not called
 *           or watchpoints are not supported in the current mode.
 */
#define dbg_unwatch(var) { \
    DBG_EXEC(debug_unwatch(&(var))); \
    }
#else
/* debug_watch() and debug_unwatch() are not available. Do nothing. */
#define dbg_watch(var)   { }
#define dbg_unwatch(var) { }
#endif  // AVR8_LOCAL_WATCH_SUPPORT >= 2


#define dbg_start()      { \
    DBG_EXEC(debug_init()); \
    DBG_EXEC(sei()); \
    DBG_EXEC(breakpoint()); \
    }

#ifdef __cplusplus
}
#endif


#endif /* AVR_DEBUGGER_H_ */
