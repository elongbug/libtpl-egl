#define inline __inline__
#include <wayland-client.h>
#undef inline

#include "tpl_internal.h"

#include <tbm_surface.h>
#include <tbm_surface_internal.h>
#include <tbm_surface_queue.h>
#include <wayland-tbm-client.h>

#include <tbm_sync.h>

#include "wayland-vulkan/wayland-vulkan-client-protocol.h"

#define CLIENT_QUEUE_SIZE 3

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

typedef struct _tpl_wayland_vk_wsi_display tpl_wayland_vk_wsi_display_t;
typedef struct _tpl_wayland_vk_wsi_surface tpl_wayland_vk_wsi_surface_t;
typedef struct _tpl_wayland_vk_wsi_buffer tpl_wayland_vk_wsi_buffer_t;

struct _tpl_wayland_vk_wsi_display {
	struct wayland_tbm_client *wl_tbm_client;
	struct {
		int min_buffer;
		int max_buffer;
		int present_modes;
	} surface_capabilities;
	struct wayland_vulkan *wl_vk_client;
};

struct _tpl_wayland_vk_wsi_surface {
	tbm_surface_queue_h tbm_queue;
	int buffer_count;

#if USE_WORKER_THREAD == 1
	/*
	 * TODO: it can move to libtbm
	 *		 libtbm already has free queue's pthread_cond and pthread_mutex
	 */
	pthread_mutex_t free_queue_mutex;
	pthread_cond_t free_queue_cond;

	/* tbm_surface list */
	tpl_list_t vblank_list;
	pthread_mutex_t vblank_list_mutex;

	tpl_bool_t vblank_done;

	tpl_worker_surface_t worker_surface;
#endif
	int present_mode;
};

struct _tpl_wayland_vk_wsi_buffer {
	tpl_display_t *display;
	struct wl_proxy *wl_proxy;
	tbm_fd sync_timeline;
	unsigned int sync_timestamp;

#if USE_WORKER_THREAD == 1
	tbm_fd wait_sync;
#endif
};

static const struct wl_registry_listener registry_listener;
static const struct wl_callback_listener sync_listener;
static const struct wl_callback_listener frame_listener;
static const struct wl_buffer_listener buffer_release_listener;

#define TPL_BUFFER_CACHE_MAX_ENTRIES 40

static int tpl_wayland_vk_wsi_buffer_key;
#define KEY_tpl_wayland_vk_wsi_buffer  (unsigned long)(&tpl_wayland_vk_wsi_buffer_key)

static void __tpl_wayland_vk_wsi_buffer_free(tpl_wayland_vk_wsi_buffer_t
		*wayland_vk_wsi_buffer);
static tpl_result_t __tpl_wayland_vk_wsi_surface_destroy_swapchain(
	tpl_surface_t *surface);

static TPL_INLINE tpl_wayland_vk_wsi_buffer_t *
__tpl_wayland_vk_wsi_get_wayland_buffer_from_tbm_surface(tbm_surface_h surface)
{
	tpl_wayland_vk_wsi_buffer_t *buf = NULL;

	if (!tbm_surface_internal_is_valid(surface))
		return NULL;

	tbm_surface_internal_get_user_data(surface, KEY_tpl_wayland_vk_wsi_buffer,
									   (void **)&buf);
	return buf;
}

static TPL_INLINE void
__tpl_wayland_vk_wsi_set_wayland_buffer_to_tbm_surface(tbm_surface_h surface,
		tpl_wayland_vk_wsi_buffer_t *buf)
{
	tbm_surface_internal_add_user_data(surface,
									   KEY_tpl_wayland_vk_wsi_buffer,
									   (tbm_data_free)__tpl_wayland_vk_wsi_buffer_free);
	tbm_surface_internal_set_user_data(surface,
									   KEY_tpl_wayland_vk_wsi_buffer, buf);
}

static TPL_INLINE tpl_bool_t
__tpl_wayland_vk_wsi_display_is_wl_display(tpl_handle_t native_dpy)
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
__tpl_wayland_vk_wsi_display_roundtrip(tpl_display_t *display)
{
	struct wl_display *wl_dpy;
	struct wl_callback *callback;
	int done = 0, ret = 0;

	TPL_ASSERT(display);
	TPL_ASSERT(display->native_handle);
	TPL_ASSERT(display->backend.data);

	wl_dpy = (struct wl_display *) display->native_handle;

	callback = wl_display_sync(wl_dpy);
	wl_callback_add_listener(callback, &sync_listener, &done);

	while (ret != -1 && !done) {
		ret = wl_display_dispatch(wl_dpy);
	}

	return ret;
}

static void
__tpl_wayland_vk_wsi_support_present_mode_listener(void *data,
												   struct wayland_vulkan *wayland_vulkan,
												   uint32_t mode)
{
	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display = data;

	switch (mode) {
		case WAYLAND_VULKAN_PRESENT_MODE_TYPE_IMMEDIATE:
			wayland_vk_wsi_display->surface_capabilities.present_modes
				|= TPL_DISPLAY_PRESENT_MODE_IMMEDIATE;
			break;
		case WAYLAND_VULKAN_PRESENT_MODE_TYPE_MAILBOX:
			wayland_vk_wsi_display->surface_capabilities.present_modes
				|= TPL_DISPLAY_PRESENT_MODE_MAILBOX;
			break;
		case WAYLAND_VULKAN_PRESENT_MODE_TYPE_FIFO:
			wayland_vk_wsi_display->surface_capabilities.present_modes
				|= TPL_DISPLAY_PRESENT_MODE_FIFO;
			break;
		case WAYLAND_VULKAN_PRESENT_MODE_TYPE_FIFO_RELAXED:
			wayland_vk_wsi_display->surface_capabilities.present_modes
				|= TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED;
			break;
		default:
			TPL_WARN("server sent unknown present type: %d", mode);
	}
}

