/** 
 * Krystian Bacławski <krystian.baclawski@gmail.com> (c) 2006
 */

#include <stdio.h>
#include <stdlib.h>

#define swapi(a,b)      { int t; t=(b); (b)=(a); (a)=t; }
#define swapp(a,b)      { void *t; t=(b); (b)=(a); (a)=t; }

typedef enum {FALSE, TRUE} bool;

typedef enum {UNKNOWN, BLACK, RED} color_t;

typedef struct node
{
	struct node *parent;
	struct node *left;
	struct node *right;

	color_t color;

	int key;
} node_t;

typedef struct tree
{
	struct node *root;
} tree_t;


node_t nil = {0xDEADC0DE, 0xDEADC0DE, 0xDEADC0DE, BLACK, 0, 0, 0}; /* Wartownik */

/**
 * Podstawowe operacje na węzłach drzewa binarnego
 */

#define node_lt(a, b) ((a)->key < (b)->key)
#define node_gt(a, b) ((a)->key > (b)->key)
#define node_eq(a, b) ((a)->key == (b)->key)

#define parent(n)			((n)->parent)
#define grandparent(n)		((n)->parent->parent)
#define uncle(n) 			(((n)->parent == grandparent(n)->left) ? (grandparent(n)->right) : (grandparent(n)->left))
#define sibling(n) 			(((n) == (n)->parent->left) ? ((n)->parent->right) : ((n)->parent->left))

#define is_nil(n)			(((n) == NULL) || ((n) == &nil))
#define is_leaf(n) 			(is_nil((n)->left) && is_nil((n)->right))
#define has_one_child(n)	(((is_nil((n)->left) && !is_nil((n)->right))) || (!is_nil((n)->left) && is_nil((n)->right)))
#define is_left_child(n)	(!is_nil((n)->parent) && ((n)->parent->left == (n)))
#define is_right_child(n)	(!is_nil((n)->parent) && ((n)->parent->right == (n)))

#define node_swap_data(a,b)	{ swapi(a->key, b->key); swapi(a->x, b->x); swapi(a->y, b->y); }

/**
 * Utwórz węzeł
 */

inline node_t *node_new(int x, int y)
{
	node_t *node = calloc(1, sizeof(node_t));

	node->x = x;
	node->y = y;
	node->key = x * x + y * y;

	return node;
}

/**
 * Usuń węzeł i wraz z podrzewem którego jest korzeniem
 */

void node_delete(node_t *n)
{
	if (!is_nil(n->left))
		node_delete(n->left);
	if (!is_nil(n->right))
		node_delete(n->right);

	free(n);
}

void node_print(node_t *n)
{
	printf("[K:%d C:%c, ", n->key, (n->color == RED) ? ('r') : ((n->color == BLACK) ? ('b') : ('?')));

	if (!is_nil(n->left))
		node_print(n->left);
	else
		printf("nil");

	printf(", ");

	if (!is_nil(n->right))
		node_print(n->right);
	else
		printf("nil");

	printf("]");
}

/**
 * Wyjmij węzeł z drzewa
 */

node_t *node_remove(node_t *n)
{
	if (is_leaf(n))
	{
		// przypadek 1: jest liściem

		// usuń wskaźnik z ojca
		if (is_left_child(n))
			parent(n)->left = NULL;
		else
			parent(n)->right = NULL;
	}
	else if (has_one_child(n))
	{
		// przypadek 2: tylko jedno dziecko
		node_t *child = (!is_nil(n->left)) ? (n->left) : (n->right);

		// niech dziadek stanie się ojcem
		parent(child) = parent(n);

		// podczep dziecko jako lewego lub prawego syna rodzica usuwanego węzła
		if (is_left_child(n))
			parent(child)->left = child;
		else
			parent(child)->right = child;
	}
	else
	{
		// przypadek 3: dwoje dzieci

		// znajdź poprzednika
		node_t *next = n->left;

		while (!is_nil(next->right))
			next = next->right;

		// zamień dane następnika z węzłem
		node_swap_data(next, n);

		n = node_remove(next);
	}

	n->left = NULL;
	n->right = NULL;
	parent(n) = NULL;

	return n;
}

