#include "tpl_internal.h"

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <tbm_surface_queue.h>

#define USE_WORKER_THREAD
#ifndef USE_WORKER_THREAD
#define USE_WORKER_THREAD 0
#else
#include "tpl_worker_thread.h"
#include <pthread.h>
#include <time.h>
#undef USE_WORKER_THREAD
#define USE_WORKER_THREAD 1
#endif

typedef struct _tpl_tbm_display tpl_tbm_display_t;
typedef struct _tpl_tbm_surface tpl_tbm_surface_t;
#if USE_WORKER_THREAD == 1
typedef struct _tpl_tbm_buffer tpl_tbm_buffer_t;
#endif

struct _tpl_tbm_display {
	int need_dpy_deinit;
	int dummy;
};

struct _tpl_tbm_surface {
#if USE_WORKER_THREAD == 1
	/* tbm_surface list */
	tpl_list_t vblank_list;
	pthread_mutex_t vblank_list_mutex;

	tpl_list_t draw_waiting_queue;
	pthread_mutex_t draw_waiting_mutex;

	tpl_bool_t vblank_done;

	tpl_worker_surface_t worker_surface;

	tpl_bool_t need_worker_clear;
#endif
	int present_mode;
};

#if USE_WORKER_THREAD == 1
struct _tpl_tbm_buffer {
	tbm_fd wait_sync;
};

static int tpl_tbm_buffer_key;
#define KEY_tpl_tbm_buffer  (unsigned long)(&tpl_tbm_buffer_key)

static void
__tpl_tbm_buffer_free(tpl_tbm_buffer_t *tbm_buffer)
{
	TPL_ASSERT(tbm_buffer);
	if (tbm_buffer->wait_sync != -1)
		close(tbm_buffer->wait_sync);
	free(tbm_buffer);
}

static void
__tpl_tbm_buffer_remove_from_list(void *data)
{
	tbm_surface_h tbm_surface = data;
	tbm_surface_internal_unref(tbm_surface);
}

static TPL_INLINE tpl_tbm_buffer_t *
__tpl_tbm_get_tbm_buffer_from_tbm_surface(tbm_surface_h surface)
{
	tpl_tbm_buffer_t *buf = NULL;

	if (!tbm_surface_internal_is_valid(surface))
		return NULL;

	tbm_surface_internal_get_user_data(surface, KEY_tpl_tbm_buffer,
									   (void **)&buf);
	return buf;
}

static TPL_INLINE void
__tpl_tbm_set_tbm_buffer_to_tbm_surface(tbm_surface_h surface,
										tpl_tbm_buffer_t *buf)
{
	tbm_surface_internal_add_user_data(surface,
									   KEY_tpl_tbm_buffer,
									   (tbm_data_free)__tpl_tbm_buffer_free);
	tbm_surface_internal_set_user_data(surface,
									   KEY_tpl_tbm_buffer, buf);
}
#endif

static tpl_result_t
__tpl_tbm_display_init(tpl_display_t *display)
{
	tpl_tbm_display_t *tbm_display = NULL;

	TPL_ASSERT(display);

	tbm_display = (tpl_tbm_display_t *) calloc(1, sizeof(tpl_tbm_display_t));

	if (!tbm_display) {
		TPL_ERR("Failed to allocate memory for new tpl_tbm_display_t.");
		return TPL_ERROR_OUT_OF_MEMORY;
	}

	if (!display->native_handle) {
		display->native_handle = tbm_bufmgr_init(-1);
		tbm_display->need_dpy_deinit = 1;
	}

	display->backend.data = tbm_display;
	display->bufmgr_fd = -1;

	return TPL_ERROR_NONE;
}

static void
__tpl_tbm_display_fini(tpl_display_t *display)
{
	tpl_tbm_display_t *tbm_display;

	TPL_ASSERT(display);

	tbm_display = (tpl_tbm_display_t *)display->backend.data;

	display->backend.data = NULL;

	if (tbm_display) {
		if (tbm_display->need_dpy_deinit)
			tbm_bufmgr_deinit((tbm_bufmgr)display->native_handle);

		free(tbm_display);
	}
}

