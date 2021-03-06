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
#include <poll.h>

#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <tbm_surface_queue.h>
#include <wayland-tbm-client.h>
#include <wayland-tbm-server.h>
#include <tdm_client.h>
#include "protocol/tizen-surface-client.h"

/* In wayland, application and compositor create its own drawing buffers. Recommend size is more than 2. */
#define CLIENT_QUEUE_SIZE 3

typedef struct _tpl_wayland_egl_display tpl_wayland_egl_display_t;
typedef struct _tpl_wayland_egl_surface tpl_wayland_egl_surface_t;
typedef struct _tpl_wayland_egl_buffer tpl_wayland_egl_buffer_t;

struct _tpl_wayland_egl_display {
	struct wayland_tbm_client *wl_tbm_client;
	struct wl_proxy *wl_tbm; /* wayland_tbm_client proxy */
	tdm_client *tdm_client;
	struct wl_display *wl_dpy;
	struct wl_event_queue *wl_tbm_event_queue;
	struct tizen_surface_shm *tizen_surface_shm; /* used for surface buffer_flush */
};

struct _tpl_wayland_egl_surface {
	tpl_object_t base;
	tbm_surface_queue_h tbm_queue;
	tbm_surface_h current_buffer;
	tpl_bool_t resized;
	tpl_bool_t reset; /* TRUE if queue reseted by external  */
	tdm_client_vblank *tdm_vblank; /* vblank object for each wl_surface */
	tpl_bool_t vblank_done;
	tpl_list_t *attached_buffers; /* list for tracking [ACQ]~[REL] buffers */
	tpl_list_t *dequeued_buffers; /* list for tracking [DEQ]~[ENQ] buffers */
	struct tizen_surface_shm_flusher *tizen_surface_shm_flusher; /* wl_proxy for buffer flush */
};

struct _tpl_wayland_egl_buffer {
	tpl_wayland_egl_display_t *display;
	tpl_wayland_egl_surface_t *wayland_egl_surface;
	tbm_bo bo;
	tpl_bool_t reset; /* TRUE if queue reseted by external */
	struct wl_proxy *wl_proxy; /* wl_buffer proxy */
};

static const struct wl_callback_listener sync_listener;
static const struct wl_buffer_listener buffer_release_listener;

static int tpl_wayland_egl_buffer_key;
#define KEY_tpl_wayland_egl_buffer  (unsigned long)(&tpl_wayland_egl_buffer_key)

static void
__tpl_wayland_egl_display_buffer_flusher_init(
	tpl_wayland_egl_display_t *wayland_egl_display);
static void
__tpl_wayland_egl_display_buffer_flusher_fini(
	tpl_wayland_egl_display_t *wayland_egl_display);
static void
__tpl_wayland_egl_surface_buffer_flusher_init(tpl_surface_t *surface);
static void
__tpl_wayland_egl_surface_buffer_flusher_fini(tpl_surface_t *surface);
static void
__tpl_wayland_egl_buffer_free(tpl_wayland_egl_buffer_t *wayland_egl_buffer);

static TPL_INLINE tpl_wayland_egl_buffer_t *
__tpl_wayland_egl_get_wayland_buffer_from_tbm_surface(tbm_surface_h surface)
{
	tpl_wayland_egl_buffer_t *buf = NULL;

	if (!tbm_surface_internal_is_valid(surface))
		return NULL;

	tbm_surface_internal_get_user_data(surface, KEY_tpl_wayland_egl_buffer,
									   (void **)&buf);

	return buf;
}

static TPL_INLINE void
__tpl_wayland_egl_set_wayland_buffer_to_tbm_surface(tbm_surface_h surface,
		tpl_wayland_egl_buffer_t *buf)
{
	tbm_surface_internal_add_user_data(surface, KEY_tpl_wayland_egl_buffer,
									   (tbm_data_free)__tpl_wayland_egl_buffer_free);

	tbm_surface_internal_set_user_data(surface, KEY_tpl_wayland_egl_buffer,
									   buf);
}

static TPL_INLINE tpl_bool_t
__tpl_wayland_egl_display_is_wl_display(tpl_handle_t native_dpy)
{
	TPL_ASSERT(native_dpy);

	struct wl_interface *wl_egl_native_dpy = *(void **) native_dpy;

	/* MAGIC CHECK: A native display handle is a wl_display if the de-referenced first value
	   is a memory address pointing the structure of wl_display_interface. */
	if (wl_egl_native_dpy == &wl_display_interface)
		return TPL_TRUE;

	if (strncmp(wl_egl_native_dpy->name, wl_display_interface.name,
				strlen(wl_display_interface.name)) == 0) {
		return TPL_TRUE;
	}

	return TPL_FALSE;
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
		return TPL_ERROR_OUT_OF_MEMORY;
	}

	display->backend.data = wayland_egl_display;
	display->bufmgr_fd = -1;

	if (__tpl_wayland_egl_display_is_wl_display(display->native_handle)) {
		tdm_error tdm_err = 0;
		struct wl_display *wl_dpy =
			(struct wl_display *)display->native_handle;
		wayland_egl_display->wl_tbm_client =
			wayland_tbm_client_init((struct wl_display *) wl_dpy);
		char *env = getenv("TPL_WAIT_VBLANK");

		if (!wayland_egl_display->wl_tbm_client) {
			TPL_ERR("Wayland TBM initialization failed!");
			goto free_wl_display;
		}

		wayland_egl_display->wl_tbm =
			(struct wl_proxy *)wayland_tbm_client_get_wl_tbm(
				wayland_egl_display->wl_tbm_client);
		if (!wayland_egl_display->wl_tbm) {
			TPL_ERR("Failed to get wl_tbm from wayland_tbm_client.");
			goto free_wl_display;
		}

		wayland_egl_display->wl_tbm_event_queue =
			wl_display_create_queue(wl_dpy);
		if (!wayland_egl_display->wl_tbm_event_queue) {
			TPL_ERR("Failed to create wl_event_queue.");
			goto free_wl_display;
		}

		wl_proxy_set_queue(wayland_egl_display->wl_tbm,
						   wayland_egl_display->wl_tbm_event_queue);

		if (env == NULL || atoi(env)) {
			TPL_LOG_B("WL_EGL", "[INIT] ENABLE wait vblank.");
			wayland_egl_display->tdm_client = tdm_client_create(&tdm_err);
			if (!wayland_egl_display->tdm_client) {
				TPL_ERR("tdm client initialization failed! tdm_err=%d", tdm_err);
				goto free_wl_display;
			}
		} else {
			TPL_LOG_B("WL_EGL", "[INIT] DISABLE wait vblank.");
			wayland_egl_display->tdm_client = NULL;
		}

		wayland_egl_display->wl_dpy = wl_dpy;
		__tpl_wayland_egl_display_buffer_flusher_init(wayland_egl_display);

	} else {
		TPL_ERR("Invalid native handle for display.");
		goto free_wl_display;
	}

	TPL_LOG_B("WL_EGL", "[INIT] tpl_wayland_egl_display_t(%p) wl_tbm_client(%p)",
			  wayland_egl_display, wayland_egl_display->wl_tbm_client);

	return TPL_ERROR_NONE;