static struct wayland_vulkan_listener wl_vk_listener = {
	__tpl_wayland_vk_wsi_support_present_mode_listener,
};

static void
__tpl_wayland_vk_wsi_registry_handle_global(void *data, struct wl_registry *registry,
											uint32_t name, const char *interface, uint32_t version)
{
	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display = data;

	if (!strcmp(interface, "wayland_vulkan")) {
		wayland_vk_wsi_display->wl_vk_client =
			wl_registry_bind(registry, name, &wayland_vulkan_interface, version);
	}
}

static const struct wl_registry_listener registry_listener = {
	__tpl_wayland_vk_wsi_registry_handle_global,
	NULL
};

static tpl_result_t
__tpl_wayland_vk_wsi_display_init(tpl_display_t *display)
{
	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display = NULL;

	TPL_ASSERT(display);

	/* Do not allow default display in wayland. */
	if (!display->native_handle) {
		TPL_ERR("Invalid native handle for display.");
		return TPL_ERROR_INVALID_PARAMETER;
	}

	wayland_vk_wsi_display = (tpl_wayland_vk_wsi_display_t *) calloc(1,
							 sizeof(tpl_wayland_vk_wsi_display_t));
	if (!wayland_vk_wsi_display) {
		TPL_ERR("Failed to allocate memory for new tpl_wayland_vk_wsi_display_t.");
		return TPL_ERROR_OUT_OF_MEMORY;
	}

	wayland_vk_wsi_display->surface_capabilities.min_buffer = 2;
	wayland_vk_wsi_display->surface_capabilities.max_buffer = CLIENT_QUEUE_SIZE;
	wayland_vk_wsi_display->surface_capabilities.present_modes =
		TPL_DISPLAY_PRESENT_MODE_MAILBOX;

	display->backend.data = wayland_vk_wsi_display;

	if (__tpl_wayland_vk_wsi_display_is_wl_display(display->native_handle)) {
		struct wl_display *wl_dpy =
			(struct wl_display *)display->native_handle;
		struct wl_registry *wl_registry;

		wayland_vk_wsi_display->wl_tbm_client =
			wayland_tbm_client_init((struct wl_display *) wl_dpy);

		if (!wayland_vk_wsi_display->wl_tbm_client) {
			TPL_ERR("Wayland TBM initialization failed!");
			goto free_wl_display;
		}

		wl_registry = wl_display_get_registry(wl_dpy);
		/* check wl_registry */
		wl_registry_add_listener(wl_registry, &registry_listener, wayland_vk_wsi_display);
		wl_display_roundtrip(wl_dpy);

		if (wayland_vk_wsi_display->wl_vk_client)
			wayland_vulkan_add_listener(wayland_vk_wsi_display->wl_vk_client,
										&wl_vk_listener, wayland_vk_wsi_display);

		wl_display_roundtrip(wl_dpy);
		wl_registry_destroy(wl_registry);
	} else {
		goto free_wl_display;
	}

	return TPL_ERROR_NONE;

free_wl_display:
	if (wayland_vk_wsi_display) {
		free(wayland_vk_wsi_display);
		display->backend.data = NULL;
	}
	return TPL_ERROR_INVALID_OPERATION;
}

static void
__tpl_wayland_vk_wsi_display_fini(tpl_display_t *display)
{
	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display;

	TPL_ASSERT(display);

	wayland_vk_wsi_display = (tpl_wayland_vk_wsi_display_t *)display->backend.data;
	if (wayland_vk_wsi_display) {
		wayland_tbm_client_deinit(wayland_vk_wsi_display->wl_tbm_client);
		if (wayland_vk_wsi_display->wl_vk_client)
			wayland_vulkan_destroy(wayland_vk_wsi_display->wl_vk_client);
		free(wayland_vk_wsi_display);
	}
	display->backend.data = NULL;
}

