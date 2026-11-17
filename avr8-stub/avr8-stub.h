/*
 * avr8-stub.h
 *
 *  Created on: 25. 3. 2015
 *      Author: Jan Dolinay
 *
 *  Header file for
 *  GDB Debugging Agent (GDB stub) for ATMega328 and ATMega1280 and ATMega2560
 * This code is actually "gdb stub" - code which runs in the debugged MCU and
 * communicates with the gdb debugger. It uses gdb RCP protocol.
 * The code needs to handle UART Receive interrupt and an interrupt for simulating
 * software interrupt (EXT INT).
 *
 * Configuration options
 * You can pass the symbols to compiler or change the value below
 * (For details see the defines in the code below)
 *
 * AVR8_BREAKPOINT_MODE - how to implement breakpoints and stepping.
 *
 * AVR8_SWINT_SOURCE - which external interrupt is used by the debugger (the corresponding pin cannot be used by user program!)
 *
 * AVR8_USE_TIMER0_INSTEAD_OF_WDT - use timer0 instead of watchdog to detect breakpoints.
 *
 * AVR8_LOAD_SUPPORT - whether the stub should support loading the program by GDB. Then you can upload the program
 *  and start debugging with single click in eclipse IDE.
 *
 * AVR8_USER_BAUDRATE - serial port baud rate for communication with the debugger.
 *   If not provided, default is 115200 for Arduino Uno, Mega, etc.
 *   Example: AVR8_USER_BAUDRATE=9600
 * 
 * AVR8_UART_NUMBER - which USART is used by the debugger. Default is 0 (use USART0). 
 *   On Arduino Mega, by using other USART than the UART0 you can use this debugger together with 
 *   the Arduino Serial functions. On Arduino Uno and other boards based on ATmega328 only 0 is available.
 *
 *
 * The following project were used (and combined) to create this stub:
 * 1) AVR-GDBServer (https://github.com/rouming/AVR-GDBServer)
 * 2) avr-stub from uOS - embedded operating system (https://code.google.com/p/uos-embedded/)
 * 3) AVR gdbstub (http://zevv.nl/play/code/avr-gdbstub/)
 *
 * The API is inspired by the GDB documentation for stubs: (quote)
 * The debugging stub for your architecture supplies these three subroutines:
 * set_debug_traps
 * This routine arranges for handle_exception to run when your program stops. You must call this subroutine explicitly in your program's startup code.
 * handle_exception
 * This is the central workhorse, but your program never calls it explicitly�the setup code arranges for handle_exception to run when a trap is triggered.
 * handle_exception takes control when your program stops during execution (for example, on a breakpoint), and mediates communications with gdb on the host machine. This is where the communications protocol is implemented; handle_exception acts as the gdb representative on the target machine. It begins by sending summary information on the state of your program, then continues to execute, retrieving and transmitting any information gdb needs, until you execute a gdb command that makes your program resume; at that point, handle_exception returns control to your own code on the target machine.
 * breakpoint
 * Use this auxiliary subroutine to make your program contain a breakpoint. Depending on the particular situation, this may be the only way for gdb to get control. For instance, if your target machine has some sort of interrupt button, you won't need to call this; pressing the interrupt button transfers control to handle_exception�in effect, to gdb. On some machines, simply receiving characters on the serial port may also trigger a trap; again, in that situation, you don't need to call breakpoint from your own program�simply running �target remote� from the host gdb session gets control.
 * Call breakpoint if none of these is true, or if you simply want to make certain your program stops at a predetermined point for the start of your debugging session.
 * (end quote)
 *
 * Note about porting to other AVR MCUs:
 * You need to change:
 * 1) UART code in uart_init, putChar and getChar
 * 2) Code for software interrupt emulation in gdb_enable_swinterrupt and gdb_disable_swinterrupt
 *
 * Port to Atmega 1280 (2560), Arduino Mega
 * USART routines work as is; ISR name changes.
 * PC is 3 bytes on Mega2560 and 2 bytes on 1280 as follows from Stack Pointer chapter in the datasheet:
 * The ATmega48A/PA/88A/PA/168A/PA/328/P Program Counter (PC) is 11/12/13/14 bits wide
 * The ATmega640/1280/1281/2560/2561 Program Counter (PC) is 15/16/17 bits wide
 *
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 */

