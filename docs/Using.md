- [Implementing callbacks](#Implementing callbacks)
    * [Log file callbacks](#Log-file-callbacks)
        + [Sub-sub-heading](#sub-sub-heading)
- [Heading](#heading-1)
    * [Sub-heading](#sub-heading-1)
        + [Sub-sub-heading](#sub-sub-heading-1)
- [Heading](#heading-2)
    * [Sub-heading](#sub-heading-2)
        + [Sub-sub-heading](#sub-sub-heading-2)


Using the library
===============

Implementing callbacks
----------------

Libraft only provides core raft logic. Application should implement some set of callbacks to provide networking and storage.


### Libraft callbacks
You provide your callbacks to the Raft server using `raft_set_callbacks()`.
Application must implement all the mandatory callbacks. You can find more detailed info in the `raft.h`.

```c
typedef struct
{
    int (*raft_appylog_f) (raft_server_t* r, void *user_data, raft_entry_t *entry, raft_index_t entry_idx);
    int (*raft_persist_metadata_f) (raft_server_t *r, void *user_data, raft_term_t term, raft_node_id_t vote);
    raft_node_id_t (*raft_get_node_id_f) (raft_server_t *r, void *user_data, raft_entry_t *entry, raft_index_t entry_idx);
    int (*raft_node_has_sufficient_logs_f) (raft_server_t *r, void *user_data, raft_node_t *node);
    raft_time_t (*raft_timestamp_f) (raft_server_t *r, void *user_data);
    
    /* Networking */
    int (*raft_send_requestvote_f) (raft_server_t *r, void *user_data, raft_node_t *node, raft_requestvote_req_t *req);
    int (*raft_send_appendentries_f) (raft_server_t *r, void *user_data, raft_node_t *node, raft_appendentries_req_t *req);
    int (*raft_send_snapshot_f) (raft_server_t *r, void *user_data, raft_node_t *node, raft_snapshot_req_t *req);
    int (*raft_send_timeoutnow_f) (raft_server_t *r, void *user_data, raft_node_t *node);
    
    /* Snapshot handling */
    int (*raft_load_snapshot_f) (raft_server_t *r, void *user_data, raft_term_t snapshot_term, raft_index_t snapshot_index);
    int (*raft_get_snapshot_chunk_f) (raft_server_t *r, void *user_data, raft_node_t *node, raft_size_t offset, raft_snapshot_chunk_t *chunk);
    int (*raft_store_snapshot_chunk_f) (raft_server_t *r, void *user_data, raft_index_t snapshot_index, raft_size_t offset, raft_snapshot_chunk_t* chunk);
    int (*raft_clear_snapshot_f) (raft_server_t *r, void *user_data);
    
    /** 
     * Optional callbacks: 
     * 
     * Notification on events, debug logging, message flow control etc.
     */
    void (*raft_membership_event_f) (raft_server_t *r,void *user_data,raft_node_t *node,raft_entry_t *entry,raft_membership_e type);
    void (*raft_state_event_f) (raft_server_t *r, void *user_data, raft_state_e state);
    void (*raft_transfer_event_f) (raft_server_t *r, void *user_data, raft_leader_transfer_e result);
    void (*raft_log_f) (raft_server_t *r, void *user_data, const char *buf);
    int (*raft_backpressure_f) (raft_server_t* r, void *user_data, raft_node_t *node);
    raft_index_t (*raft_get_entries_to_send_f) (raft_server_t *r, void *user_data, raft_node_t *node, raft_index_t idx, raft_index_t entries_n, raft_entry_t **entries);
    
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
    raft_index_t (*get_batch) (void *log, raft_index_t idx, raft_index_t entries_n, raft_entry_t **entries);
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
    // See the example implementation above for this function.
    app_configure_from_snapshot(r, cfg, last_applied_term, last_applied_index);
    
    app_load_logs_to_impl(); // Load log entries in your log implementation
    raft_restore_log();
    
    app_read_metadata_file(); // Read metadata file and extract term and vote
    raft_restore_metadata(r, term, vote);
}


```



Sending a snapshot and loading a snapshot as a follower
------------------

#### Sending a snapshot

Leader node can send snapshot to the follower node if the follower is lagging behind. When that happends, libraft will call `raft_get_snapshot_chunk_f` callback and require a chunk from snapshot from the application. An example implementation would be:

```c

// Assuming you have a pointer to snapshot 
// (e.g you can use mmap'ed file or if snapshot is small, you can have a copy of snapshot in memory)
size_t snapshot_file_len;
void *snapshot_file_in_memory;

int app_get_snapshot_chunk_impl(raft_server_t* raft,
                                void *user_data, 
                                raft_node_t *node, 
                                raft_size_t offset, 
                                raft_snapshot_chunk_t *chunk)
{
    const raft_size_t remaining_bytes = snapshot_file_len - offset;
    if (remaining_bytes == 0) {
        /* All chunks are sent */
        return RAFT_ERR_DONE;
    }

    chunk->data = (char *) snapshot_file_in_memory + offset;
    chunk->last_chunk = (offset + chunk->len == rr->outgoing_snapshot_file.len);

    return 0;
}
```

#### Receiving a snapshot

Leader node can send snapshot to the follower node. Libraft will call required callbacks when that happens:

```c
// To clear the temporary snapshot file on the disk. Remember leader can move onto
// a newer snapshot and start sending it. In that case, libraft will instruct
// application delete partial file of the previous snapshot.
int raft_clear_snapshot_f(raft_server_t *raft, void *user_data); 

int raft_store_snapshot_chunk_f(raft_server_t *raft, void *user_data, raft_index_t snapshot_index, raft_size_t offset, raft_snapshot_chunk_t *chunk);
```

An example implementation of `raft_store_snapshot_chunk_f` would be:

```c

int app_raft_store_snapshot_impl(raft_server_t *r, 
                                 void *user_data,
                                 raft_index_t snapshot_index,
                                 raft_size_t offset,
                                 raft_snapshot_chunk_t *chunk)
{
    int flags = O_WRONLY | O_CREAT;
    
    if (offset == 0) {
        flags |= O_TRUNC;
    }

    int fd = open("temp_snapshot_file.db", flags, S_IWUSR | S_IRUSR);
    if (fd == -1) {
        return -1;
    }

    off_t ret_offset = lseek(fd, offset, SEEK_CUR);
    if (ret_offset != (off_t) offset) {
        close(fd);
        return -1;
    }

    size_t len = write(fd, chunk->data, chunk->len);
    if (len != chunk->len) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

```

#### Loading the received snapshot file

When snapshot is received fully, libraft will call `raft_load_snapshot_f` callback.

These are the steps when you want to load the received snapshot. Inside the callback:

- Call `raft_begin_load_snapshot()`
- Read snapshot into your application
- Configure libraft from the application.
- Call `raft_end_load_snapshot()`

An example implementation would be:

```c
int app_raft_load_snapshot(raft_server_t *r,
                           void *user_data,
                           raft_term_t snapshot_term,
                           raft_index_t snapshot_index)
{
    // See the function app_raft_store_snapshot() above. The assumption is 
    // application stores incoming chunks in a file called "temp_snapshot_file.db".
    // If we are inside this callback, it means we've received all the chunk, and 
    // we can rename it as the current snapshot file.
    int ret = rename("temp_snapshot_file.db", "final_snapshot_file.db");
    if (ret != 0) {
        return -1;
    }

    ret = raft_begin_load_snapshot(r, snapshot_term, snapshot_index);
    if (ret != 0) {
        return ret;
    }
    
    // Configure libraft from the configuration in the snapshot
    // See the example implementation above for more details about this function
    app_configure_from_snapshot();
    raft_end_load_snapshot(rr->raft);
    
    // Probably, you need to create mmap of the snapshot file or read it into the
    // memory to be prepared for the `raft_get_snapshot_chunk_f` callback.
    // See the example above for the `raft_get_snapshot_chunk_f` callback for more details.
    
    return 0;
}
```

Adding and removing nodes
------------------

Configuration change is done by submitting configuration change entries. Only one configuration change is allowed at a time. Once the entry is applied, you can submit another configuration change entry.

#### Adding a node
Adding node is done in two steps. In the first step, you wait until the new node catches up with the leader. A node with a slow connection can never catch up the leader. In this case, adding the node without waiting to obtain enough logs can cause unavailability.

e.g 
Single node cluster adds a new node directly. Majority in the cluster becomes two. To commit an entry, we need to replicate to the both nodes. Until the new node gets all the existing entries, we cannot replicate a new one. So, cluster will be unable to commit an entry until the new node catches up.


Add a node in two steps:

- Submit an entry with the type `RAFT_LOGTYPE_ADD_NONVOTING_NODE`.
- Libraft will call `raft_node_has_sufficient_logs_f` callback once the new node obtained enough log entries and caught up with the leader.
- Inside that callback, you can submit an entry with the type `RAFT_LOGTYPE_ADD_NODE`.
- Once that entry is applied, configuration change is completed.

e.g:

```c

struct app_node_config {
    raft_node_id_t id;
    int port;
    char host[128];
};

// Step-0 This is an example implementation of `raft_get_node_id_f` callback.
raft_node_id_t app_log_get_node_id(raft_server_t *r,
                                   void *user_data,
                                   raft_entry_t *entry,
                                   raft_index_t entry_idx)
{
    struct app_node_config *cfg = (struct app_node_config *) entry->data;
    return cfg->id;
}

// Step-1
void app_add_node(raft_server_t *r)
{
    struct app_node_config cfg = { 
        .id = 9999999,
        .port = 5000,
        .host = "localhost"
    };
    
    raft_entry_req_t *entry = raft_entry_new(sizeof(cfg));
    entry->type = RAFT_LOGTYPE_ADD_NONVOTING_NODE;
    memcpy(entry->data, &cfg, sizeof(cfg));
    
    int e = raft_recv_entry(r, entry, NULL);
    if (e != 0) {
        // handle failure
    }
    raft_entry_release(entry);
}

// Step-2 (Callback will be called when the new node catches up)
int app_node_has_sufficient_logs(raft_server_t *r,
                                 void *user_data, 
                                 raft_node_t *raft_node)
{
    struct app_node_config cfg = {
        .id = 9999999,
        .port = 5000,
        .host = "localhost"
    };
    
    raft_entry_req_t *entry = raft_entry_new(sizeof(cfg));
    entry->type = RAFT_LOGTYPE_ADD_NODE;
    memcpy(entry->data, &cfg, sizeof(cfg));
    
    int e = raft_recv_entry(r, entry, &response);
    raft_entry_release(entry);

    return e;
}

// Step-3 This is an example implementation of `applylog` callback.
int app_applylog(raft_server_t *r, 
                 void *user_data, 
                 raft_entry_t *entry, 
                 raft_index_t entry_idx)
{
    struct app_node_config *cfg;

    switch (entry->type) {
        case RAFT_LOGTYPE_ADD_NONVOTING_NODE:
            cfg = (struct app_node_config *) entry->data;
            printf("node_id:%d added as non-voting node", cfg->id);
            break;
        case RAFT_LOGTYPE_REMOVE_NODE:
            cfg = (struct app_node_config *) entry->data;
            printf("node_id:%d added as voting node", cfg->id);
            break;

    return 0;
}

```

#### Removing a node

- Submit an entry with the type `RAFT_LOGTYPE_REMOVE_NODE`
- Node will be removed when entry is applied.

```c

struct app_node_config {
    raft_node_id_t id;
};

// Step-0 This is an example implementation of `raft_get_node_id_f` callback.
raft_node_id_t app_log_get_node_id(raft_server_t *r,
                                   void *user_data,
                                   raft_entry_t *entry,
                                   raft_index_t entry_idx)
{
    struct app_node_config *cfg = (struct app_node_config *) entry->data;
    return cfg->id;
}

// Step-1
void app_remove_node()
{
    raft_entry_req_t *entry = raft_entry_new(sizeof(cfg));
    entry->type = RAFT_LOGTYPE_REMOVE_NODE;
    memcpy(entry->data, &cfg, sizeof(cfg));

    int e = raft_recv_entry(r, entry, &resp);
    if (e != 0) {
        // handle error
    }
    raft_entry_release(entry);
} 

// Step-2 This is an example implementation of `applylog` callback.
int app_applylog(raft_server_t *r, 
                 void *user_data, 
                 raft_entry_t *entry, 
                 raft_index_t entry_idx)
{
    struct app_node_config *cfg;

    switch (entry->type) {
        case RAFT_LOGTYPE_REMOVE_NODE:
            cfg = (struct app_node_config *) entry->data;
            printf("node_id:%d has been removed", cfg->id);
            break;
    
    return 0;
}

```