static tpl_result_t
__tpl_wayland_vk_wsi_display_query_config(tpl_display_t *display,
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
__tpl_wayland_vk_wsi_display_filter_config(tpl_display_t *display,
		int *visual_id,
		int alpha_size)
{
	TPL_IGNORE(display);
	TPL_IGNORE(visual_id);
	TPL_IGNORE(alpha_size);
	return TPL_ERROR_NONE;
}

static tpl_result_t
__tpl_wayland_vk_wsi_display_query_window_supported_buffer_count(
	tpl_display_t *display,
	tpl_handle_t window, int *min, int *max)
{
	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display = NULL;

	TPL_ASSERT(display);
	TPL_ASSERT(window);

	wayland_vk_wsi_display = (tpl_wayland_vk_wsi_display_t *)display->backend.data;

	if (!wayland_vk_wsi_display) return TPL_ERROR_INVALID_OPERATION;

	if (min) *min = wayland_vk_wsi_display->surface_capabilities.min_buffer;
	if (max) *max = wayland_vk_wsi_display->surface_capabilities.max_buffer;

	return TPL_ERROR_NONE;
}

static tpl_result_t
__tpl_wayland_vk_wsi_display_query_window_supported_present_modes(
	tpl_display_t *display,
	tpl_handle_t window, int *modes)
{
	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display = NULL;

	TPL_ASSERT(display);
	TPL_ASSERT(window);

	wayland_vk_wsi_display = (tpl_wayland_vk_wsi_display_t *)display->backend.data;

	if (!wayland_vk_wsi_display) return TPL_ERROR_INVALID_OPERATION;

	if (modes) {
		*modes = TPL_DISPLAY_PRESENT_MODE_MAILBOX | TPL_DISPLAY_PRESENT_MODE_IMMEDIATE |
		wayland_vk_wsi_display->surface_capabilities.present_modes;
#if USE_WORKER_THREAD == 1
		if (__tpl_worker_support_vblank() == TPL_TRUE)
			*modes |= TPL_DISPLAY_PRESENT_MODE_FIFO | TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED;
#endif
	}

	return TPL_ERROR_NONE;
}

static tpl_result_t
__tpl_wayland_vk_wsi_surface_init(tpl_surface_t *surface)
{
	tpl_wayland_vk_wsi_surface_t *wayland_vk_wsi_surface = NULL;

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->type == TPL_SURFACE_TYPE_WINDOW);
	TPL_ASSERT(surface->native_handle);

	wayland_vk_wsi_surface = (tpl_wayland_vk_wsi_surface_t *) calloc(1,
							 sizeof(tpl_wayland_vk_wsi_surface_t));
	if (!wayland_vk_wsi_surface) {
		TPL_ERR("Failed to allocate memory for new tpl_wayland_vk_wsi_surface_t.");
		return TPL_ERROR_OUT_OF_MEMORY;
	}

	surface->backend.data = (void *)wayland_vk_wsi_surface;
	wayland_vk_wsi_surface->tbm_queue = NULL;

	return TPL_ERROR_NONE;
}

static void
__tpl_wayland_vk_wsi_surface_fini(tpl_surface_t *surface)
{
	tpl_wayland_vk_wsi_surface_t *wayland_vk_wsi_surface = NULL;
	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display = NULL;

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->display);

	wayland_vk_wsi_surface = (tpl_wayland_vk_wsi_surface_t *) surface->backend.data;
	if (wayland_vk_wsi_surface == NULL) return;

	wayland_vk_wsi_display = (tpl_wayland_vk_wsi_display_t *)
							 surface->display->backend.data;
	if (wayland_vk_wsi_display == NULL) return;

	if (wayland_vk_wsi_surface->tbm_queue)
		__tpl_wayland_vk_wsi_surface_destroy_swapchain(surface);

	free(wayland_vk_wsi_surface);
	surface->backend.data = NULL;
}

static void
__tpl_wayland_vk_wsi_surface_commit_buffer(tpl_surface_t *surface,
		tbm_surface_h tbm_surface)
{
	TPL_ASSERT(surface);
	TPL_ASSERT(surface->display);
	TPL_ASSERT(surface->display->native_handle);
	TPL_ASSERT(tbm_surface);
	TPL_ASSERT(tbm_surface_internal_is_valid(tbm_surface));

	struct wl_surface *wl_sfc = NULL;
	struct wl_callback *frame_callback = NULL;
	tpl_wayland_vk_wsi_surface_t *wayland_vk_wsi_surface =
		(tpl_wayland_vk_wsi_surface_t *) surface->backend.data;
	tpl_wayland_vk_wsi_buffer_t *wayland_vk_wsi_buffer =
		__tpl_wayland_vk_wsi_get_wayland_buffer_from_tbm_surface(tbm_surface);
	TPL_ASSERT(wayland_vk_wsi_buffer);


	wl_sfc = (struct wl_surface *)surface->native_handle;

	tbm_surface_internal_ref(tbm_surface);
	wl_surface_attach(wl_sfc, (void *)wayland_vk_wsi_buffer->wl_proxy, 0, 0);

	/* TODO: add num_rects and rects to tpl_wayland_vk_wsi_buffer_t */
	wl_surface_damage(wl_sfc, 0, 0, surface->width, surface->height);

	frame_callback = wl_surface_frame(wl_sfc);
	wl_callback_add_listener(frame_callback, &frame_listener, tbm_surface);

	wl_surface_commit(wl_sfc);

	wl_display_flush(surface->display->native_handle);
	wayland_vk_wsi_buffer->sync_timestamp++;

#if USE_WORKER_THREAD == 1
	pthread_mutex_lock(&wayland_vk_wsi_surface->free_queue_mutex);
#endif
	tbm_surface_queue_release(wayland_vk_wsi_surface->tbm_queue, tbm_surface);
#if USE_WORKER_THREAD == 1
	pthread_mutex_unlock(&wayland_vk_wsi_surface->free_queue_mutex);
	pthread_cond_signal(&wayland_vk_wsi_surface->free_queue_cond);
#endif
}

static tpl_result_t
__tpl_wayland_vk_wsi_surface_enqueue_buffer(tpl_surface_t *surface,
		tbm_surface_h tbm_surface,
		int num_rects, const int *rects,
		tbm_fd sync_fence)
{

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->display);
	TPL_ASSERT(surface->display->native_handle);
	TPL_ASSERT(tbm_surface);

	tpl_wayland_vk_wsi_surface_t *wayland_vk_wsi_surface =
		(tpl_wayland_vk_wsi_surface_t *) surface->backend.data;
	tpl_wayland_vk_wsi_buffer_t *wayland_vk_wsi_buffer = NULL;
	tbm_surface_queue_error_e tsq_err;

	if (!tbm_surface_internal_is_valid(tbm_surface)) {
		TPL_ERR("Failed to enqueue tbm_surface(%p) Invalid value.", tbm_surface);
		return TPL_ERROR_INVALID_PARAMETER;
	}

	wayland_vk_wsi_buffer =
		__tpl_wayland_vk_wsi_get_wayland_buffer_from_tbm_surface(tbm_surface);
	TPL_ASSERT(wayland_vk_wsi_buffer);

	TPL_IMAGE_DUMP(tbm_surface, surface->width, surface->height);

	tbm_surface_internal_unref(tbm_surface);