#ifndef DRV_DEBUG_H_
#define DRV_DEBUG_H_


#include <avr/io.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/*  --------- Configuration ------------ */
/** AVR8_BREAKPOINT_MODE
 * Select the mode for handling breakpoints and step command.
 * Options:
 *  0 - FLASH breakpoints with avr-stub bootloader
 *  1 - RAM only - RAM breakpoints and stepping
 *  2 - FLASH breakpoints with Optiboot bootloader (using do_spm() function)
 *
 * More info:
 * RAM breakpoints - use external interrupt to stop the program after each instruction and compare PC
 * 	with breakpoints. If any BP address is reached, the program is halted. Program runs slow with BP enabled.
 * (+) No wear of flash memory
 * (-) Debugged program runs slowly - interrupt occurs after every instruction
 * 		and in the ISR there are perhaps 100 cycles needed to save and restore context.
 * 		How much the debugged program is affected depends also on the design of the program,
 * 		for example, blinking LED with delays based of poling timer value may be affected
 * 		little, while delays using busy loop will be much much longer than expected.
 *
 * Flash breakpoints - writes special instruction at the position where program should stop.
 * (-) Flash memory is overwritten often during debug session. It should survive 10 000 erase-write cycles.
 * (+) Debugged program runs at normal (full) speed between breakpoints.
 *
 *
 * Note: The step command (going to next line) is implemented by comparing the PC register
 * with desired address even in FLASH breakpoints mode. In principle it would be possible to
 * implement it by writing to flash but it would wear the flash memory very fast. There are
 * about 10 overwrites for a single step in the debugger in such case.
 *
 * */
#ifndef	AVR8_BREAKPOINT_MODE
	#define	AVR8_BREAKPOINT_MODE	(1)
#endif

/** AVR8_USE_TIMER0_INSTEAD_OF_WDT
 * Select one of the following options for the interrupt to detect that
 * the program stopped on a breakpoint:
 * 0 - do not use TIMER0 interrupts but the watchdog timer interrupt
 * 1 - use the output compare A interrupt of timer 0 (the timer for millis and delays)
 *
 * Using the watchdog timer is the default, but it means that we cannot use the watchdog timer
 * in the user program.
 *
 * The other option is to use the output compare interrupt A of timer0 (TIMER0 COMPA).
 * To use this option your program must use the Arduino core and timer0 must be configured
 * by this core. Since the timer is used by Arduino for counting milliseconds, this is
 * usually the case.
 * With this option enabled, the user program can use watchdog timer.
 *
 * */
#ifndef AVR8_USE_TIMER0_INSTEAD_OF_WDT
        #define AVR8_USE_TIMER0_INSTEAD_OF_WDT (0)
#endif
#if AVR8_BREAKPOINT_MODE == 1
       #undef AVR8_USE_TIMER0_INSTEAD_OF_WDT
       #define AVR8_USE_TIMER0_INSTEAD_OF_WDT (0)
#endif

