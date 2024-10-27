[c++11-17 模板核心概念 - 知乎](https://www.zhihu.com/column/c_1306966457508118528)

### 初识函数模板

[隐式实例化，显式实例化，显式具体化，部分具体化_基础](https://blog.csdn.net/weixin_43717839/article/details/131320956)

[隐式实例化、显式实例化_进阶](https://blog.csdn.net/Jxianxu/article/details/124359007)

[7.9 模板应用于多文件编程 - 知乎](https://zhuanlan.zhihu.com/p/628170334)

[7.10 模板的显式实例化 - 知乎](https://zhuanlan.zhihu.com/p/628172691)

```c++
using namespace std;
template<typename T>
T m_max(T a, T b)
{
	return a > b ? a : b;
}

template

int main() {
	cout << m_max<int>(1, 2) << endl;	// 隐式实例化，显式实例化后面讲

	return 1;
}
```

解析：

- 函数模板不是函数，而是一个用于生成函数的“图纸”。函数模板必须实例化，才会生成具体的函数定义**（注意实例化指的是生成定义，而非直接使用）**
- 函数模板如果没有被使用，或者没有被实例化，就不会生成实际的函数代码。
- 模板是静态的，也就是说**模板实例化的过程位于编译期，没有运行时开销**

### 模板参数推导？？？重写笔记

[先看此链接—— 理解模板参数推导规则 - 知乎？？？](https://zhuanlan.zhihu.com/p/338788455)

```c++
//using namespace std;
template<typename T>
T max(const T& a, const T& b)
{
	return a > b ? a : b;
}

int main() {
/***********************************案例一***********************************/
    
	max(1, 2);				// T推导为int
    max(1, 2.1)				// 推导失败
	max<double>(1, 2.2);	// 显式指定参数，不进行类型推导
    
/***********************************案例二***********************************/
    
	int a = 1;
	const int& b = a;
	max(a, b);				// T推导为int，a的类型为const int&
	max<const int&>(a, b);	// T被指明为const int&，但是a的类型仍然为 const int&
    
/***********************************案例三***********************************/
    
	// ADL 实参依赖查找：编译器会在函数参数（string）的命名空间（std）里面查找该函数（max）
    // ∴报错：多个max实例与参数列表匹配
	max<std::string>("lus", std::string("1"));
	// 解决方法：
	std::max<std::string>("lus", std::string("1"));	// 使用std::max
	::max<std::string>("lus", std::string("1"));	// 使用全局命名空间的max

	return 1;
}
```

补充：

- 案例二：
  - line19：当模板参数为一个指针/引用，但非万能引用时，会根据情况忽略表达式的cv限制符和&，不会忽略*，这点和decltype类似
  - line20：模板在实例化时，会忽略掉多余的cv限制符和多余的&（如果模板参数是&&的话则不会忽略）

？？？分析

```c++
template<typename T>
T max(T&& a, T b)
{
	return a > b ? a : b;
}

int main() {
	int a = 1;
	const int& b = a;
	max(a, b);				// 编译错误？？？
	max<const int&>(a, b);	// T被指明为const int&，但是a的类型仍然为 const int&

	return 1;
}
```

#### 引用折叠

```c++
typedef const int T;
typedef T& TR;
TR& v = 1;			// 该申明再C++98中导致编译错误
```

其中`TR& v=1`这样的表达式会被编译器认为是不合法的表达式，而在C++11中，一 旦出现了这样的表达式，就会发生引用折叠，即将复杂的未知表达式折叠为已知的简单表达式，具体如下图。

![image-20240922100117950](./assets/image-20240922100117950.png)

**右值引用的右值引用折叠成右值引用，所有其他组合均折叠成左值引用**。而模板对类型的推导规则就比较简单，当转发函数的实参是类型的一个左值引用，那么模板参数被推导为X&类型，而转发函数的实参是类型X的一个右值引用的话，那 么模板的参数被推导为X&& 类型。结合以上的引用折叠规则，就能确定出参数的实际类型。 进一步，我们可以把转发函数写成如下形式

```c++
template <class Ty>
constexpr Ty&& forward(Ty& Arg) noexcept {
    return static_cast<Ty&&>(Arg);
}

int a = 10;            // 不重要
// 如果Arg类型也定义为Ty&&，则为万能引用，Ty被推断为int&，Ty&&为int&（万能引用）
// 正因为模板参数类型中有一个为Ty&而非Ty&&，所以在推断时不会用到万能引用，而是如同普通情况一样，被推断为int
::forward(a);
::forward<int>(a);     // 返回 int&& 因为 Ty 是 int，Ty&& 就是 int&&，未发生引用折叠
::forward<int&>(a);    // 返回 int& 因为 Ty 是 int&，Ty&& 就是 int&，Ty&被折叠成int&
::forward<int&&>(a);   // 返回 int&& 因为 Ty 是 int&&，Ty&& 就是 int&&,Ty&被折叠成int&
```

#### 万能引用

万能引用（universal reference），用在模板中，表现形式通常为`T&&`。表示**接受左值表达式那形参类型就推导为左值引用，接受右值表达式，形参类型就推导为右值引用**。

注意只有模板参数全为`T&&`的形式时，才表示根据万能引用判断规则判断T的类型，否则只会用到引用折叠。

比如：

```c++
template<typename T>
void func(T&& a)
{
	
}

int main() {
	int na = 1;
	const int cnb = na;
	int& clref = na;
	int&& drref = 1;
	func(na);	// 推导为void func<int&>(int& a);	左值推导为int&
	func(cnb);	// 推导为void func<const int&>(const int& a);	万能引用推导时会带上cv限制符
	func(clref);	// 推导为void func<int&>(int& a);	引用折叠
	func(10);	// 推导为void func<int>(int&& a);	右值推导为int，模板形参被推导为int&&
	func(drref);	// 推导为void func<int&>(int& a);	有名字的右值引用算左值

	return 1;
}
```

### 有默认实参的模板类型形参

[详情看这里：函数模板 | 现代 C++ 模板教程](https://mq-b.github.io/Modern-Cpp-templates-tutorial/md/第一部分-基础知识/01函数模板#有默认实参的模板类型形参)

```c++
using namespace std::string_literals;

template<
	typename T1,
	typename T2,
	// 不求值语境，decltype中的T1{}并没有真的创建临时对象，只是用于获取类型而已
	typename RT = decltype(true ? T1{} : T2{}) >
RT max(const T1& a, const T2& b) { // RT 是 std::string
    return a > b ? a : b;
}

int main(){
    auto ret = ::max("1", "2"s);
    std::cout << ret << '\n';
}
```

解析：

- 三目表达式的类型

  `decltype(true ? T1{} : T2{})`

  `decltype`中是一个三目运算符表达式。然后外面使用了 decltype 获取这个表达式的类型，那么问题是，为什么是 true 呢？以及为什么需要 T1{}，T2{} 这种形式？

  1. 我们为什么要设置为 **true**？

     其实无所谓，设置 false 也行，**true 还是 false 不会影响三目表达式的类型。**这涉及到了一些复杂的规则，简单的说就是三目表达式要求第二项和第三项之间能够隐式转换，**整个三目表达式的类型会是 “公共”类型。**

     比如第二项是 int 第三项是 double，三目表达式当然会是 double。
  
     ```c++
     using T = decltype(true ? 1 : 1.2);
     using T2 = decltype(false ? 1 : 1.2);
     ```

     **T 和 T2 都是 double 类型**。

  2. 为什么需要 `T1{}`，`T2{}` 这种形式？

     没有办法，必须构造临时对象来写成这种形式，这里其实是[不求值语境](https://zh.cppreference.com/w/cpp/language/expressions#.E6.BD.9C.E5.9C.A8.E6.B1.82.E5.80.BC.E8.A1.A8.E8.BE.BE.E5.BC.8F)**（就是说`T1{}`，`T2{}`并没有真的创建临时对象，写在这里只是用于让 decltype 获取表达式的类型而已）**。
  
     模板的默认实参的和函数的默认实参大部分规则相同。

简化：

1. C++11后置返回类型

   ```c++
   template<typename T,typename T2>
   auto max(const T& a, const T2& b) -> decltype(true ? a : b){
       return a > b ? a : b;
   }
   int main() {
   	const int a = 1;
   	const int b = 2;
   	max(a, b);
   }
   ```

   - 相比于原始版本：

     `decltype(true ? a : b)`可以带上cv限制符。**前提是传入的两个形参类型一样，否则三目表达式的类型会忽略其中的cv限定符和引用**

     `decltype(true ? T1{} : T2{})`无法带上cv限制符和引用，因为`T1{}`是构造一个临时对象，没有cv限定符以及引用修饰

   - 注意后置返回类型中，auto仅用于占位，不是推导。decltype才是推导

2. C++20简写函数模板（了解）

   ```c++
   decltype(auto) max(const auto& a, const auto& b)  {
       return a > b ? a : b;
   }
   ```

   1. [返回类型推导](https://zh.cppreference.com/w/cpp/language/function#.E8.BF.94.E5.9B.9E.E7.B1.BB.E5.9E.8B.E6.8E.A8.E5.AF.BC)（也就是函数可以直接写 auto 或 decltype(auto) 做返回类型，而不是像 C++11 那样，只是后置返回类型。
   2. [`decltype(auto)`](https://zh.cppreference.com/w/cpp/language/auto) 如果 `max` 示例如果不使用`decltype(auto)`而是`auto`，是不会有引用和 cv 限定的，就只能推导出返回 `T` 类型。

   > 大家需要注意后置返回类型和返回类型推导的区别，它们不是一种东西，后置返回类型虽然也是写的 `auto` ，但是它根本没推导，只是占位。

### 非类型的模板形参

**类型模板实参**传的是类型

**非类型模板实参**传的是值/对象

```c++
template<std::size_t N = 100>
void f() { std::cout << N << '\n'; }

f();     // 默认      f<100>
f<66>(); // 显式指明  f<66>
```

### 重载函数模板

```c++
template<typename T>
void test(T) { std::puts("template"); }

void test(int) { std::puts("int"); }

test(1);        // 优先选择非模板的普通函数，匹配到test(int)
test(1.2);      // 隐式实例化，匹配到模板
test("1");      // 匹配到模板
// “重载决议“就是选择最”匹配“最”合适“的函数
```

### 可变参数模板

```c++
void f(const char*, int, double) { puts("值"); }
void f(const char**, int*, double*) { puts("&"); }

template<typename... Args>//  1. 表示形参包，类型形参包，传入的类型全部存入Args中
// 2. 形参包，参数形参包const char * args0, int args1, double args2
// 2. Args... args 表示展开类型形参包，并将接收到的参数存入args中
void sum(Args... args){	
    f(args...);   // 3. 相当于 f(args0, args1, args2)
    f(&args...);  // 3. 相当于 f(&args0, &args1, &args2)
}

// args 是函数形参包，							  Args 是类型形参包
// args 存储的是传入的全部参数， 				  Args 存储的是所有参数的”类型“
// args... 表示参数形参包展开，展开args中全部参数， Args...表示类型形参包展开，展开Args中所有类型

int main() {
    sum("luse", 1, 1.2);
}
```

#### 逗号运算符

在C++中，**逗号运算符用于将多个表达式连接在一起，按顺序执行每个表达式，并返回最后一个表达式的值**。这种运算符通常用于for循环中的迭代表达式或在一行代码中执行多个操作。

示例:

```c++
int a, b;
a = (b = 3, b + 2); // b被赋值为3，然后计算b+2并赋值给a
```

在这个例子中，`b`首先被赋值为3，然后计算`b+2`的结果，最终将5赋值给`a`。整个逗号表达式的值是最右边表达式的值，即`b+2`

#### 模式

- 模式定义：后随省略号且其中**至少有一个形参包的名字**的**模式**会被展开 成零个或更多个**逗号分隔**的模式实例。

- `&args...` 中 `&args` 就是模式，`Args...`中的`Args`也是一个模式。在展开的时候，模式，也就是省略号前面的一整个表达式，会被不断复制并展开，同时形参包的位置会填入形参包中的第0个元素，然后逗号分隔，直至形参包的元素被消耗完。

- 形参包展开的场景：
  - 一是使用递归的办法把形参包里面的参数一个一个的拿出来进行处理，最后以一个默认的函数或者特化模板类来结束递归；
  - 二是直接把整个形参包展开以后传递给某个适合的函数或者类型。

```c++
template<typename...Args>
// const char (&args0)[5], const int & args1, const double & args2
void print(const Args&...args){
//	(cout << arg0 << endl, 0), (cout << arg1 << endl, 0), (cout << arg2 << endl, 0)
    int _[]{ (cout << args << endl, 0)... };
    cout << sizeof...(args) << endl;	// 计算形参包args中的元素个数（Args也是一样的）
}

int main() {
    print("luse", 1, 1.2);
}
```

习题：

1. 请你分析下列代码输出结果：

   ```c++
   template<typename...Args>
   void print(const Args&...args) {
   	int _[]{ (std::cout << args << ' ' ,0)...};
   }
   
   template<typename T,  size_t N, typename... Args>
   void func(const T(&arr)[N], Args... index){
   	print(arr[index]...);
   }
   
   int main() {
   	int arr[10] = { 0,1,2,3,4,5,6,7,8,9 };
   	func(arr, 1, 3, 5);
   	
   	return 1;
   }
   ```

   分析：

   - 模板函数通用分析步骤：

     1. **先看函数名的返回值和形参列表（根据列表中的逗号区分不同参数），不要先看模板部分。**

        `void func(const T(&arr)[N], Args... index)`：返回值为void，形参列表中一共有一个逗号，也就是两个参数，第一个参数是`const T(&arr)[N]`，第二个参数是`Args... index`

     2. **逐个分析参数，并将函数参数中涉及到的模板参数分成三类（类型模板形参，非类型模板形参，形参包），并且根据情况为这些模板形参设定默认参数，带入函数参数中进行分析。**

        第0个参数：`const T(&arr)[N]`，其中T是类型模板形参，N是非类型模板形参，我们假定`T=int`，`N=5`，显然该函数形参为一个数组引用。

        第1个参数：`Args... index`，其中`Args...`为类型形参包，说明该参数可以接收任意个函数形参

   - 显然根据上面的规则，结合模式的相关知识分析可得：

     `Args`是一个类型形参包，`Args...`表示形参包展开，结果为：`int, int, int`

     `index`是一个函数参数形参包，`arr[index]...`表示形参包展开，结果为：`arr[1], arr[3], arr[5]`

     **（分析形参包的时候，先粗略了解类型形参包中的大致类型，然后重点关注函数体中 函数参数形参包 的作用，以及展开后的结果）**

2. 实现一个函数`sum`，支持`sum(1,2,3,4.5,'1'...)`，即`sum`支持任意类型，任意个数的参数进行调用。

   - 方法一：（变长模板函数递归）

     > 注意 变长模板函数递归 的使用方式以及终止条件

     ```c++
     // 空参数版本，终结模板递归
     template<typename T>
     T sum(T num)
     {
     	return num;
     }
     // 参数递归，common_type_t<Args...>表示求形参包中的公共类型，和之前的decltype差不多功能，但更正规。
     template<typename T, typename... Args, typename RT = common_type_t<Args...>>
     RT sum(T num, Args... args)
     {
     	RT res = 0;
     	res = num + m_sum(args...);
     	return res;
     }
     int main() {
     	cout << sum(1, 2, 3, 4) << endl;
     }
     ```

   - 方法二：（数组）

     > 形参包展开，正好可以用来初始化数组

     ```c++
     template<typename... Args, typename RT = common_type_t<Args...>>
     RT sum(Args... args)
     {
     	int arr[] = { args... };	// 形参包展开，正好可以用来初始化数组
     	int res = 0;
     	for (int i = 0; i < sizeof...(args); i++)
     	{
     		res += arr[i];
     	}
     	return res;
     }
     
     int main() {
     	cout << sum(1, 2, 3, 4,5) << endl;
     }
     ```

### 模板分文件



