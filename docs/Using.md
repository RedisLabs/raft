Using the library
===============

Implementing callbacks
----------------

Libraft only provides core raft logic. Application should implement some set of callbacks to provide networking and persistence.


### Libraft callbacks
You provide your callbacks to the Raft server using `raft_set_callbacks()`.
Application must implement all the mandatory callbacks. You can find more detailed info in the `raft.h`.

```c
typedef struct
{
    raft_logentry_event_f applylog;
    raft_persist_metadata_f persist_metadata;
    raft_get_node_id_f get_node_id;
    raft_node_has_sufficient_logs_f node_has_sufficient_logs;
    raft_timestamp_f timestamp;
    
    /* Networking */
    raft_send_requestvote_f send_requestvote;
    raft_send_appendentries_f send_appendentries;
    raft_send_snapshot_f send_snapshot;
    raft_send_timeoutnow_f send_timeoutnow;
    
    /* Snapshot handling */
    raft_load_snapshot_f load_snapshot;
    raft_get_snapshot_chunk_f get_snapshot_chunk;
    raft_store_snapshot_chunk_f store_snapshot_chunk;
    raft_clear_snapshot_f clear_snapshot;
    
    /** 
     * Optional callbacks: 
     * 
     * Notification on events, debug logging, message flow control etc.
     */
    raft_membership_event_f notify_membership_event;
    raft_state_event_f notify_state_event;
    raft_transfer_event_f notify_transfer_event;
    raft_log_f log;
    raft_backpressure_f backpressure;
    raft_get_entries_to_send_f get_entries_to_send;
} raft_cbs_t;
```

### Log file callbacks

Application must provide a log implementation and pass that implementation as a parameter to `raft_new_with_log()` call.
Log implementation must implement all the callbacks. You can find more detailed info in the `raft.h`.

```c
typedef struct raft_log_impl
{
    void *(*init) (void *raft, void *arg);
    void (*free) (void *log);
    void (*reset) (void *log, raft_index_t first_idx, raft_term_t term);
    int (*append) (void *log, raft_entry_t *entry);
    int (*poll) (void *log, raft_index_t first_idx);
    int (*pop) (void *log, raft_index_t from_idx);
    raft_entry_t* (*get) (void *log, raft_index_t idx);
    raft_index_t (*get_batch) (void *log, raft_index_t idx,
                              raft_index_t entries_n, raft_entry_t **entries);
    raft_index_t (*first_idx) (void *log);
    raft_index_t (*current_idx) (void *log);
    raft_index_t (*count) (void *log);
    int (*sync) (void *log);
} raft_log_impl_t;
```


Raft Library Initialization
----------------

You should initialize raft library on all nodes:

```c
raft_server_t *r = raft_new_with_log(&log_impl, log_impl_arg);
raft_set_callbacks(r, &callback_impls, app_arg);

// Add own node as non-voting, e.g node ID here is 999999, application generates node IDs.
raft_add_non_voting_node(r, user_data_ptr, 999999, 1); 
```

and call `raft_periodic()` at periodic intervals:

```
//e.g call every 100 milliseconds
raft_periodic(r);
```


Cluster Initialization
------------------

These are steps that you need follow when you want to initialize a cluster. (Preparing to use it for the first time)
- You need to start a single node first. This node will be the first node in the cluster. Other nodes can join to this node later.

```c

// Raft Library initialization
raft_server_t *r = raft_new_with_log(&log_impl, arg);
raft_set_callbacks(r, &callback_impls, arg);
raft_add_non_voting_node(r, NULL, 9999999, 1); // Add own node as non-voting

// Bootstrap a cluster. 
// Become the leader and append the configuration entry for the own node.
raft_set_current_term(rr->raft, 1);
raft_become_leader(rr->raft);

struct config cfg = {
    .id = 9999999,
    .addr = ....,  // You should also put node address into the config entry
};

raft_entry_t *ety = raft_entry_new(sizeof(cfg));
ety->type = RAFT_LOGTYPE_ADD_NODE;
memcpy(ety->data, &cfg, sizeof(cfg));

raft_recv_entry(rr->raft, ety, NULL);
raft_entry_release(ety);

```

Submit requests
------------------

#### Submit entries

You can submit entries by calling `raft_recv_entry()`:

```c
void app_submit(raft_server_t *r, void *client_request, size_t client_request_len)
{
    raft_entry_t *ety = raft_entry_new(client_request_len);
    
    memcpy(ety->data, client_request, client_request_len);
    ety->type = RAFT_LOGTYPE_NORMAL;
    
    raft_recv_entry(r, ety, NULL);
}
```

When libraft commits the entry, it will call `applylog` callback for the entry:

