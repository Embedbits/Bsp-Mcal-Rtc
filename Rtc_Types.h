/**
 * \author Mr.Nobody
 * \file Rtc_Types.h
 * \ingroup Rtc
 * \brief Rtc module global types definition
 *
 * This file contains the types definitions used across the module and are
 * available for other modules through the Port file.
 *
 * Pin tables (\ref rtc_OutputPin_t, \ref rtc_RefInPin_t) were generated from the STM32CubeMX
 * database (STM32H503..STM32H5F5 MCU files, GPIO IP Modes XML for the AF of every pin) by a
 * small generator script modeled on the project's other pin-table generators, never
 * hand-written. RTC_OUT1 and RTC_TS exist only on PC13 with no GPIO_AF entry at all in the
 * same database (checked), so they are not generated as an AF-mux enum - see the module
 * README deviation note.
 *
 */

/**
 * \defgroup Rtc Rtc
 * \brief Rtc module
 */

#ifndef RTC_RTC_TYPES_H
#define RTC_RTC_TYPES_H
/* ============================== INCLUDES ================================== */
#include "stdint.h"                         /* Module types definition        */
/* ========================== SYMBOLIC CONSTANTS ============================ */

/** Null pointer definition */
#define RTC_NULL_PTR                          ( ( void* ) 0u )

/** First calendar year represented by \ref rtc_DateTime_t::Year (hardware BCD year 00) */
#define RTC_YEAR_BASE                         ( 2000u )

/** Last calendar year representable by the hardware BCD year register (99) */
#define RTC_YEAR_MAX                          ( 2099u )

/* ========================== EXPORTED MACROS =============================== */

/* ============================== TYPEDEFS ================================== */

/** \brief Type signaling major version of SW module */
typedef uint8_t rtc_MajorVersion_t;

/** \brief Type signaling minor version of SW module */
typedef uint8_t rtc_MinorVersion_t;

/** \brief Type signaling patch version of SW module */
typedef uint8_t rtc_PatchVersion_t;

/** \brief Type signaling actual version of SW module */
typedef struct
{
    rtc_MajorVersion_t Major; /**< Major version */
    rtc_MinorVersion_t Minor; /**< Minor version */
    rtc_PatchVersion_t Patch; /**< Patch version */
}   rtc_ModuleVersion_t;


/** Enumeration used to signal request processing state */
typedef enum
{
    RTC_REQUEST_ERROR = 0u, /**< Processing request failed  */
    RTC_REQUEST_OK          /**< Processing request succeed */
}   rtc_RequestState_t;


/** Function status enumeration */
typedef enum
{
    RTC_FUNCTION_INACTIVE = 0u, /**< Function status is inactive */
    RTC_FUNCTION_ACTIVE         /**< Function status is active   */
}   rtc_FunctionState_t;


/** Flag states enumeration */
typedef enum
{
    RTC_FLAG_INACTIVE = 0u, /**< Inactive flag state */
    RTC_FLAG_ACTIVE         /**< Active flag state   */
}   rtc_FlagState_t;


/** \brief Interrupt priority type definition */
typedef uint32_t rtc_IrqPrio_t;


/** \brief Clock source of the RTC / backup domain. Written once per backup domain life
 *  (RM0481 "the RTCSEL bits of the RCC_BDCR register are write protected once set, a reset
 *  of the backup domain, RCC_BDCR.BDRST, is required to change them"). */
typedef enum
{
    RTC_CLK_SRC_LSE = 0u, /**< LSE (32.768 kHz crystal), the accurate source           */
    RTC_CLK_SRC_LSI,      /**< LSI, no crystal needed, a few percent accuracy          */
    RTC_CLK_SRC_HSE_DIV,  /**< HSE divided by the RCC HSE-to-RTC prescaler (1 MHz typ) */
    RTC_CLK_SRC_CNT       /**< Count of available clock sources                        */
}   rtc_ClkSource_t;


/** \brief Whether the calendar found at \ref Rtc_Init was already running (adopted, untouched)
 *  or had to be initialized (lost, set to the default date). */
typedef enum
{
    RTC_TIME_VALID = 0u, /**< The calendar was running before and was adopted as is */
    RTC_TIME_LOST        /**< The calendar was not running, it was (re)initialized  */
}   rtc_TimeState_t;


/** \brief ISO-8601 weekday (1 = Monday ... 7 = Sunday). */
typedef enum
{
    RTC_WEEKDAY_MONDAY = 1u, /**< Monday    */
    RTC_WEEKDAY_TUESDAY,     /**< Tuesday   */
    RTC_WEEKDAY_WEDNESDAY,   /**< Wednesday */
    RTC_WEEKDAY_THURSDAY,    /**< Thursday  */
    RTC_WEEKDAY_FRIDAY,      /**< Friday    */
    RTC_WEEKDAY_SATURDAY,    /**< Saturday  */
    RTC_WEEKDAY_SUNDAY       /**< Sunday    */
}   rtc_WeekDay_t;