static tpl_result_t
__tpl_tbm_display_query_config(tpl_display_t *display,
							   tpl_surface_type_t surface_type, int red_size,
							   int green_size, int blue_size, int alpha_size,
							   int color_depth, int *native_visual_id,
							   tpl_bool_t *is_slow)
{
	TPL_ASSERT(display);

	if (surface_type == TPL_SURFACE_TYPE_WINDOW && red_size == 8
			&& green_size == 8 && blue_size == 8
			&& (color_depth == 32 || color_depth == 24)) {
		if (alpha_size == 8) {
			if (native_visual_id)
				*native_visual_id = TBM_FORMAT_ARGB8888;

			if (is_slow) *is_slow = TPL_FALSE;

			return TPL_ERROR_NONE;
		}
		if (alpha_size == 0) {
			if (native_visual_id)
				*native_visual_id = TBM_FORMAT_XRGB8888;

			if (is_slow) *is_slow = TPL_FALSE;

			return TPL_ERROR_NONE;
		}
	}

	return TPL_ERROR_INVALID_PARAMETER;
}

static tpl_result_t
__tpl_tbm_display_filter_config(tpl_display_t *display, int *visual_id,
								int alpha_size)
{
	TPL_IGNORE(display);

	if (visual_id && *visual_id == TBM_FORMAT_ARGB8888 && alpha_size == 0) {
		*visual_id = TBM_FORMAT_XRGB8888;
		return TPL_ERROR_NONE;
	}

	return TPL_ERROR_INVALID_PARAMETER;
}

static tpl_result_t
__tpl_tbm_display_get_window_info(tpl_display_t *display, tpl_handle_t window,
								  int *width, int *height, tbm_format *format,
								  int depth, int a_size)
{
	TPL_IGNORE(display);

	tbm_surface_queue_h surf_queue = (tbm_surface_queue_h)window;
	if (!surf_queue) {
		TPL_ERR("Native window(%p) is invalid.", window);
		return TPL_ERROR_INVALID_PARAMETER;
	}

	if (width) *width = tbm_surface_queue_get_width(surf_queue);
	if (height) *height = tbm_surface_queue_get_height(surf_queue);
	if (format) *format = tbm_surface_queue_get_format(surf_queue);

	return TPL_ERROR_NONE;
}

static tpl_result_t
__tpl_tbm_display_get_pixmap_info(tpl_display_t *display, tpl_handle_t pixmap,
								  int *width, int *height, tbm_format *format)
{
	tbm_surface_h	tbm_surface = NULL;

	tbm_surface = (tbm_surface_h)pixmap;
	if (!tbm_surface) {
		TPL_ERR("Native pixmap(%p) is invalid.", pixmap);
		return TPL_ERROR_INVALID_PARAMETER;
	}

	if (width) *width = tbm_surface_get_width(tbm_surface);
	if (height) *height = tbm_surface_get_height(tbm_surface);
	if (format) *format = tbm_surface_get_format(tbm_surface);

	return TPL_ERROR_NONE;
}

static tbm_surface_h
__tpl_tbm_display_get_buffer_from_native_pixmap(tpl_handle_t pixmap)
{
	TPL_ASSERT(pixmap);
	return (tbm_surface_h)pixmap;
}

static void
__tpl_tbm_surface_queue_notify_cb(tbm_surface_queue_h surface_queue, void *data)
{
	/* Do something */
}

