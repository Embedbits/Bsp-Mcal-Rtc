/**
 * \author Mr.Nobody
 * \file Rtc.c
 * \ingroup Rtc
 * \brief Rtc module main functionality (STM32H5).
 *
 * Implements \ref Rtc_Port.h against the RTC peripheral registers (via \ref Rtc_Reg.h), the
 * clock source (via the Rcc module), the backup registers (forwarded to the Tamp module, see
 * below), the pins (via the Gpio module) and the interrupt (via the Nvic module).
 *
 * Dependencies on other modules, never a direct register write of another peripheral:
 * - **Pwr** gives the backup domain write access (PWR_DBPCR.DBP): \ref Rtc_Init requests it
 *   through \ref Pwr_Set_BackupAccessActive before the first backup domain write and holds it
 *   while the module is initialized (every function that writes an RTC register requires an
 *   initialized module); \ref Rtc_Deinit releases it through \ref Pwr_Set_BackupAccessInactive.
 *   The access of Pwr is reference counted, so the request of this module and the one of the
 *   Tamp module do not cancel each other.
 * - One dependency named by the module card is not available in this checkout:
 * - **Rcc** exposes \ref Rcc_Set_RtcClkSource / \ref Rcc_Get_RtcClkSource and the
 *   \c RCC_PERIPH_RTC_* periph ids but no backup-domain-reset (RCC_BDCR.BDRST) function, so
 *   \ref rtc_Config_t::ForceInit cannot actually change an already-selected RTCSEL (hardware
 *   write-once-per-backup-domain-life rule, RM0481 11.7.4): when the requested clock source
 *   differs from the one Rcc reports as active, \ref Rtc_Init returns \ref RTC_REQUEST_ERROR
 *   without writing anything, ForceInit or not. Tracked as a missing Rcc API item.
 *
 * Backup registers: on STM32H5 they physically belong to TAMP (RM0481 56.3), so
 * \ref Rtc_Get_BackupRegisterCount / \ref Rtc_Set_BackupRegister / \ref Rtc_Get_BackupRegister
 * forward to \ref Tamp_Get_BackupRegisterCount / \ref Tamp_Set_BackupRegister /
 * \ref Tamp_Get_BackupRegister, matching the README rule ("the RTC module never touches the
 * registers of another peripheral").
 *
 * Implemented in this version: Init / Deinit / Get_DefaultConfig (clock source adoption or
 * initialization of a lost calendar, never a blind reset of a running one), calendar set / get,
 * Unix time conversion, daylight saving, alarms A and B, the periodic wake-up timer (1 Hz
 * resolution, see the deviation below), timestamp capture (register side only, see below),
 * digital calibration, time shift, the RTC_OUT2 output (alarm / calibration / wake-up) and the
 * RTC_REFIN reference clock input, backup register forwarding, interrupt priority.
 *
 * Not implemented in this version (see the module report for the exact reason):
 * - \ref Rtc_InitTimestampGpio always returns \ref RTC_REQUEST_ERROR : RTC_TS (and RTC_OUT1)
 *   exist only on PC13 with no GPIO_AF entry at all in the STM32CubeMX database (checked), so
 *   no AF-mux pin table can be generated for it (the same situation the Tamp module hit with
 *   its fixed pins) - the register side of the timestamp (\ref Rtc_Set_TimestampConfig and the
 *   rest) is implemented and usable once a future GPIO story clarifies how PC13 is driven.
 * - The wake-up timer always uses the ck_spre (1 Hz) clock (RTC_CR_WUCKSEL_2): the sub-second
 *   (~0.5 ms) resolution the README mentions (RTC_CR_WUCKSEL 110/111, 17-bit extended counter)
 *   is not implemented; \ref Rtc_Set_WakeUpTimer accepts whole seconds only (truncated).
 * - EXTI wake-up line configuration (waking the MCU from Stop / Standby through the EXTI
 *   module) is not wired: the interrupt is only enabled at the NVIC, not routed through Exti.
 *   Tracked as missing in this report, the Exti module itself is not touched.
 * - The HSE-divided clock source assumes the conventional 1 MHz RTCCLK (PREDIV_A = 99,
 *   PREDIV_S = 9999); if Rcc configures a different HSE/RTCPRE divider the calendar will not
 *   tick at 1 Hz. Documented, not silently wrong.
 *
 */
/* ============================== INCLUDES ================================== */
#include "Rtc.h"                            /* Module private definitions     */
#include "Rtc_Port.h"                        /* Module public functionality    */
#include "Rtc_Reg.h"                         /* Register access                */
#include "Stm32.h"                           /* RTC bit field definitions      */
#include "Rcc_Port.h"                        /* Clock source selection         */
#include "Pwr_Port.h"                        /* Backup domain write access     */
#include "Tamp_Port.h"                       /* Backup register forwarding     */
#include "Nvic_Port.h"                       /* RTC interrupt                  */
#include "Gpio_Port.h"                       /* RTC_OUT2 / RTC_REFIN pins       */
/* ============================== TYPEDEFS ================================== */

/** \brief One row mapping \ref rtc_ClkSource_t to the Rcc clock source / periph id. */
typedef struct
{
    rtc_ClkSource_t      RtcSrc;   /**< This module's clock source item   */
    rcc_Rtc_ClkSource_t  RccSrc;   /**< Matching Rcc clock source item    */
    rcc_PeriphId_t       PeriphId; /**< Matching Rcc "RTC active with..." periph id */
}   rtc_ClkSourceMap_t;

/* ========================== SYMBOLIC CONSTANTS ============================ */

#define RTC_BCD_DIGIT_RADIX              ( 10u )
#define RTC_HOUR_CNT                     ( 24u )
#define RTC_MINUTE_CNT                   ( 60u )
#define RTC_SECOND_CNT                   ( 60u )
#define RTC_MILLISECOND_CNT              ( 1000u )
#define RTC_MONTH_CNT                    ( 12u )
#define RTC_UNIX_EPOCH_YEAR              ( 1970u )

/** \brief Days from 0000-03-01 civil calendar helper constant (Howard Hinnant's algorithm). */
#define RTC_CIVIL_EPOCH_OFFSET           ( 719468 )

/** \brief ck_spre (1 Hz) prescalers for the LSE / LSI clock sources (32768 Hz, 128 * 256). */
#define RTC_PREDIV_A_32K                 ( 127u )
#define RTC_PREDIV_S_32K                 ( 255u )

/** \brief ck_spre (1 Hz) prescalers assumed for the HSE-divided clock source (1 MHz, 100 * 10000). */
#define RTC_PREDIV_A_1M                  ( 99u )
#define RTC_PREDIV_S_1M                  ( 9999u )

/** \brief Digital calibration: one CALP pulse adds 488.5 ppm, scaled to parts per billion. */
#define RTC_CALIB_CALP_PPB               ( 488500 )

/** \brief Digital calibration: one CALM LSB subtracts 0.954 ppm, scaled to parts per billion. */
#define RTC_CALIB_CALM_PPB               ( 954 )

/** \brief Maximum value of the CALM field (9 bits). */
#define RTC_CALIB_CALM_MAX               ( 511u )

/** \brief Milliseconds in one second, used by the time-shift conversion. */
#define RTC_SHIFT_MS_PER_S               ( 1000 )

/** \brief Bounded wait loop limit used for register-ready polling (ALRAWF, ALRBWF, WUTWF,
 *  SHPF, RECALPF). Register polling only, generous for the slowest supported RTC clock. */
#define RTC_WAIT_LIMIT                   ( 1000000u )

/** \brief Bounded retry limit of the consistent (non-torn) calendar read performed by
 *  \ref Rtc_Get_DateTime. The second can roll over at most once between two reads, a handful
 *  of retries is always enough to see two consecutive reads agree. */
#define RTC_DATETIME_READ_RETRY_LIMIT     ( 5u )

/** \brief Register value without any bit set (local alias of Rtc_Reg.c's private constant). */
#define RTC_REG_BITS_CLEAR               ( 0u )

/** \brief Gregorian leap year rule divisors. */
#define RTC_LEAP_YEAR_DIV_4               ( 4u )
#define RTC_LEAP_YEAR_DIV_100             ( 100u )
#define RTC_LEAP_YEAR_DIV_400             ( 400u )

/** \brief Calendar field range limits used by \ref Rtc_ValidateDateTime. */
#define RTC_MONTH_MIN                     ( 1u )
#define RTC_MONTH_FEBRUARY                ( 2u )
#define RTC_DAY_MIN                       ( 1u )

/** \brief Default interrupt priority (lowest) filled by \ref Rtc_Get_DefaultConfig. */
#define RTC_IRQ_PRIO_DEFAULT              ( 15u )

/** \brief Register value with every bit set (full-width register writes / reads). */
#define RTC_REG_ALL_BITS                  ( 0xFFFFFFFFu )

/** \brief Bits in one BCD digit (nibble) and the BCD digit radix / byte split shift. */
#define RTC_BCD_NIBBLE_SHIFT              ( 4u )
#define RTC_BCD_NIBBLE_MASK               ( 0x0Fu )

/** \brief Howard Hinnant civil-calendar algorithm constants (days_from_civil /
 *  civil_from_days), see Rtc_DaysFromCivil / Rtc_CivilFromDays. */
#define RTC_CIVIL_MONTH_ADJUST            ( 2u )
#define RTC_CIVIL_MONTH_OFFSET_EARLY      ( 9u )
#define RTC_CIVIL_MONTH_OFFSET_LATE       ( 3u )
#define RTC_CIVIL_ERA_DAYS                ( 146097 )
#define RTC_CIVIL_ERA_YEARS               ( 400 )
#define RTC_CIVIL_ERA_YEAR_ADJUST         ( 399 )
#define RTC_CIVIL_DOY_NUM                 ( 153u )
#define RTC_CIVIL_DOY_ADD                 ( 2u )
#define RTC_CIVIL_DOY_DIV                 ( 5u )
#define RTC_CIVIL_DOE_YEAR_DAYS           ( 365u )
#define RTC_CIVIL_DOE_DIV4                ( 4u )
#define RTC_CIVIL_DOE_DIV100              ( 100u )
#define RTC_CIVIL_YOE_DIV1460             ( 1460u )
#define RTC_CIVIL_YOE_DIV36524            ( 36524u )
#define RTC_CIVIL_YOE_DIV146096           ( 146096u )
#define RTC_CIVIL_MP_THRESHOLD            ( 10u )
#define RTC_CIVIL_MP_OFFSET_LATE          ( 3u )
#define RTC_CIVIL_MP_OFFSET_EARLY         ( 9u )

/** \brief Weekday formula constant (1970-01-01 is a Thursday, ISO day 4): days-since-epoch
 *  plus this offset, modulo 7, plus 1, gives the ISO weekday. */
#define RTC_WEEKDAY_EPOCH_OFFSET          ( 3 )
#define RTC_WEEKDAY_MODULO                ( 7u )

/** \brief Days in every month of a non-leap year (index 1 = January .. 12 = December). */
#define RTC_DAYS_JAN                      ( 31u )
#define RTC_DAYS_FEB                      ( 28u )
#define RTC_DAYS_MAR                      ( 31u )
#define RTC_DAYS_APR                      ( 30u )
#define RTC_DAYS_MAY                      ( 31u )
#define RTC_DAYS_JUN                      ( 30u )
#define RTC_DAYS_JUL                      ( 31u )
#define RTC_DAYS_AUG                      ( 31u )
#define RTC_DAYS_SEP                      ( 30u )
#define RTC_DAYS_OCT                      ( 31u )
#define RTC_DAYS_NOV                      ( 30u )
#define RTC_DAYS_DEC                      ( 31u )

/** \brief Maximum RTC_OUT2 / wake-up seconds value representable by the 16-bit WUTR register. */
#define RTC_WAKEUP_SECONDS_MAX           ( 0xFFFFu )

