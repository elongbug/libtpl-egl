#ifndef TPL_INTERNAL_H
#define TPL_INTERNAL_H

#include "tpl.h"
#include <stdlib.h>
#include <pthread.h>

#if defined(TPL_WINSYS_DRI2) || defined(TPL_WINSYS_DRI3)
#include <xcb/xcb.h>
#endif

#include "tpl_utils.h"

#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>

#define TPL_OBJECT_BUCKET_BITS		5
#define TPL_OBJECT_LOCK(object)		__tpl_object_lock((tpl_object_t *)(object))
#define TPL_OBJECT_UNLOCK(object)	__tpl_object_unlock((tpl_object_t *)(object))
#define TPL_OBJECT_MAGIC			0xe0b9ec75
#define TPL_OBJECT_MAGIC_FREED		0xe0bf6eed
#define TPL_OBJECT(x)				((tpl_object_t *)(x))

#ifdef OBJECT_HASH_CHECK
# define TPL_OBJECT_HASH_FIND(x)	__tpl_object_hash_find(TPL_OBJECT(x))
# define TPL_OBJECT_CHECK(x)												\
	do {																	\
		if (!TPL_OBJECT(x))return;											\
		else if ((tpl_obj_hash_check) && (!TPL_OBJECT_HASH_FIND(x))) return;\
		else if ((TPL_OBJECT(x)->magic != (int)TPL_OBJECT_MAGIC)) return;	\
	} while (0)
# define TPL_OBJECT_CHECK_RETURN(x, ret)										\
	do {																		\
		if (!TPL_OBJECT(x))return ret;											\
		else if ((tpl_obj_hash_check) && (!TPL_OBJECT_HASH_FIND(x))) return ret;\
		else if ((TPL_OBJECT(x)->magic != (int)TPL_OBJECT_MAGIC)) return ret;	\
	} while (0)
#else
# define TPL_OBJECT_CHECK(x)						do {if ((!TPL_OBJECT(x)) || (TPL_OBJECT(x)->magic != (int)TPL_OBJECT_MAGIC)) return;} while (0)
# define TPL_OBJECT_CHECK_RETURN(x, ret)			do {if ((!TPL_OBJECT(x)) || (TPL_OBJECT(x)->magic != (int)TPL_OBJECT_MAGIC)) return ret;} while (0)
#endif

#define TPL_OBJECT_TYPE_CHECK(x, tp)				do {if ((TPL_OBJECT(x)->type) != (tpl_object_type_t)(tp)) { TPL_ERR("Object type check failed"); return;} } while (0)
#define TPL_OBJECT_TYPE_CHECK_RETURN(x, tp, ret)	do {if ((TPL_OBJECT(x)->type) != (tpl_object_type_t)(tp)) { TPL_ERR("Object type check failed"); return ret;} } while (0)

typedef struct _tpl_runtime	tpl_runtime_t;
typedef struct _tpl_display_backend	tpl_display_backend_t;
typedef struct _tpl_surface_backend	tpl_surface_backend_t;
typedef struct tpl_hlist	tpl_hlist_t;

struct _tpl_display_backend {
	tpl_backend_type_t type;
	void *data;

	tpl_result_t (*init)(tpl_display_t *display);
	void (*fini)(tpl_display_t *display);
	tpl_result_t (*query_config)(tpl_display_t *display,
								 tpl_surface_type_t surface_type,
								 int red_bits, int green_bits,
								 int blue_bits, int alpha_bits,
								 int color_depth, int *native_visual_id,
								 tpl_bool_t *is_slow);
	tpl_result_t (*filter_config)(tpl_display_t *display, int *visual_id,
								  int alpha_bits);
	tpl_result_t (*get_window_info)(tpl_display_t *display,
									tpl_handle_t window, int *width,
									int *height, tbm_format *format,
									int depth, int a_size);
	tpl_result_t (*get_pixmap_info)(tpl_display_t *display,
									tpl_handle_t pixmap, int *width,
									int *height, tbm_format *format);
	tbm_surface_h (*get_buffer_from_native_pixmap)(tpl_handle_t pixmap);
	tpl_result_t (*query_window_supported_buffer_count)(tpl_display_t *display,
			tpl_handle_t window,
			int *min, int *max);
	tpl_result_t (*query_window_supported_present_modes)(tpl_display_t *display,
														tpl_handle_t window,
														int *modes);
};

