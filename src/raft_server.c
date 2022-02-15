/**
 * Copyright (c) 2013, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * @file
 * @brief Implementation of a Raft server
 * @author Willem Thiart himself@willemthiart.com
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>

#include "raft.h"
#include "raft_private.h"

#ifndef min
    #define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
    #define max(a, b) ((a) < (b) ? (b) : (a))
#endif

#ifndef __GNUC__
    #define __attribute__(a)
#endif

void *(*raft_malloc)(size_t) = malloc;
void *(*raft_calloc)(size_t, size_t) = calloc;
void *(*raft_realloc)(void *, size_t) = realloc;
void (*raft_free)(void *) = free;

void raft_set_heap_functions(void *(*_malloc)(size_t),
                             void *(*_calloc)(size_t, size_t),
                             void *(*_realloc)(void *, size_t),
                             void (*_free)(void *))
{
    raft_malloc = _malloc;
    raft_calloc = _calloc;
    raft_realloc = _realloc;
    raft_free = _free;
}

static void raft_log_node(raft_server_t *me,
                          raft_node_id_t id,
                          const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));

static void raft_log_node(raft_server_t *me,
                          raft_node_id_t id,
                          const char *fmt, ...)
{
    if (me->cb.log == NULL)
        return;

    char buf[1024];
    va_list args;

    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n < 0) {
        buf[0] = '\0';
    }
    va_end(args);

    me->cb.log(me, id, me->udata, buf);
}

#define raft_log(me, ...) (raft_log_node(me, RAFT_NODE_ID_NONE, __VA_ARGS__))

void raft_randomize_election_timeout(raft_server_t* me)
{
    /* [election_timeout, 2 * election_timeout) */
    me->election_timeout_rand = me->election_timeout + rand() % me->election_timeout;
    raft_log(me, "randomize election timeout to %d", me->election_timeout_rand);
}

void raft_update_quorum_meta(raft_server_t* me, raft_msg_id_t id)
{
    // Make sure that timeout is greater than 'randomized election timeout'
    me->quorum_timeout = me->election_timeout * 2;
    me->last_acked_msg_id = id;
}

int raft_clear_incoming_snapshot(raft_server_t* me, raft_index_t new_idx)
{
    int e = 0;

    if (me->snapshot_recv_idx != 0)
        e = me->cb.clear_snapshot(me, me->udata);

    me->snapshot_recv_idx = new_idx;
    me->snapshot_recv_offset = 0;

    return e;
}

raft_server_t* raft_new_with_log(const raft_log_impl_t *log_impl, void *log_arg)
{
    raft_server_t* me = raft_calloc(1, sizeof(raft_server_t));
    if (!me)
        return NULL;

    me->current_term = 0;
    me->voted_for = -1;
    me->timeout_elapsed = 0;
    me->request_timeout = 200;
    me->election_timeout = 1000;
    me->node_transferring_leader_to = RAFT_NODE_ID_NONE;
    me->auto_flush = 1;

    raft_update_quorum_meta(me, me->msg_id);

    raft_randomize_election_timeout(me);
    me->log_impl = log_impl;
    me->log = me->log_impl->init(me, log_arg);
    if (!me->log) {
        raft_free(me);
        return NULL;
    }

    me->voting_cfg_change_log_idx = -1;
    raft_set_state(me, RAFT_STATE_FOLLOWER);
    me->leader_id = RAFT_NODE_ID_NONE;

    me->snapshot_in_progress = 0;
    raft_set_snapshot_metadata(me, 0, 0);

    return me;
}

raft_server_t* raft_new(void)
{
    return raft_new_with_log(&raft_log_internal_impl, NULL);
}

void raft_set_callbacks(raft_server_t* me, raft_cbs_t* funcs, void* udata)
{
    memcpy(&me->cb, funcs, sizeof(raft_cbs_t));
    me->udata = udata;
}

void raft_set_election_timeout(raft_server_t* me, int millisec)
{
    me->election_timeout = millisec;

    raft_update_quorum_meta(me, me->last_acked_msg_id);
    raft_randomize_election_timeout(me);
}

void raft_set_request_timeout(raft_server_t* me, int millisec)
{
    me->request_timeout = millisec;
}

raft_node_id_t raft_get_nodeid(raft_server_t* me)
{
    if (!me->node)
        return -1;
    return raft_node_get_id(me->node);
}

int raft_get_election_timeout(raft_server_t* me)
{
    return me->election_timeout;
}

int raft_get_request_timeout(raft_server_t* me)
{
    return me->request_timeout;
}

int raft_get_num_nodes(raft_server_t* me)
{
    return me->num_nodes;
}

int raft_get_num_voting_nodes(raft_server_t* me)
{
    int i, num = 0;
    for (i = 0; i < me->num_nodes; i++)
        if (raft_node_is_voting(me->nodes[i]))
            num++;
    return num;
}

int raft_get_timeout_elapsed(raft_server_t* me)
{
    return me->timeout_elapsed;
}

raft_index_t raft_get_log_count(raft_server_t* me)
{
    return me->log_impl->count(me->log);
}

int raft_get_voted_for(raft_server_t* me)
{
    return me->voted_for;
}

int raft_set_current_term(raft_server_t* me, const raft_term_t term)
{
    if (me->current_term < term)
    {
        int voted_for = -1;
        if (me->cb.persist_term)
        {
            int e = me->cb.persist_term(me, me->udata, term, voted_for);
            if (0 != e)
                return e;
        }
        me->current_term = term;
        me->voted_for = voted_for;
        me->max_seen_msg_id = 0;
    }
    return 0;
}

raft_term_t raft_get_current_term(raft_server_t* me)
{
    return me->current_term;
}

raft_index_t raft_get_current_idx(raft_server_t* me)
{
    return me->log_impl->current_idx(me->log);
}

void raft_set_commit_idx(raft_server_t* me, raft_index_t idx)
{
    assert(me->commit_idx <= idx);
    assert(idx <= raft_get_current_idx(me));
    me->commit_idx = idx;
}

void raft_set_last_applied_idx(raft_server_t* me, raft_index_t idx)
{
    me->last_applied_idx = idx;
}

raft_index_t raft_get_last_applied_idx(raft_server_t* me)
{
    return me->last_applied_idx;
}

raft_index_t raft_get_commit_idx(raft_server_t* me)
{
    return me->commit_idx;
}

void raft_set_state(raft_server_t* me, int state)
{
    /* if became the leader, then update the current leader entry */
    if (state == RAFT_STATE_LEADER)
        me->leader_id = raft_node_get_id(me->node);
    me->state = state;
}

int raft_get_state(raft_server_t* me)
{
    return me->state;
}

raft_node_t* raft_get_node(raft_server_t *me, raft_node_id_t nodeid)
{
    int i;

    for (i = 0; i < me->num_nodes; i++)
        if (nodeid == raft_node_get_id(me->nodes[i]))
            return me->nodes[i];

    return NULL;
}

raft_node_t* raft_get_my_node(raft_server_t *me)
{
    int i;

    for (i = 0; i < me->num_nodes; i++)
        if (raft_get_nodeid(me) == raft_node_get_id(me->nodes[i]))
            return me->nodes[i];

    return NULL;
}

raft_node_t* raft_get_node_from_idx(raft_server_t* me, const raft_index_t idx)
{
    return me->nodes[idx];
}

raft_node_id_t raft_get_leader_id(raft_server_t* me)
{
    return me->leader_id;
}

raft_node_t* raft_get_leader_node(raft_server_t* me)
{
    return raft_get_node(me, me->leader_id);
}

void* raft_get_udata(raft_server_t* me)
{
    return me->udata;
}

int raft_is_follower(raft_server_t* me)
{
    return raft_get_state(me) == RAFT_STATE_FOLLOWER;
}

int raft_is_leader(raft_server_t* me)
{
    return raft_get_state(me) == RAFT_STATE_LEADER;
}

int raft_is_precandidate(raft_server_t* me)
{
    return raft_get_state(me) == RAFT_STATE_PRECANDIDATE;
}

int raft_is_candidate(raft_server_t* me)
{
    return raft_get_state(me) == RAFT_STATE_CANDIDATE;
}

raft_term_t raft_get_last_log_term(raft_server_t* me)
{
    raft_index_t current_idx = raft_get_current_idx(me);
    if (0 < current_idx)
    {
        raft_entry_t* ety = raft_get_entry_from_idx(me, current_idx);
        if (ety) {
            raft_term_t term = ety->term;
            raft_entry_release(ety);
            return term;
        } else if (raft_get_snapshot_last_idx(me) == current_idx) {
            return raft_get_snapshot_last_term(me);
        }
    }

    return 0;
}

