#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <netinet/ip.h>

#ifndef DEBUG
#define DEBUG
#endif

#include "radix.h"

struct radix_t rdx;

#define N 30
#define NBIT 32

static inline int
test_radix_walk(void *addr, void *mask) {
    DDUMP(addr, NBIT);
    DDUMP(mask, NBIT);
    return 0;
}

static int
test_radix_load(void) {
    uint32_t v, addr[N], mask[N], *a, *m,
        a_search, m_search, *r_search, a_delete;
    int ret = 0, b1, b2;

    DPRINTF("*.*\n");

    v = 0;
    b1 = 2;
    b2 = 8;
    DDUMP_BIT(&v, NBIT);
    BIT_SET(&v, b1);
    BIT_SET(&v, b2);
    DDUMP_BIT(&v, NBIT);
    BIT_UNSET(&v, b2);
    DDUMP_BIT(&v, NBIT);
    DPRINTF("bit %d is set: %d\n", b1, BIT(&v, b1));
    DPRINTF("bit %d is set: %d\n", b2, BIT(&v, b2));

    TRY(!(ret = radix_init(&rdx, NBIT)), goto err);

    a = &addr[0];
    m = &mask[0];

#define ADDR_SET(a, a1, a2, a3, a4) \
        a = htonl(((long)a1 << 24) + ((long)a2 << 16) + ((long)a3 << 8) + (long)a4);

#define _INSERT(a1, a2, a3, a4, m1, m2, m3, m4) do { \
    if (a - addr >= N) { \
        DPRINTF("Too many address\n"); \
    } else { \
        ADDR_SET(*a, a1, a2, a3, a4); \
        ADDR_SET(*m, m1, m2, m3, m4); \
        TRY(!(ret = radix_insert(&rdx, a, m)), goto err); \
        a++; \
        m++; \
        radix_print(&rdx); \
    } \
} while(0)

    _INSERT(192, 168,   1,   1,   0, 255,   0,   0);
    _INSERT(192, 168,   1,   1,   0, 255,   0,   0);
    _INSERT(192, 168,   1,   1,   0, 128,   0,   0);
    _INSERT(192, 168,   1,   0, 255, 255, 255,   0);
    _INSERT(192, 168,   1,   1, 255, 255, 255,   0);
    _INSERT(192, 168,   1,   1, 255,   0,   0,   0);
    _INSERT(192, 168,   1,   1, 255, 255,   0,   0);
    _INSERT(192, 168,   1,   1, 253,   0,   0,   0);
    _INSERT(192, 168,   1,   1, 254,   0,   0,   0);

    ADDR_SET(a_search, 192, 168,   1,   0);
    ADDR_SET(m_search, 255, 255, 255,   0);
    DDUMP(&a_search, NBIT);
    DDUMP(&m_search, NBIT);
    TRY(!(ret = radix_search(&rdx, (void**)&r_search, &a_search, &m_search)), goto err);
    DDUMP(r_search, NBIT);

    ADDR_SET(a_delete, 192, 168,   1,   1);
    TRY(!(ret = radix_delete(&rdx, &a_delete)), goto err);
    radix_print(&rdx);

    _INSERT(192, 168,   2,   1, 255,   0,   0,   0);
    _INSERT(192, 168,   2,   1, 255, 255,   0,   0);
    _INSERT(192, 168,   2,   1, 255, 255, 244,   0);

    TRY(!(ret = radix_walk(&rdx, test_radix_walk, 1)), goto err);

err:
    return ret;
}

static void
test_radix_unload(void) {
    DPRINTF("*.*\n");
    radix_free(&rdx);
}

static int
test_radix_handler(module_t mod, int what, void *arg) {
	switch (what) {
		case MOD_LOAD:
            test_radix_load();
			break;
		case MOD_UNLOAD:
            test_radix_unload();
			break;
		default:
			return (EOPNOTSUPP);
	}
	return (0);
}

static moduledata_t moduledata = {
	"test_radix",
    test_radix_handler,
	NULL
};

DECLARE_MODULE(test_radix, moduledata, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(test_radix, 1);
