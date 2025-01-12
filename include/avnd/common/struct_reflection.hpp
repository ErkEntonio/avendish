#pragma once

/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <avnd/common/coroutines.hpp>
#include <avnd/common/dummy.hpp>
#include <avnd/common/errors.hpp>
#include <avnd/common/index_sequence.hpp>
#include <boost/mp11.hpp>
#include <boost/pfr.hpp>

namespace avnd
{
namespace pfr = ::boost::pfr;

template <std::size_t N>
struct field_index { };

template <std::size_t N>
struct predicate_index { };

template <int N, typename T, T... Idx>
consteval int index_of_element(std::integer_sequence<T, Idx...>) noexcept
{
  static_assert(sizeof...(Idx) > 0);

  constexpr int ret = []
  {
    int k = 0;
    for (int i : {Idx...})
    {
      if (i == N)
        return k;
      k++;
    }
    return -1;
  }();

  static_assert(ret >= 0);
  return ret;
}

template <typename T>
using as_tuple_ref = decltype(pfr::detail::tie_as_tuple(std::declval<T&>()));
template <typename T>
using as_tuple = decltype(pfr::structure_to_tuple(std::declval<T&>()));

// Yields a tuple with the compile-time function applied to each member of the struct
template <template <typename...> typename F, typename T>
using struct_apply = boost::mp11::mp_transform<F, as_tuple<T>>;

// Select a subset of fields and apply an operation on them
template <
    template <typename...>
    typename F,
    template <typename...>
    typename Filter,
    typename T>
using filter_and_apply = boost::mp11::mp_transform<F, typename Filter<T>::fields>;

template <std::size_t Idx, typename Field>
struct field_reflection
{
  using type = Field;
  static const constexpr auto index = Idx;
};

template <class T>
constexpr std::size_t fields_count_unsafe() noexcept
{
  using type = std::remove_cv_t<T>;

  constexpr std::size_t middle = sizeof(type) / 2 + 1;
  return pfr::detail::detect_fields_count<T, 0, middle>(
      pfr::detail::multi_element_range{}, 1L);
}

/**
 * Utilities to introspect all fields in a struct
 */
template <typename T>
struct fields_introspection
{
  using type = T;

  static constexpr auto size = fields_count_unsafe<type>();
  using indices_n = std::make_integer_sequence<int, size>;

  static constexpr void for_all(auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      // C++20 : template lambda
      [&func]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        (func(field_reflection<Index, pfr::tuple_element_t<Index, type>>{}), ...);
      }
      (indices_n{});
    }
  }

  static constexpr void for_nth(int n, auto&& func) noexcept
  {
    // TODO maybe there is some dirty hack to do here with offsetof computations...
    // technically, we ought to be able to compute the address of member "n" at compile time...
    // and reinterpret_cast &fields+addr....

    if constexpr (size > 0)
    {
      [ n, &func ]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        // TODO compare with || logical-or fold ?
        ((void)(Index == n && (func(field_reflection<Index, pfr::tuple_element_t<Index, type>>{}), true)),
         ...);
      }
      (indices_n{});
    }
  }

  static constexpr void for_all(type& fields, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [&func, &fields ]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        auto&& ppl = pfr::detail::tie_as_tuple(fields);
        (func(pfr::detail::sequence_tuple::get<Index>(ppl)), ...);
      }
      (indices_n{});
    }
  }

  static constexpr void for_nth(type& fields, int n, auto&& func) noexcept
  {
    // TODO maybe there is some dirty hack to do here with offsetof computations...
    // technically, we ought to be able to compute the address of member "n" at compile time...
    // and reinterpret_cast &fields+addr....

    if constexpr (size > 0)
    {
      [ n, &func, &fields ]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        auto&& ppl = pfr::detail::tie_as_tuple(fields);
        // TODO compare with || logical-or fold ?
        ((void)(Index == n && (func(pfr::detail::sequence_tuple::get<Index>(ppl)), true)),
         ...);
      }
      (indices_n{});
    }
  }
};

/**
 * Utilities to introspect all fields in a struct which match a given predicate
 */

template <typename T, template <typename...> typename P, typename IntT>
struct matches_predicate : P<pfr::tuple_element_t<IntT::value, T>>
{
};

template <typename T, template <typename...> typename P>
struct predicate_introspection
{
  using type = T;
  using indices
      = typed_index_sequence_t<std::make_index_sequence<pfr::tuple_size_v<type>>>;

  template <typename IntT>
  struct matches_predicate_i : matches_predicate<T, P, IntT>
  {
  };

  using fields = boost::mp11::mp_copy_if<as_tuple<type>, P>;
  using indices_n
      = numbered_index_sequence_t<boost::mp11::mp_copy_if<indices, matches_predicate_i>>;
  static constexpr auto index_map = integer_sequence_to_array(indices_n{});
  static constexpr auto size = indices_n::size();