struct _tpl_surface_backend {
	tpl_backend_type_t type;
	void *data;

	tpl_result_t (*init)(tpl_surface_t *surface);
	void (*fini)(tpl_surface_t *surface);
	tpl_bool_t (*validate)(tpl_surface_t *surface);
	tbm_surface_h (*dequeue_buffer)(tpl_surface_t *surface, uint64_t timeout_ns,
									tbm_fd *sync_fence);
	tpl_result_t (*enqueue_buffer)(tpl_surface_t *surface,
								   tbm_surface_h tbm_surface,
								   int num_rects, const int *rects,
								   tbm_fd sync_fence);
	tpl_result_t (*get_swapchain_buffers)(tpl_surface_t *surface,
										  tbm_surface_h **buffers,
										  int *buffer_count);
	tpl_result_t (*create_swapchain)(tpl_surface_t *surface,
									 tbm_format format, int width,
									 int height, int buffer_count, int present_mode);
	tpl_result_t (*destroy_swapchain)(tpl_surface_t *surface);
};

struct _tpl_object {
	int magic;
	tpl_object_type_t type;
	tpl_util_atomic_uint reference;
	tpl_free_func_t free;
	pthread_mutex_t mutex;
	tpl_util_map_t user_data_map;
	tpl_util_map_entry_t *buckets[1 << TPL_OBJECT_BUCKET_BITS];
};

struct _tpl_display {
	tpl_object_t base;
	tpl_handle_t native_handle;
	int bufmgr_fd;
	tpl_display_backend_t backend;
#if defined(TPL_WINSYS_DRI2) || defined(TPL_WINSYS_DRI3)
	xcb_connection_t *xcb_connection;
#endif
};

struct _tpl_surface {
	tpl_object_t base;
	tpl_display_t *display;
	tpl_handle_t native_handle;
	tpl_surface_type_t type;
	tbm_format format;
	int width, height;
	int rotation;
	int post_interval;
	int dump_count;
	tpl_bool_t rotation_capability;
	tpl_surface_backend_t backend;

	/*For frontbuffer extension*/
	tpl_bool_t is_frontbuffer_mode;
	tbm_surface_h frontbuffer;

	/* Surface reset callback */
	tpl_surface_cb_func_t reset_cb;
	void* reset_data;
};

/*******************************************************************************
* TPL object functions
*******************************************************************************/

/** brief check wether a TPL object is valid
 * @param object the TPL object to check
 * @return TPL_ERROR_NONE on success, TPL_ERROR on error
 */
tpl_bool_t __tpl_object_is_valid(tpl_object_t *object);

/** brief initialize a TPL object
 * @param object the TPL object to initialize
 * @param type type of the TPL object
 * @param free_func customized deallocation routine for this TPL object
 * @return TPL_ERROR_NONE on success, TPL_ERROR on error
 */
tpl_result_t
__tpl_object_init(tpl_object_t *object, tpl_object_type_t type,
				  tpl_free_func_t free_func);

/** brief destroy a TPL object
 * @param object the TPL object to destroy
 * @return TPL_ERROR_NONE on success, TPL_ERROR on error
 * @warning this function is automatically called when the reference count reaches 0, therefore it should not be expliclity called
 */
tpl_result_t __tpl_object_fini(tpl_object_t *object);

/** brief lock a TPL object
 * @param object the TPL object to lock
 * @return TPL_ERROR_NONE on success, TPL_ERROR on error
 */
tpl_result_t __tpl_object_lock(tpl_object_t *object);

/** brief unlock a TPL object
 * @param object the TPL object to unlock
 */
void __tpl_object_unlock(tpl_object_t *object);

/* Display functions. */
tpl_handle_t __tpl_display_get_native_handle(tpl_display_t *display);
void __tpl_surface_set_backend_data(tpl_surface_t *surface, void *data);
void *__tpl_surface_get_backend_data(tpl_surface_t *surface);

/* Runtime functions. */
tpl_display_t *
__tpl_runtime_find_display(tpl_backend_type_t type,
						   tpl_handle_t native_display);