/* ========================== LOCAL VARIABLES ============================== */

/** \brief Module runtime state. */
static rtc_State_t rtc_State =
{
    .Initialized       = RTC_FUNCTION_INACTIVE,
    .BackupAccess      = RTC_FUNCTION_INACTIVE,
    .ClkSrc            = RTC_CLK_SRC_LSE,
    .TimeState         = RTC_TIME_LOST,
    .PredivS           = RTC_PREDIV_S_32K,
    .AlarmCallback     = { RTC_NULL_PTR, RTC_NULL_PTR },
    .WakeUpCallback    = RTC_NULL_PTR,
    .TimestampCallback = RTC_NULL_PTR,
    .IrqPriority       = 0u,
};

/** \brief Clock source lookup table (see \ref rtc_ClkSourceMap_t). */
static const rtc_ClkSourceMap_t rtc_ClkSourceList[] =
{
    { RTC_CLK_SRC_LSE,     RCC_RTC_CLK_SOURCE_LSE,     RCC_PERIPH_RTC_LSE     },
    { RTC_CLK_SRC_LSI,     RCC_RTC_CLK_SOURCE_LSI,     RCC_PERIPH_RTC_LSI     },
    { RTC_CLK_SRC_HSE_DIV, RCC_RTC_CLK_SOURCE_HSE_DIV, RCC_PERIPH_RTC_HSE_DIV32 },
};

/** \brief Days in every month of a non-leap year, indexed 1..12 (index 0 unused). */
static const uint8_t rtc_DaysInMonth[ RTC_MONTH_CNT + 1u ] =
{
    0u, RTC_DAYS_JAN, RTC_DAYS_FEB, RTC_DAYS_MAR, RTC_DAYS_APR, RTC_DAYS_MAY, RTC_DAYS_JUN,
    RTC_DAYS_JUL, RTC_DAYS_AUG, RTC_DAYS_SEP, RTC_DAYS_OCT, RTC_DAYS_NOV, RTC_DAYS_DEC
};

/* =========================== LOCAL FUNCTIONS (forward) ===================== */

static uint8_t             Rtc_Bin2Bcd                 ( uint8_t binVal );
static rtc_FlagState_t     Rtc_IsLeapYear               ( uint16_t year );
static rtc_RequestState_t  Rtc_ValidateDateTime         ( const rtc_DateTime_t * const dateTime );
static int32_t             Rtc_DaysFromCivil            ( uint16_t year, uint8_t month, uint8_t day );
static void                Rtc_CivilFromDays            ( int32_t days,
                                                          uint16_t * const year,
                                                          uint8_t * const month,
                                                          uint8_t * const day );
static rtc_WeekDay_t       Rtc_ComputeWeekDay            ( int32_t days );
static rtc_RequestState_t  Rtc_FindClkSourceByRtc        ( rtc_ClkSource_t rtcSrc,
                                                           const rtc_ClkSourceMap_t * * const row );
static void                Rtc_WriteCalendarRaw          ( const rtc_DateTime_t * const dateTime );
static void                Rtc_ReadCalendarRaw           ( rtc_DateTime_t * const dateTime );
static rtc_RequestState_t  Rtc_VerifyPrescaler            ( uint32_t predivA, uint32_t predivS );
static rtc_RequestState_t  Rtc_VerifyDateTimeWritten      ( const rtc_DateTime_t * const dateTime );
static rtc_RequestState_t  Rtc_WaitRegBitClear           ( rtc_RegId_t regId, uint32_t bitMask );
static rtc_RequestState_t  Rtc_WaitRegBitSet              ( rtc_RegId_t regId, uint32_t bitMask );
static rtc_RequestState_t  Rtc_WaitCalendarSync           ( void );
static uint32_t            Rtc_BuildAlarmReg              ( const rtc_AlarmConfig_t * const alarmConfig );
static void                Rtc_PollEvents                 ( void );
static rtc_RequestState_t  Rtc_BackupAccessAcquire         ( void );
static rtc_RequestState_t  Rtc_BackupAccessRelease         ( void );

/* ========================= EXPORTED FUNCTIONS ============================= */

/* --------------------------- Module management ---------------------------- */

/**
 * \brief Returns the module version.
 *
 * \return Module version
 */
rtc_ModuleVersion_t Rtc_Get_ModuleVersion( void )
{
    const rtc_ModuleVersion_t moduleVersion = { .Major = 1u, .Minor = 0u, .Patch = 0u };

    return ( moduleVersion );
}


/**
 * \brief Fills the default module configuration (LSE clock, calendar reset to 2000-01-01
 *        00:00:00 when lost, lowest interrupt priority).
 *
 * \param rtcConfig [out]: Configuration filled with the default values
 * \return \ref RTC_REQUEST_OK, \ref RTC_REQUEST_ERROR if \p rtcConfig is \ref RTC_NULL_PTR
 */
rtc_RequestState_t Rtc_Get_DefaultConfig( rtc_Config_t * const rtcConfig )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_NULL_PTR != rtcConfig )
    {
        rtcConfig->ClkSrc                     = RTC_CLK_SRC_LSE;
        rtcConfig->ForceInit                  = RTC_FUNCTION_INACTIVE;
        rtcConfig->DefaultDateTime.Year        = RTC_YEAR_BASE;
        rtcConfig->DefaultDateTime.Month       = 1u;
        rtcConfig->DefaultDateTime.Day         = 1u;
        rtcConfig->DefaultDateTime.WeekDay     = RTC_WEEKDAY_SATURDAY;
        rtcConfig->DefaultDateTime.Hour        = 0u;
        rtcConfig->DefaultDateTime.Minute      = 0u;
        rtcConfig->DefaultDateTime.Second      = 0u;
        rtcConfig->DefaultDateTime.Millisecond = 0u;
        rtcConfig->IrqPriority                = RTC_IRQ_PRIO_DEFAULT;
        retState                              = RTC_REQUEST_OK;
    }
    else
    {
        /* Invalid pointer, nothing filled */
    }

    return ( retState );
}


/**
 * \brief Initializes the module: selects the backup domain clock source and either adopts an
 *        already running calendar (untouched) or initializes a lost one.
 *
 * Enables the backup domain write access through the Pwr module (\ref Pwr_Set_BackupAccessActive)
 * and holds it until \ref Rtc_Deinit. When the Pwr module refuses the access, or the
 * initialization fails, the module stays uninitialized and the access requested by this call is
 * released.
 *
 * \param rtcConfig [in]: Requested configuration, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise (nothing changed
 *         for an invalid parameter; the module is left uninitialized on a refused backup domain
 *         access, a hardware timeout or on a clock source already locked to a different source
 *         by the hardware)
 */
rtc_RequestState_t Rtc_Init( const rtc_Config_t * const rtcConfig )
{
    rtc_RequestState_t         retState     = RTC_REQUEST_ERROR;
    const rtc_ClkSourceMap_t   *reqRow      = RTC_NULL_PTR;
    rcc_Rtc_ClkSource_t         curRccSrc    = RCC_RTC_CLK_SOURCE_LSE;
    rcc_FunctionState_t         periphActive = RCC_FUNCTION_INACTIVE;
    rtc_FlagState_t             sourceMatches = RTC_FLAG_INACTIVE;
    rtc_RequestState_t          clkReady     = RTC_REQUEST_ERROR;
    rtc_RequestState_t          dateValid    = RTC_REQUEST_ERROR;
    rtc_RequestState_t          rowFound     = RTC_REQUEST_ERROR;
    rtc_RequestState_t          accessState  = RTC_REQUEST_ERROR;

    if( RTC_NULL_PTR != rtcConfig )
    {
        dateValid = Rtc_ValidateDateTime( &rtcConfig->DefaultDateTime );
        rowFound  = Rtc_FindClkSourceByRtc( rtcConfig->ClkSrc, &reqRow );
    }
    else
    {
        /* Invalid pointer, dateValid / rowFound stay RTC_REQUEST_ERROR */
    }

    if( ( RTC_REQUEST_OK == dateValid ) &&
        ( RTC_REQUEST_OK == rowFound )     )
    {
        /* The backup domain is written from here on (RTCSEL, the calendar) */
        accessState = Rtc_BackupAccessAcquire();
    }
    else
    {
        /* Invalid parameter, the access is not requested */
    }

    if( ( RTC_NULL_PTR != rtcConfig ) &&
        ( RTC_REQUEST_OK == dateValid ) &&
        ( RTC_REQUEST_OK == rowFound )  &&
        ( RTC_REQUEST_OK == accessState )  )
    {
        rcc_RequestState_t srcRdState   = RCC_REQUEST_ERROR;
        rcc_RequestState_t stateRdState = RCC_REQUEST_ERROR;
        rtc_FlagState_t     rccReadOk    = RTC_FLAG_INACTIVE;

        srcRdState   = Rcc_Get_RtcClkSource( &curRccSrc );
        stateRdState = Rcc_Get_PeriphState( reqRow->PeriphId, &periphActive );

        if( RCC_REQUEST_OK == stateRdState )
        {
            rccReadOk = RTC_FLAG_ACTIVE;
        }
        else
        {
            /* Rcc could not report the periph state: do not assume anything, fail the whole
               Init instead */
            rccReadOk = RTC_FLAG_INACTIVE;
        }

        if( RTC_FLAG_ACTIVE != rccReadOk )
        {
            clkReady = RTC_REQUEST_ERROR;
        }
        else if( RCC_REQUEST_OK != srcRdState )
        {
            /* Rcc reports no clock source (RTCSEL = no clock: first power-up, loss of VBAT,
               backup domain reset), the requested source is selected below; curRccSrc keeps
               its initial value and is not compared */
            sourceMatches = RTC_FLAG_INACTIVE;
        }
        else if( curRccSrc == reqRow->RccSrc )
        {
            sourceMatches = RTC_FLAG_ACTIVE;
        }
        else
        {
            sourceMatches = RTC_FLAG_INACTIVE;
        }

        if( RTC_FLAG_ACTIVE != rccReadOk )
        {
            /* Already failed above, skip the clock source decision entirely */
        }
        else if( RTC_FLAG_ACTIVE == sourceMatches )
        {
            /* The requested source is already selected, make sure it is enabled */
            if( RCC_FUNCTION_ACTIVE == periphActive )
            {
                clkReady = RTC_REQUEST_OK;
            }
            else
            {
                rcc_RequestState_t enableState = Rcc_Set_PeriphActive( reqRow->PeriphId );

                if( RCC_REQUEST_OK == enableState )
                {
                    clkReady = RTC_REQUEST_OK;
                }
                else
                {
                    clkReady = RTC_REQUEST_ERROR;
                }
            }
        }
        else
        {
            /* A different source is requested: the hardware allows RTCSEL to be written only
               once per backup domain life, Rcc is expected to reject this when it was already
               set by a previous Init (missing Rcc API item, see the Rtc.c file header) */
            rcc_RequestState_t srcSetState = Rcc_Set_RtcClkSource( reqRow->RccSrc );

            if( RCC_REQUEST_OK == srcSetState )
            {
                rcc_RequestState_t enableState2 = Rcc_Set_PeriphActive( reqRow->PeriphId );

                if( RCC_REQUEST_OK == enableState2 )
                {
                    clkReady = RTC_REQUEST_OK;
                }
                else
                {
                    clkReady = RTC_REQUEST_ERROR;
                }
            }
            else
            {
                /* RTCSEL rejected, RTC_PERIPH not enabled either, nothing else is changed */
                clkReady = RTC_REQUEST_ERROR;
            }
        }

        if( RTC_REQUEST_OK == clkReady )
        {
            rtc_FlagState_t calendarInited = RTC_FLAG_INACTIVE;
            rtc_FlagState_t adopt          = RTC_FLAG_INACTIVE;
            uint32_t         initsBit       = RTC_REG_BITS_CLEAR;

            Rtc_Reg_Unlock();
            initsBit = Rtc_Reg_Get_RegBit( RTC_REG_ICSR, RTC_ICSR_INITS );

            if( RTC_REG_BITS_CLEAR != initsBit )
            {
                calendarInited = RTC_FLAG_ACTIVE;
            }
            else
            {
                calendarInited = RTC_FLAG_INACTIVE;
            }

            if( ( RTC_FLAG_ACTIVE == sourceMatches )    &&
                ( RTC_FLAG_ACTIVE == calendarInited )   &&
                ( RTC_FUNCTION_ACTIVE != rtcConfig->ForceInit )    )
            {
                adopt = RTC_FLAG_ACTIVE;
            }
            else
            {
                adopt = RTC_FLAG_INACTIVE;
            }

            if( RTC_FLAG_ACTIVE == adopt )
            {
                uint32_t predivSRaw = Rtc_Reg_Get_RegVal( RTC_REG_PRER, RTC_PRER_PREDIV_S_Msk );

                rtc_State.PredivS   = predivSRaw >> RTC_PRER_PREDIV_S_Pos;
                rtc_State.TimeState = RTC_TIME_VALID;
                retState            = RTC_REQUEST_OK;
            }
            else
            {
                rtc_RequestState_t initState = Rtc_Reg_EnterInitMode();

                if( RTC_REQUEST_OK == initState )
                {
                    uint32_t predivA = RTC_PREDIV_A_32K;
                    uint32_t predivS = RTC_PREDIV_S_32K;

                    if( RTC_CLK_SRC_HSE_DIV == rtcConfig->ClkSrc )
                    {
                        predivA = RTC_PREDIV_A_1M;
                        predivS = RTC_PREDIV_S_1M;
                    }
                    else
                    {
                        /* LSE / LSI: 32768 Hz prescaler already set above */
                    }

                    rtc_RequestState_t prerVerify;
                    rtc_RequestState_t dateVerify;
                    rtc_RequestState_t syncState;

                    Rtc_Reg_Set_RegVal( RTC_REG_PRER,
                                        ( RTC_PRER_PREDIV_A_Msk | RTC_PRER_PREDIV_S_Msk ),
                                        ( ( predivA << RTC_PRER_PREDIV_A_Pos ) |
                                          ( predivS << RTC_PRER_PREDIV_S_Pos )    ) );
                    Rtc_Reg_Reset_RegBit( RTC_REG_CR, RTC_CR_FMT );
                    Rtc_WriteCalendarRaw( &rtcConfig->DefaultDateTime );
                    Rtc_Reg_ExitInitMode();

                    syncState  = Rtc_WaitCalendarSync();
                    prerVerify = Rtc_VerifyPrescaler( predivA, predivS );
                    dateVerify = Rtc_VerifyDateTimeWritten( &rtcConfig->DefaultDateTime );

                    if( ( RTC_REQUEST_OK == syncState  ) &&
                        ( RTC_REQUEST_OK == prerVerify ) &&
                        ( RTC_REQUEST_OK == dateVerify )    )
                    {
                        rtc_State.PredivS   = predivS;
                        rtc_State.TimeState = RTC_TIME_LOST;
                        retState            = RTC_REQUEST_OK;
                    }
                    else
                    {
                        /* PRER / CR.FMT / TR / DR did not read back as written, report the
                           error (the module stays uninitialized, see the end of the function) */
                        retState = RTC_REQUEST_ERROR;
                    }
                }
                else
                {
                    /* INITF timeout, nothing else is changed */
                    retState = RTC_REQUEST_ERROR;
                }
            }

            Rtc_Reg_Lock();
        }
        else
        {
            /* Clock source could not be established, nothing else is changed */
            retState = RTC_REQUEST_ERROR;
        }
    }
    else
    {
        /* Invalid parameter or refused backup domain access, nothing is changed */
        retState = RTC_REQUEST_ERROR;
    }

    if( RTC_REQUEST_OK == retState )
    {
        nvic_RequestState_t prioState = NVIC_REQUEST_ERROR;

        rtc_State.ClkSrc      = rtcConfig->ClkSrc;
        rtc_State.IrqPriority = rtcConfig->IrqPriority;
        prioState = Nvic_Set_PeriphIrq_Prio( NVIC_PERIPH_IRQ_RTC, rtcConfig->IrqPriority );
        (void)prioState;
        rtc_State.Initialized = RTC_FUNCTION_ACTIVE;
    }
    else
    {
        /* Init failed, module stays uninitialized: the access requested by this call is released */
        if( RTC_FUNCTION_ACTIVE != rtc_State.Initialized )
        {
            (void)Rtc_BackupAccessRelease();
        }
        else
        {
            /* A previous successful Init keeps working with its access */
        }
    }

    return ( retState );
}


