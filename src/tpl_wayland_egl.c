#define inline __inline__

#include <wayland-client.h>

#include "wayland-egl/wayland-egl-priv.h"

#include <drm.h>
#include <xf86drm.h>

#undef inline

#include "tpl_internal.h"

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <tbm_surface_queue.h>
#include <wayland-tbm-client.h>
#include <wayland-tbm-server.h>

/* In wayland, application and compositor create its own drawing buffers. Recommend size is more than 2. */
#define CLIENT_QUEUE_SIZE 3

typedef struct _tpl_wayland_egl_display tpl_wayland_egl_display_t;
typedef struct _tpl_wayland_egl_surface tpl_wayland_egl_surface_t;
typedef struct _tpl_wayland_egl_buffer tpl_wayland_egl_buffer_t;

struct _tpl_wayland_egl_display {
	tbm_bufmgr bufmgr;
	struct wayland_tbm_client *wl_tbm_client;
	struct wl_event_queue *wl_queue;
	struct wl_registry *wl_registry;
};

struct _tpl_wayland_egl_surface {
	tbm_surface_queue_h tbm_queue;
	tbm_surface_h current_buffer;
	tpl_bool_t resized;
	struct wl_proxy *wl_proxy; /* wl_tbm_queue proxy */
};

struct _tpl_wayland_egl_buffer {
	tpl_display_t *display;
	tbm_bo bo;
	tpl_wayland_egl_surface_t *wayland_egl_surface;
	struct wl_proxy *wl_proxy; /* wl_buffer proxy */
};

static const struct wl_registry_listener registry_listener;
static const struct wl_callback_listener sync_listener;
static const struct wl_callback_listener frame_listener;
static const struct wl_buffer_listener buffer_release_listener;

#define TPL_BUFFER_CACHE_MAX_ENTRIES 40

static int tpl_wayland_egl_buffer_key;
#define KEY_tpl_wayland_egl_buffer  (unsigned long)(&tpl_wayland_egl_buffer_key)

static void __tpl_wayland_egl_buffer_free(tpl_wayland_egl_buffer_t
		*wayland_egl_buffer);

static TPL_INLINE tpl_wayland_egl_buffer_t *
__tpl_wayland_egl_get_wayland_buffer_from_tbm_surface(tbm_surface_h surface)
{
	tbm_bo bo;
	tpl_wayland_egl_buffer_t *buf = NULL;

	bo = tbm_surface_internal_get_bo(surface, 0);
	tbm_bo_get_user_data(bo, KEY_tpl_wayland_egl_buffer, (void **)&buf);

	return buf;
}

static TPL_INLINE void
__tpl_wayland_egl_set_wayland_buffer_to_tbm_surface(tbm_surface_h surface,
		tpl_wayland_egl_buffer_t *buf)
{
	tbm_bo bo;

	bo = tbm_surface_internal_get_bo(surface, 0);

	tbm_bo_add_user_data(bo, KEY_tpl_wayland_egl_buffer,
			     (tbm_data_free)__tpl_wayland_egl_buffer_free);

	tbm_bo_set_user_data(bo, KEY_tpl_wayland_egl_buffer, buf);
}

static TPL_INLINE tpl_bool_t
__tpl_wayland_egl_display_is_wl_display(tpl_handle_t native_dpy)
{
	TPL_ASSERT(native_dpy);

	struct wl_interface *wl_egl_native_dpy = *(void **) native_dpy;

	/* MAGIC CHECK: A native display handle is a wl_display if the de-referenced first value
	   is a memory address pointing the structure of wl_display_interface. */
	if ( wl_egl_native_dpy == &wl_display_interface ) {
		return TPL_TRUE;
	}

	if (strncmp(wl_egl_native_dpy->name, wl_display_interface.name,
		    strlen(wl_display_interface.name)) == 0) {
		return TPL_TRUE;
	}

	return TPL_FALSE;
}

static int
__tpl_wayland_egl_display_roundtrip(tpl_display_t *display)
{
	struct wl_display *wl_dpy;
	tpl_wayland_egl_display_t *wayland_egl_display;
	struct wl_callback *callback;
	int done = 0, ret = 0;

	TPL_ASSERT(display);
	TPL_ASSERT(display->native_handle);
	TPL_ASSERT(display->backend.data);

	wl_dpy = (struct wl_display *) display->native_handle;
	wayland_egl_display = (tpl_wayland_egl_display_t *) display->backend.data;

	callback = wl_display_sync(wl_dpy);
	wl_callback_add_listener(callback, &sync_listener, &done);

	wl_proxy_set_queue((struct wl_proxy *) callback, wayland_egl_display->wl_queue);

	while (ret != -1 && !done) {
		ret = wl_display_dispatch_queue(wl_dpy, wayland_egl_display->wl_queue);
	}

	return ret;
}

