#pragma once

#include <concepts.hpp>

#include <cstddef>
#include <utility>

namespace tmf::inline details
{

template<typename... Types>
struct typle
{
  using no_type = void;
};

template<typename Type>
struct typle<Type>
{
  using type = Type;
};

template<typename... Ls>
struct inplace_sfinae : Ls...
{
  using Ls::operator()...;
  template<typename... Types>
  constexpr auto check(typle<Types...>) const
  {
    return this->template operator()<Types...>();
  }
};

template<std::size_t>
struct unknown
{
  template<typename T>
  operator T() const;
};

#ifdef BASIC_COROUTINE_INTERFACE_CONSTRAINTS

// clang-format off

#define DETECT_MEMBER_FUNCTION(of_type, member_id)                                            \
  []<typename T_>() constexpr                                                                 \
  {                                                                                           \
    using std::index_sequence, std::make_index_sequence, std::size_t;                         \
    using tmf::details::inplace_sfinae, tmf::details::unknown;                                \
    constexpr auto check_ =                                                                   \
      tmf::inplace_sfinae                                                                     \
      {                                                                                       \
        []<typename U_, size_t... J> requires requires(U_ v){ v.member_id(unknown<J>{}...); } \
        (index_sequence<J...>) constexpr                                                      \
        {                                                                                     \
          return true;                                                                        \
        },                                                                                    \
        []<typename>                                                                          \
        (auto) constexpr                                                                      \
        {                                                                                     \
          return false;                                                                       \
        }                                                                                     \
      };                                                                                      \
    constexpr auto loop_ = [=]<size_t... I>(index_sequence<I...>) constexpr                   \
    {                                                                                         \
      return (... || check_.template operator()<T_>(make_index_sequence<I>{}));               \
    };                                                                                        \
    return loop_(make_index_sequence<32>{}); /*doubt you need more than 32*/                  \
  }.template operator()<of_type>()


#else

#define DETECT_MEMBER_FUNCTION(...) false

#endif

}