free_wl_display:
	if (wayland_egl_display->tdm_client)
		tdm_client_destroy(wayland_egl_display->tdm_client);
	if ((wayland_egl_display->wl_tbm) && (wayland_egl_display->wl_tbm_event_queue))
		wl_proxy_set_queue(wayland_egl_display->wl_tbm, NULL);
	if (wayland_egl_display->wl_tbm_event_queue)
		wl_event_queue_destroy(wayland_egl_display->wl_tbm_event_queue);
	if (wayland_egl_display->wl_tbm_client)
		wayland_tbm_client_deinit(wayland_egl_display->wl_tbm_client);

	wayland_egl_display->wl_tbm_event_queue = NULL;
	wayland_egl_display->wl_tbm_client = NULL;
	wayland_egl_display->tdm_client = NULL;
	wayland_egl_display->wl_tbm = NULL;
	wayland_egl_display->wl_dpy = NULL;

	free(wayland_egl_display);
	display->backend.data = NULL;
	return TPL_ERROR_INVALID_OPERATION;
}

static void
__tpl_wayland_egl_display_fini(tpl_display_t *display)
{
	tpl_wayland_egl_display_t *wayland_egl_display;

	TPL_ASSERT(display);

	wayland_egl_display = (tpl_wayland_egl_display_t *)display->backend.data;
	if (wayland_egl_display) {
		TPL_LOG_B("WL_EGL", "[FINI] tpl_wayland_egl_display_t(%p) wl_tbm_client(%p)",
				  wayland_egl_display, wayland_egl_display->wl_tbm_client);

		__tpl_wayland_egl_display_buffer_flusher_fini(wayland_egl_display);

		if (wayland_egl_display->tdm_client)
			tdm_client_destroy(wayland_egl_display->tdm_client);

		if ((wayland_egl_display->wl_tbm) && (wayland_egl_display->wl_tbm_event_queue))
			wl_proxy_set_queue(wayland_egl_display->wl_tbm, NULL);

		if (wayland_egl_display->wl_tbm_event_queue)
			wl_event_queue_destroy(wayland_egl_display->wl_tbm_event_queue);

		if (wayland_egl_display->wl_tbm_client)
			wayland_tbm_client_deinit(wayland_egl_display->wl_tbm_client);

		wayland_egl_display->wl_tbm_event_queue = NULL;
		wayland_egl_display->wl_tbm_client = NULL;
		wayland_egl_display->tdm_client = NULL;
		wayland_egl_display->wl_tbm = NULL;
		wayland_egl_display->wl_dpy = NULL;
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
	TPL_ASSERT(window);

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

static void
__cb_client_window_rotate_callback(struct wl_egl_window *wl_egl_window,
								   void *private);

static int
__cb_client_window_get_rotation_capability(struct wl_egl_window *wl_egl_window,
										   void *private);

static TPL_INLINE void
__tpl_wayland_egl_buffer_set_reset_flag(tpl_list_t *tracking_list)
{
	tpl_list_node_t *node = __tpl_list_get_front_node(tracking_list);

	while (node) {
		tbm_surface_h tbm_surface =
				(tbm_surface_h)__tpl_list_node_get_data(node);

		if (tbm_surface) {
			tpl_wayland_egl_buffer_t *wayland_egl_buffer =
				__tpl_wayland_egl_get_wayland_buffer_from_tbm_surface(tbm_surface);

			if (wayland_egl_buffer)
				wayland_egl_buffer->reset = TPL_TRUE;
		}

		node = __tpl_list_node_next(node);
	}
}

static void
__cb_tbm_surface_queue_reset_callback(tbm_surface_queue_h surface_queue,
									  void *data)
{
	tpl_surface_t *surface = NULL;
	tpl_wayland_egl_surface_t *wayland_egl_surface = NULL;

	surface = (tpl_surface_t *)data;
	TPL_CHECK_ON_NULL_RETURN(surface);

	wayland_egl_surface = (tpl_wayland_egl_surface_t *)surface->backend.data;
	TPL_CHECK_ON_NULL_RETURN(wayland_egl_surface);

	TPL_LOG_B("WL_EGL",
			  "[QUEUE_RESET_CB] tpl_wayland_egl_surface_t(%p) surface_queue(%p)",
			  data, surface_queue);

	TPL_OBJECT_LOCK(&wayland_egl_surface->base);

	wayland_egl_surface->reset = TPL_TRUE;

	/* Set the reset flag of the buffers which attached but not released to TPL_TRUE. */
	__tpl_wayland_egl_buffer_set_reset_flag(wayland_egl_surface->attached_buffers);

	/* Set the reset flag of the buffers which dequeued but not enqueued to TPL_TRUE. */
	__tpl_wayland_egl_buffer_set_reset_flag(wayland_egl_surface->dequeued_buffers);

	TPL_OBJECT_UNLOCK(&wayland_egl_surface->base);

	if (surface->reset_cb)
		surface->reset_cb(surface->reset_data);
}

static tpl_result_t
__tpl_wayland_egl_surface_create_vblank(tpl_wayland_egl_surface_t
										*wayland_egl_surface,
										tdm_client *tdm_client)
{
	tdm_client_output *tdm_output = NULL;
	tdm_error tdm_err_ret;

	TPL_ASSERT(wayland_egl_surface);
	TPL_ASSERT(tdm_client);

	tdm_output = tdm_client_get_output(tdm_client, "primary", &tdm_err_ret);
	if (!tdm_output) {
		TPL_ERR("Failed to get tdm client output. tdm_err(%d)", tdm_err_ret);
		return TPL_ERROR_INVALID_OPERATION;
	}

	wayland_egl_surface->tdm_vblank =
		tdm_client_output_create_vblank(tdm_output, &tdm_err_ret);
	if (!wayland_egl_surface->tdm_vblank) {
		TPL_ERR("Failed to create tdm vblank object. tdm_err(%d)", tdm_err_ret);
		return TPL_ERROR_INVALID_OPERATION;
	}

	tdm_client_vblank_set_enable_fake(wayland_egl_surface->tdm_vblank, 1);
	tdm_client_vblank_set_sync(wayland_egl_surface->tdm_vblank, 0);

	TPL_LOG_B("WL_EGL",
			  "[TDM_VBLANK_INIT] tpl_wayland_egl_surface_t(%p) tdm_vblank(%p)",
			  wayland_egl_surface, wayland_egl_surface->tdm_vblank);

	return TPL_ERROR_NONE;
}

static tpl_result_t
__tpl_wayland_egl_surface_init(tpl_surface_t *surface)
{
	tpl_wayland_egl_display_t *wayland_egl_display;
	tpl_wayland_egl_surface_t *wayland_egl_surface;
	struct wl_egl_window *wl_egl_window;

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->display);
	TPL_ASSERT(surface->type == TPL_SURFACE_TYPE_WINDOW);
	TPL_ASSERT(surface->native_handle);

	wayland_egl_display =
		(tpl_wayland_egl_display_t *)surface->display->backend.data;
	wl_egl_window = (struct wl_egl_window *)surface->native_handle;

	wayland_egl_surface = (tpl_wayland_egl_surface_t *) calloc(1,
						  sizeof(tpl_wayland_egl_surface_t));
	if (!wayland_egl_surface) {
		TPL_ERR("Failed to allocate memory for new tpl_wayland_egl_surface_t.");
		return TPL_ERROR_OUT_OF_MEMORY;
	}

	if (__tpl_object_init(&wayland_egl_surface->base, TPL_OBJECT_SURFACE,
						  NULL) != TPL_ERROR_NONE) {
		TPL_ERR("Failed to initialize backend surface's base class!");
		goto tpl_object_init_fail;
	}

	surface->backend.data = (void *)wayland_egl_surface;
	wayland_egl_surface->tbm_queue = NULL;
	wayland_egl_surface->resized = TPL_FALSE;
	wayland_egl_surface->reset = TPL_FALSE;
	wayland_egl_surface->vblank_done = TPL_TRUE;
	wayland_egl_surface->current_buffer = NULL;

	wayland_egl_surface->attached_buffers = __tpl_list_alloc();
	if (!wayland_egl_surface->attached_buffers) {
		TPL_ERR("Failed to allocate attached buffers tracking lists.");
		goto alloc_attached_buffers_fail;
	}

	wayland_egl_surface->dequeued_buffers = __tpl_list_alloc();
	if (!wayland_egl_surface->dequeued_buffers) {
		TPL_ERR("Failed to allocate dequeue buffers tracking lists.");
		goto alloc_dequeue_buffers_fail;
	}

	if (wl_egl_window->surface) {
		wayland_egl_surface->tbm_queue = wayland_tbm_client_create_surface_queue(
											 wayland_egl_display->wl_tbm_client,
											 wl_egl_window->surface,
											 CLIENT_QUEUE_SIZE,
											 wl_egl_window->width,
											 wl_egl_window->height,
											 TBM_FORMAT_ARGB8888);
	} else
		/*Why wl_surface is NULL ?*/
		wayland_egl_surface->tbm_queue = tbm_surface_queue_sequence_create(
											 CLIENT_QUEUE_SIZE,
											 wl_egl_window->width,
											 wl_egl_window->height,
											 TBM_FORMAT_ARGB8888,
											 0);

	if (!wayland_egl_surface->tbm_queue) {
		TPL_ERR("TBM surface queue creation failed!");
		goto queue_create_fail;
	}

	/* Set reset_callback to tbm_queue */
	if (tbm_surface_queue_add_reset_cb(wayland_egl_surface->tbm_queue,
				   __cb_tbm_surface_queue_reset_callback,
				   (void *)surface)) {
		TPL_ERR("TBM surface queue add reset cb failed!");
		goto add_reset_cb_fail;
	}

	surface->width = wl_egl_window->width;
	surface->height = wl_egl_window->height;
	surface->rotation = wl_egl_window->rotation;
	surface->rotation_capability = TPL_FALSE;

	wl_egl_window->private = surface;
	wl_egl_window->resize_callback = (void *)__cb_client_window_resize_callback;
	wl_egl_window->rotate_callback = (void *)__cb_client_window_rotate_callback;
	wl_egl_window->get_rotation_capability = (void *)
			__cb_client_window_get_rotation_capability;

	/* tdm_vblank object decide to be maintained every tpl_wayland_egl_surface
	   for the case where the several surfaces is created in one display connection. */
	if (wayland_egl_display->tdm_client) {
		if (TPL_ERROR_NONE != __tpl_wayland_egl_surface_create_vblank(
					wayland_egl_surface,
					wayland_egl_display->tdm_client)) {
			TPL_ERR("TBM surface create vblank failed!");
			goto create_vblank_fail;
		}
	}

	__tpl_wayland_egl_surface_buffer_flusher_init(surface);

	TPL_LOG_B("WL_EGL",
			  "[INIT] tpl_surface_t(%p) tpl_wayland_egl_surface_t(%p) tbm_queue(%p)",
			  surface, wayland_egl_surface,
			  wayland_egl_surface->tbm_queue);
	TPL_LOG_B("WL_EGL",
			  "[INIT] tpl_wayland_egl_surface_t(%p) wl_egl_window(%p) (%dx%d)",
			  wayland_egl_surface, wl_egl_window, surface->width, surface->height);

	return TPL_ERROR_NONE;

create_vblank_fail:
	tbm_surface_queue_remove_reset_cb(wayland_egl_surface->tbm_queue,
					  __cb_tbm_surface_queue_reset_callback,
					  (void *)surface);
add_reset_cb_fail:
	tbm_surface_queue_destroy(wayland_egl_surface->tbm_queue);
queue_create_fail:
	__tpl_list_free(wayland_egl_surface->attached_buffers, NULL);
alloc_dequeue_buffers_fail:
	__tpl_list_free(wayland_egl_surface->dequeued_buffers, NULL);
alloc_attached_buffers_fail:
	__tpl_object_fini(&wayland_egl_surface->base);
tpl_object_init_fail:
	free(wayland_egl_surface);
	surface->backend.data = NULL;
	return TPL_ERROR_INVALID_OPERATION;
}

static void
__tpl_wayland_egl_surface_fini(tpl_surface_t *surface)
{
	tpl_wayland_egl_surface_t *wayland_egl_surface = NULL;
	tpl_wayland_egl_display_t *wayland_egl_display = NULL;

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->display);

	wayland_egl_surface = (tpl_wayland_egl_surface_t *) surface->backend.data;
	TPL_CHECK_ON_NULL_RETURN(wayland_egl_surface);

	wayland_egl_display = (tpl_wayland_egl_display_t *)
						  surface->display->backend.data;
	TPL_CHECK_ON_NULL_RETURN(wayland_egl_display);

	if (surface->type == TPL_SURFACE_TYPE_WINDOW) {
		struct wl_egl_window *wl_egl_window = (struct wl_egl_window *)
											  surface->native_handle;

		TPL_ASSERT(wl_egl_window);
		/* TPL_ASSERT(wl_egl_window->surface); */ /* to be enabled once evas/gl patch is in place */

		wl_egl_window->private = NULL;

		wl_display_flush(wayland_egl_display->wl_dpy);
		wl_display_dispatch_queue_pending(wayland_egl_display->wl_dpy,
										  wayland_egl_display->wl_tbm_event_queue);

		if (wayland_egl_surface->tdm_vblank) {
			TPL_LOG_B("WL_EGL",
					  "[TDM_VBLANK_FINI] tpl_wayland_egl_surface_t(%p) tdm_vblank(%p)",
					  wayland_egl_surface, wayland_egl_surface->tdm_vblank);
			tdm_client_vblank_destroy(wayland_egl_surface->tdm_vblank);
		}

		TPL_LOG_B("WL_EGL",
				  "[FINI] tpl_wayland_egl_surface_t(%p) wl_egl_window(%p) tbm_queue(%p)",
				  wayland_egl_surface, wl_egl_window, wayland_egl_surface->tbm_queue);
		tbm_surface_queue_destroy(wayland_egl_surface->tbm_queue);
		wayland_egl_surface->tbm_queue = NULL;

		__tpl_wayland_egl_surface_buffer_flusher_fini(surface);
	}

	/* When surface is destroyed, unreference tbm_surface which tracked by
	 * the list of attached_buffers in order to free the created resources.
	 * (tpl_wayland_egl_buffer_t or wl_buffer) */
	if (wayland_egl_surface->attached_buffers) {
		TPL_OBJECT_LOCK(&wayland_egl_surface->base);
		while (!__tpl_list_is_empty(wayland_egl_surface->attached_buffers)) {
			tbm_surface_h tbm_surface =
				__tpl_list_pop_front(wayland_egl_surface->attached_buffers, NULL);
			tbm_surface_internal_unref(tbm_surface);
		}

		__tpl_list_free(wayland_egl_surface->attached_buffers, NULL);
		wayland_egl_surface->attached_buffers = NULL;
		TPL_OBJECT_UNLOCK(&wayland_egl_surface->base);
	}

	/* the list of dequeued_buffers just does deletion */
	if (wayland_egl_surface->dequeued_buffers) {
		TPL_OBJECT_LOCK(&wayland_egl_surface->base);
		__tpl_list_free(wayland_egl_surface->dequeued_buffers, NULL);
		wayland_egl_surface->dequeued_buffers = NULL;
		TPL_OBJECT_UNLOCK(&wayland_egl_surface->base);
	}

	__tpl_object_fini(&wayland_egl_surface->base);
	free(wayland_egl_surface);
	surface->backend.data = NULL;
}

