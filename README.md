# RTC Peripheral Driver

This module provides an abstraction layer for the **real-time clock** (`RTC`) of STM32 MCUs: the calendar (date and time with
milliseconds), the alarms, the periodic wake-up timer, the timestamp, the calibration of the crystal and the **backup
registers**. It is a low-level MCAL module: it owns the RTC hardware, the clock source selection of the backup domain,
the write protection and the initialization sequences; the application and the other modules never touch the
registers.

The public interface (`Rtc_Port.h`, `Rtc_Types.h`) is **identical for all supported families**. The implementation
for a family lives in its own branch (`Dev/<Family>` for development, `Releases/<Family>` for the released versions), so
a module or an application written against the interface does not change when the MCU family changes.

This README is the landing page and the **specification of the interface** (master). The family branches contain their
own README with everything that is specific to the family: the instances of the device, their settings and the status of
the implementation.

> **Status: design.** No implementation exists yet, the interface below is a proposal. Stories and branches follow.

---

## Intent and purpose

- The RTC is the only peripheral that **survives** a reset, the Standby mode and (with a battery on `VBAT`) the power
  loss. A driver for it has to answer two questions that no other peripheral has: *"is the time still valid?"* and
  *"may I touch the clock at all?"* The module detects whether the RTC is already running with the requested clock and a
  valid calendar after a reset or a wake-up, and then **does not reset or re-initialize it**; only a lost or an
  unset calendar is initialized.
- The user works with a date and time (`rtc_DateTime_t`), with a Unix time or with a period in ms. The module hides the
  BCD registers, the 12 / 24 hour format, the asynchronous and synchronous prescalers (it calculates them from the
  frequency of the selected clock so that the calendar ticks at exactly 1 Hz), the sub-second register, the write
  protection keys, the initialization mode and the consistent read through the shadow registers.
- The same module serves the **wake-up** of a low-power application: an alarm or the periodic wake-up timer wakes the
  MCU up from Stop and Standby, a callback reports the event after the wake-up.
- The module offers the **backup registers** under one interface in all families (in some families they are a part of the
  RTC peripheral and the module accesses them itself, in the others they are in the tamper peripheral and the module
  accesses them through the Tamp module); the data in them survive the reset and the
  Standby mode, the application keeps its small non-volatile state there (a boot reason, a counter, a flag of a
  firmware update).
- The module hides the differences between the families: the registers of the calendar and of the prescalers, the
  clock sources, the number of the backup registers, the wake-up lines and the TrustZone aliases differ, the interface
  stays the same.

What the module is **not**: it does not select the oscillators of the backup domain by itself (the RCC module starts the
LSE / LSI / HSE that was chosen), it does not handle the **tamper detection** (the Tamp module does) and it does not
know time zones or leap seconds - the time is a continuous count of the calendar.

---

## Concept

The RTC works in the backup domain, which has its own write protection and its own reset. The module keeps three
things apart:

| Item | Meaning |
|---|---|
| **Clock source** | `rtc_ClkSource_t`: `RTC_CLK_SRC_LSE` (the 32.768 kHz crystal), `RTC_CLK_SRC_LSI`, `RTC_CLK_SRC_HSE_DIV` (the HSE divided by the RCC). Selected once; **it can be changed only by a reset of the backup domain** (hardware rule) which erases the calendar. |
| **Time state** | `RTC_TIME_VALID` - the calendar was running before and is untouched; `RTC_TIME_LOST` - the calendar was initialized by the module (default date `2000-01-01 00:00:00`) and the application has to set the real time. |
| **Calendar** | Date and time in the binary form of `rtc_DateTime_t`: year 2000 ... 2099, month, day, weekday (ISO: 1 = Monday ... 7 = Sunday), hour (24 h), minute, second, millisecond. |

Rules:

1. `Rtc_Init` unlocks the backup domain, checks the clock source and the calendar. If the RTC runs with the requested
   clock and the calendar is initialized, it **only adopts** the state (`Rtc_Get_TimeState` returns `RTC_TIME_VALID`)
   and keeps the alarms, the wake-up timer and the calendar untouched; with `ForceInit` it resets the backup domain and
   starts again. If the clock source is not ready within the timeout, the call returns an error and nothing is changed.