```c
int app_appylog_callback_impl(raft_server_t *r,
                              void *user_data,
                              raft_entry_t *entry,
                              raft_index_t entry_index)
{
    void *client_request = entry->data;
    size_t client_request_len = entry->data_len;
    
    app_execute_request(client_request, client_request_len);
}
```

#### Submit readonly requests

If your operation is readonly, you can execute it without writing to the log or replicating it to other nodes. Still, libraft needs to communicate with other nodes to verify it is still the leader. So, for readonly requests, there is another API to submit it. When it is safe to execute the request, libraft will call the callback you provided:

```c
void app_submit_readonly(raft_server_t *r, void *client_request) 
{
   raft_recv_read_request(r, app_readonly_op_callback, request); 
}

void app_readonly_op_callback(void *arg, int can_read)
{
    void *client_request = arg;
    
    if (can_read == 0) {
        // Cluster is down or this node is not the leader anymore. 
        // We cannot execute the command.
        return;
    }
    
    app_execute_readonly(request);
}

```


> :bulb: In the code snippets, error handling is skipped. You should check the return code of the library functions all the time. For example, you can only submit new requests on the leader node. The above function calls will fail if the node is not the leader.



Log compaction
------------------

Over time, log file will grow. Libraft does not initiate log compaction itself. Application should decide when to take a snapshot. (e.g if log file grows over a limit)

These are the steps when you want to save a snapshot:

- Call `raft_begin_snapshot()`. 
- Save `last_applied_term` and `last_applied_index` and node configuration in to the snapshot.
- Save snapshot to the disk.
- Call `raft_end_snapshot()`

e.g
```c
void app_take_snapshot(raft_server_t *r)
{
    raft_begin_snapshot(r);

    app_serialize_into_snapshot(raft_get_last_applied_term());
    app_serialize_into_snapshot(raft_get_last_applied_index());

    for (int i = 0; i < raft_get_num_nodes(r); i++) {
        raft_node_t *n = raft_get_node_from_idx(r, i);
        /* Skip uncommitted nodes from the snapshot */
        if (!raft_node_is_addition_committed(n)) {
            continue;
        }
        raft_node_id_t id = raft_get_node_id(n);
        int voting = raft_is_voting_committed(n);
        
        // You may also want to store address info for the node but skipping that
        // part in this example as it is not part of the libraft
        serialize_node_into_snapshot(id, voting);
    }
    
    app_save_snapshot_to_disk();

    raft_end_snapshot(r);
}

```

Restore state after a restart
------------------

Raft stores all its data in three files: Snapshot, log and metadata file (see `raft_persist_metadata_f` in `raft.h` for details).

After a restart, we need to restore the state. Correct order would be:

- Initialize library (described above)
- Restore snapshot (skip this step if you don't have a snapshot)
- Restore log entries
- Restore metadata


### Restoring from snapshot

Application saves `last_applied_term` and `last_applied_index` along with node configuration into the snapshots.
On a restart, after loading the state into your application, you need to configure libraft from the configuration info in the snapshot and finally call `raft_restore_snapshot()`.

e.g: 

```c

// As an example, assuming you read a list of nodes from the snapshot
struct config {
    raft_node_id_t id;
    int voting;
    struct config *next;
};
....

void app_configure_from_snapshot(raft_server_t *r, 
                                 struct config *head, 
                                 raft_term_t last_applied_term
                                 raft_index_t last_applied_index)
{
    struct config *cfg = head;
    
    while (cfg != NULL) {
        if (cfg->id == raft_get_nodeid(r)) {
            // This is our own node configuration
            raft_node_t *n = raft_get_node(r, cfg->id);
            raft_node_set_voting(n, cfg->voting);
        } else {
            if (cfg->voting) {
                raft_add_node(r, n, cfg->id, 0);
            } else {
                raft_add_non_voting_node(r, n, cfg->id, 0);
            }
        }
        cfg = cfg->next;
    }
    
    raft_restore_snapshot(r, last_applied_term, last_applied_index);
}

```

### Restoring log entries
After restoring log file (restoring in your log implementation), you need to call one function:

```c
raft_restore_log(r);
```


### Restoring metadata
As the final step, read term and voted_for info from the metadata file and then call one libraft function:
```
raft_restore_metadata(r, term, vote);
```

### Example

If we put all steps together:

```c
void app_restore_libraft()
{
    // Raft Library initialization
    raft_server_t *r = raft_new_with_log(..., ...);
    raft_add_non_voting_node(r, NULL, 1, 1);

    // Assuming you loaded snapshot into your application,
    // extracted node configuration list,
    // extracted last_applied_term and last_applied_index
    app_configure_from_snapshot(r, cfg, last_applied_term, last_applied_index);
    
    app_load_logs_to_impl(); // Load log entries in your log implementation
    raft_restore_log();
    
    app_read_metadata_file(); // Read metadata file and extract term and vote
    raft_restore_metadata(r, term, vote);
}


```

