/**
 * \author Mr.Nobody
 * \file ItTest_Rtc.c
 * \ingroup Rtc
 * \brief Integration tests of the Rtc module on target.
 *
 * Rtc module runs on the MCU together with the real RTC / TAMP / GPIO / RCC hardware. Tests
 * verify behavior which cannot be verified by unit tests (emulated registers): a real
 * calendar initialization, a real calendar set/get round trip, a real backup register round
 * trip (forwarded to Tamp), and the RTC_OUT2 / RTC_REFIN GPIO configuration not faulting.
 *
 * Used resources:
 * - RTC / TAMP (backup domain)
 * - GPIO port B pin 2 (RTC_OUT2), pin 15 (RTC_REFIN) - not externally wired, only the GPIO
 *   configuration is checked
 *
 * \note The Rtc module takes the backup domain write access (Pwr) and the clock of the RTC (Rcc)
 *       itself, this test target sets nothing.
 * \note The RTC clock source (RCC_BDCR.RTCSEL) is written once per backup domain life and it is
 *       kept over resets (the Rcc integration test resets the backup domain after its RTC test).
 *       The tests that initialize the module therefore use the clock source the backup domain
 *       already has and the LSI when none is selected (\ref ItRtc_Get_Config).
 * \note No external reference clock or calibration measurement is exercised (it would need a
 *       probe): the tests exercise the register-level API, not a physical measurement.
 */

/* ============================= INCLUDES =================================== */
#include "unity.h"                          /* Unity testing framework        */
#include "IntegrationTesting.h"             /* Integration testing on target  */
#include "Rtc_Port.h"                        /* Module under test              */
#include "Gpio_Port.h"                       /* Pin read-back                  */
#include "Rcc_Port.h"                        /* RTC clock source in use        */
/* ============================= TYPEDEFS =================================== */

/* ======================= FORWARD DECLARATIONS ============================= */

static void ItRtc_Get_Config( rtc_Config_t * const rtcConfig );

/* ========================= SYMBOLIC CONSTANTS ============================= */

/** Backup register index used by the tests (any valid index works) */
#define IT_RTC_BACKUP_INDEX                 ( 3u )

/** Backup register data pattern used by the tests */
#define IT_RTC_BACKUP_DATA                  ( 0x5A5A5A5Au )

/** Hardware backup register count (TAMP_BKP0R .. TAMP_BKP31R) */
#define IT_RTC_BACKUP_REG_CNT               ( 32u )

/** Date and time used by the calendar round trip test (2026-10-21, a Wednesday) */
#define IT_RTC_YEAR                         ( 2026u )
#define IT_RTC_MONTH                        ( 10u )
#define IT_RTC_DAY                          ( 21u )
#define IT_RTC_HOUR                         ( 8u )
#define IT_RTC_MINUTE                       ( 0u )
#define IT_RTC_SECOND                       ( 0u )

/* ========================== LOCAL VARIABLES =============================== */

/* ============================= TEST SETUP ================================= */

void setUp( void )
{
    /* No per-test state */
}


void tearDown( void )
{
    /* Peripheral state is cleared by system reset after the test case */
}

/* =============================== TESTS ==================================== */

/**
 * \brief The module initializes with the default configuration.
 * \par Expected results
 * \ref Rtc_Init returns \ref RTC_REQUEST_OK with the default configuration (the clock source
 * already selected in the backup domain, see the file header note).
 */
void It_Rtc_Init_DefaultConfig_Succeeds( void )
{
    rtc_Config_t rtcConfig;

    ItRtc_Get_Config( &rtcConfig );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Init( &rtcConfig ) );
}


/**
 * \brief A calendar date/time is written and read back on the real peripheral.
 * \par Expected results
 * The readback date/time matches, the weekday is Wednesday (2026-10-21).
 */
void It_Rtc_SetDateTime_GetDateTime_RoundTrips( void )
{
    rtc_Config_t   rtcConfig;
    rtc_DateTime_t setDateTime = { IT_RTC_YEAR, IT_RTC_MONTH, IT_RTC_DAY, RTC_WEEKDAY_MONDAY,
                                    IT_RTC_HOUR, IT_RTC_MINUTE, IT_RTC_SECOND, 0u };
    rtc_DateTime_t getDateTime;

    ItRtc_Get_Config( &rtcConfig );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Init( &rtcConfig ) );

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_DateTime( &setDateTime ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_DateTime( &getDateTime ) );
    TEST_ASSERT_EQUAL( IT_RTC_YEAR, getDateTime.Year );
    TEST_ASSERT_EQUAL( IT_RTC_MONTH, getDateTime.Month );
    TEST_ASSERT_EQUAL( IT_RTC_DAY, getDateTime.Day );
    TEST_ASSERT_EQUAL( RTC_WEEKDAY_WEDNESDAY, getDateTime.WeekDay );
}


/**
 * \brief A backup register written through the Rtc API (forwarded to Tamp) reads back the
 *        same value.
 * \par Expected results
 * \ref Rtc_Set_BackupRegister / \ref Rtc_Get_BackupRegister round-trip the pattern.
 */
