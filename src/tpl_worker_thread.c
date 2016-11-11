#include "tpl_worker_thread.h"
#include "tpl_internal.h"

#include <unistd.h>
/*#define __USE_GNU*/
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <tdm_client.h>

#define TPL_ERR_ERRNO(f, x...)								\
	do { int err = errno; char buf[256] = {0,};				\
		strerror_r(err, buf, 255);							\
		TPL_ERR(f " | error: %d(%s)", ##x, err, buf);		\
	} while (0);

#define TPL_WARN_ERRNO(f, x...)								\
	do { int err = errno; char buf[256] = {0,};				\
		strerror_r(err, buf, 255);							\
		TPL_WARN(f " | error: %d(%s)", ##x, err, buf);		\
	} while (0);

static struct {
	int running;
	int epoll_fd;
	int event_fd;

	pthread_t worker_id;
	tpl_list_t surface_list;
	pthread_mutex_t surface_mutex;
	tpl_bool_t support_vblank;
} tpl_worker_thread;

tpl_bool_t
__tpl_worker_support_vblank()
{
	return tpl_worker_thread.support_vblank;
}

void
__tpl_worker_surface_list_insert(tpl_worker_surface_t *surface)
{
	TPL_ASSERT(surface->surface);
	TPL_ASSERT(surface->tbm_queue);

	if (pthread_mutex_lock(&tpl_worker_thread.surface_mutex) != 0) {
		TPL_ERR_ERRNO("surface list mutex lock failed");
		return;
	}

	surface->draw_wait_buffer = NULL;

	if (pthread_mutex_init(&surface->mutex, NULL) != 0)
		TPL_ERR_ERRNO("surface mutex init failed");

	__tpl_list_push_back(&tpl_worker_thread.surface_list, surface);

	pthread_mutex_unlock(&tpl_worker_thread.surface_mutex);
}

void
__tpl_worker_surface_list_remove(tpl_worker_surface_t *surface)
{
	if (pthread_mutex_lock(&tpl_worker_thread.surface_mutex) != 0) {
		TPL_ERR_ERRNO("surface list mutex lock failed");
		return;
	}

	__tpl_list_remove_data(&tpl_worker_thread.surface_list, surface,
						   TPL_FIRST, NULL);

	if (pthread_mutex_lock(&surface->mutex) != 0)
		TPL_ERR_ERRNO("surface list mutex lock failed");

	if (surface->draw_wait_buffer) {
		int wait_fd;

		wait_fd = surface->draw_wait_fd_get(surface->surface,
											surface->draw_wait_buffer);
		if (wait_fd != -1)
			epoll_ctl(tpl_worker_thread.epoll_fd, EPOLL_CTL_DEL, wait_fd, NULL);
		surface->draw_wait_buffer = NULL;
	}
	pthread_mutex_unlock(&surface->mutex);

	if (pthread_mutex_destroy(&surface->mutex) != 0)
		TPL_ERR_ERRNO("surface mutex init failed");

	pthread_mutex_unlock(&tpl_worker_thread.surface_mutex);
}

static void
__tpl_worker_event_send()
{
	int len;
	uint64_t dummy_event = 1;

	if (tpl_worker_thread.event_fd == -1) {
		TPL_ERR("worker thread not working");
		return;
	}

	len = write(tpl_worker_thread.event_fd,
				&dummy_event,
				sizeof(dummy_event));
	if (len < 0)
		TPL_WARN_ERRNO("event fd(%d) write failed.",
					   tpl_worker_thread.event_fd);
}

static void
__tpl_worker_prepare_draw_wait_buffer(int epoll_fd,
									  tpl_worker_surface_t *surface)
{
	if (surface->draw_wait_buffer)
		return;

	while (tbm_surface_queue_can_acquire(surface->tbm_queue, 0)) {
		tbm_surface_h tbm_surface = NULL;
		tbm_surface_queue_error_e tsq_err;
		int wait_fd = -1;

		tsq_err = tbm_surface_queue_acquire(surface->tbm_queue, &tbm_surface);
		if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE || tbm_surface == NULL) {
			TPL_ERR("Failed to acquire tbm_surface. | tsq_err = %d", tsq_err);
			return;
		}

		if (surface->draw_wait_fd_get)
			wait_fd = surface->draw_wait_fd_get(surface->surface, tbm_surface);

		if (wait_fd != -1) {
			struct epoll_event wait_fence_event;
			int epoll_err;

			wait_fence_event.events = EPOLLIN;
			wait_fence_event.data.ptr = surface;
			epoll_err = epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
								  wait_fd,
								  &wait_fence_event);
			if (epoll_err == 0) {
				surface->draw_wait_buffer = tbm_surface;
				return;
			}
		} /* else can't(or not need) wait fence in poll */

		if (surface->draw_done)
			surface->draw_done(surface->surface, tbm_surface,
							   TPL_ERROR_INVALID_OPERATION);
	}
}

