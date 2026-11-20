# Watchpoints in avr8-stub

avr8-stub can stop or record the program status when the value of a variable in RAM
changes. There are three ways to use this feature:

| Mode | Set by | What happens on a hit |
|---|---|---|
| GDB watchpoint | GDB command `watch var` | Program stops, GDB shows the old and new value |
| Local watchpoint | Program calls `debug_watch()` | Program stops in GDB (SIGTRAP) |
| Stand-alone | Program calls `debug_watch()`, no GDB | Hit and backtrace are recorded, program continues |
| (Disabled) | AVR8_LOCAL_WATCH_SUPPORT=0<br>No watchpoint handling on AVR side. GDB will probably fall back to software watchpoints, handled on GDB side: single stepping and memory check after each step. | Depends on GDB |

## How it works

The AVR has no hardware support for data watchpoints. The stub uses the same
mechanism as for RAM breakpoints: an external interrupt (INTx) is triggered
after every instruction of the program, and the ISR compares the watched
memory with its last known value.

Consequences:

* Watchpoints require RAM breakpoints: `AVR8_BREAKPOINT_MODE 1`.
* Only **write** watchpoints exist (`watch`). Read and access watchpoints
  (`rwatch`, `awatch`) are not supported.
* A hit is detected when the **value changes**. Writing the same value again
  is not detected.
* The program stops (or the hit is recorded) after the instruction that
  wrote the value, so the PC points to the following instruction.
* While a watchpoint is active, the program runs much slower than normal.
  If speed is important, you may want to use a hardware based solution instead,
  e.g.: https://pyavrocd.io/v2.0/
* The pin of the external interrupt (see `AVR8_SWINT_SOURCE`, D2 on the Uno by
  default) cannot be used by the program.

### Why an interrupt pin is required

A watchpoint has to check the memory after **every** instruction. Otherwise
the recorded PC and backtrace point to code that ran later than the write,
not to the code that made it. On the AVR, only a level-triggered external
interrupt, that is permanently pending, fires after each instruction.

The pin-free alternatives of the stub do not work for watchpoints:

* **Watchdog timer (WDT)** or Timer0 (`AVR8_USE_TIMER0_INSTEAD_OF_WDT`): with
  flash breakpoints these interrupts only check from time to time whether
  the program has reached a breakpoint. The shortest WDT period is 16 ms,
  about 256,000 clock cycles at 16 MHz. A watchpoint based on it could only
  report that the value changed at some point during the last period.
* **`AVR8_SWINT_SOURCE = -1`** (Timer0 compare interrupt instead of an
  external interrupt): only available with flash breakpoints. The stub
  rejects it with `#error` in RAM breakpoint mode, which watchpoints need.
  This interrupt also has a low priority, so it would not reliably fire
  after each instruction.

Choosing the pin:

* **Uno, Nano, Pro Mini (ATmega328):** D2 (`AVR8_SWINT_SOURCE 0`) or
  D3 (`AVR8_SWINT_SOURCE 1`). One of them **must** stay free. See details below.
* **Mega (ATmega1280/2560):** use `AVR8_SWINT_SOURCE 6` or `7` (INT6/INT7).
  These pins are not connected on the Arduino Mega board, so no usable pin
  is lost.
* **ATmega1284(P):** PD2 (`0`), PD3 (`1`) or PB2 (`2`).

### Wiring of the interrupt pin

Not using the pin in the program is not enough. The external circuit must
also tolerate what the stub does with it. As soon as a watchpoint or RAM
breakpoint is set, or GDB single-steps, the stub configures the pin as an
**output driven LOW**. The interrupt is level-triggered and reads the actual
pin level. Disabling the interrupt only clears the enable bit in `EIMSK`;
the pin output remains on LOW level until the next reset.

**Safe:**

* Pin not connected, e.g. a bare Uno or Nano.
* Push button to GND. Pressing it changes nothing, because the pin is
  already LOW.
* Pull-up resistor to VCC that is not too small. 10 kΩ draws about 0.5 mA,
  1 kΩ about 5 mA.
* High-impedance inputs of other devices, as long as a permanent LOW does
  no harm there.

**Not safe:**

* **Anything that actively drives the pin HIGH:** the output of a sensor or
  IC (e.g. an interrupt line), a button to VCC without a resistor, another
  microcontroller. Two outputs then drive against each other, which may
  damage the pin. In addition, the interrupt may see HIGH and never fire,
  so watchpoints, RAM breakpoints and single-stepping stop working without
  any error message.
* **Inputs that react to LOW**, such as reset, chip select or enable of
  another device. That device is held permanently in that state.
* **Shields** that use D2 or D3, e.g. as the interrupt line of an Ethernet
  or radio module. Check the schematic, or use the other pin.

