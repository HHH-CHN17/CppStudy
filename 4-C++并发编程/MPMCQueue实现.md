### SPSCQueue实现

```c++
template<typename T>
class SPSCQueue {
private:
    struct node {
        shared_ptr<T> data_;
        node* next_ = nullptr;
    };
    atomic<node*> head_;
    atomic<node*> tail_;

    node* pop_head() {
        if (head_.load(memory_order_seq_cst) == tail_.load(memory_order_seq_cst))
            return nullptr;
        node* old_head = head_.load(memory_order_seq_cst);
        head_.store(old_head->next_, memory_order_seq_cst);
        return old_head;
    }
public:
    SPSCQueue():
        head_(new node{}), tail_(head_.load(memory_order_seq_cst)) {

    }
    ~SPSCQueue() {
        while (head_.load(memory_order_seq_cst)) {
            node* old_head = head_.load(memory_order_seq_cst);
            head_.store(old_head->next_, memory_order_seq_cst);
            delete old_head;
        }
    }
    void push(T new_value) {
        shared_ptr<T> new_data = make_shared<T>(new_value);
        node* new_tail = new node{};
        
        node* old_tail = tail_.load(memory_order_seq_cst);
        old_tail->data_ = new_data;
        old_tail->next_ = new_tail;
        tail_.store(new_tail, memory_order_seq_cst);
    }

    shared_ptr<T> pop() {
        node* old_head = pop_head();
        if (!old_head)
            return shared_ptr<T>{};
        shared_ptr<T> res = old_head->data_;
        delete old_head;
        return res;
    }
};
```

解释：

- 对于`pop_head()`而言：

  显然，我们会调用两次`head_.load()`，显然单消费者的情况下，两次原子操作结果一样，所以我们可以减少一次

  ```c++
  node* pop_head() {
          node* old_head = head_.load(memory_order_seq_cst);
          if (old_head == tail_.load(memory_order_seq_cst))
              return nullptr;
          head_.store(old_head->next_, memory_order_seq_cst);
          return old_head;
      }
  ```

- 对于`void push(T new_value)`而言

  我们可以不用`old_tail_`，写成：

  ```c++
  void push(T new_value) {
      shared_ptr<T> new_data = make_shared<T>(new_value);
      node* new_tail = new node{};
  
  	tail_.load()->data_ = new_data;	// 动作1
  	tail_.load()->next_ = new_tail;	// 动作2
  	tail_.store(new_tail);			// 动作3
  }
  ```

  由于动作1，2，3默认使用最严格的内存序，所以如果其他线程能读取到动作3，那么动作1，2一定能发生，但是我们需要注意：

  1. 原子变量的操作会消耗较多资源
  2. 对于无锁编程而言，我们要求：**捕获-修改-发布**
     - **捕获 (Capture)**：在操作开始时，**一次性地**原子读取共享状态（比如tail_指针），并将其值**捕获**到一个本地、非共享的变量中。这个变量就是old_tail。这是你对共享世界的一个“快照”。
     - **修改 (Modify)**：在本地进行所有准备工作。基于你捕获的那个**稳定**的快照（old_tail），创建新节点，修改旧节点的成员等。这个过程不涉及任何共享变量的读写，因此非常快，且不会有任何竞态。
     - **发布 (Publish)**：当你的一切准备就绪，通过**一次**原子的写操作（store或compare_exchange），将你的修改结果“发布”回共享世界。

