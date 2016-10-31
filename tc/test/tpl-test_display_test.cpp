/**
 * @file tpl-test_display_test.cpp
 * @brief TPL Test case of display
 *
 */

#include "gtest/gtest.h"

#include "src/tpl-test_base.h"


class TPLDisplay : public TPLTestBase {};
class TPLDisplaySupport : public TPLDisplay {};


// Tests tpl_display_get(tpl_handle_t)
TEST_F(TPLDisplay, tpl_display_get)
{
	tpl_display_t *tpl_display = tpl_display_get(backend->display_handle);

	ASSERT_EQ(tpl_display, backend->tpl_display);
}


// Tests tpl_display_get_native_handle(tpl_handle_t)
TEST_F(TPLDisplay, tpl_display_get_native_handle)
{
	tpl_handle_t display_handle =
		tpl_display_get_native_handle(backend->tpl_display);

	ASSERT_EQ(display_handle, backend->display_handle);
}


// Tests tpl_display_query_config(...)
//
// 1. Tests with normal cases
// 2. Tests with abnormal case
TEST_F(TPLDisplay, tpl_display_query_config)
{
	tpl_result_t result;

	// #1: Normal case
	// Expected Value: TPL_ERROR_NONE
	result = tpl_display_query_config(backend->tpl_display,
									  TPL_SURFACE_TYPE_WINDOW,
									  8,		// red size
									  8,		// green size
									  8,		// blue size
									  8,		// alpha size
									  32,		// depth size
									  NULL,
									  NULL);

	ASSERT_EQ(TPL_ERROR_NONE, result);

	// #2: Normal case
	// Expected Value: TPL_ERROR_NONE
	result = tpl_display_query_config(backend->tpl_display,
									  TPL_SURFACE_TYPE_WINDOW,
									  8,
									  8,
									  8,
									  8,
									  24,
									  NULL,
									  NULL);

	ASSERT_EQ(TPL_ERROR_NONE, result);

	// #3: Abnormal case
	// Expected Value: not TPL_ERROR_NONE
	result = tpl_display_query_config(backend->tpl_display,
									  TPL_SURFACE_TYPE_WINDOW,
									  0,		// red size can't be zero
									  8,
									  8,
									  8,
									  24,
									  NULL,
									  NULL);

	ASSERT_NE(TPL_ERROR_NONE, result);
}


// Tests tpl_display_filter_config(...)
TEST_F(TPLDisplay, tpl_display_filter_config)
{
	tpl_result_t result;
	int test_visual_id = GBM_FORMAT_ARGB8888;
	result = tpl_display_filter_config(backend->tpl_display,
			 &test_visual_id, 0);

	// Expected Value: TPL_ERROR_NONE
	ASSERT_EQ(TPL_ERROR_NONE, result);
}


// Tests tpl_display_get_native_window_info(...)
TEST_F(TPLDisplay, tpl_display_get_native_window_info)
{
	tpl_result_t result;
	int width, height;
	tbm_format format;

	result = tpl_display_get_native_window_info(backend->tpl_display,
			 backend->surface_handle, &width, &height, &format,
			 config.depth, 8);

	EXPECT_EQ(config.width, width);
	EXPECT_EQ(config.height, height);
	EXPECT_EQ(TBM_FORMAT_ARGB8888, format);

	ASSERT_EQ(TPL_ERROR_NONE, result);
}


TEST_F(TPLDisplaySupport,
	   tpl_display_query_supported_buffer_count_from_native_window)
{
	tpl_result_t result;
	int min, max;

	result = tpl_display_query_supported_buffer_count_from_native_window(
			 backend->tpl_display, backend->surface_handle,
			 &min, &max);

	// SUCCEED() if backend does not support operation
	if (result == TPL_ERROR_INVALID_OPERATION) {
		SUCCEED() << "Backend does not support this operation";
		return;
	}

	ASSERT_EQ(TPL_ERROR_NONE, result);
}