int raft_is_connected(raft_server_t* me)
{
    return me->connected;
}

int raft_snapshot_is_in_progress(raft_server_t *me)
{
    return me->snapshot_in_progress;
}

int raft_is_apply_allowed(raft_server_t* me)
{
    return !raft_snapshot_is_in_progress(me) ||
           (me->snapshot_flags & RAFT_SNAPSHOT_NONBLOCKING_APPLY);
}

raft_entry_t *raft_get_last_applied_entry(raft_server_t *me)
{
    if (raft_get_last_applied_idx(me) == 0)
        return NULL;
    return me->log_impl->get(me->log, raft_get_last_applied_idx(me));
}

raft_index_t raft_get_snapshot_last_idx(raft_server_t *me)
{
    return me->snapshot_last_idx;
}

raft_term_t raft_get_snapshot_last_term(raft_server_t *me)
{
    return me->snapshot_last_term;
}

void raft_set_snapshot_metadata(raft_server_t *me, raft_term_t term, raft_index_t idx)
{
    me->last_applied_idx = idx;
    me->next_snapshot_last_term = me->snapshot_last_term;
    me->next_snapshot_last_idx = me->snapshot_last_idx;
    me->snapshot_last_term = term;
    me->snapshot_last_idx = idx;
}

int raft_is_single_node_voting_cluster(raft_server_t *me)
{
    return (1 == raft_get_num_voting_nodes(me) && raft_node_is_voting(me->node));
}

raft_msg_id_t raft_get_msg_id(raft_server_t* me)
{
    return me->msg_id;
}

/* return the targeted node_id if we are in the middle of attempting a leadership transfer
 * return RAFT_NODE_ID_NONE if no leadership transfer is in progress
 */
raft_node_id_t raft_get_transfer_leader(raft_server_t* me)
{
    return me->node_transferring_leader_to;
}

/* Forces the raft server to invoke an election on the next raft_periodic function call */
void raft_set_timeout_now(raft_server_t* me)
{
    me->timeout_now = 1;
}

void raft_destroy(raft_server_t* me)
{
    me->log_impl->free(me->log);
    raft_free(me);
}

void raft_clear(raft_server_t* me)
{
    me->current_term = 0;
    me->voted_for = -1;
    me->timeout_elapsed = 0;
    raft_randomize_election_timeout(me);
    me->voting_cfg_change_log_idx = -1;
    raft_set_state(me, RAFT_STATE_FOLLOWER);
    me->leader_id = RAFT_NODE_ID_NONE;
    me->commit_idx = 0;
    me->last_applied_idx = 0;
    me->num_nodes = 0;
    me->node = NULL;
    me->log_impl->reset(me->log, 1, 1);
}

raft_node_t* raft_add_node_internal(raft_server_t* me, raft_entry_t *ety, void* udata, raft_node_id_t id, int is_self)
{
    if (id == RAFT_NODE_ID_NONE)
        return NULL;

    /* set to voting if node already exists */
    raft_node_t* node = raft_get_node(me, id);
    if (node)
    {
        if (!raft_node_is_voting(node))
        {
            raft_node_set_voting(node, 1);
            return node;
        }
        else
            /* we shouldn't add a node twice */
            return NULL;
    }

    node = raft_node_new(udata, id);
    if (!node)
        return NULL;

    void* p = raft_realloc(me->nodes, sizeof(void*) * (me->num_nodes + 1));
    if (!p) {
        raft_node_free(node);
        return NULL;
    }
    me->num_nodes++;
    me->nodes = p;
    me->nodes[me->num_nodes - 1] = node;
    if (is_self)
        me->node = me->nodes[me->num_nodes - 1];

    node = me->nodes[me->num_nodes - 1];

    if (me->cb.notify_membership_event)
        me->cb.notify_membership_event(me, raft_get_udata(me), node, ety, RAFT_MEMBERSHIP_ADD);

    return node;
}

static raft_node_t* raft_add_non_voting_node_internal(raft_server_t* me, raft_entry_t *ety, void* udata, raft_node_id_t id, int is_self)
{
    if (raft_get_node(me, id))
        return NULL;

    raft_node_t* node = raft_add_node_internal(me, ety, udata, id, is_self);
    if (!node)
        return NULL;

    raft_node_set_voting(node, 0);
    return node;
}

raft_node_t* raft_add_node(raft_server_t* me, void* udata, raft_node_id_t id, int is_self)
{
    return raft_add_node_internal(me, NULL, udata, id, is_self);
}

raft_node_t* raft_add_non_voting_node(raft_server_t* me, void* udata, raft_node_id_t id, int is_self)
{
    return raft_add_non_voting_node_internal(me, NULL, udata, id, is_self);
}

void raft_remove_node(raft_server_t* me, raft_node_t* node)
{
    if (me->cb.notify_membership_event)
        me->cb.notify_membership_event(me, raft_get_udata(me), node, NULL, RAFT_MEMBERSHIP_REMOVE);

    assert(node);

    int i, found = 0;
    for (i = 0; i < me->num_nodes; i++)
    {
        if (me->nodes[i] == node)
        {
            found = 1;
            break;
        }
    }
    assert(found);
    memmove(&me->nodes[i], &me->nodes[i + 1], sizeof(*me->nodes) * (me->num_nodes - i - 1));
    me->num_nodes--;

    if (me->leader_id == raft_node_get_id(node)) {
        me->leader_id = RAFT_NODE_ID_NONE;
    }

    raft_node_free(node);
}

void raft_handle_append_cfg_change(raft_server_t* me, raft_entry_t* ety, raft_index_t idx)
{
    if (!raft_entry_is_cfg_change(ety))
        return;

    if (!me->cb.get_node_id)
        return;

    void* udata = raft_get_udata(me);
    raft_node_id_t node_id = me->cb.get_node_id(me, udata, ety, idx);
    raft_node_t* node = raft_get_node(me, node_id);
    int is_self = node_id == raft_get_nodeid(me);

    switch (ety->type)
    {
        case RAFT_LOGTYPE_ADD_NONVOTING_NODE:
            if (!is_self)
            {
                if (node && !raft_node_is_active(node))
                {
                    raft_node_set_active(node, 1);
                }
                else if (!node)
                {
                    node = raft_add_non_voting_node_internal(me, ety, NULL, node_id, is_self);
                    assert(node);
                }
             }
            break;

        case RAFT_LOGTYPE_ADD_NODE:
            node = raft_add_node_internal(me, ety, NULL, node_id, is_self);
            assert(node);
            assert(raft_node_is_voting(node));
            break;

        case RAFT_LOGTYPE_REMOVE_NODE:
            if (node) {
                raft_node_set_active(node, 0);
            }
            break;

        default:
            assert(0);
    }
}

void raft_handle_remove_cfg_change(raft_server_t* me, raft_entry_t* ety, const raft_index_t idx)
{
    if (!raft_entry_is_cfg_change(ety))
        return;

    if (!me->cb.get_node_id)
        return;

    void* udata = raft_get_udata(me);
    raft_node_id_t node_id = me->cb.get_node_id(me, udata, ety, idx);
    raft_node_t* node = raft_get_node(me, node_id);

    switch (ety->type)
    {
        case RAFT_LOGTYPE_REMOVE_NODE:
            raft_node_set_active(node, 1);
            break;

        case RAFT_LOGTYPE_ADD_NONVOTING_NODE:
            assert(node_id != raft_get_nodeid(me));
            raft_remove_node(me, node);
            break;

        case RAFT_LOGTYPE_ADD_NODE:
            raft_node_set_voting(node, 0);
            break;

        default:
            assert(0);
            break;
    }
}

int raft_delete_entry_from_idx(raft_server_t* me, raft_index_t idx)
{
    assert(raft_get_commit_idx(me) < idx);

    if (idx <= me->voting_cfg_change_log_idx)
        me->voting_cfg_change_log_idx = -1;

    int e = me->log_impl->pop(me->log, idx,
             (func_entry_notify_f) raft_handle_remove_cfg_change, me);
    if (e != 0)
        return e;

    return me->log_impl->sync(me->log);
}

int raft_election_start(raft_server_t* me)
{
    raft_log(me,
        "election starting: %d %d, term: %ld ci: %ld",
        me->election_timeout_rand, me->timeout_elapsed, me->current_term,
        raft_get_current_idx(me));

    me->leader_id = RAFT_NODE_ID_NONE;
    me->timeout_elapsed = 0;
    raft_randomize_election_timeout(me);

    return raft_become_precandidate(me);
}

