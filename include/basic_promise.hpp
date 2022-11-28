#pragma once

#include <fwd.hpp>
#include <concepts.hpp>
#include <details.hpp>

#include <atomic>
#include <coroutine>
#include <exception>
#include <future>
#include <mutex>
#include <utility>

namespace tmf {

template<typename Expected, typename Yielding = void>
struct co_expect
{
  using expected_type = Expected;
  using yielding_type = Yielding;
  yielding_type from;

  operator co_expect<Expected, std::remove_reference_t<yielding_type>>()
  {
    return { static_cast<yielding_type>(from) };
  }
};

template<typename Expected>
struct co_expect<Expected, void>
{
  using expected_type = Expected;
  using yielding_type = void;

  template<typename T>
  constexpr static auto from(T&& value)
  {
    return co_expect<Expected, T&&>{ std::forward<T>(value) };
  }

  template<typename T>
  constexpr static auto from(T* value)
  {
    return co_expect<Expected, T*>{ value };
  }
};

// Use this to yield without providing a value or expecting a value upon resume
static constexpr co_expect<void, void> nothing{};

template<typename>
struct implement_promise_return;

template<template<typename> typename Promise, VoidReturningFuture Future>
struct implement_promise_return<Promise<Future>>
{
  void return_void()
  {
    auto& self = static_cast<Promise<Future>&>(*this);
    self.future().on_return();
  }
};

template<template<typename> typename Promise, typename Future>
struct implement_promise_return<Promise<Future>>
{
  template<typename FwdT>
  void return_value(FwdT&& value) requires
    requires(Future& f, FwdT&& v)
    {
      f.on_return(std::forward<FwdT>(v));
    }
  {
    auto& self = static_cast<Promise<Future>&>(*this);
    self.future().on_return(std::forward<FwdT>(value));
  }
};

template<typename Future>
struct basic_promise : public implement_promise_return<basic_promise<Future>>
{
private:

  basic_coroutine<Future>* m_future{ nullptr };

  std::atomic_flag m_active{}, m_awaiting{}, m_exception_thrown{};

  std::mutex m_mutex;

  void activate()
  {
    if (m_active.test_and_set())
    {
      throw std::runtime_error(
        "[Error][Coroutine Promise]: attempted to resume an active coroutine"
        ", coroutine execution may only be transferred to a single thread at a time"
      );
    }
  }
  void deactivate() { m_active.clear(); }

  void await_value()
  {
    m_awaiting.test_and_set();
  }
  void recieve_value()
  {
    m_awaiting.clear();
  }

public:

  [[nodiscard]] bool has_future() const { return m_future != nullptr; }
  Future& future() const { return static_cast<Future&>(*m_future); }
  void set_future(basic_coroutine<Future>& init) { m_future = &init; }
  void clear_future() { m_future = nullptr; }

  bool active() const { return m_active.test(); }

  bool awaiting() const { return m_awaiting.test(); }

  [[nodiscard]] auto lock_future() { return std::move(std::unique_lock<std::mutex>{ m_mutex }); }

  static constexpr bool uses_executor()
  {
    constexpr bool result = tmf::details::inplace_sfinae
    {
      []<typename> () constexpr { return false; },
      []<typename F> requires requires(F& f) { f.executor([]() {}); }
        () constexpr { return true; }
    }.check(tmf::details::typle<Future>{});
    return result;
  }

  basic_promise() {}
  basic_promise(const basic_promise<Future>&) = delete;
  void operator=(const basic_promise<Future>&) = delete;

  Future get_return_object()
  {
    Future object{};
    basic_coroutine<Future>& base = object;
    base = std::coroutine_handle<basic_promise<Future>>::from_promise(*this);
    return object;
  }

  // BEGIN INITIAL AWAITER
  template<typename Resumer>
  struct initial_awaiter_type
  {
    basic_promise<Future>* const self;
    Resumer resumer;

