/**
 * \author Mr.Nobody
 * \file Test_Rtc.c
 * \ingroup Rtc
 * \brief Unit tests of the Rtc module (STM32H5).
 *
 * Rtc.c and Rtc_Reg.c are compiled unchanged. RTC registers are emulated by RegMem (plain
 * memory: write-1-to-clear and other hardware side effects are not emulated). The INIT/INITF
 * and the ALRAE/ALRBE/WUTE enable -> ready-flag handshakes are emulated by a small RegMem HW
 * model (\ref UtRtc_HwModel), activated per test right before the call under test, the same
 * pattern as the Wwdg module tests. Rcc, Pwr, Tamp, Nvic and Gpio are mocked by CMock.
 */

/* ============================= INCLUDES =================================== */
#include "unity.h"                          /* Unity testing framework        */
#include "RegMem.h"                         /* Register memory emulation      */
#include "Rtc_Port.h"                        /* Module under test              */
#include "MockRcc_Port.h"                    /* RCC module mock                */
#include "MockPwr_Port.h"                    /* PWR module mock                */
#include "MockTamp_Port.h"                   /* TAMP module mock               */
#include "MockNvic_Port.h"                   /* NVIC module mock               */
#include "MockGpio_Port.h"                   /* GPIO module mock               */
#include "Stm32.h"                           /* RTC registers definition       */
/* ============================= TYPEDEFS =================================== */

/* ======================= FORWARD DECLARATIONS ============================= */

static void UtRtc_HwModel( void );
static void UtRtc_HwModel_NoSync( void );
static void UtRtc_FillDefaultConfig( rtc_Config_t * const config );
static void UtRtc_InitAdopt( void );

/* ========================== SYMBOLIC CONSTANTS ============================ */

/** Default interrupt priority used by the tests (any valid value works) */
#define UT_RTC_IRQ_PRIO                      ( 5u )

/** Year of the known valid date used by most calendar tests (2026-10-21, a Wednesday) */
#define UT_RTC_YEAR                          ( 2026u )
#define UT_RTC_MONTH                        ( 10u )
#define UT_RTC_DAY                          ( 21u )
#define UT_RTC_HOUR                          ( 12u )
#define UT_RTC_MINUTE                        ( 34u )
#define UT_RTC_SECOND                        ( 56u )

/** 2024-02-29 (leap day, a Thursday) Unix timestamp used by the Unix time round trip test */
#define UT_RTC_LEAP_UNIX_TIME                ( 1709164800u )
#define UT_RTC_LEAP_YEAR                      ( 2024u )
#define UT_RTC_LEAP_MONTH                     ( 2u )
#define UT_RTC_LEAP_DAY                       ( 29u )

/** 2000-01-06 00:00:00, exactly 10962 days (an exact multiple of 7) after the Unix epoch
 *  1970-01-01: its weekday only depends on 1970-01-01 itself being a Thursday
 *  (RTC_WEEKDAY_EPOCH_OFFSET), locking in that anchor without needing a pre-2000 date (out of
 *  the supported RTC_YEAR_BASE .. RTC_YEAR_MAX range). */
#define UT_RTC_EPOCH_ANCHOR_UNIX_TIME          ( 947116800u )
#define UT_RTC_EPOCH_ANCHOR_YEAR               ( 2000u )
#define UT_RTC_EPOCH_ANCHOR_MONTH              ( 1u )
#define UT_RTC_EPOCH_ANCHOR_DAY                ( 6u )

/** Invalid field values used by the validation tests */
#define UT_RTC_MONTH_INVALID                  ( 13u )
#define UT_RTC_MONTH_APRIL                    ( 4u )
#define UT_RTC_DAY_INVALID_APRIL              ( 31u )
#define UT_RTC_HOUR_INVALID                   ( 24u )

/** Alarm configuration used by the alarm tests (06:30 every day) */
#define UT_RTC_ALARM_HOUR                     ( 6u )
#define UT_RTC_ALARM_MINUTE                   ( 30u )

/** Wake-up timer period (ms) used by the test and the WUTR value it is expected to produce */
#define UT_RTC_WAKEUP_PERIOD_MS               ( 5000u )
#define UT_RTC_WAKEUP_WUTR_EXPECTED           ( 4u )

/** BCD digits of the preset timestamp used by \ref Ut_Rtc_Get_Timestamp_PresetRegisters_DecodesFields */
#define UT_RTC_TS_HOUR_TENS                   ( 1u )
#define UT_RTC_TS_HOUR_ONES                   ( 2u )
#define UT_RTC_TS_MIN_TENS                    ( 3u )
#define UT_RTC_TS_MIN_ONES                    ( 4u )
#define UT_RTC_TS_SEC_TENS                    ( 5u )
#define UT_RTC_TS_SEC_ONES                    ( 6u )
#define UT_RTC_TS_DAY_TENS                    ( 2u )
#define UT_RTC_TS_DAY_ONES                    ( 1u )

/** Calibration values used by the calibration tests, in parts per billion */
#define UT_RTC_CALIB_POSITIVE_PPB             ( 400000 )
#define UT_RTC_CALIB_NEGATIVE_PPB             ( -20000 )
#define UT_RTC_CALIB_TOLERANCE_PPB            ( 1000 )

/** Time shift used by the time-shift test, in milliseconds */
#define UT_RTC_SHIFT_MS                       ( 500 )

/** Backup register count / index / data pattern used by the backup register tests */
#define UT_RTC_BACKUP_CNT                     ( 32u )
#define UT_RTC_BACKUP_INDEX                   ( 5u )
#define UT_RTC_BACKUP_DATA                    ( 0xA5A5A5A5u )
#define UT_RTC_BACKUP_DATA_READ               ( 0x12345678u )

/** Interrupt priority used by \ref Ut_Rtc_Set_IrqPriority_Get_IrqPriority_CachesValue */
#define UT_RTC_IRQ_PRIO_NEW                   ( 3u )

/* ============================ TEST FIXTURE ================================ */

void setUp( void )
{
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Reset() );
}