2. `Rtc_Set_DateTime` writes the calendar in the initialization mode: the calendar stops for the duration of the write (about
   two RTC clock periods + a synchronization, tens to hundreds of us) - the write itself costs a small time error, it is
   not accumulated. `Rtc_Get_DateTime` reads a **consistent** time (no torn read when the second changes).
3. The weekday is calculated by the module from the date (it cannot disagree with the date); an invalid date (30th of
   February, hour 24) is rejected before any register is written.
4. An alarm matches the fields that are not masked (`rtc_AlarmMask_t`): for example only the seconds (every minute), the
   time of the day (every day), the time of the day and the weekday (every week), or the full date. The alarm callback is
   called after the match; the alarm is repeated until it is switched off.
5. The wake-up timer is periodic. The module selects the clock of the timer: the resolution of about 0.5 ms for the
   periods up to about 32 s and 1 s above that (up to about 36 hours). The callback is called at every period.
6. The callbacks run in the interrupt of the RTC (after the wake-up from Stop mode); after the wake-up from Standby the
   MCU starts as after a reset and the application reads the cause (`Rtc_Get_WakeUpFlag`) and the time.
7. The backup registers are 32-bit words addressed by an index (`Rtc_Get_BackupRegisterCount` tells how many the
   device has); their content is untouched by `Rtc_Init`. On the families where they belong to the tamper peripheral the
   functions forward to `Tamp_Get_BackupRegister` / `Tamp_Set_BackupRegister`; the RTC module never touches the registers of another peripheral.

---

## Supported families

Every family gets its own story, its own tasks and its own branches `Dev/<Family>` and `Releases/<Family>` cut from this
master.

| Family | Branch | RTC | Backup registers | Status |
|---|---|---|---|---|
| STM32H5 | `Dev/STM32H5` | `RTC` + `TAMP`, calendar and binary mode, two alarms, 17-bit wake-up timer, timestamp, 0.95 ppm digital calibration, reference clock input, TrustZone and privilege protection | 32 (in `TAMP`) | planned (first) |
| STM32U5 | `Dev/STM32U5` | `RTC` + `TAMP`, same IP generation as the H5 | 32 (in `TAMP`) | planned |
| STM32L4, STM32G4 | `Dev/STM32L4`, `Dev/STM32G4` | `RTC` | 32 (in `RTC`) | planned |
| STM32H7, STM32F7 | `Dev/STM32H7`, `Dev/STM32F7` | `RTC` | 32 (in `RTC`) | planned |
| STM32F4 | `Dev/STM32F4` | `RTC` | 20 (in `RTC`) | planned |
| STM32U3 | `Dev/STM32U3` | `RTC` + `TAMP` | device dependent | planned |

A function without a meaning in a family stays in the interface, returns `RTC_REQUEST_ERROR` and suppresses the unused
parameters (for example the binary mode on a family that does not have it, the reference clock input, the second alarm
mask bits that the hardware does not have).

---

## Public interface in short (proposal)

| Group | Functions |
|---|---|
| Module management | `Rtc_Get_ModuleVersion`, `Rtc_Init`, `Rtc_Deinit`, `Rtc_Task`, `Rtc_Get_DefaultConfig` |
| Time | `Rtc_Set_DateTime`, `Rtc_Get_DateTime`, `Rtc_Set_UnixTime`, `Rtc_Get_UnixTime`, `Rtc_Get_TimeState`, `Rtc_Set_DaylightSaving` |
| Alarms | `Rtc_Set_Alarm`, `Rtc_Set_AlarmActive`, `Rtc_Set_AlarmInactive`, `Rtc_Get_AlarmState` |
| Wake-up timer | `Rtc_Set_WakeUpTimer`, `Rtc_Set_WakeUpTimerActive`, `Rtc_Set_WakeUpTimerInactive`, `Rtc_Get_WakeUpFlag`, `Rtc_Set_WakeUpFlagClear` |
| Timestamp | `Rtc_Set_TimestampConfig`, `Rtc_Set_TimestampActive`, `Rtc_Set_TimestampInactive`, `Rtc_Get_Timestamp` |
| Calibration | `Rtc_Set_Calibration`, `Rtc_Get_Calibration`, `Rtc_Set_TimeShift`, `Rtc_Set_ReferenceClockActive`, `Rtc_Set_ReferenceClockInactive` |
| Output | `Rtc_Set_Output` |
| Backup registers | `Rtc_Get_BackupRegisterCount`, `Rtc_Set_BackupRegister`, `Rtc_Get_BackupRegister` |
| Interrupts | `Rtc_Set_IrqPriority`, `Rtc_Get_IrqPriority` |
| Pins | `Rtc_InitTimestampGpio`, `Rtc_InitOutputGpio`, `Rtc_InitRefInGpio` |

