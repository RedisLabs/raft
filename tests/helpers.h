#ifndef _HELPERS_H
#define _HELPERS_H

#include "linked_list_queue.h"

static raft_entry_t *__MAKE_ENTRY(int id, raft_term_t term, const char *data)
{
    raft_entry_t *ety = raft_entry_new(data ? strlen(data) : 0);
    ety->id = id;
    ety->term = term;
    if (data) {
        memcpy(ety->data, data, strlen(data));
    }
    return ety;
}

static raft_entry_t **__MAKE_ENTRY_ARRAY(int id, raft_term_t term, const char *data)
{
    raft_entry_t **array = calloc(1, sizeof(raft_entry_t *));
    array[0] = __MAKE_ENTRY(id, term, data);

    return array;
}

static raft_entry_t **__MAKE_ENTRY_ARRAY_SEQ_ID(int count, int start_id, raft_term_t term, const char *data)
{
    raft_entry_t **array = calloc(count, sizeof(raft_entry_t *));
    int i;

    for (i = 0; i < count; i++) {
        array[i] = __MAKE_ENTRY(start_id++, term, data);
    }

    return array;
}

static void __RAFT_APPEND_ENTRY(void *r, int id, raft_term_t term, const char *data)
{
    raft_entry_t *e = __MAKE_ENTRY(id, term, data);
    raft_append_entry(r, e);
}

static void __RAFT_APPEND_ENTRIES_SEQ_ID(void *r, int count, int id, raft_term_t term, const char *data)
{
    int i;
    for (i = 0; i < count; i++) {
        raft_entry_t *e = __MAKE_ENTRY(id++, term, data);
        raft_append_entry(r, e);
    }
}

static void __RAFT_APPEND_ENTRIES_SEQ_ID_TERM(void *r, int count, int id, raft_term_t term, const char *data)
{
    int i;
    for (i = 0; i < count; i++) {
        raft_entry_t *e = __MAKE_ENTRY(id++, term++, data);
        raft_append_entry(r, e);
    }
}

static int __raft_persist_term(raft_server_t* raft, void *udata, raft_term_t term, int vote)
{
    return 0;
}

static int __raft_send_appendentries(raft_server_t* raft, void* udata, raft_node_t* node, msg_appendentries_t* msg)
{
    return 0;
}

static int __raft_persist_vote(raft_server_t* raft, void *udata, int vote)
{
    return 0;
}

static int __raft_send_requestvote(raft_server_t* raft, void* udata, raft_node_t* node, msg_requestvote_t* msg)
{
    return 0;
}

static int __raft_applylog(raft_server_t* raft, void *udata, raft_entry_t *ety, raft_index_t idx)
{
    return 0;
}

static int __raft_applylog_shutdown( raft_server_t* raft, void *udata, raft_entry_t *ety, raft_index_t idx)
{
    return RAFT_ERR_SHUTDOWN;
}

static int __raft_log_get_node_id(raft_server_t* raft, void *udata, raft_entry_t *entry, raft_index_t entry_idx)
{
    return atoi(entry->data);
}

static int __raft_log_offer(raft_server_t* raft, void* udata, raft_entry_t *entry, raft_index_t entry_idx)
{
    return 0;
}

static int __raft_node_has_sufficient_logs(raft_server_t* raft, void *user_data, raft_node_t* node)
{
    int *flag = (int*)user_data;
    *flag += 1;
    return 0;
}


static int __raft_send_appendentries_capture(raft_server_t* raft, void* udata, raft_node_t* node, msg_appendentries_t* msg)
{
    msg_appendentries_t* msg_captured = (msg_appendentries_t*)udata;
    memcpy(msg_captured, msg, sizeof(msg_appendentries_t));
    return 0;
}

static int __raft_send_snapshot_increment(raft_server_t* raft, void* udata, raft_node_t* node)
{
    int *counter = udata;

    (*counter)++;
    return 0;
}

static int __logentry_get_node_id(raft_server_t* raft, void *udata, raft_entry_t *ety, raft_index_t ety_idx)
{
    return 0;
}

static int __log_offer(raft_server_t* raft, void *user_data, raft_entry_t *entry, raft_index_t entry_idx)
{
    CuAssertIntEquals((CuTest*)raft, 1, entry_idx);
    return 0;
}

static int __log_pop(raft_server_t* raft, void *user_data, raft_entry_t *entry, raft_index_t entry_idx)
{
    raft_entry_t* copy = malloc(sizeof(*entry));
    memcpy(copy, entry, sizeof(*entry));
    llqueue_offer(user_data, copy);
    return 0;
}

static int __log_pop_failing(raft_server_t* raft, void *user_data, raft_entry_t *entry, raft_index_t entry_idx)
{
    return -1;
}

#endif