tpl_result_t __tpl_runtime_add_display(tpl_display_t *display);
void __tpl_runtime_remove_display(tpl_display_t *display);

/* Backend initialization functions. */
tpl_backend_type_t __tpl_display_choose_backend(tpl_handle_t native_dpy);
tpl_bool_t __tpl_display_choose_backend_gbm(tpl_handle_t native_dpy);
tpl_bool_t __tpl_display_choose_backend_tbm(tpl_handle_t native_dpy);
tpl_bool_t __tpl_display_choose_backend_wayland_egl(tpl_handle_t native_dpy);
tpl_bool_t __tpl_display_choose_backend_wayland_vk_wsi(tpl_handle_t native_dpy);
tpl_bool_t __tpl_display_choose_backend_x11_dri2(tpl_handle_t native_dpy);
tpl_bool_t __tpl_display_choose_backend_x11_dri3(tpl_handle_t native_dpy);
void __tpl_display_init_backend(tpl_display_t *display,
								tpl_backend_type_t type);
void __tpl_surface_init_backend(tpl_surface_t *surface,
								tpl_backend_type_t type);
void __tpl_display_init_backend_gbm(tpl_display_backend_t *backend);
void __tpl_display_init_backend_tbm(tpl_display_backend_t *backend,
									tpl_backend_type_t type);
void __tpl_display_init_backend_wayland_egl(tpl_display_backend_t *backend);
void __tpl_display_init_backend_wayland_vk_wsi(tpl_display_backend_t *backend);
void __tpl_display_init_backend_x11_dri2(tpl_display_backend_t *backend);
void __tpl_display_init_backend_x11_dri3(tpl_display_backend_t *backend);
void __tpl_surface_init_backend_gbm(tpl_surface_backend_t *backend);
void __tpl_surface_init_backend_tbm(tpl_surface_backend_t *backend,
									tpl_backend_type_t type);
void __tpl_surface_init_backend_wayland_egl(tpl_surface_backend_t *backend);
void __tpl_surface_init_backend_wayland_vk_wsi(tpl_surface_backend_t *backend);
void __tpl_surface_init_backend_x11_dri2(tpl_surface_backend_t *backend);
void __tpl_surface_init_backend_x11_dri3(tpl_surface_backend_t *backend);

/* OS related functions */
void __tpl_util_sys_yield(void);
int __tpl_util_clz(int input);

/** brief get the atomic variable's value
 * @param atom storage for the value to retrieve of
 * @return value stored on success, 0 or error
*/
int __tpl_util_atomic_get(const tpl_util_atomic_uint *const atom);

/** brief set the atomic variable's value
 * @param atom storage for the value to set of
 * @param val the value to set
 * @return TPL_TRUE on succes, TPL_FALSE on error
*/
void __tpl_util_atomic_set(tpl_util_atomic_uint *const atom, unsigned int val);
unsigned int __tpl_util_atomic_inc(tpl_util_atomic_uint *const atom );
unsigned int __tpl_util_atomic_dec(tpl_util_atomic_uint *const atom );

/* Data structure functions */
tpl_hlist_t *__tpl_hashlist_create();
void __tpl_hashlist_destroy(tpl_hlist_t **list);
tpl_result_t __tpl_hashlist_insert(tpl_hlist_t *list, size_t key, void *data);
void __tpl_hashlist_delete(tpl_hlist_t *list, size_t key);
void __tpl_hashlist_do_for_all_nodes(tpl_hlist_t *list,
									 void (*cb_func)(void *));
void *__tpl_hashlist_lookup(tpl_hlist_t *list, size_t key);

#ifdef OBJECT_HASH_CHECK
/* object hash check global variable */
extern tpl_bool_t tpl_obj_hash_check;
extern tpl_hlist_t *tpl_obj_hash;
/* object hash check functions */
void __tpl_object_hash_init(void);
void __tpl_object_hash_shutdown(void);
static TPL_INLINE tpl_object_t *
__tpl_object_hash_find(tpl_object_t *object)
{
	return (tpl_object_t *)__tpl_hashlist_lookup(tpl_obj_hash, (size_t)object);
}
#endif

#endif /* TPL_INTERNAL_H */
