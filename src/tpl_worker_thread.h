#ifndef TPL_WORKER_THREAD_H
#define TPL_WORKER_THREAD_H

#include "tpl.h"
#include <tbm_surface.h>
#include <tbm_surface_queue.h>
#include <tbm_sync.h>
#include <pthread.h>

typedef struct __tpl_worker_surface tpl_worker_surface_t;

struct __tpl_worker_surface {
	tpl_surface_t *surface;
	tbm_surface_queue_h tbm_queue;

	void (*draw_done)(tpl_surface_t *surface, tbm_surface_h tbm_surface, tpl_result_t result);
	int (*draw_wait_fd_get)(tpl_surface_t *surface, tbm_surface_h tbm_surface);
	void (*vblank)(tpl_surface_t *surface, unsigned int sequence, unsigned int tv_sec,
				   unsigned int tv_usec);
	tbm_surface_h (*draw_wait_buffer_get)(tpl_surface_t *surface);

	tbm_surface_h draw_wait_buffer;
};

tpl_bool_t __tpl_worker_support_vblank();
void __tpl_worker_surface_list_insert(tpl_worker_surface_t *surface);
void __tpl_worker_surface_list_remove(tpl_worker_surface_t *surface);
void __tpl_worker_new_buffer_notify(tpl_worker_surface_t *surface);

#endif //TPL_WORKER_THREAD_H