void raft_accept_leader(raft_server_t* me, raft_node_id_t leader)
{
    if (!raft_is_follower(me)) {
        raft_become_follower(me);
    }

    if (me->leader_id != leader) {
        raft_clear_incoming_snapshot(me, 0);
    }

    me->timeout_elapsed = 0;
    me->leader_id = leader;
}

int raft_become_leader(raft_server_t* me)
{
    int i;

    raft_log(me, "becoming leader term:%ld", raft_get_current_term(me));

    raft_index_t next_idx = raft_get_current_idx(me) + 1;

    if (raft_get_current_term(me) > 1) {
        raft_entry_t *noop = raft_entry_new(0);
        noop->term = raft_get_current_term(me);
        noop->type = RAFT_LOGTYPE_NO_OP;

        int e = raft_append_entry(me, noop);
        raft_entry_release(noop);
        if (0 != e)
            return e;

        e = me->log_impl->sync(me->log);
        if (0 != e)
            return e;

        raft_node_set_match_idx(me->node, raft_get_current_idx(me));
        me->next_sync_index = raft_get_current_idx(me) + 1;

        // Commit noop immediately if this is a single node cluster
        if (raft_is_single_node_voting_cluster(me)) {
            raft_set_commit_idx(me, raft_get_current_idx(me));
        }
    }

    raft_set_state(me, RAFT_STATE_LEADER);
    raft_update_quorum_meta(me, me->msg_id);
    raft_clear_incoming_snapshot(me, 0);
    me->timeout_elapsed = 0;

    raft_reset_transfer_leader(me, 0);

    if (me->cb.notify_state_event)
        me->cb.notify_state_event(me, raft_get_udata(me), RAFT_STATE_LEADER);

    for (i = 0; i < me->num_nodes; i++)
    {
        raft_node_t* node = me->nodes[i];

        if (me->node == node)
            continue;

        raft_node_set_snapshot_offset(node, 0);
        raft_node_set_next_idx(node, next_idx);
        raft_node_set_match_idx(node, 0);
        raft_send_appendentries(me, node);
    }

    return 0;
}

int raft_become_precandidate(raft_server_t* me)
{
    raft_log(me,
             "becoming pre-candidate, next term : %ld", me->current_term + 1);

    if (me->cb.notify_state_event)
        me->cb.notify_state_event(me, me->udata, RAFT_STATE_PRECANDIDATE);

    for (int i = 0; i < me->num_nodes; i++)
        raft_node_vote_for_me(me->nodes[i], 0);

    raft_set_state(me, RAFT_STATE_PRECANDIDATE);

    for (int i = 0; i < me->num_nodes; i++)
    {
        raft_node_t* node = me->nodes[i];

        if (me->node != node && raft_node_is_voting(node)) {
            raft_send_requestvote(me, node);
        }
    }

    return 0;
}

int raft_become_candidate(raft_server_t* me)
{
    int i;

    raft_log(me, "becoming candidate");
    if (me->cb.notify_state_event)
        me->cb.notify_state_event(me, raft_get_udata(me), RAFT_STATE_CANDIDATE);

    int e = raft_set_current_term(me, raft_get_current_term(me) + 1);
    if (0 != e)
        return e;
    for (i = 0; i < me->num_nodes; i++)
        raft_node_vote_for_me(me->nodes[i], 0);

    if (raft_node_is_voting(me->node))
        raft_vote(me, me->node);

    me->leader_id = RAFT_NODE_ID_NONE;
    raft_set_state(me, RAFT_STATE_CANDIDATE);

    for (i = 0; i < me->num_nodes; i++)
    {
        raft_node_t* node = me->nodes[i];

        if (me->node != node && raft_node_is_voting(node))
        {
            raft_send_requestvote(me, node);
        }
    }
    return 0;
}

void raft_become_follower(raft_server_t* me)
{
    raft_log(me, "becoming follower");
    if (me->cb.notify_state_event)
        me->cb.notify_state_event(me, raft_get_udata(me), RAFT_STATE_FOLLOWER);

    raft_set_state(me, RAFT_STATE_FOLLOWER);
    raft_randomize_election_timeout(me);
    raft_clear_incoming_snapshot(me, 0);
    me->timeout_elapsed = 0;
    me->leader_id = RAFT_NODE_ID_NONE;
}

static int msgid_cmp(const void *a, const void *b)
{
    raft_msg_id_t va = *((raft_msg_id_t*) a);
    raft_msg_id_t vb = *((raft_msg_id_t*) b);

    return va > vb ? -1 : 1;
}

static raft_msg_id_t quorum_msg_id(raft_server_t* me)
{
    raft_msg_id_t msg_ids[me->num_nodes];
    int num_voters = 0;

    for (int i = 0; i < me->num_nodes; i++) {
        raft_node_t* node = me->nodes[i];

        if (!raft_node_is_voting(node))
            continue;

        if (me->node == node) {
            msg_ids[num_voters++] = me->msg_id;
        } else {
            msg_ids[num_voters++] = raft_node_get_last_acked_msgid(node);
        }
    }

    assert(num_voters == raft_get_num_voting_nodes(me));

    /**
     *  Sort the acknowledged msg_ids in the descending order and return
     *  the median value. Median value means it's the highest msg_id
     *  acknowledged by the majority.
     */
    qsort(msg_ids, num_voters, sizeof(raft_msg_id_t), msgid_cmp);

    return msg_ids[num_voters / 2];
}

int raft_periodic(raft_server_t* me, int msec_since_last_period)
{
    me->timeout_elapsed += msec_since_last_period;

    /* Only one voting node means it's safe for us to become the leader */
    if (raft_is_single_node_voting_cluster(me) && !raft_is_leader(me)) {
        // need to update term on new leadership
        int e = raft_set_current_term(me, raft_get_current_term(me) + 1);
        if (e != 0) {
            return e;
        }

        e = raft_become_leader(me);
        if (e != 0) {
            return e;
        }
    }

    /* needs to be outside state check, as can become a followr and still timeout */
    if (me->node_transferring_leader_to != RAFT_NODE_ID_NONE) {
        me->transfer_leader_time -= msec_since_last_period;
        if (me->transfer_leader_time < 0) {
            raft_reset_transfer_leader(me, 1);
        }
    }

    if (me->state == RAFT_STATE_LEADER)
    {
        if (me->request_timeout <= me->timeout_elapsed)
        {
            me->msg_id++;
            me->timeout_elapsed = 0;
            raft_send_appendentries_all(me);
        }

        me->quorum_timeout -= msec_since_last_period;
        if (me->quorum_timeout < 0)
        {
            /**
             * Check-quorum implementation
             *
             * Periodically (every quorum_timeout), we enter this check to
             * verify quorum exists. The current 'quorum msg id' should be
             * greater than the last time we were here. It means we've got
             * responses for append entry requests from the cluster, so we
             * conclude that cluster is operational and quorum exists. In that
             * case, we save the current quorum msg id and update the
             * quorum_timeout timer. Otherwise, it means quorum does not exist.
             * We should step down and become a follower.
             */
            raft_msg_id_t quorum_id = quorum_msg_id(me);

            if (me->last_acked_msg_id == quorum_id)
            {
                raft_log(me, "quorum does not exist, stepping down");
                raft_become_follower(me);
            }

            raft_update_quorum_meta(me, quorum_id);
	    }
    }
    else if ((me->election_timeout_rand <= me->timeout_elapsed || me->timeout_now) &&
        /* Don't become the leader when building snapshots or bad things will
         * happen when we get a client request */
        !raft_snapshot_is_in_progress(me))
    {
        int e = raft_election_start(me);
        if (0 != e)
            return e;
        me->timeout_now = 0;
    }

    if (me->last_applied_idx < raft_get_commit_idx(me) &&
            raft_is_apply_allowed(me))
    {
        int e = raft_apply_all(me);
        if (0 != e)
            return e;
    }

    raft_process_read_queue(me);

    return 0;
}

raft_entry_t* raft_get_entry_from_idx(raft_server_t* me, raft_index_t etyidx)
{
    return me->log_impl->get(me->log, etyidx);
}

int raft_voting_change_is_in_progress(raft_server_t* me)
{
    return me->voting_cfg_change_log_idx != -1;
}

