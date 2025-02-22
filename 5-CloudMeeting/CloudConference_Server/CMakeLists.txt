cmake_minimum_required(VERSION 3.30)
project(CloudConference)

set(CMAKE_CXX_STANDARD 17)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lpthread")
#set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads REQUIRED)

include_directories(./      ./src
        ./src/MemoryPool    ./src/MsgQueue
        ./src/PublicDef     ./src/ProcessPool
        ./src/Protocol      ./src/RoomBusiness
        ./src/ThreadPool    ./src/UserForward
        ./src/MemoryPool    ./src/PublicDef/NetWork
        ./src/Log)

add_executable(CloudConference src/main.cpp
        src/MsgQueue/MsgQueue.cpp
        src/MsgQueue/MsgQueue.h
        src/MemoryPool/MemoryPool.cpp
        src/MemoryPool/MemoryPool.h
        src/ThreadPool/ThreadPool.cpp
        src/ThreadPool/ThreadPool.h
        test.h
        src/PublicDef/NetWork/NetWork.h
        src/PublicDef/Protocol.h
        src/PublicDef/Public.h
        src/prefix.h
        src/RoomBusiness/RoomBusiness.cpp
        src/RoomBusiness/RoomBusiness.h
        src/UserForward/UserForward.cpp
        src/UserForward/UserForward.h
        test1.h
        src/ProcessPool/ProcessPool.cpp
        src/ProcessPool/ProcessPool.h
        src/ProcessPool/SingletonBase.cpp
        src/ProcessPool/SingletonBase.h
        src/PublicDef/NetWork/NetWork.cpp
        src/Log/Error.cpp
        src/Log/Error.h
)

target_link_libraries(${PROJECT_NAME} PRIVATE -latomic Threads::Threads)
