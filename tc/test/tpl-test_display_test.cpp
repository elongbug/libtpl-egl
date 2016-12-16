/**
 * @file tpl-test_display_test.cpp
 * @brief TPL Test case of display
 *
 */

#include "gtest/gtest.h"

#include "src/tpl-test_base.h"


typedef TPLTestBase DEFAULT_tpl_display_get;

TEST_F(DEFAULT_tpl_display_get, success)
{
	tpl_display_t *tpl_display = tpl_display_get(backend->display_handle);

	ASSERT_EQ(backend->tpl_display, tpl_display);
}

TEST_F(DEFAULT_tpl_display_get, failure_null)
{
	tpl_display_t *tpl_display = tpl_display_get(NULL);

	ASSERT_EQ((void *)NULL, tpl_display);
}


typedef TPLTestBase DEFAULT_tpl_display_get_native_handle;

TEST_F(DEFAULT_tpl_display_get_native_handle, success)
{
	tpl_handle_t display_handle =
		tpl_display_get_native_handle(backend->tpl_display);

	ASSERT_EQ(backend->display_handle, display_handle);
}

TEST_F(DEFAULT_tpl_display_get_native_handle, failure_invalid_display)
{
	tpl_handle_t display_handle =
		tpl_display_get_native_handle(NULL);

	ASSERT_EQ((void *)NULL, display_handle);
}


typedef TPLTestBase DEFAULT_tpl_display_query_config;

TEST_F(DEFAULT_tpl_display_query_config, success_1)
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
}

TEST_F(DEFAULT_tpl_display_query_config, success_2)
{
	tpl_result_t result;

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
}

TEST_F(DEFAULT_tpl_display_query_config, failure_invalid_parameter_R)
{
	tpl_result_t result;

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


typedef TPLTestBase DEFAULT_tpl_display_filter_config;

TEST_F(DEFAULT_tpl_display_filter_config, success)
{
	tpl_result_t result;
	int test_visual_id = GBM_FORMAT_ARGB8888;
	result = tpl_display_filter_config(backend->tpl_display,
									   &test_visual_id, 0);

	ASSERT_EQ(TPL_ERROR_NONE, result);
}

TEST_F(DEFAULT_tpl_display_filter_config, failure_invalid_display)
{
	tpl_result_t result;
	int test_visual_id = GBM_FORMAT_ARGB8888;
	result = tpl_display_filter_config(NULL, &test_visual_id, 0);

	ASSERT_EQ(TPL_ERROR_INVALID_PARAMETER, result);
}


typedef TPLTestBase DEFAULT_tpl_display_get_native_window_info;

TEST_F(DEFAULT_tpl_display_get_native_window_info, success)
{
	tpl_result_t result;
	int width, height;
	tbm_format format;

	result = tpl_display_get_native_window_info(backend->tpl_display,
												backend->surface_handle,
												&width, &height, &format,
												config.depth, 8);

	EXPECT_EQ(config.width, width);
	EXPECT_EQ(config.height, height);
	EXPECT_EQ(TBM_FORMAT_ARGB8888, format);

	ASSERT_EQ(TPL_ERROR_NONE, result);
}

/* TODO: Make test - DEFAULT_tpl_display_get_native_window_info, failure */

typedef TPLTestBase
EXTRA_tpl_display_query_supported_buffer_count_from_native_window;

TEST_F(EXTRA_tpl_display_query_supported_buffer_count_from_native_window,
	   success)
{
	tpl_result_t result;
	int min, max;

	result = tpl_display_query_supported_buffer_count_from_native_window(
				 backend->tpl_display, backend->surface_handle, &min, &max);

	// SUCCEED() if backend does not support operation
	if (result == TPL_ERROR_INVALID_OPERATION) {
		SUCCEED() << "Backend does not support this operation";
		return;
	}

	ASSERT_EQ(TPL_ERROR_NONE, result);
}

