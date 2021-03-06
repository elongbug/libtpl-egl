#ifndef TPL_H
#define TPL_H

/**
 * @file tpl.h
 * @brief TPL API header file.
 *
 * TPL is an abstraction layer for surface & buffer management on Tizen
 * platform aimed to implement the EGL porting layer of GPU Vendor's OpenGLES
 * driver over various display protocols.
 *
 * TPL provides object-oriented interfaces. Every TPL object can be represented
 * as generic tpl_object_t which is referenced-counted and provides common
 * functions. Currently, following types of objects are provided.
 *
 * Display
 * Surface
 * Buffer
 *
 * Display, like a normal display, represents a display system which is usually
 * used for connection to the server, scope for other objects.
 *
 * Surface correponds to a native surface like X drawable or wl_surface.
 * A surface might be configured to use N-buffers. (usually double-buffered or
 * tripple-buffered).
 *
 * Buffer is actually something we can render on it, usually a set of pixels or
 * block of memory.
 *
 * Here is a simple example
 *
 * dpy = tpl_display_create(...);
 * sfc = tpl_surface_create(dpy, ...);
 *
 * while (1)
 * {
 *	buf = tpl_surface_dequeue_buffer(sfc);
 *
 *	draw something...
 *
 *	tpl_surface_enqueue_buffer(sfc, buf);
 * }
 *
 * In GPU Vendor driver, "draw something..." part is what the GPU frame builder does.
 *
 * TPL exposes native platform buffer identifiers and managers so that the
 * buffer can be used in other modules. Currently, dma_buf/DRM is supported for
 * such kind of purposes.
 *
 * EGL porting layer just calls TPL functions to do what it is requested, and
 * give the result to GPU Vendor driver. TPL does all the protocol dependent actions.
 * Such protocol dependent part can be well-separated into TPL backends.
 *
 * Also, TPL backend can be configured at runtime. Users can specify which type
 * of backend to use when initializing a display object.
 *
 * For detailed API semantics, please refer to the API documentations.
 */

#define TPL_TRUE	1
#define TPL_FALSE	0

#define TPL_DONT_CARE -1

#include <tbm_surface.h>
#include <tbm_sync.h>
/**
 * Boolean variable type.
 *
 * TPL_TRUE or TPL_FALSE
 */
typedef unsigned int tpl_bool_t;

/**
 * Handle to native objects.
 *
 * Represent a handle to a native object like pixmap, window, wl_display and
 * etc.
 */
typedef void *tpl_handle_t;

/**
 * Structure containing function pointers to DDK's EGL layer.
 *
 * TPL needs to call DDK specific functions such as atomic operations and hash
 * tables. The necessary function pointers are registered in this structure.
 */
#include <stdlib.h>

typedef volatile unsigned int tpl_util_atomic_uint;

/**
 * A structure representing generic TPL object.
 *
 * Generic base class type for various TPL objects.
 */
typedef struct _tpl_object tpl_object_t;

/**
 * A structure representing TPL display object.
 *
 * TPL display is an object representing a system which is used to display
 * things. This is similar in concept with native displays such as X Display
 * and wl_display. TPL display is used for following items.
 *
 * 1. Communication channel with native display servers.
 * 2. name space for other TPL objects.
 */
typedef struct _tpl_display tpl_display_t;

/**
 * A structure representing TPL surface object.
 *
 * TPL surface is an object representing an image which can be displayed by the
 * display system. This corresponds to a native surface like X Drawable or
 * wl_surface. A TPL surface might have several TPL buffers on which we can
 * render.
 */
typedef struct _tpl_surface tpl_surface_t;

/**
 * Function type used for freeing some data.
 */
typedef void (*tpl_free_func_t)(void *data);

/**
 * Function type used for registering callback function.
 */
typedef void (*tpl_surface_cb_func_t)(void *data);

/**
 * Object types.
 *
 * @see tpl_object_get_type()
 */
typedef enum {
	TPL_OBJECT_ERROR = -1,
	TPL_OBJECT_DISPLAY,
	TPL_OBJECT_SURFACE,
	TPL_OBJECT_MAX
} tpl_object_type_t;