static tpl_result_t
__tpl_wayland_egl_display_init(tpl_display_t *display)
{
	tpl_wayland_egl_display_t *wayland_egl_display = NULL;

	TPL_ASSERT(display);

	/* Do not allow default display in wayland. */
	if (!display->native_handle) {
		TPL_ERR("Invalid native handle for display.");
		return TPL_ERROR_INVALID_PARAMETER;
	}

	wayland_egl_display = (tpl_wayland_egl_display_t *) calloc(1,
			      sizeof(tpl_wayland_egl_display_t));
	if (!wayland_egl_display) {
		TPL_ERR("Failed to allocate memory for new tpl_wayland_egl_display_t.");
		return TPL_ERROR_INVALID_OPERATION;
	}

	display->backend.data = wayland_egl_display;
	display->bufmgr_fd = -1;

	if (__tpl_wayland_egl_display_is_wl_display(display->native_handle)) {
		struct wl_display *wl_dpy =
			(struct wl_display *)display->native_handle;
		wayland_egl_display->wl_tbm_client =
			wayland_tbm_client_init((struct wl_display *) wl_dpy);

		if (!wayland_egl_display->wl_tbm_client) {
			TPL_ERR("Wayland TBM initialization failed!");
			goto free_wl_display;
		}

		wayland_egl_display->wl_queue = wl_display_create_queue(wl_dpy);
		if (!wayland_egl_display->wl_queue) {
			TPL_ERR("Failed to create wl_queue with wl_dpy(%p).", wl_dpy);
			goto free_wl_display;
		}

		wayland_egl_display->wl_registry = wl_display_get_registry(wl_dpy);
		if (!wayland_egl_display->wl_registry) {
			TPL_ERR("Failed to get wl_registry with wl_dpy(%p).", wl_dpy);
			goto destroy_queue;
		}

		wl_proxy_set_queue((struct wl_proxy *)wayland_egl_display->wl_registry,
				   wayland_egl_display->wl_queue);
	} else {
		goto free_wl_display;
	}

	return TPL_ERROR_NONE;

destroy_queue:
	wl_event_queue_destroy(wayland_egl_display->wl_queue);

free_wl_display:
	if (wayland_egl_display) {
		free(wayland_egl_display);
		display->backend.data = NULL;
	}
	return TPL_ERROR_INVALID_OPERATION;
}

static void
__tpl_wayland_egl_display_fini(tpl_display_t *display)
{
	tpl_wayland_egl_display_t *wayland_egl_display;

	TPL_ASSERT(display);

	wayland_egl_display = (tpl_wayland_egl_display_t *)display->backend.data;
	if (wayland_egl_display) {
		wayland_tbm_client_deinit(wayland_egl_display->wl_tbm_client);
		free(wayland_egl_display);
	}
	display->backend.data = NULL;
}

static tpl_result_t
__tpl_wayland_egl_display_query_config(tpl_display_t *display,
				       tpl_surface_type_t surface_type,
				       int red_size, int green_size,
				       int blue_size, int alpha_size,
				       int color_depth, int *native_visual_id,
				       tpl_bool_t *is_slow)
{
	TPL_ASSERT(display);

	if (surface_type == TPL_SURFACE_TYPE_WINDOW && red_size == 8 &&
	    green_size == 8 && blue_size == 8 &&
	    (color_depth == 32 || color_depth == 24)) {

		if (alpha_size == 8) {
			if (native_visual_id) *native_visual_id = TBM_FORMAT_ARGB8888;
			if (is_slow) *is_slow = TPL_FALSE;
			return TPL_ERROR_NONE;
		}
		if (alpha_size == 0) {
			if (native_visual_id) *native_visual_id = TBM_FORMAT_XRGB8888;
			if (is_slow) *is_slow = TPL_FALSE;
			return TPL_ERROR_NONE;
		}
	}

	return TPL_ERROR_INVALID_PARAMETER;
}

