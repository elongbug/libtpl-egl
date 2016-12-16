/**
 * @file tpl-test_object_test.cpp
 * @brief TPL Test case of object
 *
 */

#include "gtest/gtest.h"

#include "src/tpl-test_base.h"


typedef TPLTestBase DEFAULT_tpl_object_get_type;

TEST_F(DEFAULT_tpl_object_get_type, success_get_tpl_display)
{
	int obj_type = -1;
	obj_type = tpl_object_get_type((tpl_object_t *)backend->tpl_display);
	ASSERT_EQ(TPL_OBJECT_DISPLAY, obj_type);
}

TEST_F(DEFAULT_tpl_object_get_type, success_get_tpl_surfcae)
{
	int obj_type = -1;
	obj_type = tpl_object_get_type((tpl_object_t *)backend->tpl_surface);
	ASSERT_EQ(TPL_OBJECT_SURFACE, obj_type);
}

TEST_F(DEFAULT_tpl_object_get_type, failure_invalid_object)
{
	int obj_type = -1;
	obj_type = tpl_object_get_type(NULL);
	ASSERT_EQ(TPL_OBJECT_ERROR, obj_type);
}


typedef TPLTestBase DEFAULT_tpl_object_set_get_user_data;

// Tests setting user data flow:
// 1. Set user data
// 2. Get user data
TEST_F(DEFAULT_tpl_object_set_get_user_data, success_set_get)
{
	unsigned long key;

	// set user data
	tpl_object_set_user_data((tpl_object_t *)backend->tpl_display, &key,
							 (void *)backend->display_handle, NULL);

	// get user data
	void *get_dpy = NULL;
	get_dpy = (void *)tpl_object_get_user_data(
				  (tpl_object_t *)backend->tpl_display, &key);

	ASSERT_EQ((void *)backend->display_handle, get_dpy);
}

TEST_F(DEFAULT_tpl_object_set_get_user_data, failure_set_invalid_object)
{
	unsigned long key;
	int result;

	// set user data
	result = tpl_object_set_user_data(NULL, &key,
									  (void *)backend->display_handle, NULL);

	ASSERT_EQ(TPL_ERROR_INVALID_PARAMETER, result);
}

TEST_F(DEFAULT_tpl_object_set_get_user_data, failure_get_invalid_object)
{
	unsigned long key;

	// get user data
	void *get_dpy = NULL;
	get_dpy = (void *)tpl_object_get_user_data(NULL, &key);

	ASSERT_EQ(NULL, get_dpy);
}

TEST_F(DEFAULT_tpl_object_set_get_user_data, failure_get_invalid_key)
{
	unsigned long key;

	// set user data
	tpl_object_set_user_data((tpl_object_t *)backend->tpl_display, &key,
							 (void *)backend->display_handle, NULL);

	unsigned long invalid_key;

	// get user data
	void *get_dpy = NULL;
	get_dpy = (void *)tpl_object_get_user_data(
				  (tpl_object_t *)backend->tpl_display, &invalid_key);

	ASSERT_EQ(NULL, get_dpy);
}


typedef TPLTestBase DEFAULT_tpl_object_reference;

// Tests object reference flow:
// 1. Reference
// 2. Unreference
TEST_F(DEFAULT_tpl_object_reference, ref_unref)
{
	// tpl_object_reference
	tpl_object_reference((tpl_object_t *)backend->tpl_display);

	// tpl_object_get_reference
	int ref_count = -1;
	ref_count = tpl_object_get_reference((tpl_object_t *)backend->tpl_display);
	ASSERT_NE(-1, ref_count);

	// tpl_object_unreference
	int unref_count = -1;
	unref_count = tpl_object_unreference((tpl_object_t *)backend->tpl_display);
	ASSERT_EQ(ref_count - 1, unref_count);
}

TEST_F(DEFAULT_tpl_object_reference, failure_ref_invalid_object)
{
	int result;
	result = tpl_object_reference(NULL);

	ASSERT_EQ(-1, result);
}

TEST_F(DEFAULT_tpl_object_reference, failure_unref_invalid_object)
{
	int result;
	result = tpl_object_reference(NULL);

	ASSERT_EQ(-1, result);
}

TEST_F(DEFAULT_tpl_object_reference, failure_get_ref_invalid_object)
{
	int result;
	result = tpl_object_get_reference(NULL);

	ASSERT_EQ(-1, result);
}

