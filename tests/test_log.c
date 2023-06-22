#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "CuTest.h"

#include "linked_list_queue.h"

#include "raft.h"
#include "raft_log.h"
#include "raft_private.h"

#include "helpers.h"

static void __LOG_APPEND_ENTRY(void *l, int id, raft_term_t term, const char *data)
{
    raft_entry_t *e = __MAKE_ENTRY(id, term, data);
    raft_entry_hold(e); /* need an extra ref because tests assume it lives on */
    raft_log_append_entry(l, e);
}

static void __LOG_APPEND_ENTRIES_SEQ_ID(void *l, int count, int id, raft_term_t term, const char *data)
{
    int i;
    for (i = 0; i < count; i++) {
        raft_entry_t *e = __MAKE_ENTRY(id++, term, data);
        raft_entry_hold(e); /* need an extra ref because tests assume it lives on */
        raft_log_append_entry(l, e);
    }
}

static raft_node_id_t __get_node_id(
    raft_server_t* raft,
    void *udata,
    raft_entry_t *ety,
    raft_index_t ety_idx
    )
{
    return 0;
}

static int __log_offer(
    raft_server_t* raft,
    void *user_data,
    raft_entry_t *entry,
    raft_index_t entry_idx
    )
{
    CuAssertIntEquals((CuTest*)raft, 1, entry_idx);
    return 0;
}

static int __log_pop(
    raft_server_t* raft,
    void *user_data,
    raft_entry_t *entry,
    raft_index_t entry_idx
    )
{
    raft_entry_hold(entry);
    llqueue_offer(user_data, entry);
    return 0;
}

static int __log_pop_failing(
    raft_server_t* raft,
    void *user_data,
    raft_entry_t *entry,
    raft_index_t entry_idx
    )
{
    return -1;
}

raft_cbs_t funcs = {
    .get_node_id = __get_node_id};

raft_log_cbs_t log_funcs = {
    .log_pop = __log_pop
};

void TestLog_new_is_empty(CuTest * tc)
{
    void *l;

    l = raft_log_new();
    CuAssertTrue(tc, 0 == raft_log_count(l));
    raft_log_free(l);
}

void TestLog_append_is_not_empty(CuTest * tc)
{
    void *l;
    void *r = raft_new();
    raft_entry_t *e;

    l = raft_log_new();
    raft_log_cbs_t funcs = {
        .log_offer = __log_offer
    };
    raft_log_set_callbacks(l, &funcs, r);
    __LOG_APPEND_ENTRY(l, 1, 0, NULL);
    CuAssertIntEquals(tc, 1, raft_log_count(l));
    raft_log_poll(l, &e);
    raft_entry_release(e);

    raft_log_free(l);
    raft_destroy(r);
}

void TestLog_get_at_idx(CuTest * tc)
{
    void *l;
    raft_entry_t *e1, *e2, *e3;

    l = raft_log_new();
    __LOG_APPEND_ENTRIES_SEQ_ID(l, 3, 1, 0, NULL);
    CuAssertIntEquals(tc, 3, raft_log_count(l));

    e1 = raft_log_get_at_idx(l, 1);
    CuAssertIntEquals(tc, 1, e1->id);
    raft_entry_release(e1);

    e2 = raft_log_get_at_idx(l, 2);
    CuAssertIntEquals(tc, 2, e2->id);
    raft_entry_release(e2);

    e3 = raft_log_get_at_idx(l, 3);
    CuAssertIntEquals(tc, 3, e3->id);
    raft_entry_release(e3);

    raft_log_free(l);
}

void TestLog_get_at_idx_returns_null_where_out_of_bounds(CuTest * tc)
{
    void *l;

    l = raft_log_new();
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 0));
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 1));

    __LOG_APPEND_ENTRY(l, 1, 0, NULL);
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 2));

    raft_entry_t *e;
    raft_log_poll(l, &e);
    raft_entry_release(e);

    raft_log_free(l);
}