/**
 * Surface types.
 *
 * On some display system, there're several types of native surfaces. (ex. X11
 * pixmap and window). Users might want to know what kind of native surface
 * type a TPL surface was made from.
 *
 * @see tpl_surface_create()
 * @see tpl_surface_get_type()
 */
typedef enum {
	TPL_SURFACE_ERROR = -1,
	TPL_SURFACE_TYPE_WINDOW, /**< surface gets displayed by the display server. */
	TPL_SURFACE_TYPE_PIXMAP, /**< surface is an offscreen pixmap. */
	TPL_SURFACE_MAX
} tpl_surface_type_t;

/**
 * Lock usage types.
 *
 * TPL provides buffer locks which are used for synchronization. This usage
 * indicate that what kind of purpose a locking is used for. Depending on the
 * system, multiple read locks might be allowed. Cache might be flushed when
 * CPU access is engaged.
 *
 * @see tpl_buffer_lock()
 */
typedef enum {
	TPL_LOCK_USAGE_INVALID = 0,
	TPL_LOCK_USAGE_GPU_READ,
	TPL_LOCK_USAGE_GPU_WRITE,
	TPL_LOCK_USAGE_CPU_READ,
	TPL_LOCK_USAGE_CPU_WRITE
} tpl_lock_usage_t;

/**
 * Types of TPL backend.
 *
 * TPL provides platform independent APIs by implementing platform dependent
 * things in a backend. These types represent types of such backends. One of
 * these types should be specified when creating a TPL display object when
 * calling tpl_display_create().
 *
 * @see tpl_display_create()
 */
typedef enum {
	TPL_BACKEND_UNKNOWN = -1,
	TPL_BACKEND_WAYLAND, /* this backend represend wayland-egl */
	TPL_BACKEND_GBM,
	TPL_BACKEND_X11_DRI2,
	TPL_BACKEND_X11_DRI3,
	TPL_BACKEND_TBM,
	TPL_BACKEND_WAYLAND_VULKAN_WSI,
	TPL_BACKEND_TBM_VULKAN_WSI,
	TPL_BACKEND_COUNT,
	TPL_BACKEND_MAX
} tpl_backend_type_t;

/**
 * Enumeration for TPL result type.
 *
 */
typedef enum {
	TPL_ERROR_NONE = 0, /* Successfull */
	TPL_ERROR_INVALID_PARAMETER, /* Invalid parmeter */
	TPL_ERROR_INVALID_OPERATION, /* Invalid operation */
	TPL_ERROR_OUT_OF_MEMORY /* Out of memory */
} tpl_result_t;

/**
 * Increase reference count of a TPL object.
 *
 * All TPL objects are reference-counted. They have reference count 1 on
 * creatation. When the reference count drops to 0, the object will be freed.
 *
 * @param object object which will be referenced.
 * @return reference count after reference on success, -1 on error.
 *
 * @see tpl_object_unreference()
 * @see tpl_object_get_reference()
 */
int
tpl_object_reference(tpl_object_t *object);

/**
 * Decrease reference count of a TPL object.
 *
 * @param object object which will be unreferenced.
 * @return reference count after unreference on success, -1 on error.
 *
 * @see tpl_object_reference()
 * @see tpl_object_get_reference()
 */
int
tpl_object_unreference(tpl_object_t *object);

/**
 * Get reference count of a TPL object.
 *
 * @oaram object object to get reference count.
 * @return reference count on success, -1 on error.
 *
 * @see tpl_object_reference()
 * @see tpl_object_get_reference()
 */
int
tpl_object_get_reference(tpl_object_t *object);

/**
 * Get the type of a TPL object.
 *
 * @param object object to get type.
 * @return actual type of the object on success, TPL_OBJECT_ERROR on error.
 */
tpl_object_type_t tpl_object_get_type(tpl_object_t *object);

