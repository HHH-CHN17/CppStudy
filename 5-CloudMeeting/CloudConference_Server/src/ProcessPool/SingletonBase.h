/*******************************************************************************
* @author:      LP
* @version:     1.0
* @date:        25-2-2
* @description:
*******************************************************************************/

#ifndef CLOUDCONFERENCE_SINGLETONBASE_H
#define CLOUDCONFERENCE_SINGLETONBASE_H

#include <memory>
#include <mutex>
#include <iostream>
#include <atomic>
#include <exception>

// 懒汉式单例的基类
template<typename T>    //T 是子类
class Singleton_Lazy_Base {
private:
    static std::unique_ptr<T, void(*)(T*)> up;
    static std::once_flag of;
    static std::atomic_bool ab;

    // 初始化函数也要写成static！！！
    template<typename ...Args>
    static void init(Args&&... args) {
        up.reset(new T(std::forward<Args>(args)...));
        std::cout << "init_multiArgs" << std::endl;
    }

    static void Destory(T* p_sgl) {
        delete p_sgl;
    }
public:
    template<typename ...Args>
    static T& InitInstance(Args&&... args) {
        // lambda无法使用万能引用（C++20前），故此处使用模板函数完成完美转发
        call_once(of, Singleton_Lazy_Base<T>::init<Args...>, std::forward<Args>(args)...);
        ab.store(true);
        return *up;
    }

    static T& GetInstance() {
        call_once(of, []() {
            up.reset(new T);
            std::cout << "init" << std::endl;
        });
        return *up;
    }

    Singleton_Lazy_Base(const Singleton_Lazy_Base&) = delete;
    Singleton_Lazy_Base(Singleton_Lazy_Base&&) = delete;
    Singleton_Lazy_Base& operator=(const Singleton_Lazy_Base&) = delete;

protected:
    Singleton_Lazy_Base() = default;
    ~Singleton_Lazy_Base(){ std::cout << "~Singleton" << std::endl;}
};


#endif //CLOUDCONFERENCE_SINGLETONBASE_H
