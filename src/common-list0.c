/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	Semi-template for double linked list with locking
 */

#if 0 /* === BEGIN: example usage ========================================== */

#include <stdint.h>
#include <pthread.h>
#include "common.h"

struct myList;
struct myItem;

typedef struct myList myList_t;
typedef struct myItem myItem_t;

struct myList
{
	/* list header */
	myItem_t	*first;
	myItem_t	*last;

	/* list fields */
	uint32_t	itemcnt;

	/* list lock */
	pthread_rwlock_t	 lock;
	pthread_rwlockattr_t lock_attr;
};

struct myItem
{
	/* list header */
	myItem_t	*prev;
	myItem_t	*next;

	/* key */
	uint32_t	size;
};

static inline myItem_t *myItem_get_prev(myItem_t *self) { return self->prev; };
static inline myItem_t *myItem_get_next(myItem_t *self) { return self->next; };
static inline myItem_t *myItem_set_prev(myItem_t *self, myItem_t *prev) { self->prev = prev; };
static inline myItem_t *myItem_set_next(myItem_t *self, myItem_t *next) { self->next = next; };

#define __LIST				myList
#define __LIST_T			myList_t
#define __KEY_T				uint32_t
#define __ITEM_T			myItem_t
#define __COUNTER(list)		list->itemcnt
#define __FIRST(list)		list->first
#define __LAST(list)		list->last
#define __PREV(item)		myItem_get_prev(item)
#define __NEXT(item)		myItem_get_next(item)
#define __SET_PREV(item,p)	myItem_set_prev(item, p)
#define __SET_NEXT(item,n)	myItem_set_next(item, n)
#define __KEY(item)			item->size
#define __LOCK(item)		item->lock
#define __LOCK_ATTR(item)	item->lock_attr
#define __KEY_LT(a,b)		(a) < (b)
#define __KEY_EQ(a,b)		(a) == (b)

#endif /* === END: example usage =========================================== */

/* internal macros */
#define __LIST_DECL			__LIST, __LIST_T

/* function-like internal macros */
#define __CONCAT3(A, B, C)						A ## B ## C
#define __METHOD_0(LIST, TYPE, NAME)			__CONCAT3(LIST, _, NAME)(TYPE *self)
#define __METHOD_1(LIST, TYPE, NAME, ARGS...)	__CONCAT3(LIST, _, NAME)(TYPE *self, ARGS)
#define __METHOD(DECL, NAME)					__METHOD_0(DECL, NAME)
#define __METHOD_ARGS(DECL, NAME, ARGS...)		__METHOD_1(DECL, NAME, ARGS)
#define __CALL(LIST, NAME)						__CONCAT3(LIST, _, NAME)(self)
#define __CALL_ARGS(LIST, NAME, ARGS...)		__CONCAT3(LIST, _, NAME)(self, ARGS)

/* template code */

static inline void __METHOD(__LIST_DECL, rdlock) { pthread_rwlock_rdlock(&__LOCK(self)); }
static inline void __METHOD(__LIST_DECL, wrlock) { pthread_rwlock_wrlock(&__LOCK(self)); }
static inline void __METHOD(__LIST_DECL, unlock) { pthread_rwlock_unlock(&__LOCK(self)); }

/**
 * Initializer.
 *
 * @param self
 */

void __METHOD(__LIST_DECL, init)
{
	__FIRST(self)	= NULL;
	__LAST(self)	= NULL;
	__COUNTER(self) = 0;

	/* Initialize locking mechanizm */
	pthread_rwlockattr_init(&__LOCK_ATTR(self));
	pthread_rwlockattr_setpshared(&__LOCK_ATTR(self), 1);
	pthread_rwlock_init(&__LOCK(self), &__LOCK_ATTR(self));
}

/**
 * Push an item onto the list (prepend).
 *
 * @param self
 * @param item
 * @param lock
 */