    bool is_resuming(co_control control)
    {
      switch (control)
        {
          case co_control::resume:
            return true;
            break;
          case co_control::suspend:
            return false;
            break;
          case co_control::surrender:
          default:
            return true;
            break;
        }
    }

    bool await_ready()
    {
      if constexpr (uses_executor())
      {
        return false;
      }
      else
      {
        return is_resuming(resumer);
      }
    }
    void await_suspend(std::coroutine_handle<> handle)
    {
      auto lock = self->lock_future();
      if (!self->has_future())
      {
        throw std::runtime_error(
                "[Error]@[Coroutine Promise][Initial Suspend Awaiter]: missing future object"
        );
      }
      self->deactivate();
      if constexpr (uses_executor())
      {
        if (is_resuming(resumer))
        {
          self->future().executor(std::move([=]() { handle.resume(); }));
        }
      }
    }
    void await_resume()
    {
      auto lock = self->lock_future();
      if (self->has_future()) {
        self->activate();
        if constexpr (Specializes<Resumer, co_resumer>)
          resumer.on_resume();
      } else {
        throw std::runtime_error(
          "[Error]@[Coroutine Promise][Initial Suspend Awaiter]: missing future object"
        );
      }
    }
};
// END INTITIAL AWAITER

auto
initial_suspend()
{
  auto resumer = future().on_invoke();
  return initial_awaiter_type<decltype(resumer)>{ this, std::move(resumer) };
}

// BEGIN FINAL AWAITER
struct final_awaiter_type
{
  basic_promise<Future>* const self;
  bool await_ready() noexcept
  {
    return false;
  }
  void await_suspend(std::coroutine_handle<> handle) noexcept
  {
    auto lock = self->lock_future();
    self->deactivate();
    if (!self->has_future())
    {
      handle.destroy();
      return;
    }
  }
  void await_resume() noexcept {}
};
// END FINAL AWAITER

auto
final_suspend() noexcept
{
  return final_awaiter_type{ this };
}

void
unhandled_exception()
{
  constexpr bool has_error_customization = inplace_sfinae
  {
    []<typename>() constexpr { return false; },
    []<typename F> requires
    requires(F& f, std::exception_ptr e)
    { { f.on_error(e) } -> std::same_as<void>; }
    () constexpr { return true; }
  }.check(typle<Future>{});
  if constexpr (has_error_customization) {
    auto lock = lock_future();
    if (has_future()) {
      future().on_error(std::current_exception());
    } else {
      throw std::runtime_error(
        "[Error]@[Coroutine Promise][Unhandled Exception Handler]: missing future object"
      );
    }
  }
}

// BEGIN YIELD-ONLY AWAITER
template<typename Yielding, typename Resumer>
struct yield_only_awaiter_type
{
  basic_promise<Future>* const self;
  Resumer resumer;

  bool is_resuming(co_control control)
  {
    switch (control)
      {
        case co_control::resume:
          return true;
          break;
        case co_control::suspend:
          return false;
          break;
        case co_control::surrender:
        default:
          return false;
          break;
      }
  }