/** \brief Calendar year (\ref RTC_YEAR_BASE .. \ref RTC_YEAR_MAX). */
typedef uint16_t rtc_Year_t;


/** \brief Calendar month (1 .. 12). */
typedef uint8_t rtc_Month_t;


/** \brief Day of the month (1 .. 31), in an alarm configuration also the weekday (\ref rtc_WeekDay_t). */
typedef uint8_t rtc_Day_t;


/** \brief Hour of the day (0 .. 23, 24-hour format only). */
typedef uint8_t rtc_Hour_t;


/** \brief Minute of the hour (0 .. 59). */
typedef uint8_t rtc_Minute_t;


/** \brief Second of the minute (0 .. 59). */
typedef uint8_t rtc_Second_t;


/** \brief Millisecond of the second (0 .. 999). */
typedef uint16_t rtc_Millisecond_t;


/**
 * \brief Calendar date and time in binary form (year 2000 .. 2099).
 *
 * \note The WeekDay field is calculated by the module from the date on a write and reported back on a read.
 */
typedef struct
{
    rtc_Year_t        Year;        /**< RTC_YEAR_BASE (2000) .. RTC_YEAR_MAX (2099)    */
    rtc_Month_t       Month;       /**< 1 .. 12                                        */
    rtc_Day_t         Day;         /**< 1 .. 31 (checked against Month / leap year)    */
    rtc_WeekDay_t     WeekDay;     /**< Calculated by the module, see the note above   */
    rtc_Hour_t        Hour;        /**< 0 .. 23 (24-hour format only)                  */
    rtc_Minute_t      Minute;      /**< 0 .. 59                                        */
    rtc_Second_t      Second;      /**< 0 .. 59                                        */
    rtc_Millisecond_t Millisecond; /**< 0 .. 999, derived from the sub-second register */
}   rtc_DateTime_t;


/** \brief Seconds since 1970-01-01 00:00:00 (the calendar range is limited to year 2099). */
typedef uint32_t rtc_UnixTime_t;


/** \brief One hour daylight saving adjustment of the calendar (RTC_CR.ADD1H / SUB1H). */
typedef enum
{
    RTC_DST_NONE = 0u, /**< No adjustment                           */
    RTC_DST_ADD_1H,    /**< Add one hour (daylight saving in)       */
    RTC_DST_SUB_1H     /**< Subtract one hour (daylight saving out) */
}   rtc_DaylightSaving_t;


/** \brief Alarm identification (RTC_ALARM_A / RTC_ALARM_B, STM32H5 has two). */
typedef enum
{
    RTC_ALARM_A = 0u, /**< Alarm A                   */
    RTC_ALARM_B,      /**< Alarm B                   */
    RTC_ALARM_CNT     /**< Count of available alarms */
}   rtc_AlarmId_t;


/** \brief Alarm field mask: a set bit means the field is NOT compared (matches every value of
 *  that field), mirroring the hardware MSKx bits. OR the needed bits together. */
typedef enum
{
    RTC_ALARM_MASK_NONE    = 0x00u, /**< Every field compared: full date + time match      */
    RTC_ALARM_MASK_SECONDS = 0x01u, /**< Seconds not compared (match every second)         */
    RTC_ALARM_MASK_MINUTES = 0x02u, /**< Minutes not compared                              */
    RTC_ALARM_MASK_HOURS   = 0x04u, /**< Hours not compared                                */
    RTC_ALARM_MASK_DAY     = 0x08u, /**< Day of month / weekday not compared (every day)   */
    RTC_ALARM_MASK_WEEKDAY = 0x10u  /**< The Day field is interpreted as weekday, not date */
}   rtc_AlarmMask_t;


/** \brief Alarm callback, called from the RTC interrupt after the alarm matches. */
typedef void ( rtc_Callback_t )( void );


/**
 * \brief Alarm configuration (time of day plus the optional day / weekday match).
 *
 * \note The Day field (day of month or weekday, \ref rtc_WeekDay_t) is matched unless masked by
 *       \ref RTC_ALARM_MASK_DAY.
 */
typedef struct
{
    rtc_Day_t       Day;      /**< Day of month (1..31) or weekday, see the note above  */
    rtc_Hour_t      Hour;     /**< 0 .. 23                                              */
    rtc_Minute_t    Minute;   /**< 0 .. 59                                              */
    rtc_Second_t    Second;   /**< 0 .. 59                                              */
    rtc_AlarmMask_t Mask;     /**< Fields not compared, OR of \ref rtc_AlarmMask_t bits */
    rtc_Callback_t *Callback; /**< \ref RTC_NULL_PTR disables the callback (flag only)  */
}   rtc_AlarmConfig_t;