void
__tpl_worker_new_buffer_notify(tpl_worker_surface_t *surface)
{
	TPL_ASSERT(surface->surface);

	if (pthread_mutex_lock(&surface->mutex) != 0)
		TPL_ERR_ERRNO("surface list mutex lock failed");

	__tpl_worker_prepare_draw_wait_buffer(tpl_worker_thread.epoll_fd, surface);

	pthread_mutex_unlock(&surface->mutex);
}

static tpl_bool_t
__tpl_worker_regist_vblank_handler(tdm_client_vblank *tdm_vblank);

static void
__tpl_worker_cb_vblank(tdm_client_vblank *tdm_vblank, tdm_error error,
					   unsigned int sequence, unsigned int tv_sec,
					   unsigned int tv_usec, void *user_data)
{
	tpl_list_node_t *trail;

	if (pthread_mutex_lock(&tpl_worker_thread.surface_mutex) != 0) {
		TPL_ERR_ERRNO("surface list mutex lock failed");
		return;
	}

	for (trail = __tpl_list_get_front_node(&tpl_worker_thread.surface_list);
		 trail != NULL;
		 trail = __tpl_list_node_next(trail)) {
		tpl_worker_surface_t *surface;

		surface = __tpl_list_node_get_data(trail);

		if (pthread_mutex_lock(&surface->mutex) != 0)
			TPL_ERR_ERRNO("surface list mutex lock failed");

		if (surface->vblank)
			surface->vblank(surface->surface, sequence, tv_sec, tv_usec);

		pthread_mutex_unlock(&surface->mutex);
	}
	pthread_mutex_unlock(&tpl_worker_thread.surface_mutex);

	__tpl_worker_regist_vblank_handler(tdm_vblank);
}

static tpl_bool_t
__tpl_worker_regist_vblank_handler(tdm_client_vblank *tdm_vblank)
{
	tdm_error tdm_err;

	tdm_err = tdm_client_vblank_wait(tdm_vblank,
									 1, /* interval */
									 __tpl_worker_cb_vblank, /* handler */
									 NULL);
	if (tdm_err != TDM_ERROR_NONE) {
		TPL_ERR ("Failed to tdm_client_wait_vblank. error:%d", tdm_err);
		return TPL_FALSE;
	}
	return TPL_TRUE;
}

static int
__tpl_worker_prepare_event_fd(int epoll_fd)
{
	int event_fd;
	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.ptr = &tpl_worker_thread;

	event_fd = eventfd(0, EFD_CLOEXEC);
	if (event_fd == -1) {
		TPL_ERR_ERRNO("eventfd() failed");
		return -1;
	}

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &event) != 0) {
		TPL_ERR_ERRNO("eventfd epoll ctl epoll_fd: %d, event_fd: %d.",
					  epoll_fd, tpl_worker_thread.event_fd);
		close(event_fd);
		return -1;
	}
	return event_fd;
}

