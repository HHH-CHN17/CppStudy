/*******************************************************************************
* @author       LP
* @version:     1.0
* @date         25-1-26
* @description:
*******************************************************************************/
#ifndef CLOUDCONFERENCE_MEMORYPOOL_H
#define CLOUDCONFERENCE_MEMORYPOOL_H


#include <climits>
#include <cstddef>
#include <atomic>
#include <type_traits>
#include <utility>
#include <chrono>
#include <iostream>
#include <thread>
#include <cassert>
#include <memory>
#include <mutex>

/* 在内存池与无锁队列结合后，性能是原来的1/2甚至更低，原因如下：
 * 1. 原子操作内存序未规定
 * 2. 在new_element()，delete_element()的时候，由于成员变量aba的存在，实际上并没有实现真正的并发
 * 3. 后续优化策略：memorypool中，空闲链表（注意是空闲链表，而不是已占用链表）本身也能理解成一个消息队列
 *      其中链表中的push()，对应内存池的delete_element()，pop()同理
 *      其次是当空闲链表无空闲空间时，需要new一块新的block
 */

// 一个进程仅能拥有一个内存池，因为stop_是静态成员变量，多个内存池实例会影响判断要检测的内存池是否还在。
template <typename T, std::size_t block_size = 4096>
class MemoryPool
{
public:
    struct slot_t {
        T element;
        slot_t *next = nullptr;
        //std::atomic<uintptr_t> same = 0;
    };

    struct slot_head_t {
        uintptr_t aba = 0;
        slot_t* node = nullptr;
    };

    struct allocated_block_t {
        char *buffer = nullptr;
        allocated_block_t *next = nullptr;

        ~allocated_block_t() { operator delete(buffer); }
    };

/*
    class smart_ptr {
    private:
        slot_t* pslt_ = nullptr;
        uintptr_t same_ = 0;
    public:
        smart_ptr() = default;
        explicit smart_ptr(slot_t* pslt) {
            pslt_ = pslt;
            same_ = pslt->same.load();
        }
        smart_ptr(const smart_ptr& sp) {
            pslt_ = sp.pslt_;
            same_ = sp.same_;
        }
        smart_ptr(smart_ptr&& sp) noexcept {
            pslt_ = sp.pslt_;
            same_ = sp.same_;
            sp.pslt_ = nullptr;
        }
        ~smart_ptr() = default;

        slot_t* slt() {
            if (stop_)
                return nullptr;
            return pslt_;
        }

        T* ele() {
            if (!slt())
                return nullptr;
            return &(pslt_->element);
        }

        T operator*() {
            if (stop_)
                return T{};
            if (pslt_->same.compare_exchange_strong(same_, same_))
                return pslt_->element;
            return T{};
        }
    };*/

public:
    /* Member types */
    typedef T                       value_type;
    typedef T*                      pointer;
    typedef T&                      reference;
    typedef const T*                const_pointer;
    typedef const T&                const_reference;
    typedef size_t                  size_type;
    typedef char *                  data_pointer;
    //typedef smart_ptr               SmartPtr;

    // Constructor / destructor
    MemoryPool() noexcept {
        allocate();
    }
    ~MemoryPool() noexcept {
        stop_.store(true);
        allocated_block_t *curr = m_allocated_block_head;
        allocated_block_t *next = nullptr;
        while (curr != nullptr) {
            next = curr->next;
            delete curr;
            curr = next;
        }
    }

    MemoryPool(MemoryPool &&mp) = delete;
    MemoryPool& operator=(MemoryPool&& mp) = delete;

    MemoryPool(const MemoryPool& memoryPool) noexcept = delete;
    MemoryPool& operator=(const MemoryPool& memoryPool) = delete;

    [[nodiscard]] std::size_t max_size() const noexcept { return m_max_size * sizeof(slot_t); }
    [[nodiscard]] std::size_t max_number_objects() const noexcept { return m_max_size; }

    // Public member functions
    static pointer address(reference x) noexcept { return &x; };
    static pointer address(const_reference x) noexcept { return &x; };

    // Can only allocate one object at a time. n and hint are ignored
    // There is opportunity here for the ABA problem to rear it's ugly head.
    // See here: https://en.wikipedia.org/wiki/ABA_problem
    // The solution below works adequately.
    pointer allocate(size_type n = 1, const_pointer hint = nullptr) {
        slot_head_t next, orig = m_free.load();
        do {
            while (orig.node == nullptr) {
                if (!allocate_block())
                    return nullptr;
                orig = m_free.load();
            }
            next.aba = orig.aba + 1;
            next.node = orig.node->next;
        }
        while (!atomic_compare_exchange_weak(&m_free, &orig, next));
        return reinterpret_cast<pointer>(orig.node);
    }

