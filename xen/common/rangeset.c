/******************************************************************************
 * rangeset.c
 * 
 * Creation, maintenance and automatic destruction of per-domain sets of
 * numeric ranges.
 * 
 * Copyright (c) 2005, K A Fraser
 */

#include <xen/sched.h>
#include <xen/errno.h>
#include <xen/rangeset.h>
#include <xsm/xsm.h>

/* An inclusive range [s,e] and pointer to next range in ascending order. */
struct range {
    struct list_head list;
    unsigned long s, e;
};

struct rangeset {
    /* Owning domain and threaded list of rangesets. */
    struct list_head rangeset_list;
    struct domain   *domain;

    /* Ordered list of ranges contained in this set, and protecting lock. */
    struct list_head range_list;
    spinlock_t       lock;

    /* Pretty-printing name. */
    char             name[32];

    /* RANGESETF flags. */
    unsigned int     flags;
};

/*****************************
 * Private range functions hide the underlying linked-list implemnetation.
 */

/* Find highest range lower than or containing s. NULL if no such range. */
static struct range *find_range(
    struct rangeset *r, unsigned long s)
{
    struct range *x = NULL, *y;

    list_for_each_entry ( y, &r->range_list, list )
    {
        if ( y->s > s )
            break;
        x = y;
    }

    return x;
}

/* Return the lowest range in the set r, or NULL if r is empty. */
static struct range *first_range(
    struct rangeset *r)
{
    if ( list_empty(&r->range_list) )
        return NULL;
    return list_entry(r->range_list.next, struct range, list);
}

/* Return range following x in ascending order, or NULL if x is the highest. */
static struct range *next_range(
    struct rangeset *r, struct range *x)
{
    if ( x->list.next == &r->range_list )
        return NULL;
    return list_entry(x->list.next, struct range, list);
}

/* Insert range y after range x in r. Insert as first range if x is NULL. */
static void insert_range(
    struct rangeset *r, struct range *x, struct range *y)
{
    list_add(&y->list, (x != NULL) ? &x->list : &r->range_list);
}

/* Remove a range from its list and free it. */
static void destroy_range(
    struct range *x)
{
    list_del(&x->list);
    xfree(x);
}

/*****************************
 * Core public functions
 */

int rangeset_add_range(
    struct rangeset *r, unsigned long s, unsigned long e)
{
    struct range *x, *y;
    int rc = 0;

    ASSERT(s <= e);

    spin_lock(&r->lock);

    x = find_range(r, s);
    y = find_range(r, e);

    if ( x == y )
    {
        if ( (x == NULL) || ((x->e < s) && ((x->e + 1) != s)) )
        {
            x = xmalloc(struct range);
            if ( x == NULL )
            {
                rc = -ENOMEM;
                goto out;
            }

            x->s = s;
            x->e = e;

            insert_range(r, y, x);
        }
        else if ( x->e < e )
            x->e = e;
    }
    else
    {
        if ( x == NULL )
        {
            x = first_range(r);
            x->s = s;
        }
        else if ( (x->e < s) && ((x->e + 1) != s) )
        {
            x = next_range(r, x);
            x->s = s;
        }
        
        x->e = (y->e > e) ? y->e : e;

        for ( ; ; )
        {
            y = next_range(r, x);
            if ( (y == NULL) || (y->e > x->e) )
                break;
            destroy_range(y);
        }
    }

    y = next_range(r, x);
    if ( (y != NULL) && ((x->e + 1) == y->s) )
    {
        x->e = y->e;
        destroy_range(y);
    }

 out:
    spin_unlock(&r->lock);
    return rc;
}

int rangeset_remove_range(
    struct rangeset *r, unsigned long s, unsigned long e)
{
    struct range *x, *y, *t;
    int rc = 0;

    ASSERT(s <= e);

    spin_lock(&r->lock);

    x = find_range(r, s);
    y = find_range(r, e);

    if ( x == y )
    {
        if ( (x == NULL) || (x->e < s) )
            goto out;

        if ( (x->s < s) && (x->e > e) )
        {
            y = xmalloc(struct range);
            if ( y == NULL )
            {
                rc = -ENOMEM;
                goto out;
            }

            y->s = e + 1;
            y->e = x->e;
            x->e = s - 1;

            insert_range(r, x, y);
        }
        else if ( (x->s == s) && (x->e <= e) )
            destroy_range(x);
        else if ( x->s == s )
            x->s = e + 1;
        else if ( x->e <= e )
            x->e = s - 1;
    }
    else
    {
        if ( x == NULL )
            x = first_range(r);

        if ( x->s < s )
        {
            x->e = s - 1;
            x = next_range(r, x);
        }

        while ( x != y )
        {
            t = x;
            x = next_range(r, x);
            destroy_range(t);
        }

        x->s = e + 1;
        if ( x->s > x->e )
            destroy_range(x);
    }

