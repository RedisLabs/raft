[![Daily CI](https://github.com/RedisLabs/raft/actions/workflows/daily.yml/badge.svg)](https://github.com/RedisLabs/raft/actions/workflows/daily.yml)
[![codecov](https://codecov.io/gh/RedisLabs/raft/branch/master/graph/badge.svg?token=66M19HJ7K9)](https://codecov.io/gh/RedisLabs/raft)

A complete implementation of the [Raft Consensus Algorithm](https://raft.github.io) as a C library, licensed under BSD.

This is a fork of the original library created by Willem-Hendrik Thiart, which is now actively maintained by Redis Ltd. and used as part of [RedisRaft](https://github.com/redislabs/redisraft).

See [raft.h](https://github.com/redislabs/raft/blob/master/include/raft.h) for full API documentation.

Main Features
=============

The library implements all basic features of Raft as well as some extensions, including:

* Leader election
* Log replication and FSM interface
* Compaction and snapshot support
* Cluster membership changes
* Quorum check and step-down by leader
* Pre-vote
* Leadership transfer

The library is completely abstract and it is up to the caller to implement things like:
* RPC and networking
* Log storage and access
* Generation of snapshots from FSM state

Getting Started
===============

To build and run basic tests:

```
make tests
```

If you prefer, you can also use cmake:

```
mkdir build
cd build
cmake ..
make
make test
```

Tests
=====

The library uses the following testing methods:

* A simulator (virtraft2) is used to test the Raft invariants on unreliable networks
* All bugs have regression tests
* Many unit tests

virtraft2
---------

This cluster simulator checks the following:

* Log Matching (servers must have matching logs)
* State Machine Safety (applied entries have the same ID)
* Election Safety (only one valid leader per term)
* Current Index Validity (does the current index have an existing entry?)
* Entry ID Monotonicity (entries aren't appended out of order)
* Committed entry popping (committed entries are not popped from the log)
* Log Accuracy (does the server's log match mirror an independent log?)
* Deadlock detection (does the cluster continuously make progress?)

Chaos generated by virtraft2:

* Random bi-directional partitions between nodes
* Message dropping
* Message duplication
* Membership change injection
* Random compactions

Run the simulator using:

```
mkdir .env
virtualenv .env
.env/bin/activate
pip install -r tests/requirements.txt
make test_virtraft
```