#if USE_WORKER_THREAD == 1
	wayland_vk_wsi_buffer->wait_sync = sync_fence;
#endif
	tsq_err = tbm_surface_queue_enqueue(wayland_vk_wsi_surface->tbm_queue,
										tbm_surface);
	if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE) {
		TPL_ERR("Failed to enqeueue tbm_surface. | tsq_err = %d", tsq_err);
		return TPL_ERROR_INVALID_OPERATION;
	}

#if USE_WORKER_THREAD == 0
	if (sync_fence != -1) {
		/* non worker thread mode */
		/* TODO: set max wait time */
		if (tbm_sync_fence_wait(sync_fence, -1) != 1) {
			char buf[1024];
			strerror_r(errno, buf, sizeof(buf));
			TPL_ERR("Failed to wait sync. | error: %d(%s)", errno, buf);
		}
		close(sync_fence);
	}

	tsq_err = tbm_surface_queue_acquire(wayland_vk_wsi_surface->tbm_queue,
										&tbm_surface);
	if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE) {
		TPL_ERR("Failed to acquire tbm_surface. | tsq_err = %d", tsq_err);
		return TPL_ERROR_INVALID_OPERATION;
	}

	__tpl_wayland_vk_wsi_surface_commit_buffer(surface, tbm_surface);

#else
	__tpl_worker_new_buffer_notify(&wayland_vk_wsi_surface->worker_surface);

#endif

	/*
	 * tbm_surface insert to free queue.
	 * tbm_surface_can_dequeue always return true in single thread.
	 * __tpl_wayland_vk_wsi_surface_dequeue_buffer doesn't call wl_display_dispatch.
	 * wayland event queue are fulled and occur broken pipe.
	 * so need call wl_display_dispatch.
	 * need discussion wl_display_dispatch position(in dequeue or worker thread ??).
	 */
	wl_display_dispatch(surface->display->native_handle);

	return TPL_ERROR_NONE;
}

static tpl_bool_t
__tpl_wayland_vk_wsi_surface_validate(tpl_surface_t *surface)
{
	TPL_ASSERT(surface);
	TPL_ASSERT(surface->backend.data);

	return TPL_TRUE;
}