void __METHOD_ARGS(__LIST_DECL, push, __ITEM_T *item, locking_t lock)
{
	if (lock)
		__CALL(__LIST, wrlock);

	if (__FIRST(self) == NULL) {
		__SET_PREV(item, NULL);
		__SET_NEXT(item, NULL);
		__FIRST(self) = item;
		__LAST(self)  = item;
	} else {
		__SET_PREV(item, NULL);
		__SET_NEXT(item, __FIRST(self));
		__SET_PREV(__FIRST(self), item);
		__FIRST(self) = item;
	}

	__COUNTER(self)++;

	if (lock)
		__CALL(__LIST, unlock);
}

/**
 * Append an item to the list.
 *
 * @param self
 * @param item
 * @param lock
 */

void __METHOD_ARGS(__LIST_DECL, append, __ITEM_T *item, locking_t lock)
{
	if (lock)
		__CALL(__LIST, wrlock);

	if (__LAST(self) == NULL) {
		__SET_PREV(item, NULL);
		__SET_NEXT(item, NULL);
		__FIRST(self) = item;
		__LAST(self)  = item;
	} else {
		__SET_PREV(item, __LAST(self));
		__SET_NEXT(item, NULL);
		__SET_NEXT(__LAST(self), item);
		__LAST(self) = item;
	}

	__COUNTER(self)++;

	if (lock)
		__CALL(__LIST, unlock);
}

/**
 * Pop an item from the list.
 *
 * @param self
 * @param lock
 * @return
 */

__ITEM_T *__METHOD_ARGS(__LIST_DECL, pop, locking_t lock)
{
	if (lock)
		__CALL(__LIST, wrlock);

	__ITEM_T *result = __FIRST(self);

	if (__FIRST(self) == __LAST(self)) {
		__FIRST(self) = NULL;
		__LAST(self) = NULL;
	} else {
		__FIRST(self) = __NEXT(result);
		__SET_PREV(__FIRST(self), NULL);
	}

	__SET_PREV(result, NULL);
	__SET_NEXT(result, NULL);

	__COUNTER(self)--;
	
	if (lock)
		__CALL(__LIST, unlock);

	return result;
}

/**
 * Insert an item onto the sorted list.
 *
 * @param self
 * @param item
 * @param lock
 */

void __METHOD_ARGS(__LIST_DECL, insert, __ITEM_T *item, locking_t lock)
{
	if (lock)
		__CALL(__LIST, wrlock);

	if (__FIRST(self) == NULL) {
		/* list is empty */
		__SET_PREV(item, NULL);
		__SET_NEXT(item, NULL);
		__FIRST(self) = item;
		__LAST(self)  = item;
	} else if (__KEY_LT(__KEY(item), __KEY(__FIRST(self)))) {
		/* before first item */
		__SET_PREV(item, NULL);
		__SET_NEXT(item, __FIRST(self));
		__SET_PREV(__FIRST(self), item);
		__FIRST(self) = item;
	} else if (__KEY_LT(__KEY(__LAST(self)), __KEY(item))) {
		/* after last item */
		__SET_PREV(item, __LAST(self));
		__SET_NEXT(item, NULL);
		__SET_NEXT(__LAST(self), item);
		__LAST(self) = item;
	} else {
		/* after first item which has smaller value of key */
		__ITEM_T *iter = __FIRST(self);

		while (__KEY_LT(__KEY(__NEXT(iter)), __KEY(item)))
			iter = __NEXT(iter);

		__SET_PREV(item, iter);
		__SET_NEXT(item, __NEXT(iter));
	
		__SET_NEXT(__PREV(item), item);
		__SET_PREV(__NEXT(item), item);
	}

	__COUNTER(self)++;

	if (lock)
		__CALL(__LIST, unlock);
}

/**
 * Search the list for an item matching criteria.
 *
 * @param self
 * @param key
 * @param lock
 * @return
 */

__ITEM_T *__METHOD_ARGS(__LIST_DECL, search, __KEY_T key, locking_t lock)
{
	if (lock)
		__CALL(__LIST, wrlock);

	__ITEM_T *iter = __FIRST(self);

	while ((iter != NULL) && !(__KEY_EQ(__KEY(iter), key)))
		iter = __NEXT(iter);

	if (lock)
		__CALL(__LIST, unlock);

	return iter;
}