void TestLog_delete(CuTest * tc)
{
    void *l;
    raft_entry_t *e;

    void* queue = llqueue_new();
    void *r = raft_new();
    raft_cbs_t funcs = {
        .get_node_id = __get_node_id
    };
    raft_log_cbs_t log_funcs = {
        .log_pop = __log_pop
    };

    raft_set_callbacks(r, &funcs, queue);

    l = raft_log_new();
    raft_log_set_callbacks(l, &log_funcs, r);

    __LOG_APPEND_ENTRIES_SEQ_ID(l, 3, 1, 0, NULL);
    CuAssertIntEquals(tc, 3, raft_log_count(l));
    CuAssertIntEquals(tc, 3, raft_log_get_current_idx(l));

    e = raft_log_get_at_idx(l, 3);
    raft_log_delete(l, 3);
    CuAssertIntEquals(tc, 2, raft_log_count(l));
    raft_entry_release(e);
    e = llqueue_poll(queue);
    CuAssertIntEquals(tc, 3, e->id);
    CuAssertIntEquals(tc, 2, raft_log_count(l));
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 3));
    raft_entry_release(e);

    e = raft_log_get_at_idx(l, 2);
    raft_log_delete(l, 2);
    CuAssertIntEquals(tc, 1, raft_log_count(l));
    raft_entry_release(e);
    e = llqueue_poll(queue);
    CuAssertIntEquals(tc, 2, e->id);
    CuAssertIntEquals(tc, 1, raft_log_count(l));
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 2));
    raft_entry_release(e);

    e = raft_log_get_at_idx(l, 1);
    raft_log_delete(l, 1);
    CuAssertIntEquals(tc, 0, raft_log_count(l));
    raft_entry_release(e);
    e = llqueue_poll(queue);
    CuAssertIntEquals(tc, 1, e->id);
    CuAssertIntEquals(tc, 0, raft_log_count(l));
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 1));
    raft_entry_release(e);

    raft_log_free(l);
    llqueue_free(queue);
    raft_destroy(r);
}

void TestLog_delete_onwards(CuTest * tc)
{
    void* queue = llqueue_new();
    void *r = raft_new();
    raft_cbs_t funcs = {
        .get_node_id = __get_node_id};
    raft_log_cbs_t log_funcs = {
        .log_pop = __log_pop
    };
    raft_set_callbacks(r, &funcs, queue);

    void *l;

    l = raft_log_new();
    raft_log_set_callbacks(l, &log_funcs, r);

    __LOG_APPEND_ENTRIES_SEQ_ID(l, 3, 1, 0, NULL);
    CuAssertIntEquals(tc, 3, raft_log_count(l));

    /* even 3 gets deleted */
    raft_entry_t *e2 = raft_log_get_at_idx(l, 2);
    raft_entry_t *e3 = raft_log_get_at_idx(l, 3);

    raft_log_delete(l, 2);
    CuAssertIntEquals(tc, 1, raft_log_count(l));

    CuAssertIntEquals(tc, 1, raft_log_get_at_idx(l, 1)->id);
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 2));
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 3));

    raft_entry_release(e2);
    raft_entry_release(e3);

    raft_entry_t *e;

    while (raft_log_poll(l, &e) == 0) {
        raft_entry_release(e);
    }

    while ((e = llqueue_poll(queue)) != NULL) {
        raft_entry_release(e);
    }

    llqueue_free(queue);
    raft_log_free(l);
    raft_destroy(r);
}

void TestLog_delete_handles_log_pop_failure(CuTest * tc)
{
    void *l;

    void* queue = llqueue_new();
    void *r = raft_new();
    raft_cbs_t funcs = {
        .get_node_id = __get_node_id};
    raft_log_cbs_t log_funcs = {
        .log_pop = __log_pop_failing
    };
    raft_set_callbacks(r, &funcs, queue);

    l = raft_log_new();
    raft_log_set_callbacks(l, &log_funcs, r);

    __LOG_APPEND_ENTRIES_SEQ_ID(l, 3, 1, 0, NULL);
    CuAssertIntEquals(tc, 3, raft_log_count(l));
    CuAssertIntEquals(tc, 3, raft_log_get_current_idx(l));

    CuAssertIntEquals(tc, -1, raft_log_delete(l, 3));
    CuAssertIntEquals(tc, 3, raft_log_count(l));
    CuAssertIntEquals(tc, 3, raft_log_count(l));
    CuAssertIntEquals(tc, 3, ((raft_entry_t*) raft_log_peektail(l))->id);

    raft_entry_t *e;

    while (raft_log_poll(l, &e) == 0) {
        raft_entry_release(e);
    }

    while ((e = llqueue_poll(queue)) != NULL) {
        raft_entry_release(e);
    }

    llqueue_free(queue);
    raft_log_free(l);
    raft_destroy(r);
 }

