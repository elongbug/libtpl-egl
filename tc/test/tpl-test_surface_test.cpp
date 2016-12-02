/**
 * @file tpl-test_surface_test.cpp
 * @brief TPL Test case of surface
 *
 */

#include "gtest/gtest.h"

#include "src/tpl-test_base.h"


class TPLSurface : public TPLTestBase {};
class TPLSurfaceSupport : public TPLSurface {};


// Tests tpl_surface_validate(tpl_surface_t *)
TEST_F(TPLSurface, tpl_surface_validate)
{
	tpl_bool_t result = tpl_surface_validate(backend->tpl_surface);

	// Expected Value: TPL_TRUE
	ASSERT_EQ(TPL_TRUE, result);
}


TEST_F(TPLSurface, tpl_surface_get_args)
{
	// tpl_surface_get_display test
	tpl_display_t *test_dpy = tpl_surface_get_display(backend->tpl_surface);
	ASSERT_EQ(test_dpy, backend->tpl_display);

	// tpl_surface_get_native_handle
	tpl_handle_t test_handle =
		tpl_surface_get_native_handle(backend->tpl_surface);
	ASSERT_EQ(test_handle, backend->surface_handle);

	// tpl_surface_get_type test
	tpl_surface_type_t test_type = tpl_surface_get_type(backend->tpl_surface);
	ASSERT_EQ(TPL_SURFACE_TYPE_WINDOW, test_type);

	// tpl_surface_get_size test
	int width, height;
	tpl_result_t test_size =
		tpl_surface_get_size(backend->tpl_surface, &width, &height);

	EXPECT_EQ(config.width, width);
	EXPECT_EQ(config.height, height);
	ASSERT_EQ(TPL_ERROR_NONE, test_size);
}


// Tests simple normal buffer flow:
// 1. Dequeue buffer by calling tpl_surface_dequeue_buffer
// 2. Set post interval
// 3. Enqueue buffer by calling tpl_surface_enqueue_buffer
TEST_F(TPLSurface, tpl_surface_dequeue_and_enqueue_buffer_test)
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


TEST_F(TPLSurfaceSupport, tpl_surface_create_get_destroy_swapchain_test)
{
	int buffer_set = 2;
	tpl_result_t result;

	// Create swapchain
	result = tpl_surface_create_swapchain(backend->tpl_surface,
			 TBM_FORMAT_ARGB8888, config.width, config.height, buffer_set,
			 TPL_DISPLAY_PRESENT_MODE_IMMEDIATE);

	// SUCCEED() if backend does not support operation
	if (result == TPL_ERROR_INVALID_OPERATION) {
		SUCCEED() << "Backend does not support this operation";
		return;
	}

	ASSERT_EQ(TPL_ERROR_NONE, result);

	tbm_surface_h **buffers;
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

