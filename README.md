# Smart Block Device Driver

This repository contains a Linux kernel block device driver implementing a **priority-aware, cached write-back strategy**. The driver is designed for flexibility, efficiency, and real-time introspection via the procfs interface.

## Features

* **Priority-Aware Asynchronous Writes**
  Write requests are buffered and ordered by process priority, ensuring higher-priority operations are serviced first.

* **In-Memory Write Caching**
  Requests are cached in memory to minimize direct disk writes and optimize I/O performance.

* **Process-Specific Cache Flush**
  Supports flushing of write requests associated with a specific process using the procfs interface.

* **Dynamic Cache Resizing**
  Cache size can be adjusted at runtime via procfs.

* **Statistics and Monitoring**
  Real-time statistics and cache contents are accessible via `/proc/smart_block/`.

## Build and Installation

### Build the Module

```bash
make
```

### Load the Module

```bash
sudo insmod smart_block.ko cache_size=131072
```

This loads the block device driver with a 128KB cache (default is 64KB).

### Create a Device Node

```bash
sudo mknod /dev/smart_block b <major_number> 0
```

Replace `<major_number>` with the actual major number printed in `dmesg`.

### Unload the Module

```bash
sudo rmmod smart_block
```

## Procfs Interface

The driver exposes a procfs directory at `/proc/smart_block/` with the following entries:

| File           | Description                                                 |
| -------------- | ----------------------------------------------------------- |
| `stats`        | Displays cached bytes and total flushes to disk             |
| `flush`        | Accepts a PID to flush that process's write requests        |
| `resize_cache` | Accepts a new cache size (in bytes)                         |
| `cache`        | Displays a list of all pending write requests with metadata |

### Examples

Flush cache for PID 1234:

```bash
echo 1234 | sudo tee /proc/smart_block/flush
```

Resize cache to 256KB:

```bash
echo 262144 | sudo tee /proc/smart_block/resize_cache
```

Check statistics:

```bash
cat /proc/smart_block/stats
```

## Design Highlights

* **Write requests** are maintained in a linked list, sorted by process priority.
* **Flush operations** can be triggered automatically (e.g., cache overflow) or manually (via procfs).
* **Spinlocks** are used to ensure thread-safe cache access.
* **blk-mq** is used for scalable request queuing and dispatching.

## Testing

A test suite is included to verify:

* Write ordering by priority
* Cache resizing behavior
* Process-aware flush correctness
* Proper statistics reporting

## License

This project is licensed under the GPL-2.0 License.

## Author

Anubhav Pandey


