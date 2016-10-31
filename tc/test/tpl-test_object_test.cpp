/**
 * @file tpl-test_object_test.cpp
 * @brief TPL Test case of object
 *
 */

#include "gtest/gtest.h"

#include "src/tpl-test_base.h"


class TPLObject : public TPLTestBase {};


// Tests tpl_object_get_type(tpl_object_t *)
TEST_F(TPLObject, tpl_object_get_type)
{
	int obj_type = -1;
	obj_type = tpl_object_get_type((tpl_object_t *)backend->tpl_display);
	ASSERT_EQ(TPL_OBJECT_DISPLAY, obj_type);

	obj_type = -1;
	obj_type = tpl_object_get_type((tpl_object_t *)backend->tpl_surface);
	ASSERT_EQ(TPL_OBJECT_SURFACE, obj_type);
}


// Tests setting user data flow:
// 1. Set user data
// 2. Get user data
TEST_F(TPLObject, tpl_object_userdata_test)
{
	unsigned long key;

	// TODO: Check
	// set user data
	tpl_object_set_user_data((tpl_object_t *)backend->tpl_display, &key,
							 (void *)backend->display_handle, NULL);

	// get user data
	void *get_dpy = NULL;
	get_dpy = (void *)tpl_object_get_user_data(
			  (tpl_object_t *)backend->tpl_display, &key);

	ASSERT_EQ(get_dpy, (void *)backend->display_handle);
}

// Tests object reference flow:
// 1. Reference
// 2. Unreference
TEST_F(TPLObject, tpl_object_reference)
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

