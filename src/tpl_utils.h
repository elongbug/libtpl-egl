#ifndef TPL_UTILS_H
#define TPL_UTILS_H

#include "tpl.h"
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>

#if defined(__GNUC__) && __GNUC__ >= 4
#   define TPL_API __attribute__ ((visibility("default")))
#else
#   define TPL_API
#endif
#define TPL_ASSERT(expr) assert(expr)
#define TPL_INLINE __inline__
#define TPL_IGNORE(x) (void)x

#ifdef ARM_ATOMIC_OPERATION
#define TPL_DMB() __asm__ volatile("dmb sy" : : : "memory")
#else /* ARM_ATOMIC_OPERATION */
#define TPL_DMB() __asm__ volatile("" : : : "memory")
#endif /* ARM_ATOMIC_OPERATION */

#if (TTRACE_ENABLE)
#include <ttrace.h>
#define TRACE_BEGIN(name,...) traceBegin(TTRACE_TAG_GRAPHICS, name, ##__VA_ARGS__)
#define TRACE_END() traceEnd(TTRACE_TAG_GRAPHICS)
#define TRACE_ASYNC_BEGIN(key, name,...) traceAsyncBegin(TTRACE_TAG_GRAPHICS, key, name, ##__VA_ARGS__)
#define TRACE_ASYNC_END(key, name,...) traceAsyncEnd(TTRACE_TAG_GRAPHICS, key, name, ##__VA_ARGS__)
#define TRACE_COUNTER(value, name,...) traceCounter(TTRACE_TAG_GRAPHICS, value, name, ##__VA_ARGS__)
#define TRACE_MARK(name,...) traceMark(TTRACE_TAG_GRAPHICS, name, ##__VA_ARGS__)
#else /* TTRACE_ENABLE */
#define TRACE_BEGIN(name,...)
#define TRACE_END()
#define TRACE_ASYNC_BEGIN(key, name,...)
#define TRACE_ASYNC_END(key, name,...)
#define TRACE_COUNTER(value, name,...)
#define TRACE_MARK(name,...)
#endif /* TTRACE_ENABLE */

#ifndef NDEBUG
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include <tbm_surface.h>
#include <tbm_surface_internal.h>

/* 0:uninitialized, 1:initialized,no log, 2:user log */
extern unsigned int tpl_log_lvl;
extern unsigned int tpl_log_initialized;
extern unsigned int tpl_dump_lvl;