int raft_recv_appendentries_response(raft_server_t* me,
                                     raft_node_t* node,
                                     msg_appendentries_response_t* r)
{
    raft_log_node(me, raft_node_get_id(node),
          "received appendentries response %s ci:%ld rci:%ld msgid:%lu",
          r->success == 1 ? "SUCCESS" : "fail",
          raft_get_current_idx(me),
          r->current_idx, r->msg_id);

    if (!node)
        return -1;

    if (!raft_is_leader(me))
        return RAFT_ERR_NOT_LEADER;

    if (raft_node_get_last_acked_msgid(node) > r->msg_id) {
        // this was received out of order and is now irrelevant.
        return 0;
    }

    /* If response contains term T > currentTerm: set currentTerm = T
       and convert to follower (§5.3) */
    if (me->current_term < r->term)
    {
        int e = raft_set_current_term(me, r->term);
        if (0 != e)
            return e;
        raft_become_follower(me);

        return 0;
    }
    else if (me->current_term != r->term)
        return 0;

    // if we got here, it means that the follower has acked us as a leader, even if it cant accept the append_entry
    raft_node_set_last_ack(node, r->msg_id, r->term);

    raft_index_t match_idx = raft_node_get_match_idx(node);

    if (0 == r->success)
    {
        /* Stale response -- ignore */
        if (r->current_idx < match_idx)
            return 0;

        raft_index_t next = min(r->current_idx + 1, raft_get_current_idx(me));
        assert(0 < next);

        raft_node_set_next_idx(node, next);

        /* retry */
        raft_send_appendentries(me, node);
        return 0;
    }

    if (me->cb.send_timeoutnow && raft_get_transfer_leader(me) == raft_node_get_id(node) && !me->sent_timeout_now
        && raft_get_current_idx(me) == r->current_idx) {
        me->cb.send_timeoutnow(me, node);
        me->sent_timeout_now = 1;
    }

    if (!raft_node_is_voting(node) &&
        !raft_voting_change_is_in_progress(me) &&
        raft_get_current_idx(me) <= r->current_idx + 1 &&
        !raft_node_is_voting_committed(node) &&
        raft_node_is_addition_committed(node) &&
        me->cb.node_has_sufficient_logs &&
        0 == raft_node_has_sufficient_logs(node)
        )
    {
        int e = me->cb.node_has_sufficient_logs(me, me->udata, node);
        if (0 == e)
            raft_node_set_has_sufficient_logs(node);
    }

    if (r->current_idx <= match_idx)
        return 0;

    assert(r->current_idx <= raft_get_current_idx(me));

    raft_node_set_next_idx(node, r->current_idx + 1);
    raft_node_set_match_idx(node, r->current_idx);

    if (me->auto_flush)
        return raft_flush(me, 0);

    return 0;
}

static int raft_receive_term(raft_server_t* me, raft_term_t term)
{
    int e;

    if (raft_is_candidate(me) && me->current_term == term)
    {
        raft_become_follower(me);
    }
    else if (me->current_term < term)
    {
        e = raft_set_current_term(me, term);
        if (0 != e)
            return e;

        raft_become_follower(me);
    }
    else if (term < me->current_term)
    {
        return RAFT_ERR_STALE_TERM;
    }

    return 0;
}

int raft_recv_appendentries(
    raft_server_t* me,
    raft_node_t* node,
    msg_appendentries_t* ae,
    msg_appendentries_response_t *r
    )
{
    int e = 0;

    if (0 < ae->n_entries)
    {
        raft_log_node(
            me, ae->leader_id,
            "recvd appendentries li:%d t:%ld ci:%ld lc:%ld pli:%ld plt:%ld #%d",
            ae->leader_id, ae->term, raft_get_current_idx(me),
            ae->leader_commit, ae->prev_log_idx, ae->prev_log_term,
            ae->n_entries);
    }

    r->msg_id = ae->msg_id;
    r->success = 0;

    e = raft_receive_term(me, ae->term);
    if (e != 0) {
        if (e == RAFT_ERR_STALE_TERM) {
            /* 1. Reply false if term < currentTerm (§5.1) */
            raft_log_node(me, ae->leader_id,
                          "AE term %ld is less than current term %ld", ae->term,
                          me->current_term);
            e = 0;
        }

        goto out;
    }

    if (node != NULL) {
        raft_node_update_max_seen_msg_id(node, ae->msg_id);
    }

    /* update current leader because ae->term is up to date */
    raft_accept_leader(me, ae->leader_id);
    raft_reset_transfer_leader(me, 0);

    /* Not the first appendentries we've received */
    /* NOTE: the log starts at 1 */
    if (0 < ae->prev_log_idx)
    {
        raft_entry_t* ety = raft_get_entry_from_idx(me, ae->prev_log_idx);

        /* Is a snapshot */
        if (ae->prev_log_idx == me->snapshot_last_idx)
        {
            if (me->snapshot_last_term != ae->prev_log_term)
            {
                /* Should never happen; something is seriously wrong! */
                raft_log_node(me, ae->leader_id,
                            "Snapshot AE prev conflicts with committed entry");
                e = RAFT_ERR_SHUTDOWN;
                if (ety)
                    raft_entry_release(ety);
                goto out;
            }
        }
        /* 2. Reply false if log doesn't contain an entry at prevLogIndex
           whose term matches prevLogTerm (§5.3) */
        else if (!ety)
        {
            raft_log_node(me, ae->leader_id,
                      "AE no log at prev_idx %ld", ae->prev_log_idx);
            goto out;
        }
        else if (ety->term != ae->prev_log_term)
        {
            raft_log_node(me, ae->leader_id, "AE term doesn't match prev_term (ie. %ld vs %ld) ci:%ld comi:%ld lcomi:%ld pli:%ld",
                  ety->term, ae->prev_log_term, raft_get_current_idx(me),
                  raft_get_commit_idx(me), ae->leader_commit, ae->prev_log_idx);
            if (ae->prev_log_idx <= raft_get_commit_idx(me))
            {
                /* Should never happen; something is seriously wrong! */
                raft_log_node(me, ae->leader_id,
                            "AE prev conflicts with committed entry");
                e = RAFT_ERR_SHUTDOWN;
                raft_entry_release(ety);
                goto out;
            }
            /* Delete all the following log entries because they don't match */
            e = raft_delete_entry_from_idx(me, ae->prev_log_idx);
            raft_entry_release(ety);
            goto out;
        }
        if (ety)
            raft_entry_release(ety);
    }

    r->success = 1;
    r->current_idx = ae->prev_log_idx;
    /* synchronize msg_id to leader.  not really needed for raft, but needed for virtraft for msg_id to be increasing
     * cluster wide so that can verify read_queue correctness easily.  Otherwise, it be fine for msg_id to be unique to
     * each raft_server_t.
     */
    if (ae->msg_id > me->msg_id) {
        me->msg_id = ae->msg_id;
    }

    /* 3. If an existing entry conflicts with a new one (same index
       but different terms), delete the existing entry and all that
       follow it (§5.3) */
    int i;
    for (i = 0; i < ae->n_entries; i++)
    {
        raft_entry_t* ety = ae->entries[i];
        raft_index_t ety_index = ae->prev_log_idx + 1 + i;

        raft_entry_t* existing_ety = raft_get_entry_from_idx(me, ety_index);
        raft_term_t existing_term = existing_ety ? existing_ety->term : 0;
        if (existing_ety)
            raft_entry_release(existing_ety);

        if (existing_ety && existing_term != ety->term)
        {
            if (ety_index <= raft_get_commit_idx(me))
            {
                /* Should never happen; something is seriously wrong! */
                raft_log_node(me, ae->leader_id, "AE entry conflicts with committed entry ci:%ld comi:%ld lcomi:%ld pli:%ld",
                      raft_get_current_idx(me), raft_get_commit_idx(me),
                      ae->leader_commit, ae->prev_log_idx);
                e = RAFT_ERR_SHUTDOWN;
                goto out;
            }
            e = raft_delete_entry_from_idx(me, ety_index);
            if (0 != e)
                goto out;
            break;
        }
        else if (!existing_ety)
            break;
        r->current_idx = ety_index;
    }

    /* Pick up remainder in case of mismatch or missing entry */
    for (; i < ae->n_entries; i++)
    {
        e = raft_append_entry(me, ae->entries[i]);
        if (0 != e)
            goto out;
        r->current_idx = ae->prev_log_idx + 1 + i;
    }

    if (ae->n_entries > 0) {
        e = me->log_impl->sync(me->log);
        if (0 != e)
            goto out;
    }

    /* 4. If leaderCommit > commitIndex, set commitIndex =
        min(leaderCommit, index of most recent entry) */
    if (raft_get_commit_idx(me) < ae->leader_commit)
    {
        raft_index_t last_log_idx = max(raft_get_current_idx(me), 1);
        raft_set_commit_idx(me, min(last_log_idx, ae->leader_commit));
    }

out:
    r->term = me->current_term;
    if (0 == r->success)
        r->current_idx = raft_get_current_idx(me);
    return e;
}