/**
 * Set user data to a TPL object.
 *
 * Users want to relate some data with a TPL object. This function provides
 * registering a pointer to such data which can be retrieved later using
 * tpl_object_get_user_data().
 * key should the pointer value itself as a key
 *
 * @param object object to set user data to.
 * @param key pointer to the key.
 * @param data pointer to the user data.
 * @param free_func free function which is used for freeing the user data when the object is destroyed.
 * @return TPL_ERROR_NONE on success, TPL_ERROR_INVALID_PARAMETER on error.
 *
 * @see tpl_object_get_user_data()
 */
tpl_result_t
tpl_object_set_user_data(tpl_object_t *object, void *key, void *data,
						 tpl_free_func_t free_func);

/**
 * Get registered user data of a TPL object.
 *
 * @param object object to get user data.
 * @param key pointer to the key.
 * @return pointer to the registered user data on success, NULL on error.
 *
 * @see tpl_object_set_user_data()
 */
void *
tpl_object_get_user_data(tpl_object_t *object, void *key);

/**
 * Create TPL display object for the given native display.
 *
 * Create a TPL display if there's no already existing TPL display for the
 * given native display. If given NULL for native_dpy, this function will
 * return default display.
 *
 * @param type backend type of the given native display.
 * @param native_dpy handle to the native display.
 * @return pointer to the display on success, NULL on failure.
 */
tpl_display_t *
tpl_display_create(tpl_backend_type_t type, tpl_handle_t native_dpy);

/**
 * Get TPL display object for the given native display.
 *
 * If there's already existing TPL display for the given native display,
 * then return the TPL display.
 *
 * @param native_dpy handle to the native display.
 * @return pointer to the display on success, NULL on failure.
 */
tpl_display_t *
tpl_display_get(tpl_handle_t native_dpy);

/**
 * Get the native display handle which the given TPL display is created for.
 *
 * @param display display to get native handle.
 * @return Handle to the native display.
 *
 * @see tpl_display_get()
 */
tpl_handle_t
tpl_display_get_native_handle(tpl_display_t *display);

/**
 * Query supported pixel formats for the given TPL display.
 *
 * Users might want to know what pixel formats are available on the given
 * display. This function is used to query such available pixel formats. Give
 * TPL_DONT_CARE to parameters for size values if any values are acceptable.
 *
 * @param display display to query pixel formats.
 * @param surface_type surface type to query for.
 * @param red_size Size of the red component in bits.
 * @param green_size Size of the green component in bits.
 * @param blue_size Size of the blue component in bits.
 * @param alpha_size Size of the alpha component in bits.
 * @param depth_size Size of a pixel in bits (Color depth).
 * @param native_visual_id Pointer to receive native visual id.
 * @param is_slow Pointer to receive whether the given config is slow.
 * @return TPL_ERROR_NONE is the given config is supported, TPL_ERROR otherwise.
 */
tpl_result_t
tpl_display_query_config(tpl_display_t *display,
						 tpl_surface_type_t surface_type,
						 int red_size, int green_size, int blue_size,
						 int alpha_size, int depth_size, int *native_visual_id,
						 tpl_bool_t *is_slow);

/**
 * Filter config according to given TPL display.
 *
 * This function modifies current config specific to the current given TPL
 * display.
 *
 * @param display display to query pixel formats.
 * @param visual_id Pointer to receive native visual id.
 * @param alpha_size Size of the alpha component in bits.
 * @return TPL_ERROR_NONE if the given config has been modified, TPL_ERROR otherwise.
 */
tpl_result_t
tpl_display_filter_config(tpl_display_t *display, int *visual_id,
						  int alpha_size);

/**
 * Create a TPL surface for the given native surface.
 *
 * @param display display used for surface creation.
 * @param handle Handle to the native surface.
 * @param type Type of the surface (Window or Pixmap).
 * @param format Pixel format of the surface.
 * @return Created surface on success, NULL otherwise.
 */
tpl_surface_t *
tpl_surface_create(tpl_display_t *display, tpl_handle_t handle,
				   tpl_surface_type_t type, tbm_format format);

/**
 * Get the TPL display where the given TPL surface was created from.
 *
 * @param surface surface to get display.
 * @return display of the given surface.
 *
 * @see tpl_surface_create()
 */
tpl_display_t *
tpl_surface_get_display(tpl_surface_t *surface);