  template<std::size_t Idx>
  static consteval int map() noexcept {
    return index_map[Idx];
  }
  template<std::size_t Idx>
  static consteval int unmap() noexcept {
    return avnd::index_of_element<Idx>(indices_n{});
  }

  static constexpr void for_all(auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [&func]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        (func(field_reflection<Index, pfr::tuple_element_t<Index, T>>{}), ...);
      }
      (indices_n{});
    }
  }

  // n is in [0; total number of ports[ (even those that don't match the predicate)
  static constexpr void for_nth_raw(int n, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [ n, &func ]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        // TODO compare with || logical-or fold ?
        ((void)(Index == n && (func(field_reflection<Index, pfr::tuple_element_t<Index, T>>{}), true)),
         ...);
      }
      (indices_n{});
    }
  }

  // n is in [0; number of ports matching that predicate[
  static constexpr void for_nth_mapped(int n, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [ k = index_map[n], &
        func ]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        // TODO compare with || logical-or fold ?
        ((void)(Index == k && (func(field_reflection<Index, pfr::tuple_element_t<Index, T>>{}), true)),
         ...);
      }
      (indices_n{});
    }
  }

  // Goes from 0, 1, 2 indices to indices in the complete
  // struct with members that may not match this predicate
  template <std::size_t N>
  using nth_element = std::decay_t<decltype(pfr::get<index_map[N]>(type{}))>;

  template <std::size_t N>
  static constexpr auto get(type& unfiltered_fields) noexcept -> decltype(auto)
  {
    return pfr::get<index_map[N]>(unfiltered_fields);
  }

  // Gives std::tuple<field1&, field2&, etc...>
  static constexpr auto tie(type& unfiltered_fields)
  {
    return [&]<typename K, K... Index>(std::integer_sequence<K, Index...>)
    {
      return std::tie(pfr::get<Index>(unfiltered_fields)...);
    }
    (indices_n{});
  }

  // Gives std::tuple<field1, field2, etc...>
  static constexpr auto make_tuple(type& unfiltered_fields)
  {
    return [&]<typename K, K... Index>(std::integer_sequence<K, Index...>)
    {
      return std::make_tuple(pfr::get<Index>(unfiltered_fields)...);
    }
    (indices_n{});
  }

  // Gives std::tuple<f(field1), f(field2), etc...>
  static constexpr auto filter_tuple(type& unfiltered_fields, auto filter)
  {
    return [&]<typename K, K... Index>(std::integer_sequence<K, Index...>)
    {
      return std::make_tuple(filter(pfr::get<Index>(unfiltered_fields))...);
    }
    (indices_n{});
  }

  static constexpr void for_all(type& unfiltered_fields, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [&func, &
       unfiltered_fields ]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        auto&& ppl = pfr::detail::tie_as_tuple(unfiltered_fields);
        (func(pfr::detail::sequence_tuple::get<Index>(ppl)), ...);
      }
      (indices_n{});
    }
  }

  template <typename U>
  static constexpr void
  for_all(member_iterator<U>&& unfiltered_fields, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [&func, &
       unfiltered_fields ]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        for (auto& m : unfiltered_fields)
        {
          auto&& ppl = pfr::detail::tie_as_tuple(m);
          (func(pfr::detail::sequence_tuple::get<Index>(ppl)), ...);
        }
      }
      (indices_n{});
    }
  }

  template <typename U>
  static constexpr void
  for_all(member_iterator<U>& unfiltered_fields, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [&func, &
       unfiltered_fields ]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        for (auto& m : unfiltered_fields)
        {
          auto&& ppl = pfr::detail::tie_as_tuple(m);
          (func(pfr::detail::sequence_tuple::get<Index>(ppl)), ...);
        }
      }
      (indices_n{});
    }
  }

  // Same as for_all but also passes the predicate-based index (0, 1, 2) as template argument
  static constexpr void for_all_n(type& unfiltered_fields, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [&func, &unfiltered_fields ]<typename K, K... Index, size_t... LocalIndex>(
          std::integer_sequence<K, Index...>,
          std::integer_sequence<size_t, LocalIndex...>)
      {
        auto&& ppl = pfr::detail::tie_as_tuple(unfiltered_fields);
        (func(
             pfr::detail::sequence_tuple::get<Index>(ppl),
             avnd::predicate_index<LocalIndex>{}),
         ...);
      }
      (indices_n{}, std::make_index_sequence<size>{});
    }
  }

  // Same as for_all_n but also passes the struct-based index (2, 7, 12) as template argument
  static constexpr void for_all_n2(type& unfiltered_fields, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [&func, &unfiltered_fields ]<typename K, K... Index, size_t... LocalIndex>(
          std::integer_sequence<K, Index...>,
          std::integer_sequence<size_t, LocalIndex...>)
      {
        auto&& ppl = pfr::detail::tie_as_tuple(unfiltered_fields);
        (func(
             pfr::detail::sequence_tuple::get<Index>(ppl),
             avnd::predicate_index<LocalIndex>{},
             avnd::field_index<Index>{}),
         ...);
      }
      (indices_n{}, std::make_index_sequence<size>{});
    }
  }

  template <typename U>
  static constexpr void
  for_all_n(member_iterator<U> unfiltered_fields, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [&func, &
       unfiltered_fields ]<typename K, K... Index, size_t... LocalIndex>(
                    std::integer_sequence<K, Index...>,
                    std::integer_sequence<size_t, LocalIndex...>)
      {
        for (auto& m : unfiltered_fields)
        {
          auto&& ppl = pfr::detail::tie_as_tuple(m);
          (func(
               pfr::detail::sequence_tuple::get<Index>(ppl),
               avnd::predicate_index<LocalIndex>{}),
           ...);
        }
      }
      (indices_n{}, std::make_index_sequence<size>{});
    }
  }

  // Will stop if an error is encountered (func should return bool)
  static constexpr bool for_all_unless(type& unfiltered_fields, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      return [&func, &unfiltered_fields ]<typename K, K... Index>(
          std::integer_sequence<K, Index...>)
      {
        auto&& ppl = pfr::detail::tie_as_tuple(unfiltered_fields);
        return (func(pfr::detail::sequence_tuple::get<Index>(ppl)) && ...);
      }
      (indices_n{});
    }
    else
    {
      return true;
    }
  }

  template <typename U>
  static constexpr bool
  for_all_unless(member_iterator<U> unfiltered_fields, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      AVND_ERROR(U, "Cannot use for_all_unless when there are multiple instances");
      return false;
    }
    else
    {
      return true;
    }
  }

  static constexpr void for_nth_raw(type& fields, int n, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [ n, &func, &fields ]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        auto&& ppl = pfr::detail::tie_as_tuple(fields);
        ((void)(Index == n && (func(pfr::detail::sequence_tuple::get<Index>(ppl)), true)),
         ...);
      }
      (indices_n{});
    }
  }

  static constexpr void for_nth_mapped(type& fields, int n, auto&& func) noexcept
  {
    if constexpr (size > 0)
    {
      [ k = index_map[n], &func, &
        fields ]<typename K, K... Index>(std::integer_sequence<K, Index...>)
      {
        auto&& ppl = pfr::detail::tie_as_tuple(fields);
        ((void)(Index == k && (func(pfr::detail::sequence_tuple::get<Index>(ppl)), true)),
         ...);
      }
      (indices_n{});
    }
  }
};

