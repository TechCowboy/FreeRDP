
set(MODULE_NAME "TestFreeRDPCrypto")
set(MODULE_PREFIX "TEST_FREERDP_CRYPTO")

disable_warnings_for_directory(${CMAKE_CURRENT_BINARY_DIR})

set(${MODULE_PREFIX}_DRIVER ${MODULE_NAME}.c)

set(${MODULE_PREFIX}_TESTS
	TestKnownHosts.c
	TestBase64.c
	Test_x509_utils.c)

create_test_sourcelist(${MODULE_PREFIX}_SRCS
	${${MODULE_PREFIX}_DRIVER}
	${${MODULE_PREFIX}_TESTS})

include_directories(SYSTEM ${OPENSSL_INCLUDE_DIR})

add_executable(${MODULE_NAME} ${${MODULE_PREFIX}_SRCS})

set(TEST_PATH ${CMAKE_CURRENT_SOURCE_DIR})

add_definitions(-DTEST_SOURCE_DIR="${TEST_PATH}")
target_link_libraries(${MODULE_NAME} freerdp winpr ${OPENSSL_LIBRARIES})

set_target_properties(${MODULE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${TESTING_OUTPUT_DIRECTORY}")

foreach(test ${${MODULE_PREFIX}_TESTS})
	get_filename_component(TestName ${test} NAME_WE)
	add_test(${TestName} ${TESTING_OUTPUT_DIRECTORY}/${MODULE_NAME} ${TestName})
endforeach()

set_property(TARGET ${MODULE_NAME} PROPERTY FOLDER "FreeRDP/Test")

