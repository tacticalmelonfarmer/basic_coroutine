#include <basic_coroutine.hpp>

#include <vector>
#include <iostream>

using namespace tmf;

template<typename T>
struct Generator : basic_coroutine<Generator<T>>
{
  std::vector<T> output;

  auto on_invoke()
  {
    return co_control::suspend;
  }

  void on_return(T const& value)
  {
    output.push_back(value);
  }

  auto on_yield(T const& value)
  {
    output.push_back(value);
    return co_control::suspend;
  }

  void operator()()
  {
    if(!this->resume())
    {
      // handle inability to resume
    }
  } 
};

Generator<int> iota(int begin, int end)
{
  int n = begin;
  if(begin < end)
  {
    while(n < end)
    {
      co_yield n++;
    }
  }
  else
  {
    while(n > end)
    {
      co_yield n--;
    }
  }
  co_return n;
}

Generator<int> fibonacci(bool& stop)
{
  int a=0, b=1, t;
  if (stop) co_return a;
  co_yield a;
  if (stop) co_return b;
  co_yield b;
  while(true) // :)
  {
    t = a + b;
    a = b;
    b = t;
    if (stop) co_return t;
    co_yield t;
  }
}

int main()
{
  {
    auto g = iota(0, 10);
    while(!g.done())
    {
      g();
      for (auto i : g.output)
        std::cout << i << ' ';
      std::cout << '\n';
    }
  }
  {
    bool stop = false;
    auto g = fibonacci(stop);
    while(!g.done())
    {
      if (g.output.size() >= 9) stop = true;
      g();
      for (auto i : g.output)
        std::cout << i << ' ';
      std::cout << "\n";
    }
  }
}
