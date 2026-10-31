#===================== Integration tests configuration ========================#
# Integration tests of Rtc module on target (Nucleo board). Registered in module
# CMakeLists.txt by IntegrationTesting_AddPath() and created by
# IntegrationTesting_Generate() in "IntegrationTest" build
# (see EmBi_Platform/CMake/IntegrationTesting/README.md).
#
# The test set is independent - target interface (entry point, initialization,
# fault handlers, system reset) is implemented by ItTarget_Rtc.c and
# all libraries of the firmware are listed here.

# StartUp calls BspMain() - the entry point of the test firmware is provided by
# the test set (BspMain.h, ItTarget_Rtc.c)
if(NOT TARGET BspMain_Lib)
    add_library(BspMain_Lib INTERFACE)
    target_include_directories(BspMain_Lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

IntegrationTesting_Add_Test(
    NAME            Rtc
    TEST_SOURCE     ItTest_Rtc.c
    SOURCES         ItTarget_Rtc.c      # Target interface of the test set
    LINK_LIBS       StartUp_Lib         # Startup code (calls BspMain)
                    Nvic_Lib            # Fault handlers, system reset (ItTarget)
                    Rtc_Lib
                    Tamp_Lib            # Backup registers forwarded by Rtc
                    Gpio_Lib            # Pin mode read-back
                    Rcc_Lib             # peripheral clock
    TIMEOUT         60
)