/** \brief Wake-up timer period in milliseconds (about 0.5 ms steps up to ~32 s, 1 s steps up
 *  to ~36 hours, see \ref Rtc_Set_WakeUpTimer). */
typedef uint32_t rtc_Period_ms_t;


/** \brief Digital calibration correction in parts per billion (hardware resolution ~954 ppb,
 *  i.e. 1 pulse of CALP/CALM per 2^20 RTCCLK pulses, see RM0481 RTC_CALR). */
typedef int32_t rtc_Calibration_ppb_t;


/** \brief Smooth calendar time shift in milliseconds (negative: subtract, 0 .. +1 s add,
 *  see \ref Rtc_Set_TimeShift and RTC_SHIFTR). */
typedef int32_t rtc_Shift_ms_t;


/** \brief Timestamp source edge. */
typedef enum
{
    RTC_TS_EDGE_RISING = 0u, /**< Timestamp captured on the rising edge  */
    RTC_TS_EDGE_FALLING      /**< Timestamp captured on the falling edge */
}   rtc_TimestampEdge_t;


/** \brief Timestamp capture configuration (RTC_TS pin, not implemented in this version - see
 *  the module README deviation note; the signature is kept, \ref Rtc_Set_TimestampConfig
 *  returns \ref RTC_REQUEST_ERROR). */
typedef struct
{
    rtc_TimestampEdge_t Edge; /**< Active edge of the RTC_TS pin */
}   rtc_TimestampConfig_t;


/**
 * \brief RTC_OUT2 pin table (RTC_OUT1 on PC13 is not represented - see the module README
 *        deviation note; this item selects the calibration / alarm output function on PB2,
 *        additionally on PA0 on STM32H503).
 */
typedef enum
{
#if defined(STM32H503xx)
    RTC_OUT2_PIN_PA0 = 0u, /**< RTC_OUT2 on PA0 */
#endif
    RTC_OUT2_PIN_PB2,      /**< RTC_OUT2 on PB2 */
    RTC_OUT2_PIN_UNUSED    /**< No pin assigned */
}   rtc_OutputPin_t;


/** \brief RTC_REFIN pin table (reference clock input, used to fine-calibrate the LSE). */
typedef enum
{
    RTC_REFIN_PIN_PB15 = 0u, /**< RTC_REFIN on PB15 */
    RTC_REFIN_PIN_UNUSED     /**< No pin assigned   */
}   rtc_RefInPin_t;


/** \brief RTC_OUT2 selectable output signal. */
typedef enum
{
    RTC_OUTPUT_DISABLED = 0u, /**< RTC_OUT2 not driven by the RTC  */
    RTC_OUTPUT_ALARM,         /**< Alarm pulse on RTC_OUT2         */
    RTC_OUTPUT_CALIBRATION,   /**< 512 Hz / 1 Hz calibration clock */
    RTC_OUTPUT_WAKEUP         /**< Wake-up timer event             */
}   rtc_OutputSignal_t;


/**
 * \brief RTC_OUT2 configuration.
 *
 * \note \ref RTC_OUT2_PIN_UNUSED in the Pin field disables the output function as a whole
 *       (CR.OSEL / COE / OUT2EN all cleared), regardless of the Signal field: RTC_OUT1 / PC13 is
 *       not supported, so there is no pin left to route the signal to.
 */
typedef struct
{
    rtc_OutputPin_t    Pin;    /**< Pin of the output, see the note above                             */
    rtc_OutputSignal_t Signal; /**< Signal routed to the pin, ignored when Pin is RTC_OUT2_PIN_UNUSED */
}   rtc_OutputConfig_t;


/** \brief Backup register index (0 .. \ref Rtc_Get_BackupRegisterCount - 1). Forwarded to the
 *  Tamp module on STM32H5 (the backup registers physically belong to TAMP on this family). */
typedef uint8_t rtc_BackupIndex_t;


/** \brief Backup register count type. */
typedef uint8_t rtc_BackupCnt_t;


/** \brief 32-bit data of a backup register. */
typedef uint32_t rtc_BackupData_t;


/**
 * \brief Full module configuration used by \ref Rtc_Init.
 *
 * \note RTC_FUNCTION_ACTIVE in the ForceInit field resets the backup domain and reinitializes the
 *       calendar even if one already runs with a different clock source.
 */
typedef struct
{
    rtc_ClkSource_t     ClkSrc;          /**< Requested clock source of the backup domain  */
    rtc_FunctionState_t ForceInit;       /**< Forced reinitialization, see the note above  */
    rtc_DateTime_t      DefaultDateTime; /**< Written only when the calendar is found lost */
    rtc_IrqPrio_t       IrqPriority;     /**< RTC interrupt priority (0 - highest)         */
}   rtc_Config_t;

/* ========================== EXPORTED VARIABLES ============================ */

/* ========================= EXPORTED FUNCTIONS ============================= */


#endif /* RTC_RTC_TYPES_H */