#if USE_WORKER_THREAD == 1
static void
__tpl_tbm_draw_done(tpl_surface_t *surface, tbm_surface_h tbm_surface, tpl_result_t result)
{
	tpl_tbm_surface_t *tpl_tbm_surface = NULL;
	tpl_tbm_buffer_t *tpl_tbm_buffer = NULL;
	tbm_surface_queue_h tbm_queue = NULL;

	TPL_ASSERT(surface);
	TPL_ASSERT(tbm_surface);
	TPL_ASSERT(tbm_surface_internal_is_valid(tbm_surface));

	tpl_tbm_surface = (tpl_tbm_surface_t *)surface->backend.data;
	tpl_tbm_buffer = __tpl_tbm_get_tbm_buffer_from_tbm_surface(tbm_surface);
	tbm_queue = (tbm_surface_queue_h)surface->native_handle;

	TPL_ASSERT(tpl_tbm_surface);
	TPL_ASSERT(tpl_tbm_buffer);
	TPL_ASSERT(tbm_queue);

	close(tpl_tbm_buffer->wait_sync);
	tpl_tbm_buffer->wait_sync = -1;

	/* if server supported current supported mode then just send */

	if (tpl_tbm_surface->present_mode == TPL_DISPLAY_PRESENT_MODE_FIFO) {
		pthread_mutex_lock(&tpl_tbm_surface->vblank_list_mutex);
		/* unref in tpl list remove callback
		   (__tpl_tbm_buffer_remove_from_list) */
		tbm_surface_internal_ref(tbm_surface);
		__tpl_list_push_back(&tpl_tbm_surface->vblank_list, tbm_surface);
		pthread_mutex_unlock(&tpl_tbm_surface->vblank_list_mutex);
	} else if (tpl_tbm_surface->present_mode == TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED &&
			   tpl_tbm_surface->vblank_done == TPL_FALSE) {
		/* if can't process previous vblank event, send buffer immediately */
		pthread_mutex_lock(&tpl_tbm_surface->vblank_list_mutex);
		/* unref in tpl list remove callback
		   (__tpl_tbm_buffer_remove_from_list) */
		tbm_surface_internal_ref(tbm_surface);
		__tpl_list_push_back(&tpl_tbm_surface->vblank_list, tbm_surface);
		tpl_tbm_surface->vblank_done = TPL_TRUE;
		pthread_mutex_unlock(&tpl_tbm_surface->vblank_list_mutex);
	} else {
		tbm_surface_internal_unref(tbm_surface);
		if (tbm_surface_queue_enqueue(tbm_queue, tbm_surface) != TBM_SURFACE_QUEUE_ERROR_NONE) {
			TPL_ERR("tbm_surface_queue_enqueue failed. tbm_queue(%p) tbm_surface(%p)",
					tbm_queue, tbm_surface);
		}
	}
}

static int
__tpl_tbm_draw_wait_fd_get(tpl_surface_t *surface, tbm_surface_h tbm_surface)
{
	tpl_tbm_buffer_t *tpl_tbm_buffer;

	TPL_ASSERT(tbm_surface);
	TPL_ASSERT(tbm_surface_internal_is_valid(tbm_surface));

	tpl_tbm_buffer = __tpl_tbm_get_tbm_buffer_from_tbm_surface(tbm_surface);
	return tpl_tbm_buffer->wait_sync;
}

static void
__tpl_tbm_vblank(tpl_surface_t *surface, unsigned int sequence, unsigned int tv_sec,
				 unsigned int tv_usec)
{
	tpl_tbm_surface_t *tpl_tbm_surface;
	tbm_surface_h tbm_surface;

	TPL_ASSERT(surface);

	tpl_tbm_surface = (tpl_tbm_surface_t *)surface->backend.data;

	TPL_ASSERT(tpl_tbm_surface);

	if ((tpl_tbm_surface->present_mode &
		 (TPL_DISPLAY_PRESENT_MODE_FIFO | TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED)) == 0)
		return;

	pthread_mutex_lock(&tpl_tbm_surface->vblank_list_mutex);
	tbm_surface = __tpl_list_pop_front(&tpl_tbm_surface->vblank_list,
									   __tpl_tbm_buffer_remove_from_list);
	pthread_mutex_unlock(&tpl_tbm_surface->vblank_list_mutex);

	 if (tbm_surface_internal_is_valid(tbm_surface)) {
		 tbm_surface_queue_h tbm_queue = (tbm_surface_queue_h)surface->native_handle;
		 if (tbm_surface_queue_enqueue(tbm_queue, tbm_surface) != TBM_SURFACE_QUEUE_ERROR_NONE) {
			TPL_ERR("tbm_surface_queue_enqueue failed. tbm_queue(%p) tbm_surface(%p)",
					tbm_queue, tbm_surface);
		}
		tpl_tbm_surface->vblank_done = TPL_TRUE;
	} else {
		tpl_tbm_surface->vblank_done = TPL_FALSE;
	}

}

static tbm_surface_h
__tpl_tbm_draw_wait_buffer_get(tpl_surface_t *surface)
{
	tpl_tbm_surface_t *tpl_tbm_surface;
	tbm_surface_h tbm_surface;

	tpl_tbm_surface = surface->backend.data;
	pthread_mutex_init(&tpl_tbm_surface->draw_waiting_mutex, NULL);
	tbm_surface = __tpl_list_pop_front(&tpl_tbm_surface->draw_waiting_queue, NULL);
	pthread_mutex_unlock(&tpl_tbm_surface->draw_waiting_mutex);

	return tbm_surface;
}
#endif

