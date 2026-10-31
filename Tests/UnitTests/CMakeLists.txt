#======================== Unit tests configuration ============================#
# Unit tests of Rtc module. Registered in module CMakeLists.txt by
# UnitTesting_AddPath() and created by UnitTesting_Generate() in "UnitTest"
# build (see EmBi_Platform/CMake/UnitTesting/README.md).

get_filename_component(RTC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(MCAL_DIR "${RTC_DIR}/.." ABSOLUTE)

UnitTesting_Add_Test(
    NAME            Rtc
    TEST_SOURCE     Test_Rtc.c
    UUT_SOURCES     ${RTC_DIR}/Rtc.c
                    ${RTC_DIR}/Rtc_Reg.c
    MOCK_HEADERS    ${MCAL_DIR}/Rcc/Rcc_Port.h
                    ${MCAL_DIR}/Pwr/Pwr_Port.h
                    ${MCAL_DIR}/Tamp/Tamp_Port.h
                    ${MCAL_DIR}/Nvic/Nvic_Port.h
                    ${MCAL_DIR}/Gpio/Gpio_Port.h
    INCLUDE_DIRS    ${RTC_DIR}
                    ${MCAL_DIR}/Rcc
                    ${MCAL_DIR}/Pwr
                    ${MCAL_DIR}/Tamp
                    ${MCAL_DIR}/Nvic
                    ${MCAL_DIR}/Gpio
    LINK_LIBS       ut_RegMem                           # Registers in host memory
                    ut_CmsisHost                        # CMSIS core intrinsics on host
    FORCE_INCLUDES  ${UNIT_TESTING_CMSIS_HOST_HEADER}   # Replaces cmsis_gcc.h
    HOST_LIBS       Ral_Lib                             # ST LL compiled for host
    COVERAGE_EXCLUDE ".*/Ral/.*"
)