void tearDown( void )
{
    /* Mocks are verified by generated runner */
}

/* ========================== MODULE VERSION ================================ */

/**
 * \brief The module version is returned with the fields populated.
 * \par Expected results
 * Major / minor / patch fields hold the compiled-in version.
 */
void Ut_Rtc_Get_ModuleVersion_Call_ReturnsVersion( void )
{
    const rtc_ModuleVersion_t version = Rtc_Get_ModuleVersion();

    TEST_ASSERT_EQUAL( 1u, version.Major );
    TEST_ASSERT_EQUAL( 0u, version.Minor );
    TEST_ASSERT_EQUAL( 0u, version.Patch );
}

/* ========================== DEFAULT CONFIG ================================= */

/**
 * \brief A null configuration pointer is rejected.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned.
 */
void Ut_Rtc_Get_DefaultConfig_NullPtr_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Get_DefaultConfig( RTC_NULL_PTR ) );
}


/**
 * \brief A valid configuration pointer is filled with the LSE / 2000-01-01 defaults.
 * \par Expected results
 * \ref RTC_REQUEST_OK is returned, ClkSrc is LSE, the default date is 2000-01-01 00:00:00.
 */
void Ut_Rtc_Get_DefaultConfig_ValidPtr_FillsDefaults( void )
{
    rtc_Config_t config;

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_DefaultConfig( &config ) );
    TEST_ASSERT_EQUAL( RTC_CLK_SRC_LSE, config.ClkSrc );
    TEST_ASSERT_EQUAL( RTC_FUNCTION_INACTIVE, config.ForceInit );
    TEST_ASSERT_EQUAL( RTC_YEAR_BASE, config.DefaultDateTime.Year );
    TEST_ASSERT_EQUAL( 1u, config.DefaultDateTime.Month );
    TEST_ASSERT_EQUAL( 1u, config.DefaultDateTime.Day );
}

/* ============================== INIT ======================================= */

/**
 * \brief A null configuration pointer is rejected, nothing is read or written.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned, no mock is called.
 */
void Ut_Rtc_Init_NullPtr_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Init( RTC_NULL_PTR ) );
}


/**
 * \brief An invalid default date (month 13) is rejected before any register is touched.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned, no mock is called.
 */
void Ut_Rtc_Init_InvalidDate_ReturnsError( void )
{
    rtc_Config_t config;

    UtRtc_FillDefaultConfig( &config );
    config.DefaultDateTime.Month = UT_RTC_MONTH_INVALID;

    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Init( &config ) );
}


/**
 * \brief The requested clock source is already selected and running with an initialized
 *        calendar: the module adopts it (calendar state VALID) without writing TR/DR.
 * \par Expected results
 * \ref RTC_REQUEST_OK is returned, \ref Rtc_Get_TimeState reports \ref RTC_TIME_VALID, TR/DR
 * stay zero (not written).
 */
void Ut_Rtc_Init_RunningCalendarSameSource_AdoptsWithoutWrite( void )
{
    rtc_Config_t     config;
    rtc_TimeState_t  timeState;

    UtRtc_FillDefaultConfig( &config );
    RTC->ICSR = RTC_ICSR_INITS;

    Pwr_Set_BackupAccessActive_ExpectAndReturn( PWR_REQUEST_OK );
    Rcc_Get_RtcClkSource_ExpectAndReturn( RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_RtcClkSource_IgnoreArg_clkSource();
    Rcc_Get_RtcClkSource_ReturnThruPtr_clkSource( &( rcc_Rtc_ClkSource_t ){ RCC_RTC_CLK_SOURCE_LSE } );
    Rcc_Get_PeriphState_ExpectAndReturn( RCC_PERIPH_RTC_LSE, RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_PeriphState_IgnoreArg_funcState();
    Rcc_Get_PeriphState_ReturnThruPtr_funcState( &( rcc_FunctionState_t ){ RCC_FUNCTION_ACTIVE } );
    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_RTC, UT_RTC_IRQ_PRIO, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Init( &config ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_TimeState( &timeState ) );
    TEST_ASSERT_EQUAL( RTC_TIME_VALID, timeState );
    TEST_ASSERT_EQUAL( 0u, RTC->TR );
    TEST_ASSERT_EQUAL( 0u, RTC->DR );
}


/**
 * \brief The requested clock source is not enabled yet and the calendar was never
 *        initialized: the module enables the clock and initializes the default date/time.
 * \par Expected results
 * \ref RTC_REQUEST_OK is returned, \ref Rtc_Get_TimeState reports \ref RTC_TIME_LOST, the
 * readback date matches the 2000-01-01 00:00:00 default with the correct (Saturday) weekday.
 */
void Ut_Rtc_Init_LostCalendar_InitializesDefaultDateTime( void )
{
    rtc_Config_t     config;
    rtc_TimeState_t  timeState;
    rtc_DateTime_t   dateTime;

    UtRtc_FillDefaultConfig( &config );

    Pwr_Set_BackupAccessActive_ExpectAndReturn( PWR_REQUEST_OK );
    Rcc_Get_RtcClkSource_ExpectAndReturn( RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_RtcClkSource_IgnoreArg_clkSource();
    Rcc_Get_RtcClkSource_ReturnThruPtr_clkSource( &( rcc_Rtc_ClkSource_t ){ RCC_RTC_CLK_SOURCE_LSE } );
    Rcc_Get_PeriphState_ExpectAndReturn( RCC_PERIPH_RTC_LSE, RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_PeriphState_IgnoreArg_funcState();
    Rcc_Get_PeriphState_ReturnThruPtr_funcState( &( rcc_FunctionState_t ){ RCC_FUNCTION_INACTIVE } );
    Rcc_Set_PeriphActive_ExpectAndReturn( RCC_PERIPH_RTC_LSE, RCC_REQUEST_OK );
    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_RTC, UT_RTC_IRQ_PRIO, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtRtc_HwModel ) );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Init( &config ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_TimeState( &timeState ) );
    TEST_ASSERT_EQUAL( RTC_TIME_LOST, timeState );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_DateTime( &dateTime ) );
    TEST_ASSERT_EQUAL( RTC_YEAR_BASE, dateTime.Year );
    TEST_ASSERT_EQUAL( 1u, dateTime.Month );
    TEST_ASSERT_EQUAL( 1u, dateTime.Day );
    TEST_ASSERT_EQUAL( RTC_WEEKDAY_SATURDAY, dateTime.WeekDay );
    TEST_ASSERT_EQUAL( 0u, dateTime.Hour );
}


/**
 * \brief The backup domain has no clock source selected (RTCSEL = no clock after the first
 *        power-up or a backup domain reset, \ref Rcc_Get_RtcClkSource reports an error): the
 *        module selects and enables the requested source and initializes the calendar.
 * \par Expected results
 * \ref Rcc_Set_RtcClkSource and \ref Rcc_Set_PeriphActive are called with the requested source
 * (the mocks verify it), \ref RTC_REQUEST_OK is returned, \ref Rtc_Get_TimeState reports
 * \ref RTC_TIME_LOST.
 */
void Ut_Rtc_Init_NoSourceSelected_SelectsRequestedSource( void )
{
    rtc_Config_t     config;
    rtc_TimeState_t  timeState;

    UtRtc_FillDefaultConfig( &config );

    Pwr_Set_BackupAccessActive_ExpectAndReturn( PWR_REQUEST_OK );
    Rcc_Get_RtcClkSource_ExpectAndReturn( RTC_NULL_PTR, RCC_REQUEST_ERROR );
    Rcc_Get_RtcClkSource_IgnoreArg_clkSource();
    Rcc_Get_PeriphState_ExpectAndReturn( RCC_PERIPH_RTC_LSE, RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_PeriphState_IgnoreArg_funcState();
    Rcc_Get_PeriphState_ReturnThruPtr_funcState( &( rcc_FunctionState_t ){ RCC_FUNCTION_INACTIVE } );
    Rcc_Set_RtcClkSource_ExpectAndReturn( RCC_RTC_CLK_SOURCE_LSE, RCC_REQUEST_OK );
    Rcc_Set_PeriphActive_ExpectAndReturn( RCC_PERIPH_RTC_LSE, RCC_REQUEST_OK );
    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_RTC, UT_RTC_IRQ_PRIO, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtRtc_HwModel ) );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Init( &config ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_TimeState( &timeState ) );
    TEST_ASSERT_EQUAL( RTC_TIME_LOST, timeState );
}