/**
 * Get the native surface handle of the given TPL surface.
 *
 * @param surface surface to get native handle.
 * @return handle to the native surface.
 *
 * @see tpl_surface_create()
 */
tpl_handle_t
tpl_surface_get_native_handle(tpl_surface_t *surface);

/**
 * Get the type of the given TPL surface.
 *
 * @param surface surface to get type.
 * @return type of the surface.
 *
 * @see tpl_surface_create()
 */
tpl_surface_type_t
tpl_surface_get_type(tpl_surface_t *surface);

/**
 * Get the current size of the given TPL surface.
 *
 * Size of a surface might change when a user resizes window or server resizes
 * it. TPL updates such size information every time when a buffer is queried
 * using tpl_surface_dequeue_buffer(). User have to consider that there might be
 * still mismatch between actual surface size and cached one.
 *
 * @param surface surface to get size.
 * @param width pointer to receive width value.
 * @param height pointer to receive height value.
 */
tpl_result_t
tpl_surface_get_size(tpl_surface_t *surface, int *width, int *height);

/**
 * Get the current rotation-angle of the given TPL surface.
 *
 * tpl_surface's angle can be in 0, 90, 180, 270.
 *
 * @param surface surface to get rotation-angle.
 * @param rotation pointer to receive rotation-angle value.
 */
tpl_result_t
tpl_surface_get_rotation(tpl_surface_t *surface, int *rotation);

/**
 * Validate current frame of the given TPL surface.
 *
 * Users should call this function before getting actual final render target
 * buffer. Calling tpl_surface_dequeue_buffer() after calling this function might
 * give different output with previous one. Buffer returned after calling this
 * function is guaranteed to be not changing.
 *
 * @param surface surface to validate its current buffer.
 * @return TPL_FALSE if current buffer is changed due to this validation, TPL_TRUE otherwise.
 *
 * @see tpl_surface_dequeue_buffer()
 */
tpl_bool_t
tpl_surface_validate(tpl_surface_t *surface);

/**
 * Get the buffer of the current frame for the given TPL surface.
 *
 * This function returns buffer of the current frame. Depending on backend,
 * communication with the server might be required. Returned buffers are used
 * for render target to draw current frame.
 *
 * Returned buffers are valid until next tpl_surface_dequeue_buffer().
 * But if tpl_surface_validate() returns TPL_FALSE, previously returned buffers
 * should no longer be used. Then, this function will be called again before drawing,
 * and returns valid buffer.
 *
 * @param surface surface to get buffer for the current frame.
 * @return buffer for the current frame.
 *
 * Calling this function multiple times within a single frame is not guranteed
 * to return a same buffer.
 *
 * @see tpl_surface_enqueue_buffer()
 * @see tpl_surface_enqueue_buffer_with_damage()
 * @see tpl_surface_get_swapchain_buffers()
 */
tbm_surface_h
tpl_surface_dequeue_buffer(tpl_surface_t *surface);

/**
 * Get the buffer of the current frame for the given TPL surface.
 *
 * This function returns buffer of the current frame. Depending on backend,
 * communication with the server might be required.
 * Returned buffers can used for render target after sync_fence signaled.
 *
 * @param surface surface to get buffer for the current frame.
 * @param timeout_ns block timeout nano seconds.
 * @param sync_fence signal when buffer is valid.
 * @return buffer for the current frame or NULL when timeout.
 *
 * @see tpl_surface_enqueue_buffer()
 * @see tpl_surface_enqueue_buffer_with_damage()
 * @see tpl_surface_get_swapchain_buffers()
 */
tbm_surface_h
tpl_surface_dequeue_buffer_with_sync(tpl_surface_t *surface,
									 uint64_t timeout_ns,
									 tbm_fd *sync_fence);

/**
 * Post a given tbm_surface.
 *
 * This function request display server to post a frame. This is the
 * function which can enqueue a buffer to the tbm_surface_queue.
 *
 * Make sure this function is called exactly once for a frame.
 * Scheduling post calls on a separate thread is recommended.
 *
 * @param surface surface to post a frame.
 * @param tbm_surface buffer to post.
 *
 * @see tpl_surface_enqueue_buffer_with_damage()
 * @see tpl_surface_dequeue_buffer()
 * @see tpl_surface_get_swapchain_buffers()
 */
