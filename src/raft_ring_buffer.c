/**
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>

#include "raft.h"
#include "raft_private.h"
#include "raft_ring_buffer.h"
#include "raft_types.h"

raft_ring_buffer_t* raft_ring_buffer_new(int capacity)
{
    raft_ring_buffer_t* rb = raft_malloc(sizeof(raft_ring_buffer_t));
    if (!rb) {
        return NULL;
    }
    rb->capacity = capacity;
    rb->begin = 0;
    rb->end = 0;
    rb->num = 0;
    rb->data = raft_malloc(sizeof(*rb->data) * capacity);
    if (!rb->data) {
        raft_free(rb);
        return NULL;
    }
    return rb;
}

void raft_ring_buffer_free(raft_ring_buffer_t* rb)
{
    raft_free(rb->data);
    raft_free(rb);
}

static int raft_ring_buffer_increase_capacity(raft_ring_buffer_t* rb)
{
    int new_capacity = rb->capacity * 2;
    intptr_t* new_data = raft_malloc(sizeof(*new_data) * new_capacity);
    if (!new_data) {
        return -1;
    }
    int i;
    for (i = 0; i < rb->num; ++i) {
        new_data[i] = rb->data[(rb->begin + i) % rb->capacity];
    }
    raft_free(rb->data);
    rb->data = new_data;
    rb->capacity = new_capacity;
    rb->begin = 0;
    rb->end = rb->num;
    return 0;
}

int raft_ring_buffer_push_front(raft_ring_buffer_t* rb, intptr_t value)
{
    if (rb->num == rb->capacity) {
        if (raft_ring_buffer_increase_capacity(rb) != 0) {
            return -1;
        }
    }
    rb->begin = (rb->begin - 1 + rb->capacity) % rb->capacity;
    rb->data[rb->begin] = value;
    ++rb->num;
    return 0;
}

int raft_ring_buffer_push_back(raft_ring_buffer_t* rb, intptr_t value)
{
    if (rb->num == rb->capacity) {
        if (raft_ring_buffer_increase_capacity(rb) != 0) {
            return -1;
        }
    }
    rb->data[rb->end] = value;
    rb->end = (rb->end + 1) % rb->capacity;
    ++rb->num;
    return 0;
}

intptr_t raft_ring_buffer_pop_front(raft_ring_buffer_t* rb)
{
    assert(rb->num > 0);
    intptr_t value = rb->data[rb->begin];
    rb->begin = (rb->begin + 1) % rb->capacity;
    --rb->num;
    return value;
}

intptr_t raft_ring_buffer_pop_back(raft_ring_buffer_t* rb)
{
    assert(rb->num > 0);
    rb->end = (rb->end - 1 + rb->capacity) % rb->capacity;
    --rb->num;
    return rb->data[rb->end];
}

int raft_ring_buffer_size(raft_ring_buffer_t* rb)
{
    return rb->num;
}

int raft_ring_buffer_capacity(raft_ring_buffer_t* rb)
{
    return rb->capacity;
}

intptr_t raft_ring_buffer_front(raft_ring_buffer_t* rb)
{
    assert(rb->num > 0);
    return rb->data[rb->begin];
}

intptr_t raft_ring_buffer_back(raft_ring_buffer_t* rb)
{
    assert(rb->num > 0);
    return rb->data[(rb->begin + rb->num - 1) % rb->capacity];
}

void raft_ring_buffer_remove_from_front(raft_ring_buffer_t* rb, intptr_t value)
{
    int i;
    for (i = 0; i < rb->num; ++i) {
        if (rb->data[(rb->begin + i) % rb->capacity] == value) {
            break;
        }
    }
    if (i == rb->num) {
        return;
    }
    for (; i > 0; --i) {
        rb->data[(rb->begin + i) % rb->capacity] =
            rb->data[(rb->begin + i - 1) % rb->capacity];
    }
    rb->begin = (rb->begin + 1) % rb->capacity;
    --rb->num;
}

void raft_ring_buffer_remove_from_back(raft_ring_buffer_t* rb, intptr_t value)
{
    int i;
    for (i = 0; i < rb->num; ++i) {
        if (rb->data[(rb->begin + i) % rb->capacity] == value) {
            break;
        }
    }
    if (i == rb->num) {
        return;
    }
    for (; i < rb->num - 1; ++i) {
        rb->data[(rb->begin + i) % rb->capacity] =
            rb->data[(rb->begin + i + 1) % rb->capacity];
    }
    rb->end = (rb->end - 1 + rb->capacity) % rb->capacity;
    --rb->num;
}

void raft_ring_buffer_clear(raft_ring_buffer_t* rb)
{
    rb->begin = 0;
    rb->end = 0;
    rb->num = 0;
}