static void
__tpl_wayland_egl_surface_wait_vblank(tpl_surface_t *surface)
{
	tdm_error tdm_err = 0;
	tpl_wayland_egl_display_t *wayland_egl_display =
		(tpl_wayland_egl_display_t *)surface->display->backend.data;
	tpl_wayland_egl_surface_t *wayland_egl_surface =
		(tpl_wayland_egl_surface_t *)surface->backend.data;

	TPL_OBJECT_UNLOCK(surface);
	do {
		tdm_err = tdm_client_handle_events(wayland_egl_display->tdm_client);

		if (tdm_err != TDM_ERROR_NONE) {
			TPL_ERR("Failed to tdm_client_handle_events.");
			break;
		}

	} while (wayland_egl_surface->vblank_done == TPL_FALSE);
	TPL_OBJECT_LOCK(surface);
}

static void
__cb_tdm_client_wait_vblank(tdm_client_vblank *vblank, tdm_error error,
							unsigned int sequence, unsigned int tv_sec,
							unsigned int tv_usec, void *user_data)
{
	tpl_wayland_egl_surface_t *wayland_egl_surface =
		(tpl_wayland_egl_surface_t *)user_data;
	wayland_egl_surface->vblank_done = TPL_TRUE;
	TRACE_MARK("TDM_CLIENT_VBLACK");
}

