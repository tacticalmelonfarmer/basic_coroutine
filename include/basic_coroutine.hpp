#pragma once

#include <fwd.hpp>
#include <details.hpp>
#include <basic_promise.hpp>

#include <concepts>
#include <coroutine>
#include <mutex>
#include <type_traits>
#include <utility>

namespace tmf {

template<typename Future>
struct basic_coroutine
{
  using handle_type = std::coroutine_handle<basic_promise<Future>>;

private:

  friend basic_promise<Future>;

  handle_type m_handle{ nullptr };

  basic_coroutine& operator=(handle_type handle)
  {
    auto lock = handle.promise().lock_future();
    m_handle = handle;
    m_handle.promise().set_future(*this);
    return *this;
  }

public:
  basic_coroutine() {}
  basic_coroutine(basic_coroutine<Future> const&) = delete;
  basic_coroutine(basic_coroutine<Future>&& moved_from) noexcept
  {
    auto lock = m_handle.promise().lock_future();
    m_handle = std::exchange(moved_from.m_handle, nullptr);
    m_handle.promise().set_future(*this);
  }

  virtual ~basic_coroutine()
  {
    auto lock = m_handle.promise().lock_future();
    if(m_handle)
    {
      m_handle.promise().clear_future();
      if (m_handle.done())
      {
        m_handle.destroy();
      }
    }
  }

  // has this coroutine reached the final supension point?
  bool done() const
  {
    return m_handle.done();
  }

  // is this coroutine currently being executed?
  bool active() const
  {
    return m_handle.promise().active();
  }

  // is this coroutine suspended from a co_await (NOT co_yield) expression
  bool awaiting() const
  {
    return m_handle.promise().awaiting();
  }

  // resume the coroutine, if not returned from, and if not busy
  [[nodiscard]] bool resume()
  {
    if(done() || active() || awaiting())
    {
      return false;
    }
    else
    {
      if constexpr (basic_promise<Future>::uses_executor())
      {
        static_cast<Future&>(*this).executor(std::move([this]() { m_handle.resume(); }));
      }
      else
      {
        m_handle.resume();
      }
      return true;
    }
  }
};

} // end namespace tmf

namespace std
{

template<typename Future, typename... ArgTs>
requires std::derived_from<Future, tmf::basic_coroutine<Future>>
struct coroutine_traits<Future, ArgTs...>
{
  static constexpr auto check()
  {
    /*
    constexpr bool has_interface = DETECT_MEMBER_FUNCTION(Future, interface);
    constexpr bool conforming = requires
    {
      static_cast<void (Future::*)(ArgTs...)>(&Future::interface);
    };
    // if `Future` has 1 or more non-static member function declarations named
    // `interface`
    if constexpr (has_interface) {
      // then make sure the coroutine being constructed has a compatible
      // parameter list to 1 of these `interface` declarations
      static_assert(conforming,
                    "coroutine function signature does not conform to any "
                    "supplied interface(s)\n");
      return tmf::basic_promise<Future>{};
    }
    */
    
    return tmf::basic_promise<Future>{};
  }

  using promise_type = decltype(check());
};

} // end namespace std