void TestLog_delete_fails_for_idx_zero(CuTest * tc)
{
    void* queue = llqueue_new();
    void *r = raft_new();
    raft_cbs_t funcs = {
        .get_node_id = __get_node_id};
    raft_log_cbs_t log_funcs = {
        .log_pop = __log_pop
    };
    raft_set_callbacks(r, &funcs, queue);
    raft_log_set_callbacks(raft_get_log(r), &log_funcs, r);

    void *l;

    l = raft_log_alloc(1);
    raft_log_set_callbacks(l, &log_funcs, r);
    __LOG_APPEND_ENTRIES_SEQ_ID(l, 4, 1, 0, NULL);
    CuAssertIntEquals(tc, raft_log_delete(l, 0), -1);

    raft_entry_t *e;

    while (raft_log_poll(l, &e) == 0) {
        raft_entry_release(e);
    }

    while ((e = llqueue_poll(queue)) != NULL) {
        raft_entry_release(e);
    }

    raft_log_free(l);
    llqueue_free(queue);
    raft_destroy(r);
}

void TestLog_poll(CuTest * tc)
{
    void* queue = llqueue_new();
    void *r = raft_new();
    raft_cbs_t funcs = {
        .get_node_id = __get_node_id};
    raft_log_cbs_t log_funcs = {
        .log_pop = __log_pop
    };
    raft_set_callbacks(r, &funcs, queue);

    void *l;

    l = raft_log_new();
    raft_log_set_callbacks(l, &log_funcs, r);

    __LOG_APPEND_ENTRY(l, 1, 0, NULL);
    CuAssertIntEquals(tc, 1, raft_log_get_current_idx(l));

    __LOG_APPEND_ENTRY(l, 2, 0, NULL);
    CuAssertIntEquals(tc, 2, raft_log_get_current_idx(l));

    __LOG_APPEND_ENTRY(l, 3, 0, NULL);
    CuAssertIntEquals(tc, 3, raft_log_count(l));
    CuAssertIntEquals(tc, 3, raft_log_get_current_idx(l));

    raft_entry_t *ety;

    /* remove 1st */
    ety = NULL;
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertTrue(tc, NULL != ety);
    CuAssertIntEquals(tc, 2, raft_log_count(l));
    CuAssertIntEquals(tc, ety->id, 1);
    CuAssertIntEquals(tc, 1, raft_log_get_base(l));
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 1));
    CuAssertTrue(tc, NULL != raft_log_get_at_idx(l, 2));
    CuAssertTrue(tc, NULL != raft_log_get_at_idx(l, 3));
    CuAssertIntEquals(tc, 3, raft_log_get_current_idx(l));
    raft_entry_release(ety);

    /* remove 2nd */
    ety = NULL;
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertTrue(tc, NULL != ety);
    CuAssertIntEquals(tc, 1, raft_log_count(l));
    CuAssertIntEquals(tc, ety->id, 2);
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 1));
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 2));
    CuAssertTrue(tc, NULL != raft_log_get_at_idx(l, 3));
    CuAssertIntEquals(tc, 3, raft_log_get_current_idx(l));
    raft_entry_release(ety);

    /* remove 3rd */
    ety = NULL;
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertTrue(tc, NULL != ety);
    CuAssertIntEquals(tc, 0, raft_log_count(l));
    CuAssertIntEquals(tc, ety->id, 3);
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 1));
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 2));
    CuAssertTrue(tc, NULL == raft_log_get_at_idx(l, 3));
    CuAssertIntEquals(tc, 3, raft_log_get_current_idx(l));
    raft_entry_release(ety);

    llqueue_free(queue);
    raft_log_free(l);
    raft_destroy(r);
}

void TestLog_peektail(CuTest * tc)
{
    void *l;

    l = raft_log_new();

    __LOG_APPEND_ENTRIES_SEQ_ID(l, 3, 1, 0, NULL);
    CuAssertIntEquals(tc, 3, raft_log_count(l));
    CuAssertIntEquals(tc, 3, raft_log_peektail(l)->id);

    raft_entry_t *e;

    while (raft_log_poll(l, &e) == 0) {
        raft_entry_release(e);
    }

    raft_log_free(l);
}

#if 0
// TODO: duplicate testing not implemented yet
void T_estlog_cant_append_duplicates(CuTest * tc)
{
    void *l;
    raft_entry_t e;

    e.id = 1;

    l = log_new();
    CuAssertTrue(tc, 1 == log_append_entry(l, &e));
    CuAssertTrue(tc, 1 == log_count(l));
}
#endif

