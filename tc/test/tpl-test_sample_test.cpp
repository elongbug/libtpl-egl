/**
 * @file tpl-test_sample_test.cpp
 * @brief TPL Test Sample test cases
 *
 */

#include "gtest/gtest.h"

#include "src/tpl-test_base.h"

// Declare test case with the name as TestCaseName
// Remove 'DISABLED_' to run this test case
class DISABLED_TestCaseName : public TPLTestBase {};

// Declare test with declared TestCaseName and TestName
// Remove 'DISABLED_' to run this test
TEST_F(DISABLED_TestCaseName, TestName)
{
	int answer = 1;
	ASSERT_EQ(answer, 2 - 1);
}