static void
__tpl_wayland_egl_surface_commit(tpl_surface_t *surface,
								 tbm_surface_h tbm_surface,
								 int num_rects, const int *rects)
{
	tpl_wayland_egl_buffer_t *wayland_egl_buffer = NULL;
	struct wl_egl_window *wl_egl_window =
		(struct wl_egl_window *)surface->native_handle;
	tpl_wayland_egl_display_t *wayland_egl_display =
		(tpl_wayland_egl_display_t *) surface->display->backend.data;
	tpl_wayland_egl_surface_t *wayland_egl_surface =
		(tpl_wayland_egl_surface_t *) surface->backend.data;
	tdm_error tdm_err = 0;

	wayland_egl_buffer =
		__tpl_wayland_egl_get_wayland_buffer_from_tbm_surface(tbm_surface);
	TPL_ASSERT(wayland_egl_buffer);

	TRACE_ASYNC_END((int)wayland_egl_buffer, "[DEQ]~[ENQ] BO_NAME:%d",
					tbm_bo_export(wayland_egl_buffer->bo));

	TPL_IMAGE_DUMP(tbm_surface, surface->width, surface->height);

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
			/* The rectangles are specified relative to the bottom-left of the
			 * GL surface. So, these rectanglesd has to be converted to
			 * WINDOW(Top-left) coord like below.
			 * y = [WINDOW.HEIGHT] - (RECT.Y + RECT.HEIGHT) */
			int inverted_y =
				wl_egl_window->height - (rects[i * 4 + 1] + rects[i * 4 + 3]);
			wl_surface_damage(wl_egl_window->surface,
							  rects[i * 4 + 0], inverted_y,
							  rects[i * 4 + 2], rects[i * 4 + 3]);
		}
	}

	wl_surface_commit(wl_egl_window->surface);

	wl_display_flush(wayland_egl_display->wl_dpy);

	TPL_LOG_B("WL_EGL",
			  "[COMMIT] wl_surface(%p) wl_egl_window(%p)(%dx%d) wl_buffer(%p)",
			  wl_egl_window->surface, wl_egl_window,
			  wl_egl_window->width, wl_egl_window->height, wayland_egl_buffer->wl_proxy);

	if (wayland_egl_surface->attached_buffers) {
		TPL_OBJECT_LOCK(&wayland_egl_surface->base);
		/* Start tracking of this tbm_surface until release_cb called. */
		__tpl_list_push_back(wayland_egl_surface->attached_buffers,
							 (void *)tbm_surface);
		TPL_OBJECT_UNLOCK(&wayland_egl_surface->base);
	}

	/* TPL_WAIT_VBLANK = 1 */
	if (wayland_egl_display->tdm_client) {
		tdm_err = tdm_client_vblank_wait(wayland_egl_surface->tdm_vblank,
										 1, /* interval */
										 __cb_tdm_client_wait_vblank, /* handler */
										 surface->backend.data); /* user_data */

		if (tdm_err == TDM_ERROR_NONE)
			wayland_egl_surface->vblank_done = TPL_FALSE;
		else
			TPL_ERR("Failed to tdm_client_wait_vblank. error:%d", tdm_err);
	}

	TRACE_ASYNC_BEGIN((int)tbm_surface, "[COMMIT ~ RELEASE_CB] BO_NAME:%d",
					  tbm_bo_export(wayland_egl_buffer->bo));
}