void TestLog_load_from_snapshot(CuTest * tc)
{
    void *l;

    l = raft_log_new();
    CuAssertIntEquals(tc, 0, raft_log_get_current_idx(l));
    CuAssertIntEquals(tc, 0, raft_log_load_from_snapshot(l, 10, 5));
    CuAssertIntEquals(tc, 10, raft_log_get_current_idx(l));
    CuAssertIntEquals(tc, 0, raft_log_count(l));

    raft_log_free(l);
}

void TestLog_load_from_snapshot_clears_log(CuTest * tc)
{
    void *l;

    l = raft_log_new();

    __LOG_APPEND_ENTRIES_SEQ_ID(l, 2, 1, 0, NULL);
    CuAssertIntEquals(tc, 2, raft_log_count(l));
    CuAssertIntEquals(tc, 2, raft_log_get_current_idx(l));

    raft_entry_t *e1 = raft_log_get_at_idx(l, 1);
    raft_entry_t *e2 = raft_log_get_at_idx(l, 2);

    CuAssertIntEquals(tc, 0, raft_log_load_from_snapshot(l, 10, 5));
    CuAssertIntEquals(tc, 0, raft_log_count(l));
    CuAssertIntEquals(tc, 10, raft_log_get_current_idx(l));

    raft_entry_release(e1);
    raft_entry_release(e2);

    raft_entry_t *e;

    while (raft_log_poll(l, &e) == 0) {
        raft_entry_release(e);
    }

    raft_log_free(l);
}

void TestLog_front_pushes_across_boundary(CuTest * tc)
{
    void* queue = llqueue_new();
    void *r = raft_new();
    raft_set_callbacks(r, &funcs, queue);
    raft_log_set_callbacks(raft_get_log(r), &log_funcs, r);

    void *l;

    l = raft_log_alloc(1);
    raft_log_set_callbacks(l, &log_funcs, r);

    raft_entry_t* ety;

    __LOG_APPEND_ENTRY(l, 1, 0, NULL);
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 1);
    raft_entry_release(ety);

    __LOG_APPEND_ENTRY(l, 2, 0, NULL);
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 2);
    raft_entry_release(ety);

    llqueue_free(queue);
    raft_log_free(l);
    raft_destroy(r);
}

void TestLog_front_and_back_pushed_across_boundary_with_enlargement_required(CuTest * tc)
{
    void *l;

    l = raft_log_alloc(1);

    raft_entry_t* ety;

    /* append */
    __LOG_APPEND_ENTRY(l, 1, 0, NULL);

    /* poll */
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 1);
    raft_entry_release(ety);

    /* append */
    __LOG_APPEND_ENTRY(l, 2, 0, NULL);

    /* poll */
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 2);
    raft_entry_release(ety);

    /* append append */
    __LOG_APPEND_ENTRY(l, 3, 0, NULL);
    __LOG_APPEND_ENTRY(l, 4, 0, NULL);

    /* poll */
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 3);
    raft_entry_release(ety);

    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 4);
    raft_entry_release(ety);

    raft_log_free(l);
}

void TestLog_delete_after_polling(CuTest * tc)
{
    void *l;

    l = raft_log_alloc(1);

    raft_entry_t* ety;

    /* append */
    __LOG_APPEND_ENTRY(l, 1, 0, NULL);
    CuAssertIntEquals(tc, 1, raft_log_count(l));

    /* poll */
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 1);
    CuAssertIntEquals(tc, 0, raft_log_count(l));
    raft_entry_release(ety);

    /* append */
    __LOG_APPEND_ENTRY(l, 2, 0, NULL);
    CuAssertIntEquals(tc, 1, raft_log_count(l));

    /* poll */
    raft_entry_t *e = raft_log_get_at_idx(l, 2);
    CuAssertIntEquals(tc, raft_log_delete(l, 1), 0);
    CuAssertIntEquals(tc, 0, raft_log_count(l));
    raft_entry_release(e);

    raft_log_free(l);
}