static tpl_result_t
__tpl_wayland_egl_display_filter_config(tpl_display_t *display, int *visual_id,
					int alpha_size)
{
	TPL_IGNORE(display);
	TPL_IGNORE(visual_id);
	TPL_IGNORE(alpha_size);
	return TPL_ERROR_NONE;
}

static tpl_result_t
__tpl_wayland_egl_display_get_window_info(tpl_display_t *display,
		tpl_handle_t window, int *width,
		int *height, tbm_format *format,
		int depth, int a_size)
{
	TPL_ASSERT(display);
	if (!window) return TPL_ERROR_INVALID_PARAMETER;

	struct wl_egl_window *wl_egl_window = (struct wl_egl_window *)window;

	if (format) {
		/* Wayland-egl window doesn't have native format information.
		   It is fixed from 'EGLconfig' when called eglCreateWindowSurface().
		   So we use the tpl_surface format instead. */
		tpl_surface_t *surface = wl_egl_window->private;
		if (surface) *format = surface->format;
		else {
			if (a_size == 8) *format = TBM_FORMAT_ARGB8888;
			else if (a_size == 0) *format = TBM_FORMAT_XRGB8888;
		}
	}
	if (width != NULL) *width = wl_egl_window->width;
	if (height != NULL) *height = wl_egl_window->height;

	return TPL_ERROR_NONE;
}

static tpl_result_t
__tpl_wayland_egl_display_get_pixmap_info(tpl_display_t *display,
		tpl_handle_t pixmap, int *width,
		int *height, tbm_format *format)
{
	tbm_surface_h	tbm_surface = NULL;

	tbm_surface = wayland_tbm_server_get_surface(NULL,
			(struct wl_resource *)pixmap);
	if (!tbm_surface) {
		TPL_ERR("Failed to get tbm_surface_h from native pixmap.");
		return TPL_ERROR_INVALID_OPERATION;
	}

	if (width) *width = tbm_surface_get_width(tbm_surface);
	if (height) *height = tbm_surface_get_height(tbm_surface);
	if (format) *format = tbm_surface_get_format(tbm_surface);

	return TPL_ERROR_NONE;
}

static tbm_surface_h
__tpl_wayland_egl_display_get_buffer_from_native_pixmap(tpl_handle_t pixmap)
{
	tbm_surface_h tbm_surface = NULL;

	TPL_ASSERT(pixmap);

	tbm_surface = wayland_tbm_server_get_surface(NULL,
			(struct wl_resource *)pixmap);
	if (!tbm_surface) {
		TPL_ERR("Failed to get tbm_surface_h from wayland_tbm.");
		return NULL;
	}

	return tbm_surface;
}

static void
__cb_client_window_resize_callback(struct wl_egl_window *wl_egl_window,
				   void *private);