/**
 * \brief A different clock source is already selected by the hardware: Rcc is expected to
 *        reject the change (RTCSEL write-once-per-backup-domain-life rule), the module stays
 *        uninitialized.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned, a later call that requires the module to be initialized
 * also fails, the backup domain access requested by the Init is released again.
 */
void Ut_Rtc_Init_DifferentSourceLocked_ReturnsError( void )
{
    rtc_Config_t     config;
    rtc_TimeState_t  timeState;

    UtRtc_FillDefaultConfig( &config );

    Pwr_Set_BackupAccessActive_ExpectAndReturn( PWR_REQUEST_OK );
    Rcc_Get_RtcClkSource_ExpectAndReturn( RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_RtcClkSource_IgnoreArg_clkSource();
    Rcc_Get_RtcClkSource_ReturnThruPtr_clkSource( &( rcc_Rtc_ClkSource_t ){ RCC_RTC_CLK_SOURCE_LSI } );
    Rcc_Get_PeriphState_ExpectAndReturn( RCC_PERIPH_RTC_LSE, RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_PeriphState_IgnoreArg_funcState();
    Rcc_Get_PeriphState_ReturnThruPtr_funcState( &( rcc_FunctionState_t ){ RCC_FUNCTION_INACTIVE } );
    Rcc_Set_RtcClkSource_ExpectAndReturn( RCC_RTC_CLK_SOURCE_LSE, RCC_REQUEST_ERROR );
    Pwr_Set_BackupAccessInactive_ExpectAndReturn( PWR_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Init( &config ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Get_TimeState( &timeState ) );
}


/**
 * \brief The Init enables the backup domain write access through the Pwr module, before the
 *        clock source of the backup domain is touched.
 * \par Expected results
 * \ref Pwr_Set_BackupAccessActive is called once (the mock verifies it), \ref RTC_REQUEST_OK.
 */
void Ut_Rtc_Init_ValidConfig_CallsPwrBackupAccessActive( void )
{
    UtRtc_InitAdopt();
}


/**
 * \brief The Pwr module refuses the backup domain write access.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned, neither Rcc nor Nvic is called, the module stays
 * uninitialized and no access is released (none was granted).
 */
void Ut_Rtc_Init_PwrBackupAccessFails_ReturnsErrorAndStaysUninitialized( void )
{
    rtc_Config_t     config;
    rtc_TimeState_t  timeState;

    UtRtc_FillDefaultConfig( &config );
    Pwr_Set_BackupAccessActive_ExpectAndReturn( PWR_REQUEST_ERROR );

    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Init( &config ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Get_TimeState( &timeState ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Deinit() );
}


/**
 * \brief A second Init of an initialized module does not take a second Pwr reference.
 * \par Expected results
 * Both Init calls return \ref RTC_REQUEST_OK, \ref Pwr_Set_BackupAccessActive is called once,
 * the Deinit releases the access once.
 */
void Ut_Rtc_Init_CalledTwice_TakesOnePwrReference( void )
{
    rtc_Config_t config;

    UtRtc_InitAdopt();
    UtRtc_FillDefaultConfig( &config );

    Rcc_Get_RtcClkSource_ExpectAndReturn( RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_RtcClkSource_IgnoreArg_clkSource();
    Rcc_Get_RtcClkSource_ReturnThruPtr_clkSource( &( rcc_Rtc_ClkSource_t ){ RCC_RTC_CLK_SOURCE_LSE } );
    Rcc_Get_PeriphState_ExpectAndReturn( RCC_PERIPH_RTC_LSE, RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_PeriphState_IgnoreArg_funcState();
    Rcc_Get_PeriphState_ReturnThruPtr_funcState( &( rcc_FunctionState_t ){ RCC_FUNCTION_ACTIVE } );
    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_RTC, UT_RTC_IRQ_PRIO, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Init( &config ) );

    Pwr_Set_BackupAccessInactive_ExpectAndReturn( PWR_REQUEST_OK );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Deinit() );
}


/**
 * \brief The Deinit releases the backup domain write access taken by the Init.
 * \par Expected results
 * \ref Pwr_Set_BackupAccessInactive is called once, \ref RTC_REQUEST_OK, the module is
 * uninitialized; a second Deinit does not release anything.
 */
void Ut_Rtc_Deinit_AfterInit_ReleasesPwrBackupAccess( void )
{
    rtc_TimeState_t timeState;

    UtRtc_InitAdopt();
    Pwr_Set_BackupAccessInactive_ExpectAndReturn( PWR_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Deinit() );
    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Get_TimeState( &timeState ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Deinit() );
}


/**
 * \brief The Pwr module refuses the release of the access.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned, the module is deinitialized anyway.
 */
void Ut_Rtc_Deinit_PwrReleaseFails_ReturnsError( void )
{
    rtc_TimeState_t timeState;

    UtRtc_InitAdopt();
    Pwr_Set_BackupAccessInactive_ExpectAndReturn( PWR_REQUEST_ERROR );

    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Deinit() );
    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Get_TimeState( &timeState ) );
}

/* --------------------------------- Time ------------------------------------ */

/**
 * \brief Writing the calendar before \ref Rtc_Init is rejected.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned, nothing written.
 */
void Ut_Rtc_Set_DateTime_NotInitialized_ReturnsError( void )
{
    rtc_DateTime_t dateTime = { UT_RTC_YEAR, UT_RTC_MONTH, UT_RTC_DAY, RTC_WEEKDAY_WEDNESDAY,
                                 UT_RTC_HOUR, UT_RTC_MINUTE, UT_RTC_SECOND, 0u };

    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Set_DateTime( &dateTime ) );
}


/**
 * \brief An invalid date (31st of April) is rejected.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned.
 */
void Ut_Rtc_Set_DateTime_InvalidDate_ReturnsError( void )
{
    rtc_DateTime_t dateTime = { UT_RTC_YEAR, UT_RTC_MONTH_APRIL, UT_RTC_DAY_INVALID_APRIL,
                                 RTC_WEEKDAY_WEDNESDAY, 0u, 0u, 0u, 0u };

    UtRtc_InitAdopt();

    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Set_DateTime( &dateTime ) );
}