  bool await_ready()
  {
    if constexpr (uses_executor())
    {
      return false;
    }
    else
    {
      return is_resuming(resumer);
    }    
  }
  void await_suspend(std::coroutine_handle<> handle)
  {
    auto lock = self->lock_future();
    self->deactivate();
    if (!self->has_future())
    {
      handle.destroy();
      return;
    }
    if constexpr (uses_executor())
    {
      if (is_resuming(resumer))
      {
        self->future().executor([=]() { handle.resume(); });
      }
    }
  }
  decltype(auto) await_resume()
  {
    auto lock = self->lock_future();
    if constexpr (Specializes<Resumer, co_resumer>)
    {
      if (!self->has_future()) {
        throw std::runtime_error(
          "[Error]@[Coroutine Promise][Yield-Only Awaiter]: missing future object"
        );
      }
      self->activate();
      resumer.on_resume();
      return;
    }
    else
    {
      return;
    }
  }
};
// END YIELD-ONLY AWAITER

// yield a value, expecting non-input on next resume, using empty resume handler
template<typename Yielding>
auto yield_value(Yielding&& value) requires
  (!Specializes<Yielding, co_expect>) // co_expect<void, T> is too verbose
  &&
  requires(Future& f, Yielding&& v)
  { { f.on_yield(std::forward<Yielding>(v)) } -> std::same_as<co_control>; }
{
  auto lock = lock_future();
  auto resumer = future().on_yield(std::forward<Yielding>(value));
  return yield_only_awaiter_type<Yielding&&, decltype(resumer)>
  {
    this,
    std::move(resumer)
  };
}

// yield a value, expecting non-input on resume, using a custom resume handler
template<typename Yielding>
auto yield_value(Yielding&& value) requires
  (!Specializes<Yielding, co_expect>)
  &&
  requires(Future& f, Yielding&& v)
  { { f.on_yield(std::forward<Yielding>(v)) } -> Specializes<co_resumer>; }
{
  auto lock = lock_future();
  auto resumer = future().on_yield(std::forward<Yielding>(value));
  return yield_only_awaiter_type<Yielding&&, decltype(resumer)>
  {
    this,
    std::move(resumer)
  };
}

// BEGIN 2-WAY YIELD AWAITER
template<typename Expecting, typename Yielding, typename Resumer>
struct two_way_yield_awaiter_type
{
  basic_promise<Future>* const self;
  Resumer resumer;

  bool is_resuming(co_control control)
  {
    switch (control)
      {
        case co_control::resume:
          return true;
          break;
        case co_control::suspend:
          return false;
          break;
        case co_control::surrender:
        default:
          return false;
          break;
      }
  }

  bool await_ready()
  {
    if constexpr (uses_executor())
    {
      return false;
    }
    else
    {
      return is_resuming(resumer);
    }
  }
  void await_suspend(std::coroutine_handle<> handle)
  {
    auto lock = self->lock_future();
    self->deactivate();
    if (!self->has_future())
    {
      handle.destroy();
      return;
    }
    if constexpr (uses_executor())
    {
      if (is_resuming(resumer))
      {
        self->future().executor(std::move([=]() { handle.resume(); }));
      }
    }
  }
  Expecting await_resume()
  {
    auto lock = self->lock_future();
    if (!self->has_future()) {
      throw std::runtime_error(
        "[Error]@[Coroutine Promise][2-Way Yield Awaiter]: missing future object"
      );
    }
    self->activate();
    return resumer.on_resume();
  }
};
// END 2-WAY YIELD AWAITER

template<typename Expecting, typename Yielding>
auto
yield_value(co_expect<Expecting, Yielding> e) requires
  (!std::is_same_v<Expecting, void> && !std::is_same_v<Yielding, void>) &&
  requires(Future& f, Yielding y)
  {
    // 2-way yielding requires a co_resumer
    { f.on_yield(co_expect<Expecting>::from(y)) } -> Specializes<co_resumer>;
  }
{
  auto lock = lock_future();
  auto resumer = future().on_yield(std::move(co_expect<Expecting>::from(static_cast<Yielding>(e.from))));
  return two_way_yield_awaiter_type<Expecting, Yielding, decltype(resumer)>
  { 
    this,
    std::move(resumer)
  };
}

// BEGIN VOID YIELD AWAITER (0/1-WAY)
template<typename Expecting, typename Resumer>
struct void_yield_awaiter_type
{
  basic_promise<Future>* const self;
  Resumer resumer;

  bool is_resuming(co_control control)
  {
    switch (control)
      {
        case co_control::resume:
          return true;
          break;
        case co_control::suspend:
          return false;
          break;
        case co_control::surrender:
        default:
          return false;
          break;
      }
  }

