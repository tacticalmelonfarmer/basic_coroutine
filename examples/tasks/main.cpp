#include "task.hpp"

Task<int> awaited()
{
  co_yield nothing;
  co_yield nothing;
  co_yield nothing;
  co_return 0;
}

Task<int> awaiting()
{
  auto cr = awaited();
  // returns this thread to caller, and suspends this coroutine to resume on
  // another thread (after the awaited yields or returns), until the awaited returns
  while (!cr.ready())
  {
    co_await cr;
  }
  co_return cr.get();
}

int main()
{
  auto cr = awaiting();
  cr.run();
  return cr.get();
}