/**
 * \brief A valid date is written and read back consistently, the weekday is recomputed by
 *        the module (the caller's WeekDay field is ignored).
 * \par Expected results
 * The readback date matches, WeekDay is \ref RTC_WEEKDAY_WEDNESDAY (2026-10-21).
 */
void Ut_Rtc_Set_DateTime_ValidDate_RoundTrips( void )
{
    rtc_DateTime_t setDateTime = { UT_RTC_YEAR, UT_RTC_MONTH, UT_RTC_DAY, RTC_WEEKDAY_MONDAY,
                                    UT_RTC_HOUR, UT_RTC_MINUTE, UT_RTC_SECOND, 0u };
    rtc_DateTime_t getDateTime;

    UtRtc_InitAdopt();
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtRtc_HwModel ) );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_DateTime( &setDateTime ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_DateTime( &getDateTime ) );
    TEST_ASSERT_EQUAL( UT_RTC_YEAR, getDateTime.Year );
    TEST_ASSERT_EQUAL( UT_RTC_MONTH, getDateTime.Month );
    TEST_ASSERT_EQUAL( UT_RTC_DAY, getDateTime.Day );
    TEST_ASSERT_EQUAL( RTC_WEEKDAY_WEDNESDAY, getDateTime.WeekDay );
    TEST_ASSERT_EQUAL( UT_RTC_HOUR, getDateTime.Hour );
    TEST_ASSERT_EQUAL( UT_RTC_MINUTE, getDateTime.Minute );
    TEST_ASSERT_EQUAL( UT_RTC_SECOND, getDateTime.Second );
}


/**
 * \brief The calendar write fails when the shadow registers do not synchronize.
 * \par Expected results
 * \ref Rtc_Set_DateTime returns \ref RTC_REQUEST_ERROR when ICSR.RSF stays cleared after the
 * initialization mode (the RTC clock does not run), the shadow registers still hold the old
 * calendar so the write can not be verified.
 */
void Ut_Rtc_Set_DateTime_ShadowNotSynchronized_ReturnsError( void )
{
    rtc_DateTime_t setDateTime = { UT_RTC_YEAR, UT_RTC_MONTH, UT_RTC_DAY, RTC_WEEKDAY_MONDAY,
                                    UT_RTC_HOUR, UT_RTC_MINUTE, UT_RTC_SECOND, 0u };

    UtRtc_InitAdopt();
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtRtc_HwModel_NoSync ) );

    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Set_DateTime( &setDateTime ) );
}


/**
 * \brief The calendar is not read while the shadow registers are not synchronized.
 * \par Expected results
 * \ref Rtc_Get_DateTime returns \ref RTC_REQUEST_ERROR and leaves the output untouched when
 * ICSR.RSF is cleared.
 */
void Ut_Rtc_Get_DateTime_ShadowNotSynchronized_ReturnsErrorOutputUntouched( void )
{
    rtc_DateTime_t dateTime = { UT_RTC_YEAR, UT_RTC_MONTH, UT_RTC_DAY, RTC_WEEKDAY_MONDAY,
                                UT_RTC_HOUR, UT_RTC_MINUTE, UT_RTC_SECOND, 0u };

    UtRtc_InitAdopt();

    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Get_DateTime( &dateTime ) );
    TEST_ASSERT_EQUAL( UT_RTC_YEAR, dateTime.Year );
    TEST_ASSERT_EQUAL( UT_RTC_SECOND, dateTime.Second );
}