/**
 * \brief Deinitializes the module (clears the registered callbacks) and releases the backup domain
 *        write access taken by \ref Rtc_Init (the Pwr module keeps the access while another
 *        module, for example Tamp, still holds it). Never resets the backup domain, the
 *        calendar or the NVIC priority (survives the reset by design, see README.md).
 *
 * \return \ref RTC_REQUEST_OK, \ref RTC_REQUEST_ERROR when the Pwr module refused the release of
 *         the access (the module is deinitialized anyway)
 */
rtc_RequestState_t Rtc_Deinit( void )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    rtc_State.Initialized       = RTC_FUNCTION_INACTIVE;
    rtc_State.AlarmCallback[0]  = RTC_NULL_PTR;
    rtc_State.AlarmCallback[1]  = RTC_NULL_PTR;
    rtc_State.WakeUpCallback    = RTC_NULL_PTR;
    rtc_State.TimestampCallback = RTC_NULL_PTR;
    retState                    = Rtc_BackupAccessRelease();

    return ( retState );
}


/**
 * \brief Cyclic task: polls the RTC status flags and dispatches the alarm / wake-up /
 *        timestamp callbacks (see \ref Rtc_PollEvents and the Rtc.c file header deviation
 *        note about the NVIC wiring).
 */
void Rtc_Task( void )
{
    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        Rtc_PollEvents();
    }
    else
    {
        /* Module not initialized, nothing to poll */
    }
}

/* --------------------------------- Time ------------------------------------ */

/**
 * \brief Writes the calendar (initialization mode, see README.md "the calendar stops for the
 *        duration of the write").
 *
 * \param dateTime [in]: Date and time to write, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise (nothing written)
 */
rtc_RequestState_t Rtc_Set_DateTime( const rtc_DateTime_t * const dateTime )
{
    rtc_RequestState_t retState  = RTC_REQUEST_ERROR;
    rtc_RequestState_t dateValid = Rtc_ValidateDateTime( dateTime );

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )          &&
        ( RTC_NULL_PTR != dateTime )                              &&
        ( RTC_REQUEST_OK == dateValid )        )
    {
        rtc_RequestState_t initState;

        Rtc_Reg_Unlock();
        initState = Rtc_Reg_EnterInitMode();

        if( RTC_REQUEST_OK == initState )
        {
            rtc_RequestState_t dateVerify;
            rtc_RequestState_t syncState;

            Rtc_WriteCalendarRaw( dateTime );
            Rtc_Reg_ExitInitMode();
            syncState  = Rtc_WaitCalendarSync();
            dateVerify = Rtc_VerifyDateTimeWritten( dateTime );

            if( ( RTC_REQUEST_OK == syncState  ) &&
                ( RTC_REQUEST_OK == dateVerify )    )
            {
                retState = RTC_REQUEST_OK;
            }
            else
            {
                /* The shadow registers did not synchronize or TR / DR did not read back as written */
                retState = RTC_REQUEST_ERROR;
            }
        }
        else
        {
            /* INITF timeout, nothing written */
            retState = RTC_REQUEST_ERROR;
        }

        Rtc_Reg_Lock();
    }
    else
    {
        /* Module not initialized, invalid pointer or invalid date, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Reads the calendar through a consistent (non-torn) double read.
 *
 * \param dateTime [out]: Date and time read, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Get_DateTime( rtc_DateTime_t * const dateTime )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    rtc_RequestState_t syncState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_NULL_PTR != dateTime )                        )
    {
        syncState = Rtc_WaitCalendarSync();
    }
    else
    {
        /* Module not initialized or invalid pointer, nothing read */
    }

    if( RTC_REQUEST_OK == syncState )
    {
        rtc_DateTime_t  prevRead;
        rtc_DateTime_t  currRead = { 0u };
        rtc_FlagState_t stable   = RTC_FLAG_INACTIVE;

        Rtc_ReadCalendarRaw( &prevRead );

        for( uint32_t retryIdx = 0u; RTC_DATETIME_READ_RETRY_LIMIT > retryIdx; retryIdx++ )
        {
            Rtc_ReadCalendarRaw( &currRead );

            if( ( prevRead.Second == currRead.Second ) &&
                ( prevRead.Minute == currRead.Minute ) &&
                ( prevRead.Hour   == currRead.Hour   ) &&
                ( prevRead.Day    == currRead.Day    )    )
            {
                stable = RTC_FLAG_ACTIVE;
                break;
            }
            else
            {
                /* The second changed between the two reads (rollover), read again */
                prevRead = currRead;
            }
        }

        (void)stable; /* Only used to break out of the retry loop early, see above */

        *dateTime = currRead;
        retState  = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized, invalid pointer or the shadow registers are not synchronized,
           nothing read */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Writes the calendar from a Unix timestamp.
 *
 * \param unixTime [in]: Seconds since 1970-01-01 00:00:00
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_UnixTime( rtc_UnixTime_t unixTime )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;
    rtc_DateTime_t      dateTime = { 0u, 0u, 0u, RTC_WEEKDAY_MONDAY, 0u, 0u, 0u, 0u };
    int32_t             totalDays;
    uint32_t            secOfDay;

    totalDays       = (int32_t)( unixTime / ( RTC_SECOND_CNT * RTC_MINUTE_CNT * RTC_HOUR_CNT ) );
    secOfDay        = unixTime % ( RTC_SECOND_CNT * RTC_MINUTE_CNT * RTC_HOUR_CNT );
    dateTime.Hour   = (uint8_t)( secOfDay / ( RTC_SECOND_CNT * RTC_MINUTE_CNT ) );
    dateTime.Minute = (uint8_t)( ( secOfDay / RTC_SECOND_CNT ) % RTC_MINUTE_CNT );
    dateTime.Second = (uint8_t)( secOfDay % RTC_SECOND_CNT );

    Rtc_CivilFromDays( totalDays, &dateTime.Year, &dateTime.Month, &dateTime.Day );
    dateTime.WeekDay = Rtc_ComputeWeekDay( totalDays );

    retState = Rtc_Set_DateTime( &dateTime );

    return ( retState );
}


