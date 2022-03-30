#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include <stdbool.h>
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "CuTest.h"

#include "linked_list_queue.h"

#include "raft.h"
#include "mock_send_functions.h"

typedef struct
{
    void* outbox;
    void* inbox;
    void* raft;
} sender_t;

typedef struct
{
    void* data;
    int len;
    /* what type of message is it? */
    int type;
    /* who sent this? */
    raft_node_t* sender;
} msg_t;

static sender_t** __senders = NULL;
static int __nsenders = 0;

void senders_new()
{
    __senders = NULL;
    __nsenders = 0;
}

static int __append_msg(
    sender_t* me,
    void* data,
    int type,
    int len,
    raft_node_t* node,
    raft_server_t* raft
    )
{
    msg_t* m = malloc(sizeof(msg_t));
    m->type = type;
    m->len = len;
    m->data = malloc(len);
    m->sender = raft_get_node(raft, raft_get_nodeid(raft));
    memcpy(m->data, data, len);
    llqueue_offer(me->outbox, m);

    /* give to peer */
    sender_t* peer = raft_node_get_udata(node);
    if (peer)
    {
        msg_t* m2 = malloc(sizeof(msg_t));
        memcpy(m2, m, sizeof(msg_t));
        m2->sender = raft_get_node(peer->raft, raft_get_nodeid(raft));
        llqueue_offer(peer->inbox, m2);
    }

    return 0;
}

int sender_requestvote(raft_server_t* raft,
                       void* udata, raft_node_t* node,
                       raft_requestvote_req_t* msg)
{
    return __append_msg(udata, msg, RAFT_REQUESTVOTE_REQ, sizeof(*msg), node,
                        raft);
}

int sender_requestvote_response(raft_server_t* raft,
                                void* udata, raft_node_t* node,
                                raft_requestvote_resp_t* msg)
{
    return __append_msg(udata, msg, RAFT_REQUESTVOTE_RESP, sizeof(*msg),
                        node, raft);
}

int sender_appendentries(raft_server_t* raft,
                         void* udata, raft_node_t* node,
                         raft_appendentries_req_t* msg)
{
    raft_entry_req_t** entries = calloc(1, sizeof(raft_entry_req_t *) * msg->n_entries);
    int i;
    for (i = 0; i < msg->n_entries; i++) {
        entries[i] = msg->entries[i];
        raft_entry_hold(entries[i]);
    }

    raft_entry_req_t** old_entries = msg->entries;
    msg->entries = entries;
    int ret = __append_msg(udata, msg, RAFT_APPENDENTRIES_REQ, sizeof(*msg), node,
                        raft);
    msg->entries = old_entries;
    return ret;
}

int sender_appendentries_response(raft_server_t* raft,
                                  void* udata, raft_node_t* node,
                                  raft_appendentries_resp_t* msg)
{
    return __append_msg(udata, msg, RAFT_APPENDENTRIES_RESP,
                        sizeof(*msg), node, raft);
}

int sender_entries_response(raft_server_t* raft,
                            void* udata, raft_node_t* node,
                            raft_entry_resp_t* msg)
{
    return __append_msg(udata, msg, RAFT_ENTRY_RESP, sizeof(*msg), node, raft);
}

void* sender_new(void* address)
{
    sender_t* me = malloc(sizeof(sender_t));
    me->outbox = llqueue_new();
    me->inbox = llqueue_new();
    __senders = realloc(__senders, sizeof(sender_t*) * (++__nsenders));
    __senders[__nsenders - 1] = me;
    return me;
}

void* sender_poll_msg_data(void* s)
{
    sender_t* me = s;
    msg_t* msg = llqueue_poll(me->outbox);
    return NULL != msg ? msg->data : NULL;
}

void sender_set_raft(void* s, void* r)
{
    sender_t* me = s;
    me->raft = r;
}

int sender_msgs_available(void* s)
{
    sender_t* me = s;

    return 0 < llqueue_count(me->inbox);
}

void sender_poll_msgs(void* s)
{
    sender_t* me = s;
    msg_t* m;

    while ((m = llqueue_poll(me->inbox)))
    {
        switch (m->type)
        {
        case RAFT_APPENDENTRIES_REQ:
        {
            raft_appendentries_resp_t response;
            raft_recv_appendentries(me->raft, m->sender, m->data, &response);
            __append_msg(me, &response, RAFT_APPENDENTRIES_RESP,
                         sizeof(response), m->sender, me->raft);
        }
        break;
        case RAFT_APPENDENTRIES_RESP:
            raft_recv_appendentries_response(me->raft, m->sender, m->data);
            break;
        case RAFT_REQUESTVOTE_REQ:
        {
            raft_requestvote_resp_t response;
            raft_recv_requestvote(me->raft, m->sender, m->data, &response);
            __append_msg(me, &response, RAFT_REQUESTVOTE_RESP,
                         sizeof(response), m->sender, me->raft);
        }
        break;
        case RAFT_REQUESTVOTE_RESP:
            raft_recv_requestvote_response(me->raft, m->sender, m->data);
            break;
        case RAFT_ENTRY_REQ:
        {
            raft_entry_resp_t response;
            raft_recv_entry(me->raft, m->data, &response);
            __append_msg(me, &response, RAFT_ENTRY_RESP,
                         sizeof(response), m->sender, me->raft);
        }
        break;

        case RAFT_ENTRY_RESP:
#if 0
            raft_recv_entry_response(me->raft, m->sender, m->data);
#endif
            break;
        }
    }
}