/**
 * Remove item from list.
 *
 * @param self	list from which item will be removed
 * @param item	pointer to an item on the list
 * @param lock	whether to lock list before item removal
 * @return
 */

void __METHOD_ARGS(__LIST_DECL, remove, __ITEM_T *item, locking_t lock)
{
	if (lock)
		__CALL(__LIST, wrlock);

	if ((__FIRST(self) == item) && (__LAST(self) == item)) {
		/* the only item on the list */
		__FIRST(self) = NULL;
		__LAST(self) = NULL;
	} else if (__FIRST(self) == item) {
		/* first item on the list */
		__FIRST(self) = __NEXT(item);
		__SET_PREV(__FIRST(self), NULL);
	} else if (__LAST(self) == item) {
		/* last item on the list */
		__LAST(self) = __PREV(item);
		__SET_NEXT(__LAST(self), NULL);
	} else {
		/* item is somewhere in the middle of list */
		__SET_NEXT(__PREV(item), __NEXT(item));
		__SET_PREV(__NEXT(item), __PREV(item));
	}

	__SET_PREV(item, NULL);
	__SET_NEXT(item, NULL);

	__COUNTER(self)--;

	if (lock)
		__CALL(__LIST, unlock);
}

/**
 * Split list at a point given by the item.
 *
 * @param self
 * @param list
 * @param item
 * @param lock
 * @return
 */

void __METHOD_ARGS(__LIST_DECL, split, __LIST_T *list, __ITEM_T *item, locking_t lock)
{
	if (lock)
		__CALL(__LIST, wrlock);

	__ITEM_T *iter = __FIRST(self);

	uint32_t counter;

	while (iter && (iter == item)) {
		iter = __NEXT(iter);
		counter++;
	}

	assert(iter);

	__FIRST(list) = iter;
	__LAST(list)  = __LAST(self);
	__LAST(self)  = __PREV(iter);

	__SET_NEXT(__LAST(self), NULL);
	__SET_PREV(__FIRST(list), NULL);

	__COUNTER(list) = __COUNTER(self) - counter;
	__COUNTER(self) = counter;

	if (lock)
		__CALL(__LIST, unlock);
}

/**
 * Join two lists. Join operation does not care about order.
 *
 * @param self	base list
 * @param list	list from which items will be removed and appended to base list
 * @param lock	whether to lock base list during operation
 * @return
 */

void __METHOD_ARGS(__LIST_DECL, join, __LIST_T *list, locking_t lock)
{
	if (lock)
		__CALL(__LIST, wrlock);

	if (__FIRST(self) == NULL) {
		/* first list is empty */
		__FIRST(self)   = __FIRST(list);
		__LAST(self)    = __LAST(list);
		__COUNTER(self) = __COUNTER(list);
	} else if (__FIRST(list) != NULL) {
		/* both list are nonempty */
		__SET_PREV(__FIRST(list), __LAST(self));
		__SET_NEXT(__LAST(self), __FIRST(list));

		__LAST(self)     = __LAST(list);
		__COUNTER(self) += __COUNTER(list);

		__FIRST(list)   = NULL;
		__LAST(list)    = NULL;
		__COUNTER(list) = 0;
	}

	if (lock)
		__CALL(__LIST, unlock);
}

/* === undefine macros ===================================================== */

#undef __LIST
#undef __LIST_T
#undef __KEY_T
#undef __ITEM_T
#undef __COUNTER
#undef __FIRST
#undef __LAST
#undef __PREV
#undef __NEXT
#undef __KEY
#undef __LOCK
#undef __LOCK_ATTR
#undef __KEY_LT
#undef __KEY_EQ

#undef __LIST_DECL

#undef __CONCAT3
#undef __METHOD_0
#undef __METHOD_1
#undef __METHOD
#undef __METHOD_ARGS
#undef __CALL
#undef __CALL_ARGS

/* ========================================================================= */
