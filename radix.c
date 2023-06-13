#include <sys/kernel.h>
#include <sys/malloc.h>

#include "radix.h"

MALLOC_DEFINE(RADIX_MEMORY, "RADIX_MEMORY", "RADIX_MEMORY");
#define MALLOC(p, s) ({ \
    p = (__typeof__(p))kmalloc((s), RADIX_MEMORY, M_NOWAIT | M_ZERO); \
    (p == NULL) ? ENOMEM : 0; \
})
#define FREE(p) kfree((p), RADIX_MEMORY)

#define ASSERTF(exp, fmt, ...) TRY(exp, panic(fmt, ##__VA_ARGS__))
#define ASSERT(exp) ASSERTF(exp, "%s", "RADIX");

int
radix_init0(struct radix_t *rdx_addr, struct radix_t *rdx_mask, int bit) {
    int ret = 0;

    TRY(rdx_addr != NULL, return EINVAL);
    TRY(bit > 0, return EINVAL);
    if (rdx_mask != NULL)
        TRY(rdx_mask->bit == bit, return EINVAL);

    bzero(rdx_addr, sizeof(*rdx_addr));
    TRY(!(ret = MALLOC(rdx_addr->root, sizeof(*rdx_addr->root))), goto err);
    TRY(!(ret = MALLOC(rdx_addr->buf, bit+1)), FREE(rdx_addr->root); goto err);

    lockinit(&rdx_addr->lock, "radix lock", 0, LK_CANRECURSE);
    rdx_addr->bit = bit;
    rdx_addr->mask = rdx_mask;
    rdx_addr->is_mask = (rdx_mask == NULL);
    return ret;

err:
    bzero(rdx_addr, sizeof(*rdx_addr));
    return ret;
}

static inline void
_radix_free(struct radix_node *n) {
    struct radix_mask_node *m0, *m1;

    if (n == NULL) return;

    for (m0 = n->mask; m0 != NULL; m0 = m1) {
        m1 = m0->next;
        FREE(m0);
    }

    _radix_free(n->left);
    _radix_free(n->right);
    FREE(n);
}

void
radix_free0(struct radix_t *rdx) {
    if (rdx == NULL) return;
    lockmgr(&rdx->lock, LK_EXCLUSIVE);
    FREE(rdx->buf);
    _radix_free(rdx->root);
    lockmgr(&rdx->lock, LK_RELEASE);
    lockuninit(&rdx->lock);
    bzero(rdx, sizeof(*rdx));
}

int
radix_init(struct radix_t *rdx, int bit) {
    int ret;
    TRY(!(ret = radix_init0(rdx, NULL, bit)), return ret);
    rdx->is_mask = 0;
    TRY(!(ret = MALLOC(rdx->mask, sizeof(*rdx->mask))),
        radix_free0(rdx); return ret);
    TRY(!(ret = radix_init0(rdx->mask, NULL, bit)), return ret);
    return ret;
}

void
radix_free(struct radix_t *rdx) {
    radix_free0(rdx->mask);
    FREE(rdx->mask);
    radix_free0(rdx);
}

static inline int
_radix_addr_compare(void *a, void *b, int bit) {
    for (int i = 0; i < bit; i++) {
        if (BIT(a, i) == BIT(b, i))
            continue;
        if (BIT(a, i))
            return 1;
        else
            return -1;
    }
    return 0;
}

static inline int
_radix_insert(struct radix_t *rdx, struct radix_node **_n, void *addr) {
    struct radix_node *nn, *n, *n0;
    int bit, ret = 0, r;

    TRY(!(ret = MALLOC(nn, sizeof(*nn))), goto err);
    nn->addr = addr;
    nn->is_leaf = 1;
    for (bit = 0, n = rdx->root; bit < rdx->bit; bit++) {
        if (!n->is_leaf) {
            if (BIT(addr, bit)) {
                if (n->right == NULL) {
                    n->right = nn;
                    nn->parent = n;
                    break;
                }
                n = n->right;
            } else {
                if (n->left == NULL) {
                    n->left = nn;
                    nn->parent = n;
                    break;
                }
                n = n->left;
            }
            continue;
        }

        ASSERT(n->addr != NULL);
        r = _radix_addr_compare(addr, n->addr, rdx->bit);
        if (!r) {
            FREE(nn);
            nn = n;
            goto exist;
        }

        TRY(!(ret = MALLOC(n0, sizeof(*n0))),  FREE(nn); goto err);
        n0->parent = n->parent;
        if (n->parent->left == n)
            n->parent->left = n0;
        else
            n->parent->right = n0;

        n->parent = n0;
        if (BIT(n->addr, bit)) {
            n0->right  = n;
            if (!BIT(addr, bit)) {
                n0->left = nn;
                nn->parent = n0;
                break;
            }
        } else {
            n0->left = n;
            if (BIT(addr, bit)) {
                n0->right = nn;
                nn->parent = n0;
                break;
            }
        }
    }
    if (bit == rdx->bit) {
        ASSERT(n != NULL && n->is_leaf);
        ASSERT(!_radix_addr_compare(addr, n->addr, rdx->bit));
        FREE(nn);
        nn = n;
    }
exist:
    *_n = nn;
    return ret;

err:
    *_n = NULL;
    return ret;
}