static tbm_surface_h
__tpl_wayland_vk_wsi_surface_dequeue_buffer(tpl_surface_t *surface,
											uint64_t timeout_ns,
											tbm_fd *sync_fence)
{
	TPL_ASSERT(surface);
	TPL_ASSERT(surface->backend.data);
	TPL_ASSERT(surface->display);

	tbm_surface_h tbm_surface = NULL;
	tpl_wayland_vk_wsi_buffer_t *wayland_vk_wsi_buffer = NULL;
	tpl_wayland_vk_wsi_surface_t *wayland_vk_wsi_surface =
		(tpl_wayland_vk_wsi_surface_t *)surface->backend.data;
	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display =
		(tpl_wayland_vk_wsi_display_t *)surface->display->backend.data;
	struct wl_proxy *wl_proxy = NULL;
	tbm_surface_queue_error_e tsq_err = 0;

	if (sync_fence)
		*sync_fence = -1;

#if USE_WORKER_THREAD == 0
	TPL_OBJECT_UNLOCK(surface);
	while (tbm_surface_queue_can_dequeue(
				wayland_vk_wsi_surface->tbm_queue, 0) == 0) {
		/* Application sent all buffers to the server. Wait for server response. */

		if (wl_display_dispatch(surface->display->native_handle) == -1) {
			TPL_OBJECT_LOCK(surface);
			return NULL;
		}
	}
	TPL_OBJECT_LOCK(surface);
#else
	/*
	 * TODO: it can move to libtbm
	 *		 libtbm already has free queue's pthread_cond and pthread_mutex
	 */
	struct timespec abs_time;
	if (timeout_ns != UINT64_MAX) {
		clock_gettime(CLOCK_REALTIME, &abs_time);
		abs_time.tv_sec += (timeout_ns / 1000000000L);
		abs_time.tv_nsec += (timeout_ns % 1000000000L);
		if (abs_time.tv_nsec >= 1000000000L) {
			abs_time.tv_sec += (abs_time.tv_nsec / 1000000000L);
			abs_time.tv_nsec = (abs_time.tv_nsec % 1000000000L);
		}
	}
	pthread_mutex_lock(&wayland_vk_wsi_surface->free_queue_mutex);
	while (tbm_surface_queue_can_dequeue(wayland_vk_wsi_surface->tbm_queue,
										 0) == 0) {
		if (timeout_ns != UINT64_MAX) {
			int ret;
			ret = pthread_cond_timedwait(&wayland_vk_wsi_surface->free_queue_cond,
										 &wayland_vk_wsi_surface->free_queue_mutex,
										 &abs_time);
			if (ret == ETIMEDOUT) {
				/* timeout */
				pthread_mutex_unlock(&wayland_vk_wsi_surface->free_queue_mutex);
				return NULL;
			}
		} else {
			pthread_cond_wait(&wayland_vk_wsi_surface->free_queue_cond,
							  &wayland_vk_wsi_surface->free_queue_mutex);
		}
	}
#endif

	tsq_err = tbm_surface_queue_dequeue(wayland_vk_wsi_surface->tbm_queue,
										&tbm_surface);
#if USE_WORKER_THREAD == 1
	pthread_mutex_unlock(&wayland_vk_wsi_surface->free_queue_mutex);
#endif
	if (!tbm_surface) {
		TPL_ERR("Failed to get tbm_surface from tbm_surface_queue | tsq_err = %d",
				tsq_err);
		return NULL;
	}

	tbm_surface_internal_ref(tbm_surface);

	if ((wayland_vk_wsi_buffer =
				__tpl_wayland_vk_wsi_get_wayland_buffer_from_tbm_surface(
					tbm_surface)) != NULL) {
		if (sync_fence) {
			if (wayland_vk_wsi_buffer->sync_timestamp) {
				/* first return -1 */
				char name[32];
				snprintf(name, 32, "%d",
						 tbm_bo_export(tbm_surface_internal_get_bo(tbm_surface, 0)));
				*sync_fence = tbm_sync_fence_create(wayland_vk_wsi_buffer->sync_timeline,
													name,
													wayland_vk_wsi_buffer->sync_timestamp);
				if (*sync_fence == -1) {
					char buf[1024];
					strerror_r(errno, buf, sizeof(buf));
					TPL_ERR("Failed to create TBM sync fence: %d(%s)", errno, buf);
				}
			} else {
				*sync_fence = -1;
			}
		}
		return tbm_surface;
	}

	wayland_vk_wsi_buffer = (tpl_wayland_vk_wsi_buffer_t *) calloc(1,
							sizeof(tpl_wayland_vk_wsi_buffer_t));
	if (!wayland_vk_wsi_buffer) {
		TPL_ERR("Mem alloc for wayland_vk_wsi_buffer failed!");
		tbm_surface_internal_unref(tbm_surface);
		return NULL;
	}

	wl_proxy = (struct wl_proxy *)wayland_tbm_client_create_buffer(
				   wayland_vk_wsi_display->wl_tbm_client, tbm_surface);
	if (!wl_proxy) {
		TPL_ERR("Failed to create TBM client buffer!");
		tbm_surface_internal_unref(tbm_surface);
		free(wayland_vk_wsi_buffer);
		return NULL;
	}

	/* can change signaled sync */
	if (sync_fence)
		*sync_fence = -1;
	wayland_vk_wsi_buffer->sync_timeline = tbm_sync_timeline_create();
	if (wayland_vk_wsi_buffer->sync_timeline == -1) {
		char buf[1024];
		strerror_r(errno, buf, sizeof(buf));
		TPL_ERR("Failed to create TBM sync timeline: %d(%s)", errno, buf);
		wl_proxy_destroy(wl_proxy);
		tbm_surface_internal_unref(tbm_surface);
		free(wayland_vk_wsi_buffer);
		return NULL;
	}
	wayland_vk_wsi_buffer->sync_timestamp = 0;
	wayland_tbm_client_set_sync_timeline(wayland_vk_wsi_display->wl_tbm_client,
										 (void *)wl_proxy,
										 wayland_vk_wsi_buffer->sync_timeline);

	wl_buffer_add_listener((void *)wl_proxy, &buffer_release_listener,
						   tbm_surface);

	wl_display_flush((struct wl_display *)surface->display->native_handle);

	wayland_vk_wsi_buffer->display = surface->display;
	wayland_vk_wsi_buffer->wl_proxy = wl_proxy;

	__tpl_wayland_vk_wsi_set_wayland_buffer_to_tbm_surface(tbm_surface,
			wayland_vk_wsi_buffer);

	return tbm_surface;
}

static tpl_result_t
__tpl_wayland_vk_wsi_surface_get_swapchain_buffers(tpl_surface_t *surface,
		tbm_surface_h **buffers,
		int *buffer_count)
{
	tbm_surface_h buffer = NULL;
	tbm_surface_h *swapchain_buffers = NULL;
	tpl_wayland_vk_wsi_surface_t *wayland_vk_wsi_surface = NULL;
	tbm_surface_queue_error_e tsq_err;
	int i, dequeue_count;
	tpl_result_t ret = TPL_ERROR_NONE;

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->backend.data);
	TPL_ASSERT(surface->display);
	TPL_ASSERT(buffers);
	TPL_ASSERT(buffer_count);

	wayland_vk_wsi_surface = (tpl_wayland_vk_wsi_surface_t *)surface->backend.data;
	swapchain_buffers = (tbm_surface_h *)calloc(
							wayland_vk_wsi_surface->buffer_count, sizeof(tbm_surface_h));
	if (!swapchain_buffers) {
		TPL_ERR("Failed to allocate memory for buffers.");
		return TPL_ERROR_OUT_OF_MEMORY;
	}

	for (i = 0 ; i < wayland_vk_wsi_surface->buffer_count ; i++) {
		tsq_err = tbm_surface_queue_dequeue(wayland_vk_wsi_surface->tbm_queue, &buffer);
		if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE) {
			TPL_ERR("Failed to get tbm_surface from tbm_surface_queue | tsq_err = %d",
					tsq_err);
			dequeue_count = i;
			ret = TPL_ERROR_OUT_OF_MEMORY;
			goto get_buffer_fail;
		}
		swapchain_buffers[i] = buffer;
	}

	for (i = 0 ; i < wayland_vk_wsi_surface->buffer_count ; i++) {
		tsq_err = tbm_surface_queue_release(wayland_vk_wsi_surface->tbm_queue,
											swapchain_buffers[i]);
		if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE) {
			TPL_ERR("Failed to release tbm_surface. | tsq_err = %d", tsq_err);
			ret = TPL_ERROR_INVALID_OPERATION;
			goto release_buffer_fail;
		}
	}

	*buffers = swapchain_buffers;
	*buffer_count = wayland_vk_wsi_surface->buffer_count;
	return TPL_ERROR_NONE;

