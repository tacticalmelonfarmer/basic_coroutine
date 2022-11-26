#pragma once

#include <utility>

namespace tmf
{

template<typename>
struct basic_promise;

template<typename>
struct basic_coroutine;

enum class co_control
{
  suspend,
  resume,
  surrender // this will assume reasonable default behaviours when used
};

template<typename F>
struct co_resumer
{
  co_control control;
  F on_resume;
  
  co_resumer<F>& operator=(co_control init)
  {
    control = init;
    return *this;
  }
  
  operator co_control()
  {
    return control;
  }
};

template<typename F>
co_resumer<F> operator>> (co_control control, F&& then)
{
  return { control, std::forward<F>(then) };
}

}