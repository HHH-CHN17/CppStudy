[现代C++并发编程教程 | 现代C++并发编程教程](https://mq-b.github.io/ModernCpp-ConcurrentProgramming-Tutorial/)

## 前言

- 并发，并行，线程，进程

  - 线程是轻量级进程，本课程仅讨论线程知识
  - 并发：指一个处理器同时处理多个任务。
    并行：指多个处理器或者是多核的处理器同时处理多个不同的任务。
    并发是逻辑上的同时发生（simultaneous），而并行是物理上的同时发生。

    所以说对于进程来说，可以并发，也可以并行；对于线程来说，也一样都可以，从写代码的角度来说，并发和并行没啥区别，可以当成一个东西

## Hello World

```c++
#include <iostream>
#include <thread>  // 引入线程支持头文件

void hello(){     // 定义一个函数用作打印任务
    std::cout << "Hello World" << std::endl;
}

int main(){
    std::thread t{ hello };		    // 创建线程对象，并将该对象关联到一个线程资源上
    cout << t.joinable() << endl;	// true，表示当前对象t关联了一个活跃线程
    t.join();		// 阻塞，并等待t对应的线程执行完毕，执行完毕后会将joinable()中的值设为false
    cout << t.joinable() << endl;	// flase，表示当前对象t没有关联一个活跃线程
  
    std::thread t1{ };		// 默认构造创建线程对象，但是不会关联到具体的线程资源上
    cout << t1.joinable() << endl;	// false
}
```

解析：

- `std::thread t{ hello };` 创建了一个线程对象 `t`，将 `hello` 作为它的[可调用(Callable)]([https://blog.csdn.net/qq_43145072/article/details/103749956)对象**（此处我理解为函数指针和[闭包类的对象](https://blog.csdn.net/skdkjzz/article/details/43968449)）**在新线程中执行。**线程对象关联了一个线程资源**，在线程对象构造成功后，就自动在新线程中执行函数 `hello`。
- （看《C++并发编程》p24）启动了线程，你需要明确是要等待线程结束（加入式），还是让其自主运行（分离式）。如果 `std::thread` 对象销毁之前还没有做出决定，程序就会终止（ `std::thread` 的析构函数会调用 `std::terminate()` ）。
- `t.join();` 等待线程对象 `t` 关联的线程执行完毕，否则将一直阻塞。这里的调用是必须的，**因为程序结束时，相关联的子线程资源必须释放**，如果不调用，`std::thread` 的析构函数将调用 [`std::terminate()`](https://zh.cppreference.com/w/cpp/error/terminate) 无法正确析构。
- 这是因为我们创建线程对象 `t` 的时候就关联了一个活跃的线程，调用 `join()` 就是确保线程对象关联的线程已经执行完毕，然后会修改对象的状态，让 [`std::thread::joinable()`](https://zh.cppreference.com/w/cpp/thread/thread/joinable) 返回 `false`，表示线程对象目前没有关联活跃线程。`std::thread` 的析构函数，正是通过 `joinable()` 判断线程对象目前是否有关联活跃线程，如果为 `true`，那么就当做有关联活跃线程。**显然在thread类对象析构时，其对应的线程依然活跃是不正常的**，所以会调用 `std::terminate()`**（在noexcept修饰的函数中抛出异常时也会调用该函数）**。

## 当前环境支持的并发线程数

[看这里：当前环境支持的并发线程数](https://mq-b.github.io/ModernCpp-ConcurrentProgramming-Tutorial/md/02使用线程.html#当前环境支持并发线程数)

使用 [`hardware_concurrency`](https://zh.cppreference.com/w/cpp/thread/thread/hardware_concurrency) 函数可以获得我们当前硬件支持的并发线程数量，它是 `std::thread` 的静态成员函数。

针对并行求和的函数，只需要注意一个点：

```c++
for (std::size_t i = 0; i < num_threads; ++i) {
            auto end = std::next(start, chunk_size + (i < remainder ? 1 : 0));
            threads.emplace_back([start, end, &results, i] {
                results[i] = std::accumulate(start, end, value_type{});
            });
```

其中，在第三行的地方创建并启动了线程，且该线程的可调用对象是一个lambda函数。

emplace_back是一个函数模板，参数是形参包，将参数全部完美转发到内部，然后使用placement new（`new(&threads[index]) thread(lambda)`）在threads的尾部调用thread的有参构造函数原地构造线程对象并启动线程

## 线程管理

在 C++ 标准库中，没有直接管理线程的机制，只能通过对象关联线程后，**通过该对象来管理线程**。类 `std::thread` 的对象就是指代线程的对象，而我们本节说的“线程管理”，其实也就是指管理 `std::thread` 对象。（这句话很关键，好好理解）

### 启动新线程

[具体细节看这里](https://mq-b.github.io/ModernCpp-ConcurrentProgramming-Tutorial/md/02使用线程.html#启动新线程)

#### 默认构造

```c++
std::thread t; //  构造不表示线程的新 std::thread 对象
```

**默认构造**，`std::thread` 线程对象没有关联线程，也不会启动线程执行任务callable，后续会讲到，现在没啥用

#### 有参构造

想要构造的对象能启动线程，我们需要在构造时传递一个 `可调用（callable）对象`。在之前，我们传递的参数有 `函数名（就是函数指针）`，[#lambda表达式](../2-深入理解C++11/深入理解C++11.md#lambda函数)，当然，也可以传递 `函数对象`：

```c++
class Task{
public:
	void operator()()const {
		std::cout << "operator()()const\n";
	}
};

int main() {
	thread t(Task());	// 语义分析中，该语句被认为是函数申明
	t.join();
}
```

Oops！编译失败了，编译器会将第9行解析为函数声明，而不是类型对象的定义

- 原因：

  - 先看看我们最熟悉的函数申明：

    ```c++
    int test(int);
    ```

    其中参数列表中，只有形参类型，没有形参名称，是一个占位参数，很好理解
  - 再来看看第九行：

    ```c++
    thread t(Task());
    ```

    `thread`：函数返回类型

    `t`：函数名称

    `Task()`：函数形参类型，是个占位参数，分析如下：

    - `int a`：`a`为 `int`类型，其中 `int`表示类型名，`a`表示声明的具体变量
    - `Task (*p)()`：`p`为函数指针类型，其中 `Task(*)()`表示类型名，`p`表示声明的具体变量
    - `Task p()`：函数类型声明，`p`为函数类型，其中 `Task()`表示类型名，`p`表示声明的具体变量

    所以你发现了，`thread t(Task())`相当于 `thread t(Task p())`。但这并不准确，实际上是相当于 `thread t(Task (*p)())`，这是因为：

    > 在确定每个形参的类型后，类型是 “T 的数组”或某个**函数类型 T 的形参会调整为具有类型“指向 T 的指针”**
    >

    显然 `Task()`是个函数类型，它被调整为了指向这个函数类型的指针类型：`Task(*)()`。
  - 总结：

    通过上面分析，你会发现 `int test(int)`和 `thread t(Task())`都是只有一个占位参数的函数声明，很好理解。
- 解决办法：

  - thread初始化时使用[#列表初始化](../2-深入理解C++11/深入理解C++11.md#C++11列表初始化)：

    ```c++
    thread t{Task()};
    ```

    但注意，我们平时使用列表初始化时，如果类中有定义 `参数为initializer_list的构造函数`，则会优先调用此构造，而非 多参构造函数。
  - 使用括号表达式：

    ```c++
    thread t((Task()));
    ```

#### 线程的执行策略

启动线程后（也就是构造 `std::thread` 对象）我们必须在线程对象的生存期结束之前，即 [`std::thread::~thread`](https://zh.cppreference.com/w/cpp/thread/thread/~thread) 调用之前，决定它的执行策略，是 [`join()`](https://zh.cppreference.com/w/cpp/thread/thread/join)（加入，可以理解成将子线程加入/合并进当前线程，使得当前线程的结束时间由两者中最晚结束的那个线程决定）还是 [`detach()`](https://zh.cppreference.com/w/cpp/thread/thread/detach)（分离，即当前线程和子线程分离）。

我们先前使用的就是 `join()`，我们聊一下 `detach()`，当 `std::thread` 线程对象调用了 `detach()`，那么就是**线程对象放弃了对线程资源的所有权，不再管理此线程，允许此线程独立的运行，在线程退出时释放所有分配的资源**。（很重要的一句话，好好理解）

放弃了对线程资源的所有权，也就是线程对象没有关联活跃线程了，此时 joinable 为 **`false`**。

在单线程的代码中，对象销毁之后再去访问，会产生[未定义行为](https://zh.cppreference.com/w/cpp/language/ub)，多线程增加了这个问题发生的几率。

比如函数结束，那么函数局部对象的生存期都已经结束了，都被销毁了，此时线程函数还持有函数局部对象的指针或引用。

```c++
int main() {
	int a = 100;
	thread t([&]() {
		this_thread::sleep_for(2s);
		cout << a << endl;
	});
	t.detach();						// 不会阻塞，可能产生潜在的未定义行为
	cout << t.joinable() << endl;	// 显然，分离后，当前线程对象不在管理原来的子线程，输出显然为false
}
// detach不会阻塞，就是线程分离了
// 分离后子线程可能还在执行的时候，主线程已经销毁释放资源了
// 如果子线程在主线程结束后依然访问主线程中的对象，那么就是未定义行为
// 本例中，使用cout（位于主线程的std中），访问a（位于主线程的main函数里）都是未定义行为。
```

这里需要点名批评几种蠢得死行为：

- `join()`后又 `detach()`

  人家 `join()`本来就是等待线程执行结束的，线程都执行结束了，就没必要 `detach()`了
- `detach()`后又 `join()`

  `detach()`后子线程和主线程中的线程对象都分离了，再来 `join()`有什么意义吗

#### 异常

```c++
struct Task; // 复用之前
void f(){
	int n = 0;
	thread t{ Task{} };
	try{
		throw exception();	// 抛出异常
	}
	catch (exception& e){
		t.join(); // 1
		throw e;			// catch住异常后要再次抛出
	}
	t.join();    // 2
}

int main() {
	try {
		f();
	}
	catch (...) {
		cout << "异常" << endl;
	}
}
```

解释：

- 不能通过主线程来处理子线程的异常，异常处理是线程自己的事

  ```c++
  void f(){
  	int n = 0;
  	try{
  		thread t{ Task{} };		// 编译错误
  	}
  	catch (exception& e){
  		t.join(); // 1
  		throw e;			// catch住异常后要再次抛出
  	}
  	t.join();    // 2
  }
  ```

- 为什么需要两个`t.join()`，为什么catch住异常后需要再次抛出

  第一个`t.join()`用于确保在**catch住异常后**，子线程正常执行完成，线程对象正常析构

  第二个`t.join()`用于确保在**不发生异常时**，子线程正常执行完成，线程对象正常析构

  `throw e`用于保证在执行完第一个`t.join()`后，不会执行到第二个`t.join()`

- 当然也可以改成这样：

  ```c++
  void f(){
  	int n = 0;
  	thread t{ Task{} };
  	try{
  		throw exception();		// 抛出异常
  	}
  	catch (exception& e){
  		t.join(); // 1
  		cout << "异常" << endl;	// 在当前函数就处理了异常
  	}
  	
  	if (t.joinable()) {		// 先判断一下能不能join()
  		t.join();    // 2
  	}
  }
  
  ```

### RAII

“[资源获取即初始化](https://zh.cppreference.com/w/cpp/language/raii)”(RAII，Resource Acquisition Is Initialization)。

简单的说是：***构造函数申请资源，析构函数释放资源，让对象的生命周期和资源绑定***。当异常抛出时，C++ 会自动调用对象的析构函数。

```c++
class thread_guard{
private:
	std::thread m_Thr;
public:
	template<typename Callable_Ty, typename... Args>
	explicit thread_guard(Callable_Ty&& obj, Args&&... args)
			: m_Thr(std::forward<Callable_Ty>(obj), std::forward<Args>(args)...)
	{}

	~thread_guard(){
		if (m_Thr.joinable()) { // 线程对象当前关联了活跃线程
			m_Thr.join();
		}
	}
	thread_guard(const thread_guard&) = delete;
	thread_guard& operator=(const thread_guard&) = delete;
};
```

解释：

- 哈哈这是我自己写的，叼吧
- 构造函数使用[#完美转发](../2-深入理解C++11/深入理解C++11.md#完美转发？？？)来创造线程对象，保证时间消耗少，
- 析构函数用于判断该函数是否能`join()`，并在`析构函数`中等待监控的子线程`m_Thr`执行完毕
- 复制赋值和复制构造定义为 `=delete` ，显然如果`thread_guard`能被复制，则`thread`也会被跟着复制，这是肯定不行的，**一个`线程资源`只能被一个`线程类对象`管理监视，一个`线程类对象`也只能管理一个`线程资源`**。
- 通过使用RAII，我们就不用像上一节那样写多个`join()`了

使用：

```c++
void f(){
    int n = 0;
    thread_guard tg{ func{n}, 10 };
    throw exception();
}

int main() {
    try {
       f();
    }
    catch (exception e) {
       cout << "异常" << endl;
    }
    //f();	
}
```

注意只有在抛出的异常被捕获时，thread_guard的析构函数才会被调用

### 传递参数

> - 一般情况下，不论线程函数中的参数类型是否为引用，在参数传递时都是先使用[`decay_t`](https://zh.cppreference.com/w/cpp/types/decay)确定退化后的参数类型，然后通过复制构造函数/移动构造函数构造出一个新的副本，存入`tuple`中，最后将该参数转换为[退化后的纯右值副本](https://zh.cppreference.com/w/cpp/standard_library/decay-copy)（也就是`decay_t`中获得的类型），存入`unique_ptr`，并传入子线程中，然后子线程再将该副本作为函数实参传入可调用对象，所以当函数参数类型为普通类型`T`和右值类型`T&&`时，可以按预期正常使用，类型为`T&`时，需要使用`ref()`才行（看不懂先看下面）
>
>   ```c++
>   void f(int a, move_only&& mo, int& b) { }
>   int main() {
>       move_only mo;	// move_only是一个只能默认构造，移动构造的类。
>       int m = 2;
>       std::thread t { f, m, move(mo), ref(m) };
>       t.join();
>   }
>   ```

- 容易出现的问题

  向可调用对象传递参数很简单，我们前面也都写了，只需要将这些参数作为 `std::thread` 的构造参数即可。需要注意的是，这些参数会复制到新线程的内存空间中，即使函数中的参数是引用，依然**实际是复制**。

  ```c++
  void f(int, const int& a) {
      std::cout << &a << '\n'; 
  }
  
  int main() {
      int n = 1;
      std::cout << &n << '\n';
      std::thread t { f, 3, n };
      t.join();
  }
  ```

  问题：

  - `&a`和`&n`两者值不同

    说明在一般情况下，主线程向子线程传递参数时是**值传递**

  - 如果去掉函数`f`中参数`a`的const修饰，则编译失败

    这是因为 `std::thread` 内部会将保有的参数副本转换为**右值表达式进行传递**，这是为了那些**只支持移动的类型**，左值引用没办法引用右值表达式，所以产生编译错误。

    ```c++
    // 只支持默认构造和移动构造
    struct move_only {
        move_only() { std::puts("默认构造"); }
        move_only(move_only&&)noexcept {
            std::puts("移动构造");
        }
        
        move_only(const move_only&) = delete;
    };
    
    void f(move_only){}	// 线程函数
    
    int main(){
        move_only obj;
        std::thread t{ f,std::move(obj) };
        t.join();
    }
    // 默认构造
    // 移动构造
    // 移动构造
    ```

    没有 `std::ref` 自然是会保有一个副本，所以有两次移动构造，第一次是在 `std::thread` 中通过移动构造函数移动构造了一个`move(obj)`的副本，第二次是调用函数 `f`。

- 解决办法：

  使用标准库的 [`std::ref`](https://zh.cppreference.com/w/cpp/utility/functional/ref) 、 `std::cref` 函数模板

  ```c++
  void f(int, int& a) {
      std::cout << &a << '\n'; 
  }
  
  int main() {
      int n = 1;
      std::cout << &n << '\n';
      std::thread t { f, 3, std::ref(n) };	// 使用ref，此时&a，&n两者值相同
      t.join();
  }
  ```

  解释：

  - `std::ref`(reference)函数模板返回一个包装类`std::reference_wrapper<T>`，该类是包装引用对象的类模板，将对象包装，可以隐式转换为被包装对象的引用（在本例中用来包装对象n，并可以隐式转换为n的引用）。
  - `std::cref`(const reference)同理，返回`std::reference_wrapper<const T>`，不过它是转换为包装对象的const引用。

- 在子线程中执行类的成员函数

  [**成员函数指针**](https://zh.cppreference.com/w/cpp/language/pointer#.E6.88.90.E5.91.98.E5.87.BD.E6.95.B0.E6.8C.87.E9.92.88)也是[*可调用*](https://zh.cppreference.com/w/cpp/named_req/Callable)(*Callable*)的 ，可以传递给 `std::thread` 作为构造参数，让其关联的线程执行成员函数。

  ```c++
  struct X{
      void task_run(int& n)const;
  };
  int main(){
  	X x;
  	int n = 0;
  	std::thread t{ &X::task_run, &x, ref(n) };
  	t.join();
  }
  
  ```

  解释：

  - 类的成员函数前面要加限定符
  - 成员函数第一个隐藏默认实参是该类的对象指针
  - 和之前一样，引用传递要用`ref()`

  当然还能用`bind()`

  ```c++
  struct X {
      void task_run(int& a)const{
          std::cout << &a << '\n';
      }
  };
  int main(){
  	X x;
  	int n = 0;
  	std::cout << &n << '\n';
  	std::thread t{ std::bind(&X::task_run, &x, ref(n)) };
  	t.join();
  }
  
  ```

  解释：

  - bind()忘了看[#这里](../C++八股文/C++学习难点.md#非静态函数)
  - `std::bind` 也是默认按值[**复制**](https://godbolt.org/z/c5bh8Easd)的，所以和我们之前的处理一样，引用需要使用`ref()`

#### 传递参数中的bug悬空引用

> - `std::thread` 构造仅代表“创建并使子线程进入就绪态”，而可调用对象由对应的，进入运行态的子线程进行调用。

- 前置知识

  **A的引用只能引用A，或者以任何形式转换到A**

  ```c++
  int main() {
      double a = 1;
      //int& p = a;   编译失败
      const int& p = a;
  }
  ```

  解释：

  - a隐式转换到了int类型，转换后的结果是**纯右值表达式**，所以需要用`const int&`或者`int&&`来接收

- 问题代码

  ```c++
  void f(const std::string&){}
  void test(){
      char buffer[1024]{};
      //todo.. code
      std::thread t{ f,buffer };
      t.detach();
  }
  ```

  解释：

  - buffer 是一个数组对象，作为 `std::thread` 构造参数的传递的时候会[*`decay-copy`*](https://zh.cppreference.com/w/cpp/standard_library/decay-copy) （确保实参在按值传递时会退化） **隐式转换为了指向这个数组的指针**。

  - 本例中线程创建，执行流程

    `std::thread` 的构造函数中调用了创建线程的函数（windows 下可能为 [`_beginthreadex`](https://learn.microsoft.com/zh-cn/cpp/c-runtime-library/reference/beginthread-beginthreadex?view=msvc-170)），它将我们传入的参数，f、buffer ，传递给这个函数，在新线程中执行函数 `f`。也就是说，调用和执行 `f(buffer)` 并不是说要在 `std::thread` 的构造函数中，而是在创建的新线程中，具体什么时候执行，取决于操作系统的调度，所以完全有可能函数 `test` 先执行完，而新线程此时还没有进行 `f(buffer)` 的调用，转换为`std::string`，那么 buffer 指针就**悬空**了，会导致问题。

  解决办法：

  - 将 `detach()` 替换为 `join()`。
  - `thread`构造时显式将 `buffer` 转换为 `std::string`。

### std::this_thread

[看这里就好了，没什么难的](https://mq-b.github.io/ModernCpp-ConcurrentProgramming-Tutorial/md/02使用线程.html#std-this-thread)

### `std::thread`转移所有权

> - 一个线程对象有且仅能拥有一个线程资源，一个线程资源能且仅能被一个线程对象持有
> - 所有权的转移，可以通过 `移动构造`，`移动赋值`，`swap()` 进行

传入可调用对象以及参数，构造 `std::thread` 对象，启动线程，而线程对象拥有了线程的所有权，线程是一种系统资源，所以可称作“*线程资源*”。

std::thread 不可复制。两个 std::thread 对象不可表示一个线程，std::thread 对线程资源是独占所有权。而**移动**操作可以将一个 `std::thread` 对象的线程资源所有权转移给另一个 `std::thread` 对象。

```c++
void f() {}
int main() {
	thread t1(f);
	thread t2(move(t1));	// 通过移动构造将t1持有的线程资源转移给t2
	thread t3 = move(t2);	// 通过移动赋值将t2持有的线程资源转移给t3
	thread t4 = thread(f);	// 临时对象也是右值表达式
	swap(t3, t4);			// 通过swap交换t3，t4的线程资源
}
```

函数返回 `std::thread` 对象：

```c++
std::thread f(){
    std::thread t{ [] {} };
    return t;
}

int main(){
    std::thread rt = f();
    rt.join();
}
```

解释：

- [#请耐心看完这里](../2-深入理解C++11/深入理解C++11.md#右值引用，移动语义，完美转发)
- 在关闭rvo/nrvo的情况下，一共发生了三次构造（默认构造，移动构造，移动构造）

所有权通过函数参数传递：

> 根据函数栈帧相关理解，函数调用传参，实际上是初始化了（构造）形参的对象

```c++
void f(std::thread t){
    t.join();
}

int main(){
    std::thread t{ [] {} };
    f(std::move(t));
    f(std::thread{ [] {} });
}
```

## std::thread构造-源码解析



## 项目要求？？？

学完模板编程后，针对云会议项目中的消息队列，完成以下需求：

1. 将原有的线程创建方式改为：《C++并发编程实战》p27的形式，[#RAII](#RAII)