/**
 * \brief A Unix timestamp of a known leap day is written and read back.
 * \par Expected results
 * \ref Rtc_Get_UnixTime returns the same value, the decoded calendar date is 2024-02-29.
 */
void Ut_Rtc_Set_UnixTime_Get_UnixTime_RoundTrips( void )
{
    rtc_UnixTime_t unixTime;
    rtc_DateTime_t dateTime;

    UtRtc_InitAdopt();
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtRtc_HwModel ) );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_UnixTime( UT_RTC_LEAP_UNIX_TIME ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_UnixTime( &unixTime ) );
    TEST_ASSERT_EQUAL( UT_RTC_LEAP_UNIX_TIME, unixTime );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_DateTime( &dateTime ) );
    TEST_ASSERT_EQUAL( UT_RTC_LEAP_YEAR, dateTime.Year );
    TEST_ASSERT_EQUAL( UT_RTC_LEAP_MONTH, dateTime.Month );
    TEST_ASSERT_EQUAL( UT_RTC_LEAP_DAY, dateTime.Day );
}


/**
 * \brief Locks in the Unix epoch weekday anchor (1970-01-01 is a Thursday) used by the
 *        weekday formula: 2000-01-06 is exactly 10962 days (a multiple of 7) after the epoch,
 *        so it reads back as a Thursday only if the epoch itself is correctly a Thursday.
 * \par Expected results
 * The decoded WeekDay is \ref RTC_WEEKDAY_THURSDAY.
 */
void Ut_Rtc_Set_UnixTime_EpochWeekMultiple_IsThursday( void )
{
    rtc_DateTime_t dateTime;

    UtRtc_InitAdopt();
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtRtc_HwModel ) );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_UnixTime( UT_RTC_EPOCH_ANCHOR_UNIX_TIME ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_DateTime( &dateTime ) );
    TEST_ASSERT_EQUAL( UT_RTC_EPOCH_ANCHOR_YEAR, dateTime.Year );
    TEST_ASSERT_EQUAL( UT_RTC_EPOCH_ANCHOR_MONTH, dateTime.Month );
    TEST_ASSERT_EQUAL( UT_RTC_EPOCH_ANCHOR_DAY, dateTime.Day );
    TEST_ASSERT_EQUAL( RTC_WEEKDAY_THURSDAY, dateTime.WeekDay );
}


/**
 * \brief Requesting a one hour daylight saving addition sets CR.ADD1H.
 * \par Expected results
 * RTC_CR_ADD1H is set in the CR register.
 */
void Ut_Rtc_Set_DaylightSaving_Add1h_SetsRegisterBit( void )
{
    UtRtc_InitAdopt();

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_DaylightSaving( RTC_DST_ADD_1H ) );
    TEST_ASSERT_EQUAL( RTC_CR_ADD1H, ( RTC->CR & RTC_CR_ADD1H ) );
}

/* -------------------------------- Alarms ------------------------------------ */

/**
 * \brief A valid alarm configuration is written to ALRMAR and left inactive.
 * \par Expected results
 * \ref RTC_REQUEST_OK is returned, \ref Rtc_Get_AlarmState reports \ref RTC_FUNCTION_INACTIVE
 * (not activated yet).
 */
void Ut_Rtc_Set_Alarm_ValidConfig_WritesRegisters( void )
{
    rtc_AlarmConfig_t    alarmConfig = { 0u, UT_RTC_ALARM_HOUR, UT_RTC_ALARM_MINUTE, 0u,
                                          RTC_ALARM_MASK_DAY, RTC_NULL_PTR };
    rtc_FunctionState_t  alarmState;

    UtRtc_InitAdopt();
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtRtc_HwModel ) );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_Alarm( RTC_ALARM_A, &alarmConfig ) );
    TEST_ASSERT_EQUAL( RTC_ALRMAR_MSK4, ( RTC->ALRMAR & RTC_ALRMAR_MSK4 ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_AlarmState( RTC_ALARM_A, &alarmState ) );
    TEST_ASSERT_EQUAL( RTC_FUNCTION_INACTIVE, alarmState );
}


/**
 * \brief An out of range hour (24) is rejected.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned.
 */
void Ut_Rtc_Set_Alarm_InvalidHour_ReturnsError( void )
{
    rtc_AlarmConfig_t alarmConfig = { 0u, UT_RTC_HOUR_INVALID, 0u, 0u, RTC_ALARM_MASK_DAY, RTC_NULL_PTR };

    UtRtc_InitAdopt();

    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Set_Alarm( RTC_ALARM_B, &alarmConfig ) );
}


/**
 * \brief \ref Rtc_Set_AlarmActive / \ref Rtc_Set_AlarmInactive toggle the CR enable bit.
 * \par Expected results
 * \ref Rtc_Get_AlarmState follows the last call.
 */
void Ut_Rtc_Set_AlarmActive_Inactive_TogglesEnableBit( void )
{
    rtc_FunctionState_t alarmState;

    UtRtc_InitAdopt();

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_AlarmActive( RTC_ALARM_B ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_AlarmState( RTC_ALARM_B, &alarmState ) );
    TEST_ASSERT_EQUAL( RTC_FUNCTION_ACTIVE, alarmState );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_AlarmInactive( RTC_ALARM_B ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_AlarmState( RTC_ALARM_B, &alarmState ) );
    TEST_ASSERT_EQUAL( RTC_FUNCTION_INACTIVE, alarmState );
}

/* ----------------------------- Wake-up timer -------------------------------- */

/**
 * \brief A valid period (5 s) is written to WUTR (period - 1) with the ck_spre clock selected.
 * \par Expected results
 * WUTR == 4, CR.WUCKSEL selects ck_spre (RTC_CR_WUCKSEL_2).
 */
