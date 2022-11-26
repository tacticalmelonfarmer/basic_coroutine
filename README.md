# basic_coroutine
This is a small template library aiming to make composing coroutine *types* much simpler. Implementing *future* semantics without implementing a
`promise_type` or touching `std::coroutine_traits`. It is still very not production worthy and no tests exist yet but there are a
 few examples to showcase basic functionality.
 
## Creating a coroutine type
Implementation begins with your standard CRTP pattern
```c++
#include <basic_coroutine.hpp>

struct my_coro_type : tmf::basic_coroutine<my_coro_type>
...
```
to create a valid type you need to implement 2 member callables `on_invoke` which is called when you create a coroutine instance and 
an `on_return` function whose parameter type is the coroutine return type.
### on_invoke, called when a coroutine instance is created
```c++
tmf::co_control on_invoke()
{
  return tmf::co_control::suspend;
}
```
the return type here `co_control` is an enum which tells the coroutine to either resume or suspend, 
an other return type `co_resumer` is possible but is not referenced directly
```c++
auto on_invoke()
{
  return tmf::co_control::suspend >> []() { return; };
}
```
this does the same as before but invokes a lambda on the next resume
### on_return, sets the return type and behaviour
```c++
void on_return(int value)
{
  do_something(value);
}
```
the coroutine returns an `int` and `my_coro_type` does something with it

## on_yield
member callables are optional to implement but required to support yielding from coroutines
### yielding a value, as in: `co_yield 42;`
```c++
tmf::co_control on_yield(int value)
{
  do_something(value);
  return tmf::co_control::suspend;
}
```
you can return a `co_resumer` as we did with `on_invoke`
### yielding a value and expecting a value upon resume, as in: `co_yield tmf::co_expect<int>::from(13);`
```c++
tmf::co_control on_yield(tmf::co_expect<int, int> ce)
{
  do_something(ce.from);
  return tmf::co_control::suspend >> []() { return 42; };
}
```
### yielding nothing and expecting a value upon resume, as in: `co_yield tmf::co_expect<int>();`
```c++
auto on_yield(tmf::co_expect<int> ce)
{
  return tmf::co_control::suspend >> []() { return 42; };
}
```
### yield with no input or output, as in: `co_yield tmf::nothing`
### yielding a value and expecting a value upon resume, as in: `co_yield tmf::co_expect<int>::from(13);`
```c++
tmf::co_control on_yield()
{
  return tmf::co_control::suspend;
}
```
## on_await
intercepts a `co_await` and can transform the result (type included) before giving it to the coroutine
you can use 2 types of resumers and `co_expect` is required in the signature
```c++
auto on_await(tmf::co_expect<int>)
{
  return tmf::co_control::surrender >> []() { /*no return value and no parameters*/};
}
```
the resumer can just be side effects, the resumer can be omitted if not needed
```c++
auto on_await(tmf::co_expect<int>)
{
  return tmf::co_control::surrender >> [](int n) { return n + 1; };
}
```
resumers can also be a transform

note: when awaiting and `tmf::co_control::surrender` is part of the return value, the coroutine does whatever the awaited object is doing

## executor
to customize how a coroutine is executed you will use the executor callable member
```c++
template<typename F>
void executor(F&& f)
{
  f();
}
```
this is the default behaviour, just done manually
anything can be used like threads, or a thread pool. making `my_coro_type` an awaiter type
by implementing `await_ready`, `await_suspend` and `await_resume` and coordinating with the executor can give you context dependent executors
such that only `co_await` operations inside a coroutine can spawn a new thread or vice-versa. sky's the limit