```c
rtc_ModuleVersion_t Rtc_Get_ModuleVersion        ( void );

rtc_RequestState_t  Rtc_Init                     ( const rtc_Config_t * const rtcConfig );
rtc_RequestState_t  Rtc_Deinit                   ( void );
void                Rtc_Task                     ( void );
rtc_RequestState_t  Rtc_Get_DefaultConfig        ( rtc_Config_t * const rtcConfig );

rtc_RequestState_t  Rtc_Set_DateTime             ( const rtc_DateTime_t * const dateTime );
rtc_RequestState_t  Rtc_Get_DateTime             ( rtc_DateTime_t * const dateTime );
rtc_RequestState_t  Rtc_Set_UnixTime             ( rtc_UnixTime_t unixTime );
rtc_RequestState_t  Rtc_Get_UnixTime             ( rtc_UnixTime_t * const unixTime );
rtc_RequestState_t  Rtc_Get_TimeState            ( rtc_TimeState_t * const timeState );
rtc_RequestState_t  Rtc_Set_DaylightSaving       ( rtc_DaylightSaving_t daylightSaving );

rtc_RequestState_t  Rtc_Set_Alarm                ( rtc_AlarmId_t alarmId, const rtc_AlarmConfig_t * const alarmConfig );
rtc_RequestState_t  Rtc_Set_AlarmActive          ( rtc_AlarmId_t alarmId );
rtc_RequestState_t  Rtc_Set_AlarmInactive        ( rtc_AlarmId_t alarmId );
rtc_RequestState_t  Rtc_Get_AlarmState           ( rtc_AlarmId_t alarmId, rtc_FunctionState_t * const alarmState );

rtc_RequestState_t  Rtc_Set_WakeUpTimer          ( rtc_Period_ms_t period, rtc_Callback_t * const callback );
rtc_RequestState_t  Rtc_Set_WakeUpTimerActive    ( void );
rtc_RequestState_t  Rtc_Set_WakeUpTimerInactive  ( void );

rtc_RequestState_t  Rtc_Set_Calibration          ( rtc_Calibration_ppb_t calibration );
rtc_RequestState_t  Rtc_Get_Calibration          ( rtc_Calibration_ppb_t * const calibration );
rtc_RequestState_t  Rtc_Set_TimeShift            ( rtc_Shift_ms_t shift );

rtc_RequestState_t  Rtc_Get_BackupRegisterCount  ( rtc_BackupCnt_t * const registerCnt );
rtc_RequestState_t  Rtc_Set_BackupRegister       ( rtc_BackupIndex_t index, rtc_BackupData_t data );
rtc_RequestState_t  Rtc_Get_BackupRegister       ( rtc_BackupIndex_t index, rtc_BackupData_t * const data );
```

Every function returns `RTC_REQUEST_OK` / `RTC_REQUEST_ERROR` and checks its parameters and the state of the module
before it touches a register.

| Type | Meaning |
|---|---|
| `rtc_Config_t` | Clock source, `ForceInit`, the date and time written when the calendar is lost (optional), interrupt priority |
| `rtc_DateTime_t` | `Year` (2000 ... 2099), `Month`, `Day`, `WeekDay`, `Hour`, `Minute`, `Second`, `Millisecond` |
| `rtc_UnixTime_t` | Seconds since `1970-01-01 00:00:00` (32-bit, the calendar range is limited to 2099) |
| `rtc_AlarmConfig_t` | Time of the day, day of the month or weekday, `Mask` (`RTC_ALARM_MASK_SECONDS` ... `RTC_ALARM_MASK_DAY`), callback |
| `rtc_TimeState_t` | `RTC_TIME_VALID`, `RTC_TIME_LOST` |
| `rtc_Calibration_ppb_t` | Correction of the clock in parts per billion (the resolution of the hardware is about 1 ppm) |
| `rtc_BackupData_t` | 32-bit data of a backup register |