/**
 * \brief Reads the calendar as a Unix timestamp.
 *
 * \param unixTime [out]: Seconds since 1970-01-01 00:00:00, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Get_UnixTime( rtc_UnixTime_t * const unixTime )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;
    rtc_DateTime_t      dateTime;
    rtc_RequestState_t  dtState = Rtc_Get_DateTime( &dateTime );

    if( ( RTC_NULL_PTR != unixTime ) &&
        ( RTC_REQUEST_OK == dtState )   )
    {
        int32_t  days   = Rtc_DaysFromCivil( dateTime.Year, dateTime.Month, dateTime.Day );
        uint32_t secOfDay = ( (uint32_t)dateTime.Hour * RTC_MINUTE_CNT * RTC_SECOND_CNT ) +
                            ( (uint32_t)dateTime.Minute * RTC_SECOND_CNT ) +
                            (uint32_t)dateTime.Second;

        *unixTime = ( (rtc_UnixTime_t)days * RTC_SECOND_CNT * RTC_MINUTE_CNT * RTC_HOUR_CNT ) +
                    (rtc_UnixTime_t)secOfDay;
        retState  = RTC_REQUEST_OK;
    }
    else
    {
        /* Invalid pointer or the calendar could not be read, nothing returned */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Reports whether the calendar was adopted (already running) or was (re)initialized by
 *        \ref Rtc_Init.
 *
 * \param timeState [out]: State read, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Get_TimeState( rtc_TimeState_t * const timeState )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_NULL_PTR != timeState )                       )
    {
        *timeState = rtc_State.TimeState;
        retState   = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized or invalid pointer, nothing returned */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Applies a one hour daylight saving adjustment (RTC_CR.ADD1H / SUB1H, self clearing).
 *
 * \param daylightSaving [in]: Adjustment to apply
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_DaylightSaving( rtc_DaylightSaving_t daylightSaving )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        Rtc_Reg_Unlock();

        if( RTC_DST_ADD_1H == daylightSaving )
        {
            Rtc_Reg_Set_RegBit( RTC_REG_CR, RTC_CR_ADD1H );
        }
        else
        {
            if( RTC_DST_SUB_1H == daylightSaving )
            {
                Rtc_Reg_Set_RegBit( RTC_REG_CR, RTC_CR_SUB1H );
            }
            else
            {
                /* RTC_DST_NONE, no bit to write */
            }
        }

        Rtc_Reg_Lock();
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}

/* -------------------------------- Alarms ------------------------------------ */

/**
 * \brief Configures an alarm (time of day plus the optional day / weekday match) and its
 *        callback. The alarm is left inactive, call \ref Rtc_Set_AlarmActive to start it.
 *
 * \param alarmId     [in]: \ref RTC_ALARM_A or \ref RTC_ALARM_B
 * \param alarmConfig [in]: Configuration to apply, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_Alarm( rtc_AlarmId_t alarmId, const rtc_AlarmConfig_t * const alarmConfig )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_ALARM_CNT > alarmId )                      &&
        ( RTC_NULL_PTR != alarmConfig )                  &&
        ( RTC_HOUR_CNT > alarmConfig->Hour )              &&
        ( RTC_MINUTE_CNT > alarmConfig->Minute )          &&
        ( RTC_SECOND_CNT > alarmConfig->Second )             )
    {
        uint32_t     alarmBits = Rtc_BuildAlarmReg( alarmConfig );
        rtc_RegId_t  alarmReg;
        rtc_RegId_t  alarmSsReg;
        uint32_t     alarmEnBit;
        uint32_t     alarmWfBit;

        if( RTC_ALARM_A == alarmId )
        {
            alarmReg   = RTC_REG_ALRMAR;
            alarmSsReg = RTC_REG_ALRMASSR;
            alarmEnBit = RTC_CR_ALRAE;
            alarmWfBit = RTC_ICSR_ALRAWF;
        }
        else
        {
            alarmReg   = RTC_REG_ALRMBR;
            alarmSsReg = RTC_REG_ALRMBSSR;
            alarmEnBit = RTC_CR_ALRBE;
            alarmWfBit = RTC_ICSR_ALRBWF;
        }

        rtc_RequestState_t waitState;

        Rtc_Reg_Unlock();
        Rtc_Reg_Reset_RegBit( RTC_REG_CR, alarmEnBit );
        waitState = Rtc_WaitRegBitSet( RTC_REG_ICSR, alarmWfBit );

        if( RTC_REQUEST_OK == waitState )
        {
            Rtc_Reg_Set_RegVal( alarmReg, RTC_REG_ALL_BITS, alarmBits );
            Rtc_Reg_Set_RegVal( alarmSsReg, RTC_REG_ALL_BITS, 0u );
            rtc_State.AlarmCallback[ alarmId ] = alarmConfig->Callback;
            retState                          = RTC_REQUEST_OK;
        }
        else
        {
            /* ALRAWF / ALRBWF timeout, alarm left disabled, nothing written */
            retState = RTC_REQUEST_ERROR;
        }

        Rtc_Reg_Lock();
    }
    else
    {
        /* Module not initialized or invalid parameter, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Activates a configured alarm (enables the match and its interrupt).
 *
 * \param alarmId [in]: \ref RTC_ALARM_A or \ref RTC_ALARM_B
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_AlarmActive( rtc_AlarmId_t alarmId )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_ALARM_CNT > alarmId )                         )
    {
        Rtc_Reg_Unlock();

        if( RTC_ALARM_A == alarmId )
        {
            Rtc_Reg_Set_RegBit( RTC_REG_CR, ( RTC_CR_ALRAE | RTC_CR_ALRAIE ) );
        }
        else
        {
            Rtc_Reg_Set_RegBit( RTC_REG_CR, ( RTC_CR_ALRBE | RTC_CR_ALRBIE ) );
        }

        Rtc_Reg_Lock();
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized or invalid alarm id, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Deactivates an alarm.
 *
 * \param alarmId [in]: \ref RTC_ALARM_A or \ref RTC_ALARM_B
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_AlarmInactive( rtc_AlarmId_t alarmId )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_ALARM_CNT > alarmId )                         )
    {
        Rtc_Reg_Unlock();

        if( RTC_ALARM_A == alarmId )
        {
            Rtc_Reg_Reset_RegBit( RTC_REG_CR, ( RTC_CR_ALRAE | RTC_CR_ALRAIE ) );
        }
        else
        {
            Rtc_Reg_Reset_RegBit( RTC_REG_CR, ( RTC_CR_ALRBE | RTC_CR_ALRBIE ) );
        }

        Rtc_Reg_Lock();
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized or invalid alarm id, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Reads whether an alarm is currently active.
 *
 * \param alarmId    [in]:  \ref RTC_ALARM_A or \ref RTC_ALARM_B
 * \param alarmState [out]: State read, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Get_AlarmState( rtc_AlarmId_t alarmId, rtc_FunctionState_t * const alarmState )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_ALARM_CNT > alarmId )                      &&
        ( RTC_NULL_PTR != alarmState )                       )
    {
        uint32_t enBit;

        if( RTC_ALARM_A == alarmId )
        {
            enBit = RTC_CR_ALRAE;
        }
        else
        {
            enBit = RTC_CR_ALRBE;
        }

        uint32_t enBitVal = Rtc_Reg_Get_RegBit( RTC_REG_CR, enBit );

        if( 0u != enBitVal )
        {
            *alarmState = RTC_FUNCTION_ACTIVE;
        }
        else
        {
            *alarmState = RTC_FUNCTION_INACTIVE;
        }

        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized, invalid alarm id or invalid pointer, nothing returned */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}

/* ----------------------------- Wake-up timer -------------------------------- */

/**
 * \brief Configures the periodic wake-up timer (ck_spre / 1 Hz resolution, see the Rtc.c file
 *        header deviation note). The timer is left inactive.
 *
 * \param period   [in]: Period in milliseconds, truncated to whole seconds, 1000 .. 65536000
 * \param callback [in]: Called from the RTC interrupt at every period, \ref RTC_NULL_PTR
 *                        disables the callback (flag only)
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_WakeUpTimer( rtc_Period_ms_t period, rtc_Callback_t * const callback )
{
    rtc_RequestState_t retState  = RTC_REQUEST_ERROR;
    uint32_t             periodS = period / (uint32_t)RTC_SHIFT_MS_PER_S;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( 0u < periodS )                                  &&
        ( RTC_WAKEUP_SECONDS_MAX >= ( periodS - 1u ) )        )
    {
        rtc_RequestState_t waitState;

        Rtc_Reg_Unlock();
        Rtc_Reg_Reset_RegBit( RTC_REG_CR, RTC_CR_WUTE );
        waitState = Rtc_WaitRegBitSet( RTC_REG_ICSR, RTC_ICSR_WUTWF );

        if( RTC_REQUEST_OK == waitState )
        {
            Rtc_Reg_Set_RegVal( RTC_REG_CR, RTC_CR_WUCKSEL_Msk, RTC_CR_WUCKSEL_2 );
            Rtc_Reg_Set_RegVal( RTC_REG_WUTR, RTC_WUTR_WUT_Msk, ( periodS - 1u ) );
            rtc_State.WakeUpCallback = callback;
            retState                = RTC_REQUEST_OK;
        }
        else
        {
            /* WUTWF timeout, wake-up timer left disabled, nothing written */
            retState = RTC_REQUEST_ERROR;
        }

        Rtc_Reg_Lock();
    }
    else
    {
        /* Module not initialized or period out of range, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Activates the configured wake-up timer.
 *
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_WakeUpTimerActive( void )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        Rtc_Reg_Unlock();
        Rtc_Reg_Set_RegBit( RTC_REG_CR, ( RTC_CR_WUTE | RTC_CR_WUTIE ) );
        Rtc_Reg_Lock();
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Deactivates the wake-up timer.
 *
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_WakeUpTimerInactive( void )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        Rtc_Reg_Unlock();
        Rtc_Reg_Reset_RegBit( RTC_REG_CR, ( RTC_CR_WUTE | RTC_CR_WUTIE ) );
        Rtc_Reg_Lock();
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Reads the wake-up timer event flag (SR.WUTF).
 *
 * \param wakeUpFlag [out]: Flag read, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Get_WakeUpFlag( rtc_FlagState_t * const wakeUpFlag )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_NULL_PTR != wakeUpFlag )                       )
    {
        uint32_t wutfVal = Rtc_Reg_Get_RegBit( RTC_REG_SR, RTC_SR_WUTF );

        if( 0u != wutfVal )
        {
            *wakeUpFlag = RTC_FLAG_ACTIVE;
        }
        else
        {
            *wakeUpFlag = RTC_FLAG_INACTIVE;
        }

        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized or invalid pointer, nothing returned */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Clears the wake-up timer event flag (SCR.CWUTF).
 *
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_WakeUpFlagClear( void )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        Rtc_Reg_Set_RegBit( RTC_REG_SCR, RTC_SCR_CWUTF );
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}

/* ------------------------------- Timestamp ---------------------------------- */

