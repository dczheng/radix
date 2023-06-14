#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/lock.h>

#define BIT_INDEX(b)        ((b)/8)
#define BIT_OFFSET(b)       (1 << (7 - (b) % 8))
#define BIT_BASE(v, b)      (((uint8_t*)(v)) + BIT_INDEX((b)))

#define BIT(v, b)           ((*BIT_BASE((v), (b)) & BIT_OFFSET((b))) ? 1 : 0)
#define BIT_SET(v, b)       (*BIT_BASE((v), (b)) |= BIT_OFFSET((b)))
#define BIT_UNSET(v, b)     (*BIT_BASE((v), (b)) &= ~BIT_OFFSET((b)))

#define TRYF(exp, next, fmt, ...) do { \
    if (!(exp)) { \
        kprintf("(%s %s %d) `%s FAILED` " fmt "\n", \
        __FILE__, __FUNCTION__, __LINE__, #exp, ##__VA_ARGS__); \
        next; \
    } \
} while(0)
#define TRY(exp, next) TRYF(exp, next, "")
#define DUMP_BIT(_v, _k) do { \
    if ((_v) != NULL) { \
        for (int i = 0; i < (_k); i++) \
            kprintf("%d", BIT((_v), i)); \
        kprintf("\n"); \
    } else { \
        kprintf("NULL\n"); \
    } \
} while(0)
#define DUMP(_v, _k) do { \
    if ((_v) != NULL) { \
        for (int i = 0; i < (_k); i += 8) \
            kprintf("%3d ", *BIT_BASE(_v, i)); \
        kprintf("\n"); \
    } else { \
        kprintf("NULL\n"); \
    } \
} while(0)

#define DEBUG
#ifdef DEBUG
#define DPRINTF(_fmt, ...) \
    kprintf("[%s] "_fmt, __FUNCTION__, ##__VA_ARGS__)
#define DPRINT_BOOL(_v) \
    DPRINTF("%s: %s\n", #_v, (_v) ? "TRUE" : "FALSE")

#define DDUMP_BIT(_v, _k) do { \
    kprintf("[%s] %s: ", __FUNCTION__, #_v); \
    DUMP_BIT((_v), (_k)); \
} while(0)
#define DDUMP(_v, _k) do { \
    kprintf("[%s] %s: ", __FUNCTION__, #_v); \
    DUMP((_v), (_k)); \
} while(0)
#else
#define DPRINTF(_fmt, ...)
#define DPRINT_BOOL(_v)
#define DDUMP(_v, _k)
#define DDUMP_BIT(_v, _k)
#endif // DEBUG

struct radix_node;
struct radix_mask_node {
    struct radix_node *mask;
    struct radix_mask_node *next;
};

struct radix_node {
    struct radix_node *left, *right, *parent;
    struct radix_mask_node *mask;
    void *addr;
    int is_leaf, bit;
};

struct radix_t {
    struct radix_node *root;
    struct radix_t *mask;
    struct lock lock;
    int bit, is_mask, walk;
    uint8_t *buf;
};

typedef int radix_walk_func(void*, void*);

int radix_init0(struct radix_t*, struct radix_t*, int);
int radix_init(struct radix_t*, int);
void radix_free0(struct radix_t*);
void radix_free(struct radix_t*);
int radix_insert(struct radix_t*, void*, void*);
void radix_print(struct radix_t*);
int radix_search(struct radix_t*, void**, void*, void*);
int radix_delete(struct radix_t*, void*);
int radix_walk(struct radix_t*, radix_walk_func, int);