int raft_recv_requestvote(raft_server_t* me,
                          raft_node_t* node,
                          msg_requestvote_t* vr,
                          msg_requestvote_response_t *r)
{
    int e = 0;

    r->prevote = vr->prevote;
    r->request_term = vr->term;
    r->vote_granted = 0;

    /* Reject request if we have a leader, during prevote */
    if (vr->prevote &&
        !vr->transfer_leader &&
        me->leader_id != RAFT_NODE_ID_NONE &&
        me->leader_id != vr->candidate_id &&
        me->timeout_elapsed < me->election_timeout) {
        goto done;
    }

    /* Update the term only if this is not a prevote request */
    if (!vr->prevote && raft_get_current_term(me) < vr->term)
    {
        e = raft_set_current_term(me, vr->term);
        if (0 != e) {
            goto done;
        }
        raft_become_follower(me);
    }

    if (me->current_term > vr->term) {
        goto done;
    }

    if (me->current_term == vr->term &&
        (me->voted_for != -1 && me->voted_for != vr->candidate_id)) {
        goto done;
    }

    /* Below we check if log is more up-to-date... */
    raft_index_t current_idx = raft_get_current_idx(me);
    raft_term_t ety_term = raft_get_last_log_term(me);

    if (vr->last_log_term < ety_term ||
        (vr->last_log_term == ety_term && vr->last_log_idx < current_idx)) {
        goto done;
    }

    r->vote_granted = 1;

    if (!vr->prevote)
    {
        /* It shouldn't be possible for a leader or candidate to grant a vote
         * Both states would have voted for themselves */
        assert(!(raft_is_leader(me) || raft_is_candidate(me)));

        e = raft_vote_for_nodeid(me, vr->candidate_id);
        if (0 != e)
            r->vote_granted = 0;

        /* must be in an election. */
        me->leader_id = RAFT_NODE_ID_NONE;
        me->timeout_elapsed = 0;
    }

done:
    raft_log_node(me, vr->candidate_id, "node requested vote: %d, t:%ld, pv:%d replying: %s",
          vr->candidate_id,
          vr->term,
          vr->prevote,
          r->vote_granted == 1 ? "granted" :
          r->vote_granted == 0 ? "not granted" : "unknown");

    r->term = raft_get_current_term(me);
    return e;
}

int raft_votes_is_majority(const int num_nodes, const int nvotes)
{
    if (num_nodes < nvotes)
        return 0;
    int half = num_nodes / 2;
    return half + 1 <= nvotes;
}

int raft_recv_requestvote_response(raft_server_t* me,
                                   raft_node_t* node,
                                   msg_requestvote_response_t* r)
{
    raft_log_node(me, raft_node_get_id(node),
             "node responded to requestvote status:%s pv:%d rt:%ld ct:%ld rt:%ld",
             r->vote_granted == 1 ? "granted" :
             r->vote_granted == 0 ? "not granted" : "unknown",
             r->prevote,
             r->request_term,
             me->current_term,
             r->term);

    if (r->term > me->current_term)
    {
        int e = raft_set_current_term(me, r->term);
        if (0 != e)
            return e;
        raft_become_follower(me);

        return 0;
    }

    if (r->prevote) {
        /* Validate prevote is not stale */
        if (!raft_is_precandidate(me) || r->request_term != me->current_term + 1)
            return 0;
    } else {
        /* Validate reqvote is not stale */
        if (!raft_is_candidate(me) || r->request_term != me->current_term)
            return 0;
    }

    if (r->vote_granted)
    {
        if (node)
            raft_node_vote_for_me(node, 1);

        int votes = raft_get_nvotes_for_me(me);
        int nodes = raft_get_num_voting_nodes(me);

        if (raft_votes_is_majority(nodes, votes)) {
            int e = raft_is_precandidate(me) ? raft_become_candidate(me) :
                                                raft_become_leader(me);
            if (0 != e)
                return e;
        }
    }

    return 0;
}

int raft_recv_entry(raft_server_t* me,
                    msg_entry_t* ety,
                    msg_entry_response_t *r)
{
    if (raft_entry_is_voting_cfg_change(ety))
    {
        /* Only one voting cfg change at a time */
        if (raft_voting_change_is_in_progress(me))
            return RAFT_ERR_ONE_VOTING_CHANGE_ONLY;

        /* Multi-threading: need to fail here because user might be
         * snapshotting membership settings. */
        if (!raft_is_apply_allowed(me))
            return RAFT_ERR_SNAPSHOT_IN_PROGRESS;
    }

    if (!raft_is_leader(me))
        return RAFT_ERR_NOT_LEADER;

    if (raft_get_transfer_leader(me) != RAFT_NODE_ID_NONE)
        return RAFT_ERR_LEADER_TRANSFER_IN_PROGRESS;

    raft_log(me, "received entry t:%ld id: %d idx: %ld",
          me->current_term, ety->id, raft_get_current_idx(me) + 1);

    ety->term = me->current_term;
    int e = raft_append_entry(me, ety);
    if (0 != e)
        return e;

    r->id = ety->id;
    r->idx = raft_get_current_idx(me);
    r->term = me->current_term;

    if (me->auto_flush) {
        e = me->log_impl->sync(me->log);
        if (0 != e)
            return e;

        return raft_flush(me, raft_get_current_idx(me));
    }

    return 0;
}

int raft_send_requestvote(raft_server_t* me, raft_node_t* node)
{
    msg_requestvote_t rv;
    int e = 0;

    assert(node);
    assert(node != me->node);

    raft_node_id_t id = raft_node_get_id(node);
    raft_log_node(me, id, "sending requestvote to: %d", id);

    if (raft_is_precandidate(me))
    {
        rv.prevote = 1;
        rv.term = me->current_term + 1;
    }
    else
    {
        rv.prevote = 0;
        rv.term = me->current_term;
    }

    rv.last_log_idx = raft_get_current_idx(me);
    rv.last_log_term = raft_get_last_log_term(me);
    rv.candidate_id = raft_get_nodeid(me);
    rv.transfer_leader = me->timeout_now;

    if (me->cb.send_requestvote)
        e = me->cb.send_requestvote(me, me->udata, node, &rv);

    return e;
}

int raft_append_entry(raft_server_t* me, raft_entry_t* ety)
{
    /* Don't allow inserting entries that are > our term.
     * term needs to be updated first
     */
    assert(me->current_term >= ety->term);

    int e = me->log_impl->append(me->log, ety);
    if (e < 0)
        return e;

    if (raft_entry_is_voting_cfg_change(ety))
        me->voting_cfg_change_log_idx = raft_get_current_idx(me);

    if (raft_entry_is_cfg_change(ety)) {
        raft_handle_append_cfg_change(me, ety, raft_get_current_idx(me));
    }

    return 0;
}

int raft_apply_entry(raft_server_t* me)
{
    if (!raft_is_apply_allowed(me))
        return -1;

    /* Don't apply after the commit_idx */
    if (me->last_applied_idx == raft_get_commit_idx(me))
        return -1;

    raft_index_t log_idx = me->last_applied_idx + 1;

    raft_entry_t* ety = raft_get_entry_from_idx(me, log_idx);
    if (!ety)
        return -1;

    raft_log(me, "applying log: %ld, id: %d size: %d",
          log_idx, ety->id, ety->data_len);

    me->last_applied_idx++;
    if (me->cb.applylog)
    {
        int e = me->cb.applylog(me, me->udata, ety, me->last_applied_idx);
        assert(e == 0 || e == RAFT_ERR_SHUTDOWN);
        if (RAFT_ERR_SHUTDOWN == e) {
            raft_entry_release(ety);
            return RAFT_ERR_SHUTDOWN;
        }
    }

    /* voting cfg change is now complete.
     * TODO: there seem to be a possible off-by-one bug hidden here, requiring
     * checking log_idx >= voting_cfg_change_log_idx rather than plain ==.
     */
    if (log_idx >= me->voting_cfg_change_log_idx)
        me->voting_cfg_change_log_idx = -1;

    if (!raft_entry_is_cfg_change(ety))
        goto exit;

    raft_node_id_t node_id = me->cb.get_node_id(me, raft_get_udata(me), ety, log_idx);
    raft_node_t* node = raft_get_node(me, node_id);

    switch (ety->type) {
        case RAFT_LOGTYPE_ADD_NODE:
            raft_node_set_addition_committed(node, 1);
            raft_node_set_voting_committed(node, 1);
            /* Membership Change: confirm connection with cluster */
            raft_node_set_has_sufficient_logs(node);
            if (node_id == raft_get_nodeid(me))
                me->connected = RAFT_NODE_STATUS_CONNECTED;
            break;
        case RAFT_LOGTYPE_ADD_NONVOTING_NODE:
            raft_node_set_addition_committed(node, 1);
            break;
        case RAFT_LOGTYPE_REMOVE_NODE:
            if (node) {
                raft_remove_node(me, node);
            }
            break;
        default:
            break;
    }

exit:

    raft_entry_release(ety);
    return 0;
}