/**
 * \brief Configures the timestamp capture edge. Register side only, see the Rtc.c file header
 *        deviation note about \ref Rtc_InitTimestampGpio.
 *
 * \param timestampConfig [in]: Configuration to apply, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_TimestampConfig( const rtc_TimestampConfig_t * const timestampConfig )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_NULL_PTR != timestampConfig )                 )
    {
        Rtc_Reg_Unlock();
        Rtc_Reg_Reset_RegBit( RTC_REG_CR, RTC_CR_TSE );

        if( RTC_TS_EDGE_FALLING == timestampConfig->Edge )
        {
            Rtc_Reg_Set_RegBit( RTC_REG_CR, RTC_CR_TSEDGE );
        }
        else
        {
            Rtc_Reg_Reset_RegBit( RTC_REG_CR, RTC_CR_TSEDGE );
        }

        Rtc_Reg_Lock();
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized or invalid pointer, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Activates the timestamp capture.
 *
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_TimestampActive( void )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        Rtc_Reg_Unlock();
        Rtc_Reg_Set_RegBit( RTC_REG_CR, RTC_CR_TSE );
        Rtc_Reg_Lock();
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Deactivates the timestamp capture.
 *
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_TimestampInactive( void )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        Rtc_Reg_Unlock();
        Rtc_Reg_Reset_RegBit( RTC_REG_CR, RTC_CR_TSE );
        Rtc_Reg_Lock();
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Reads the last captured timestamp (TSTR / TSDR, no sub-second: TSSSR is ignored, the
 *        Millisecond field is always 0).
 *
 * \param dateTime [out]: Timestamp read, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Get_Timestamp( rtc_DateTime_t * const dateTime )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_NULL_PTR != dateTime )                        )
    {
        uint32_t tsTimeReg = Rtc_Reg_Get_RegVal( RTC_REG_TSTR, RTC_REG_ALL_BITS );
        uint32_t tsDateReg = Rtc_Reg_Get_RegVal( RTC_REG_TSDR, RTC_REG_ALL_BITS );

        uint8_t monthTens = (uint8_t)( ( tsDateReg & RTC_DR_MT_Msk ) >> RTC_DR_MT_Pos );
        uint8_t monthOnes = (uint8_t)( ( tsDateReg & RTC_DR_MU_Msk ) >> RTC_DR_MU_Pos );
        uint8_t dayTens   = (uint8_t)( ( tsDateReg & RTC_DR_DT_Msk ) >> RTC_DR_DT_Pos );
        uint8_t dayOnes   = (uint8_t)( ( tsDateReg & RTC_DR_DU_Msk ) >> RTC_DR_DU_Pos );
        uint8_t hourTens  = (uint8_t)( ( tsTimeReg & RTC_TR_HT_Msk ) >> RTC_TR_HT_Pos );
        uint8_t hourOnes  = (uint8_t)( ( tsTimeReg & RTC_TR_HU_Msk ) >> RTC_TR_HU_Pos );
        uint8_t minTens   = (uint8_t)( ( tsTimeReg & RTC_TR_MNT_Msk ) >> RTC_TR_MNT_Pos );
        uint8_t minOnes   = (uint8_t)( ( tsTimeReg & RTC_TR_MNU_Msk ) >> RTC_TR_MNU_Pos );
        uint8_t secTens   = (uint8_t)( ( tsTimeReg & RTC_TR_ST_Msk ) >> RTC_TR_ST_Pos );
        uint8_t secOnes   = (uint8_t)( ( tsTimeReg & RTC_TR_SU_Msk ) >> RTC_TR_SU_Pos );

        dateTime->Year        = 0u; /* The year is not stored by the timestamp registers */
        dateTime->Month       = (uint8_t)( ( monthTens * RTC_BCD_DIGIT_RADIX ) + monthOnes );
        dateTime->Day         = (uint8_t)( ( dayTens   * RTC_BCD_DIGIT_RADIX ) + dayOnes   );
        dateTime->WeekDay     = (rtc_WeekDay_t)( ( tsDateReg & RTC_DR_WDU_Msk ) >> RTC_DR_WDU_Pos );
        dateTime->Hour        = (uint8_t)( ( hourTens  * RTC_BCD_DIGIT_RADIX ) + hourOnes  );
        dateTime->Minute      = (uint8_t)( ( minTens   * RTC_BCD_DIGIT_RADIX ) + minOnes   );
        dateTime->Second      = (uint8_t)( ( secTens   * RTC_BCD_DIGIT_RADIX ) + secOnes   );
        dateTime->Millisecond = 0u;
        retState              = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized or invalid pointer, nothing returned */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}

/* ------------------------------- Calibration -------------------------------- */