/**
 * AVR8_SWINT_SOURCE
 * Source for software interrupt.
 * Valid values are listed below. The pin selected here will be used for generating SW interrupt
 * by the debugger and cannot be used for anything else - it becomes reserved for the debugger.
 *
 * Supported values for Atmega328 (UNO):
 * 0 - use INT0 (pin PD2, Arduino pin 2)
 * 1 - use INT1 (pin PD3, Arduino pin 3)
 *
 * Supported values for ATmega1284(P):
 * 0 - use INT0 (pin PD2)
 * 1 - use INT1 (pin PD3)
 * 2 - use INT3 (pin PB2)
 *
 * Supported values for Atmega2560 and Atmega1280:
 * TIP: Use value 6 or 7; the pins for these INTs are not connected on Arduino
 * mega board, so you will not waste any pin.
 * 0 - INT0 (pin PD0, Arduino pin 21)
 * 1 - INT1 (pin PD1, Arduino pin 20)
 * 2 - INT2 (pin PD2, Arduino pin 19)
 * 3 - INT3 (pin PD3, Arduino pin 18)
 * 4 - INT4 (pin PE4, Arduino pin 2)
 * 5 - INT5 (pin PE5, Arduino pin 3)
 * 6 - INT6 (pin PE6, not used on Arduino)
 * 7 - INT7 (pin PE7, not used on Arduino)
 * Note to implementors
 *  INT0-3 uses EICRA reg. and port D
 *  INT4 - 7 uses EICRB reg. and port E
 *  Pins PE6 and PE7 are not connected on Arduino Mega boards.
 * 
 * NOTE: Special value -1 can be defined to use output compare interrupt of timer0
 * instead of external interrupt.
 * IMPORTANT:
 *  - The debugger may not work properly if any interrupt with higher priority that the
 * timer0 compare interrupt (TIMER0 COMPA) is enabled in the debugged program.
 *  - analogWrite for the pin connected to COMPA channel of timer0 (pin 6  on Arduino Uno)
 *  may be affected by this option.
 * This option is only available if you use FLASH breakpoints (see AVR8_BREAKPOINT_MODE).
 *
 * Note: Pin Change Interrupt (PCINT) could be used also.
 * It could be one of the Arduino analog pins (PC0 - PC5) which are less likely
 * to be used by the user program.
 * Note that if PCINT is used, then INT0 and INT1 used by the user program
 * could cause troubles, because they have higher priority than PCINT and could prevent
 * the debugger from catching the program properly...
 */
  
#ifndef	AVR8_SWINT_SOURCE
	#define	AVR8_SWINT_SOURCE	(0)
#endif


/**
  AVR8_LOAD_SUPPORT
  Enable or disable support for loading the program from the debugger.
  If enabled you can just click the Debug button in IDE to upload new program
  and debug it.
  IMPORTANT: requires support in bootloader so the bootloader in Arduino must
   be replaced with the bootloader provided in this package.
   It doesn't work with the Optiboot bootloader

   Options:
    0 - load from GDB disabled. Load the program using avrdude as usual.
    1 - load from GDB enabled.
*/
#ifndef	AVR8_LOAD_SUPPORT
	#define	AVR8_LOAD_SUPPORT	(0)
#endif

/**
 * UART module used by the debugger.
 * This select the serial communication peripheral used for communicating with the GDB.
 *  
 * This option only makes sense for Arduino Mega because the Atmega2560 and 1280
 * have 4 hardware UARTs. By using other UART than the UART0 you can use this debugger
 * together with the Arduino Serial functions.
 * For Arduino boards based on ATmega328 (like Uno, Nano,...) the only valid
 * option is 0 (to use UART0) because there are no other hardware UARTs.
 * 
 * Supported values for Atmega328 (UNO):
 * 0 - use USART0 (Arduino Serial cannot be used with the debugger)
  *
 * Supported values for ATmega1284(P):
 * 0 - use USART0 
 * 1 - use USART1 
 * 
 * Supported values for Atmega2560 and Atmega1280:
 * 0 - use USART0 (Arduino Serial cannot be used with the debugger)
 * 1 - use USART1 (Rx=PD2=D19, Tx=PD3=D18)
 * 2 - use USART2 (Rx=PH0=D17, Tx=PH1=D16)
 * 3 - use USART3 (Rx=PJ0=D15, Tx=PJ1=D14)
 * 
 * Note: the number in AVR8_UART_NUMBER is used to generate register names in macros; 
 * don't put it into parentheses.
 */
#ifndef AVR8_UART_NUMBER
        #define AVR8_UART_NUMBER       0
#endif

/**
 * Maximum number of breakpoints supported.
 * Note that gdb will set temporary breakpoint, for example, for step into function
 * or the Run to line command, so the actual number of breakpoints user can set will
 * be lower.
 * For flash breakpoints it is better to use lower number - each breakpoint means
 * writing to flash, user will often forget about old breakpoints and these will be
 * rewritten to flash with no need...So it is better to limit the number and make
 * the user delete breakpoints when not needed. It is quite possible to debug
 * a program with single breakpoint.
 */
#if (AVR8_BREAKPOINT_MODE == 1)	/* RAM only breakpoints; recommended 8 breakpoints */
	#define AVR8_MAX_BREAKS       (8)
#else							/* Flash breakpoints, RAM stepping; recommended 4 breakpoints */
	#define	AVR8_MAX_BREAKS       (4)
