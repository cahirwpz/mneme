/*
 * Author:	Krystian Bacławski <name.surname@gmail.com>
 * Desc:	Semi-template for splay tree with locking
 */

#if 1 /* === BEGIN: example usage ========================================== */

#include <stdint.h>
#include <pthread.h>
#include "common.h"

struct myTree;
struct myItem;

typedef struct myTree myTree_t;
typedef struct myNode myNode_t;

struct myTree
{
	/* tree root */
	myNode_t *root;

	/* tree lock */
	pthread_rwlock_t	 lock;
	pthread_rwlockattr_t lock_attr;
};

struct myNode
{
	/* tree header */
	myNode_t *left;
	myNode_t *right;
	myNode_t *parent;

	/* key */
	uint32_t	size;
};

#define __TREE				myTree
#define __TREE_T			myTree_t
#define __NODE				myNode
#define __NODE_T			myNode_t
#define __KEY_T				uint32_t
#define __ROOT(tree)		tree->root
#define __LEFT(node)		node->left
#define __RIGHT(node)		node->right
#define __PARENT(node)		node->parent
#define __KEY(node)			node->size
#define __LOCK(tree)		tree->lock
#define __LOCK_ATTR(tree)	tree->lock_attr
#define __KEY_LT(a,b)		a < b
#define __KEY_EQ(a,b)		a == b

#endif /* === END: example usage =========================================== */

/* internal macros */
#define __TREE_DECL			__TREE, __TREE_T
#define __NODE_DECL			__NODE, __NODE_T

/* function-like internal macros */
#define __CONCAT3(A, B, C)						A ## B ## C
#define __METHOD_0(OBJ, TYPE, NAME)				__CONCAT3(OBJ, _, NAME)(TYPE *self)
#define __METHOD_1(OBJ, TYPE, NAME, ARGS...)	__CONCAT3(OBJ, _, NAME)(TYPE *self, ARGS)
#define __METHOD(DECL, NAME)					__METHOD_0(DECL, NAME)
#define __METHOD_ARGS(DECL, NAME, ARGS...)		__METHOD_1(DECL, NAME, ARGS)
#define __CALL(OBJ, NAME, ARGS...)				__CONCAT3(OBJ, _, NAME)(ARGS)

/* template code */

static inline void __METHOD(__TREE_DECL, rdlock) { pthread_rwlock_rdlock(&__LOCK(self)); }
static inline void __METHOD(__TREE_DECL, wrlock) { pthread_rwlock_wrlock(&__LOCK(self)); }
static inline void __METHOD(__TREE_DECL, unlock) { pthread_rwlock_unlock(&__LOCK(self)); }

/**
 * Splay operation for given node.
 *
 * @param self
 * @param x
 */

static void __METHOD_ARGS(__TREE_DECL, splay, __NODE_T *x)
{
	I(x != NULL);

	__NODE_T *p;
	__NODE_T *g;

	while (TRUE) {
		p = __PARENT(x);
		
		if (p == NULL)
			break;

		g = __PARENT(p);

		if (g == NULL) {
			__ROOT(self) = x;
			__PARENT(x)  = NULL;
			__PARENT(p)  = x;

			if (__LEFT(p) == x) {
				/* left zig */
				__LEFT(p)  = __RIGHT(x);
				__RIGHT(x) = p;
			} else {
				/* right zig */
				__RIGHT(p) = __LEFT(x);
				__LEFT(x)  = p;
			}
		} else {
			if (__PARENT(g)) {
				if (__LEFT(__PARENT(g)) == g)
					__LEFT(__PARENT(g)) = x;
				else
					__RIGHT(__PARENT(g)) = x;
			} else {
				__ROOT(self) = x;
			}

			__PARENT(x) = __PARENT(g);
			__PARENT(p) = x;

			if (__LEFT(p) == x) {
				if (__LEFT(g) == p) {
					/* left zig-zig */
					__PARENT(g) = p;

					__LEFT(g)  = __RIGHT(p);
					__LEFT(p)  = __RIGHT(x);
					__RIGHT(p) = g;
					__RIGHT(x) = p;
				} else {
					/* left zig-zag */
					__PARENT(g) = x;

					__RIGHT(p) = __LEFT(x);
					__LEFT(g)  = __RIGHT(x);
					__LEFT(x)  = p;
					__RIGHT(x) = g;
				}
			} else {
				if (__RIGHT(g) == p) {
					/* right zig-zig */
					__PARENT(g) = p;

					__RIGHT(g) = __LEFT(p);
					__RIGHT(p) = __LEFT(x);
					__LEFT(p)  = g;
					__LEFT(x)  = p;
				} else {
					/* right zig-zag */
					__PARENT(g) = x;

					__LEFT(p)  = __RIGHT(x);
					__RIGHT(g) = __LEFT(x);
					__LEFT(x)  = g;
					__RIGHT(x) = p;
				}
			}
		}
	}
}

/**
 * Find next node.
 */

__NODE_T *__METHOD(__NODE_DECL, next)
{
	__NODE_T *next = __RIGHT(self);

	if (next != NULL) {
		while (__LEFT(next) != NULL)
			next = __LEFT(next);
	} else {
		__NODE_T *son;

		do {
			son  = next;
			next = __PARENT(next);
		} while ((next != NULL) && (__RIGHT(next) == son));
	}

	return next;
}

/**
 * Find previous node.
 */