static tpl_result_t
__tpl_wayland_egl_surface_enqueue_buffer(tpl_surface_t *surface,
		tbm_surface_h tbm_surface,
		int num_rects, const int *rects, tbm_fd sync_fence)
{
	TPL_ASSERT(surface);
	TPL_ASSERT(surface->display);
	TPL_ASSERT(tbm_surface);
	TPL_OBJECT_CHECK_RETURN(surface, TPL_ERROR_INVALID_PARAMETER);

	tpl_wayland_egl_surface_t *wayland_egl_surface =
		(tpl_wayland_egl_surface_t *) surface->backend.data;
	tpl_wayland_egl_buffer_t *wayland_egl_buffer = NULL;
	tbm_surface_queue_error_e tsq_err;

	if (!wayland_egl_surface) return TPL_ERROR_INVALID_PARAMETER;

	if (!tbm_surface_internal_is_valid(tbm_surface)) {
		TPL_ERR("Failed to enqueue tbm_surface(%p) Invalid value.",
				tbm_surface);
		return TPL_ERROR_INVALID_PARAMETER;
	}

	TRACE_MARK("[ENQ] BO_NAME:%d",
			   tbm_bo_export(tbm_surface_internal_get_bo(tbm_surface, 0)));

	TPL_LOG_B("WL_EGL",
			  "[ENQ] tpl_wayland_egl_surface_t(%p) tbm_queue(%p) tbm_surface(%p) bo(%d)",
			  wayland_egl_surface, wayland_egl_surface->tbm_queue,
			  tbm_surface, tbm_bo_export(tbm_surface_internal_get_bo(tbm_surface, 0)));

	if (wayland_egl_surface->vblank_done == TPL_FALSE)
		__tpl_wayland_egl_surface_wait_vblank(surface);

	if (sync_fence != -1) {
		tbm_sync_fence_wait(sync_fence, -1);
		close(sync_fence);
	}

	if (wayland_egl_surface->dequeued_buffers) {
		TPL_OBJECT_LOCK(&wayland_egl_surface->base);
		/* Stop tracking of this render_done tbm_surface. */
		__tpl_list_remove_data(wayland_egl_surface->dequeued_buffers,
							   (void *)tbm_surface, TPL_FIRST, NULL);
		TPL_OBJECT_UNLOCK(&wayland_egl_surface->base);
	}

	wayland_egl_buffer =
		__tpl_wayland_egl_get_wayland_buffer_from_tbm_surface(tbm_surface);

	if (!wayland_egl_buffer) return TPL_ERROR_INVALID_PARAMETER;

	if (wayland_egl_buffer->reset) {
		/*
		 * When tbm_surface_queue being reset for receiving
		 * scan-out buffer or resized buffer
		 * tbm_surface_queue_enqueue will return error.
		 * This error condition leads to skip frame.
		 *
		 * tbm_surface received from argument this function,
		 * may be rendered done. So this tbm_surface is better to do
		 * commit forcibly without handling queue in order to prevent
		 * frame skipping.
		 */
		__tpl_wayland_egl_surface_commit(surface, tbm_surface,
										 num_rects, rects);
		return TPL_ERROR_NONE;
	}

	tsq_err = tbm_surface_queue_enqueue(wayland_egl_surface->tbm_queue,
										tbm_surface);
	if (tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE) {
		/*
		 * If tbm_surface_queue has not been reset, tbm_surface_queue_enqueue
		 * will return ERROR_NONE. Otherwise, queue has been reset
		 * this tbm_surface may have only one ref_count. So we need to
		 * unreference this tbm_surface after getting ERROR_NONE result from
		 * tbm_surface_queue_enqueue in order to prevent destruction.
		 */
		tbm_surface_internal_unref(tbm_surface);
	} else {
		/*
		 * If tbm_surface is valid but it is not tracked by tbm_surface_queue,
		 * tbm_surface_queue_enqueue will return below value.
		 * TBM_SURFACE_QUEUE_ERROR_UNKNOWN_SURFACE
		 * It means tbm_surface_queue has been reset before client try
		 * to enqueue this tbm_surface.
		 * We should commit this buffer to display to assure the latest frame.
		 *
		 * In enlightenment(E20) of TIZEN platform, depending on
		 * some situation(Activate, Deactivate), the compositor may or may not
		 * display the last forcibly commited buffer in this way.
		 *
		 * In this situation, the compositor's display policy may vary from
		 * server to server.
		 */
		if (tsq_err == TBM_SURFACE_QUEUE_ERROR_UNKNOWN_SURFACE) {
			__tpl_wayland_egl_surface_commit(surface, tbm_surface,
											 num_rects, rects);
			return TPL_ERROR_NONE;
		}

		TPL_ERR("Failed to enqeueue tbm_surface(%p). | tsq_err = %d",
				tbm_surface, tsq_err);
		return TPL_ERROR_INVALID_OPERATION;
	}

	tsq_err = tbm_surface_queue_acquire(wayland_egl_surface->tbm_queue,
										&tbm_surface);
	if (tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE) {
		tbm_surface_internal_ref(tbm_surface);
	} else {
		TPL_ERR("Failed to acquire tbm_surface(%p). | tsq_err = %d",
				tbm_surface, tsq_err);
		return TPL_ERROR_INVALID_OPERATION;
	}

	__tpl_wayland_egl_surface_commit(surface, tbm_surface, num_rects, rects);

	return TPL_ERROR_NONE;
}

static tpl_bool_t
__tpl_wayland_egl_surface_validate(tpl_surface_t *surface)
{
	tpl_bool_t retval = TPL_TRUE;

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->backend.data);

	tpl_wayland_egl_surface_t *wayland_egl_surface =
		(tpl_wayland_egl_surface_t *)surface->backend.data;

	retval = !(wayland_egl_surface->resized || wayland_egl_surface->reset);

	return retval;
}

