#pragma once

#include <fwd.hpp>

#include <concepts>
#include <coroutine>

namespace tmf
{

template<typename T, typename... Us>
concept ToOneOf = (... || std::convertible_to<T, Us>);

template<typename T, typename... Us>
concept IsOneOf = (... || std::same_as<T, Us>);

template<typename T>
concept GlobalAwaitable = requires(T v)
{
  operator co_await(v);
};

template<typename T>
concept LocalAwaitable = requires(T v)
{
  v.operator co_await();
};

template<typename T>
concept BasicAwaitable = GlobalAwaitable<T> || LocalAwaitable<T>;

template<typename T>
concept BasicAwaiter = requires(T v)
{
  { v.await_ready() } -> std::convertible_to<bool>;
  { v.await_suspend(std::coroutine_handle<>{}) } -> ToOneOf<void, bool, std::coroutine_handle<>>;
  v.await_resume();
};

template<typename T, template <typename...> typename Of>
struct is_specialization_of
{
  constexpr static bool value = false;
};

template<template <typename...> typename Of, typename... Us>
struct is_specialization_of<Of<Us...>, Of>
{
  constexpr static bool value = true;
};

template<typename T, template <typename...> typename Of>
concept Specializes = is_specialization_of<T, Of>::value;

template<typename T>
concept VoidReturningFuture = requires(T& f)
{
  f.on_return();
};

}