static tpl_result_t
__tpl_tbm_surface_create_swapchain(tpl_surface_t *surface,
		tbm_format format, int width,
		int height, int buffer_count, int present_mode)
{
	tpl_tbm_surface_t *tpl_tbm_surface = NULL;

	TPL_ASSERT(surface);

	tpl_tbm_surface = (tpl_tbm_surface_t *) surface->backend.data;
	TPL_ASSERT(tpl_tbm_surface);

	/* FIXME: vblank has performance problem so replace all present mode to MAILBOX */
	present_mode = TPL_DISPLAY_PRESENT_MODE_MAILBOX;

	/* TODO: check server supported present modes */
	switch (present_mode) {
		case TPL_DISPLAY_PRESENT_MODE_MAILBOX:
		case TPL_DISPLAY_PRESENT_MODE_IMMEDIATE:
			break;
#if USE_WORKER_THREAD == 1
		case TPL_DISPLAY_PRESENT_MODE_FIFO:
		case TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED:
			if (__tpl_worker_support_vblank() == TPL_FALSE) {
				TPL_ERR("Unsupported present mode: %d, worker not support vblank",
						present_mode);
				return TPL_ERROR_INVALID_PARAMETER;
			}
#endif
		default:
			TPL_ERR("Unsupported present mode: %d", present_mode);
			return TPL_ERROR_INVALID_PARAMETER;
	}

	tpl_tbm_surface->present_mode = present_mode;

#if USE_WORKER_THREAD == 1
	tpl_tbm_surface->worker_surface.surface = surface;
	tpl_tbm_surface->worker_surface.draw_done = __tpl_tbm_draw_done;
	tpl_tbm_surface->worker_surface.draw_wait_fd_get = __tpl_tbm_draw_wait_fd_get;
	tpl_tbm_surface->worker_surface.vblank = __tpl_tbm_vblank;
	tpl_tbm_surface->worker_surface.draw_wait_buffer_get = __tpl_tbm_draw_wait_buffer_get;

	__tpl_list_init(&tpl_tbm_surface->vblank_list);
	__tpl_list_init(&tpl_tbm_surface->draw_waiting_queue);
	pthread_mutex_init(&tpl_tbm_surface->vblank_list_mutex, NULL);
	pthread_mutex_init(&tpl_tbm_surface->draw_waiting_mutex, NULL);

	__tpl_worker_surface_list_insert(&tpl_tbm_surface->worker_surface);
	tpl_tbm_surface->need_worker_clear = TPL_TRUE;
#endif

	return TPL_ERROR_NONE;
}

#if USE_WORKER_THREAD == 1
static tpl_result_t
__tpl_tbm_surface_destroy_swapchain(tpl_surface_t *surface)
{
	tpl_tbm_surface_t *tpl_tbm_surface = NULL;

	TPL_ASSERT(surface);

	tpl_tbm_surface = (tpl_tbm_surface_t *) surface->backend.data;
	TPL_ASSERT(tpl_tbm_surface);

	__tpl_worker_surface_list_remove(&tpl_tbm_surface->worker_surface);

	pthread_mutex_lock(&tpl_tbm_surface->vblank_list_mutex);
	__tpl_list_fini(&tpl_tbm_surface->vblank_list, NULL);
	pthread_mutex_unlock(&tpl_tbm_surface->vblank_list_mutex);
	pthread_mutex_destroy(&tpl_tbm_surface->vblank_list_mutex);

	pthread_mutex_lock(&tpl_tbm_surface->draw_waiting_mutex);
	__tpl_list_fini(&tpl_tbm_surface->draw_waiting_queue, NULL);
	pthread_mutex_unlock(&tpl_tbm_surface->draw_waiting_mutex);
	pthread_mutex_destroy(&tpl_tbm_surface->draw_waiting_mutex);
	tpl_tbm_surface->need_worker_clear = TPL_FALSE;

	return TPL_ERROR_NONE;
}
#endif