/**
 * Lewa rotacja
 */

void node_rotate_left(tree_t *tree, node_t *x)
{
	//printf("@rotate-left %d\n", x->key);

	node_t *y;
	
	y = x->right;

	x->right = y->left;
	
	if (!is_nil(y->left))
		parent(y->left) = x;

	parent(y) = parent(x);

	if (is_nil(parent(x)))
	{
		tree->root = y;
	}
	else
	{
		if (is_left_child(x))
			parent(x)->left = y;
		else
			parent(x)->right = y;
	}
	
	y->left = x;

	parent(x) = y;
}

/**
 * Prawa rotacja
 */

void node_rotate_right(tree_t *tree, node_t *x)
{
	//printf("@rotate-right %d\n", x->key);

	node_t *y;
	
	y = x->left;

	x->left = y->right;
	
	if (!is_nil(y->right))
		parent(y->right) = x;

	parent(y) = parent(x);

	if (is_nil(parent(x)))
	{
		tree->root = y;
	}
	else
	{
		if (is_right_child(x))
			parent(x)->right = y;
		else
			parent(x)->left = y;
	}
	
	y->right = x;

	parent(x) = y;
}

/**
 * Wstawianie węzła do drzewa binarnego
 */

bool tree_insert(tree_t *tree, node_t *n)
{
	if (is_nil(tree->root))
	{
		tree->root = n;
	}
	else
	{
		node_t *current = tree->root;

		// jedź w dół, aż będziemy w liściu
		while (!is_leaf(current))
		{
			if (node_lt(n, current))
			{
				if (is_nil(current->left))
					break;

				current = (current->left);
			}
			else
			{
				if (is_nil(current->right))
					break;

				current = (current->right);
			}
		}

		// ustal ojca
		parent(n) = current;

		// wstaw po lewej
		if (node_lt(n, current))
			current->left = n;

		// wstaw po prawej
		if (node_gt(n, current))
			current->right = n;
	}

	return TRUE;
}

/**
 * Wyszukiwanie w drzewie binarnym węzła o zadanym kluczu
 */

node_t *tree_search(tree_t *tree, int key)
{
	node_t *current = tree->root;

	// jedź w dół, aż znajdziesz lub nie będzie gdzie szukać
	while (!is_nil(current) && (current->key != key))
		current = (current->key > key) ? (current->left) : (current->right);

	return current;
}

/**
 * Wyjmowanie z drzewa binarnego węzła o zadanym kluczu
 */

node_t *tree_remove(tree_t *tree, int key)
{
	node_t *n = tree_search(tree, key);

	if (n == tree->root)
		tree->root = NULL;

	return (!is_nil(n)) ? (node_remove(n)) : NULL;
}

/**
 * Usuwanie drzewa
 */

void tree_delete(tree_t *tree)
{
	if (!is_nil(tree->root))
		node_delete(tree->root);

	free(tree);
}

/**
 * Naprawanie własności drzewa czerwono-czarnego po wstawieniu węzła
 */

void rb_tree_insert_repair(tree_t *tree, node_t *n)
{
	// przypadek 1: korzeń
    if (is_nil(parent(n)))
	{
        n->color = BLACK;
		return;
	}
    
	// przypadek 2: rodzic czarny
	if (parent(n)->color == BLACK)
		return;

	// przypadek 3: rodzic czerwony; wuj czerwony;
	if (!is_nil(uncle(n)) && uncle(n)->color == RED)
	{
		parent(n)->color = BLACK;
		uncle(n)->color = BLACK;
		grandparent(n)->color = RED;

		rb_tree_insert_repair(tree, grandparent(n));
	}
	else
	// przypadek 4: rodzic czerwony; wuj czarny;
	{
		if (is_right_child(n) && is_left_child(parent(n)))
		{
			node_rotate_left(tree, parent(n));
			n = n->left;
		}
		else if (is_left_child(n) && is_right_child(parent(n)))
		{
			node_rotate_right(tree, parent(n));
			n = n->right;
		}

		// case 5: wuj czarny; nowy, rodzic, dziadek na prostej; nowy i rodzic czerwoni;
		parent(n)->color = BLACK;
		grandparent(n)->color = RED;

		if (is_left_child(n) && is_left_child(parent(n)))
			node_rotate_right(tree, grandparent(n));
		else
			node_rotate_left(tree, grandparent(n));
	}
}