static tpl_result_t
__tpl_wayland_egl_surface_wait_dequeuable(tpl_surface_t *surface)
{
	tpl_wayland_egl_display_t *wayland_egl_display = NULL;
	tpl_wayland_egl_surface_t *wayland_egl_surface = NULL;
	tpl_result_t ret = TPL_ERROR_NONE;

	wayland_egl_display = (tpl_wayland_egl_display_t *)
						  surface->display->backend.data;
	wayland_egl_surface = (tpl_wayland_egl_surface_t *)surface->backend.data;

	wl_display_dispatch_queue_pending(wayland_egl_display->wl_dpy,
									  wayland_egl_display->wl_tbm_event_queue);

	if (tbm_surface_queue_can_dequeue(wayland_egl_surface->tbm_queue, 0))
		return TPL_ERROR_NONE;

	TRACE_BEGIN("WAITING FOR DEQUEUEABLE");
	TPL_OBJECT_UNLOCK(surface);

	/* Dispatching "wayland_egl_display->wl_tbm_event_queue" handles
	 * wl_buffer_release event, wl_tbm event, and wl_tbm_queue event.
	 *
	 * 1. wl_tbm proxy handles what received below wayland events.
	 *   - buffer_attached_with_id
	 *   - buffer_attached_with_fd
	 * 2. wl_tbm_queue handles what received below wayland events.
	 *    - active
	 *    - deactive
	 */
	while (tbm_surface_queue_can_dequeue(
				wayland_egl_surface->tbm_queue, 0) == 0) {
		/* Application sent all buffers to the server. Wait for server response. */
		if (wl_display_dispatch_queue(wayland_egl_display->wl_dpy,
									  wayland_egl_display->wl_tbm_event_queue) == -1) {
			int dpy_err;
			char buf[1024];
			strerror_r(errno, buf, sizeof(buf));

			TPL_ERR("falied to wl_display_dispatch_queue. error:%d(%s)", errno,
					buf);

			dpy_err = wl_display_get_error(wayland_egl_display->wl_dpy);
			if (dpy_err == EPROTO) {
				const struct wl_interface *err_interface;
				uint32_t err_proxy_id, err_code;
				err_code = wl_display_get_protocol_error(wayland_egl_display->wl_dpy,
														 &err_interface, &err_proxy_id);
				TPL_ERR("[Protocol Error] interface: %s, error_code: %d, proxy_id: %d",
						err_interface->name, err_code, err_proxy_id);
			}

			ret = TPL_ERROR_INVALID_OPERATION;
			break;
		}
	}
	TPL_OBJECT_LOCK(surface);
	TRACE_END();

	return ret;
}

static tbm_surface_h
__tpl_wayland_egl_surface_dequeue_buffer(tpl_surface_t *surface, uint64_t timeout_ns,
										 tbm_fd *sync_fence)
{
	TPL_ASSERT(surface);
	TPL_ASSERT(surface->backend.data);
	TPL_ASSERT(surface->display);
	TPL_ASSERT(surface->display->backend.data);
	TPL_OBJECT_CHECK_RETURN(surface, NULL);

	tbm_surface_h tbm_surface = NULL;
	tpl_wayland_egl_buffer_t *wayland_egl_buffer = NULL;
	tpl_wayland_egl_surface_t *wayland_egl_surface =
		(tpl_wayland_egl_surface_t *)surface->backend.data;
	tpl_wayland_egl_display_t *wayland_egl_display =
		(tpl_wayland_egl_display_t *)surface->display->backend.data;
	struct wl_proxy *wl_proxy = NULL;
	tbm_surface_queue_error_e tsq_err = 0;

	if (sync_fence)
		*sync_fence = -1;

	/* Check whether the surface was resized by wayland_egl */
	if (wayland_egl_surface->resized == TPL_TRUE) {
		struct wl_egl_window *wl_egl_window =
			(struct wl_egl_window *)surface->native_handle;
		int width, height, format;
		width = wl_egl_window->width;
		height = wl_egl_window->height;
		format = tbm_surface_queue_get_format(wayland_egl_surface->tbm_queue);

		tbm_surface_queue_reset(wayland_egl_surface->tbm_queue, width, height, format);
		surface->width = width;
		surface->height = height;

		wayland_egl_surface->resized = TPL_FALSE;
		wayland_egl_surface->reset = TPL_FALSE;
	}

	if (__tpl_wayland_egl_surface_wait_dequeuable(surface)) {
		TPL_ERR("Failed to wait dequeeable buffer");
		return NULL;
	}

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
		TRACE_MARK("[DEQ][REUSED]BO_NAME:%d", tbm_bo_export(wayland_egl_buffer->bo));
		TRACE_ASYNC_BEGIN((int)wayland_egl_buffer, "[DEQ]~[ENQ] BO_NAME:%d",
						  tbm_bo_export(wayland_egl_buffer->bo));
		TPL_LOG_B("WL_EGL",
				  "[DEQ][R] tpl_wayland_surface_t(%p) wl_buffer(%p) tbm_surface(%p) bo(%d)",
				  wayland_egl_surface,
				  wayland_egl_buffer->wl_proxy,
				  tbm_surface, tbm_bo_export(wayland_egl_buffer->bo));

		wayland_egl_buffer->reset = TPL_FALSE;
		wayland_egl_surface->reset = TPL_FALSE;

		if (wayland_egl_surface->dequeued_buffers) {
			TPL_OBJECT_LOCK(&wayland_egl_surface->base);
			/* Start tracking of this tbm_surface until enqueue */
			__tpl_list_push_back(wayland_egl_surface->dequeued_buffers,
								 (void *)tbm_surface);
			TPL_OBJECT_UNLOCK(&wayland_egl_surface->base);
		}

		return tbm_surface;
	}

	wayland_egl_buffer = (tpl_wayland_egl_buffer_t *) calloc(1,
						 sizeof(tpl_wayland_egl_buffer_t));
	if (!wayland_egl_buffer) {
		TPL_ERR("Mem alloc for wayland_egl_buffer failed!");
		tbm_surface_internal_unref(tbm_surface);
		return NULL;
	}

	wl_proxy =
		(struct wl_proxy *)wayland_tbm_client_create_buffer(
			wayland_egl_display->wl_tbm_client, tbm_surface);
	if (!wl_proxy) {
		TPL_ERR("Failed to create TBM client buffer!");
		tbm_surface_internal_unref(tbm_surface);
		free(wayland_egl_buffer);
		return NULL;
	}

	wl_buffer_add_listener((void *)wl_proxy, &buffer_release_listener,
						   tbm_surface);

	wl_display_flush(wayland_egl_display->wl_dpy);

	wayland_egl_buffer->display = wayland_egl_display;
	wayland_egl_buffer->wl_proxy = wl_proxy;
	wayland_egl_buffer->bo = tbm_surface_internal_get_bo(tbm_surface, 0);
	wayland_egl_buffer->wayland_egl_surface = wayland_egl_surface;

	/* reset flag is to check whether it is the buffer before tbm_surface_queue is reset or not. */
	wayland_egl_buffer->reset = TPL_FALSE;

	wayland_egl_surface->current_buffer = tbm_surface;
	wayland_egl_surface->reset = TPL_FALSE;

	__tpl_wayland_egl_set_wayland_buffer_to_tbm_surface(tbm_surface,
			wayland_egl_buffer);

	TRACE_MARK("[DEQ][NEW]BO_NAME:%d", tbm_bo_export(wayland_egl_buffer->bo));
	TRACE_ASYNC_BEGIN((int)wayland_egl_buffer, "[DEQ]~[ENQ] BO_NAME:%d",
					  tbm_bo_export(wayland_egl_buffer->bo));
	TPL_LOG_B("WL_EGL",
			  "[DEQ][N] tpl_wayland_egl_buffer_t(%p) wl_buffer(%p) tbm_surface(%p) bo(%d)",
			  wayland_egl_buffer, wayland_egl_buffer->wl_proxy, tbm_surface,
			  tbm_bo_export(wayland_egl_buffer->bo));

	if (wayland_egl_surface->dequeued_buffers) {
		TPL_OBJECT_LOCK(&wayland_egl_surface->base);
		__tpl_list_push_back(wayland_egl_surface->dequeued_buffers,
							 (void *)tbm_surface);
		TPL_OBJECT_UNLOCK(&wayland_egl_surface->base);
	}

	return tbm_surface;
}