static tpl_result_t
__tpl_tbm_surface_init(tpl_surface_t *surface)
{
	tpl_tbm_surface_t *tpl_tbm_surface = NULL;
	TPL_ASSERT(surface);

	tpl_tbm_surface = (tpl_tbm_surface_t *) calloc(1, sizeof(tpl_tbm_surface_t));
	if (!tpl_tbm_surface) {
		TPL_ERR("Failed to allocate memory for new tpl_tbm_surface_t");
		return TPL_ERROR_OUT_OF_MEMORY;
	}

	surface->backend.data = (void *)tpl_tbm_surface;

	if (surface->type == TPL_SURFACE_TYPE_WINDOW) {
		if (__tpl_tbm_display_get_window_info(surface->display,
											  surface->native_handle, &surface->width,
											  &surface->height, NULL, 0, 0) != TPL_ERROR_NONE) {
			TPL_ERR("Failed to get native window(%p) info.",
					surface->native_handle);
			goto error;
		}

		tbm_surface_queue_add_destroy_cb((tbm_surface_queue_h)surface->native_handle,
										 __tpl_tbm_surface_queue_notify_cb,
										 surface);

		return TPL_ERROR_NONE;
	} else if (surface->type == TPL_SURFACE_TYPE_PIXMAP) {
		if (__tpl_tbm_display_get_pixmap_info(surface->display,
											  surface->native_handle, &surface->width,
											  &surface->height, NULL) != TPL_TRUE) {
			TPL_ERR("Failed to get native pixmap(%p) info.",
					surface->native_handle);

			goto error;
		}

		tbm_surface_internal_ref((tbm_surface_h)surface->native_handle);

		return TPL_ERROR_NONE;
	}

error:
	free(tpl_tbm_surface);
	surface->backend.data = NULL;

	return TPL_ERROR_INVALID_OPERATION;
}

static void
__tpl_tbm_surface_fini(tpl_surface_t *surface)
{
#if USE_WORKER_THREAD == 1
	tpl_tbm_surface_t *tpl_tbm_surface = NULL;

	TPL_ASSERT(surface);

	tpl_tbm_surface = (tpl_tbm_surface_t *) surface->backend.data;
	TPL_ASSERT(tpl_tbm_surface);

	if (tpl_tbm_surface->need_worker_clear)
		__tpl_tbm_surface_destroy_swapchain(surface);
#endif

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->display);

	if (surface->type == TPL_SURFACE_TYPE_PIXMAP)
		tbm_surface_internal_unref((tbm_surface_h)surface->native_handle);
	else if (surface->type == TPL_SURFACE_TYPE_WINDOW) {
		tbm_surface_queue_remove_destroy_cb(
			(tbm_surface_queue_h)surface->native_handle,
			 __tpl_tbm_surface_queue_notify_cb, surface);
		/*TODO: we need fix for dequeued surface*/
	}

	free(surface->backend.data);
	surface->backend.data = NULL;
}

static tpl_result_t
__tpl_tbm_surface_enqueue_buffer(tpl_surface_t *surface,
								 tbm_surface_h tbm_surface, int num_rects,
								 const int *rects, tbm_fd sync_fence)
{
#if USE_WORKER_THREAD == 1
	tpl_tbm_surface_t *tpl_tbm_surface = NULL;
	tpl_tbm_buffer_t *tpl_tbm_buffer = NULL;
#else
	tbm_surface_queue_h tbm_queue;
#endif

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->display);
	TPL_ASSERT(surface->display->native_handle);
	TPL_ASSERT(tbm_surface);
	TPL_IGNORE(num_rects);
	TPL_IGNORE(rects);

	if (!tbm_surface_internal_is_valid(tbm_surface)) {
		TPL_ERR("Failed to enqueue tbm_surface(%p) Invalid value.", tbm_surface);
		return TPL_ERROR_INVALID_PARAMETER;
	}

	tbm_surface_internal_unref(tbm_surface);

	if (surface->type == TPL_SURFACE_TYPE_PIXMAP) {
		TPL_ERR("Pixmap cannot post(%p, %p)", surface,
				surface->native_handle);
		return TPL_ERROR_INVALID_PARAMETER;
	}
#if USE_WORKER_THREAD == 1
	tpl_tbm_surface = surface->backend.data;

	tpl_tbm_buffer = __tpl_tbm_get_tbm_buffer_from_tbm_surface(tbm_surface);
	tpl_tbm_buffer->wait_sync = sync_fence;

	tbm_surface_internal_ref(tbm_surface);
	pthread_mutex_init(&tpl_tbm_surface->draw_waiting_mutex, NULL);
	__tpl_list_push_back(&tpl_tbm_surface->draw_waiting_queue, tbm_surface);
	pthread_mutex_unlock(&tpl_tbm_surface->draw_waiting_mutex);
	__tpl_worker_new_buffer_notify(&tpl_tbm_surface->worker_surface);
