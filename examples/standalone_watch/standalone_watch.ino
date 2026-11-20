// Example for the stand-alone watchpoint mode of avr8-stub (no GDB needed).
//
// The sketch contains a deliberate memory corruption bug: setName() copies a
// name into a char[8] with strcpy(). The name "Kimberly" has 8 characters, so
// the terminating zero is written one byte past the array - into the field
// "mode" which follows it in the struct.
// A watchpoint on settings.mode catches the write; the sketch prints the old
// and new value, the PC and the backtrace over Serial.
//
// Setup (see doc/watchpoints.md for details):
// 1. Use the avr-debugger library with the watchpoint extension
//    (avr8-stub.c, avr8-stub.h, avr_debugger.h).
// 2. In the library's avr8-stub.h or in your makefile set
//        #define AVR8_LOCAL_WATCH_SUPPORT (2)
//        #define AVR8_STANDALONE_WATCH    (1)
//    Defining it in this sketch would NOT be enough, because the library's
//    avr8-stub.c is compiled separately and never sees the sketch's defines.
//    Keep the defaults AVR8_BREAKPOINT_MODE 1 and AVR8_LOAD_SUPPORT 0.
// 3. Do not use pin D2 (Uno/Nano, AVR8_SWINT_SOURCE 0): the stub uses its
//    external interrupt INT0 to check the watchpoints after every instruction.
//    It will set this pin as output and switch it to LOW. Check your circuit
//    and make sure, that this pin is either not connected at all or
//    permanent LOW is ok. See doc/watchpoints.md for details.
// 4. Upload and open the Serial Monitor at 115200 baud.
//
// To translate the printed addresses into function names and source lines,
// run the below command on the .elf file of this sketch
// (Arduino IDE: Sketch > Export Compiled Binary, or enable verbose compile
//               output to see the build folder):
//     avr-addr2line -f -C -e standalone_watch.ino.elf 0x<pc> 0x<bt0> 0x<bt1> ...
// Tip: the backtrace is most readable with compiler optimization turned off
// (-O0, see platform.local.txt in the library); with -Os small functions
// are inlined and do not show up as separate entries.
// Note: This demo will probably only work with real hardware,
//       not on a simulator (it would not report the data modification there).
//
#include <string.h>
#include <Arduino.h>
#include <avr8-stub.h>

#if !defined(AVR8_STANDALONE_WATCH) || (! AVR8_STANDALONE_WATCH)
  #warning Please set AVR8_STANDALONE_WATCH to 1 in your makefile or in the library file avr8-stub.h, if you want the stand-alone demonstration.
#endif
#if AVR8_STANDALONE_WATCH  &&  (! (AVR8_LOCAL_WATCH_SUPPORT >= 2))
  #error Please set AVR8_LOCAL_WATCH_SUPPORT to 2 in your makefile or in the library file avr8-stub.h. Required to run this demo in stand-alone debug mode.
#endif

#if AVR8_STANDALONE_WATCH
   // The UART is not occupied by GDB. We can use it.
#  define    INFO_MSG(txt)   Serial.print (txt)
#else
#  ifdef __DEBUG__
      // GDB connection expected. Send the text there.
#     define INFO_MSG(txt)   debug_message(txt)
#  else
#     define INFO_MSG(txt)   Serial.print (txt)
#  endif
#endif


struct Settings {
  char    name[8];   // room for 7 characters + terminating zero
  uint8_t mode;      // gets overwritten by the bug
};

Settings settings;

const char* const names[] = { "Anna", "Bob", "Kimberly", "Max" };
const uint8_t NAME_COUNT = sizeof(names) / sizeof(names[0]);



// The buggy function: no length check.
// noinline keeps it as a separate function so it shows up in the backtrace.
__attribute__((noinline)) void setName (const char* newName)
{
  INFO_MSG ("setName (");
  INFO_MSG (newName);
  INFO_MSG (F(")\n"));
  strcpy (settings.name, newName);   // BUG: "Kimberly" needs 9 bytes
} // setName



