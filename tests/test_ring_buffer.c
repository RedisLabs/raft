#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "CuTest.h"

#include "raft.h"
#include "raft_private.h"
#include "raft_ring_buffer.h"

void TestRaft_ring_buffer(CuTest * tc)
{
    /* push_back */
    {
        raft_ring_buffer_t* rb = raft_ring_buffer_new(2);
        raft_ring_buffer_push_back(rb, 1);
        raft_ring_buffer_push_back(rb, 2);
        raft_ring_buffer_push_back(rb, 3);
        /* [1, 2, 3] */

        CuAssertTrue(tc, 3 == raft_ring_buffer_size(rb));
        CuAssertTrue(tc, 1 == raft_ring_buffer_front(rb));
        CuAssertTrue(tc, 3 == raft_ring_buffer_back(rb));

        CuAssertTrue(tc, 1 == raft_ring_buffer_pop_front(rb));
        CuAssertTrue(tc, 3 == raft_ring_buffer_pop_back(rb));
        CuAssertTrue(tc, 2 == raft_ring_buffer_pop_front(rb));
        CuAssertTrue(tc, 0 == raft_ring_buffer_size(rb));
        raft_ring_buffer_free(rb);
    }

    /* push_front */
    {
        raft_ring_buffer_t* rb = raft_ring_buffer_new(2);
        raft_ring_buffer_push_front(rb, 1);
        raft_ring_buffer_push_front(rb, 2);
        raft_ring_buffer_push_front(rb, 3);
        /* [3, 2, 1] */

        CuAssertTrue(tc, 3 == raft_ring_buffer_size(rb));
        CuAssertTrue(tc, 3 == raft_ring_buffer_front(rb));
        CuAssertTrue(tc, 1 == raft_ring_buffer_back(rb));

        CuAssertTrue(tc, 3 == raft_ring_buffer_pop_front(rb));
        CuAssertTrue(tc, 1 == raft_ring_buffer_pop_back(rb));
        CuAssertTrue(tc, 2 == raft_ring_buffer_pop_front(rb));
        CuAssertTrue(tc, 0 == raft_ring_buffer_size(rb));
        raft_ring_buffer_free(rb);
    }

    /* remove_from_front */
    {
        raft_ring_buffer_t* rb = raft_ring_buffer_new(2);
        raft_ring_buffer_push_back(rb, 1);
        raft_ring_buffer_push_back(rb, 2);
        raft_ring_buffer_push_back(rb, 3);
        CuAssertTrue(tc, 3 == raft_ring_buffer_size(rb));

        raft_ring_buffer_remove_from_front(rb, 2);
        CuAssertTrue(tc, 2 == raft_ring_buffer_size(rb));
        CuAssertTrue(tc, 1 == raft_ring_buffer_pop_front(rb));
        CuAssertTrue(tc, 3 == raft_ring_buffer_pop_front(rb));
        CuAssertTrue(tc, 0 == raft_ring_buffer_size(rb));
        raft_ring_buffer_free(rb);
    }

    /* remove_from_back */
    {
        raft_ring_buffer_t* rb = raft_ring_buffer_new(2);
        raft_ring_buffer_push_back(rb, 1);
        raft_ring_buffer_push_back(rb, 2);
        raft_ring_buffer_push_back(rb, 3);
        CuAssertTrue(tc, 3 == raft_ring_buffer_size(rb));

        raft_ring_buffer_remove_from_back(rb, 2);
        CuAssertTrue(tc, 2 == raft_ring_buffer_size(rb));
        CuAssertTrue(tc, 1 == raft_ring_buffer_pop_front(rb));
        CuAssertTrue(tc, 3 == raft_ring_buffer_pop_front(rb));
        CuAssertTrue(tc, 0 == raft_ring_buffer_size(rb));
        raft_ring_buffer_free(rb);
    }

    /* many elements */
    {
        raft_ring_buffer_t* rb = raft_ring_buffer_new(2);
        int i;
        for (i = 0; i < 100; ++i) {
            raft_ring_buffer_push_back(rb, i);
        }
        CuAssertTrue(tc, 100 == raft_ring_buffer_size(rb));

        for (i = 0; i < 50; ++i) {
            raft_ring_buffer_remove_from_front(rb, i);
        }

        CuAssertTrue(tc, 50 == raft_ring_buffer_size(rb));
        CuAssertTrue(tc, 50 == raft_ring_buffer_front(rb));
        CuAssertTrue(tc, 99 == raft_ring_buffer_back(rb));

        for (i = 100; i < 200; ++i) {
            raft_ring_buffer_push_back(rb, i);
        }

        CuAssertTrue(tc, 150 == raft_ring_buffer_size(rb));
        CuAssertTrue(tc, 50 == raft_ring_buffer_front(rb));
        CuAssertTrue(tc, 199 == raft_ring_buffer_back(rb));

        for (i = 50; i < 200; ++i) {
            raft_ring_buffer_remove_from_back(rb, i);
        }

        CuAssertTrue(tc, 0 == raft_ring_buffer_size(rb));
        raft_ring_buffer_free(rb);
    }
}

int main(void)
{
    CuString *output = CuStringNew();
    CuSuite* suite = CuSuiteNew();

    SUITE_ADD_TEST(suite, TestRaft_ring_buffer);

    CuSuiteRun(suite);
    CuSuiteDetails(suite, output);
    printf("%s\n", output->buffer);

    int rc = suite->failCount == 0 ? 0 : 1;

    CuStringFree(output);
    CuSuiteFree(suite);

    return rc;
}