void It_Rtc_BackupRegister_WriteThenRead_RoundTrips( void )
{
    rtc_BackupData_t readBack = 0u;

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Set_BackupRegister( IT_RTC_BACKUP_INDEX, IT_RTC_BACKUP_DATA ) );
    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_BackupRegister( IT_RTC_BACKUP_INDEX, &readBack ) );
    TEST_ASSERT_EQUAL_UINT32( IT_RTC_BACKUP_DATA, readBack );
}


/**
 * \brief The backup register count is forwarded from the real Tamp peripheral.
 * \par Expected results
 * \ref Rtc_Get_BackupRegisterCount returns 32 on STM32H5.
 */
void It_Rtc_Get_BackupRegisterCount_Returns32( void )
{
    rtc_BackupCnt_t registerCnt = 0u;

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_BackupRegisterCount( &registerCnt ) );
    TEST_ASSERT_EQUAL( IT_RTC_BACKUP_REG_CNT, registerCnt );
}


/**
 * \brief The RTC_OUT2 pin is configured without a fault.
 * \par Expected results
 * \ref Rtc_InitOutputGpio returns \ref RTC_REQUEST_OK, the pin reads back as alternate
 * function AF0.
 */
void It_Rtc_InitOutputGpio_ValidPin_ConfiguresWithoutFault( void )
{
    gpio_PinMode_t     pinMode;
    gpio_AltFunction_t altFunc;

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_InitOutputGpio( RTC_OUT2_PIN_PB2 ) );
    TEST_ASSERT_EQUAL( GPIO_REQUEST_OK, Gpio_Get_PinMode( GPIO_PORT_B, GPIO_PIN_ID_2, &pinMode ) );
    TEST_ASSERT_EQUAL( GPIO_PIN_MODE_ALTERNATE, pinMode );
    TEST_ASSERT_EQUAL( GPIO_REQUEST_OK, Gpio_Get_PinAltFunction( GPIO_PORT_B, GPIO_PIN_ID_2, &altFunc ) );
    TEST_ASSERT_EQUAL( GPIO_ALT_FUNC_0, altFunc );
}


/**
 * \brief The RTC_REFIN pin is configured without a fault.
 * \par Expected results
 * \ref Rtc_InitRefInGpio returns \ref RTC_REQUEST_OK, the pin reads back as alternate
 * function AF0.
 */
void It_Rtc_InitRefInGpio_ValidPin_ConfiguresWithoutFault( void )
{
    gpio_PinMode_t     pinMode;
    gpio_AltFunction_t altFunc;

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_InitRefInGpio( RTC_REFIN_PIN_PB15 ) );
    TEST_ASSERT_EQUAL( GPIO_REQUEST_OK, Gpio_Get_PinMode( GPIO_PORT_B, GPIO_PIN_ID_15, &pinMode ) );
    TEST_ASSERT_EQUAL( GPIO_PIN_MODE_ALTERNATE, pinMode );
    TEST_ASSERT_EQUAL( GPIO_REQUEST_OK, Gpio_Get_PinAltFunction( GPIO_PORT_B, GPIO_PIN_ID_15, &altFunc ) );
    TEST_ASSERT_EQUAL( GPIO_ALT_FUNC_0, altFunc );
}

/* ========================== LOCAL FUNCTIONS ================================ */

/**
 * \brief Fills the default configuration with the RTC clock source the backup domain already has.
 *
 * The clock source (RCC_BDCR.RTCSEL) is written once per backup domain life and survives the
 * resets, so a source selected by an earlier test can not be changed. When no source is selected
 * (backup domain after a reset) the LSI is used: it needs no crystal and no start-up wait, the
 * LSE of the default configuration is not started by anybody before Rtc_Init.
 *
 * \param rtcConfig [out]: Configuration for \ref Rtc_Init
 */
static void ItRtc_Get_Config( rtc_Config_t * const rtcConfig )
{
    rcc_Rtc_ClkSource_t rccSource = RCC_RTC_CLK_SOURCE_LSE;

    TEST_ASSERT_EQUAL( RTC_REQUEST_OK, Rtc_Get_DefaultConfig( rtcConfig ) );

    const rcc_RequestState_t rccState = Rcc_Get_RtcClkSource( &rccSource );

    if( RCC_REQUEST_OK != rccState )
    {
        /* No source selected yet, the LSI runs without a crystal */
        rtcConfig->ClkSrc = RTC_CLK_SRC_LSI;
    }
    else if( RCC_RTC_CLK_SOURCE_LSI == rccSource )
    {
        rtcConfig->ClkSrc = RTC_CLK_SRC_LSI;
    }
    else if( RCC_RTC_CLK_SOURCE_HSE_DIV == rccSource )
    {
        rtcConfig->ClkSrc = RTC_CLK_SRC_HSE_DIV;
    }
    else
    {
        rtcConfig->ClkSrc = RTC_CLK_SRC_LSE;
    }
}