get_buffer_fail:
	for (i = 0 ; i < dequeue_count ; i++) {
		tsq_err = tbm_surface_queue_release(wayland_vk_wsi_surface->tbm_queue,
											swapchain_buffers[i]);
		if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE) {
			TPL_ERR("Failed to release tbm_surface. | tsq_err = %d", tsq_err);
			goto release_buffer_fail;
		}
	}

release_buffer_fail:
	free(swapchain_buffers);
	return ret;
}

#if USE_WORKER_THREAD == 1
static void
__tpl_wayland_vk_wsi_process_draw_done(tpl_surface_t *surface,
									   tbm_surface_h tbm_surface,
									   tpl_result_t result)
{
	tpl_wayland_vk_wsi_surface_t *wayland_vk_wsi_surface = NULL;
	tpl_wayland_vk_wsi_buffer_t *wayland_vk_wsi_buffer = NULL;
	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display = NULL;

	TPL_ASSERT(surface);
	TPL_ASSERT(tbm_surface);
	TPL_ASSERT(tbm_surface_internal_is_valid(tbm_surface));

	wayland_vk_wsi_surface =
		(tpl_wayland_vk_wsi_surface_t *)surface->backend.data;
	wayland_vk_wsi_buffer =
		__tpl_wayland_vk_wsi_get_wayland_buffer_from_tbm_surface(tbm_surface);
	wayland_vk_wsi_display = (tpl_wayland_vk_wsi_display_t *)
							 surface->display->backend.data;

	TPL_ASSERT(wayland_vk_wsi_surface);
	TPL_ASSERT(wayland_vk_wsi_buffer);
	TPL_ASSERT(wayland_vk_wsi_display);

	close(wayland_vk_wsi_buffer->wait_sync);
	wayland_vk_wsi_buffer->wait_sync = -1;

	/* if server supported current supported mode then just send */

	if (wayland_vk_wsi_surface->present_mode &
		wayland_vk_wsi_display->surface_capabilities.present_modes) {
		__tpl_wayland_vk_wsi_surface_commit_buffer(surface, tbm_surface);
		return;
	}

	if (wayland_vk_wsi_surface->present_mode == TPL_DISPLAY_PRESENT_MODE_FIFO) {
		pthread_mutex_lock(&wayland_vk_wsi_surface->vblank_list_mutex);
		/* unref in tpl list remove callback
		   (__tpl_wayland_vk_wsi_buffer_remove_from_vblank_list) */
		tbm_surface_internal_ref(tbm_surface);
		__tpl_list_push_back(&wayland_vk_wsi_surface->vblank_list, tbm_surface);
		pthread_mutex_unlock(&wayland_vk_wsi_surface->vblank_list_mutex);
	} else if (wayland_vk_wsi_surface->present_mode == TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED &&
			   wayland_vk_wsi_surface->vblank_done == TPL_FALSE) {
		/* if can't process previous vblank event, send buffer immediately */
		pthread_mutex_lock(&wayland_vk_wsi_surface->vblank_list_mutex);
		/* unref in tpl list remove callback
		   (__tpl_wayland_vk_wsi_buffer_remove_from_vblank_list) */
		tbm_surface_internal_ref(tbm_surface);
		__tpl_list_push_back(&wayland_vk_wsi_surface->vblank_list, tbm_surface);
		wayland_vk_wsi_surface->vblank_done = TPL_TRUE;
		pthread_mutex_unlock(&wayland_vk_wsi_surface->vblank_list_mutex);
	} else {
		__tpl_wayland_vk_wsi_surface_commit_buffer(surface, tbm_surface);
	}
}

static int
__tpl_wayland_vk_wsi_draw_wait_fd_get(tpl_surface_t *surface,
									  tbm_surface_h tbm_surface)
{
	tpl_wayland_vk_wsi_buffer_t *wayland_vk_wsi_buffer = NULL;

	/*TPL_ASSERT(surface);*/
	TPL_ASSERT(tbm_surface);

	wayland_vk_wsi_buffer =
		__tpl_wayland_vk_wsi_get_wayland_buffer_from_tbm_surface(tbm_surface);

	TPL_ASSERT(wayland_vk_wsi_buffer);

	return wayland_vk_wsi_buffer->wait_sync;
}

static void
__tpl_wayland_vk_wsi_buffer_remove_from_vblank_list(void *data)
{
	tbm_surface_h tbm_surface = data;
	tbm_surface_internal_unref(tbm_surface);
}