If something drives the pin HIGH, disconnect it while debugging or select
the other pin with `AVR8_SWINT_SOURCE`.

**Note:**
avr_debug will probably only work with real hardware.
During some tests it did <u>not</u> work with the simulator "simavr", at least not out of the box.
Maybe it would work with some additional setup / configuration, e.g. for pin 2.
But simavr has its own gdb mode anyway. So, you do not need avr_debug there.



## Configuration (avr8-stub.h)

| Option | Default | Meaning |
|---|---|---|
| `AVR8_LOCAL_WATCH_SUPPORT` | 0 | Local watchpoint handling's "main switch":<br>`2:` Local watchpoint handling enabled, also compiles `debug_watch()` / `debug_unwatch()`. Application can add/remove watchpoints.<br>`1:` Local watchpoint handling enabled, only GDB can use it. Saves some flash space.<br>`0:` Local watchpoint handling disabled. Exactly like the pre-November-2026 version of avr8_stub. Saves flash space. GDB needs to handle watchpoints itself ("software watchpoints") and might refuse to set watchpoints.|
| `AVR8_MAX_WATCHES` | 2 | Number of watchpoints that can be active at the same time. `0` removes the watchpoint support completely. |
| `AVR8_WATCH_MAXLEN` | 4 | Maximum size of one watched variable / memory area in bytes. |
| `AVR8_STANDALONE_WATCH` | 0 | `1:` Enables the stand-alone mode (see below). |
| `AVR8_BACKTRACE_DEPTH` | 8 | Stand-alone mode: maximum number of backtrace entries to be recorded. |

All options use `#ifndef`, so they can also be set as compiler flags
(`-DAVR8_STANDALONE_WATCH=1`).

**Arduino IDE:** the library's `avr8-stub.c` is compiled separately from the
sketch. A `#define` in the sketch does **not** change the library. Edit
`avr8-stub.h` in the library folder, or add the flag to the compiler options.

## GDB watchpoints

Use `watch variable` in GDB as usual. GDB sends the packet `Z2,<addr>,<len>`
to set and `z2,<addr>,<len>` to remove the watchpoint. RAM addresses have the
offset `0x800000`. When the watchpoint triggers, the stub adds
`watch:<addr>;` to the stop reply so that GDB can show the old and new value.

## Local watchpoints

The application can add or remove watchpoints itself.

```c
#include "avr8-stub.h"

uint8_t debug_watch(const volatile void* addr, uint8_t len);
uint8_t debug_unwatch(const volatile void* addr);
```

* `addr`: RAM address of the variable, normally `&variable`.
* `len`: number of bytes to watch, normally `sizeof(variable)`,
  from 1 to `AVR8_WATCH_MAXLEN`.
* Both functions return 1 on success and 0 on failure. Failure means no free
  slot, invalid parameters, `debug_init()` was not called, or watchpoints
  are not supported in this configuration.
* Calling `debug_watch()` again on the same address updates the watchpoint.
  This also takes the current value as the new reference value.

With `avr_debugger.h` you can use the macros `dbg_watch(var)` and
`dbg_unwatch(var)` instead, for convenience. They expand to nothing unless `__DEBUG__` is defined.

Note:
GDB does not know about watchpoints set by the application.
If GDB removes a watchpoint on the same address (`z2`), the local watchpoint is removed as well.

## Stand-alone mode

This can be useful for applications, that need the (only) UART themselves. So, GDB cannot be connected.

Set `AVR8_STANDALONE_WATCH` to 1 to find memory corruption without a debugger.
In this mode:

* The stub does not use the UART at all, so the program can use `Serial`.
* There is no GDB communication. Breakpoints and stepping are not available.
  `breakpoint()` and `debug_message()` do nothing.
* When a watchpoint triggers, the stub records the hit and the program
  continues. Only the **last** hit is kept.
* Interrupts of the program keep working. After each check, the stub briefly
  lets one pending interrupt run. Interrupt handlers of the program run on
  the stub's internal stack (`GDB_STACKSIZE`, 160 bytes in this mode).
  Increase it if your interrupt handlers use a lot of stack.

Requirements: `AVR8_BREAKPOINT_MODE 1`, `AVR8_LOAD_SUPPORT 0`,
`AVR8_MAX_WATCHES > 0`, `AVR8_LOCAL_WATCH_SUPPORT 2`.
Other combinations stop the build with `#error`.

### Querying a hit

```c
uint8_t debug_watch_get_hit(debug_watch_hit_t* hit);
```

The function returns the number of hits since the previous call.
The count saturates at 255, and 0 means no hit.
If the return value is not 0, the data of the last hit is copied to `*hit`.
`hit` may be `NULL`, which only resets the counter.