void TestLog_delete_after_polling_from_double_append(CuTest * tc)
{
    void* queue = llqueue_new();
    void *r = raft_new();
    raft_cbs_t funcs = {
        .get_node_id = __get_node_id};
    raft_log_cbs_t log_funcs = {
        .log_pop = __log_pop
    };
    raft_set_callbacks(r, &funcs, queue);

    void *l;

    l = raft_log_alloc(1);
    raft_log_set_callbacks(l, &log_funcs, r);

    raft_entry_t* ety;

    /* append append */
    __LOG_APPEND_ENTRIES_SEQ_ID(l, 2, 1, 0, NULL);
    CuAssertIntEquals(tc, 2, raft_log_count(l));

    /* poll */
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 1);
    CuAssertIntEquals(tc, 1, raft_log_count(l));
    raft_entry_release(ety);

    /* append */
    __LOG_APPEND_ENTRY(l, 3, 0, NULL);
    CuAssertIntEquals(tc, 2, raft_log_count(l));

    /* poll */
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 2);
    CuAssertIntEquals(tc, 1, raft_log_count(l));
    raft_entry_release(ety);

    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 3);
    CuAssertIntEquals(tc, 0, raft_log_count(l));
    raft_entry_release(ety);

    llqueue_free(queue);
    raft_log_free(l);
    raft_destroy(r);
}

void TestLog_get_from_idx_with_base_off_by_one(CuTest * tc)
{
    void* queue = llqueue_new();
    void *r = raft_new();
    raft_cbs_t funcs = {
        .get_node_id = __get_node_id};
    raft_log_cbs_t log_funcs = {
        .log_pop = __log_pop
    };
    raft_set_callbacks(r, &funcs, queue);

    void *l;

    l = raft_log_alloc(1);
    raft_log_set_callbacks(l, &log_funcs, r);

    raft_entry_t* ety;

    /* append append */
    __LOG_APPEND_ENTRIES_SEQ_ID(l, 2, 1, 0, NULL);
    CuAssertIntEquals(tc, 2, raft_log_count(l));

    /* poll */
    CuAssertIntEquals(tc, raft_log_poll(l, (void *) &ety), 0);
    CuAssertIntEquals(tc, ety->id, 1);
    CuAssertIntEquals(tc, 1, raft_log_count(l));
    raft_entry_release(ety);

    /* get off-by-one index */
    long n_etys;
    CuAssertPtrEquals(tc, raft_log_get_from_idx(l, 1, &n_etys), NULL);
    CuAssertIntEquals(tc, n_etys, 0);

    /* now get the correct index */
    raft_entry_t** e;
    e = raft_log_get_from_idx(l, 2, &n_etys);
    CuAssertPtrNotNull(tc, e);
    CuAssertIntEquals(tc, n_etys, 1);
    CuAssertIntEquals(tc, e[0]->id, 2);

    raft_entry_t *entry;
    while (raft_log_poll(l, &entry) == 0) {
        raft_entry_release(entry);
    }

    llqueue_free(queue);
    raft_log_free(l);
    raft_destroy(r);
}

int main(void)
{
    CuString *output = CuStringNew();
    CuSuite* suite = CuSuiteNew();

    SUITE_ADD_TEST(suite, TestLog_new_is_empty);
    SUITE_ADD_TEST(suite, TestLog_append_is_not_empty);
    SUITE_ADD_TEST(suite, TestLog_get_at_idx);
    SUITE_ADD_TEST(suite, TestLog_get_at_idx_returns_null_where_out_of_bounds);
    SUITE_ADD_TEST(suite, TestLog_delete);
    SUITE_ADD_TEST(suite, TestLog_delete_onwards);
    SUITE_ADD_TEST(suite, TestLog_delete_handles_log_pop_failure);
    SUITE_ADD_TEST(suite, TestLog_delete_fails_for_idx_zero);
    SUITE_ADD_TEST(suite, TestLog_poll);
    SUITE_ADD_TEST(suite, TestLog_peektail);
    SUITE_ADD_TEST(suite, TestLog_load_from_snapshot);
    SUITE_ADD_TEST(suite, TestLog_load_from_snapshot_clears_log);
    SUITE_ADD_TEST(suite, TestLog_front_pushes_across_boundary);
    SUITE_ADD_TEST(suite, TestLog_front_and_back_pushed_across_boundary_with_enlargement_required);
    SUITE_ADD_TEST(suite, TestLog_delete_after_polling);
    SUITE_ADD_TEST(suite, TestLog_delete_after_polling_from_double_append);
    SUITE_ADD_TEST(suite, TestLog_get_from_idx_with_base_off_by_one);

    CuSuiteRun(suite);
    CuSuiteDetails(suite, output);
    printf("%s\n", output->buffer);

    int rc = suite->failCount == 0 ? 0 : 1;

    CuStringFree(output);
    CuSuiteFree(suite);

    return rc;
}