void Ut_Rtc_Set_WakeUpTimer_ValidPeriod_WritesWutr( void )
{
    UtRtc_InitAdopt();
    TEST_ASSERT_EQUAL( REGMEM_REQUEST_OK, RegMem_Set_ModelActive( UtRtc_HwModel ) );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_WakeUpTimer( UT_RTC_WAKEUP_PERIOD_MS, RTC_NULL_PTR ) );
    TEST_ASSERT_EQUAL( UT_RTC_WAKEUP_WUTR_EXPECTED, ( RTC->WUTR & RTC_WUTR_WUT_Msk ) );
    TEST_ASSERT_EQUAL( RTC_CR_WUCKSEL_2, ( RTC->CR & RTC_CR_WUCKSEL_Msk ) );
}


/**
 * \brief A zero period is rejected.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned.
 */
void Ut_Rtc_Set_WakeUpTimer_ZeroPeriod_ReturnsError( void )
{
    UtRtc_InitAdopt();

    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_Set_WakeUpTimer( 0u, RTC_NULL_PTR ) );
}


/**
 * \brief The wake-up flag is read and cleared.
 * \par Expected results
 * The flag reads active after SR.WUTF is preset, inactive and SCR.CWUTF is set after clear.
 */
void Ut_Rtc_Get_WakeUpFlag_Set_WakeUpFlagClear( void )
{
    rtc_FlagState_t wakeUpFlag;

    UtRtc_InitAdopt();
    RTC->SR = RTC_SR_WUTF;

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_WakeUpFlag( &wakeUpFlag ) );
    TEST_ASSERT_EQUAL( RTC_FLAG_ACTIVE, wakeUpFlag );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_WakeUpFlagClear() );
    TEST_ASSERT_EQUAL( RTC_SCR_CWUTF, ( RTC->SCR & RTC_SCR_CWUTF ) );
}

/* ------------------------------- Timestamp ---------------------------------- */

/**
 * \brief The timestamp capture is configured and activated.
 * \par Expected results
 * CR.TSE is set, CR.TSEDGE follows the requested edge.
 */
void Ut_Rtc_Set_TimestampConfig_Active_SetsRegisterBits( void )
{
    rtc_TimestampConfig_t timestampConfig = { RTC_TS_EDGE_FALLING };

    UtRtc_InitAdopt();

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_TimestampConfig( &timestampConfig ) );
    TEST_ASSERT_EQUAL( RTC_CR_TSEDGE, ( RTC->CR & RTC_CR_TSEDGE ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_TimestampActive() );
    TEST_ASSERT_EQUAL( RTC_CR_TSE, ( RTC->CR & RTC_CR_TSE ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_TimestampInactive() );
    TEST_ASSERT_EQUAL( 0u, ( RTC->CR & RTC_CR_TSE ) );
}


/**
 * \brief A preset TSTR/TSDR (12:34:56, day 21) is decoded by \ref Rtc_Get_Timestamp.
 * \par Expected results
 * Hour / minute / second / day match the preset BCD fields.
 */
void Ut_Rtc_Get_Timestamp_PresetRegisters_DecodesFields( void )
{
    rtc_DateTime_t dateTime;

    UtRtc_InitAdopt();

    RTC->TSTR = ( UT_RTC_TS_HOUR_TENS << RTC_TR_HT_Pos ) | ( UT_RTC_TS_HOUR_ONES << RTC_TR_HU_Pos ) |
                ( UT_RTC_TS_MIN_TENS << RTC_TR_MNT_Pos ) | ( UT_RTC_TS_MIN_ONES << RTC_TR_MNU_Pos ) |
                ( UT_RTC_TS_SEC_TENS << RTC_TR_ST_Pos )  | ( UT_RTC_TS_SEC_ONES << RTC_TR_SU_Pos );
    RTC->TSDR = ( UT_RTC_TS_DAY_TENS << RTC_DR_DT_Pos ) | ( UT_RTC_TS_DAY_ONES << RTC_DR_DU_Pos );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_Timestamp( &dateTime ) );
    TEST_ASSERT_EQUAL( UT_RTC_HOUR, dateTime.Hour );
    TEST_ASSERT_EQUAL( UT_RTC_MINUTE, dateTime.Minute );
    TEST_ASSERT_EQUAL( UT_RTC_SECOND, dateTime.Second );
    TEST_ASSERT_EQUAL( UT_RTC_DAY, dateTime.Day );
}

/* ------------------------------- Calibration -------------------------------- */

/**
 * \brief A positive calibration value is split into CALP (coarse +) and CALM (fine -).
 * \par Expected results
 * CALR.CALP is set, the readback calibration is close to the requested value (CALM rounding).
 */
void Ut_Rtc_Set_Calibration_Positive_RoundTrips( void )
{
    rtc_Calibration_ppb_t readBack;

    UtRtc_InitAdopt();

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_Calibration( UT_RTC_CALIB_POSITIVE_PPB ) );
    TEST_ASSERT_EQUAL( RTC_CALR_CALP, ( RTC->CALR & RTC_CALR_CALP ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_Calibration( &readBack ) );
    TEST_ASSERT_INT_WITHIN( UT_RTC_CALIB_TOLERANCE_PPB, UT_RTC_CALIB_POSITIVE_PPB, readBack );
}


/**
 * \brief A negative calibration value uses CALM only (no coarse addition).
 * \par Expected results
 * CALR.CALP stays clear, the readback calibration is negative.
 */
void Ut_Rtc_Set_Calibration_Negative_NoCoarseBit( void )
{
    rtc_Calibration_ppb_t readBack;

    UtRtc_InitAdopt();

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_Calibration( UT_RTC_CALIB_NEGATIVE_PPB ) );
    TEST_ASSERT_EQUAL( 0u, ( RTC->CALR & RTC_CALR_CALP ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_Calibration( &readBack ) );
    TEST_ASSERT_TRUE( 0 > readBack );
}


/**
 * \brief A positive time shift sets SHIFTR.ADD1S.
 * \par Expected results
 * \ref RTC_REQUEST_OK is returned, SHIFTR.ADD1S is set.
 */
void Ut_Rtc_Set_TimeShift_Positive_SetsAdd1s( void )
{
    UtRtc_InitAdopt();

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_TimeShift( UT_RTC_SHIFT_MS ) );
    TEST_ASSERT_EQUAL( RTC_SHIFTR_ADD1S, ( RTC->SHIFTR & RTC_SHIFTR_ADD1S ) );
}