---

## Usage

### Start-up of an application that keeps the time over resets

```c
#include "Rtc_Port.h"

rtc_Config_t    rtcConfig;
rtc_TimeState_t timeState;

(void)Rtc_Get_DefaultConfig( &rtcConfig );
rtcConfig.ClkSrc = RTC_CLK_SRC_LSE;

(void)Rtc_Init( &rtcConfig );                      /* does not touch a calendar that is running */
(void)Rtc_Get_TimeState( &timeState );

if( RTC_TIME_LOST == timeState )
{
    /* the battery was disconnected: ask for the time (GNSS, network, user) and set it */
    const rtc_DateTime_t now = { 2026u, 10u, 17u, RTC_WEEKDAY_SATURDAY, 15u, 32u, 0u, 0u };

    (void)Rtc_Set_DateTime( &now );
}
else
{
    /* The calendar is running, nothing to set */
}
```

### Alarm every day at 06:30

```c
static void App_Alarm( void )
{
    /* called from the interrupt, also after the wake-up from Stop mode */
}

const rtc_AlarmConfig_t morning =
{
    .Hour = 6u, .Minute = 30u, .Second = 0u,
    .Mask = RTC_ALARM_MASK_DAY,                    /* the date and the weekday do not matter */
    .Callback = App_Alarm,
};

(void)Rtc_Set_Alarm( RTC_ALARM_A, &morning );
(void)Rtc_Set_AlarmActive( RTC_ALARM_A );
```

### Periodic wake-up from Stop mode and the persistent counter

```c
rtc_BackupData_t bootCnt = 0u;

(void)Rtc_Set_WakeUpTimer( 5000u, App_Tick );      /* every 5 s */
(void)Rtc_Set_WakeUpTimerActive();

(void)Rtc_Get_BackupRegister( 0u, &bootCnt );
(void)Rtc_Set_BackupRegister( 0u, bootCnt + 1u );  /* survives the reset and the Standby mode */
```

---

## Behaviour and limits

- **Clock source and reset.** The source is selected once; a change needs a reset of the backup domain and loses the
  calendar and the backup registers of the families where the reset erases them. `Rtc_Init` with a different source than
  the running one returns an error unless `ForceInit` is set.
- **LSE start-up.** The crystal needs hundreds of ms to seconds to start; the wait is bounded by the timeout of the RCC
  module and reported as an error. The LSI works without a crystal but its accuracy (a few percent) is not suited for a
  calendar - use it only for a rough time keeping or as a fallback chosen by the application.
- **Accuracy.** The accuracy is the one of the crystal; `Rtc_Set_Calibration` corrects a constant error (about 1 ppm
  steps), `Rtc_Set_TimeShift` adjusts the time on the fly by a small offset (synchronization with a master clock).
- **Consistent read.** The time is read through the shadow registers in the order that locks them; a read at the second
  change does not return a torn value. The first read after the wake-up from Stop waits for the synchronization.
- **Alarms and wake-up.** The wake-up of the MCU from Stop and Standby is configured by the module (the wake-up line /
  EXTI line of the family). A callback after the wake-up from Standby does not exist - the MCU is reset, the cause is
  read by `Rtc_Get_WakeUpFlag`.
- **Backup registers.** They are in the backup domain: untouched by the system reset, erased by the reset of the backup
  domain and (on the families with the tamper peripheral) by a tamper event. Do not use them for the data that must
  survive a tamper.
- **Not part of the first version:** the binary and the mixed mode of the new RTC IP, the TrustZone / privilege
  configuration, the tamper detection and the timestamp by a tamper event (Tamp module), the RTC output as a trigger
  of other peripherals, the sub-second alarm mask, the daylight saving automation (only the one hour add / subtract bit).

---

## Dependencies