static void
__tpl_wayland_vk_wsi_vblank(tpl_surface_t *surface, unsigned int sequence,
							unsigned int tv_sec, unsigned int tv_usec)
{
	tpl_wayland_vk_wsi_surface_t *wayland_vk_wsi_surface;
	tbm_surface_h tbm_surface;

	TPL_ASSERT(surface);

	wayland_vk_wsi_surface =
		(tpl_wayland_vk_wsi_surface_t *) surface->backend.data;

	TPL_ASSERT(wayland_vk_wsi_surface);

	if ((wayland_vk_wsi_surface->present_mode &
		 (TPL_DISPLAY_PRESENT_MODE_FIFO | TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED)) == 0)
		return;

	pthread_mutex_lock(&wayland_vk_wsi_surface->vblank_list_mutex);
	tbm_surface = __tpl_list_pop_front(&wayland_vk_wsi_surface->vblank_list,
									   __tpl_wayland_vk_wsi_buffer_remove_from_vblank_list);
	pthread_mutex_unlock(&wayland_vk_wsi_surface->vblank_list_mutex);

	 if (tbm_surface_internal_is_valid(tbm_surface)) {
		__tpl_wayland_vk_wsi_surface_commit_buffer(surface, tbm_surface);
		wayland_vk_wsi_surface->vblank_done = TPL_TRUE;
	} else {
		wayland_vk_wsi_surface->vblank_done = TPL_FALSE;
	}
}
#endif

static tpl_result_t
__tpl_wayland_vk_wsi_surface_create_swapchain(tpl_surface_t *surface,
		tbm_format format, int width,
		int height, int buffer_count, int present_mode)
{
	tpl_wayland_vk_wsi_surface_t *wayland_vk_wsi_surface = NULL;
	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display = NULL;

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->backend.data);
	TPL_ASSERT(surface->display);

	wayland_vk_wsi_surface = (tpl_wayland_vk_wsi_surface_t *) surface->backend.data;
	TPL_ASSERT(wayland_vk_wsi_surface);

	wayland_vk_wsi_display = (tpl_wayland_vk_wsi_display_t *)
							 surface->display->backend.data;
	TPL_ASSERT(wayland_vk_wsi_display);

	if ((buffer_count < wayland_vk_wsi_display->surface_capabilities.min_buffer)
			|| (buffer_count > wayland_vk_wsi_display->surface_capabilities.max_buffer)) {
		TPL_ERR("Invalid buffer_count!");
		return TPL_ERROR_INVALID_PARAMETER;
	}

	/* FIXME: vblank has performance problem so replace all present mode to MAILBOX */
	present_mode = TPL_DISPLAY_PRESENT_MODE_MAILBOX;

	if ((present_mode & wayland_vk_wsi_display->surface_capabilities.present_modes) == 0) {
		/* server not supported current mode check client mode */
		switch (present_mode) {
#if USE_WORKER_THREAD == 1
			case TPL_DISPLAY_PRESENT_MODE_FIFO:
			case TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED:
				if (__tpl_worker_support_vblank() == TPL_FALSE) {
					TPL_ERR("Unsupported present mode: %d, worker not support vblank",
							present_mode);
					return TPL_ERROR_INVALID_PARAMETER;
				}
#endif
			case TPL_DISPLAY_PRESENT_MODE_MAILBOX:
			case TPL_DISPLAY_PRESENT_MODE_IMMEDIATE:
				break;
			default:
				TPL_ERR("Unsupported present mode: %d", present_mode);
				return TPL_ERROR_INVALID_PARAMETER;
		}
	}

	wayland_vk_wsi_surface->present_mode = present_mode;

	wayland_vk_wsi_surface->tbm_queue = tbm_surface_queue_create(buffer_count,
										width,
										height,
										TBM_FORMAT_ARGB8888,
										0);

	if (!wayland_vk_wsi_surface->tbm_queue) {
		TPL_ERR("TBM surface queue creation failed!");
		return TPL_ERROR_OUT_OF_MEMORY;
	}

	wayland_vk_wsi_surface->buffer_count = buffer_count;

	surface->width = width;
	surface->height = height;

#if USE_WORKER_THREAD == 1
	pthread_mutex_init(&wayland_vk_wsi_surface->free_queue_mutex, NULL);
	pthread_cond_init(&wayland_vk_wsi_surface->free_queue_cond, NULL);

	wayland_vk_wsi_surface->worker_surface.surface = surface;
	wayland_vk_wsi_surface->worker_surface.tbm_queue =
		wayland_vk_wsi_surface->tbm_queue;

	wayland_vk_wsi_surface->worker_surface.draw_done =
		__tpl_wayland_vk_wsi_process_draw_done;
	wayland_vk_wsi_surface->worker_surface.draw_wait_fd_get =
		__tpl_wayland_vk_wsi_draw_wait_fd_get;
	if ((wayland_vk_wsi_surface->present_mode &
		 (TPL_DISPLAY_PRESENT_MODE_FIFO | TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED))) {
		wayland_vk_wsi_surface->worker_surface.vblank =
			__tpl_wayland_vk_wsi_vblank;
		pthread_mutex_init(&wayland_vk_wsi_surface->vblank_list_mutex, NULL);
		__tpl_list_init(&wayland_vk_wsi_surface->vblank_list);
	}

	__tpl_worker_surface_list_insert(&wayland_vk_wsi_surface->worker_surface);
#endif
	return TPL_ERROR_NONE;
}