/**
 * \brief Activating the reference clock input configures the RTC_REFIN pin and sets
 *        CR.REFCKON; deactivating clears it.
 * \par Expected results
 * \ref Gpio_Init is called once, CR.REFCKON follows active / inactive.
 */
void Ut_Rtc_Set_ReferenceClockActive_Inactive_TogglesRefckon( void )
{
    UtRtc_InitAdopt();

    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_ReferenceClockActive( RTC_REFIN_PIN_PB15 ) );
    TEST_ASSERT_EQUAL( RTC_CR_REFCKON, ( RTC->CR & RTC_CR_REFCKON ) );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_ReferenceClockInactive() );
    TEST_ASSERT_EQUAL( 0u, ( RTC->CR & RTC_CR_REFCKON ) );
}

/* ---------------------------------- Output ---------------------------------- */

/**
 * \brief Selecting the alarm output on RTC_OUT2 configures the pin and sets OUT2EN + OSEL.
 * \par Expected results
 * \ref Gpio_Init is called once, CR.OUT2EN is set, CR.OSEL selects alarm A.
 */
void Ut_Rtc_Set_Output_Alarm_ConfiguresOut2( void )
{
    rtc_OutputConfig_t outputConfig = { RTC_OUT2_PIN_PB2, RTC_OUTPUT_ALARM };

    UtRtc_InitAdopt();

    Gpio_Init_ExpectAnyArgsAndReturn( GPIO_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_Output( &outputConfig ) );
    TEST_ASSERT_EQUAL( RTC_CR_OUT2EN, ( RTC->CR & RTC_CR_OUT2EN ) );
    TEST_ASSERT_EQUAL( RTC_CR_OSEL_0, ( RTC->CR & RTC_CR_OSEL_Msk ) );
}

/* ----------------------------- Backup registers ----------------------------- */

/**
 * \brief The backup register count is forwarded to Tamp.
 * \par Expected results
 * The value Tamp returns is passed through unchanged.
 */
void Ut_Rtc_Get_BackupRegisterCount_ForwardsToTamp( void )
{
    rtc_BackupCnt_t registerCnt;

    Tamp_Get_BackupRegisterCount_ExpectAndReturn( RTC_NULL_PTR, TAMP_REQUEST_OK );
    Tamp_Get_BackupRegisterCount_IgnoreArg_registerCnt();
    Tamp_Get_BackupRegisterCount_ReturnThruPtr_registerCnt( &( tamp_BackupCnt_t ){ UT_RTC_BACKUP_CNT } );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_BackupRegisterCount( &registerCnt ) );
    TEST_ASSERT_EQUAL( UT_RTC_BACKUP_CNT, registerCnt );
}


/**
 * \brief Writing a backup register is forwarded to Tamp unchanged.
 * \par Expected results
 * \ref RTC_REQUEST_OK is returned when Tamp accepts the write.
 */
void Ut_Rtc_Set_BackupRegister_ForwardsToTamp( void )
{
    Tamp_Set_BackupRegister_ExpectAndReturn( UT_RTC_BACKUP_INDEX, UT_RTC_BACKUP_DATA, TAMP_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_BackupRegister( UT_RTC_BACKUP_INDEX, UT_RTC_BACKUP_DATA ) );
}


/**
 * \brief Reading a backup register is forwarded to Tamp unchanged.
 * \par Expected results
 * The value Tamp returns is passed through unchanged.
 */
void Ut_Rtc_Get_BackupRegister_ForwardsToTamp( void )
{
    rtc_BackupData_t data;

    Tamp_Get_BackupRegister_ExpectAndReturn( UT_RTC_BACKUP_INDEX, RTC_NULL_PTR, TAMP_REQUEST_OK );
    Tamp_Get_BackupRegister_IgnoreArg_data();
    Tamp_Get_BackupRegister_ReturnThruPtr_data( &( tamp_BackupData_t ){ UT_RTC_BACKUP_DATA_READ } );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_BackupRegister( UT_RTC_BACKUP_INDEX, &data ) );
    TEST_ASSERT_EQUAL( UT_RTC_BACKUP_DATA_READ, data );
}

/* ------------------------------- Interrupts ---------------------------------- */

/**
 * \brief The interrupt priority is forwarded to Nvic and cached.
 * \par Expected results
 * \ref Rtc_Get_IrqPriority returns the value last applied.
 */
void Ut_Rtc_Set_IrqPriority_Get_IrqPriority_CachesValue( void )
{
    rtc_IrqPrio_t irqPriority;

    UtRtc_InitAdopt();

    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_RTC, UT_RTC_IRQ_PRIO_NEW, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_IrqPriority( UT_RTC_IRQ_PRIO_NEW ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_IrqPriority( &irqPriority ) );
    TEST_ASSERT_EQUAL( UT_RTC_IRQ_PRIO_NEW, irqPriority );
}

/* ---------------------------------- Pins ------------------------------------- */

/**
 * \brief The timestamp pin is not implemented in this version (no GPIO_AF entry, see the
 *        Rtc.c file header).
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned.
 */
void Ut_Rtc_InitTimestampGpio_Call_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_InitTimestampGpio() );
}


/**
 * \brief The unused output pin item is rejected without calling Gpio.
 * \par Expected results
 * \ref RTC_REQUEST_ERROR is returned, no mock is called.
 */
void Ut_Rtc_InitOutputGpio_UnusedPin_ReturnsError( void )
{
    TEST_ASSERT_EQUAL( RTC_REQUEST_ERROR, Rtc_InitOutputGpio( RTC_OUT2_PIN_UNUSED ) );
}


/**
 * \brief A valid output pin item configures the GPIO as alternate function, AF0.
 * \par Expected results
 * \ref Gpio_Init is called once with the matching port / pin / alternate function.
 */