- **Rcc** - selection and start of the LSE / LSI / HSE, enable of the RTC clock, reset of the backup domain
- **Pwr** - the write access to the backup domain (reference counted)
- **Tamp** - the backup registers on the families where they are a part of the tamper peripheral
- **Exti** - wake-up lines of the alarms, the wake-up timer and the timestamp on the families that route them through the EXTI
- **Nvic** - RTC interrupt
- **Gpio** - timestamp, output and reference clock pins
- **RAL (Register Abstraction Layer)** - used internally to access the registers

All mandatory RCC, PWR, EXTI, NVIC and GPIO configurations are handled internally.

---

## Testing

- **Unit tests** (host, Unity + CMock + RegMem): the conversion between the date, the BCD registers and the Unix time
  (leap years, the end of the month and of the year, 2099), the validation of the dates, the prescaler calculation for
  the clock sources, the adoption of a running RTC (no write) and the initialization of a lost calendar, the alarm masks, the
  wake-up timer clock selection, the calibration value, the backup register access, the read-back and the timeouts of the
  initialization mode.
- **Integration tests** (target, probe-rs): the time keeping against a timer for several minutes, the survival of the time and
  of the backup registers over a reset, the alarm and the wake-up timer callbacks, the wake-up from Stop mode, the
  calibration measured on the output pin.

---

## Interface conventions

The module follows the common rules of the MCAL layer, so that it behaves like the other modules:

- **Lists instead of free members.** Everything that the hardware restricts (the pins, the kernel clock sources, the DMA
  channels, the triggers and the other selectable sources) is a `typedef enum` list with exactly the items that are valid
  for the device line; a pin is an item `RTC_<SIGNAL>_PIN_<INSTANCE>_P<port><pin>` and there is an `*_UNUSED` item. A choice
  that is not in the list cannot be written.
- **Own peripheral only.** The module calls only the registers / LL functions of its own peripheral. The clocks, the
  interrupts, the pins, the DMA and the EXTI lines go through the API of the Rcc, Nvic, Gpio, Gpdma and Exti modules; a
  missing function is added to that module, it is not bypassed.
- **Init verifies itself.** `Rtc_Init` checks the result at the end; when it is not as required, the module is
  deinitialized and an error is returned. The waiting for the hardware is a loop on the register state with a timeout,
  never a blind delay.
- **Transfer modes.** Where the module moves data, the POLL (served by `Rtc_Task`), ISR and DMA modes have the same
  configuration structure, the same request and the same callbacks (error, half transfer, transfer complete).
- **Flags and states are enums of the module** (`rtc_FunctionState_t`, `rtc_FlagState_t`), the limits and the constants are
  named, there are no magic numbers in the interface.
- **Unsupported functions stay.** A function without a meaning in a family keeps its signature, suppresses the unused
  parameters and returns `RTC_REQUEST_ERROR`.
- **Documentation.** Every function and type has a Doxygen description, the main header has the `\defgroup`.

---

## Repository layout and branches

The master holds this README and the license. The family branches (`Dev/<Family>`, `Releases/<Family>`) hold:

| Path | Content |
|---|---|
| `README.md` | README of the family: the instances of the device, specifics, errata, status |
| `Rtc_Port.h`, `Rtc_Types.h` | Public interface (identical in all families) |
| `Rtc*.c`, `Rtc.h` | Implementation of the family |
| `CMakeLists.txt` | Library `Rtc_Lib` of the EmBi platform |
| `Tests/UnitTests` | Unit tests (Unity + CMock + RegMem) |
| `Tests/IntegrationTests` | Integration tests on the target (probe-rs) |

A change of the public interface is made in all families.

---

## CMake integration

```cmake
target_link_libraries( User_Lib PRIVATE Rtc_Lib )
```

1. Link `Rtc_Lib` to your CMake library.
2. Include `Rtc_Port.h` in your project.
3. Configure the module as needed for your hardware.

---

## License

This project is licensed under the **Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**.

You are free to use, modify, and share this work for **non-commercial purposes**, provided appropriate credit is given.

See [LICENSE.md](LICENSE.md) for full terms or visit [creativecommons.org/licenses/by-nc/4.0](https://creativecommons.org/licenses/by-nc/4.0/).

---

## Authors

- **Mr.Nobody** - [embedbits.com](https://embedbits.com)

Contributions are welcome! Please open a pull request.
