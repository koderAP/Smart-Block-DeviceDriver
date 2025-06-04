#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/seq_file.h>



#define DEVICE_NAME "smart_block"
#define NSECTORS    4096
#define DEV_BYTES   (NSECTORS * SECTOR_SIZE)
#define SMARTBLOCK_MINORS 1

static int cache_size = 65536; // default 64KB
module_param(cache_size, int, 0644);
MODULE_PARM_DESC(cache_size, "Initial cache size in bytes");

struct write_req {
    struct list_head list;
    pid_t pid;
    int prio;
    sector_t sector;
    unsigned int len;
    void *data;
};

struct smartblock_dev {
    u8 *data;
    struct gendisk *gd;
    struct request_queue *queue;
    struct blk_mq_tag_set tag_set;
    spinlock_t lock;
    struct list_head cache;
    size_t cached_bytes;
    size_t cache_limit;
    unsigned long write_to_disk_count;

    //Workqueue for normal flushing
    struct workqueue_struct *flush_wq;
    struct work_struct flush_work;
};

static struct smartblock_dev device;

static struct proc_dir_entry *proc_dir, *proc_stats, *proc_flush, *proc_resize, *proc_cache;

static void flush_write_req(struct write_req *req)
{
    size_t offset = req->sector * SECTOR_SIZE;
    if (offset + req->len > DEV_BYTES)
        req->len = DEV_BYTES - offset;
    memcpy(device.data + offset, req->data, req->len);
    printk(KERN_INFO "Data flushed at sector :%llu.\n", req->sector);
    device.write_to_disk_count++;
}



static void flush_cache_locked(pid_t target_pid)
{
    struct list_head *pos, *n;
    struct write_req *req;
    list_for_each_safe(pos, n, &device.cache) {
        req = list_entry(pos, struct write_req, list);
        if (target_pid == -1 || req->pid == target_pid) {
            flush_write_req(req);
            device.cached_bytes -= req->len;
            list_del(pos);
            kfree(req->data);
            kfree(req);
        }
    }
}

static void insert_write_req(struct write_req *new_req)
{
    struct list_head *pos;
    struct write_req *req;
    // Insert by descending priority (higher prio first)
    list_for_each(pos, &device.cache) {
        req = list_entry(pos, struct write_req, list);
        if (new_req->prio < req->prio)
            break;
    }
    list_add_tail(&new_req->list, pos);
    device.cached_bytes += new_req->len;
    printk(KERN_INFO "PID is :%d", new_req->pid);
}


static void flush_cache_for_range(sector_t start_sector, unsigned int nsect)
{
    struct list_head *pos, *n;
    struct write_req *req;
    sector_t req_start, req_end, read_start, read_end;
    // printk(KERN_INFO "Sector is : %llu, No of sectors is : %u \n", start_sector, nsect);

    read_start = start_sector;
    read_end = start_sector + nsect - 1;

    list_for_each_safe(pos, n, &device.cache) {
        req = list_entry(pos, struct write_req, list);
        req_start = req->sector;
        req_end = req->sector + (req->len / SECTOR_SIZE) - 1;
        // Check for overlap
        if (!(req_end < read_start || req_start > read_end)) {
            flush_write_req(req);
            device.cached_bytes -= req->len;
            list_del(pos);
            kfree(req->data);
            kfree(req);
        }
    }
}

static void flush_cache_bytes(size_t bytes_needed)
{
    struct list_head *pos, *n;
    struct write_req *req;
    size_t freed = 0;

    list_for_each_safe(pos, n, &device.cache) {
        if (freed >= 3*bytes_needed)
            break;
        req = list_entry(pos, struct write_req, list);
        flush_write_req(req);
        freed += req->len;
        device.cached_bytes -= req->len;
        list_del(pos);
        kfree(req->data);
        kfree(req);
    }
}




static void smartblock_normal_flush_work(struct work_struct *work)
{
    struct write_req *req = NULL;

    spin_lock(&device.lock);
    if (!list_empty(&device.cache)) {
        req = list_first_entry(&device.cache, struct write_req, list);
        list_del(&req->list);
        device.cached_bytes -= req->len;
    }
    spin_unlock(&device.lock);

    if (req) {
        flush_write_req(req); // Write to device memory
        kfree(req->data);
        kfree(req);
    }
}