    template <class... Args>
    pointer new_element(Args&&... args) {
        pointer result{ allocate() };
        if (!result) {
            return nullptr;
        }
        //result->same.fetch_add(1);
        construct<value_type>(result, std::forward<Args>(args)...);
        return result;
    }

    template <class U, class... Args>
    void construct(U* p, Args&&... args) {
        if (p != nullptr)
            new (p) U (std::forward<Args>(args)...);
    }

    void deallocate(pointer p, size_type n = 1)
    {
        slot_head_t next, orig = m_free.load();
        auto* tp = reinterpret_cast<slot_t*>(p);
        do {
            tp->next = orig.node;
            next.aba = orig.aba + 1;
            next.node = tp;
        }
        while (!atomic_compare_exchange_weak(&m_free, &orig, next));
    }

    void delete_element(pointer p) {
        if (p) {
            //p.slt()->same.fetch_add(1);
            p->~value_type();
            deallocate(p);
            //std::cout << "delete: " << typeid(T).name() <<  std::endl;
        }
    }

    // This prevents blocks from being allocated more quickly than "threshold" seconds
    void set_allocate_block_threshold(uint32_t thresh) { m_allocate_block_threshold = thresh; }

    template <class U>
    void destroy(U* p) {
        if (p != nullptr)
            p->~U();
    }

private:
    // Private variables
    uint32_t m_allocate_block_threshold = 0;
    uint64_t m_max_size = 0;
    slot_t *m_last_slot = nullptr;
    allocated_block_t *m_allocated_block_head = nullptr;
    std::atomic<slot_head_t> m_free;
    std::mutex m_lock;
    std::chrono::system_clock::time_point m_last_allocate_block_time { std::chrono::system_clock::now() };
    inline static std::atomic_bool stop_ = false;

    // Private functions
    size_type pad_pointer(const data_pointer p, size_type align) const noexcept {
        auto result = reinterpret_cast<uintptr_t>(p);
        return ((align - result) % align);
    }

    bool allocate_block() {
        std::lock_guard<std::mutex> lgmtx{m_lock};
        // After coming out of the mtx_, if the condition that got us here is now false, we can safely return
        // and do nothing.  This means another thread beat us to the allocation.  If we don't do this, we could
        // potentially allocate an entire block_size of memory that would never get used.
        if (m_free.load().node != nullptr) { return true; }

        std::chrono::system_clock::time_point now { std::chrono::system_clock::now() };
        if (m_max_size > 0 &&
            (now <= m_last_allocate_block_time + std::chrono::seconds(m_allocate_block_threshold))) {
            return false;
        }

        m_last_allocate_block_time = std::chrono::system_clock::now();

        auto* new_block = new allocated_block_t();
        new_block->next = m_allocated_block_head;
        m_allocated_block_head = new_block;

        new_block->buffer = reinterpret_cast<char *>(operator new(block_size * sizeof(slot_t)));

        // Pad block body to satisfy the alignment requirements for elements
        char *body = new_block->buffer + sizeof(slot_t *);
        std::size_t body_padding = pad_pointer(body, alignof(slot_t));
        char *start = body + body_padding;
        char *end = (new_block->buffer + (block_size * sizeof(slot_t)));

        // Update the old last slot's next ptr to point to the first slot of the new block
        if (m_last_slot)
            m_last_slot->next = reinterpret_cast<slot_t *>(start);

        // We'll never get exactly the number of objects requested, but it should be close.
        for (; (start + sizeof(slot_t)) < end; start += sizeof(slot_t)) {
            reinterpret_cast<slot_t *>(start)->next = reinterpret_cast<slot_t *>(start + sizeof(slot_t));
            m_max_size++;
        }

        // "start" should now point to one byte past the end of the last slot.  Subtract the size of slot_t from it to
        // get a pointer to the beginning of the last slot.
        m_last_slot = reinterpret_cast<slot_t *>(start - sizeof(slot_t));
        m_last_slot->next = nullptr;

        // If there's anything in the free list, make sure it doesn't get lost when we reset m_free
        if (m_free.load().node != nullptr)
            m_last_slot->next = m_free.load().node;

        // Update the head of the free list to point to the start of the new block
        slot_head_t first;
        first.aba = 0;
        first.node = reinterpret_cast<slot_t *>(body + body_padding);
        m_free.store(first);

        return true;
    }
};

#endif //CLOUDCONFERENCE_MEMORYPOOL_H
