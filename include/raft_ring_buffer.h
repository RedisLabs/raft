/**
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef RAFT_RING_BUFFER_H_
#define RAFT_RING_BUFFER_H_

#include <stdint.h>

/**
 * A ring buffer of intptr_t values.
 *
 * The ring buffer is implemented as a variable-size array of intptr_t values. The
 * array is indexed by two variables, begin and end, which indicate the first
 * and last element of the ring buffer, respectively. The ring buffer is empty
 * when begin == end. But it is also possible that begin == end even if the ring
 * buffer is full. In that case, the ring buffer is full if num == capacity, and
 * empty otherwise.
 */
struct raft_ring_buffer {
    int capacity;
    int begin;
    int end;
    int num;
    intptr_t* data;
};

/**
 * Ring buffer.
 */
typedef struct raft_ring_buffer raft_ring_buffer_t;

/**
 * Initialize a ring buffer.
 *
 * @param capacity The initial maximum number of elements that the ring buffer can hold.
 * @return The ring buffer, or NULL if memory could not be allocated.
 */
raft_ring_buffer_t* raft_ring_buffer_new(int capacity);

/**
 * Release all memory used by the ring buffer.
 *
 * @param[in] rb The ring buffer.
 */
void raft_ring_buffer_free(raft_ring_buffer_t* rb);

/**
 * Push a value to the front of the ring buffer.
 *
 * @param[in] rb The ring buffer.
 * @param[in] value The value to push.
 * @return 0 on success, or -1 if memory could not be allocated.
 */
int raft_ring_buffer_push_front(raft_ring_buffer_t* rb, intptr_t value);

/**
 * Push a value to the back of the ring buffer.
 *
 * @param[in] rb The ring buffer.
 * @param[in] value The value to push.
 * @return 0 on success, or -1 if memory could not be allocated.
 */
int raft_ring_buffer_push_back(raft_ring_buffer_t* rb, intptr_t value);

/**
 * Pop a value from the front of the ring buffer.
 *
 * The ring buffer must not be empty.
 * @param[in] rb The ring buffer.
 * @return The value.
 */
intptr_t raft_ring_buffer_pop_front(raft_ring_buffer_t* rb);

/**
 * Pop a value from the back of the ring buffer.
 *
 * The ring buffer must not be empty.
 * @param[in] rb The ring buffer.
 * @return The value.
 */
intptr_t raft_ring_buffer_pop_back(raft_ring_buffer_t* rb);

/**
 * Return the number of elements in the ring buffer.
 *
 * @param[in] rb The ring buffer.
 * @return The number of elements.
 */
int raft_ring_buffer_size(raft_ring_buffer_t* rb);

/**
 * Return the current maximum number of elements that the ring buffer can hold.
 *
 * Capacity will be increased automatically when the ring buffer is full.
 * @param[in] rb The ring buffer.
 * @return The capacity.
 */
int raft_ring_buffer_capacity(raft_ring_buffer_t* rb);

/**
 * Return the value at the front of the ring buffer.
 *
 * The ring buffer must not be empty.
 * @param[in] rb The ring buffer.
 * @return The value.
 */
intptr_t raft_ring_buffer_front(raft_ring_buffer_t* rb);

/**
 * Return the value at the back of the ring buffer.
 *
 * The ring buffer must not be empty.
 * @param[in] rb The ring buffer.
 * @return The value.
 */
intptr_t raft_ring_buffer_back(raft_ring_buffer_t* rb);

/**
 * Remove a value from the ring buffer by value.
 *
 * Search from front for the first occurrence of the given value and remove it. If the
 * value is not found, the ring buffer is left unchanged.
 * @param[in] rb The ring buffer.
 * @param[in] value The value to remove.
 */ 
void raft_ring_buffer_remove_from_front(raft_ring_buffer_t* rb, intptr_t value);

/**
 * Remove a value from the ring buffer by value.
 *
 * Search from back for the first occurrence of the given value and remove it. If the
 * value is not found, the ring buffer is left unchanged.
 * @param[in] rb The ring buffer.
 * @param[in] value The value to remove.
 */
void raft_ring_buffer_remove_from_back(raft_ring_buffer_t* rb, intptr_t value);

/**
 * Clear the ring buffer.
 *
 * @param[in] rb The ring buffer.
 */
void raft_ring_buffer_clear(raft_ring_buffer_t* rb);

#endif  /* RAFT_RING_BUFFER_H_ */