/**
 * Naprawanie własności drzewa czerwono-czarnego po usunięciu węzła
 */

void rb_tree_delete_repair(tree_t *tree, node_t *n)
{
	// przypadek 1: korzeń lub czerwony
    if (is_nil(parent(n)) || n->color == RED)
        return;
    
	// przypadek 2:
    if (sibling(n)->color == RED)
	{
        parent(n)->color = RED;
        sibling(n)->color = BLACK;

        if (is_left_child(n))
            node_rotate_left(tree, parent(n));
        else
            node_rotate_right(tree, parent(n));
    }

	// przypadek 3: 
    if (parent(n)->color == BLACK && sibling(n)->color == BLACK && sibling(n)->left->color == BLACK && sibling(n)->right->color == BLACK)
    {
        sibling(n)->color = RED;
        rb_tree_delete_repair(tree, parent(n));
		return;
    }

	// przypadek 4:
    if (parent(n)->color == RED && sibling(n)->color == BLACK && sibling(n)->left->color == BLACK && sibling(n)->right->color == BLACK)
    {
        sibling(n)->color = RED;
        parent(n)->color = BLACK;
		return;
    }

	// przypadek 5
    if (is_left_child(n) && sibling(n)->color == BLACK && sibling(n)->left->color == RED && sibling(n)->right->color == BLACK)
    {
        sibling(n)->color = RED;
        sibling(n)->left->color = BLACK;

        node_rotate_right(tree, sibling(n));
    }
    else if (is_right_child(n) && sibling(n)->color == BLACK &&	sibling(n)->right->color == RED && sibling(n)->left->color == BLACK)
    {
        sibling(n)->color = RED;
        sibling(n)->right->color = BLACK;

        node_rotate_left(tree, sibling(n));
    }

	// przypadek 6:
    sibling(n)->color = parent(n)->color;
    parent(n)->color = BLACK;

    if (is_left_child(n))
	{
        sibling(n)->right->color = BLACK;

        node_rotate_left(tree, parent(n));
    }
    else
    {
        sibling(n)->left->color = BLACK;

        node_rotate_right(tree, parent(n));
    }
}

/**
 * Wstawianie do drzewa czerwono-czarnego
 */

bool rb_tree_insert_node(tree_t *tree, node_t *n)
{
	n->parent = &nil;
	n->left	  = &nil;
	n->right  = &nil;

	if (tree_insert(tree, n))
	{
		n->color = RED;

		rb_tree_insert_repair(tree, n);

		return TRUE;
	}

	return FALSE;
}

/**
 * Usuwanie węzła z drzewa czerwono-czarnego.
 */

node_t *rb_tree_remove_node(tree_t *tree, node_t *n)
{
	node_t *c, *old_n = n;

	if (!is_nil(n->left) && !is_nil(n->right))
	{
		// znajdź następnika
		node_t *next = n->right;

		while (!is_nil(next->left))
			next = next->left;

		old_n = n;
		n = next;
	}

	c = (!is_nil(n->left)) ? (n->left) : (n->right);

	if (is_nil(c))
		parent(c) = n;

	parent(c) = parent(n);

	if (is_nil(parent(n)))
	{
		tree->root = c;
	}
	else
	{
		if (is_left_child(n))
			parent(n)->left = c;
		else
			parent(n)->right = c;
	}

	if (old_n != n)
		node_swap_data(old_n, n);

	if (n->color == BLACK)
		rb_tree_delete_repair(tree, c);

	n->left = NULL;
	n->right = NULL;
	parent(n) = NULL;

	return n;
}
