#include <basic_coroutine.hpp>

#include <atomic>
#include <string>
#include <iostream>
#include <thread>

using namespace tmf;

template<typename T>
struct Task : basic_coroutine<Task<T>>
{
private:
  inline static int m_instances{0};

  std::atomic_flag m_ready{};
  bool m_without_executor{ false };
  T m_value{};
  int m_id;

public:

  // when any user-defined constructors exist, you need to 
  // implement a user-defined default-construcor as well
  Task()
    : m_id{ m_instances++ }
  {
  }

  // `basic_coroutine` is only movable
  Task(Task<T>&& other)
    : m_value{ other.m_value }
    , m_id{ other.m_id }
  {
    if (other.m_ready.test())
      m_ready.test_and_set();
  }

  ///! <customization points>

  // use this to customize how a coroutine-resume is executed
  // common uses of 
  template<typename F>
  void executor(F&& callable)
  {
    std::cout << "executor->resuming coroutine: [" << m_id << "]\n\tfrom thread: [" << std::this_thread::get_id() << "]\n";
    if (m_without_executor)
    {
      callable();
    }
    else
    {
      std::thread{std::forward<F>(callable)}.detach();
    }
  }

  auto on_invoke()
  {
    std::cout << "creating coroutine: [" << m_id << "]\n\tfrom thread: [" << std::this_thread::get_id() << "]\n";
    return co_control::suspend;
  }

  void on_return(T value)
  {
    std::cout << "returning from coroutine: [" << m_id << "]\n\tfrom thread: [" << std::this_thread::get_id() << "]\n";
    m_value = value;
    m_ready.test_and_set();
    m_ready.notify_all();
  }

  auto on_yield()
  {
    std::cout << "yielding from coroutine: [" << m_id << "]\n\tfrom thread: [" << std::this_thread::get_id() << "]\n";
    return co_control::suspend;
  }

  auto on_await(co_expect<Task<T>&> e)
  {
    std::cout << "awaiting: [" << m_id << "]\n\tfrom thread: [" << std::this_thread::get_id() << "]\n";
    // `on_await` supports 2 types of `co_resumer<F>::on_resume`: one that only has side effect (shown here)
    // the other takes the awaited value as input, and transforms it as output (not shown here)
    return co_control::surrender >> []() { std::cout << "recieving\n\tfrom thread: [" << std::this_thread::get_id() << "]\n"; };
    // when awaiting `surrender` will use whatever the original awaiter intended (safest option)
  }

  void on_error(std::exception_ptr eptr)
  try
  {
    std::cout << "an exception has occured inside coroutine: [" << m_id << "]\n\tfrom thread: [" << std::this_thread::get_id() << "]\n";
    std::rethrow_exception(eptr);
  }
  catch(std::exception& e)
  {
    std::cout << e.what();
  }

  ///! </customization points>

  void run()
  {
    if (!this->resume())
    {
      // unable to resume? `.done()`, `.active()` and `.awaiting()` will tell you why.
    }
  }

  void sync_run()
  {
    m_without_executor = true;
    run();
    m_without_executor = false;
  }

  bool ready() const
  {
    return m_ready.test();
  }

  T get()
  {
    m_ready.wait(false);
    return m_value;
  }

  bool await_ready() const
  { return false; }
  void await_suspend(std::coroutine_handle<> h)
  {
    sync_run();
    h.resume();
  }
  decltype(auto) await_resume()
  { return *this; }
};