static tpl_result_t
__tpl_wayland_vk_wsi_surface_destroy_swapchain(tpl_surface_t *surface)
{
	tpl_wayland_vk_wsi_surface_t *wayland_vk_wsi_surface = NULL;

	TPL_ASSERT(surface);
	TPL_ASSERT(surface->backend.data);
	TPL_ASSERT(surface->display);

	wayland_vk_wsi_surface = (tpl_wayland_vk_wsi_surface_t *) surface->backend.data;
	TPL_ASSERT(wayland_vk_wsi_surface);

#if USE_WORKER_THREAD == 1
	__tpl_worker_surface_list_remove(&wayland_vk_wsi_surface->worker_surface);
#endif
	if (surface->type == TPL_SURFACE_TYPE_WINDOW) {

		wl_display_flush(surface->display->native_handle);
		__tpl_wayland_vk_wsi_display_roundtrip(surface->display);

		tbm_surface_queue_destroy(wayland_vk_wsi_surface->tbm_queue);
		wayland_vk_wsi_surface->tbm_queue = NULL;
	}

#if USE_WORKER_THREAD == 1
	if ((wayland_vk_wsi_surface->present_mode &
		 (TPL_DISPLAY_PRESENT_MODE_FIFO | TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED))) {
		pthread_mutex_lock(&wayland_vk_wsi_surface->vblank_list_mutex);
		__tpl_list_fini(&wayland_vk_wsi_surface->vblank_list,
						__tpl_wayland_vk_wsi_buffer_remove_from_vblank_list);
		pthread_mutex_unlock(&wayland_vk_wsi_surface->vblank_list_mutex);
		pthread_mutex_destroy(&wayland_vk_wsi_surface->vblank_list_mutex);
	}

	pthread_cond_destroy(&wayland_vk_wsi_surface->free_queue_cond);
	pthread_mutex_destroy(&wayland_vk_wsi_surface->free_queue_mutex);
#endif
	return TPL_ERROR_NONE;
}

static void
__tpl_wayland_vk_wsi_buffer_free(tpl_wayland_vk_wsi_buffer_t
								 *wayland_vk_wsi_buffer)
{
	TPL_ASSERT(wayland_vk_wsi_buffer);
	TPL_ASSERT(wayland_vk_wsi_buffer->display);

	tpl_wayland_vk_wsi_display_t *wayland_vk_wsi_display =
		(tpl_wayland_vk_wsi_display_t *)wayland_vk_wsi_buffer->display->backend.data;

	wl_display_flush((struct wl_display *)
					 wayland_vk_wsi_buffer->display->native_handle);

	if (wayland_vk_wsi_buffer->wl_proxy)
		wayland_tbm_client_destroy_buffer(wayland_vk_wsi_display->wl_tbm_client,
										  (void *)wayland_vk_wsi_buffer->wl_proxy);

	if (wayland_vk_wsi_buffer->sync_timeline != -1)
		close(wayland_vk_wsi_buffer->sync_timeline);

	free(wayland_vk_wsi_buffer);
}

tpl_bool_t
__tpl_display_choose_backend_wayland_vk_wsi(tpl_handle_t native_dpy)
{
	if (!native_dpy) return TPL_FALSE;

	if (__tpl_wayland_vk_wsi_display_is_wl_display(native_dpy))
		return TPL_TRUE;

	return TPL_FALSE;
}

void
__tpl_display_init_backend_wayland_vk_wsi(tpl_display_backend_t *backend)
{
	TPL_ASSERT(backend);

	backend->type = TPL_BACKEND_WAYLAND_VULKAN_WSI;
	backend->data = NULL;

	backend->init = __tpl_wayland_vk_wsi_display_init;
	backend->fini = __tpl_wayland_vk_wsi_display_fini;
	backend->query_config = __tpl_wayland_vk_wsi_display_query_config;
	backend->filter_config = __tpl_wayland_vk_wsi_display_filter_config;
	backend->query_window_supported_buffer_count =
		__tpl_wayland_vk_wsi_display_query_window_supported_buffer_count;
	backend->query_window_supported_present_modes =
		__tpl_wayland_vk_wsi_display_query_window_supported_present_modes;
}

void
__tpl_surface_init_backend_wayland_vk_wsi(tpl_surface_backend_t *backend)
{
	TPL_ASSERT(backend);

	backend->type = TPL_BACKEND_WAYLAND_VULKAN_WSI;
	backend->data = NULL;

	backend->init = __tpl_wayland_vk_wsi_surface_init;
	backend->fini = __tpl_wayland_vk_wsi_surface_fini;
	backend->validate = __tpl_wayland_vk_wsi_surface_validate;
	backend->dequeue_buffer = __tpl_wayland_vk_wsi_surface_dequeue_buffer;
	backend->enqueue_buffer = __tpl_wayland_vk_wsi_surface_enqueue_buffer;
	backend->get_swapchain_buffers =
		__tpl_wayland_vk_wsi_surface_get_swapchain_buffers;
	backend->create_swapchain = __tpl_wayland_vk_wsi_surface_create_swapchain;
	backend->destroy_swapchain = __tpl_wayland_vk_wsi_surface_destroy_swapchain;
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
	tpl_wayland_vk_wsi_buffer_t *wayland_vk_wsi_buffer = NULL;
	tbm_surface_h tbm_surface = NULL;

	TPL_ASSERT(data);

	tbm_surface = (tbm_surface_h) data;

	wayland_vk_wsi_buffer =
		__tpl_wayland_vk_wsi_get_wayland_buffer_from_tbm_surface(tbm_surface);

	if (wayland_vk_wsi_buffer)
		tbm_surface_internal_unref(tbm_surface);
}

static const struct wl_buffer_listener buffer_release_listener = {
	(void *)__cb_client_buffer_release_callback,
};