static tpl_result_t
__tpl_wayland_egl_surface_init(tpl_surface_t *surface)
{
	tpl_wayland_egl_display_t *wayland_egl_display =
		(tpl_wayland_egl_display_t *) surface->display->backend.data;
	tpl_wayland_egl_surface_t *wayland_egl_surface = NULL;

	struct wl_egl_window *wl_egl_window = (struct wl_egl_window *)
					      surface->native_handle;

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->type == TPL_SURFACE_TYPE_WINDOW);
	TPL_ASSERT(surface->native_handle);

	wayland_egl_surface = (tpl_wayland_egl_surface_t *) calloc(1,
			      sizeof(tpl_wayland_egl_surface_t));
	if (!wayland_egl_surface) {
		TPL_ERR("Failed to allocate memory for new tpl_wayland_egl_surface_t.");
		return TPL_ERROR_INVALID_OPERATION;
	}

	surface->backend.data = (void *)wayland_egl_surface;
	wayland_egl_surface->tbm_queue = NULL;
	wayland_egl_surface->resized = TPL_FALSE;
	wayland_egl_surface->current_buffer = NULL;

	if (wl_egl_window->surface) {
		wayland_egl_surface->tbm_queue = wayland_tbm_client_create_surface_queue(
				wayland_egl_display->wl_tbm_client,
				wl_egl_window->surface,
				3,
				wl_egl_window->width,
				wl_egl_window->height,
				TBM_FORMAT_ARGB8888);

		/* libtpl-egl processes the wl_proxy(wl_tbm_queue) event at dequeue.
		 * When libtpl-egl gets the activate event from wl_tbm_queue,
		 * it gets the scanout wl_buffer from the display server.
		 * When libtpl-egl gets the decativate event from wl_tbm_queue,
		 * it release the sacnout wl_buffer and allocates the offscreen wl_buffer
		 * at the client-side.
		 * The activate/decactivate events is sent from the display server
		 * at the no-comoposite mode and the composite mode.
		 */
		wayland_egl_surface->wl_proxy = (struct wl_proxy *)wayland_tbm_client_get_wl_tbm_queue(wayland_egl_display->wl_tbm_client,
				wl_egl_window->surface);
		if (!wayland_egl_surface->wl_proxy) {
			TPL_ERR("Failed to allocate memory for new tpl_wayland_egl_surface_t.");
			return TPL_ERROR_INVALID_OPERATION;
		}
		/* set wl_proxy(wl_tbm_queue) to the wl_queue */
		wl_proxy_set_queue(wayland_egl_surface->wl_proxy, wayland_egl_display->wl_queue);
	}
	else
		/*Why wl_surafce is NULL ?*/
		wayland_egl_surface->tbm_queue = tbm_surface_queue_sequence_create(3,
						 wl_egl_window->width,
						 wl_egl_window->height,
						 TBM_FORMAT_ARGB8888,
						 0);

	if (!wayland_egl_surface->tbm_queue) {
		TPL_ERR("TBM surface queue creation failed!");
		free(wayland_egl_surface);
		return TPL_ERROR_INVALID_OPERATION;
	}

	surface->width = wl_egl_window->width;
	surface->height = wl_egl_window->height;
	surface->capabilities.min_buffer =
		tbm_surface_queue_get_size(wayland_egl_surface->tbm_queue);
	surface->capabilities.max_buffer =
		tbm_surface_queue_get_size(wayland_egl_surface->tbm_queue);

	wl_egl_window->private = surface;
	wl_egl_window->resize_callback = (void *)__cb_client_window_resize_callback;

	TPL_LOG(3, "window(%p, %p) %dx%d", surface, surface->native_handle,
		surface->width, surface->height);

	return TPL_ERROR_NONE;
}

static void
__tpl_wayland_egl_surface_fini(tpl_surface_t *surface)
{
	tpl_wayland_egl_surface_t *wayland_egl_surface = NULL;
	tpl_wayland_egl_display_t *wayland_egl_display = NULL;

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->display);

	wayland_egl_surface = (tpl_wayland_egl_surface_t *) surface->backend.data;
	if (wayland_egl_surface == NULL) return;

	wayland_egl_display = (tpl_wayland_egl_display_t *)
			      surface->display->backend.data;
	if (wayland_egl_display == NULL) return;

	TPL_LOG(3, "window(%p, %p)", surface, surface->native_handle);

	if (surface->type == TPL_SURFACE_TYPE_WINDOW) {
		struct wl_egl_window *wl_egl_window = (struct wl_egl_window *)
						      surface->native_handle;

		TPL_ASSERT(wl_egl_window);
		/* TPL_ASSERT(wl_egl_window->surface); */ /* to be enabled once evas/gl patch is in place */

		wl_egl_window->private = NULL;

		/* Detach all pending buffers */
		if (wl_egl_window->surface &&
		    /* if-statement to be removed once evas/gl patch is in place */
		    wl_egl_window->width == wl_egl_window->attached_width &&
		    wl_egl_window->height == wl_egl_window->attached_height) {

			wl_surface_attach(wl_egl_window->surface, NULL, 0, 0);
			wl_surface_commit(wl_egl_window->surface);
		}

		wl_display_flush(surface->display->native_handle);
		__tpl_wayland_egl_display_roundtrip(surface->display);

		if (wayland_egl_surface->current_buffer)
			tbm_surface_internal_unref(wayland_egl_surface->current_buffer);

		tbm_surface_queue_destroy(wayland_egl_surface->tbm_queue);
		wayland_egl_surface->tbm_queue = NULL;
	}

	free(wayland_egl_surface);
	surface->backend.data = NULL;
}

