#pragma once


#if __cplusplus > 199711L


#include <type_traits>
#include <utility>
#include <thrust/tuple.h>
#include <thrust/functional.h>


namespace thrust
{
namespace experimental
{
namespace detail
{
namespace bind_detail
{


template<class T>
using decay_t = typename std::decay<T>::type;


template<class _Tp, _Tp... _Ip>
struct integer_sequence
{
  typedef _Tp value_type;
  static_assert(std::is_integral<_Tp>::value,
                "std::integer_sequence can only be instantiated with an integral type" );
  static constexpr size_t size() noexcept { return sizeof...(_Ip); }
};


template<size_t... _Ip>
using index_sequence = integer_sequence<size_t, _Ip...>;


template <class _Tp, _Tp _Sp, _Tp _Ep, class _IntSequence>
struct make_integer_sequence_impl_unchecked;


template <class _Tp, _Tp _Sp, _Tp _Ep, _Tp ..._Indices>
struct make_integer_sequence_impl_unchecked<_Tp, _Sp, _Ep,
                                            integer_sequence<_Tp, _Indices...>>
{
  typedef typename make_integer_sequence_impl_unchecked
                   <
                      _Tp, _Sp+1, _Ep,
                      integer_sequence<_Tp, _Indices..., _Sp>
                   >::type type;
};


template <class _Tp, _Tp _Ep, _Tp ..._Indices>
struct make_integer_sequence_impl_unchecked<_Tp, _Ep, _Ep,
                                            integer_sequence<_Tp, _Indices...>>
{
  typedef integer_sequence<_Tp, _Indices...> type;
};


template <class _Tp, _Tp _Ep>
struct make_integer_sequence_impl
{
  static_assert(std::is_integral<_Tp>::value,
                "std::make_integer_sequence can only be instantiated with an integral type" );
  static_assert(0 <= _Ep, "std::make_integer_sequence input shall not be negative");
  typedef typename make_integer_sequence_impl_unchecked
                   <
                      _Tp, 0, _Ep, integer_sequence<_Tp>
                   >::type type;
};


template<class _Tp, _Tp _Np>
using make_integer_sequence = typename make_integer_sequence_impl<_Tp, _Np>::type;


template<size_t _Np>
using make_index_sequence = make_integer_sequence<size_t, _Np>;


template<class... _Tp>
using index_sequence_for = make_index_sequence<sizeof...(_Tp)>;


template<typename F, typename Tuple, size_t... I>
__host__ __device__
auto apply_impl(F&& f, Tuple&& t, index_sequence<I...>)
  -> decltype(
       std::forward<F>(f)(
         thrust::get<I>(std::forward<Tuple>(t))...
       )
     )
{
  return std::forward<F>(f)(
    thrust::get<I>(std::forward<Tuple>(t))...
  );
}


template<typename F, typename Tuple>
__host__ __device__
auto apply(F&& f, Tuple&& t)
  -> decltype(
       apply_impl(
         std::forward<F>(f),
         std::forward<Tuple>(t),
         make_index_sequence<thrust::tuple_size<decay_t<Tuple>>::value>()
       )
     )
{
  using Indices = make_index_sequence<thrust::tuple_size<decay_t<Tuple>>::value>;
  return apply_impl(
    std::forward<F>(f),
    std::forward<Tuple>(t),
    Indices()
  );
}


// XXX should use enable_if is_placeholder
template<class ArgTuple, class BoundArg>
__host__ __device__
auto substitute_arg(ArgTuple&, BoundArg&& bound_arg)
  -> decltype(
       std::forward<BoundArg>(bound_arg)
     )
{
  return std::forward<BoundArg>(bound_arg);
}


// XXX should use enable_if is_placeholder
template<class ArgTuple, size_t i>
__host__ __device__
auto substitute_arg(ArgTuple&& arg_tuple, const typename thrust::detail::functional::placeholder<i>::type&)
  -> decltype(
       thrust::get<i>(std::forward<ArgTuple>(arg_tuple))
     )
{
  return thrust::get<i>(std::forward<ArgTuple>(arg_tuple));
}


template<class ArgTuple, class BoundArgTuple, size_t... I>
__host__ __device__
auto substitute_impl(ArgTuple& arg_tuple, BoundArgTuple& bound_arg_tuple, index_sequence<I...>)
  -> decltype(
       thrust::tie(
         substitute_arg(
           arg_tuple, thrust::get<I>(bound_arg_tuple)
         )...
       )
     )
{
  return thrust::tie(
    substitute_arg(
      arg_tuple, thrust::get<I>(bound_arg_tuple)
    )...
  );
}


template<class ArgTuple, class BoundArgTuple>
__host__ __device__
auto substitute(ArgTuple& arg_tuple, BoundArgTuple& bound_arg_tuple)
  -> decltype(
       substitute_impl(
         arg_tuple,
         bound_arg_tuple,
         make_index_sequence<thrust::tuple_size<decay_t<BoundArgTuple>>::value>()
       )
     )
{
  using Indices = make_index_sequence<thrust::tuple_size<decay_t<BoundArgTuple>>::value>;
  return substitute_impl(arg_tuple, bound_arg_tuple, Indices());
}


template<class F, class... BoundArgs>
class bind_expression
{
  public:
    __host__ __device__
    bind_expression(const F& f, const BoundArgs&... bound_args)
      : fun_(f),
        bound_args_(bound_args...)
    {}

    // XXX WAR nvcc 6.5 issue
    __thrust_hd_warning_disable__
    template<int = 0>
    __host__ __device__
    typename std::result_of<F()>::type operator()()
    {
      return fun_();
    }


    __thrust_hd_warning_disable__
    template<class... OtherArgs>
    __host__ __device__
    auto operator()(OtherArgs&&... args)
      -> decltype(
           apply(
             // XXX WAR nvcc 6.5 issue
             //fun_,
             *std::declval<F*>(),
             substitute(
               thrust::tie(std::forward<OtherArgs>(args)...),
               // XXX WAR nvcc 6.5 issue
               //bound_args_
               *std::declval<thrust::tuple<BoundArgs...>*>()
             )
           )
         )
    {
      return apply(
        fun_,
        substitute(
          thrust::tie(std::forward<OtherArgs>(args)...),
          bound_args_
        )
      );
    }


    __thrust_hd_warning_disable__
    template<class... OtherArgs>
    __host__ __device__
    auto operator()(OtherArgs&&... args) const
      -> decltype(
           apply(
             // XXX WAR nvcc 6.5 issue
             //fun_,
             *std::declval<const F*>(),
             substitute(
               thrust::tie(std::forward<OtherArgs>(args)...),
               // XXX WAR nvcc 6.5 issue
               //bound_args_
               *std::declval<const thrust::tuple<BoundArgs...>*>()
             )
           )
         )
    {
      return apply(
        fun_,
        substitute(
          thrust::tie(std::forward<OtherArgs>(args)...),
          bound_args_
        )
      );
    }

  private:
    F fun_;
    thrust::tuple<BoundArgs...> bound_args_;
};


} // end bind_detail
} // end detail


template<class F, class... BoundArgs>
__host__ __device__
detail::bind_detail::bind_expression<
  detail::bind_detail::decay_t<F>,
  detail::bind_detail::decay_t<BoundArgs>...
> bind(F&& f, BoundArgs&&... bound_args)
{
  using namespace thrust::experimental::detail::bind_detail;
  return bind_expression<decay_t<F>,decay_t<BoundArgs>...>(std::forward<F>(f), std::forward<BoundArgs>(bound_args)...);
}


} // end experimental
} // end thrust

#endif // __cplusplus