  bool await_ready()
  {
    if constexpr (uses_executor())
    {
      return false;
    }
    else
    {
      return is_resuming(resumer);
    }
  }
  void await_suspend(std::coroutine_handle<> handle)
  {
    auto lock = self->lock_future();
    self->deactivate();
    if (!self->has_future())
    {
      handle.destroy();
      return;
    }
    if constexpr (uses_executor())
    {
      if (is_resuming(resumer))
      {
        self->future().executor(std::move([=]() { handle.resume(); }));
      }
    }
  }
  Expecting await_resume()
  {
    auto lock = self->lock_future();
    if (!self->has_future()) {
      throw std::runtime_error(
        "[Error][Coroutine Promise][Void Yield Awaiter]: missing future object"
      );
    }
    self->activate();
    if constexpr (Specializes<Resumer, co_resumer>)
      return resumer.on_resume();
    else
    {
      static_assert(std::is_same_v<Expecting, void>, "yield handler expects a value upon resume, implement a resumer");
      return;
    }
  }
};
// END VOID YIELD AWAITER

auto yield_value(co_expect<void, void> exp) requires
  requires(Future& f)
  {
    { f.on_yield() } -> std::same_as<co_control>;
  }
{
  auto lock = lock_future();
  auto resumer = future().on_yield();
  return void_yield_awaiter_type<void, decltype(resumer)>{ this, std::move(resumer) };
}

template<typename Expecting>
auto yield_value(co_expect<Expecting, void> exp) requires
  requires(Future& f)
  {
    { f.on_yield(co_expect<Expecting, void>{}) } -> std::same_as<co_control>;
  }
{
  auto lock = lock_future();
  auto resumer = future().on_yield(co_expect<Expecting>{});
  return void_yield_awaiter_type<Expecting, decltype(resumer)>{ this, std::move(resumer) };
}

// BEGIN TRANSFORMING AWAITER
template<typename Recievable>
static constexpr bool has_await_wrapper()
{
  constexpr bool result = inplace_sfinae
  {
    []<typename>() constexpr { return 0; },
    []<typename U> requires
      requires(U& f)
      {
        { f.on_await(co_expect<Recievable>{}) } -> std::same_as<co_control>;
      }() constexpr { return 1; },
    []<typename U> requires
      requires(U& f)
      {
        { f.on_await(co_expect<Recievable>{}) } -> Specializes<co_resumer>;
      }() constexpr { return 2; }
  }.check(typle<Future>{});
  return result;
}

template<typename Recievable, typename WrappedAwaiter, typename Resumer>
struct transforming_awaiter
{
  basic_promise<Future>* const self;
  WrappedAwaiter wrapped;
  Resumer resumer;