 out:
    spin_unlock(&r->lock);
    return rc;
}

int rangeset_contains_range(
    struct rangeset *r, unsigned long s, unsigned long e)
{
    struct range *x;
    int contains;

    ASSERT(s <= e);

    spin_lock(&r->lock);
    x = find_range(r, s);
    contains = (x && (x->e >= e));
    spin_unlock(&r->lock);

    return contains;
}

int rangeset_overlaps_range(
    struct rangeset *r, unsigned long s, unsigned long e)
{
    struct range *x;
    int overlaps;

    ASSERT(s <= e);

    spin_lock(&r->lock);
    x = find_range(r, e);
    overlaps = (x && (s <= x->e));
    spin_unlock(&r->lock);

    return overlaps;
}

int rangeset_report_ranges(
    struct rangeset *r, unsigned long s, unsigned long e,
    int (*cb)(unsigned long s, unsigned long e, void *), void *ctxt)
{
    struct range *x;
    int rc = 0;

    spin_lock(&r->lock);

    for ( x = find_range(r, s); x && (x->s <= e) && !rc; x = next_range(r, x) )
        if ( x->e >= s )
            rc = cb(max(x->s, s), min(x->e, e), ctxt);

    spin_unlock(&r->lock);

    return rc;
}

int rangeset_add_singleton(
    struct rangeset *r, unsigned long s)
{
    return rangeset_add_range(r, s, s);
}

int rangeset_remove_singleton(
    struct rangeset *r, unsigned long s)
{
    return rangeset_remove_range(r, s, s);
}

int rangeset_contains_singleton(
    struct rangeset *r, unsigned long s)
{
    return rangeset_contains_range(r, s, s);
}

int rangeset_is_empty(
    struct rangeset *r)
{
    return ((r == NULL) || list_empty(&r->range_list));
}

struct rangeset *rangeset_new(
    struct domain *d, char *name, unsigned int flags)
{
    struct rangeset *r;

    r = xmalloc(struct rangeset);
    if ( r == NULL )
        return NULL;

    spin_lock_init(&r->lock);
    INIT_LIST_HEAD(&r->range_list);

    BUG_ON(flags & ~RANGESETF_prettyprint_hex);
    r->flags = flags;

    if ( name != NULL )
    {
        safe_strcpy(r->name, name);
    }
    else
    {
        snprintf(r->name, sizeof(r->name), "(no name)");
    }

    if ( (r->domain = d) != NULL )
    {
        spin_lock(&d->rangesets_lock);
        list_add(&r->rangeset_list, &d->rangesets);
        spin_unlock(&d->rangesets_lock);
    }

    return r;
}

void rangeset_destroy(
    struct rangeset *r)
{
    struct range *x;

    if ( r == NULL )
        return;

    if ( r->domain != NULL )
    {
        spin_lock(&r->domain->rangesets_lock);
        list_del(&r->rangeset_list);
        spin_unlock(&r->domain->rangesets_lock);
    }

    while ( (x = first_range(r)) != NULL )
        destroy_range(x);

    xfree(r);
}

void rangeset_domain_initialise(
    struct domain *d)
{
    INIT_LIST_HEAD(&d->rangesets);
    spin_lock_init(&d->rangesets_lock);
}

void rangeset_domain_destroy(
    struct domain *d)
{
    struct rangeset *r;

    while ( !list_empty(&d->rangesets) )
    {
        r = list_entry(d->rangesets.next, struct rangeset, rangeset_list);

        BUG_ON(r->domain != d);
        r->domain = NULL;
        list_del(&r->rangeset_list);

        rangeset_destroy(r);
    }
}

/*****************************
 * Pretty-printing functions
 */

static void print_limit(struct rangeset *r, unsigned long s)
{
    printk((r->flags & RANGESETF_prettyprint_hex) ? "%lx" : "%lu", s);
}

void rangeset_printk(
    struct rangeset *r)
{
    int nr_printed = 0;
    struct range *x;

    spin_lock(&r->lock);

    printk("%-10s {", r->name);

    for ( x = first_range(r); x != NULL; x = next_range(r, x) )
    {
        if ( nr_printed++ )
            printk(",");
        printk(" ");
        print_limit(r, x->s);
        if ( x->s != x->e )
        {
            printk("-");
            print_limit(r, x->e);
        }
    }

    printk(" }");

    spin_unlock(&r->lock);
}

void rangeset_domain_printk(
    struct domain *d)
{
    struct rangeset *r;

    printk("Rangesets belonging to domain %u:\n", d->domain_id);

    spin_lock(&d->rangesets_lock);

    if ( list_empty(&d->rangesets) )
        printk("    None\n");

    list_for_each_entry ( r, &d->rangesets, rangeset_list )
    {
        printk("    ");
        rangeset_printk(r);
        printk("\n");
    }

    spin_unlock(&d->rangesets_lock);
}