static blk_status_t smartblock_queue_rq(struct blk_mq_hw_ctx *hctx,
                                        const struct blk_mq_queue_data *bd)
{
    struct request *req = bd->rq;
    struct bio *bio = req->bio;
    pid_t owner = bio->pid_owner;
    int priority = bio->bi_ioprio;
    sector_t sector = blk_rq_pos(req);
    
    unsigned int nsect = blk_rq_sectors(req);
    unsigned int len = nsect * SECTOR_SIZE;
    int dir = rq_data_dir(req);

    blk_mq_start_request(req);

    spin_lock(&device.lock);
    if (dir == WRITE) {
        void *buffer = kmalloc(len, GFP_KERNEL);
        if (!buffer) {
            spin_unlock(&device.lock);
            blk_mq_end_request(req, BLK_STS_RESOURCE);
            return BLK_STS_RESOURCE;
        }
        struct bio_vec bvec;
        struct req_iterator iter;
        unsigned int copied = 0;
        rq_for_each_segment(bvec, req, iter) {
            void *iovec_mem = kmap_local_page(bvec.bv_page) + bvec.bv_offset;
            memcpy(buffer + copied, iovec_mem, bvec.bv_len);
            kunmap_local(iovec_mem - bvec.bv_offset);
            copied += bvec.bv_len;
            if (copied >= len)
                break;
        }

        

        size_t bytes_needed = (device.cached_bytes + len > device.cache_limit) ?
                      (device.cached_bytes + len - device.cache_limit) : 0;
        if (bytes_needed > 0){
            printk(KERN_INFO "WHAT THE WHAT");
            flush_cache_bytes(bytes_needed);}

        struct write_req *wreq = kmalloc(sizeof(*wreq), GFP_KERNEL);
        if (!wreq) {
            kfree(buffer);
            spin_unlock(&device.lock);
            blk_mq_end_request(req, BLK_STS_RESOURCE);
            return BLK_STS_RESOURCE;
        }
        wreq->pid = owner;
        wreq->prio = priority;
        // wreq->pid = current->pid;
        // wreq->prio = current->prio;
        wreq->sector = sector;
        wreq->len = len;
        wreq->data = buffer;
        INIT_LIST_HEAD(&wreq->list);
        if(len > device.cache_limit){
            // if request is greater than cache limit, write through after clearing cache
            printk(KERN_INFO "WHAT THE WHAT1");
            flush_cache_locked(-1);
            flush_write_req(wreq);
        }else{
            insert_write_req(wreq);
            // queue_work(device.flush_wq, &device.flush_work);
        }
    } else{
        // Read: flush cache first to ensure latest data ?
        // flush_cache_locked(-1);
        flush_cache_for_range(sector, nsect);
        if (sector * SECTOR_SIZE + len > DEV_BYTES)
            len = DEV_BYTES - sector * SECTOR_SIZE;
        struct bio_vec bvec;
        struct req_iterator iter;
        unsigned int copied = 0;
        rq_for_each_segment(bvec, req, iter) {
            void *iovec_mem = kmap_local_page(bvec.bv_page) + bvec.bv_offset;
            memcpy(iovec_mem, device.data + sector * SECTOR_SIZE + copied, bvec.bv_len);
            kunmap_local(iovec_mem - bvec.bv_offset);
            copied += bvec.bv_len;
            if (copied >= len)
                break;
        }
    }
    spin_unlock(&device.lock);
    blk_mq_end_request(req, BLK_STS_OK);
    return BLK_STS_OK;
}

static int smartblock_open(struct gendisk *disk, blk_mode_t mode)
{
    printk(KERN_INFO "[smart_block] Device opened by PID %d (%s)\n", current->pid, current->comm);
    return 0;
}

static void smartblock_release(struct gendisk *gd)
{
}

static struct block_device_operations smartblock_ops = {
    .owner = THIS_MODULE,
    .open  = smartblock_open,
};

static ssize_t stats_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    char tmp[128];
    int len = snprintf(tmp, sizeof(tmp),
        "Cached bytes: %zu\nWrite-to-disk count: %lu\n",
        device.cached_bytes, device.write_to_disk_count);
    return simple_read_from_buffer(buf, count, ppos, tmp, len);
}

static ssize_t flush_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    printk(KERN_INFO "PID file opened");
    char kbuf[32];
    pid_t pid;
    if (count >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;
    kbuf[count] = 0;
    if (kstrtoint(kbuf, 10, &pid))
        return -EINVAL;
    spin_lock(&device.lock);
    flush_cache_locked(pid);
    spin_unlock(&device.lock);
    return count;
}

static ssize_t resize_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char kbuf[32];
    size_t new_limit;
    if (count >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;
    kbuf[count] = 0;
    if (kstrtoint(kbuf, 10, (int *)&new_limit))
        return -EINVAL;
    spin_lock(&device.lock);
    device.cache_limit = new_limit;
    if (device.cached_bytes > device.cache_limit)
        flush_cache_locked(-1);
    spin_unlock(&device.lock);
    return count;
}

#include <linux/seq_file.h>

//Read callback for /proc/smart_block/cache
static int cache_show(struct seq_file *m, void *v)
{
    struct write_req *req;
    unsigned int idx = 0;

    spin_lock(&device.lock);
    list_for_each_entry(req, &device.cache, list) {
        seq_printf(m, "[%u] PID: %d  Prio: %d  Sector: %llu  Len: %u\n",
            idx++, req->pid, req->prio, (unsigned long long)req->sector, req->len);
    }
    spin_unlock(&device.lock);

    if (idx == 0)
        seq_puts(m, "(cache empty)\n");
    return 0;
}

static int cache_open(struct inode *inode, struct file *file)
{
    return single_open(file, cache_show, NULL);
}

static const struct proc_ops cache_fops = {
    .proc_open    = cache_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};


static const struct proc_ops stats_fops = {
    .proc_read = stats_read,
};