| Field | Meaning |
|---|---|
| `watch_addr` | RAM address of the watchpoint that triggered |
| `len` | Number of watched bytes |
| `old_value[]`, `new_value[]` | Value before and after the change (`len` bytes) |
| `pc` | Byte address of the instruction **after** the one that changed the value |
| `sp` | Stack pointer of the program at that moment |
| `in_isr` | 1 if an interrupt handler of the program changed the value. `pc` and `backtrace` then show the interrupted code, not the handler. |
| `depth` | Number of valid entries in `backtrace[]` |
| `backtrace[]` | Return addresses (byte addresses), innermost caller first |

### Resolving the addresses

All addresses are byte addresses, the same as in `avr-objdump` and the
`.map` file. Resolve them with the `.elf` file of the program:

```
avr-addr2line -f -C -e program.elf 0x1a2c 0x0f40 0x0b12
```

The tool prints the function name and source line of each address.
For `pc` you get the line after the write. Backtrace entries point to the line after
the call.
To see the exact writing instruction, look at `pc` in `avr-objdump -d -S program.elf`;
the store instruction is directly before it.

### How the backtrace is found

The target has no frame information. The stub scans the stack from `sp` up to
`RAMEND`. A value counts as a return address if it is a valid flash address
and the instruction before it is `CALL`, `RCALL`, `ICALL` or `EICALL`.
This heuristic has limits:

* Data on the stack that happens to look like a return address produces an
  extra entry.
* Inlined functions have no entry of their own. Compile with `-O0`, or mark
  the suspicious functions `__attribute__((noinline))`.
* Return addresses of interrupts are skipped.

### Tips for finding a memory corruption

* Watch the byte that gets corrupted and set the watchpoint after the last
  legitimate write, or remove it around legitimate writes with
  `debug_unwatch()` / `debug_watch()`.
* Query `debug_watch_get_hit()` often. A later legitimate write overwrites
  the recorded hit.
* If `in_isr` is 1, look for the culprit in your interrupt handlers.

### Example

See `examples/standalone_watch/standalone_watch.ino`. It contains a classic
off-by-one `strcpy()` that overwrites the struct field after a `char[8]`, and
prints the hit and backtrace over `Serial`.

**Output:**
```
avr8-stub stand-alone watchpoint example
========================================
Running in stand-alone debug mode.
setName (Init)
Local watchpoint 'mode' added successfully.
demo 0, uptime: 0 s: setName (Anna)
demo 1, uptime: 1 s: setName (Bob)
demo 2, uptime: 2 s: setName (Kimberly)
Watchpoint hit (1x since last query):
  address 0x1BD, 1 byte(s)
  value   0x3 -> 0x0
  pc 0x1DD6  sp 0x88D
  backtrace: 0xA20 0xA9A 0x4ADA 0x6674 0x41C0 0xCEA 0x44A8 0x16C2
demo 3, uptime: 5 s: setName (Max)
demo 4, uptime: 6 s: setName (Anna)
demo 5, uptime: 8 s: setName (Bob)
demo 6, uptime: 9 s: setName (Kimberly)
Watchpoint hit (1x since last query):
  address 0x1BD, 1 byte(s)
  value   0x3 -> 0x0
  pc 0x1DD6  sp 0x88D
  backtrace: 0xA20 0xA9A 0x4ADA 0x6674 0x41C0 0xCEA 0x14E8 0x16C2
demo 7, uptime: 12 s: setName (Max)
...
```

This shows, that after demo line 2 a data modification 0x3 -> 0x0 at address 0x1BD was detected. Watchpoint hit.
It also shows, that millis() and Serial still work.

Using avr-addr2line, we can find out, where those addresses belong to:
```
avr-addr2line -f -C -e standalone_debugdemo_af 0x1DD6
strcpy
??:?
```
We see: strcpy modified the byte in question. "??:?" ... We do not know the source code file name of strcpy.

Who called strcpy?
Here is the whole story:
```
avr-addr2line -f -C -e standalone_debugdemo_af 0x1DD6 0xA20 0xA9A 0x4ADA 0x6674 0x41C0 0xCEA 0x44A8 0x16C2
strcpy
??:?
setName(char const*)
avr_debug/examples/standalone_watch/standalone_watch.ino:81
nextUser(unsigned char)
avr_debug/examples/standalone_watch/standalone_watch.ino:91
??
??:0
??
??:0
??
??:0
loop
avr_debug/examples/standalone_watch/standalone_watch.ino:181
??
??:0
main
hardware/arduino/avr/cores/arduino/main.cpp:47
```
We see, that the above mentioned heuristic returned a bit too much information. But that is easy to skip and we have all details, that we need to fix the problem.