tpl_result_t
tpl_surface_enqueue_buffer(tpl_surface_t *surface, tbm_surface_h tbm_surface);

/**
 * Post a given tbm_surface with region of damage.
 *
 * Damage information is used for reducing number of pixels composited in the
 * compositor. Setting num_rects to 0 or rects to NULL means entire area is damaged.
 *
 * This function request display server to post a frame.
 * This function is identical with tpl_surface_enqueue_buffer except delivering
 * the damage information for updating.
 *
 * Make sure this function is called exactly once for a frame.
 * Scheduling post calls on a separate thread is recommended.
 *
 * @param surface surface to post a frame.
 * @param tbm_surface buffer to post.
 * @param num_rects the number of rectangles of the damage region.
 * @param rects pointer to coordinates of rectangles. x0, y0, w0, h0, x1, y1, w1, h1...
 *
 * @see tpl_surface_enqueue_buffer()
 * @see tpl_surface_dequeue_buffer()
 * @see tpl_surface_get_swapchain_buffers()
 */
tpl_result_t
tpl_surface_enqueue_buffer_with_damage(tpl_surface_t *surface,
									   tbm_surface_h tbm_surface,
									   int num_rects, const int *rects);

/**
 * Post a given tbm_surface with region of damage.
 *
 * Damage information is used for reducing number of pixels composited in the
 * compositor. Setting num_rects to 0 or rects to NULL means entire area is damaged.
 *
 * This function request display server to post a frame.
 * This function is identical with tpl_surface_enqueue_buffer except delivering
 * the damage information for updating.
 * This function will be wait internally until signaled sync.
 *
 * Make sure this function is called exactly once for a frame.
 * Scheduling post calls on a separate thread is recommended.
 *
 * @param surface surface to post a frame.
 * @param tbm_surface buffer to post.
 * @param num_rects the number of rectangles of the damage region.
 * @param rects pointer to coordinates of rectangles. x0, y0, w0, h0, x1, y1, w1, h1...
 * @param sync_fence signal when draw done.
 *
 * @see tpl_surface_enqueue_buffer()
 * @see tpl_surface_dequeue_buffer()
 * @see tpl_surface_get_swapchain_buffers()
 */
tpl_result_t
tpl_surface_enqueue_buffer_with_damage_and_sync(tpl_surface_t *surface,
												tbm_surface_h tbm_surface,
												int num_rects, const int *rects,
												tbm_fd sync_fence);
/**
 * Set frame interval of the given TPL surface.
 *
 * Frame interval ensures that only a single frame is posted within the
 * specified vsync intervals. When a frame ends, the frame's interval is set to
 * the surface's current interval.
 *
 * @param surface surface to set frame interval.
 * @param interval minimum number of vsync between frames.
 *
 * @see tpl_surface_get_post_interval()
 */
tpl_result_t
tpl_surface_set_post_interval(tpl_surface_t *surface, int interval);

/**
 * Get frame interval of the given TPL surface.
 *
 * @param surface surface to get frame interval.
 * @return frame interval.
 *
 * @see tpl_surface_set_post_interval()
 */
int
tpl_surface_get_post_interval(tpl_surface_t *surface);

/**
 * Create a swapchain for the given TPL surface.
 *
 * This function creates buffers for swapchain which is binded to the given tpl surface.
 *
 * @param surface surface used for creation of swapchain buffers.
 * @param format Pixel format of the swapchain
 * @param width width to the swapchain buffer.
 * @param height height to the swapchain buffer.
 * @param buffer_count buffer count to the swapchain.
 * @param buffer present mode to the swapchain.
 * @return TPL_ERROR_NONE if this function is supported and the tpl_surface is valid, TPL_ERROR otherwise.
 *
 * @see tpl_surface_get_swapchain_buffers()
 * @see tpl_surface_destroy_swapchain()
 * @see tpl_display_present_mode_t
 */
tpl_result_t
tpl_surface_create_swapchain(tpl_surface_t *surface, tbm_format format,
							 int width, int height, int buffer_count, int present_mode);

