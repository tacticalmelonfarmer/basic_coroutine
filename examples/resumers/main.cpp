#include <basic_coroutine.hpp>

#include <vector>
#include <string>
#include <iostream>

using namespace tmf;

// coroutine takes `I` type as input, `O` as output
template<typename I, typename O>
struct Coro : basic_coroutine<Coro<I, O>>
{
private:
  I input_value;
  O yielded_value;

public:

  auto on_invoke()
  {
    return co_control::resume;
  }

  void on_return()
  {
  }

  // yielding an `O`, expecting an `I` on resume
  auto on_yield(co_expect<I, O> e)
  {
    yielded_value = e.from;
    // this expression creates a `co_resumer` object(2 member objects)
    // `co_resumer<F>::control` determines the current execution path (resume or suspend)
    // and `co_resumer<F>::on_resume` is a callable which provides the coroutine with an input value
    return co_control::suspend >> [&]() { return input_value; };
  }

  O resume(I value)
  {
    input_value = value;
    if (!this->resume())
    {
      // handle inability to resume
    }
    return yielded_value;
  }
};

int main()
{
}