void Ut_Rtc_InitOutputGpio_ValidPin_CallsGpioInit( void )
{
    static gpio_Config_t expected;

    expected                = (gpio_Config_t){ 0 };
    expected.PortId         = GPIO_PORT_B;
    expected.PinId          = GPIO_PIN_ID_2;
    expected.PinMode        = GPIO_PIN_MODE_ALTERNATE;
    expected.PinPull        = GPIO_PIN_PULL_NONE;
    expected.PinSpeed       = GPIO_PIN_SPEED_LOW;
    expected.PinOutType     = GPIO_PIN_OUTPUT_PUSHPULL;
    expected.PinAltFunction = GPIO_ALT_FUNC_0;
    expected.PinActiveLevel = GPIO_PIN_LEVEL_HIGH;

    Gpio_Init_ExpectAndReturn( &expected, GPIO_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_InitOutputGpio( RTC_OUT2_PIN_PB2 ) );
}


/**
 * \brief A valid reference input pin item configures the GPIO as alternate function, AF0.
 * \par Expected results
 * \ref Gpio_Init is called once with the matching port / pin / alternate function.
 */
void Ut_Rtc_InitRefInGpio_ValidPin_CallsGpioInit( void )
{
    static gpio_Config_t expected;

    expected                = (gpio_Config_t){ 0 };
    expected.PortId         = GPIO_PORT_B;
    expected.PinId          = GPIO_PIN_ID_15;
    expected.PinMode        = GPIO_PIN_MODE_ALTERNATE;
    expected.PinPull        = GPIO_PIN_PULL_NONE;
    expected.PinSpeed       = GPIO_PIN_SPEED_LOW;
    expected.PinOutType     = GPIO_PIN_OUTPUT_PUSHPULL;
    expected.PinAltFunction = GPIO_ALT_FUNC_0;
    expected.PinActiveLevel = GPIO_PIN_LEVEL_HIGH;

    Gpio_Init_ExpectAndReturn( &expected, GPIO_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_InitRefInGpio( RTC_REFIN_PIN_PB15 ) );
}

/* =========================== LOCAL FUNCTIONS ============================== */

/**
 * \brief RegMem HW model: mirrors ICSR.INIT into ICSR.INITF (RSF is cleared in the initialization
 *        mode and set again after it) and every alarm / wake-up enable
 *        bit (CR.ALRAE / ALRBE / WUTE) into its ready flag (ICSR.ALRAWF / ALRBWF / WUTWF),
 *        the same "enable -> ready flag" pattern as the Wwdg module tests.
 */
static void UtRtc_HwModel( void )
{
    uint32_t cr   = RTC->CR;
    uint32_t icsr = RTC->ICSR;

    if( 0u != ( icsr & RTC_ICSR_INIT ) )
    {
        icsr |= RTC_ICSR_INITF;
        icsr &= ~RTC_ICSR_RSF;
    }
    else
    {
        icsr &= ~RTC_ICSR_INITF;
        icsr |= RTC_ICSR_RSF;
    }

    if( 0u == ( cr & RTC_CR_ALRAE ) )
    {
        icsr |= RTC_ICSR_ALRAWF;
    }
    else
    {
        icsr &= ~RTC_ICSR_ALRAWF;
    }

    if( 0u == ( cr & RTC_CR_ALRBE ) )
    {
        icsr |= RTC_ICSR_ALRBWF;
    }
    else
    {
        icsr &= ~RTC_ICSR_ALRBWF;
    }

    if( 0u == ( cr & RTC_CR_WUTE ) )
    {
        icsr |= RTC_ICSR_WUTWF;
    }
    else
    {
        icsr &= ~RTC_ICSR_WUTWF;
    }

    RTC->ICSR = icsr;
}


/**
 * \brief RegMem HW model of an RTC whose clock does not run: ICSR.INIT is mirrored into
 *        ICSR.INITF, ICSR.RSF is never set.
 */
static void UtRtc_HwModel_NoSync( void )
{
    uint32_t icsr = RTC->ICSR;

    if( 0u != ( icsr & RTC_ICSR_INIT ) )
    {
        icsr |= RTC_ICSR_INITF;
    }
    else
    {
        icsr &= ~RTC_ICSR_INITF;
    }

    icsr &= ~RTC_ICSR_RSF;

    RTC->ICSR = icsr;
}


/**
 * \brief Fills a configuration with the LSE / default-date values plus the test IRQ priority.
 */
static void UtRtc_FillDefaultConfig( rtc_Config_t * const config )
{
    (void)Rtc_Get_DefaultConfig( config );
    config->IrqPriority = UT_RTC_IRQ_PRIO;
}


/**
 * \brief Initializes the module through the simplest path (adopt a running calendar), used as
 *        a precondition by the tests that only exercise a function past \ref Rtc_Init.
 */
static void UtRtc_InitAdopt( void )
{
    rtc_Config_t config;

    UtRtc_FillDefaultConfig( &config );
    RTC->ICSR = RTC_ICSR_INITS;

    Pwr_Set_BackupAccessActive_ExpectAndReturn( PWR_REQUEST_OK );
    Rcc_Get_RtcClkSource_ExpectAndReturn( RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_RtcClkSource_IgnoreArg_clkSource();
    Rcc_Get_RtcClkSource_ReturnThruPtr_clkSource( &( rcc_Rtc_ClkSource_t ){ RCC_RTC_CLK_SOURCE_LSE } );
    Rcc_Get_PeriphState_ExpectAndReturn( RCC_PERIPH_RTC_LSE, RTC_NULL_PTR, RCC_REQUEST_OK );
    Rcc_Get_PeriphState_IgnoreArg_funcState();
    Rcc_Get_PeriphState_ReturnThruPtr_funcState( &( rcc_FunctionState_t ){ RCC_FUNCTION_ACTIVE } );
    Nvic_Set_PeriphIrq_Prio_ExpectAndReturn( NVIC_PERIPH_IRQ_RTC, UT_RTC_IRQ_PRIO, NVIC_REQUEST_OK );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Init( &config ) );
}

/* =========================== INTERRUPT HANDLERS =========================== */

/* ================================ TASKS =================================== */