static tpl_result_t
__tpl_wayland_egl_surface_enqueue_buffer(tpl_surface_t *surface,
		tbm_surface_h tbm_surface,
		int num_rects, const int *rects)
{
	TPL_ASSERT(surface);
	TPL_ASSERT(surface->display);
	TPL_ASSERT(surface->display->native_handle);
	TPL_ASSERT(tbm_surface);

	struct wl_egl_window *wl_egl_window = NULL;
	tpl_wayland_egl_display_t *wayland_egl_display =
		(tpl_wayland_egl_display_t *) surface->display->backend.data;
	tpl_wayland_egl_surface_t *wayland_egl_surface =
		(tpl_wayland_egl_surface_t *) surface->backend.data;
	tpl_wayland_egl_buffer_t *wayland_egl_buffer = NULL;
	tbm_surface_queue_error_e tsq_err;

	TPL_LOG(3, "window(%p, %p)", surface, surface->native_handle);

	wayland_egl_buffer =
		__tpl_wayland_egl_get_wayland_buffer_from_tbm_surface(tbm_surface);
	TPL_ASSERT(wayland_egl_buffer);

	tbm_bo_handle bo_handle =
		tbm_bo_get_handle(wayland_egl_buffer->bo , TBM_DEVICE_CPU);

	if (bo_handle.ptr)
		TPL_IMAGE_DUMP(bo_handle.ptr, surface->width, surface->height,
			       surface->dump_count++);

	wl_egl_window = (struct wl_egl_window *)surface->native_handle;

	tbm_surface_internal_unref(tbm_surface);

	tsq_err = tbm_surface_queue_enqueue(wayland_egl_surface->tbm_queue,
					    tbm_surface);
	if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE) {
		TPL_ERR("Failed to enqeueue tbm_surface. | tsq_err = %d", tsq_err);
		return TPL_ERROR_INVALID_OPERATION;
	}

	/* deprecated */
	tsq_err = tbm_surface_queue_acquire(wayland_egl_surface->tbm_queue,
					    &tbm_surface);
	if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE) {
		TPL_ERR("Failed to acquire tbm_surface. | tsq_err = %d", tsq_err);
		return TPL_ERROR_INVALID_OPERATION;
	}

	tbm_surface_internal_ref(tbm_surface);
	wl_surface_attach(wl_egl_window->surface, (void *)wayland_egl_buffer->wl_proxy,
			  wl_egl_window->dx, wl_egl_window->dy);

	wl_egl_window->attached_width = wl_egl_window->width;
	wl_egl_window->attached_height = wl_egl_window->height;

	if (num_rects < 1 || rects == NULL) {
		wl_surface_damage(wl_egl_window->surface,
				  wl_egl_window->dx, wl_egl_window->dy,
				  wl_egl_window->width, wl_egl_window->height);
	} else {
		int i;
		for (i = 0; i < num_rects; i++) {
			wl_surface_damage(wl_egl_window->surface,
					  rects[i * 4 + 0], rects[i * 4 + 1],
					  rects[i * 4 + 2], rects[i * 4 + 3]);
		}
	}

	{
		/* Register a meaningless surface frame callback.
		   Because the buffer_release callback only be triggered if this callback is registered. */
		struct wl_callback *frame_callback = NULL;
		frame_callback = wl_surface_frame(wl_egl_window->surface);
		wl_callback_add_listener(frame_callback, &frame_listener, tbm_surface);
		wl_proxy_set_queue((struct wl_proxy *)frame_callback,
				   wayland_egl_display->wl_queue);
	}
	wl_surface_commit(wl_egl_window->surface);

	wl_display_flush(surface->display->native_handle);

	return TPL_ERROR_NONE;
}

static tpl_bool_t
__tpl_wayland_egl_surface_validate(tpl_surface_t *surface)
{
	TPL_ASSERT(surface);
	TPL_ASSERT(surface->backend.data);

	tpl_wayland_egl_surface_t *wayland_egl_surface =
		(tpl_wayland_egl_surface_t *)surface->backend.data;

	if (wayland_egl_surface->resized) return TPL_FALSE;

	return TPL_TRUE;
}