static const struct proc_ops flush_fops = {
    .proc_write = flush_write,
};

static const struct proc_ops resize_fops = {
    .proc_write = resize_write,
};

static int __init smartblock_init(void)
{
    int ret = 0;
    device.data = vzalloc(DEV_BYTES);
    if (!device.data)
        return -ENOMEM;
    spin_lock_init(&device.lock);
    INIT_LIST_HEAD(&device.cache);
    device.cache_limit = cache_size;
    device.cached_bytes = 0;
    device.write_to_disk_count = 0;

    static struct blk_mq_ops mq_ops = {
        .queue_rq = smartblock_queue_rq,
    };
    device.tag_set.ops = &mq_ops;
    device.tag_set.nr_hw_queues = 1;
    device.tag_set.queue_depth = 128;
    device.tag_set.numa_node = NUMA_NO_NODE;
    device.tag_set.cmd_size = 0;
    device.tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
    device.tag_set.driver_data = NULL;

    ret = blk_mq_alloc_tag_set(&device.tag_set);
    if (ret)
        goto out_data;

    device.queue = blk_mq_alloc_queue(&device.tag_set, NULL, &device);
    if (IS_ERR(device.queue)) {
        ret = PTR_ERR(device.queue);
        goto out_tagset;
    }
    // ret = blk_mq_init_allocated_queue(device.queue, &device.tag_set);
    // if (ret)
    //     goto out_queue;

    device.gd = blk_mq_alloc_disk_for_queue(device.queue, NULL);
    if (IS_ERR(device.gd)) {
        ret = PTR_ERR(device.gd);
        // goto err_destroy_queue;
        goto out_queue;
    }

    device.gd->major = register_blkdev(0, DEVICE_NAME);
    if (device.gd->major <= 0) {
        ret = -EBUSY;
        goto out_disk;
    }
    device.gd->first_minor = 0;
    device.gd->minors = 1;
    device.gd->fops = &smartblock_ops;
    device.gd->queue = device.queue;
    device.gd->private_data = &device;
    snprintf(device.gd->disk_name, 32, DEVICE_NAME);
    set_capacity(device.gd, NSECTORS);
    printk(KERN_INFO "Adding smart_block driver\n");
    printk(KERN_INFO "  with Major => %d\n", device.gd->major);
    printk(KERN_INFO "  and Minors => %d\n", device.gd->minors);
    printk(KERN_INFO "  and Disk name => %s\n", device.gd->disk_name);
    printk(KERN_INFO "  and Capacity => %llu sectors\n", get_capacity(device.gd));


    ret = add_disk(device.gd);
    if (ret) {
        printk("smartblock: add_disk failed with %d\n", ret);
        goto out_disk;
    }

    device.flush_wq = create_singlethread_workqueue("smartblock_flush_wq");
    if (!device.flush_wq) {
        printk(KERN_ERR "Failed to create workqueue\n");
        ret = -ENOMEM;
        goto out_disk;
    }
    INIT_WORK(&device.flush_work, smartblock_normal_flush_work);


    // procfs
    proc_dir = proc_mkdir(DEVICE_NAME, NULL);
    proc_stats = proc_create("stats", 0444, proc_dir, &stats_fops);
    proc_flush = proc_create("flush", 0222, proc_dir, &flush_fops);
    proc_resize = proc_create("resize_cache", 0222, proc_dir, &resize_fops);
    proc_cache = proc_create("cache", 0444, proc_dir, &cache_fops);

    printk(KERN_INFO "smartblock: loaded\n");
    return 0;
out_disk:
    del_gendisk(device.gd);
    put_disk(device.gd);
out_queue:
    blk_put_queue(device.queue);
out_tagset:
    blk_mq_free_tag_set(&device.tag_set);
out_data:
    vfree(device.data);
    return ret;
// err_destroy_queue:
//     blk_mq_destroy_queue(device.queue);
//     return ret;
}

static void __exit smartblock_exit(void)
{
    spin_lock(&device.lock);
    flush_cache_locked(-1);
    spin_unlock(&device.lock);

    

    if (proc_stats) remove_proc_entry("stats", proc_dir);
    if (proc_flush) remove_proc_entry("flush", proc_dir);
    if (proc_resize) remove_proc_entry("resize_cache", proc_dir);
    if (proc_cache) remove_proc_entry("cache", proc_dir);
    if (proc_dir) remove_proc_entry(DEVICE_NAME, NULL);
    



    unregister_blkdev(device.gd->major,DEVICE_NAME);
    flush_workqueue(device.flush_wq);
    destroy_workqueue(device.flush_wq);

    

    del_gendisk(device.gd);
    put_disk(device.gd);
    blk_mq_destroy_queue(device.queue);
    blk_put_queue(device.queue);
    blk_mq_free_tag_set(&device.tag_set);
    vfree(device.data);
    printk(KERN_INFO "smartblock: unloaded\n");
}

module_init(smartblock_init);
module_exit(smartblock_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anubhav");
MODULE_DESCRIPTION("Smart Block Device Driver with Priority Caching");
