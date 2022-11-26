cmake_minimum_required (VERSION 3.12)

project ("basic_coroutine")

add_executable(generators EXCLUDE_FROM_ALL "generators/main.cpp")
target_link_libraries(generators PRIVATE basic_coroutine)

add_executable(resumers EXCLUDE_FROM_ALL "resumers/main.cpp")
target_link_libraries(resumers PRIVATE basic_coroutine)

add_executable(tasks EXCLUDE_FROM_ALL "tasks/main.cpp")
target_link_libraries(tasks PRIVATE basic_coroutine)

add_custom_target(examples)
add_dependencies(examples generators resumers tasks)