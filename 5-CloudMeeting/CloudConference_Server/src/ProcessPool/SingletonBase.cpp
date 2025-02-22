/*******************************************************************************
* @author:      LP
* @version:     1.0
* @date:        25-2-2
* @description:
*******************************************************************************/

#include "SingletonBase.h"
#include "ProcessPool.h"    // .cpp文件中引用，防止循环引用

// static变量必须在.cpp文件中初始化。防止引用时在不同翻译单元有同一个static变量的副本
template<typename T>
std::unique_ptr<T, void(*)(T*)> Singleton_Lazy_Base<T>::up(nullptr, T::Destory);
template<typename T>
std::once_flag Singleton_Lazy_Base<T>::of;
template<typename T>
std::atomic_bool  Singleton_Lazy_Base<T>::ab{false};



// 显式实例化
template struct Singleton_Lazy_Base<processpool>;