static tbm_surface_h
__tpl_wayland_egl_surface_dequeue_buffer(tpl_surface_t *surface)
{
	TPL_ASSERT(surface);
	TPL_ASSERT(surface->backend.data);
	TPL_ASSERT(surface->display);

	tbm_surface_h tbm_surface = NULL;
	tpl_wayland_egl_buffer_t *wayland_egl_buffer = NULL;
	tpl_wayland_egl_surface_t *wayland_egl_surface =
		(tpl_wayland_egl_surface_t *)surface->backend.data;
	tpl_wayland_egl_display_t *wayland_egl_display =
		(tpl_wayland_egl_display_t *)surface->display->backend.data;
	struct wl_proxy *wl_proxy = NULL;
	tbm_surface_queue_error_e tsq_err = 0;

	/* Check whether the surface was resized by wayland_egl */
	if (wayland_egl_surface->resized == TPL_TRUE) {
		struct wl_egl_window *wl_egl_window = (struct wl_egl_window *)
						      surface->native_handle;
		int width, height, format;
		width = wl_egl_window->width;
		height = wl_egl_window->height;
		format = tbm_surface_queue_get_format(wayland_egl_surface->tbm_queue);

		/* TODO: In dequeue_buffer case, does it explictly need on unref of current_buffer? */
		if (wayland_egl_surface->current_buffer)
			tbm_surface_internal_unref(wayland_egl_surface->current_buffer);

		tbm_surface_queue_reset(wayland_egl_surface->tbm_queue, width, height, format);
		wayland_egl_surface->resized = TPL_FALSE;
		surface->width = width;
		surface->height = height;
	}

	TPL_OBJECT_UNLOCK(surface);
	wl_display_dispatch_queue_pending((struct wl_display *)
					  surface->display->native_handle,
					  wayland_egl_display->wl_queue);
	while (tbm_surface_queue_can_dequeue(
		       wayland_egl_surface->tbm_queue, 0) == 0) {
		/* Application sent all buffers to the server. Wait for server response. */
		if (wl_display_dispatch_queue(surface->display->native_handle,
					      wayland_egl_display->wl_queue) == -1) {
			TPL_OBJECT_LOCK(surface);
			return NULL;
		}
	}
	TPL_OBJECT_LOCK(surface);

	tsq_err = tbm_surface_queue_dequeue(wayland_egl_surface->tbm_queue,
					    &tbm_surface);
	if (!tbm_surface) {
		TPL_ERR("Failed to get tbm_surface from tbm_surface_queue | tsq_err = %d",
			tsq_err);
		return NULL;
	}

	tbm_surface_internal_ref(tbm_surface);

	if ((wayland_egl_buffer =
		     __tpl_wayland_egl_get_wayland_buffer_from_tbm_surface(tbm_surface)) != NULL) {
		return tbm_surface;
	}

	wayland_egl_buffer = (tpl_wayland_egl_buffer_t *) calloc(1,
			     sizeof(tpl_wayland_egl_buffer_t));
	if (!wayland_egl_buffer) {
		TPL_ERR("Mem alloc for wayland_egl_buffer failed!");
		tbm_surface_internal_unref(tbm_surface);
		return NULL;
	}

	wl_proxy = (struct wl_proxy *)wayland_tbm_client_create_buffer(
			   wayland_egl_display->wl_tbm_client, tbm_surface);
	if (!wl_proxy) {
		TPL_ERR("Failed to create TBM client buffer!");
		tbm_surface_internal_unref(tbm_surface);
		free(wayland_egl_buffer);
		return NULL;
	}

	wl_proxy_set_queue(wl_proxy, wayland_egl_display->wl_queue);
	wl_buffer_add_listener((void *)wl_proxy, &buffer_release_listener,
			       tbm_surface);

	wl_display_flush((struct wl_display *)surface->display->native_handle);

	wayland_egl_buffer->display = surface->display;
	wayland_egl_buffer->wl_proxy = wl_proxy;
	wayland_egl_buffer->bo = tbm_surface_internal_get_bo(tbm_surface, 0);
	wayland_egl_buffer->wayland_egl_surface = wayland_egl_surface;
	wayland_egl_surface->current_buffer = tbm_surface;

	__tpl_wayland_egl_set_wayland_buffer_to_tbm_surface(tbm_surface,
			wayland_egl_buffer);

	return tbm_surface;
}

static void
__tpl_wayland_egl_buffer_free(tpl_wayland_egl_buffer_t *wayland_egl_buffer)
{
	TPL_ASSERT(wayland_egl_buffer);
	TPL_ASSERT(wayland_egl_buffer->display);

	tpl_wayland_egl_display_t *wayland_egl_display =
		(tpl_wayland_egl_display_t *)wayland_egl_buffer->display->backend.data;

	wl_display_flush((struct wl_display *)
			 wayland_egl_buffer->display->native_handle);

	if (wayland_egl_buffer->wl_proxy)
		wayland_tbm_client_destroy_buffer(wayland_egl_display->wl_tbm_client,
						  (void *)wayland_egl_buffer->wl_proxy);

	free(wayland_egl_buffer);
}