static void
__tpl_wayland_egl_buffer_free(tpl_wayland_egl_buffer_t *wayland_egl_buffer)
{
	TPL_ASSERT(wayland_egl_buffer);
	TPL_ASSERT(wayland_egl_buffer->display);

	tpl_wayland_egl_display_t *wayland_egl_display = wayland_egl_buffer->display;

	TPL_LOG_B("WL_EGL", "[FREE] tpl_wayland_egl_buffer_t(%p) wl_buffer(%p)",
			  wayland_egl_buffer, wayland_egl_buffer->wl_proxy);
	wl_display_flush(wayland_egl_display->wl_dpy);

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
__cb_client_buffer_release_callback(void *data, struct wl_proxy *proxy)
{
	tbm_surface_h tbm_surface = NULL;

	TPL_ASSERT(data);

	tbm_surface = (tbm_surface_h) data;

	TRACE_ASYNC_END((int)tbm_surface, "[COMMIT ~ RELEASE_CB] BO_NAME:%d",
					tbm_bo_export(tbm_surface_internal_get_bo(tbm_surface, 0)));
	TPL_LOG_B("WL_EGL", "[RELEASE_CB] wl_buffer(%p) tbm_surface(%p) bo(%d)",
			  proxy, tbm_surface,
			  tbm_bo_export(tbm_surface_internal_get_bo(tbm_surface, 0)));

	if (tbm_surface_internal_is_valid(tbm_surface)) {
		tpl_wayland_egl_surface_t *wayland_egl_surface = NULL;
		tpl_wayland_egl_buffer_t *wayland_egl_buffer = NULL;

		wayland_egl_buffer =
			__tpl_wayland_egl_get_wayland_buffer_from_tbm_surface(tbm_surface);

		if (wayland_egl_buffer) {
			wayland_egl_surface = wayland_egl_buffer->wayland_egl_surface;

			if (wayland_egl_surface->attached_buffers) {
				TPL_OBJECT_LOCK(&wayland_egl_surface->base);
				/* Stop tracking of this released tbm_surface. */
				__tpl_list_remove_data(wayland_egl_surface->attached_buffers,
									   (void *)tbm_surface, TPL_FIRST, NULL);
				TPL_OBJECT_UNLOCK(&wayland_egl_surface->base);
			}
			/* If tbm_surface_queue was reset before release_cb called out,
			 * tbm_surface_queue_release doesn't have to be done. */
			if (wayland_egl_buffer->reset == TPL_FALSE)
				tbm_surface_queue_release(wayland_egl_surface->tbm_queue, tbm_surface);
		}

		tbm_surface_internal_unref(tbm_surface);
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

	int cur_w, cur_h, req_w, req_h;
	tpl_surface_t *surface = (tpl_surface_t *)private;
	tpl_wayland_egl_surface_t *wayland_egl_surface =
		(tpl_wayland_egl_surface_t *)surface->backend.data;

	cur_w = tbm_surface_queue_get_width(wayland_egl_surface->tbm_queue);
	cur_h = tbm_surface_queue_get_height(wayland_egl_surface->tbm_queue);
	req_w = wl_egl_window->width;
	req_h = wl_egl_window->height;

	TPL_LOG_B("WL_EGL", "[RESIZE_CB] wl_egl_window(%p) (%dx%d) -> (%dx%d)",
			wl_egl_window, cur_w, cur_h, req_w, req_h);

	/* Check whether the surface was resized by wayland_egl */
	if ((req_w != cur_w) || (req_h != cur_h))
		wayland_egl_surface->resized = TPL_TRUE;
}

static void
__cb_client_window_rotate_callback(struct wl_egl_window *wl_egl_window,
								   void *private)
{
	TPL_ASSERT(private);
	TPL_ASSERT(wl_egl_window);

	int rotation;
	tpl_surface_t *surface = (tpl_surface_t *)private;

	rotation = wl_egl_window->rotation;

	TPL_LOG_B("WL_EGL", "[ROTATE_CB] wl_egl_window(%p) (%d) -> (%d)",
			  wl_egl_window, surface->rotation, rotation);
	/* Check whether the surface was resized by wayland_egl */
	surface->rotation = rotation;
}

static int
__cb_client_window_get_rotation_capability(struct wl_egl_window *wl_egl_window,
										   void *private)
{
	int rotation_capability = WL_EGL_WINDOW_CAPABILITY_NONE;
	TPL_ASSERT(private);
	TPL_ASSERT(wl_egl_window);
	tpl_surface_t *surface = (tpl_surface_t *)private;
	if (TPL_TRUE == surface->rotation_capability)
		rotation_capability = WL_EGL_WINDOW_CAPABILITY_ROTATION_SUPPORTED;
	else
		rotation_capability = WL_EGL_WINDOW_CAPABILITY_ROTATION_UNSUPPORTED;

	return rotation_capability;
}


void
__cb_resistry_global_callback(void *data, struct wl_registry *wl_registry,
							  uint32_t name, const char *interface,
							  uint32_t version)
{
	tpl_wayland_egl_display_t *wayland_egl_display = data;

	if (!strcmp(interface, "tizen_surface_shm")) {
		wayland_egl_display->tizen_surface_shm =
			wl_registry_bind(wl_registry,
							 name,
							 &tizen_surface_shm_interface,
							 version);
	}
}

void
__cb_resistry_global_remove_callback(void *data,
									 struct wl_registry *wl_registry,
									 uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	__cb_resistry_global_callback,
	__cb_resistry_global_remove_callback
};

static void
__tpl_wayland_egl_display_buffer_flusher_init(
	tpl_wayland_egl_display_t *wayland_egl_display)
{
	struct wl_registry *registry = NULL;
	struct wl_event_queue *queue = NULL;
	int ret;

	queue = wl_display_create_queue(wayland_egl_display->wl_dpy);
	if (!queue) {
		TPL_ERR("Failed to create wl_queue");
		goto fini;
	}

	registry = wl_display_get_registry(wayland_egl_display->wl_dpy);
	if (!registry) {
		TPL_ERR("Failed to create wl_registry");
		goto fini;
	}

	wl_proxy_set_queue((struct wl_proxy *)registry, queue);
	if (wl_registry_add_listener(registry, &registry_listener,
								 wayland_egl_display)) {
		TPL_ERR("Failed to wl_registry_add_listener");
		goto fini;
	}

	ret = wl_display_roundtrip_queue(wayland_egl_display->wl_dpy, queue);
	if (ret == -1) {
		TPL_ERR("Failed to wl_display_roundtrip_queue ret:%d, err:%d", ret, errno);
		goto fini;
	}

	/* set tizen_surface_shm's queue as client's default queue */
	if (wayland_egl_display->tizen_surface_shm)
		wl_proxy_set_queue((struct wl_proxy *)wayland_egl_display->tizen_surface_shm,
						   NULL);

fini:
	if (queue)
		wl_event_queue_destroy(queue);
	if (registry)
		wl_registry_destroy(registry);
}

static void
__tpl_wayland_egl_display_buffer_flusher_fini(
	tpl_wayland_egl_display_t *wayland_egl_display)
{
	if (wayland_egl_display->tizen_surface_shm) {
		tizen_surface_shm_destroy(wayland_egl_display->tizen_surface_shm);
		wayland_egl_display->tizen_surface_shm = NULL;
	}
}

static void __cb_tizen_surface_shm_flusher_flush_callback(void *data,
		struct tizen_surface_shm_flusher *tizen_surface_shm_flusher)
{
	tpl_surface_t *surface = data;
	tpl_wayland_egl_surface_t *wayland_egl_surface;
	tpl_wayland_egl_display_t *wayland_egl_display;
	int ret;

	TPL_CHECK_ON_NULL_RETURN(surface);
	wayland_egl_surface = surface->backend.data;
	TPL_CHECK_ON_NULL_RETURN(wayland_egl_surface);
	TPL_CHECK_ON_NULL_RETURN(surface->display);
	wayland_egl_display = surface->display->backend.data;
	TPL_CHECK_ON_NULL_RETURN(wayland_egl_display);

	TPL_CHECK_ON_NULL_RETURN(wayland_egl_display->wl_dpy);
	TPL_CHECK_ON_NULL_RETURN(wayland_egl_display->wl_tbm_event_queue);
	TPL_CHECK_ON_NULL_RETURN(wayland_egl_surface->tbm_queue);

	TPL_LOG_B("WL_EGL", "[FLUSH_CB] tpl_wayland_egl_surface_t(%p)",
			  wayland_egl_surface);

	/*Fist distach panding queue for TPL
		- dispatch buffer-release
		- dispatch queue flush
	*/
	ret = wl_display_dispatch_queue_pending(wayland_egl_display->wl_dpy,
											wayland_egl_display->wl_tbm_event_queue);
	if (ret == -1) {
		TPL_ERR("Failed to wl_display_dispatch_queue_pending ret:%d, err:%d", ret,
				errno);
		return;
	}

	tbm_surface_queue_flush(wayland_egl_surface->tbm_queue);

	/* Only when client call tpl_surface_dequeue_buffer(), client can do
	 * unreference tbm_surface although there are release events in the event queue,
	 * After tbm_surface_queue_flush, queue has no tbm_surface, client can do
	 * unreference attached buffers using the list of attached_buffers.
	 * Then, client does not need to wait for release_callback to unreference
	 * attached buffer.
	 */
	if (wayland_egl_surface->attached_buffers) {
		TPL_OBJECT_LOCK(&wayland_egl_surface->base);
		while (!__tpl_list_is_empty(wayland_egl_surface->attached_buffers)) {
			tbm_surface_h tbm_surface =
				__tpl_list_pop_front(wayland_egl_surface->attached_buffers, NULL);
			tbm_surface_internal_unref(tbm_surface);
		}
		TPL_OBJECT_UNLOCK(&wayland_egl_surface->base);
	}
}

static const struct tizen_surface_shm_flusher_listener
tizen_surface_shm_flusher_listener = {
	__cb_tizen_surface_shm_flusher_flush_callback
};

static void
__tpl_wayland_egl_surface_buffer_flusher_init(tpl_surface_t *surface)
{
	tpl_wayland_egl_display_t *wayland_egl_display = surface->display->backend.data;
	tpl_wayland_egl_surface_t *wayland_egl_surface = surface->backend.data;
	struct wl_egl_window *wl_egl_window = (struct wl_egl_window *)
										  surface->native_handle;

	if (!wayland_egl_display->tizen_surface_shm)
		return;

	wayland_egl_surface->tizen_surface_shm_flusher =
		tizen_surface_shm_get_flusher(wayland_egl_display->tizen_surface_shm,
									  wl_egl_window->surface);
	tizen_surface_shm_flusher_add_listener(
		wayland_egl_surface->tizen_surface_shm_flusher,
		&tizen_surface_shm_flusher_listener, surface);
}

static void
__tpl_wayland_egl_surface_buffer_flusher_fini(tpl_surface_t *surface)
{
	tpl_wayland_egl_surface_t *wayland_egl_surface = surface->backend.data;

	if (wayland_egl_surface->tizen_surface_shm_flusher) {
		tizen_surface_shm_flusher_destroy(
			wayland_egl_surface->tizen_surface_shm_flusher);
		wayland_egl_surface->tizen_surface_shm_flusher = NULL;
	}
}