raft_entry_t** raft_get_entries_from_idx(raft_server_t* me, raft_index_t idx, int* n_etys)
{
    if (raft_get_current_idx(me) < idx) {
        *n_etys = 0;
        return NULL;
    }

    raft_index_t size = raft_get_current_idx(me) - idx + 1;
    raft_entry_t **e = raft_malloc(size * sizeof(raft_entry_t*));
    int n = me->log_impl->get_batch(me->log, idx, (int) size, e);

    if (n < 1) {
        raft_free(e);
        *n_etys = 0;
        return NULL;
    }

    *n_etys = n;
    return e;
}

int raft_send_snapshot(raft_server_t* me, raft_node_t* node)
{
    if (!me->cb.send_snapshot)
        return 0;

    while (1) {
        raft_size_t offset = raft_node_get_snapshot_offset(node);

        msg_snapshot_t msg = {
            .leader_id = raft_get_nodeid(me),
            .snapshot_index = me->snapshot_last_idx,
            .snapshot_term = me->snapshot_last_term,
            .term = me->current_term,
            .msg_id = ++me->msg_id,
            .chunk.offset = offset
        };

        raft_snapshot_chunk_t *chunk = &msg.chunk;

        int e = me->cb.get_snapshot_chunk(me, me->udata, node, offset, chunk);
        if (e != 0) {
            return (e != RAFT_ERR_DONE) ? e : 0;
        }

        e = me->cb.send_snapshot(me, me->udata, node, &msg);
        if (e != 0) {
            return e;
        }

        if (chunk->last_chunk) {
            raft_node_set_snapshot_offset(node, 0);
            raft_node_set_next_idx(node, me->snapshot_last_idx + 1);
            return 0;
        }

        raft_node_set_snapshot_offset(node, offset + chunk->len);
    }
}

int raft_recv_snapshot(raft_server_t* me,
                       raft_node_t* node,
                       msg_snapshot_t *req,
                       msg_snapshot_response_t *resp)
{
    int e = 0;

    raft_log_node(me, raft_node_get_id(node),
                  "recv snapshot: ci:%lu comi:%lu t:%lu li:%d mi:%lu si:%lu st:%lu o:%llu, lc:%d, len:%llu",
                  raft_get_current_idx(me),
                  raft_get_commit_idx(me),
                  req->term,
                  req->leader_id,
                  req->msg_id,
                  req->snapshot_index,
                  req->snapshot_term,
                  req->chunk.offset,
                  req->chunk.last_chunk,
                  req->chunk.len);

    resp->msg_id = req->msg_id;
    resp->last_chunk = req->chunk.last_chunk;
    resp->offset = 0;
    resp->success = 0;

    e = raft_receive_term(me, req->term);
    if (e != 0) {
        if (e == RAFT_ERR_STALE_TERM) {
            raft_log_node(me, req->leader_id,
                          "Snapshot req term %ld is less than current term %ld",
                          req->term, me->current_term);
            e = 0;
        }

        goto out;
    }

    if (node != NULL) {
        raft_node_update_max_seen_msg_id(node, req->msg_id);
    }

    raft_accept_leader(me, req->leader_id);
    raft_reset_transfer_leader(me, 0);

    /** If we already have this snapshot, inform the leader. */
    if (req->snapshot_index <= me->snapshot_last_idx) {
        /** Set response as if it is the last chunk to tell leader that we have
         * the snapshot */
        resp->last_chunk = 1;
        goto success;
    }

    /** In case leader takes another snapshot, it may start sending a more
     * recent snapshot. In that case, we dismiss existing snapshot file. */
    if (me->snapshot_recv_idx != req->snapshot_index) {
        e = raft_clear_incoming_snapshot(me, req->snapshot_index);
        if (e != 0) {
            goto out;
        }
    }

    /** Reject message if this is not our current offset. */
    if (me->snapshot_recv_offset != req->chunk.offset) {
        resp->offset = me->snapshot_recv_offset;
        goto out;
    }

    e = me->cb.store_snapshot_chunk(me, me->udata, req->snapshot_index,
                                    req->chunk.offset, &req->chunk);
    if (e != 0) {
        goto out;
    }

    me->snapshot_recv_offset = req->chunk.offset + req->chunk.len;

    if (req->chunk.last_chunk) {
        e = me->cb.load_snapshot(me, me->udata,
                                 req->snapshot_index, req->snapshot_term);
        if (e != 0) {
            goto out;
        }
    }

success:
    resp->offset = req->chunk.len + req->chunk.offset;
    resp->success = 1;
out:
    resp->term = me->current_term;

    return e;
}

int raft_recv_snapshot_response(raft_server_t* me,
                                raft_node_t* node,
                                msg_snapshot_response_t *r)
{
    raft_log_node(me, raft_node_get_id(node),
                  "recv snapshot response: ci:%lu comi:%lu mi:%lu t:%ld o:%llu s:%d lc:%d",
                  raft_get_current_idx(me),
                  raft_get_commit_idx(me),
                  r->msg_id,
                  r->term,
                  r->offset,
                  r->success,
                  r->last_chunk);

    if (!raft_is_leader(me))
        return RAFT_ERR_NOT_LEADER;

    if (raft_node_get_last_acked_msgid(node) > r->msg_id) {
        return 0;
    }

    if (me->current_term < r->term)
    {
        int e = raft_set_current_term(me, r->term);
        if (0 != e)
            return e;

        raft_become_follower(me);
        return 0;
    }
    else if (me->current_term != r->term)
    {
        return 0;
    }

    raft_node_set_last_ack(node, r->msg_id, r->term);

    if (!r->success) {
        raft_node_set_snapshot_offset(node, r->offset);
    }

    if (r->success && r->last_chunk) {
        raft_node_set_snapshot_offset(node, 0);
        raft_node_set_next_idx(node, max(me->snapshot_last_idx + 1,
                                         raft_node_get_next_idx(node)));
    }

    if (me->auto_flush)
        return raft_flush(me, 0);

    return 0;
}

int raft_send_appendentries(raft_server_t* me, raft_node_t* node)
{
    assert(node);
    assert(node != me->node);

    if (!raft_node_is_active(node)) {
        return 0;
    }

    raft_index_t next_idx = raft_node_get_next_idx(node);

    /* figure out if the client needs a snapshot sent */
    if (me->snapshot_last_idx > 0 && next_idx <= me->snapshot_last_idx)
    {
        return raft_send_snapshot(me, node);
    }

    if (!me->cb.send_appendentries)
        return -1;

    if (me->cb.backpressure) {
        if (me->cb.backpressure(me, me->udata, node) != 0) {
            return 0;
        }
    }

    msg_appendentries_t ae = {
        .term = me->current_term,
        .leader_id = raft_get_nodeid(me),
        .leader_commit = raft_get_commit_idx(me),
        .msg_id = ++me->msg_id,
    };

    ae.entries = raft_get_entries_from_idx(me, next_idx, &ae.n_entries);
    assert((!ae.entries && 0 == ae.n_entries) ||
            (ae.entries && 0 < ae.n_entries));

    /* previous log is the log just before the new logs */
    if (next_idx > 1)
    {
        raft_entry_t* prev_ety = raft_get_entry_from_idx(me, next_idx - 1);
        if (!prev_ety)
        {
            ae.prev_log_idx = me->snapshot_last_idx;
            ae.prev_log_term = me->snapshot_last_term;
        }
        else
        {
            ae.prev_log_idx = next_idx - 1;
            ae.prev_log_term = prev_ety->term;
            raft_entry_release(prev_ety);
        }
    }

    raft_log_node(me,
              raft_node_get_id(node),
              "sending appendentries: ci:%lu comi:%lu t:%lu lc:%lu pli:%lu plt:%lu msgid:%lu #%d",
              raft_get_current_idx(me),
              raft_get_commit_idx(me),
              ae.term,
              ae.leader_commit,
              ae.prev_log_idx,
              ae.prev_log_term,
              ae.msg_id,
              ae.n_entries);

    int res = me->cb.send_appendentries(me, me->udata, node, &ae);
    if (!res) {
        raft_node_set_next_idx(node, next_idx + ae.n_entries);
    }
    raft_entry_release_list(ae.entries, ae.n_entries);
    raft_free(ae.entries);

    return res;
}