static tpl_bool_t
__tpl_worker_prepare_vblank(int epoll_fd, tdm_client **ret_client, tdm_client_vblank **ret_vblank)
{
	tdm_error tdm_err;
	tdm_client *tdm_client = NULL;
	tdm_client_output *tdm_output = NULL;
	tdm_client_vblank *tdm_vblank = NULL;
	int tdm_fd, ret;
	struct epoll_event event;

	TPL_ASSERT(ret_client);
	TPL_ASSERT(ret_vblank);

	tdm_client = tdm_client_create(&tdm_err);
	if (!tdm_client) {
		TPL_ERR("tdm_client_create failed | tdm_err: %d\n", tdm_err);
		goto error_cleanup;
	}

	tdm_err = tdm_client_get_fd(tdm_client, &tdm_fd);
	if (tdm_err != TDM_ERROR_NONE || tdm_fd < 0) {
		TPL_ERR("tdm_client_get_fd failed | tdm_err: %d\n", tdm_err);
		goto error_cleanup;
	}

	tdm_output = tdm_client_get_output(tdm_client, "primary", &tdm_err);
	if (!tdm_output) {
		TPL_ERR("Failed to get tdm client output. tdm_err(%d)", tdm_err);
		goto error_cleanup;
	}

	tdm_vblank = tdm_client_output_create_vblank(tdm_output, &tdm_err);
	if (!tdm_vblank) {
		TPL_ERR("Failed to create tdm vblank output. tdm_err(%d)", tdm_err);
		goto error_cleanup;
	}

	tdm_client_vblank_set_enable_fake(tdm_vblank, 1);
	tdm_client_vblank_set_sync(tdm_vblank, 0);

	if (__tpl_worker_regist_vblank_handler(tdm_vblank) == TPL_FALSE)
		goto error_cleanup;

	event.events = EPOLLIN;
	event.data.ptr = tdm_client;
	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tdm_fd, &event);
	if (ret != 0) {
		TPL_ERR_ERRNO("tdm epoll ctl epoll_fd: %d, tdm_fd: %d.",
					  epoll_fd, tdm_fd);
		goto error_cleanup;
	}

	*ret_vblank = tdm_vblank;
	*ret_client = tdm_client;

	return TPL_TRUE;

error_cleanup:
	if (tdm_vblank)
		tdm_client_vblank_destroy(tdm_vblank);
	if (tdm_client)
		tdm_client_destroy(tdm_client);
	return TPL_FALSE;
}

