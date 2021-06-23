#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "CuTest.h"

#include "raft.h"
#include "raft_log.h"
#include "raft_private.h"
#include "mock_send_functions.h"

#include "helpers.h"

static int __raft_persist_term(raft_server_t* raft, void *udata, raft_term_t term, int vote)
{
    return 0;
}

static int __raft_send_appendentries(raft_server_t* raft, void* udata, raft_node_t* node, msg_appendentries_t* msg)
{
    return 0;
}

static raft_node_id_t log_get_node_id_test(raft_server_t *r, void *data, raft_entry_t *entry, raft_index_t entry_idx)
{
    return (raft_node_id_t) *(entry->data);
}

static int __raft_persist_vote(raft_server_t* raft, void *udata, int vote)
{
    return 0;
}

static int __raft_send_requestvote(raft_server_t* raft, void* udata, raft_node_t* node, msg_requestvote_t* msg)
{
    return 0;
}

/*
 * tests:
 * 1) appending a TRANSFER_LEADER log entry to a node sets the transfer_leader value correctly
 * 2) committing a TRANSFER_LEADER log entry to the target node, sets the timeout_now value.
 * 3) appending any entry after a TRANSFER_LEADER log entry resets the transfer_leader value
 */
void TestRaft_transfer_leader_tag_node(CuTest *tc)
{
    raft_cbs_t funcs = {
            .persist_term = __raft_persist_term,
            .send_appendentries = __raft_send_appendentries,
            .log_get_node_id = log_get_node_id_test,
    };

    void *r = raft_new();
    raft_set_callbacks(r, &funcs, NULL);

    raft_add_node(r, NULL, 1, 1);
    raft_add_node(r, NULL, 2, 0);

    raft_set_state(r, RAFT_STATE_FOLLOWER);
    raft_set_current_term(r, 1);
    raft_set_commit_idx(r, 0);

    msg_entry_t *ety = __MAKE_ENTRY(1, 1, NULL);
    ety->type = RAFT_LOGTYPE_TRANSFER_LEADER;

    raft_node_id_t target = 1;
    memcpy(ety->data, &target, sizeof(target));

    raft_append_entry(r, ety);
    CuAssertTrue(tc, 1 == raft_get_transfer_leader(r));

    raft_set_commit_idx(r, 1);
    raft_apply_all(r);
    CuAssertTrue(tc, 1 == raft_get_timeout_now(r));

    ety = __MAKE_ENTRY(1, 1, NULL);
    raft_append_entry(r, ety);
    CuAssertTrue(tc, 0 == raft_get_transfer_leader(r));
}

/*
 * tests:
 * 1) if transfer_leader is set, will not accept a request vote from any node not specified in it
 * 2) if transfer_leader is set, will accept a request from the node specified by it.
 */
void TestRaft_server_recv_requestvote_with_transfer_node(CuTest * tc)
{
    raft_cbs_t funcs = { 0 };

    void *r = raft_new();
    raft_set_callbacks(r, &funcs, NULL);

    raft_add_node(r, NULL, 1, 1);
    raft_add_node(r, NULL, 2, 0);
    raft_add_node(r, NULL, 3, 0);
    raft_set_current_term(r, 1);
    raft_set_election_timeout(r, 1000);

    raft_set_transfer_leader(r, 2);

    msg_appendentries_t ae = { 0 };
    msg_appendentries_response_t aer;
    ae.term = 1;

    raft_recv_appendentries(r, raft_get_node(r, 2), &ae, &aer);
    CuAssertTrue(tc, 1 == aer.success);

    msg_requestvote_t rv = {
            .term = 2,
            .candidate_id = 3,
            .last_log_idx = 0,
            .last_log_term = 1
    };
    msg_requestvote_response_t rvr;
    raft_recv_requestvote(r, raft_get_node(r, 3), &rv, &rvr);
    CuAssertTrue(tc, 1 != rvr.vote_granted);

    rv.candidate_id = 2;
    raft_recv_requestvote(r, raft_get_node(r, 2), &rv, &rvr);
    CuAssertTrue(tc, 1 == rvr.vote_granted);
}

void TestRaft_targeted_node_becomes_candidate_when_before_real_timeout_occurs(CuTest * tc)
{
    raft_cbs_t funcs = {
            .persist_term = __raft_persist_term,
            .persist_vote = __raft_persist_vote,
            .send_requestvote = __raft_send_requestvote,
    };

    void *r = raft_new();
    raft_set_callbacks(r, &funcs, NULL);

    raft_set_timeout_now(r);

    /*  1 second election timeout */
    raft_set_election_timeout(r, 1000);

    raft_add_node(r, NULL, 1, 1);
    raft_add_node(r, NULL, 2, 0);

    /*  max election timeout have passed */
    raft_periodic(r, 1);

    /* is a candidate now */
    CuAssertTrue(tc, 1 == raft_is_candidate(r));
}
