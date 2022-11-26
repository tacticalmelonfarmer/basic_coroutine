#include <basic_coroutine.hpp>

#include <vector>
#include <string>
#include <cstddef>
#include <iostream>
#include <variant>

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

  O operator() (I value)
  {
    input_value = value;
    if (!this->resume())
    {
      // handle inability to resume
    }
    return yielded_value;
  }
};

Coro<std::variant<int64_t, double, bool>, std::string*> csv_log(size_t max_line_width)
{
  std::string log;
  size_t lines = 0;
  while(true)
  {
    auto input = co_yield co_expect<std::variant<int64_t, double, bool>>::from(&log);
    if (std::holds_alternative<int64_t>(input))
    {
      log += std::to_string(std::get<int64_t>(input));
    }
    else if (std::holds_alternative<double>(input))
    {
      log += std::to_string(std::get<double>(input));
    }
    else if (std::holds_alternative<bool>(input))
    {
      log += std::get<bool>(input) ? "true" : "false";
    }
    log += ", ";
    if(lines++)
    {
      auto line_width = std::distance(log.begin() + log.find_last_of('\n'), log.end());
      if (line_width >= max_line_width)
        log += '\n';
    }
    else if (log.size() >= max_line_width)
    {
        log += '\n';
    }
  }
}

int main()
{
  auto cr = csv_log(7);
  cr(42);
  cr(false);
  cr(3.14159);
  cr(13);
  std::cout << *cr(true);
}