add_library(epoll_lib Epoll.cpp)

add_executable(main main.cpp)
target_link_libraries(main epoll_lib)