#else
	tbm_queue = (tbm_surface_queue_h)surface->native_handle;

	if (!tbm_queue) {
		TPL_ERR("tbm_surface_queue is invalid.");
		return TPL_ERROR_INVALID_PARAMETER;
	}

	if (sync_fence != -1) {
		tbm_sync_fence_wait(sync_fence, -1);
		close(sync_fence);
	}

	if (tbm_surface_queue_enqueue(tbm_queue, tbm_surface)
			!= TBM_SURFACE_QUEUE_ERROR_NONE) {
		TPL_ERR("tbm_surface_queue_enqueue failed. tbm_queue(%p) tbm_surface(%p)",
				tbm_queue, tbm_surface);
		return TPL_ERROR_INVALID_OPERATION;
	}
#endif

	return TPL_ERROR_NONE;
}

static tpl_bool_t
__tpl_tbm_surface_validate(tpl_surface_t *surface)
{
	TPL_IGNORE(surface);

	return TPL_TRUE;
}

static tbm_surface_h
__tpl_tbm_surface_dequeue_buffer(tpl_surface_t *surface, uint64_t timeout_ns,
								 tbm_fd *sync_fence)
{
	tbm_surface_h tbm_surface = NULL;
	tbm_surface_queue_h tbm_queue = NULL;
	tbm_surface_queue_error_e tsq_err = 0;
#if USE_WORKER_THREAD == 1
	tpl_tbm_buffer_t *tpl_tbm_buffer = NULL;
#endif

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->native_handle);
	TPL_ASSERT(surface->display);
	TPL_ASSERT(surface->display->native_handle);

	if (sync_fence)
		*sync_fence = -1;

	tbm_queue = (tbm_surface_queue_h)surface->native_handle;

	tsq_err = tbm_surface_queue_dequeue(tbm_queue, &tbm_surface);
	if (!tbm_surface  && tbm_surface_queue_can_dequeue(tbm_queue, 1) == 1) {
		tsq_err = tbm_surface_queue_dequeue(tbm_queue, &tbm_surface);
		if (!tbm_surface) {
			TPL_ERR("Failed to get tbm_surface from tbm_surface_queue | tsq_err = %d",
					tsq_err);
			return NULL;
		}
	}

#if USE_WORKER_THREAD == 1
	if ((tpl_tbm_buffer =__tpl_tbm_get_tbm_buffer_from_tbm_surface(tbm_surface)) == NULL) {
		tpl_tbm_buffer = (tpl_tbm_buffer_t *) calloc(1, sizeof(tpl_tbm_buffer_t));
		if (!tpl_tbm_buffer) {
			TPL_ERR("Mem alloc for tpl_tbm_buffer failed!");
			return NULL;
		}
	}
#endif

	/* Inc ref count about tbm_surface */
	/* It will be dec when before tbm_surface_queue_enqueue called */
	tbm_surface_internal_ref(tbm_surface);

	return tbm_surface;
}

static tpl_result_t
__tpl_tbm_surface_get_swapchain_buffers(tpl_surface_t *surface,
		tbm_surface_h **buffers,
		int *buffer_count)
{
	tbm_surface_h buffer = NULL;
	tbm_surface_queue_h tbm_queue = NULL;
	tbm_surface_h *swapchain_buffers = NULL;
	tbm_surface_queue_error_e tsq_err;
	tpl_result_t ret = TPL_ERROR_NONE;
	int i, queue_size, dequeue_count = 0;

	TPL_ASSERT(surface);
	TPL_ASSERT(buffers);
	TPL_ASSERT(buffer_count);

	tbm_queue = (tbm_surface_queue_h)surface->native_handle;
	TPL_ASSERT(tbm_queue);

	queue_size = tbm_surface_queue_get_size(tbm_queue);
	swapchain_buffers = (tbm_surface_h *)calloc(1, sizeof(tbm_surface_h) * queue_size);
	if (!swapchain_buffers) {
		TPL_ERR("Failed to allocate memory for buffers.");
		return TPL_ERROR_OUT_OF_MEMORY;
	}

	for (i = 0; i < queue_size; i++) {
		tsq_err = tbm_surface_queue_dequeue(tbm_queue, &buffer);
		if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE) {
			TPL_ERR("Failed to get tbm_surface from tbm_surface_queue | tsq_err = %d",
					tsq_err);
			dequeue_count = i;
			ret = TPL_ERROR_OUT_OF_MEMORY;
			goto get_buffer_fail;
		}
		swapchain_buffers[i] = buffer;
	}

	for (i = 0 ; i < queue_size; i++) {
		tsq_err = tbm_surface_queue_release(tbm_queue, swapchain_buffers[i]);
		if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE) {
			TPL_ERR("Failed to release tbm_surface. | tsq_err = %d", tsq_err);
			ret = TPL_ERROR_INVALID_OPERATION;
			goto release_buffer_fail;
		}
	}

	*buffers = swapchain_buffers;
	*buffer_count = queue_size;
	return TPL_ERROR_NONE;