/**
 * \brief Applies a digital calibration correction (RTC_CALR, 32 s calibration window).
 *
 * \param calibration [in]: Correction in parts per billion
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_Calibration( rtc_Calibration_ppb_t calibration )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        uint32_t calp;
        int32_t  calmRaw;
        uint32_t calm;

        if( 0 <= calibration )
        {
            calp    = RTC_CALR_CALP;
            calmRaw = ( (int32_t)RTC_CALIB_CALP_PPB - calibration ) / RTC_CALIB_CALM_PPB;
        }
        else
        {
            calp    = 0u;
            calmRaw = ( -calibration ) / RTC_CALIB_CALM_PPB;
        }

        if( 0 > calmRaw )
        {
            calm = 0u;
        }
        else
        {
            if( (int32_t)RTC_CALIB_CALM_MAX < calmRaw )
            {
                calm = RTC_CALIB_CALM_MAX;
            }
            else
            {
                calm = (uint32_t)calmRaw;
            }
        }

        rtc_RequestState_t waitState = Rtc_WaitRegBitClear( RTC_REG_ICSR, RTC_ICSR_RECALPF );

        if( RTC_REQUEST_OK == waitState )
        {
            Rtc_Reg_Unlock();
            Rtc_Reg_Set_RegVal( RTC_REG_CALR,
                                ( RTC_CALR_CALP | RTC_CALR_CALM_Msk ),
                                ( calp | ( calm << RTC_CALR_CALM_Pos ) ) );
            Rtc_Reg_Lock();
            retState = RTC_REQUEST_OK;
        }
        else
        {
            /* RECALPF timeout, nothing written */
            retState = RTC_REQUEST_ERROR;
        }
    }
    else
    {
        /* Module not initialized, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Reads back the applied digital calibration correction (lossy inverse of the
 *        \ref Rtc_Set_Calibration rounding).
 *
 * \param calibration [out]: Correction read, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Get_Calibration( rtc_Calibration_ppb_t * const calibration )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_NULL_PTR != calibration )                     )
    {
        uint32_t calrReg = Rtc_Reg_Get_RegVal( RTC_REG_CALR, ( RTC_CALR_CALP | RTC_CALR_CALM_Msk ) );
        uint32_t calm    = ( calrReg & RTC_CALR_CALM_Msk ) >> RTC_CALR_CALM_Pos;
        int32_t  result  = -( (int32_t)calm * RTC_CALIB_CALM_PPB );

        if( 0u != ( calrReg & RTC_CALR_CALP ) )
        {
            result += (int32_t)RTC_CALIB_CALP_PPB;
        }
        else
        {
            /* CALP not set, no coarse addition */
        }

        *calibration = result;
        retState     = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized or invalid pointer, nothing returned */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Applies a smooth calendar time shift (RTC_SHIFTR, synchronization with a master
 *        clock), using the PREDIV_S recorded by \ref Rtc_Init.
 *
 * \param shift [in]: Shift in milliseconds, -999 (PREDIV_S dependent) .. +1000
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_TimeShift( rtc_Shift_ms_t shift )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        uint32_t add1s;
        int32_t  remainderMs;
        int32_t  subFsRaw;
        uint32_t subFs;

        if( 0 <= shift )
        {
            add1s       = RTC_SHIFTR_ADD1S;
            remainderMs = RTC_SHIFT_MS_PER_S - shift;
        }
        else
        {
            add1s       = 0u;
            remainderMs = -shift;
        }

        subFsRaw = ( remainderMs * (int32_t)( rtc_State.PredivS + 1u ) ) / RTC_SHIFT_MS_PER_S;

        if( 0 > subFsRaw )
        {
            subFs = 0u;
        }
        else
        {
            if( (int32_t)rtc_State.PredivS < subFsRaw )
            {
                subFs = rtc_State.PredivS;
            }
            else
            {
                subFs = (uint32_t)subFsRaw;
            }
        }

        rtc_RequestState_t shpfWaitState = Rtc_WaitRegBitClear( RTC_REG_ICSR, RTC_ICSR_SHPF );

        if( RTC_REQUEST_OK == shpfWaitState )
        {
            Rtc_Reg_Unlock();
            Rtc_Reg_Set_RegVal( RTC_REG_SHIFTR,
                                ( RTC_SHIFTR_ADD1S | RTC_SHIFTR_SUBFS_Msk ),
                                ( add1s | ( subFs << RTC_SHIFTR_SUBFS_Pos ) ) );
            Rtc_Reg_Lock();
            retState = RTC_REQUEST_OK;
        }
        else
        {
            /* SHPF timeout, nothing written */
            retState = RTC_REQUEST_ERROR;
        }
    }
    else
    {
        /* Module not initialized, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Activates the RTC_REFIN reference clock input (fine calibration of the LSE) and
 *        configures its pin.
 *
 * \param refInPin [in]: Pin item, never \ref RTC_REFIN_PIN_UNUSED
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_ReferenceClockActive( rtc_RefInPin_t refInPin )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        rtc_RequestState_t pinState = Rtc_InitRefInGpio( refInPin );

        if( RTC_REQUEST_OK == pinState )
        {
            Rtc_Reg_Unlock();
            Rtc_Reg_Set_RegBit( RTC_REG_CR, RTC_CR_REFCKON );
            Rtc_Reg_Lock();
            retState = RTC_REQUEST_OK;
        }
        else
        {
            /* Pin GPIO configuration failed, nothing written */
            retState = RTC_REQUEST_ERROR;
        }
    }
    else
    {
        /* Module not initialized or invalid / unavailable pin, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Deactivates the RTC_REFIN reference clock input.
 *
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_ReferenceClockInactive( void )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        Rtc_Reg_Unlock();
        Rtc_Reg_Reset_RegBit( RTC_REG_CR, RTC_CR_REFCKON );
        Rtc_Reg_Lock();
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}

/* ---------------------------------- Output ---------------------------------- */

/**
 * \brief Configures the RTC_OUT2 output signal and its pin (RTC_OUT1 / PC13 is not supported,
 *        see the Rtc.c file header deviation note; \ref rtc_OutputConfig_t::Pin always
 *        redirects the output function to RTC_OUT2 through CR.OUT2EN).
 *
 * \param outputConfig [in]: Configuration to apply, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_Output( const rtc_OutputConfig_t * const outputConfig )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_NULL_PTR != outputConfig )                    )
    {
        rtc_RequestState_t pinState  = RTC_REQUEST_OK;
        uint32_t            oselVal  = 0u;
        uint32_t            coeVal   = 0u;
        uint32_t            out2EnVal = 0u;

        if( RTC_OUT2_PIN_UNUSED != outputConfig->Pin )
        {
            pinState  = Rtc_InitOutputGpio( outputConfig->Pin );
            out2EnVal = RTC_CR_OUT2EN;
        }
        else
        {
            /* No pin configured: the output function is disabled as a whole (RTC_OUT1 / PC13
               is not supported, see the Rtc.c file header), OSEL/COE/OUT2EN all stay clear
               regardless of the requested Signal */
        }

        if( RTC_REQUEST_OK == pinState )
        {
            if( RTC_OUT2_PIN_UNUSED == outputConfig->Pin )
            {
                /* OSEL/COE/OUT2EN already 0, nothing else to compute */
            }
            else
            {
                if( RTC_OUTPUT_ALARM == outputConfig->Signal )
                {
                    oselVal = RTC_CR_OSEL_0;
                }
                else
                {
                    if( RTC_OUTPUT_WAKEUP == outputConfig->Signal )
                    {
                        oselVal = ( RTC_CR_OSEL_0 | RTC_CR_OSEL_1 );
                    }
                    else
                    {
                        if( RTC_OUTPUT_CALIBRATION == outputConfig->Signal )
                        {
                            coeVal = RTC_CR_COE;
                        }
                        else
                        {
                            /* RTC_OUTPUT_DISABLED, OSEL/COE stay at 0 */
                        }
                    }
                }
            }

            Rtc_Reg_Unlock();
            Rtc_Reg_Set_RegVal( RTC_REG_CR,
                                ( RTC_CR_OSEL_Msk | RTC_CR_COE | RTC_CR_OUT2EN ),
                                ( oselVal | coeVal | out2EnVal ) );
            Rtc_Reg_Lock();
            retState = RTC_REQUEST_OK;
        }
        else
        {
            /* Pin GPIO configuration failed, nothing written */
            retState = RTC_REQUEST_ERROR;
        }
    }
    else
    {
        /* Module not initialized or invalid pointer, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}

/* ----------------------------- Backup registers ----------------------------- */

/**
 * \brief Returns the number of backup registers (forwarded to the Tamp module, see the
 *        Rtc.c file header).
 *
 * \param registerCnt [out]: Count read, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Get_BackupRegisterCount( rtc_BackupCnt_t * const registerCnt )
{
    rtc_RequestState_t  retState = RTC_REQUEST_ERROR;
    tamp_BackupCnt_t    tampCnt  = 0u;
    tamp_RequestState_t tampState = Tamp_Get_BackupRegisterCount( &tampCnt );

    if( ( RTC_NULL_PTR != registerCnt ) &&
        ( TAMP_REQUEST_OK == tampState )    )
    {
        *registerCnt = (rtc_BackupCnt_t)tampCnt;
        retState     = RTC_REQUEST_OK;
    }
    else
    {
        /* Invalid pointer or Tamp not initialized, nothing returned */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Writes a backup register (forwarded to the Tamp module).
 *
 * \param index [in]: Backup register index
 * \param data  [in]: Value to write
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_BackupRegister( rtc_BackupIndex_t index, rtc_BackupData_t data )
{
    rtc_RequestState_t  retState  = RTC_REQUEST_ERROR;
    tamp_RequestState_t tampState = Tamp_Set_BackupRegister( (tamp_BackupIndex_t)index, (tamp_BackupData_t)data );

    if( TAMP_REQUEST_OK == tampState )
    {
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Invalid index or Tamp not initialized, nothing written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Reads a backup register (forwarded to the Tamp module).
 *
 * \param index [in]:  Backup register index
 * \param data  [out]: Value read, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Get_BackupRegister( rtc_BackupIndex_t index, rtc_BackupData_t * const data )
{
    rtc_RequestState_t  retState  = RTC_REQUEST_ERROR;
    tamp_BackupData_t   tampData  = 0u;
    tamp_RequestState_t tampState = Tamp_Get_BackupRegister( (tamp_BackupIndex_t)index, &tampData );

    if( ( RTC_NULL_PTR != data ) &&
        ( TAMP_REQUEST_OK == tampState )    )
    {
        *data    = (rtc_BackupData_t)tampData;
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Invalid pointer, invalid index or Tamp not initialized, nothing returned */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}

/* ------------------------------- Interrupts ---------------------------------- */

/**
 * \brief Changes the RTC interrupt priority at the NVIC.
 *
 * \param irqPriority [in]: Priority to apply (0 - highest)
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Set_IrqPriority( rtc_IrqPrio_t irqPriority )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( RTC_FUNCTION_ACTIVE == rtc_State.Initialized )
    {
        nvic_RequestState_t nvicState = Nvic_Set_PeriphIrq_Prio( NVIC_PERIPH_IRQ_RTC, irqPriority );

        if( NVIC_REQUEST_OK == nvicState )
        {
            rtc_State.IrqPriority = irqPriority;
            retState              = RTC_REQUEST_OK;
        }
        else
        {
            /* Nvic rejected the priority, nothing changed */
            retState = RTC_REQUEST_ERROR;
        }
    }
    else
    {
        /* Module not initialized, nothing changed */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Reads the RTC interrupt priority applied at the NVIC.
 *
 * \param irqPriority [out]: Priority read, never \ref RTC_NULL_PTR
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_Get_IrqPriority( rtc_IrqPrio_t * const irqPriority )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;

    if( ( RTC_FUNCTION_ACTIVE == rtc_State.Initialized ) &&
        ( RTC_NULL_PTR != irqPriority )                     )
    {
        *irqPriority = rtc_State.IrqPriority;
        retState     = RTC_REQUEST_OK;
    }
    else
    {
        /* Module not initialized or invalid pointer, nothing returned */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}

/* ---------------------------------- Pins ------------------------------------- */

/**
 * \brief Not implemented in this version: RTC_TS exists only on PC13 with no GPIO_AF entry in
 *        the STM32CubeMX database, so no AF-mux pin table can be generated for it (see the
 *        Rtc.c file header deviation note).
 *
 * \return \ref RTC_REQUEST_ERROR always
 */
rtc_RequestState_t Rtc_InitTimestampGpio( void )
{
    return ( RTC_REQUEST_ERROR );
}


/**
 * \brief Configures the GPIO pin of the RTC_OUT2 alarm / calibration / wake-up output.
 *
 * \param outputPin [in]: Pin item, never \ref RTC_OUT2_PIN_UNUSED
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_InitOutputGpio( rtc_OutputPin_t outputPin )
{
    rtc_RequestState_t         retState = RTC_REQUEST_ERROR;
    const rtc_OutputPinInfo_t *pinInfo  = RTC_NULL_PTR;
    rtc_RequestState_t         lookupState = RTC_REQUEST_ERROR;

    if( RTC_OUT2_PIN_UNUSED != outputPin )
    {
        lookupState = Rtc_Reg_Get_OutputPinInfo( outputPin, &pinInfo );
    }
    else
    {
        /* Unused pin item, lookupState stays RTC_REQUEST_ERROR */
    }

    if( ( RTC_OUT2_PIN_UNUSED != outputPin ) &&
        ( RTC_REQUEST_OK == lookupState )       )
    {
        gpio_Config_t        gpioConfig;
        gpio_RequestState_t  gpioState;

        gpioConfig.PortId         = pinInfo->GpioPort;
        gpioConfig.PinId          = pinInfo->GpioPin;
        gpioConfig.PinMode        = GPIO_PIN_MODE_ALTERNATE;
        gpioConfig.PinPull        = GPIO_PIN_PULL_NONE;
        gpioConfig.PinSpeed       = GPIO_PIN_SPEED_LOW;
        gpioConfig.PinOutType     = GPIO_PIN_OUTPUT_PUSHPULL;
        gpioConfig.PinAltFunction = pinInfo->AltFunc;
        gpioConfig.PinActiveLevel = GPIO_PIN_LEVEL_HIGH;
        gpioState                = Gpio_Init( &gpioConfig );

        if( GPIO_REQUEST_OK == gpioState )
        {
            retState = RTC_REQUEST_OK;
        }
        else
        {
            /* Gpio rejected the configuration, nothing else changed */
            retState = RTC_REQUEST_ERROR;
        }
    }
    else
    {
        /* Unused or unknown pin item, nothing changed */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Configures the GPIO pin of the RTC_REFIN reference clock input.
 *
 * \param refInPin [in]: Pin item, never \ref RTC_REFIN_PIN_UNUSED
 * \return \ref RTC_REQUEST_OK on success, \ref RTC_REQUEST_ERROR otherwise
 */
rtc_RequestState_t Rtc_InitRefInGpio( rtc_RefInPin_t refInPin )
{
    rtc_RequestState_t         retState = RTC_REQUEST_ERROR;
    const rtc_RefInPinInfo_t  *pinInfo  = RTC_NULL_PTR;
    rtc_RequestState_t         lookupState = RTC_REQUEST_ERROR;

    if( RTC_REFIN_PIN_UNUSED != refInPin )
    {
        lookupState = Rtc_Reg_Get_RefInPinInfo( refInPin, &pinInfo );
    }
    else
    {
        /* Unused pin item, lookupState stays RTC_REQUEST_ERROR */
    }

    if( ( RTC_REFIN_PIN_UNUSED != refInPin ) &&
        ( RTC_REQUEST_OK == lookupState )       )
    {
        gpio_Config_t        gpioConfig;
        gpio_RequestState_t  gpioState;

        gpioConfig.PortId         = pinInfo->GpioPort;
        gpioConfig.PinId          = pinInfo->GpioPin;
        gpioConfig.PinMode        = GPIO_PIN_MODE_ALTERNATE;
        gpioConfig.PinPull        = GPIO_PIN_PULL_NONE;
        gpioConfig.PinSpeed       = GPIO_PIN_SPEED_LOW;
        gpioConfig.PinOutType     = GPIO_PIN_OUTPUT_PUSHPULL;
        gpioConfig.PinAltFunction = pinInfo->AltFunc;
        gpioConfig.PinActiveLevel = GPIO_PIN_LEVEL_HIGH;
        gpioState                = Gpio_Init( &gpioConfig );

        if( GPIO_REQUEST_OK == gpioState )
        {
            retState = RTC_REQUEST_OK;
        }
        else
        {
            /* Gpio rejected the configuration, nothing else changed */
            retState = RTC_REQUEST_ERROR;
        }
    }
    else
    {
        /* Unused or unknown pin item, nothing changed */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}

/* =========================== LOCAL FUNCTIONS ============================== */

/**
 * \brief Converts a binary digit pair (0..99) to BCD.
 */
static uint8_t Rtc_Bin2Bcd( uint8_t binVal )
{
    uint8_t tens = binVal / RTC_BCD_DIGIT_RADIX;
    uint8_t ones = binVal % RTC_BCD_DIGIT_RADIX;

    return ( (uint8_t)( ( tens << RTC_BCD_NIBBLE_SHIFT ) | ones ) );
}


/**
 * \brief Gregorian leap year rule.
 */
static rtc_FlagState_t Rtc_IsLeapYear( uint16_t year )
{
    rtc_FlagState_t retFlag = RTC_FLAG_INACTIVE;

    if( ( 0u == ( year % RTC_LEAP_YEAR_DIV_4 ) )                             &&
        ( ( 0u != ( year % RTC_LEAP_YEAR_DIV_100 ) )                         ||
          ( 0u == ( year % RTC_LEAP_YEAR_DIV_400 ) )                     )      )
    {
        retFlag = RTC_FLAG_ACTIVE;
    }
    else
    {
        retFlag = RTC_FLAG_INACTIVE;
    }

    return ( retFlag );
}


/**
 * \brief Validates a date and time (range of every field, day against the month / leap year).
 *        The weekday field is not validated: it is always recomputed from the date.
 */
static rtc_RequestState_t Rtc_ValidateDateTime( const rtc_DateTime_t * const dateTime )
{
    rtc_RequestState_t retState  = RTC_REQUEST_ERROR;
    uint8_t              maxDay   = 0u;

    if( ( RTC_NULL_PTR != dateTime )             &&
        ( RTC_YEAR_BASE <= dateTime->Year )      &&
        ( RTC_YEAR_MAX >= dateTime->Year )       &&
        ( RTC_MONTH_MIN <= dateTime->Month )     &&
        ( RTC_MONTH_CNT >= dateTime->Month )     &&
        ( RTC_HOUR_CNT > dateTime->Hour )        &&
        ( RTC_MINUTE_CNT > dateTime->Minute )    &&
        ( RTC_SECOND_CNT > dateTime->Second )    &&
        ( RTC_MILLISECOND_CNT > dateTime->Millisecond )    )
    {
        rtc_FlagState_t leapYear = Rtc_IsLeapYear( dateTime->Year );

        maxDay = rtc_DaysInMonth[ dateTime->Month ];

        if( ( RTC_MONTH_FEBRUARY == dateTime->Month ) &&
            ( RTC_FLAG_ACTIVE == leapYear )               )
        {
            maxDay++;
        }
        else
        {
            /* Not February on a leap year, the table value already applies */
        }

        if( ( RTC_DAY_MIN <= dateTime->Day ) &&
            ( maxDay >= dateTime->Day )          )
        {
            retState = RTC_REQUEST_OK;
        }
        else
        {
            /* Day out of range for the month / year */
            retState = RTC_REQUEST_ERROR;
        }
    }
    else
    {
        /* Invalid pointer or a field out of range */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Days since 1970-01-01 of a civil (year, month, day) date (Howard Hinnant's algorithm).
 */
static int32_t Rtc_DaysFromCivil( uint16_t year, uint8_t month, uint8_t day )
{
    int32_t  y = (int32_t)year;
    uint32_t mAdj;
    int32_t  era;
    uint32_t yoe;
    uint32_t doy;
    uint32_t doe;

    if( RTC_CIVIL_MONTH_ADJUST >= month )
    {
        y--;
        mAdj = (uint32_t)month + RTC_CIVIL_MONTH_OFFSET_EARLY;
    }
    else
    {
        mAdj = (uint32_t)month - RTC_CIVIL_MONTH_OFFSET_LATE;
    }

    if( 0 <= y )
    {
        era = y / RTC_CIVIL_ERA_YEARS;
    }
    else
    {
        era = ( y - RTC_CIVIL_ERA_YEAR_ADJUST ) / RTC_CIVIL_ERA_YEARS;
    }

    yoe = (uint32_t)( y - ( era * RTC_CIVIL_ERA_YEARS ) );
    doy = ( ( RTC_CIVIL_DOY_NUM * mAdj ) + RTC_CIVIL_DOY_ADD ) / RTC_CIVIL_DOY_DIV +
          (uint32_t)day - RTC_DAY_MIN;
    doe = ( yoe * RTC_CIVIL_DOE_YEAR_DAYS ) + ( yoe / RTC_CIVIL_DOE_DIV4 ) -
          ( yoe / RTC_CIVIL_DOE_DIV100 ) + doy;

    return ( ( era * RTC_CIVIL_ERA_DAYS ) + (int32_t)doe - RTC_CIVIL_EPOCH_OFFSET );
}


/**
 * \brief Inverse of \ref Rtc_DaysFromCivil (valid for non-negative \p days, i.e. 1970-01-01 or
 *        later, which always holds for the supported \ref RTC_YEAR_BASE .. \ref RTC_YEAR_MAX
 *        range).
 */
static void Rtc_CivilFromDays( int32_t days, uint16_t * const year, uint8_t * const month, uint8_t * const day )
{
    int32_t  z   = days + RTC_CIVIL_EPOCH_OFFSET;
    int32_t  era = z / RTC_CIVIL_ERA_DAYS;
    uint32_t doe = (uint32_t)( z - ( era * RTC_CIVIL_ERA_DAYS ) );
    uint32_t yoe = ( doe - ( doe / RTC_CIVIL_YOE_DIV1460 ) + ( doe / RTC_CIVIL_YOE_DIV36524 ) -
                     ( doe / RTC_CIVIL_YOE_DIV146096 ) ) / RTC_CIVIL_DOE_YEAR_DAYS;
    int32_t  y   = (int32_t)yoe + ( era * RTC_CIVIL_ERA_YEARS );
    uint32_t doy = doe - ( ( RTC_CIVIL_DOE_YEAR_DAYS * yoe ) + ( yoe / RTC_CIVIL_DOE_DIV4 ) -
                           ( yoe / RTC_CIVIL_DOE_DIV100 ) );
    uint32_t mp  = ( ( RTC_CIVIL_DOY_DIV * doy ) + RTC_CIVIL_DOY_ADD ) / RTC_CIVIL_DOY_NUM;
    uint32_t d   = doy - ( ( ( RTC_CIVIL_DOY_NUM * mp ) + RTC_CIVIL_DOY_ADD ) / RTC_CIVIL_DOY_DIV ) +
                   RTC_DAY_MIN;
    uint32_t m;

    if( RTC_CIVIL_MP_THRESHOLD > mp )
    {
        m = mp + RTC_CIVIL_MP_OFFSET_LATE;
    }
    else
    {
        m = mp - RTC_CIVIL_MP_OFFSET_EARLY;
    }

    if( RTC_CIVIL_MONTH_ADJUST >= m )
    {
        y++;
    }
    else
    {
        /* Not January / February, no year carry */
    }

    *year  = (uint16_t)y;
    *month = (uint8_t)m;
    *day   = (uint8_t)d;
}


/**
 * \brief ISO-8601 weekday (1 = Monday .. 7 = Sunday) of a days-since-1970-01-01 value
 *        (1970-01-01 is a Thursday, ISO day 4).
 */
static rtc_WeekDay_t Rtc_ComputeWeekDay( int32_t days )
{
    return ( (rtc_WeekDay_t)( ( (uint32_t)( days + RTC_WEEKDAY_EPOCH_OFFSET ) % RTC_WEEKDAY_MODULO ) +
                              RTC_DAY_MIN ) );
}


/**
 * \brief Looks up the Rcc mapping row for a \ref rtc_ClkSource_t item.
 */
static rtc_RequestState_t Rtc_FindClkSourceByRtc( rtc_ClkSource_t rtcSrc, const rtc_ClkSourceMap_t * * const row )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;
    const uint32_t      rowCnt  = (uint32_t)( sizeof(rtc_ClkSourceList) / sizeof(rtc_ClkSourceMap_t) );

    for( uint32_t rowIdx = 0u; rowCnt > rowIdx; rowIdx++ )
    {
        if( rtcSrc == rtc_ClkSourceList[rowIdx].RtcSrc )
        {
            *row     = &rtc_ClkSourceList[rowIdx];
            retState = RTC_REQUEST_OK;
            break;
        }
        else
        {
            /* Not this row, keep searching */
        }
    }

    return ( retState );
}


/**
 * \brief Writes TR/DR with the given date and time in BCD, weekday recomputed from the date.
 *        Must be called with the write protection unlocked and the init mode entered.
 */
static void Rtc_WriteCalendarRaw( const rtc_DateTime_t * const dateTime )
{
    int32_t       days    = Rtc_DaysFromCivil( dateTime->Year, dateTime->Month, dateTime->Day );
    rtc_WeekDay_t weekDay = Rtc_ComputeWeekDay( days );
    uint8_t       yearBin = (uint8_t)( dateTime->Year - RTC_YEAR_BASE );
    uint8_t       hourBcd = Rtc_Bin2Bcd( dateTime->Hour );
    uint8_t       minBcd  = Rtc_Bin2Bcd( dateTime->Minute );
    uint8_t       secBcd  = Rtc_Bin2Bcd( dateTime->Second );
    uint8_t       dayBcd  = Rtc_Bin2Bcd( dateTime->Day );
    uint8_t       monBcd  = Rtc_Bin2Bcd( dateTime->Month );
    uint8_t       yearBcd = Rtc_Bin2Bcd( yearBin );

    Rtc_Reg_Set_RegVal( RTC_REG_TR,
                        RTC_REG_ALL_BITS,
                        ( ( (uint32_t)( hourBcd >> RTC_BCD_NIBBLE_SHIFT ) << RTC_TR_HT_Pos  ) |
                          ( (uint32_t)( hourBcd & RTC_BCD_NIBBLE_MASK ) << RTC_TR_HU_Pos  ) |
                          ( (uint32_t)( minBcd  >> RTC_BCD_NIBBLE_SHIFT ) << RTC_TR_MNT_Pos ) |
                          ( (uint32_t)( minBcd  & RTC_BCD_NIBBLE_MASK ) << RTC_TR_MNU_Pos ) |
                          ( (uint32_t)( secBcd  >> RTC_BCD_NIBBLE_SHIFT ) << RTC_TR_ST_Pos  ) |
                          ( (uint32_t)( secBcd  & RTC_BCD_NIBBLE_MASK ) << RTC_TR_SU_Pos  )    ) );

    Rtc_Reg_Set_RegVal( RTC_REG_DR,
                        RTC_REG_ALL_BITS,
                        ( ( (uint32_t)( yearBcd >> RTC_BCD_NIBBLE_SHIFT )  << RTC_DR_YT_Pos ) |
                          ( (uint32_t)( yearBcd & RTC_BCD_NIBBLE_MASK )  << RTC_DR_YU_Pos ) |
                          ( (uint32_t)weekDay              << RTC_DR_WDU_Pos ) |
                          ( (uint32_t)( monBcd  >> RTC_BCD_NIBBLE_SHIFT )  << RTC_DR_MT_Pos ) |
                          ( (uint32_t)( monBcd  & RTC_BCD_NIBBLE_MASK )  << RTC_DR_MU_Pos ) |
                          ( (uint32_t)( dayBcd  >> RTC_BCD_NIBBLE_SHIFT )  << RTC_DR_DT_Pos ) |
                          ( (uint32_t)( dayBcd  & RTC_BCD_NIBBLE_MASK )  << RTC_DR_DU_Pos )    ) );
}


/**
 * \brief Reads TR/DR/SSR once and converts them to \ref rtc_DateTime_t (binary, weekday as
 *        stored, milliseconds derived from SSR using the recorded PREDIV_S).
 */
static void Rtc_ReadCalendarRaw( rtc_DateTime_t * const dateTime )
{
    uint32_t drReg  = Rtc_Reg_Get_RegVal( RTC_REG_DR, RTC_REG_ALL_BITS );
    uint32_t trReg  = Rtc_Reg_Get_RegVal( RTC_REG_TR, RTC_REG_ALL_BITS );
    uint32_t ssrReg = Rtc_Reg_Get_RegVal( RTC_REG_SSR, RTC_REG_ALL_BITS );
    uint8_t  yearTens  = (uint8_t)( ( drReg & RTC_DR_YT_Msk ) >> RTC_DR_YT_Pos );
    uint8_t  yearOnes  = (uint8_t)( ( drReg & RTC_DR_YU_Msk ) >> RTC_DR_YU_Pos );
    uint8_t  monthTens = (uint8_t)( ( drReg & RTC_DR_MT_Msk ) >> RTC_DR_MT_Pos );
    uint8_t  monthOnes = (uint8_t)( ( drReg & RTC_DR_MU_Msk ) >> RTC_DR_MU_Pos );
    uint8_t  dayTens   = (uint8_t)( ( drReg & RTC_DR_DT_Msk ) >> RTC_DR_DT_Pos );
    uint8_t  dayOnes   = (uint8_t)( ( drReg & RTC_DR_DU_Msk ) >> RTC_DR_DU_Pos );
    uint8_t  hourTens  = (uint8_t)( ( trReg & RTC_TR_HT_Msk ) >> RTC_TR_HT_Pos );
    uint8_t  hourOnes  = (uint8_t)( ( trReg & RTC_TR_HU_Msk ) >> RTC_TR_HU_Pos );
    uint8_t  minTens   = (uint8_t)( ( trReg & RTC_TR_MNT_Msk ) >> RTC_TR_MNT_Pos );
    uint8_t  minOnes   = (uint8_t)( ( trReg & RTC_TR_MNU_Msk ) >> RTC_TR_MNU_Pos );
    uint8_t  secTens   = (uint8_t)( ( trReg & RTC_TR_ST_Msk ) >> RTC_TR_ST_Pos );
    uint8_t  secOnes   = (uint8_t)( ( trReg & RTC_TR_SU_Msk ) >> RTC_TR_SU_Pos );
    uint8_t  yearBin   = (uint8_t)( ( yearTens * RTC_BCD_DIGIT_RADIX ) + yearOnes );

    dateTime->Year    = (uint16_t)( RTC_YEAR_BASE + yearBin );
    dateTime->Month   = (uint8_t)( ( monthTens * RTC_BCD_DIGIT_RADIX ) + monthOnes );
    dateTime->Day     = (uint8_t)( ( dayTens   * RTC_BCD_DIGIT_RADIX ) + dayOnes   );
    dateTime->WeekDay = (rtc_WeekDay_t)( ( drReg & RTC_DR_WDU_Msk ) >> RTC_DR_WDU_Pos );
    dateTime->Hour    = (uint8_t)( ( hourTens  * RTC_BCD_DIGIT_RADIX ) + hourOnes  );
    dateTime->Minute  = (uint8_t)( ( minTens   * RTC_BCD_DIGIT_RADIX ) + minOnes   );
    dateTime->Second  = (uint8_t)( ( secTens   * RTC_BCD_DIGIT_RADIX ) + secOnes   );

    if( 0u < rtc_State.PredivS )
    {
        uint32_t ssVal = ssrReg & RTC_SSR_SS_Msk;
        uint32_t elapsed = rtc_State.PredivS - ssVal;

        dateTime->Millisecond = (uint16_t)( ( elapsed * RTC_MILLISECOND_CNT ) /
                                             ( rtc_State.PredivS + 1u ) );
    }
    else
    {
        dateTime->Millisecond = 0u;
    }
}


/**
 * \brief Verifies that PRER and CR.FMT read back as written by \ref Rtc_Init's lost-calendar
 *        branch (rule 2.7: Init verifies its own result by read-back).
 */
static rtc_RequestState_t Rtc_VerifyPrescaler( uint32_t predivA, uint32_t predivS )
{
    rtc_RequestState_t retState     = RTC_REQUEST_ERROR;
    uint32_t             prerReg     = Rtc_Reg_Get_RegVal( RTC_REG_PRER,
                                                            ( RTC_PRER_PREDIV_A_Msk | RTC_PRER_PREDIV_S_Msk ) );
    uint32_t             expectedPrer = ( ( predivA << RTC_PRER_PREDIV_A_Pos ) |
                                          ( predivS << RTC_PRER_PREDIV_S_Pos )    );
    uint32_t             fmtBit       = Rtc_Reg_Get_RegBit( RTC_REG_CR, RTC_CR_FMT );

    if( ( prerReg == expectedPrer ) &&
        ( RTC_REG_BITS_CLEAR == fmtBit )    )
    {
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* PRER or CR.FMT does not match what was written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Verifies that the calendar reads back the date/time just written (rule 2.7: Init /
 *        a write verifies its own result by read-back). The weekday (recomputed by the
 *        module, never supplied by the caller) and the millisecond (sub-second, not part of
 *        the write) are not part of the comparison.
 */
static rtc_RequestState_t Rtc_VerifyDateTimeWritten( const rtc_DateTime_t * const dateTime )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;
    rtc_DateTime_t       readBack;

    Rtc_ReadCalendarRaw( &readBack );

    if( ( readBack.Year   == dateTime->Year )   &&
        ( readBack.Month  == dateTime->Month )  &&
        ( readBack.Day    == dateTime->Day )    &&
        ( readBack.Hour   == dateTime->Hour )   &&
        ( readBack.Minute == dateTime->Minute ) &&
        ( readBack.Second == dateTime->Second )    )
    {
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* The calendar does not read back the date/time just written */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Bounded wait until a register bit reads clear.
 */
static rtc_RequestState_t Rtc_WaitRegBitClear( rtc_RegId_t regId, uint32_t bitMask )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;
    uint32_t             bitVal  = bitMask;

    for( uint32_t waitIdx = 0u; RTC_WAIT_LIMIT > waitIdx; waitIdx++ )
    {
        bitVal = Rtc_Reg_Get_RegBit( regId, bitMask );

        if( 0u == bitVal )
        {
            break;
        }
        else
        {
            /* Bit still set, keep waiting */
        }
    }

    if( 0u == bitVal )
    {
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Timeout, caller reports the error */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Bounded wait until the calendar shadow registers are synchronized (ICSR.RSF).
 *
 * The shadow registers (SSR, TR, DR) are read by the module, RSF is cleared by the hardware in the
 * initialization mode, so they hold the old calendar until it is set again after an initialization
 * (RM0481 "Calendar ultra-low-power mode" / "Reading the calendar"). RSF is set again within two
 * RTC clock periods, it stays set while the calendar runs.
 *
 * \return \ref RTC_REQUEST_OK when RSF is set, \ref RTC_REQUEST_ERROR on timeout (RTC clock stopped)
 */
static rtc_RequestState_t Rtc_WaitCalendarSync( void )
{
    return ( Rtc_WaitRegBitSet( RTC_REG_ICSR, RTC_ICSR_RSF ) );
}


/**
 * \brief Bounded wait until a register bit reads set.
 */
static rtc_RequestState_t Rtc_WaitRegBitSet( rtc_RegId_t regId, uint32_t bitMask )
{
    rtc_RequestState_t retState = RTC_REQUEST_ERROR;
    uint32_t             bitVal  = 0u;

    for( uint32_t waitIdx = 0u; RTC_WAIT_LIMIT > waitIdx; waitIdx++ )
    {
        bitVal = Rtc_Reg_Get_RegBit( regId, bitMask );

        if( bitMask == bitVal )
        {
            break;
        }
        else
        {
            /* Bit not set yet, keep waiting */
        }
    }

    if( bitMask == bitVal )
    {
        retState = RTC_REQUEST_OK;
    }
    else
    {
        /* Timeout, caller reports the error */
        retState = RTC_REQUEST_ERROR;
    }

    return ( retState );
}


/**
 * \brief Builds the ALRMAR / ALRMBR register value from an alarm configuration.
 */
static uint32_t Rtc_BuildAlarmReg( const rtc_AlarmConfig_t * const alarmConfig )
{
    uint8_t  dayBcd  = Rtc_Bin2Bcd( alarmConfig->Day );
    uint8_t  hourBcd = Rtc_Bin2Bcd( alarmConfig->Hour );
    uint8_t  minBcd  = Rtc_Bin2Bcd( alarmConfig->Minute );
    uint8_t  secBcd  = Rtc_Bin2Bcd( alarmConfig->Second );
    uint32_t maskBits = 0u;
    uint32_t wdSel    = 0u;

    if( 0u != ( alarmConfig->Mask & RTC_ALARM_MASK_SECONDS ) )
    {
        maskBits |= RTC_ALRMAR_MSK1;
    }
    else
    {
        /* Seconds compared */
    }

    if( 0u != ( alarmConfig->Mask & RTC_ALARM_MASK_MINUTES ) )
    {
        maskBits |= RTC_ALRMAR_MSK2;
    }
    else
    {
        /* Minutes compared */
    }

    if( 0u != ( alarmConfig->Mask & RTC_ALARM_MASK_HOURS ) )
    {
        maskBits |= RTC_ALRMAR_MSK3;
    }
    else
    {
        /* Hours compared */
    }

    if( 0u != ( alarmConfig->Mask & RTC_ALARM_MASK_DAY ) )
    {
        maskBits |= RTC_ALRMAR_MSK4;
    }
    else
    {
        /* Day / weekday compared */
    }

    if( 0u != ( alarmConfig->Mask & RTC_ALARM_MASK_WEEKDAY ) )
    {
        wdSel = RTC_ALRMAR_WDSEL;
    }
    else
    {
        /* Day field is a date, not a weekday */
    }

    return ( maskBits | wdSel |
             ( (uint32_t)( dayBcd  >> RTC_BCD_NIBBLE_SHIFT )  << RTC_ALRMAR_DT_Pos ) |
             ( (uint32_t)( dayBcd  & RTC_BCD_NIBBLE_MASK )  << RTC_ALRMAR_DU_Pos ) |
             ( (uint32_t)( hourBcd >> RTC_BCD_NIBBLE_SHIFT )  << RTC_ALRMAR_HT_Pos ) |
             ( (uint32_t)( hourBcd & RTC_BCD_NIBBLE_MASK )  << RTC_ALRMAR_HU_Pos ) |
             ( (uint32_t)( minBcd  >> RTC_BCD_NIBBLE_SHIFT )  << RTC_ALRMAR_MNT_Pos ) |
             ( (uint32_t)( minBcd  & RTC_BCD_NIBBLE_MASK )  << RTC_ALRMAR_MNU_Pos ) |
             ( (uint32_t)( secBcd  >> RTC_BCD_NIBBLE_SHIFT )  << RTC_ALRMAR_ST_Pos ) |
             ( (uint32_t)( secBcd  & RTC_BCD_NIBBLE_MASK )  << RTC_ALRMAR_SU_Pos )    );
}

/**
 * \brief Requests the backup domain write access from the Pwr module, once per module life
 *        (a repeated \ref Rtc_Init does not take a second reference).
 *
 * \return \ref RTC_REQUEST_OK when the module holds the access, \ref RTC_REQUEST_ERROR when
 *         the Pwr module refused it
 */
static rtc_RequestState_t Rtc_BackupAccessAcquire( void )
{
    rtc_RequestState_t retState = RTC_REQUEST_OK;

    if( RTC_FUNCTION_ACTIVE != rtc_State.BackupAccess )
    {
        const pwr_RequestState_t pwrState = Pwr_Set_BackupAccessActive();

        if( PWR_REQUEST_OK == pwrState )
        {
            rtc_State.BackupAccess = RTC_FUNCTION_ACTIVE;
        }
        else
        {
            retState = RTC_REQUEST_ERROR;
        }
    }
    else
    {
        /* The access is already held */
    }

    return ( retState );
}


/**
 * \brief Releases the backup domain write access taken by \ref Rtc_BackupAccessAcquire. The
 *        module forgets the access even when the Pwr module refuses the release.
 *
 * \return \ref RTC_REQUEST_OK (nothing held, or released), \ref RTC_REQUEST_ERROR when the Pwr
 *         module refused the release
 */
static rtc_RequestState_t Rtc_BackupAccessRelease( void )
{
    rtc_RequestState_t retState = RTC_REQUEST_OK;

    if( RTC_FUNCTION_ACTIVE == rtc_State.BackupAccess )
    {
        const pwr_RequestState_t pwrState = Pwr_Set_BackupAccessInactive();

        rtc_State.BackupAccess = RTC_FUNCTION_INACTIVE;

        if( PWR_REQUEST_OK != pwrState )
        {
            retState = RTC_REQUEST_ERROR;
        }
        else
        {
            /* Released */
        }
    }
    else
    {
        /* Nothing held */
    }

    return ( retState );
}

/* =========================== INTERRUPT HANDLERS =========================== */

/* ================================ TASKS =================================== */

/**
 * \brief Reads the RTC status flags, calls the matching callback and clears the flag. Called
 *        from \ref Rtc_Task (polled, not a real NVIC vector in this version: the module only
 *        sets the NVIC priority in \ref Rtc_Init, matching the Tamp module precedent of not
 *        wiring \ref Nvic_Set_PeriphIrq_Handler / \ref Nvic_Set_PeriphIrq_Active either -
 *        tracked as a deviation in the module report, not a real interrupt handler).
 */
static void Rtc_PollEvents( void )
{
    uint32_t srReg = Rtc_Reg_Get_RegVal( RTC_REG_SR, RTC_REG_ALL_BITS );

    if( 0u != ( srReg & RTC_SR_ALRAF ) )
    {
        Rtc_Reg_Set_RegBit( RTC_REG_SCR, RTC_SCR_CALRAF );

        if( RTC_NULL_PTR != rtc_State.AlarmCallback[ RTC_ALARM_A ] )
        {
            rtc_State.AlarmCallback[ RTC_ALARM_A ]();
        }
        else
        {
            /* No callback registered */
        }
    }
    else
    {
        /* Alarm A did not match */
    }

    if( 0u != ( srReg & RTC_SR_ALRBF ) )
    {
        Rtc_Reg_Set_RegBit( RTC_REG_SCR, RTC_SCR_CALRBF );

        if( RTC_NULL_PTR != rtc_State.AlarmCallback[ RTC_ALARM_B ] )
        {
            rtc_State.AlarmCallback[ RTC_ALARM_B ]();
        }
        else
        {
            /* No callback registered */
        }
    }
    else
    {
        /* Alarm B did not match */
    }

    if( 0u != ( srReg & RTC_SR_WUTF ) )
    {
        Rtc_Reg_Set_RegBit( RTC_REG_SCR, RTC_SCR_CWUTF );

        if( RTC_NULL_PTR != rtc_State.WakeUpCallback )
        {
            rtc_State.WakeUpCallback();
        }
        else
        {
            /* No callback registered */
        }
    }
    else
    {
        /* Wake-up timer did not elapse */
    }

    if( 0u != ( srReg & RTC_SR_TSF ) )
    {
        Rtc_Reg_Set_RegBit( RTC_REG_SCR, RTC_SCR_CTSF );

        if( RTC_NULL_PTR != rtc_State.TimestampCallback )
        {
            rtc_State.TimestampCallback();
        }
        else
        {
            /* No callback registered */
        }
    }
    else
    {
        /* No timestamp captured */
    }
}