template <>
struct fields_introspection<avnd::dummy>
{
  using type = avnd::dummy;
  using fields = std::tuple<>;
  static constexpr auto index_map = std::array<int, 0>{};
  static constexpr auto size = 0;

  static constexpr std::tuple<> tie(auto&& unfiltered_fields) { return {}; }
  static constexpr std::tuple<> make_tuple(auto&& unfiltered_fields) { return {}; }

  static constexpr void for_all(auto&& func) noexcept { }
  static constexpr void for_all_n(auto&& func) noexcept { }
  static constexpr void for_nth(int n, auto&& func) noexcept { }
  static constexpr void for_all_unless(auto&& func) noexcept { }
  static constexpr void for_all(avnd::dummy fields, auto&& func) noexcept { }
  static constexpr void for_nth(avnd::dummy fields, int n, auto&& func) noexcept { }
  static constexpr void for_all_unless(avnd::dummy fields, auto&& func) noexcept { }
};

template <template <typename...> typename P>
struct predicate_introspection<avnd::dummy, P>
{
  using type = avnd::dummy;
  using fields = std::tuple<>;
  static constexpr auto index_map = std::array<int, 0>{};
  static constexpr auto size = 0;

  template <std::size_t N>
  using nth_element = void;

  template <std::size_t N>
  static constexpr void get(avnd::dummy unfiltered_fields) noexcept
  {
  }

  static constexpr std::tuple<> tie(auto&& unfiltered_fields) { return {}; }
  static constexpr std::tuple<> make_tuple(auto&& unfiltered_fields) { return {}; }

  static constexpr void for_all(auto&& func) noexcept { }
  static constexpr void for_all_n(auto&& func) noexcept { }
  static constexpr void for_nth_raw(int n, auto&& func) noexcept { }
  static constexpr void for_nth_mapped(int n, auto&& func) noexcept { }
  static constexpr void for_all_unless(auto&& func) noexcept { }
  static constexpr void for_all(avnd::dummy fields, auto&& func) noexcept { }
  static constexpr void for_nth_raw(avnd::dummy fields, int n, auto&& func) noexcept { }
  static constexpr void for_nth_mapped(avnd::dummy fields, int n, auto&& func) noexcept
  {
  }
  static constexpr void for_all_unless(avnd::dummy fields, auto&& func) noexcept { }
};

}