__attribute__((noinline)) void nextUser (uint8_t index)
{
  char buf[50];
  snprintf (buf, sizeof(buf), "demo %d, uptime: %ld s: ", (int)index, (long)(millis()/1000));
  INFO_MSG (buf);
  setName (names[index % NAME_COUNT]);
} // nextUser



// Print the data of a watchpoint hit
#if AVR8_STANDALONE_WATCH
void printHit (uint8_t count, const debug_watch_hit_t& hit)
{
  Serial.print (F("Watchpoint hit ("));
  Serial.print (count);
  Serial.println (F("x since last query):"));

  Serial.print (F("  address 0x"));
  Serial.print (hit.watch_addr, HEX);
  Serial.print (F(", "));
  Serial.print (hit.len);
  Serial.println (F(" byte(s)"));

  Serial.print (F("  value  "));
  for (uint8_t i = 0; i < hit.len; i++) {
    Serial.print (F(" 0x"));
    Serial.print (hit.old_value[i], HEX);
  }
  Serial.print (F(" ->"));
  for (uint8_t i = 0; i < hit.len; i++) {
    Serial.print (F(" 0x"));
    Serial.print (hit.new_value[i], HEX);
  }
  Serial.println ();

  Serial.print (F("  pc 0x"));
  Serial.print (hit.pc, HEX);
  Serial.print (F("  sp 0x"));
  Serial.print (hit.sp, HEX);
  if (hit.in_isr)
    Serial.print (F("  (changed by an interrupt handler)"));
  Serial.println ();

  Serial.print (F("  backtrace:"));
  for (uint8_t i = 0; i < hit.depth; i++) {
    Serial.print (F(" 0x"));
    Serial.print (hit.backtrace[i], HEX);
  }
  Serial.println ();
} // printHit
#endif



void setup (void)
{
  //dbg_start ();  // Initialize the stub if __DEBUG__ is defined. Does not touch the UART, if AVR8_STANDALONE_WATCH is enabled.
  debug_init ();  // Initialize the stub. Does not touch the UART, if AVR8_STANDALONE_WATCH is enabled.
  sei();
# if AVR8_STANDALONE_WATCH
  Serial.begin (115200);
  Serial.println (F("avr8-stub stand-alone watchpoint example"));
  Serial.println (F("========================================"));
  Serial.println (F("Running in stand-alone debug mode."));
# else
#   ifdef __DEBUG__
      debug_message ("avr8-stub watchpoint example");
#   else
      Serial.println (F("Running in normal (non-debug) mode."));
#   endif
# endif

  pinMode (LED_BUILTIN, OUTPUT);

  settings.mode = 3;            // Legitimate write - before the watchpoint is set
  setName ("Init");

# if AVR8_LOCAL_WATCH_SUPPORT >= 2
  if (debug_watch (&settings.mode, sizeof(settings.mode)))
    INFO_MSG ("Local watchpoint 'mode' added successfully.\n");
  else
    INFO_MSG ("debug_watch() FAILED - check configuration\n");
# endif
} // setup



void loop (void)
{
  static uint8_t    user = 0;

  nextUser (user++);             // Every 4th call corrupts settings.mode

# if AVR8_STANDALONE_WATCH
  debug_watch_hit_t hit;
  uint8_t           count = debug_watch_get_hit (&hit);
  if (count) {                // Data modification detected!
    printHit (count, hit);    // Print details.
    settings.mode = 3;                 // Repair. Note: this write triggers the watchpoint ...
    (void)debug_watch_get_hit (NULL);  // ... so, discard that (expected) hit.
  }
# endif

/*# ifdef AVR8_STUB_DEBUG   // Extra output for advanced users who need to debug the debugger (gdb stub) itself.
  extern uint8_t  G_Debug_INTxCount;  // How many times external INTx ISR was called
  extern uint16_t G_LastPC;           // Value of PC register in INTx ISR
  Serial.print (F("G_Debug_INTxCount=")); Serial.print (G_Debug_INTxCount); Serial.print (F("   G_LastPC=")); Serial.println (G_LastPC);
# endif */

  // Interrupts (millis, delay, Serial) keep working while a watchpoint is active,
  // but the program runs much slower than normal.
  digitalWrite (LED_BUILTIN, !digitalRead(LED_BUILTIN));
  delay (500);
} // loop