/**
 * Destroy a swapchain for the given TPL surface.
 *
 * @param surface surface used for destroying of swapchain buffers.
 * @return TPL_ERROR_NONE if this function is supported and the tpl_surface is valid, TPL_ERROR otherwise.
 *
 * @see tpl_surface_create_swapchain()
 * @see tpl_surface_get_swapchain_buffers()
 */
tpl_result_t
tpl_surface_destroy_swapchain(tpl_surface_t *surface);

/**
 * Get the swapchain buffer list of the given TPL surface.
 *
 * This function returns the swapchain buffer list of binding to given TPL surface. Depending on backend,
 * communication with the server might be required. Returned buffers are used
 * for render target to draw frame.
 *
 * Returned buffers are valid until tpl_surface_enqueue_buffer().
 * But if tpl_surface_validate() returns TPL_FALSE, previously returned buffers
 * should no longer be used.
 * caller reposible for free of the buffer list (only buffer list, do not free buffer list's contents)
 *
 * @param surface surface to get buffer list.
 * @param buffers pointer to receive tbm_surface list value.
 * @param buffer_count pointer to receive buffer_count value.
 * @return TPL_ERROR_NONE if this function is supported and the tpl_surface is valid, TPL_ERROR otherwise.
 *
 * @see tpl_surface_enqueue_buffer()
 * @see tpl_surface_enqueue_buffer_with_damage()
 * @see tpl_surface_dequeue_buffer()
 * @see tpl_surface_create_swapchain()
 * @see tpl_surface_destroy_swapchain()
 */
tpl_result_t
tpl_surface_get_swapchain_buffers(tpl_surface_t *surface,
								  tbm_surface_h **buffers, int *buffer_count);

/**
 * Query information on the given native window.
 *
 * @param display display used for query.
 * @param window handle to the native window.
 * @param width pointer to receive width of the window.
 * @param height pointer to receive height of the window.
 * @param format pointer to receive format of the window.
 * @return TPL_ERROR_NONE if the window is valid, TPL_ERROR otherwise.
 */
tpl_result_t
tpl_display_get_native_window_info(tpl_display_t *display, tpl_handle_t window,
								   int *width, int *height, tbm_format *format,
								   int depth, int a_size);

/**
 * Query information on the given native pixmap.
 *
 * @param display display used for query.
 * @param pixmap handle to the native pixmap.
 * @param width pointer to receive width of the pixmap.
 * @param height pointer to receive height of the pixmap.
 * @param format pointer to receive format of the pixmap.
 * @return TPL_ERROR_NONE if the pixmap is valid, TPL_ERROR otherwise.
 */
tpl_result_t
tpl_display_get_native_pixmap_info(tpl_display_t *display, tpl_handle_t pixmap,
								   int *width, int *height, tbm_format *format);


/**
 * Get the buffer count capability of the given native window.
 *
 * @param display display used for query.
 * @param window window used for query the buffer count capability
 * @paran min pointer to receive min buffer count value.
 * @paran max pointer to receive max buffer count value.
 * @return TPL_ERROR_NONE if this function is supported and the window is valid, TPL_ERROR otherwise.
 */
tpl_result_t
tpl_display_query_supported_buffer_count_from_native_window(tpl_display_t *display,
															tpl_handle_t window,
															int *min, int *max);

/**
 * Get native buffer from the given native pixmap.
 *
 * @param display display used for query.
 * @param pixmap handle of the native pixmap.
 * @return tbm_surface_h native buffer.
 */
tbm_surface_h
tpl_display_get_buffer_from_native_pixmap(tpl_display_t *display,
										  tpl_handle_t pixmap);

/**
 * Set frontbuffer mode to render to only frontbuffer.
 *
 * @param surface surface to set mode.
 * @param set TPL_TRUE if user want to set tpl_surface to frontbuffer mode.
 * @return TPL_ERROR_NONE if tpl_surface is valid.
 */
tpl_result_t
tpl_surface_set_frontbuffer_mode(tpl_surface_t *surface, tpl_bool_t set);