  bool await_ready()
  {
    if(self->awaiting())
      return false;
    if constexpr (has_await_wrapper<Recievable>()) {
      bool is_resuming = wrapped.await_ready(); // what is returned by the awaited object
      switch (co_control{ resumer }) { // what was returned by the `Future::on_await` callable
        case co_control::resume:
          if (!is_resuming)
          {
            // footgun check
            throw std::runtime_error(
              "[Error]@[Coroutine Promise][Transforming Awaiter]: attempting to override-resume a co_awaiting coroutine is dangerous"
              ", only override-suspend is permitted for co_await operations"
            );
          }
          return true;
          break;
        case co_control::suspend:
          return false;
          break;
        case co_control::surrender:
        default:
          return is_resuming;
          break;
      }
    } else {
      return wrapped.await_ready();
    }
  }
  auto await_suspend(std::coroutine_handle<> handle)
  {
    using ST = decltype(wrapped.await_suspend(handle));
    if(self->awaiting())
    {
      // here we have encountered a double await, which should not be an error
      // instead it is a no-op to facilitate the act of waiting for the previously started operation to finish
      if constexpr (Specializes<ST, std::coroutine_handle>)
      {
        return static_cast<std::coroutine_handle<>>(std::noop_coroutine()); // thank you for this helper function!
      }
      else if constexpr (std::convertible_to<ST, bool>)
      {
        return true;
      }
      else
      {
        return;
      }
    }
    else
    {
      // an await operation is semantically different from a yield operation
      // yielding communicates with the caller of the coroutine
      // awaiting communicates with the object being awaited on
      // so when you await something it temporarily takes control away from the coroutine future object
      // and gives it to the awaited object, it is the awaited objects responsibility to resume eventually
      // `std::coroutine_handle`s are copyable but it is dangerous to double-resume from the raw handle
      // use it once and dispose of it
      self->deactivate();
      self->await_value();
      if constexpr (Specializes<ST, std::coroutine_handle>)
      {
        return static_cast<std::coroutine_handle<>>(wrapped.await_suspend(handle));
      }
      else if constexpr (std::convertible_to<ST, bool>)
      {
        return wrapped.await_suspend(handle);
      }
      else
      {
        wrapped.await_suspend(handle);
      }
    }
  }
  decltype(auto) await_resume()
  {
    // this can only be reached by accessing the raw `coroutine_handle`
    // `basic_coroutine::resume` only resumes manually if NOT awaiting a value
    self->recieve_value();
    self->activate();
    if constexpr (!std::is_same_v<Resumer, co_control>)
    {
      if constexpr (std::is_invocable_v<decltype(resumer.on_resume), Recievable>)
      {
        // this is where magic can happen
        // transforming the value and/or type returned to the coroutine depending on what type was expected
        return resumer.on_resume(static_cast<Recievable>(wrapped.await_resume()));
      }
      else
      {
        // this is a better option
        // just adds side effects to the resume portion of the transaction
        resumer.on_resume();
        return wrapped.await_resume();
      }
    }
  }
};
// END TRANSFORMING AWAITER

template<typename U>
auto
await_transform(U&& awaitable) requires GlobalAwaitable<U>
{
  auto&& awaiter = operator co_await(std::forward<U>(awaitable));
  using Recievable = decltype(awaiter.await_resume());
  using Awaiter = decltype(awaiter);
  if constexpr (has_await_wrapper<Recievable>())
  {
    auto resumer = future().on_await(co_expect<Recievable>{});
    return transforming_awaiter<Recievable, Awaiter, decltype(resumer)>{ this, static_cast<Awaiter>(awaiter), std::move(resumer) };
  }
  else
  {
    return transforming_awaiter<Recievable, Awaiter, co_control>{ this, static_cast<Awaiter>(awaiter), co_control::surrender };
  }
}

template<typename U>
auto
await_transform(U&& awaitable) requires LocalAwaitable<U>
{
  auto&& awaiter = awaitable.operator co_await();
  using Recievable = decltype(awaiter.await_resume());
  using Awaiter = decltype(awaiter);
  if constexpr (has_await_wrapper<Recievable>())
  {
    auto resumer = future().on_await(co_expect<Recievable>{});
    return transforming_awaiter<Recievable, Awaiter, decltype(resumer)>{ this, static_cast<Awaiter>(awaiter), std::move(resumer) };
  }
  else
  {
    return transforming_awaiter<Recievable, Awaiter, co_control>{ this, static_cast<Awaiter>(awaiter), co_control::surrender };
  }
}

template<typename U>
auto
await_transform(U&& awaiter) requires BasicAwaiter<U>
{
  using Recievable = decltype(awaiter.await_resume());
  if constexpr (has_await_wrapper<Recievable>())
  {
    auto resumer = future().on_await(co_expect<Recievable>{});
    return transforming_awaiter<Recievable, U&&, decltype(resumer)>{ this, std::forward<U>(awaiter), std::move(resumer) };
  }
  else
  {
    return transforming_awaiter<Recievable, U&&, co_control>{ this, std::forward<U>(awaiter), co_control::surrender };
  }
}
};

}