static void *
__tpl_worker_thread_loop(void *arg)
{
#define EPOLL_MAX_SIZE 100
	int ret, epoll_fd = epoll_create(EPOLL_MAX_SIZE);
	struct epoll_event ev_list[EPOLL_MAX_SIZE];
	tdm_client *tdm_client = NULL;
	tdm_client_vblank *tdm_vblank = NULL;

	if (epoll_fd == -1) {
		TPL_ERR_ERRNO("epoll create failed");
		goto cleanup;
	}

	/* event fd */
	tpl_worker_thread.event_fd = __tpl_worker_prepare_event_fd(epoll_fd);
	if (tpl_worker_thread.event_fd == -1)
		goto cleanup;

	/* vblank fd */
	/* FIXME: vblank has performance problem */
	/*if (__tpl_worker_prepare_vblank(epoll_fd, &tdm_client, &tdm_vblank))
		tpl_worker_thread.support_vblank = TPL_TRUE;*/
	tpl_worker_thread.support_vblank = TPL_TRUE;

	while(tpl_worker_thread.running) {
		int i;

		/* wait events */
		ret = epoll_wait(epoll_fd, ev_list, EPOLL_MAX_SIZE, -1);
		if (ret == -1) {
			TPL_ERR_ERRNO("epoll fd: %d.", epoll_fd);
			continue;
		}

		for (i = 0; i < ret; i++) {
			if (ev_list[i].data.ptr == &tpl_worker_thread) {
				/* thread terminate event */
				if (ev_list[i].events & EPOLLIN) {
					int len;
					uint64_t read_buf;

					len = read(tpl_worker_thread.event_fd,
						 &read_buf, sizeof(uint64_t));
					if (len < 0) {
						TPL_WARN_ERRNO("event fd(%d) read failed.",
									   tpl_worker_thread.event_fd);
						continue;
					} else {
						break;
					}
				}
			} else if (ev_list[i].data.ptr == tdm_client) {
				/* vblank */
				tdm_client_handle_events(tdm_client);
				/* process in __tpl_worker_cb_vblank */
			} else {
				/* draw done */
				tpl_worker_surface_t *surface = ev_list[i].data.ptr;

				if (!(ev_list[i].events & EPOLLIN))
					continue;

				if (pthread_mutex_lock(&surface->mutex) != 0)
					TPL_ERR_ERRNO("surface list mutex lock failed");

				if (surface->draw_wait_buffer) {
					int wait_fd;

					wait_fd = surface->draw_wait_fd_get(surface->surface,
														surface->draw_wait_buffer);
					if (wait_fd == -1) {
						if (surface->draw_done)
							surface->draw_done(surface->surface, surface->draw_wait_buffer,
											   TPL_ERROR_INVALID_OPERATION);
						surface->draw_wait_buffer = NULL;
					} else {
						int fence_result;

						switch (fence_result = tbm_sync_fence_wait(wait_fd, 0)) {
							case 0:
								TPL_ERR_ERRNO("sync_fence_wait return error.");
								break;
							case 1:
								/* some time recieve event two times */
								epoll_ctl(epoll_fd, EPOLL_CTL_DEL, wait_fd, NULL);
								if (surface->draw_done)
									surface->draw_done(surface->surface,
													   surface->draw_wait_buffer,
													   fence_result == 1 ?
													   TPL_ERROR_NONE :
													   TPL_ERROR_INVALID_OPERATION);
								surface->draw_wait_buffer = NULL;
								break;
							case -1:
								TPL_WARN("sync_fence_wait return timeout.");
								break;
						}
					}
				} else {
					TPL_WARN("recieve already signaled event\n");
				}

				if (surface->draw_wait_buffer == NULL)
					__tpl_worker_prepare_draw_wait_buffer(epoll_fd, surface);
				pthread_mutex_unlock(&surface->mutex);
			}
		}
	}

cleanup:
	/* thread cleanup */
	if (tdm_vblank)
		tdm_client_vblank_destroy(tdm_vblank);
	if (tdm_client)
		tdm_client_destroy(tdm_client);

	if (epoll_fd != -1) {
		close(epoll_fd);
		epoll_fd = -1;
	}
	if (tpl_worker_thread.event_fd != -1) {
		close(tpl_worker_thread.event_fd);
		tpl_worker_thread.event_fd = -1;
	}

	return NULL;
}

static void __attribute__((constructor))
__tpl_worker_init(void)
{
	/*
	 * It can be move to display or surface create function
	 * with pthread_once
	 */
	tpl_worker_thread.running = 1;
	tpl_worker_thread.support_vblank = TPL_FALSE;

	if (pthread_mutex_init(&tpl_worker_thread.surface_mutex, NULL) != 0) {
		TPL_ERR_ERRNO("surface list mutex init failed");
		goto error;
	}

	__tpl_list_init(&tpl_worker_thread.surface_list);

	if (pthread_create(&tpl_worker_thread.worker_id, NULL,
				   __tpl_worker_thread_loop,
				   NULL) != 0) {
		TPL_ERR_ERRNO("worker thread create failed");
		goto error_thread_create;
	}
	/*pthread_setname_np(tpl_worker_thread.worker_id, "tpl_worker_thread");*/

	return;

error_thread_create:
	pthread_mutex_destroy(&tpl_worker_thread.surface_mutex);

error:
	tpl_worker_thread.running = 0;
}

static void __attribute__((destructor))
__tpl_worker_fini(void)
{
	if (tpl_worker_thread.running == 0)
		return;

	/* deinitailize global object */
	tpl_worker_thread.running = 0;

	/* maybe EPOLLRDHUP not work with eventfd */
	/* close(tpl_worker_thread.event_fd); */
	__tpl_worker_event_send();

	if (__tpl_list_get_count(&tpl_worker_thread.surface_list))
		TPL_WARN("called destructor, but tpl surface count: %d",
				 __tpl_list_get_count(&tpl_worker_thread.surface_list));

	pthread_join(tpl_worker_thread.worker_id, NULL);
	pthread_mutex_destroy(&tpl_worker_thread.surface_mutex);
}