static inline int
_radix_traverse_ok(struct radix_t *rdx, int ignore_mask) {
    TRY(rdx != NULL, return 0);
    TRY(rdx->root != NULL, return 0);
    if (!ignore_mask) {
        TRY(!rdx->is_mask, return 0);
        TRY(rdx->mask != NULL, return 0);
        TRY(rdx->mask->root != NULL, return 0);
    }
    return 1;
}

int
radix_insert(struct radix_t *rdx, void *addr, void *mask) {
    int ret = 0;
    struct radix_node *n_addr = NULL, *n_mask = NULL;
    struct radix_mask_node *mn;

    DPRINTF("\n");

    TRY(addr != NULL, return EINVAL);
    TRY(mask != NULL, return EINVAL);
    TRY(_radix_traverse_ok(rdx, 0), return EINVAL);
    TRY(!rdx->walk, return EBUSY);

    lockmgr(&rdx->lock, LK_EXCLUSIVE);
    lockmgr(&rdx->mask->lock, LK_EXCLUSIVE);

    DDUMP(addr, rdx->bit);
    DDUMP(mask, rdx->mask->bit);

    TRY(!(ret = _radix_insert(rdx->mask, &n_mask, mask)), goto err);
    ASSERT(n_mask != NULL);
    DDUMP(n_mask->addr, rdx->mask->bit);

    TRY(!(ret = _radix_insert(rdx, &n_addr, addr)), goto err);
    ASSERT(n_addr != NULL);
    DDUMP(n_addr->addr, rdx->bit);

    for (mn = n_addr->mask; mn != NULL; mn = mn->next)
        if (mn->mask == n_mask)
            goto exist;

    TRY(!(ret = MALLOC(mn, sizeof(*mn))), goto err);
    mn->mask = n_mask;
    mn->next = n_addr->mask;
    n_addr->mask = mn;

exist:
err:
    lockmgr(&rdx->lock, LK_RELEASE);
    lockmgr(&rdx->mask->lock, LK_RELEASE);
    return ret;
}

static inline int
_radix_is_ok(struct radix_t *rdx, struct radix_node *n) {
    struct radix_mask_node *m;

    if (n == NULL)
        return 1;

    TRY(rdx->is_mask || rdx->mask != NULL, return 0);

    if (n == rdx->root)
        goto next;

    TRY(n->parent != NULL, return 0);
    TRY(n->parent->left == n || n->parent->right == n, return 0);

    if (!n->is_leaf)
        goto next;

    TRY(n->left == NULL, return 0);
    TRY(n->right == NULL, return 0);
    TRY(n->addr != NULL, return 0);
    for (m = n->mask; m != NULL; m = m->next)
        TRY(m->mask != NULL, return 0);

next:
    TRY(_radix_is_ok(rdx, n->left), return 0);
    return _radix_is_ok(rdx, n->right);
}

static inline void
_radix_print(struct radix_t *rdx, struct radix_node *n) {
    static int cp;
    struct radix_mask_node *m;
    int i;

    if (n == NULL) return;

    if (n == rdx->root) {
        kprintf("%s\n", rdx->is_mask ? "[MASK]" : "[ADDR]");
        goto next;
    }

    if (!n->is_leaf)
        goto next;

    for (i = 1; i <= cp; i++)
        kprintf("%c", BIT(rdx->buf, i) ? 'R' : 'L');
    for (; i < rdx->bit+1; i++)
        kprintf(" ");
    DUMP(n->addr, rdx->bit);
    //DUMP_BIT(n->addr, rdx->bit);
    if (!rdx->is_mask) {
        for (m = n->mask; m != NULL; m = m->next)
            DUMP(m->mask->addr, rdx->mask->bit);
    }

next:
    cp++;
    BIT_UNSET(rdx->buf, cp);
    _radix_print(rdx, n->left);
    cp--;

    cp++;
    BIT_SET(rdx->buf, cp);
    _radix_print(rdx, n->right);
    cp--;
}