int raft_send_appendentries_all(raft_server_t* me)
{
    int i, e;
    int ret = 0;

    for (i = 0; i < me->num_nodes; i++)
    {
        if (me->node == me->nodes[i])
            continue;

        e = raft_send_appendentries(me, me->nodes[i]);
        if (0 != e)
            ret = e;
    }

    return ret;
}

int raft_get_nvotes_for_me(raft_server_t* me)
{
    int i, votes;

    for (i = 0, votes = 0; i < me->num_nodes; i++)
    {
        if (me->node != me->nodes[i] &&
            raft_node_is_voting(me->nodes[i]) &&
            raft_node_has_vote_for_me(me->nodes[i]))
        {
            votes += 1;
        }
    }

    if (raft_node_is_voting(me->node))
        votes += 1;

    return votes;
}

int raft_vote(raft_server_t* me, raft_node_t* node)
{
    return raft_vote_for_nodeid(me, node ? raft_node_get_id(node) : -1);
}

int raft_vote_for_nodeid(raft_server_t* me, const raft_node_id_t nodeid)
{
    if (me->cb.persist_vote) {
        int e = me->cb.persist_vote(me, me->udata, nodeid);
        if (0 != e)
            return e;
    }
    me->voted_for = nodeid;
    return 0;
}

int raft_msg_entry_response_committed(raft_server_t* me,
                                      const msg_entry_response_t* r)
{
    raft_entry_t* ety = raft_get_entry_from_idx(me, r->idx);
    if (!ety)
        return 0;
    raft_term_t ety_term = ety->term;
    raft_entry_release(ety);

    /* entry from another leader has invalidated this entry message */
    if (r->term != ety_term)
        return -1;
    return r->idx <= raft_get_commit_idx(me);
}

int raft_apply_all(raft_server_t* me)
{
    if (!raft_is_apply_allowed(me))
        return 0;

    while (raft_get_last_applied_idx(me) < raft_get_commit_idx(me))
    {
        int e = raft_apply_entry(me);
        if (0 != e)
            return e;
    }

    return 0;
}

int raft_entry_is_voting_cfg_change(raft_entry_t* ety)
{
    return RAFT_LOGTYPE_ADD_NODE == ety->type ||
           RAFT_LOGTYPE_REMOVE_NODE == ety->type;
}

int raft_entry_is_cfg_change(raft_entry_t* ety)
{
    return (
        RAFT_LOGTYPE_ADD_NODE == ety->type ||
        RAFT_LOGTYPE_ADD_NONVOTING_NODE == ety->type ||
        RAFT_LOGTYPE_REMOVE_NODE == ety->type);
}

int raft_poll_entry(raft_server_t* me)
{
    /* We should never drop uncommitted entries */
    assert(me->log_impl->first_idx(me->log) <= raft_get_commit_idx(me));

    int e = me->log_impl->poll(me->log, me->log_impl->first_idx(me->log));
    if (e != 0)
        return e;

    return me->log_impl->sync(me->log);
}

int raft_pop_entry(raft_server_t* me)
{
    raft_index_t cur_idx = me->log_impl->current_idx(me->log);

    int e = me->log_impl->pop(me->log, cur_idx,
               (func_entry_notify_f) raft_handle_remove_cfg_change, me);
    if (e != 0)
        return e;

    return me->log_impl->sync(me->log);
}

raft_index_t raft_get_first_entry_idx(raft_server_t* me)
{
    assert(0 < raft_get_current_idx(me));

    if (me->snapshot_last_idx == 0)
        return 1;

    return me->snapshot_last_idx;
}

raft_index_t raft_get_num_snapshottable_logs(raft_server_t *me)
{
    if (raft_get_log_count(me) <= 1)
        return 0;
    return raft_get_commit_idx(me) - me->log_impl->first_idx(me->log) + 1;
}

int raft_begin_snapshot(raft_server_t *me, int flags)
{
    if (raft_get_num_snapshottable_logs(me) == 0)
        return -1;

    raft_index_t snapshot_target = raft_get_commit_idx(me);
    if (!snapshot_target)
        return -1;

    raft_entry_t* ety = raft_get_entry_from_idx(me, snapshot_target);
    if (!ety)
        return -1;
    raft_term_t ety_term = ety->term;
    raft_entry_release(ety);

    /* we need to get all the way to the commit idx */
    int e = raft_apply_all(me);
    if (e != 0)
        return e;

    assert(raft_get_commit_idx(me) == raft_get_last_applied_idx(me));

    me->snapshot_in_progress = 1;
    me->next_snapshot_last_idx = snapshot_target;
    me->next_snapshot_last_term = ety_term;
    me->snapshot_flags = flags;

    raft_log(me,
        "begin snapshot sli:%ld slt:%ld slogs:%ld",
        me->snapshot_last_idx,
        me->snapshot_last_term,
        raft_get_num_snapshottable_logs(me));

    return 0;
}

int raft_cancel_snapshot(raft_server_t *me)
{
    me->snapshot_in_progress = 0;
    return 0;
}

int raft_end_snapshot(raft_server_t *me)
{
    if (!me->snapshot_in_progress)
        return -1;

    me->snapshot_last_idx = me->next_snapshot_last_idx;
    me->snapshot_last_term = me->next_snapshot_last_term;

    /* If needed, remove compacted logs */
    int e = me->log_impl->poll(me->log, me->snapshot_last_idx + 1);
    if (e != 0)
        return e;

    e = me->log_impl->sync(me->log);
    if (e != 0)
        return e;

    me->snapshot_in_progress = 0;

    raft_log(me,
        "end snapshot base:%ld commit-index:%ld current-index:%ld",
        me->log_impl->first_idx(me->log) - 1,
        raft_get_commit_idx(me),
        raft_get_current_idx(me));

    if (!raft_is_leader(me))
        return 0;

    for (int i = 0; i < me->num_nodes; i++)
    {
        raft_node_t* node = me->nodes[i];

        if (me->node == node || !raft_node_is_active(node))
            continue;

        raft_node_set_snapshot_offset(node, 0);
        raft_index_t next_idx = raft_node_get_next_idx(node);

        /* figure out if the client needs a snapshot sent */
        if (me->snapshot_last_idx > 0 && next_idx <= me->snapshot_last_idx)
        {
            raft_send_snapshot(me, node);
        }
    }

    return 0;
}

int raft_begin_load_snapshot(
    raft_server_t *me,
    raft_term_t last_included_term,
    raft_index_t last_included_index)
{
    if (last_included_index == -1)
        return -1;

    if (last_included_index == 0 || last_included_term == 0)
        return -1;

    /* loading the snapshot will break cluster safety */
    if (last_included_index < me->last_applied_idx)
        return -1;

    /* snapshot was unnecessary */
    if (last_included_index < raft_get_current_idx(me))
        return -1;

    if (last_included_index <= me->snapshot_last_idx)
        return RAFT_ERR_SNAPSHOT_ALREADY_LOADED;

    if (me->current_term < last_included_term) {
        raft_set_current_term(me, last_included_term);
        me->current_term = last_included_term;
    }

    raft_set_state(me, RAFT_STATE_FOLLOWER);
    me->leader_id = RAFT_NODE_ID_NONE;

    me->log_impl->reset(me->log, last_included_index + 1, last_included_term);

    if (raft_get_commit_idx(me) < last_included_index)
        raft_set_commit_idx(me, last_included_index);

    me->last_applied_idx = last_included_index;
    me->next_snapshot_last_term = last_included_term;
    me->next_snapshot_last_idx = last_included_index;

    /* remove all nodes but self */
    int i, my_node_by_idx = 0;
    for (i = 0; i < me->num_nodes; i++)
    {
        if (raft_get_nodeid(me) == raft_node_get_id(me->nodes[i]))
            my_node_by_idx = i;
        else {
            raft_node_free(me->nodes[i]);
            me->nodes[i] = NULL;
        }
    }

    /* this will be realloc'd by a raft_add_node */
    me->nodes[0] = me->nodes[my_node_by_idx];
    me->num_nodes = 1;

    raft_log(me,
        "loaded snapshot sli:%ld slt:%ld slogs:%ld",
        me->snapshot_last_idx,
        me->snapshot_last_term,
        raft_get_num_snapshottable_logs(me));

    return 0;
}

int raft_end_load_snapshot(raft_server_t *me)
{
    int i;

    me->snapshot_last_idx = me->next_snapshot_last_idx;
    me->snapshot_last_term = me->next_snapshot_last_term;

    /* Set nodes' voting status as committed */
    for (i = 0; i < me->num_nodes; i++)
    {
        raft_node_t* node = me->nodes[i];
        raft_node_set_voting_committed(node, raft_node_is_voting(node));
        raft_node_set_addition_committed(node, 1);
        if (raft_node_is_voting(node))
            raft_node_set_has_sufficient_logs(node);
    }

    return 0;
}