get_buffer_fail:
	for (i = 0 ; i < dequeue_count ; i++) {
		tsq_err = tbm_surface_queue_release(tbm_queue, swapchain_buffers[i]);
		if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE) {
			TPL_ERR("Failed to release tbm_surface. | tsq_err = %d", tsq_err);
			goto release_buffer_fail;
		}
	}

release_buffer_fail:
	free(swapchain_buffers);
	return ret;

}

tpl_bool_t
__tpl_display_choose_backend_tbm(tpl_handle_t native_dpy)
{
	tpl_bool_t ret = TPL_FALSE;
	tbm_bufmgr bufmgr = NULL;

	if (!native_dpy) return TPL_FALSE;

	bufmgr = tbm_bufmgr_init(-1);

	if (bufmgr == (tbm_bufmgr)native_dpy) ret = TPL_TRUE;

	if (bufmgr) tbm_bufmgr_deinit(bufmgr);

	return ret;
}

static tpl_result_t
__tpl_tbm_display_query_window_supported_buffer_count(
	tpl_display_t *display,
	tpl_handle_t window, int *min, int *max)
{
	TPL_ASSERT(display);

	if (!display->backend.data) return TPL_ERROR_INVALID_OPERATION;

	if (min) *min = 0;
	if (max) *max = 0; /* 0 mean no limit in vulkan */

	return TPL_ERROR_NONE;
}

static tpl_result_t
__tpl_tbm_display_query_window_supported_present_modes(
	tpl_display_t *display,
	tpl_handle_t window, int *modes)
{
	TPL_ASSERT(display);

	if (!display->backend.data) return TPL_ERROR_INVALID_OPERATION;

	if (modes) {
		*modes = TPL_DISPLAY_PRESENT_MODE_MAILBOX | TPL_DISPLAY_PRESENT_MODE_IMMEDIATE;
#if USE_WORKER_THREAD == 1
		if (__tpl_worker_support_vblank() == TPL_TRUE)
			*modes |= TPL_DISPLAY_PRESENT_MODE_FIFO | TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED;
#endif
	}


	return TPL_ERROR_NONE;
}

void
__tpl_display_init_backend_tbm(tpl_display_backend_t *backend)
{
	TPL_ASSERT(backend);

	backend->type = TPL_BACKEND_TBM;
	backend->data = NULL;

	backend->init = __tpl_tbm_display_init;
	backend->fini = __tpl_tbm_display_fini;
	backend->query_config = __tpl_tbm_display_query_config;
	backend->filter_config = __tpl_tbm_display_filter_config;
	backend->get_window_info = __tpl_tbm_display_get_window_info;
	backend->get_pixmap_info = __tpl_tbm_display_get_pixmap_info;
	backend->get_buffer_from_native_pixmap =
		__tpl_tbm_display_get_buffer_from_native_pixmap;
	backend->query_window_supported_buffer_count =
		__tpl_tbm_display_query_window_supported_buffer_count;
	backend->query_window_supported_present_modes =
		__tpl_tbm_display_query_window_supported_present_modes;

}

void
__tpl_surface_init_backend_tbm(tpl_surface_backend_t *backend)
{
	TPL_ASSERT(backend);

	backend->type = TPL_BACKEND_TBM;
	backend->data = NULL;

	backend->init = __tpl_tbm_surface_init;
	backend->fini = __tpl_tbm_surface_fini;
	backend->validate = __tpl_tbm_surface_validate;
	backend->dequeue_buffer = __tpl_tbm_surface_dequeue_buffer;
	backend->enqueue_buffer = __tpl_tbm_surface_enqueue_buffer;
	backend->create_swapchain = __tpl_tbm_surface_create_swapchain;
#if USE_WORKER_THREAD == 1
	backend->destroy_swapchain = __tpl_tbm_surface_destroy_swapchain;
#endif
	backend->get_swapchain_buffers =
		__tpl_tbm_surface_get_swapchain_buffers;
}