void
radix_print(struct radix_t *rdx) {
    TRY(_radix_traverse_ok(rdx, 1), return);

    lockmgr(&rdx->lock, LK_SHARED);
    TRY(_radix_is_ok(rdx, rdx->root), goto err);
    if (rdx->mask != NULL)
        TRY(_radix_is_ok(rdx->mask, rdx->mask->root), goto err);

    _radix_print(rdx, rdx->root);
    if (rdx->mask != NULL)
        radix_print(rdx->mask);

err:
    lockmgr(&rdx->lock, LK_RELEASE);
}


static inline struct radix_node*
_radix_search(struct radix_t *rdx, void *addr) {
    struct radix_node *n;
    int bit;
    
    for (bit = 0, n = rdx->root; bit < rdx->bit; bit++) {
        if (n->is_leaf) {
            if (!_radix_addr_compare(addr, n->addr, rdx->bit))
                return n;
        }
        if (BIT(addr, bit)) {
            if (n->right == NULL)
                return NULL;
            n = n->right;
        } else {
            if (n->left == NULL)
                return NULL;
            n = n->left;
        }
    }
    if (bit == rdx->bit) {
        ASSERT(n != NULL && n->is_leaf);
        ASSERT(!_radix_addr_compare(addr, n->addr, rdx->bit));
        return n;
    }
    return NULL;
}

int
radix_search(struct radix_t *rdx, void **raddr, void *addr, void *mask) {
    struct radix_node *n;
    struct radix_mask_node *m;
    int ret = 0;
    
    *raddr = NULL;
    TRY(addr != NULL, return EINVAL);
    TRY(_radix_traverse_ok(rdx, !(mask == NULL)), return EINVAL);

    lockmgr(&rdx->lock, LK_SHARED);
    if (mask != NULL)
        lockmgr(&rdx->mask->lock, LK_SHARED);

    n = _radix_search(rdx, addr);
    if (n == NULL) {
        ret = EAGAIN;
        goto err;
    }
    DDUMP(n->addr, rdx->bit);
    if (mask != NULL) {
        for (m = n->mask; m != NULL; m = m->next) {
            ASSERT(m->mask != NULL);
            ASSERT(m->mask->addr != NULL);
            if (!_radix_addr_compare(mask, m->mask->addr, rdx->bit))
                break;
        }
        if (m == NULL) {
            ret = EAGAIN;
            goto err;
        }
    }
    *raddr = n->addr;

err:
    lockmgr(&rdx->lock, LK_RELEASE);
    if (mask != NULL)
        lockmgr(&rdx->mask->lock, LK_RELEASE);
    return ret;
}

int
radix_delete(struct radix_t *rdx, void *addr) {
    struct radix_node *n;
    struct radix_mask_node *m, *m0;
    int ret = 0;

    TRY(addr != NULL, return EINVAL);
    TRY(_radix_traverse_ok(rdx, 0), return EINVAL);
    TRY(!rdx->walk, return EBUSY);
    TRY(!rdx->is_mask, return 0);

    lockmgr(&rdx->lock, LK_EXCLUSIVE);

    n = _radix_search(rdx, addr);
    if (n == NULL) {
        ret = EAGAIN;
        goto end;
    }

    DDUMP(n->addr, rdx->bit);

    for (m = n->mask; m != NULL; m = m0) {
        m0 = m->next;
        FREE(m);
    }
    n->mask = NULL;

    if (n->parent->left == n)
        n->parent->left = NULL;
    else
        n->parent->right = NULL;
    FREE(n);

end:
    lockmgr(&rdx->lock, LK_RELEASE);
    return ret;
}

static inline void
_radix_walk(struct radix_node *n, radix_walk_func f, int mask) {
    struct radix_mask_node *m;
    static int stop;

    if (stop || n == NULL) return;

    if (!n->is_leaf)
        goto next;

    if (!mask) {
        if (f(n->addr, NULL))
            stop = 1;
        goto next;
    }

    for (m = n->mask; m != NULL; m = m->next) {
        ASSERT(m->mask != NULL);
        ASSERT(m->mask->addr != NULL);
        if (f(n->addr, m->mask->addr)) {
            stop = 1;
            break;
        }
    }

next:
    _radix_walk(n->left, f, mask);
    _radix_walk(n->right, f, mask);
}

int
radix_walk(struct radix_t *rdx, radix_walk_func f, int mask) {
    TRY(_radix_traverse_ok(rdx, 0), return EINVAL);
    lockmgr(&rdx->lock, LK_SHARED);
    rdx->walk = 1;
    _radix_walk(rdx->root, f, mask);
    rdx->walk = 0;
    lockmgr(&rdx->lock, LK_RELEASE);
    return 0;
}