/**
 * Set callback function to tpl_surface for receiving reset information.
 *
 * This function should be called when after tpl_surface_create() if
 * frontend surface want to know that if surface reset or not.
 *
 * @param surface surface to set callback. It has to be not NULL.
 * @param data data to be delivered to callback function.
 * @param reset_cb pointer value of callback funtion.
 *  - It can be NULL if frontend wants to delete callback function.
 * @return TPL_ERROR_NONE if tpl_surface is valid.
 */
tpl_result_t
tpl_surface_set_reset_cb(tpl_surface_t *surface,
						 void* data, tpl_surface_cb_func_t reset_cb);

/**
 * Set rotation capability to the given tpl_surface
 * @param surface surface used for set
 * @param set TPL_TRUE if user want to set to enable / disable capability of rotation
 */
tpl_result_t
tpl_surface_set_rotation_capability(tpl_surface_t *surface, tpl_bool_t set);

/**
 * Present mode types.
 *
 * @TPL_DISPLAY_MODE_IMMEDIATE_KHR: The presentation engine does not wait for
 *		a vertical blanking period to update the current image, meaning this
 *		mode may result in visible tearing.
 *		No internal queuing of presentation requests is needed,
 *		as the requests are applied immediately.
 * @TPL_DISPLAY_MODE_MAILBOX_KHR: The presentation engine waits for the next
 *		vertical blanking period to update the current image.
 *		Tearing cannot be observed. An internal single-entry queue is used
 *		to hold pending presentation requests. If the queue is full when
 *		a new presentation request is received, the new request replaces the
 *		existing entry, and any images associated with the prior entry become
 *		available for re-use by the application. One request is removed from
 *		the queue and processed during each vertical blanking period in
 *		which the queue is non-empty.
 * @TPL_DISPLAY_MODE_FIFO_KHR: The presentation engine waits for the next
 *		vertical blanking period to update the current image.
 *		Tearing cannot be observed. An internal queue is used to hold pending
 *		presentation requests. New requests are appended to the end of the queue,
 *		and one request is removed from the beginning of the queue and processed
 *		during each vertical blanking period in which the queue is non-empty.
 *		This is the only value of presentMode that is required to be supported.
 * @TPL_DISPLAY_MODE_FIFO_RELAXED_KHR: The presentation engine generally waits
 *		for the next vertical blanking period to update the current image.
 *		If a vertical blanking period has already passed since the last update
 *		of the current image then the presentation engine does not wait for
 *		another vertical blanking period for the update, meaning this mode may
 *		result in visible tearing in this case. This mode is useful for reducing
 *		visual stutter with an application that will mostly present
 *		a new image before the next vertical blanking period, but may
 *		occasionally be late, and present a new image just after the next
 *		vertical blanking period.
 *		An internal queue is used to hold pending presentation requests.
 *		New requests are appended to the end of the queue,
 *		and one request is removed from the beginning of the queue and processed
 *		during or after each vertical blanking period in which the queue is non-empty.
 * default TPL_DISPLAY_PRESENT_MODE_MAILBOX
 *
 * @see tpl_display_query_supported_present_mode_from_native_window
 */
typedef enum {
	TPL_DISPLAY_PRESENT_MODE_MAILBOX = 1,
	TPL_DISPLAY_PRESENT_MODE_FIFO = 2,
	TPL_DISPLAY_PRESENT_MODE_IMMEDIATE = 4,
	TPL_DISPLAY_PRESENT_MODE_FIFO_RELAXED = 8,
	TPL_DISPLAY_PRESENT_MODE_MAX,
} tpl_display_present_mode_t;

/**
 * Get the present mode capability of the given native window.
 *
 * @param display display used for query.
 * @param window window used for query the present mode capability
 * @paran modes pointer to receive present modes bitwise or.
 * @return TPL_ERROR_NONE if this function is supported and the window is valid, TPL_ERROR otherwise.
 *
 * @see tpl_display_present_mode_t
 */
tpl_result_t
tpl_display_query_supported_present_modes_from_native_window(tpl_display_t *display,
															 tpl_handle_t window,
															 int *modes);
#endif /* TPL_H */