__NODE_T *__METHOD(__NODE_DECL, prev)
{
	__NODE_T *prev = __LEFT(self);

	if (prev != NULL) {
		while (__RIGHT(next) != NULL)
			prev = __RIGHT(next);
	} else {
		__NODE_T *son;

		do {
			son  = prev;
			prev = __PARENT(prev);
		} while ((prev != NULL) && (__LEFT(prev) == son));
	}

	return prev;
}

/**
 * Split the splay tree with given node.
 */

void __METHOD_ARGS(__TREE_DECL, split, __TREE_T *tree, __NODE_T *node)
{
	if (__ROOT(self)) {
		/* splay at the node */
		__CALL(__TREE, splay, self, node);

		I(__ROOT(self) == node);

		/* left subtree will be first tree */
		__ROOT(tree) = __ROOT(self);
		__ROOT(self) = __LEFT(__ROOT(self));

		if (__ROOT(self)) {
			__PARENT(__ROOT(self)) = NULL;
		}

		__LEFT(__ROOT(tree)) = NULL;
	}
}

/**
 * Merge two splay trees.
 */

void __METHOD_ARGS(__TREE_DECL, merge, __TREE_T *tree);
{
	I(__ROOT(self) && __ROOT(root));

	/* find last node in first tree */
	__NODE_T *last = __ROOT(self);

	while (__RIGHT(last))
		last = __RIGHT(last);

	__CALL(__TREE, splay, self, last);

	/* find first node in the second tree */
	__NODE_T *first = __ROOT(tree);

	while (__LEFT(first))
		first = __LEFT(first);

	__CALL(__TREE, splay, tree, first);

	/* make second tree a right subtree of first tree */
	__RIGHT(__ROOT(self)) = __ROOT(tree);
	__PARENT(__RIGHT(__ROOT(self))) = __ROOT(self);

	__ROOT(tree) = NULL;
}

/**
 * Initializer.
 *
 * @param self
 */

void __METHOD(__TREE_DECL, init)
{
	__ROOT(self)	= NULL;

	/* Initialize locking mechanizm */
	pthread_rwlockattr_init(&__LOCK_ATTR(self));
	pthread_rwlockattr_setpshared(&__LOCK_ATTR(self), 1);
	pthread_rwlock_init(&__LOCK(self), &__LOCK_ATTR(self));
}

/**
 * Insert a node to the splay tree.
 *
 * @param self
 * @param node
 */

void __METHOD_ARGS(__TREE_DECL, insert, __NODE_T *node)
{
	__NODE_T *iter = __ROOT(self);

	while (TRUE) {
		if (__KEY_LT(__KEY(node), __KEY(iter))) {
			if (__LEFT(iter)) {
				iter = __LEFT(iter);
			} else {
				__LEFT(iter) = node;
				__PARENT(node) = iter;

				break;
			}
		} else {
			if (__RIGHT(iter)) {
				iter = __RIGHT(iter);
			} else {
				__RIGHT(iter) = node;
				__PARENT(node) = iter;

				break;
			}
		}
	}

	__CALL(__TREE, splay, self, node);
}

/**
 * Search the tree for a node matching criteria.
 *
 * @param self
 * @param key
 * @return
 */

__NODE_T *__METHOD_ARGS(__TREE_DECL, search, __KEY_T key)
{
	__NODE_T *iter    = __ROOT(self);
	__NODE_T *nonnull = __ROOT(self);

	while (iter) {
		if (__KEY_EQ(key, __KEY(iter)))
			break;

		nonnull = iter;

		iter = (__KEY_LT(key, __KEY(iter))) ? (__LEFT(iter)) : (__RIGHT(iter));
	}

	if (iter != NULL)
		nonnull = iter;

	if (nonnull)
		__CALL(__TREE, splay, self, nonnull);

	return iter;
}

/**
 * Remove a node from the tree.
 *
 * @param self
 * @param node
 */

void __METHOD_ARGS(__TREE_DECL, remove, __NODE_T *node)
{
	/* move node to root */
	__CALL(__TREE, splay, self, node);

	I(__ROOT(self) == node);

	/* get left and right subtree */
	__NODE_T *left  = __LEFT(__ROOT(self));
	__NODE_T *right = __RIGHT(__ROOT(self));

	if ((left != NULL) && (right != NULL)) {
		__LEFT(right)   = left;
		__PARENT(right) = NULL;
		__PARENT(left)  = right;
		__ROOT(self)    = right;
	} else if (left != NULL) {
		__ROOT(self)    = left;
		__PARENT(left)  = NULL;
	} else if (right != NULL) {
		__ROOT(self)    = right;
		__PARENT(right) = NULL;
	}

	__LEFT(node)   = NULL;
	__RIGHT(node)  = NULL;
	__PARENT(node) = NULL;
}

/* === undefine macros ===================================================== */

#undef __TREE
#undef __TREE_T
#undef __NODE
#undef __NODE_T
#undef __KEY_T
#undef __ROOT
#undef __LEFT
#undef __RIGHT
#undef __PARENT
#undef __KEY
#undef __LOCK
#undef __LOCK_ATTR
#undef __KEY_LT
#undef __KEY_EQ

#undef __TREE_DECL
#undef __NODE_DECL

#undef __CONCAT3
#undef __METHOD_0
#undef __METHOD_1
#undef __METHOD
#undef __METHOD_ARGS
#undef __CALL

/* ========================================================================= */
