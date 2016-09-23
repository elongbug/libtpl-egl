#ifndef TIZEN_SURFACE_CLIENT_PROTOCOL_H
#define TIZEN_SURFACE_CLIENT_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-client.h"

struct wl_client;
struct wl_resource;

struct tizen_surface_shm;
struct tizen_surface_shm_flusher;

extern const struct wl_interface tizen_surface_shm_interface;
extern const struct wl_interface tizen_surface_shm_flusher_interface;

#define TIZEN_SURFACE_SHM_GET_FLUSHER	0

static inline void
tizen_surface_shm_set_user_data(struct tizen_surface_shm *tizen_surface_shm,
								void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) tizen_surface_shm, user_data);
}

static inline void *
tizen_surface_shm_get_user_data(struct tizen_surface_shm *tizen_surface_shm)
{
	return wl_proxy_get_user_data((struct wl_proxy *) tizen_surface_shm);
}

static inline void
tizen_surface_shm_destroy(struct tizen_surface_shm *tizen_surface_shm)
{
	wl_proxy_destroy((struct wl_proxy *) tizen_surface_shm);
}

static inline struct tizen_surface_shm_flusher *
tizen_surface_shm_get_flusher(struct tizen_surface_shm *tizen_surface_shm,
							  struct wl_surface *surface)
{
	struct wl_proxy *id;

	id = wl_proxy_marshal_constructor((struct wl_proxy *) tizen_surface_shm,
									  TIZEN_SURFACE_SHM_GET_FLUSHER, &tizen_surface_shm_flusher_interface, NULL,
									  surface);

	return (struct tizen_surface_shm_flusher *) id;
}

struct tizen_surface_shm_flusher_listener {
	/**
	 * flush - (none)
	 */
	void (*flush)(void *data,
				  struct tizen_surface_shm_flusher *tizen_surface_shm_flusher);
};

static inline int
tizen_surface_shm_flusher_add_listener(struct tizen_surface_shm_flusher
									   *tizen_surface_shm_flusher,
									   const struct tizen_surface_shm_flusher_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) tizen_surface_shm_flusher,
								 (void (* *)(void)) listener, data);
}

#define TIZEN_SURFACE_SHM_FLUSHER_DESTROY	0

static inline void
tizen_surface_shm_flusher_set_user_data(struct tizen_surface_shm_flusher
										*tizen_surface_shm_flusher, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) tizen_surface_shm_flusher,
						   user_data);
}

static inline void *
tizen_surface_shm_flusher_get_user_data(struct tizen_surface_shm_flusher
										*tizen_surface_shm_flusher)
{
	return wl_proxy_get_user_data((struct wl_proxy *) tizen_surface_shm_flusher);
}

static inline void
tizen_surface_shm_flusher_destroy(struct tizen_surface_shm_flusher
								  *tizen_surface_shm_flusher)
{
	wl_proxy_marshal((struct wl_proxy *) tizen_surface_shm_flusher,
					 TIZEN_SURFACE_SHM_FLUSHER_DESTROY);

	wl_proxy_destroy((struct wl_proxy *) tizen_surface_shm_flusher);
}

#ifdef  __cplusplus
}
#endif

#endif