#endif

/**
 * Maximum number of (write) watchpoints which can be active at the same time.
 * Watchpoints are only supported with RAM breakpoints (AVR8_BREAKPOINT_MODE == 1).
 * They can be set either from GDB ("watch var") or from the debugged program
 * by calling debug_watch().
 * Each watchpoint needs (3 + AVR8_WATCH_MAXLEN) bytes of RAM.
 * Set to 0 to remove the watchpoint support completely (saves flash and RAM).
 */
#ifndef AVR8_MAX_WATCHES
	#define AVR8_MAX_WATCHES      (2)
#endif

/**
 * Maximum size in bytes of the memory area (variable) watched by one watchpoint.
 */
#ifndef AVR8_WATCH_MAXLEN
	#define AVR8_WATCH_MAXLEN     (4)
#endif

/**
 * Local watchpoints - functions debug_watch() and debug_unwatch() which allow
 * the debugged program to set watchpoints itself.
 * 1 - functions are available
 * 0 - functions are not compiled (saves flash); watchpoints can still be set from GDB.
 *     The dbg_watch()/dbg_unwatch() macros in avr_debugger.h then expand to nothing.
 */
#ifndef AVR8_LOCAL_WATCH_SUPPORT
	#define AVR8_LOCAL_WATCH_SUPPORT  (1)
#endif

/**
 * Stand-alone watchpoint mode - debugging without GDB.
 * 1 - The stub does not use the UART at all (the program can use Serial itself)
 *     and does not communicate with GDB. Only local watchpoints work:
 *     when a watchpoint triggers, the stub records the watchpoint, the PC and
 *     a backtrace and lets the program continue. The program can query the
 *     last hit with debug_watch_get_hit().
 *     Requires AVR8_BREAKPOINT_MODE 1, AVR8_LOAD_SUPPORT 0, AVR8_MAX_WATCHES > 0
 *     and AVR8_LOCAL_WATCH_SUPPORT 1. The pin of the external interrupt
 *     selected by AVR8_SWINT_SOURCE is still used by the stub.
 *     breakpoint() and debug_message() do nothing in this mode.
 * 0 - Normal mode: debugging with GDB over UART.
 */
#ifndef AVR8_STANDALONE_WATCH
	#define AVR8_STANDALONE_WATCH  (0)
#endif

/**
 * Stand-alone mode only: maximum number of return addresses recorded in the
 * backtrace when a watchpoint triggers.
 * Each entry needs 2 bytes of RAM (4 bytes on MCUs with more than 64 KB flash).
 */
#ifndef AVR8_BACKTRACE_DEPTH
	#define AVR8_BACKTRACE_DEPTH  (8)
#endif

/* Derived: 1 if watchpoints are supported in the current configuration. Do not change. */
#if (AVR8_BREAKPOINT_MODE == 1) && (AVR8_MAX_WATCHES > 0)
	#define AVR8_WATCH_SUPPORT    (1)
#else
	#define AVR8_WATCH_SUPPORT    (0)
#endif


/**
 * Define this to enable some global variables to make it easier to debug this stub.
 * This is for advanced users who need to debug the debugger (gbd stub) itself.
 */
/* #define AVR8_STUB_DEBUG */




/**
 * Initialize the debugger driver
 * ? Is this our version of set_debug_traps ? (see above)
 * But the interrupts are re-directed in compile time, so we do not need such function.
 */
void debug_init(void);

/**
 * Insert breakpoint at the position where this function is called.
 * To set-up breakpoints before compilation.
*/
void breakpoint(void);


/**
 * Send text message to gdb console.
 * The function appends "\n" to the msg.
 * Note that GDB will queue the messages until "\n" (0x0a) is received; then
 * it displays the messages in console.
 */
void debug_message(const char* msg);