tpl_bool_t
__tpl_display_choose_backend_wayland_egl(tpl_handle_t native_dpy)
{
	if (!native_dpy) return TPL_FALSE;

	if (__tpl_wayland_egl_display_is_wl_display(native_dpy))
		return TPL_TRUE;

	return TPL_FALSE;
}

void
__tpl_display_init_backend_wayland_egl(tpl_display_backend_t *backend)
{
	TPL_ASSERT(backend);

	backend->type = TPL_BACKEND_WAYLAND;
	backend->data = NULL;

	backend->init = __tpl_wayland_egl_display_init;
	backend->fini = __tpl_wayland_egl_display_fini;
	backend->query_config = __tpl_wayland_egl_display_query_config;
	backend->filter_config = __tpl_wayland_egl_display_filter_config;
	backend->get_window_info = __tpl_wayland_egl_display_get_window_info;
	backend->get_pixmap_info = __tpl_wayland_egl_display_get_pixmap_info;
	backend->get_buffer_from_native_pixmap =
		__tpl_wayland_egl_display_get_buffer_from_native_pixmap;
}

void
__tpl_surface_init_backend_wayland_egl(tpl_surface_backend_t *backend)
{
	TPL_ASSERT(backend);

	backend->type = TPL_BACKEND_WAYLAND;
	backend->data = NULL;

	backend->init = __tpl_wayland_egl_surface_init;
	backend->fini = __tpl_wayland_egl_surface_fini;
	backend->validate = __tpl_wayland_egl_surface_validate;
	backend->dequeue_buffer = __tpl_wayland_egl_surface_dequeue_buffer;
	backend->enqueue_buffer = __tpl_wayland_egl_surface_enqueue_buffer;
}

static void
__cb_client_sync_callback(void *data, struct wl_callback *callback,
			  uint32_t serial)
{
	int *done;

	TPL_ASSERT(data);

	done = data;
	*done = 1;

	wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {
	__cb_client_sync_callback
};

static void
__cb_client_frame_callback(void *data, struct wl_callback *callback,
			   uint32_t time)
{
	/* We moved the buffer reclaim logic to buffer_release_callback().
	   buffer_release_callback() is more suitable point to delete or reuse buffer instead of frame_callback().
	   But we remain this callback because buffer_release_callback() works only when frame_callback() is activated.*/
	TPL_IGNORE(data);
	TPL_IGNORE(time);

	wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_listener = {
	__cb_client_frame_callback
};

static void
__cb_client_buffer_release_callback(void *data, struct wl_proxy *proxy)
{
	tpl_wayland_egl_surface_t *wayland_egl_surface = NULL;
	tpl_wayland_egl_buffer_t *wayland_egl_buffer = NULL;
	tbm_surface_h tbm_surface = NULL;

	TPL_ASSERT(data);

	tbm_surface = (tbm_surface_h) data;

	wayland_egl_buffer =
		__tpl_wayland_egl_get_wayland_buffer_from_tbm_surface(tbm_surface);

	if (wayland_egl_buffer) {
		wayland_egl_surface = wayland_egl_buffer->wayland_egl_surface;

		tbm_surface_internal_unref(tbm_surface);

		tbm_surface_queue_release(wayland_egl_surface->tbm_queue, tbm_surface);
	}
}

static const struct wl_buffer_listener buffer_release_listener = {
	(void *)__cb_client_buffer_release_callback,
};

static void
__cb_client_window_resize_callback(struct wl_egl_window *wl_egl_window,
				   void *private)
{
	TPL_ASSERT(private);
	TPL_ASSERT(wl_egl_window);

	int width, height;
	tpl_surface_t *surface = (tpl_surface_t *)private;
	tpl_wayland_egl_surface_t *wayland_egl_surface = (tpl_wayland_egl_surface_t *)
			surface->backend.data;

	width = wl_egl_window->width;
	height = wl_egl_window->height;

	/* Check whether the surface was resized by wayland_egl */
	if ((width != tbm_surface_queue_get_width(wayland_egl_surface->tbm_queue))
	    || (height != tbm_surface_queue_get_height(wayland_egl_surface->tbm_queue)))
		wayland_egl_surface->resized = TPL_TRUE;
}