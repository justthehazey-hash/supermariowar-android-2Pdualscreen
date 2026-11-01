add_executable(smw-tests
	runner.cpp
	common/test_FileIO.cpp
	common/test_GameModeSettingsSerialization.cpp
	common/test_version.cpp
	common/test_tokenize.cpp
	common/math/test_vec2.cpp
	common/util/test_DirIterator.cpp
)
target_link_libraries(smw-tests PRIVATE doctest CommonFiles)
doctest_discover_tests(smw-tests)

add_executable(smw-input-tests
	runner.cpp
	common/test_input_pad.cpp
	common/input_test_globals.cpp
)
target_link_libraries(smw-input-tests PRIVATE doctest CommonFiles)
doctest_discover_tests(smw-input-tests)
