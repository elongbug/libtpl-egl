/**
 * @file tpl-test_surface_test.cpp
 * @brief TPL Test case of surface
 *
 */

#include "gtest/gtest.h"

#include "src/tpl-test_base.h"


typedef TPLTestBase DEFAULT_tpl_surface_validate;

TEST_F(DEFAULT_tpl_surface_validate, success)
{
	tpl_bool_t result = tpl_surface_validate(backend->tpl_surface);

	ASSERT_EQ(TPL_TRUE, result);
}

TEST_F(DEFAULT_tpl_surface_validate, failure_invalid_surface)
{
	tpl_bool_t result = tpl_surface_validate(NULL);

	ASSERT_EQ(TPL_FALSE, result);
}


typedef TPLTestBase DEFAULT_tpl_surface_get_display;

TEST_F(DEFAULT_tpl_surface_get_display, success)
{
	// tpl_surface_get_display test
	tpl_display_t *test_dpy = tpl_surface_get_display(backend->tpl_surface);

	ASSERT_EQ(backend->tpl_display, test_dpy);
}

TEST_F(DEFAULT_tpl_surface_get_display, failure_invalid_surface)
{
	// tpl_surface_get_display test
	tpl_display_t *test_dpy = tpl_surface_get_display(NULL);

	ASSERT_EQ((void *)NULL, test_dpy);
}


typedef TPLTestBase DEFAULT_tpl_surface_get_native_handle;

TEST_F(DEFAULT_tpl_surface_get_native_handle, success)
{
	// tpl_surface_get_native_handle
	tpl_handle_t test_handle =
		tpl_surface_get_native_handle(backend->tpl_surface);

	ASSERT_EQ(backend->surface_handle, test_handle);
}

TEST_F(DEFAULT_tpl_surface_get_native_handle, failure_invalid_surface)
{
	// tpl_surface_get_native_handle
	tpl_handle_t test_handle = tpl_surface_get_native_handle(NULL);

	ASSERT_EQ((void *)NULL, test_handle);
}


typedef TPLTestBase DEFAULT_tpl_surface_get_type;

TEST_F(DEFAULT_tpl_surface_get_type, success)
{
	// tpl_surface_get_type test
	tpl_surface_type_t test_type = tpl_surface_get_type(backend->tpl_surface);
	ASSERT_EQ(TPL_SURFACE_TYPE_WINDOW, test_type);
}

TEST_F(DEFAULT_tpl_surface_get_type, failure_invalid_surface)
{
	// tpl_surface_get_type test
	tpl_surface_type_t test_type = tpl_surface_get_type(NULL);
	ASSERT_EQ(TPL_SURFACE_ERROR, test_type);
}


typedef TPLTestBase DEFAULT_tpl_surface_get_size;

TEST_F(DEFAULT_tpl_surface_get_size, success)
{
	// tpl_surface_get_size test
	int width, height;
	tpl_result_t test_size =
		tpl_surface_get_size(backend->tpl_surface, &width, &height);

	ASSERT_EQ(TPL_ERROR_NONE, test_size);

	EXPECT_EQ(config.width, width);
	EXPECT_EQ(config.height, height);
}

TEST_F(DEFAULT_tpl_surface_get_size, failure_invalid_surface)
{
	int width, height;
	tpl_result_t test_size =
		tpl_surface_get_size(NULL, &width, &height);

	ASSERT_EQ(TPL_ERROR_INVALID_PARAMETER, test_size);
}


typedef TPLTestBase DEFAULT_tpl_surface_dequeue_enqueue;

// Tests simple normal buffer flow:
// 1. Dequeue buffer by calling tpl_surface_dequeue_buffer
// 2. Set post interval
// 3. Enqueue buffer by calling tpl_surface_enqueue_buffer
TEST_F(DEFAULT_tpl_surface_dequeue_enqueue, deq_enq)
{
	// dequeue
	tbm_surface_h tbm_surf = NULL;
	tbm_surf = tpl_surface_dequeue_buffer(backend->tpl_surface);
	ASSERT_NE((void *)NULL, tbm_surf);

	// set and get interval
	int interval_set = 2;
	tpl_surface_set_post_interval(backend->tpl_surface, interval_set);
	int interval_get = tpl_surface_get_post_interval(backend->tpl_surface);
	ASSERT_EQ(interval_set, interval_get);

	// enqueue
	tpl_result_t result = TPL_ERROR_INVALID_PARAMETER;
	result = tpl_surface_enqueue_buffer(backend->tpl_surface, tbm_surf);
	ASSERT_EQ(TPL_ERROR_NONE, result);
}


typedef TPLTestBase EXTRA_tpl_surface_swapchain;

TEST_F(EXTRA_tpl_surface_swapchain, success)
{
	int buffer_set = 2;
	tpl_result_t result;

	// Create swapchain
	result = tpl_surface_create_swapchain(backend->tpl_surface,
										  TBM_FORMAT_ARGB8888,
										  config.width, config.height,
										  buffer_set,
										  TPL_DISPLAY_PRESENT_MODE_IMMEDIATE);

	// SUCCEED() if backend does not support operation
	if (result == TPL_ERROR_INVALID_OPERATION) {
		SUCCEED() << "Backend does not support this operation";
		return;
	}

	ASSERT_EQ(TPL_ERROR_NONE, result);

	tbm_surface_h **buffers = NULL;
	int buffer_get;

	// Get swapchain buffers
	result = tpl_surface_get_swapchain_buffers(backend->tpl_surface,
											   buffers, &buffer_get);

	EXPECT_EQ(buffer_set, buffer_get);
	ASSERT_EQ(TPL_ERROR_NONE, result);

	// Destroy swapchain
	result = tpl_surface_destroy_swapchain(backend->tpl_surface);
	ASSERT_EQ(TPL_ERROR_NONE, result);
}

