#pragma once

#ifndef __FAAS_SRC
#error ipc/spsc_queue.h cannot be included outside
#endif

#include "base/common.h"
#include "utils/shared_memory.h"

namespace faas {
namespace ipc {

template<class T>
class SPSCQueue {
public:
    ~SPSCQueue();
    static_assert(sizeof(T) <= __FAAS_CACHE_LINE_SIZE,
                  "Message type must fit in one cache line");

    static constexpr size_t kConsumerSleepMask = size_t{1} << (sizeof(size_t)*8-1);

    // Called by the consumer
    static std::unique_ptr<SPSCQueue<T>> Create(std::string_view name, size_t queue_size);
    // Called by the producer
    static std::unique_ptr<SPSCQueue<T>> Open(std::string_view name);

    // Methods called by the producer
    void SetWakeupConsumerFn(std::function<void()> fn);
    bool Push(const T& message);  // Return false if queue is full

    // Methods called by the consumer
    void ConsumerEnterSleep();
    bool Pop(T* message);  // Return false if queue is empty

private:
    bool consumer_;
    utils::SharedMemory::Region* shm_region_;
    size_t queue_size_;
    size_t* head_;
    size_t* tail_;
    char* cell_base_;

    std::function<void()> wakeup_consumer_fn_;
    bool wakeup_consumer_flag_;

    SPSCQueue(bool producer, utils::SharedMemory::Region* shm_region);
    static size_t compute_total_bytesize(size_t queue_size);
    static void BuildMemoryLayout(char* base_ptr, size_t queue_size);
    void* cell(size_t idx) { return cell_base_ + idx * __FAAS_CACHE_LINE_SIZE; }

    DISALLOW_COPY_AND_ASSIGN(SPSCQueue);
};

}  // namespace ipc
}  // namespace faas

#include "ipc/spsc_queue-inl.h"