void *raft_get_log(raft_server_t *me)
{
    return me->log;
}

raft_entry_t *raft_entry_new(unsigned int data_len)
{
    raft_entry_t *ety = raft_calloc(1, sizeof(raft_entry_t) + data_len);
    ety->data_len = data_len;
    ety->refs = 1;

    return ety;
}

void raft_entry_hold(raft_entry_t *ety)
{
    assert(ety->refs < UINT16_MAX);
    ety->refs++;
}

void raft_entry_release(raft_entry_t *ety)
{
    assert(ety->refs > 0);
    ety->refs--;

    if (!ety->refs) {
        if (ety->free_func) {
            ety->free_func(ety);
        } else {
            raft_free(ety);
        }
    }
}

void raft_entry_release_list(raft_entry_t **ety_list, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        raft_entry_release(ety_list[i]);
    }
}

int raft_queue_read_request(raft_server_t* me, func_read_request_callback_f cb, void *cb_arg)
{
    raft_read_request_t *req = raft_malloc(sizeof(raft_read_request_t));

    req->read_idx = raft_get_current_idx(me);
    req->read_term = raft_get_current_term(me);
    req->msg_id = ++me->msg_id;
    req->cb = cb;
    req->cb_arg = cb_arg;
    req->next = NULL;

    if (!me->read_queue_head)
        me->read_queue_head = req;
    if (me->read_queue_tail)
        me->read_queue_tail->next = req;
    me->read_queue_tail = req;

    me->need_quorum_round = 1;

    if (me->auto_flush)
        return raft_flush(me, 0);

    return 0;
}

static void pop_read_queue(raft_server_t *me, int can_read)
{
    raft_read_request_t *p = me->read_queue_head;

    p->cb(p->cb_arg, can_read);

    /* remove entry and update head/tail */
    if (p->next) {
        me->read_queue_head = p->next;
        if (!me->read_queue_head->next)
            me->read_queue_tail = me->read_queue_head;
    } else {
        me->read_queue_head = NULL;
        me->read_queue_tail = NULL;
    }

    raft_free(p);
}

void raft_process_read_queue(raft_server_t* me)
{
    if (!me->read_queue_head)
        return;

    /* As a follower we drop all queued read requests */
    if (raft_is_follower(me)) {
        while (me->read_queue_head) {
            pop_read_queue(me, 0);
        }
        return;
    }

    /* As a leader we can process requests that fulfill these conditions:
     * 1) Heartbeat acknowledged by majority
     * 2) We're on the same term (note: is this needed or over cautious?)
     */
    if (!raft_is_leader(me))
        return;

    /* Quickly bail if nothing to do */
    if (!me->read_queue_head)
        return;

    if (raft_get_num_voting_nodes(me) > 1) {
        raft_entry_t *ety = raft_get_entry_from_idx(me, raft_get_commit_idx(me));
        if (!ety)
            return;

        raft_term_t ety_term = ety->term;
        raft_entry_release(ety);

        /* Don't read if we did not commit an entry this term yet!
         * A new term has a NO_OP committed so if everything is well
         * we can except that to happen.
         */
        if (ety_term < me->current_term)
            return;
    }

    raft_msg_id_t last_acked_msgid = quorum_msg_id(me);
    raft_index_t last_applied_idx = me->last_applied_idx;

    /* Special case: the log's first index is 1, so we need to account
     * for that in case we read before anything was ever committed.
     *
     * Note that this also implies a single node because adding nodes would
     * bump the log and commit index.
     */
    if (!me->commit_idx && !me->last_applied_idx && raft_get_current_idx(me) == 1)
        last_applied_idx = 1;

    while (me->read_queue_head &&
            me->read_queue_head->msg_id <= last_acked_msgid &&
            me->read_queue_head->read_idx <= last_applied_idx) {
        pop_read_queue(me, me->read_queue_head->read_term == me->current_term);
    }
}

/* invoke a leadership transfer
 * node_id = targeted node we are transfering to
 * timeout = how long this should be allowed to take in milliseconds, as calculated by calls to raft_periodic)
 * return an error if leadership transfer is already in progress of the targeted node_id is unknown
 */
int raft_transfer_leader(raft_server_t* me, raft_node_id_t node_id, long timeout)
{
    if (me->state != RAFT_STATE_LEADER) {
        return RAFT_ERR_NOT_LEADER;
    }

    if (me->node_transferring_leader_to != RAFT_NODE_ID_NONE) {
        return RAFT_ERR_LEADER_TRANSFER_IN_PROGRESS;
    }

    raft_node_t * target = raft_get_node(me, node_id);
    if (target == NULL) {
        return RAFT_ERR_INVALID_NODEID;
    }

    if (me->cb.send_timeoutnow &&
    raft_get_current_idx(me) == raft_node_get_match_idx(target)) {
        me->cb.send_timeoutnow(me, target);
        me->sent_timeout_now = 1;
    }

    me->node_transferring_leader_to = node_id;
    if (timeout == 0) {
        me->transfer_leader_time = me->election_timeout;
    } else {
        me->transfer_leader_time = timeout;
    }

    return 0;
}

/* Stop trying to transfer leader to a targeted node
 * internally used because either we have timed out our attempt or because we are no longer the leader
 * possible to be used by a client as well.
 */
void raft_reset_transfer_leader(raft_server_t* me, int timed_out)
{
    if (me->node_transferring_leader_to != RAFT_NODE_ID_NONE) {
        if (me->cb.notify_transfer_event) {
            raft_transfer_state_e state = RAFT_STATE_LEADERSHIP_TRANSFER_EXPECTED_LEADER;
            if (me->node_transferring_leader_to != me->leader_id) {
                state = RAFT_STATE_LEADERSHIP_TRANSFER_UNEXPECTED_LEADER;
            }
            if (timed_out) {
                state = RAFT_STATE_LEADERSHIP_TRANSFER_TIMEOUT;
            }
            me->cb.notify_transfer_event(me, raft_get_udata(me), state);
        }

        me->node_transferring_leader_to = RAFT_NODE_ID_NONE;
        me->transfer_leader_time = 0;
        me->sent_timeout_now = 0;
    }
}

static int index_cmp(const void *a, const void *b)
{
    raft_index_t va = *((raft_index_t*) a);
    raft_index_t vb = *((raft_index_t*) b);

    return va > vb ? -1 : 1;
}

static int raft_update_commit_idx(raft_server_t* me)
{
    raft_index_t indexes[me->num_nodes];
    int num_voters = 0;

    memset(indexes, 0, sizeof(indexes));

    for (int i = 0; i < me->num_nodes; i++) {
        if (!raft_node_is_voting(me->nodes[i]))
            continue;

        indexes[num_voters++] = raft_node_get_match_idx(me->nodes[i]);
    }

    qsort(indexes, num_voters, sizeof(raft_index_t), index_cmp);
    raft_index_t commit = indexes[num_voters / 2];
    if (commit > me->commit_idx) {
        /* Leader can only commit entries from the current term */
        raft_entry_t *ety = raft_get_entry_from_idx(me, commit);
        if (ety->term == me->current_term)
            raft_set_commit_idx(me, commit);

        raft_entry_release(ety);
    }

    return 0;
}

raft_index_t raft_get_index_to_sync(raft_server_t *me)
{
    raft_index_t idx = raft_get_current_idx(me);
    if (me->next_sync_index > idx)
        return 0;

    me->next_sync_index = idx + 1;
    return idx;
}

int raft_set_auto_flush(raft_server_t* me, int flush)
{
    me->auto_flush = flush ? 1 : 0;
    return 0;
}

int raft_flush(raft_server_t* me, raft_index_t sync_index)
{
    if (!raft_is_leader(me)) {
        return 0;
    }

    if (sync_index > raft_node_get_match_idx(me->node)) {
        raft_node_set_match_idx(me->node, sync_index);
    }

    int e = raft_update_commit_idx(me);
    if (e != 0) {
        return e;
    }

    for (int i = 0; i < me->num_nodes; i++)
    {
        if (me->node == me->nodes[i])
            continue;

        if (!me->need_quorum_round &&
            raft_node_get_next_idx(me->nodes[i]) > raft_get_current_idx(me))
            continue;

        raft_send_appendentries(me, me->nodes[i]);
    }

    me->need_quorum_round = 0;

    if (me->last_applied_idx < raft_get_commit_idx(me)) {
        e = raft_apply_all(me);
        if (e != 0) {
            return e;
        }
    }

    raft_process_read_queue(me);

    return 0;
}