#ifdef DLOG_DEFAULT_ENABLE
#define LOG_TAG "TPL"
#include <dlog.h>
#define TPL_LOG_F(f, x...)		LOGD(f, ##x)
#define TPL_LOG_B(b, f, x...)	LOGD(f, ##x)
#define TPL_DEBUG(f, x...)		LOGD(f, ##x)
#define TPL_ERR(f, x...)		LOGE(f, ##x)
#define TPL_WARN(f, x...)		LOGW(f, ##x)
#else /* DLOG_DEFAULT_ENABLE */
#ifdef LOG_DEFAULT_ENABLE
#define TPL_LOG_F(f, x...)								\
	fprintf(stderr, "[TPL_F(%d):%s(%d)] " f "\n",		\
			getpid(), __func__, __LINE__, ##x)
#define TPL_LOG_B(b, f, x...)							\
	fprintf(stderr,	"[TPL_" b "(%d):%s(%d)] " f "\n",	\
			getpid(), __FILE__, __LINE__, ##x)
#define TPL_DEBUG(f, x...)								\
	fprintf(stderr,	"[TPL_D(%d):%s(%d)] " f "\n",		\
			getpid(), __func__, __LINE__, ##x)
#else /* LOG_DEFAULT_ENABLE */
/*
 * TPL_LOG_LEVEL
 * export TPL_LOG_LEVEL=[lvl]
 * -1: uninitialized.
 * 0: initialized but do not print any log.
 * 1: enable only frontend API logs. TPL_LOG_F
 * 2: enable also backend API logs. TPL_LOG_B (detail info about window system)
 * 3: enable also debug logs. TPL_DEBUG
 * 4: enable only debug logs. TPL_DEBUG
 */
#define LOG_INIT()										\
	{													\
		if (!tpl_log_initialized)						\
		{												\
			char *env = getenv("TPL_LOG_LEVEL");		\
			if (env == NULL)							\
				tpl_log_lvl = 0;						\
			else										\
				tpl_log_lvl = atoi(env);				\
			tpl_log_initialized = 1;					\
		}												\
	}

#define TPL_LOG_F(f, x...)								\
	{													\
		LOG_INIT();										\
		if (tpl_log_lvl > 0 && tpl_log_lvl < 4)			\
			fprintf(stderr, "[TPL_F(%d):%s(%d)] " f "\n",\
					getpid(), __func__, __LINE__, ##x);	\
	}

#define TPL_LOG_B(b, f, x...)								\
	{														\
		LOG_INIT();											\
		if (tpl_log_lvl > 1 && tpl_log_lvl < 4)				\
			fprintf(stderr,	"[TPL_" b "(%d):%s(%d)] " f "\n",\
					getpid(), __FILE__, __LINE__, ##x);		\
	}

#define TPL_DEBUG(f, x...)								\
	{													\
		LOG_INIT();										\
		if (tpl_log_lvl > 2)							\
			fprintf(stderr,	"[TPL_D(%d):%s(%d)] " f "\n",\
					getpid(), __func__, __LINE__, ##x);	\
	}

#endif /* LOG_DEFAULT_ENABLE */

#define TPL_ERR(f, x...)								\
	fprintf(stderr,										\
			"[TPL_ERR(%d):%s(%d)] " f "\n",				\
			getpid(), __func__, __LINE__, ##x)

#define TPL_WARN(f, x...)								\
	fprintf(stderr,										\
			"[TPL_WARN(%d):%s(%d)] " f "\n",			\
			getpid(), __func__, __LINE__, ##x)
#endif /* DLOG_DEFAULT_ENABLE */
#else /* NDEBUG */
#define TPL_LOG_F(f, x...)
#define TPL_LOG_B(b, f, x...)
#define TPL_DEBUG(f, x...)
#define TPL_ERR(f, x...)
#define TPL_WARN(f, x...)
#endif /* NDEBUG */

#define TPL_CHECK_ON_NULL_RETURN(exp)							\
	{															\
		if ((exp) == NULL)										\
		{														\
			TPL_ERR("%s", "check failed: " # exp " == NULL");	\
			return;												\
		}														\
	}

#define TPL_CHECK_ON_NULL_RETURN_VAL(exp, val)					\
	{															\
		if ((exp) == NULL)										\
		{														\
			TPL_ERR("%s", "check failed: " # exp " == NULL");	\
			return (val);										\
		}														\
	}

#define TPL_CHECK_ON_NULL_GOTO(exp, label)						\
	{															\
		if ((exp) == NULL)										\
		{														\
			TPL_ERR("%s", "check failed: " # exp " == NULL");	\
			goto label;											\
		}														\
	}

#define TPL_CHECK_ON_TRUE_RETURN(exp)							\
	{															\
		if (exp)												\
		{														\
			TPL_ERR("%s", "check failed: " # exp " is true");	\
			return;												\
		}														\
	}

#define TPL_CHECK_ON_TRUE_RETURN_VAL(exp, val)					\
	{															\
		if (exp)												\
		{														\
			TPL_ERR("%s", "check failed: " # exp " is true");	\
			return val;											\
		}														\
	}

#define TPL_CHECK_ON_TRUE_GOTO(exp, label)				        \
	{															\
		if (exp)												\
		{														\
			TPL_ERR("%s", "check failed: " # exp " is true");   \
			goto label;											\
		}														\
	}

#define TPL_CHECK_ON_FALSE_RETURN(exp)							\
	{															\
		if (!(exp))												\
		{														\
			TPL_ERR("%s", "check failed: " # exp " is false");	\
			return;												\
		}														\
	}

#define TPL_CHECK_ON_FALSE_RETURN_VAL(exp, val)					\
	{															\
		if (!(exp))												\
		{														\
			TPL_ERR("%s", "check failed: " # exp " is false");	\
			return val;											\
		}														\
	}

#define TPL_CHECK_ON_FALSE_GOTO(exp, label)						\
	{															\
		if (!(exp))												\
		{														\
			TPL_ERR("%s", "check failed: " # exp " is false");	\
			goto label;											\
		}														\
	}

#define TPL_CHECK_ON_FALSE_ASSERT_FAIL(exp, mesg)	\
	{												\
		if (!(exp))									\
		{											\
			TPL_ERR("%s", mesg);					\
			assert(0);								\
		}											\
	}

#ifdef DEFAULT_DUMP_ENABLE
#define TPL_IMAGE_DUMP __tpl_util_image_dump
#else
#define TPL_IMAGE_DUMP(data, width, height)												\
	{																					\
		if (tpl_dump_lvl != 0)															\
		{																				\
			__tpl_util_image_dump(data, width, height);									\
		}																				\
		else																			\
		{																				\
			char *env = getenv("TPL_DUMP_LEVEL");										\
			if (env == NULL)															\
				tpl_dump_lvl = 0;														\
			else																		\
				tpl_dump_lvl = atoi(env);												\
																						\
			if (tpl_dump_lvl != 0)														\
				__tpl_util_image_dump(data, width, height);								\
		}																				\
	}
#endif


typedef struct _tpl_list_node	tpl_list_node_t;
typedef struct _tpl_list	tpl_list_t;
typedef struct tpl_util_map_entry tpl_util_map_entry_t;
typedef struct tpl_util_map tpl_util_map_t;
typedef union tpl_util_key tpl_util_key_t;

typedef int (*tpl_util_hash_func_t)(const tpl_util_key_t key, int key_length);
typedef int (*tpl_util_key_length_func_t)(const tpl_util_key_t key);
typedef int (*tpl_util_key_compare_func_t)(const tpl_util_key_t key0,
		int key0_length,
		const tpl_util_key_t key1,
		int key1_length);

enum _tpl_occurrence {
	TPL_FIRST,
	TPL_LAST,
	TPL_ALL
};

union tpl_util_key {
	uint32_t key32;
	uint64_t key64;
	void *ptr; /*pointer key or user defined key(string)*/
};

struct _tpl_list_node {
	tpl_list_node_t *prev;
	tpl_list_node_t *next;
	void *data;
	tpl_list_t *list;
};

struct _tpl_list {
	tpl_list_node_t head;
	tpl_list_node_t tail;
	int count;
};

struct tpl_util_map {
	tpl_util_hash_func_t hash_func;
	tpl_util_key_length_func_t key_length_func;
	tpl_util_key_compare_func_t key_compare_func;
	int bucket_bits;
	int bucket_size;
	int bucket_mask;
	tpl_util_map_entry_t **buckets;
};

void tpl_util_map_init(tpl_util_map_t *map, int bucket_bits,
					   tpl_util_hash_func_t hash_func,
					   tpl_util_key_length_func_t key_length_func,
					   tpl_util_key_compare_func_t key_compare_func,
					   void *buckets);

void tpl_util_map_int32_init(tpl_util_map_t *map, int bucket_bits,
							 void *buckets);

void tpl_util_map_int64_init(tpl_util_map_t *map, int bucket_bits,
							 void *buckets);

void tpl_util_map_pointer_init(tpl_util_map_t *map, int bucket_bits,
							   void *buckets);

void tpl_util_map_fini(tpl_util_map_t *map);

tpl_util_map_t *
tpl_util_map_create(int bucket_bits, tpl_util_hash_func_t hash_func,
					tpl_util_key_length_func_t key_length_func,
					tpl_util_key_compare_func_t key_compare_func);

tpl_util_map_t *tpl_util_map_int32_create(int bucket_bits);

tpl_util_map_t *tpl_util_map_int64_create(int bucket_bits);

tpl_util_map_t *tpl_util_map_pointer_create(int bucket_bits);

void tpl_util_map_destroy(tpl_util_map_t *map);

void tpl_util_map_clear(tpl_util_map_t *map);

void *tpl_util_map_get(tpl_util_map_t *map, const tpl_util_key_t key);

void
tpl_util_map_set(tpl_util_map_t *map, const tpl_util_key_t key, void *data,
				 tpl_free_func_t free_func);

static TPL_INLINE int
__tpl_list_get_count(const tpl_list_t *list)
{
	TPL_ASSERT(list);

	return list->count;
}

static TPL_INLINE tpl_bool_t
__tpl_list_is_empty(const tpl_list_t *list)
{
	TPL_ASSERT(list);

	return list->count == 0;
}

static TPL_INLINE void
__tpl_list_init(tpl_list_t *list)
{
	TPL_ASSERT(list);

	list->head.list = list;
	list->tail.list = list;

	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;

	list->count = 0;
}

static TPL_INLINE void
__tpl_list_fini(tpl_list_t *list, tpl_free_func_t func)
{
	tpl_list_node_t *node;

	TPL_ASSERT(list);

	node = list->head.next;

	while (node != &list->tail) {
		tpl_list_node_t *free_node = node;
		node = node->next;

		TPL_ASSERT(free_node);

		if (func) func(free_node->data);

		free(free_node);
	}

	__tpl_list_init(list);
}

static TPL_INLINE tpl_list_t *
__tpl_list_alloc()
{
	tpl_list_t *list;

	list = (tpl_list_t *) malloc(sizeof(tpl_list_t));
	if (!list) return NULL;

	__tpl_list_init(list);

	return list;
}

static TPL_INLINE void
__tpl_list_free(tpl_list_t *list, tpl_free_func_t func)
{
	TPL_ASSERT(list);

	__tpl_list_fini(list, func);
	free(list);
}

static TPL_INLINE void *
__tpl_list_node_get_data(const tpl_list_node_t *node)
{
	TPL_ASSERT(node);

	return node->data;
}

static TPL_INLINE tpl_list_node_t *
__tpl_list_get_front_node(tpl_list_t *list)
{
	TPL_ASSERT(list);

	if (__tpl_list_is_empty(list)) return NULL;

	return list->head.next;
}

static TPL_INLINE tpl_list_node_t *
__tpl_list_get_back_node(tpl_list_t *list)
{
	TPL_ASSERT(list);

	if (__tpl_list_is_empty(list)) return NULL;

	return list->tail.prev;
}

static TPL_INLINE tpl_list_node_t *
__tpl_list_node_prev(tpl_list_node_t *node)
{
	TPL_ASSERT(node);
	TPL_ASSERT(node->list);

	if (node->prev != &node->list->head)
		return (tpl_list_node_t *)node->prev;

	return NULL;
}

static TPL_INLINE tpl_list_node_t *
__tpl_list_node_next(tpl_list_node_t *node)
{
	TPL_ASSERT(node);
	TPL_ASSERT(node->list);

	if (node->next != &node->list->tail)
		return node->next;

	return NULL;
}

static TPL_INLINE void *
__tpl_list_get_front(const tpl_list_t *list)
{
	TPL_ASSERT(list);

	if (__tpl_list_is_empty(list))
		return NULL;

	TPL_ASSERT(list->head.next);

	return list->head.next->data;
}

static TPL_INLINE void *
__tpl_list_get_back(const tpl_list_t *list)
{
	TPL_ASSERT(list);

	if (__tpl_list_is_empty(list)) return NULL;

	TPL_ASSERT(list->tail.prev);

	return list->tail.prev->data;
}

static TPL_INLINE void
__tpl_list_remove(tpl_list_node_t *node, tpl_free_func_t func)
{
	TPL_ASSERT(node);
	TPL_ASSERT(node->prev);
	TPL_ASSERT(node->next);

	node->prev->next = node->next;
	node->next->prev = node->prev;

	if (func) func(node->data);

	node->list->count--;
	free(node);
}

static TPL_INLINE tpl_result_t
__tpl_list_insert(tpl_list_node_t *pos, void *data)
{
	tpl_list_node_t *node = (tpl_list_node_t *)malloc(sizeof(tpl_list_node_t));
	if (!node) {
		TPL_ERR("Failed to allocate new tpl_list_node_t.");
		return TPL_ERROR_INVALID_OPERATION;
	}

	node->data = data;
	node->list = pos->list;

	pos->next->prev = node;
	node->next = pos->next;

	pos->next = node;
	node->prev = pos;

	pos->list->count++;

	return TPL_ERROR_NONE;
}

static TPL_INLINE void
__tpl_list_remove_data(tpl_list_t *list, void *data, int occurrence,
					   tpl_free_func_t func)
{
	tpl_list_node_t *node;

	TPL_ASSERT(list);

	if (occurrence == TPL_FIRST) {
		node = list->head.next;

		while (node != &list->tail) {
			tpl_list_node_t *curr;

			curr = node;
			node = node->next;

			TPL_ASSERT(curr);
			TPL_ASSERT(node);

			if (curr->data == data) {
				if (func) func(data);

				__tpl_list_remove(curr, func);
				return;
			}
		}
	} else if (occurrence == TPL_LAST) {
		node = list->tail.prev;

		while (node != &list->head) {
			tpl_list_node_t *curr;

			curr = node;
			node = node->prev;

			TPL_ASSERT(curr);
			TPL_ASSERT(node);

			if (curr->data == data) {
				if (func) func(data);

				__tpl_list_remove(curr, func);
				return;
			}
		}
	} else if (occurrence == TPL_ALL) {
		node = list->head.next;

		while (node != &list->tail) {
			tpl_list_node_t *curr;

			curr = node;
			node = node->next;

			TPL_ASSERT(curr);
			TPL_ASSERT(node);

			if (curr->data == data) {
				if (func) func(data);

				__tpl_list_remove(curr, func);
			}
		}
	}
}

static TPL_INLINE void
__tpl_list_push_front(tpl_list_t *list, void *data)
{
	TPL_ASSERT(list);

	__tpl_list_insert(&list->head, data);
}

static TPL_INLINE tpl_result_t
__tpl_list_push_back(tpl_list_t *list, void *data)
{
	TPL_ASSERT(list);

	return __tpl_list_insert(list->tail.prev, data);
}

static TPL_INLINE void *
__tpl_list_pop_front(tpl_list_t *list, tpl_free_func_t func)
{
	void *data;

	TPL_ASSERT(list);

	if (__tpl_list_is_empty(list)) return NULL;

	data = list->head.next->data;
	__tpl_list_remove(list->head.next, func);

	return data;
}

static TPL_INLINE void *
tpl_list_pop_back(tpl_list_t *list, tpl_free_func_t func)
{
	void *data;

	TPL_ASSERT(list);

	if (__tpl_list_is_empty(list)) return NULL;

	data = list->tail.prev->data;
	__tpl_list_remove(list->tail.prev, func);

	return data;
}

static TPL_INLINE void
__tpl_util_image_dump(void *data, int width, int height)
{
	char path_name[20] = "/tmp";

	tbm_surface_internal_dump_start(path_name, width, height, 1);
	tbm_surface_internal_dump_buffer((tbm_surface_h)data, "png");
	tbm_surface_internal_dump_end();
}
#endif /* TPL_UTILS_H */