#if (AVR8_LOCAL_WATCH_SUPPORT == 1)
/**
 * Set a write watchpoint from within the debugged program.
 * This has the same effect as the GDB command "watch <variable>": after every
 * instruction the stub compares the watched memory with its last known value;
 * if the value has changed, the program is stopped (SIGTRAP) and control is
 * passed to GDB. The program stops at the instruction following the write.
 * Notes:
 * - Only available with RAM breakpoints (AVR8_BREAKPOINT_MODE == 1); in the
 *   other modes the function does nothing and returns 0.
 * - Only changes of the value are detected; writing the same value again
 *   does not trigger the watchpoint.
 * - While a watchpoint is active the program runs slowly (as with RAM breakpoints).
 * - debug_init() must have been called before.
 * - GDB does not know about watchpoints set by this function, so it reports
 *   the stop as a plain SIGTRAP.
 * - In stand-alone mode (AVR8_STANDALONE_WATCH == 1) the program is not stopped;
 *   the hit is recorded and can be read with debug_watch_get_hit().
 *
 * @param addr  RAM address of the memory to watch, typically &variable.
 * @param len   Number of bytes to watch starting at addr, typically
 *              sizeof(variable); 1 to AVR8_WATCH_MAXLEN.
 * @return      1 if the watchpoint was set (or an existing watchpoint on the
 *              same address was updated); 0 if no free watchpoint slot is
 *              available, the parameters are invalid, debug_init() was not
 *              called or watchpoints are not supported in this mode.
 */
uint8_t debug_watch(const volatile void* addr, uint8_t len);

/**
 * Remove a watchpoint set by debug_watch() or by GDB.
 *
 * @param addr  RAM address the watchpoint was set on (the same value as
 *              passed to debug_watch()).
 * @return      1 if a watchpoint on this address was found and removed;
 *              0 if no such watchpoint exists, debug_init() was not called
 *              or watchpoints are not supported in this mode.
 */
uint8_t debug_unwatch(const volatile void* addr);
#endif	/* AVR8_LOCAL_WATCH_SUPPORT */

#if (AVR8_STANDALONE_WATCH == 1)
/**
 * Type for code (flash) addresses in the backtrace. These are BYTE addresses
 * as used by avr-objdump, avr-addr2line and the .map file.
 */
#if (FLASHEND > 0xFFFF)
	typedef uint32_t debug_code_addr_t;
#else
	typedef uint16_t debug_code_addr_t;
#endif

/**
 * Information about the last watchpoint hit in stand-alone mode.
 * Filled by debug_watch_get_hit().
 */
typedef struct
{
	/** RAM address of the watchpoint which triggered (as passed to debug_watch()). */
	uint16_t watch_addr;
	/** Number of watched bytes. */
	uint8_t len;
	/** Value of the watched memory before the change (first len bytes valid). */
	uint8_t old_value[AVR8_WATCH_MAXLEN];
	/** Value of the watched memory after the change (first len bytes valid). */
	uint8_t new_value[AVR8_WATCH_MAXLEN];
	/** Byte address of the instruction FOLLOWING the one which changed the value.
	 * The writing instruction is the one just before it. */
	debug_code_addr_t pc;
	/** Stack pointer of the program when the change was detected. */
	uint16_t sp;
	/** 1 if the value was changed by an interrupt handler of the program.
	 * pc and backtrace then show the main program code which was interrupted,
	 * not the ISR itself. 0 if changed by the code at pc. */
	uint8_t in_isr;
	/** Number of valid entries in backtrace[]. */
	uint8_t depth;
	/** Return addresses found on the stack (byte addresses), innermost caller
	 * first. Each address points just after a call instruction; resolve it
	 * with: avr-addr2line -f -e <program.elf> <address>
	 * The backtrace is found by a heuristic stack scan, see avr8-stub.c. */
	debug_code_addr_t backtrace[AVR8_BACKTRACE_DEPTH];
} debug_watch_hit_t;

/**
 * Stand-alone mode: query whether a watchpoint has triggered and get the
 * information about the last hit. Calling this function resets the hit counter.
 * Only the last hit is stored; earlier hits are overwritten.
 *
 * @param hit   Pointer to a structure which receives the data of the last hit.
 *              It is written only if the return value is not 0. May be NULL
 *              if only the hit counter is of interest.
 * @return      Number of watchpoint hits since the previous call
 *              (saturates at 255); 0 if no watchpoint has triggered.
 */
uint8_t debug_watch_get_hit(debug_watch_hit_t* hit);
#endif	/* AVR8_STANDALONE_WATCH */



#ifdef __cplusplus
}
#endif


#endif /* DRV_DEBUG_H_ */
