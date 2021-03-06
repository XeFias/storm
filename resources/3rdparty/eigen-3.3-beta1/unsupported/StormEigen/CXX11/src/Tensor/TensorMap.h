// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2014 Benoit Steiner <benoit.steiner.goog@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef STORMEIGEN_CXX11_TENSOR_TENSOR_MAP_H
#define STORMEIGEN_CXX11_TENSOR_TENSOR_MAP_H

namespace StormEigen {

/** \class TensorMap
  * \ingroup CXX11_Tensor_Module
  *
  * \brief A tensor expression mapping an existing array of data.
  *
  */

template<typename PlainObjectType, int Options_> class TensorMap : public TensorBase<TensorMap<PlainObjectType, Options_> >
{
  public:
    typedef TensorMap<PlainObjectType, Options_> Self;
    typedef typename PlainObjectType::Base Base;
    typedef typename StormEigen::internal::nested<Self>::type Nested;
    typedef typename internal::traits<PlainObjectType>::StorageKind StorageKind;
    typedef typename internal::traits<PlainObjectType>::Index Index;
    typedef typename internal::traits<PlainObjectType>::Scalar Scalar;
    typedef typename internal::packet_traits<Scalar>::type Packet;
    typedef typename NumTraits<Scalar>::Real RealScalar;
    typedef typename Base::CoeffReturnType CoeffReturnType;

  /*    typedef typename internal::conditional<
                         bool(internal::is_lvalue<PlainObjectType>::value),
                         Scalar *,
                         const Scalar *>::type
                     PointerType;*/
    typedef Scalar* PointerType;
    typedef PointerType PointerArgType;

    static const int Options = Options_;

    static const Index NumIndices = PlainObjectType::NumIndices;
    typedef typename PlainObjectType::Dimensions Dimensions;

    enum {
      IsAligned = ((int(Options_)&Aligned)==Aligned),
      PacketAccess = (internal::packet_traits<Scalar>::size > 1),
      Layout = PlainObjectType::Layout,
      CoordAccess = true
    };

    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE TensorMap(PointerArgType dataPtr) : m_data(dataPtr), m_dimensions() {
      // The number of dimensions used to construct a tensor must be equal to the rank of the tensor.
      STORMEIGEN_STATIC_ASSERT((0 == NumIndices || NumIndices == Dynamic), YOU_MADE_A_PROGRAMMING_MISTAKE)
    }

#ifdef STORMEIGEN_HAS_VARIADIC_TEMPLATES
    template<typename... IndexTypes> STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE TensorMap(PointerArgType dataPtr, Index firstDimension, IndexTypes... otherDimensions) : m_data(dataPtr), m_dimensions(firstDimension, otherDimensions...) {
      // The number of dimensions used to construct a tensor must be equal to the rank of the tensor.
      STORMEIGEN_STATIC_ASSERT((sizeof...(otherDimensions) + 1 == NumIndices || NumIndices == Dynamic), YOU_MADE_A_PROGRAMMING_MISTAKE)
    }
#else
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE TensorMap(PointerArgType dataPtr, Index firstDimension) : m_data(dataPtr), m_dimensions(firstDimension) {
      // The number of dimensions used to construct a tensor must be equal to the rank of the tensor.
      STORMEIGEN_STATIC_ASSERT((1 == NumIndices || NumIndices == Dynamic), YOU_MADE_A_PROGRAMMING_MISTAKE)
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE TensorMap(PointerArgType dataPtr, Index dim1, Index dim2) : m_data(dataPtr), m_dimensions(dim1, dim2) {
      STORMEIGEN_STATIC_ASSERT(2 == NumIndices || NumIndices == Dynamic, YOU_MADE_A_PROGRAMMING_MISTAKE)
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE TensorMap(PointerArgType dataPtr, Index dim1, Index dim2, Index dim3) : m_data(dataPtr), m_dimensions(dim1, dim2, dim3) {
      STORMEIGEN_STATIC_ASSERT(3 == NumIndices || NumIndices == Dynamic, YOU_MADE_A_PROGRAMMING_MISTAKE)
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE TensorMap(PointerArgType dataPtr, Index dim1, Index dim2, Index dim3, Index dim4) : m_data(dataPtr), m_dimensions(dim1, dim2, dim3, dim4) {
      STORMEIGEN_STATIC_ASSERT(4 == NumIndices || NumIndices == Dynamic, YOU_MADE_A_PROGRAMMING_MISTAKE)
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE TensorMap(PointerArgType dataPtr, Index dim1, Index dim2, Index dim3, Index dim4, Index dim5) : m_data(dataPtr), m_dimensions(dim1, dim2, dim3, dim4, dim5) {
      STORMEIGEN_STATIC_ASSERT(5 == NumIndices || NumIndices == Dynamic, YOU_MADE_A_PROGRAMMING_MISTAKE)
    }
#endif

   STORMEIGEN_DEVICE_FUNC STORMEIGEN_STRONG_INLINE TensorMap(PointerArgType dataPtr, const array<Index, NumIndices>& dimensions)
      : m_data(dataPtr), m_dimensions(dimensions)
    { }

    template <typename Dimensions>
    STORMEIGEN_DEVICE_FUNC STORMEIGEN_STRONG_INLINE TensorMap(PointerArgType dataPtr, const Dimensions& dimensions)
      : m_data(dataPtr), m_dimensions(dimensions)
    { }

    STORMEIGEN_DEVICE_FUNC STORMEIGEN_STRONG_INLINE TensorMap(PlainObjectType& tensor)
      : m_data(tensor.data()), m_dimensions(tensor.dimensions())
    { }

    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Index rank() const { return m_dimensions.rank(); }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Index dimension(Index n) const { return m_dimensions[n]; }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE const Dimensions& dimensions() const { return m_dimensions; }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Index size() const { return m_dimensions.TotalSize(); }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Scalar* data() { return m_data; }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE const Scalar* data() const { return m_data; }

    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE const Scalar& operator()(const array<Index, NumIndices>& indices) const
    {
      //      eigen_assert(checkIndexRange(indices));
      if (PlainObjectType::Options&RowMajor) {
        const Index index = m_dimensions.IndexOfRowMajor(indices);
        return m_data[index];
      } else {
        const Index index = m_dimensions.IndexOfColMajor(indices);
        return m_data[index];
      }
    }

    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE const Scalar& operator()() const
    {
      STORMEIGEN_STATIC_ASSERT(NumIndices == 0, YOU_MADE_A_PROGRAMMING_MISTAKE)
      return m_data[0];
    }

#ifdef STORMEIGEN_HAS_VARIADIC_TEMPLATES
    template<typename... IndexTypes> STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE const Scalar& operator()(Index firstIndex, IndexTypes... otherIndices) const
    {
      STORMEIGEN_STATIC_ASSERT(sizeof...(otherIndices) + 1 == NumIndices, YOU_MADE_A_PROGRAMMING_MISTAKE)
      if (PlainObjectType::Options&RowMajor) {
        const Index index = m_dimensions.IndexOfRowMajor(array<Index, NumIndices>{{firstIndex, otherIndices...}});
        return m_data[index];
      } else {
        const Index index = m_dimensions.IndexOfColMajor(array<Index, NumIndices>{{firstIndex, otherIndices...}});
        return m_data[index];
      }
    }
#else
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE const Scalar& operator()(Index index) const
    {
      eigen_internal_assert(index >= 0 && index < size());
      return m_data[index];
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE const Scalar& operator()(Index i0, Index i1) const
    {
      if (PlainObjectType::Options&RowMajor) {
        const Index index = i1 + i0 * m_dimensions[1];
        return m_data[index];
      } else {
        const Index index = i0 + i1 * m_dimensions[0];
        return m_data[index];
      }
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE const Scalar& operator()(Index i0, Index i1, Index i2) const
    {
      if (PlainObjectType::Options&RowMajor) {
         const Index index = i2 + m_dimensions[2] * (i1 + m_dimensions[1] * i0);
         return m_data[index];
      } else {
         const Index index = i0 + m_dimensions[0] * (i1 + m_dimensions[1] * i2);
        return m_data[index];
      }
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE const Scalar& operator()(Index i0, Index i1, Index i2, Index i3) const
    {
      if (PlainObjectType::Options&RowMajor) {
        const Index index = i3 + m_dimensions[3] * (i2 + m_dimensions[2] * (i1 + m_dimensions[1] * i0));
        return m_data[index];
      } else {
        const Index index = i0 + m_dimensions[0] * (i1 + m_dimensions[1] * (i2 + m_dimensions[2] * i3));
        return m_data[index];
      }
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE const Scalar& operator()(Index i0, Index i1, Index i2, Index i3, Index i4) const
    {
      if (PlainObjectType::Options&RowMajor) {
        const Index index = i4 + m_dimensions[4] * (i3 + m_dimensions[3] * (i2 + m_dimensions[2] * (i1 + m_dimensions[1] * i0)));
        return m_data[index];
      } else {
        const Index index = i0 + m_dimensions[0] * (i1 + m_dimensions[1] * (i2 + m_dimensions[2] * (i3 + m_dimensions[3] * i4)));
        return m_data[index];
      }
    }
#endif

    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Scalar& operator()(const array<Index, NumIndices>& indices)
    {
      //      eigen_assert(checkIndexRange(indices));
      if (PlainObjectType::Options&RowMajor) {
        const Index index = m_dimensions.IndexOfRowMajor(indices);
        return m_data[index];
      } else {
        const Index index = m_dimensions.IndexOfColMajor(indices);
        return m_data[index];
      }
    }

    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Scalar& operator()()
    {
      STORMEIGEN_STATIC_ASSERT(NumIndices == 0, YOU_MADE_A_PROGRAMMING_MISTAKE)
      return m_data[0];
    }

#ifdef STORMEIGEN_HAS_VARIADIC_TEMPLATES
    template<typename... IndexTypes> STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Scalar& operator()(Index firstIndex, IndexTypes... otherIndices)
    {
      static_assert(sizeof...(otherIndices) + 1 == NumIndices || NumIndices == Dynamic, "Number of indices used to access a tensor coefficient must be equal to the rank of the tensor.");
      const std::size_t NumDims = sizeof...(otherIndices) + 1;
      if (PlainObjectType::Options&RowMajor) {
        const Index index = m_dimensions.IndexOfRowMajor(array<Index, NumDims>{{firstIndex, otherIndices...}});
        return m_data[index];
      } else {
        const Index index = m_dimensions.IndexOfColMajor(array<Index, NumDims>{{firstIndex, otherIndices...}});
        return m_data[index];
      }
    }
#else
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Scalar& operator()(Index index)
    {
      eigen_internal_assert(index >= 0 && index < size());
      return m_data[index];
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Scalar& operator()(Index i0, Index i1)
    {
       if (PlainObjectType::Options&RowMajor) {
         const Index index = i1 + i0 * m_dimensions[1];
        return m_data[index];
      } else {
        const Index index = i0 + i1 * m_dimensions[0];
        return m_data[index];
      }
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Scalar& operator()(Index i0, Index i1, Index i2)
    {
       if (PlainObjectType::Options&RowMajor) {
         const Index index = i2 + m_dimensions[2] * (i1 + m_dimensions[1] * i0);
        return m_data[index];
      } else {
         const Index index = i0 + m_dimensions[0] * (i1 + m_dimensions[1] * i2);
        return m_data[index];
      }
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Scalar& operator()(Index i0, Index i1, Index i2, Index i3)
    {
      if (PlainObjectType::Options&RowMajor) {
        const Index index = i3 + m_dimensions[3] * (i2 + m_dimensions[2] * (i1 + m_dimensions[1] * i0));
        return m_data[index];
      } else {
        const Index index = i0 + m_dimensions[0] * (i1 + m_dimensions[1] * (i2 + m_dimensions[2] * i3));
        return m_data[index];
      }
    }
    STORMEIGEN_DEVICE_FUNC
    STORMEIGEN_STRONG_INLINE Scalar& operator()(Index i0, Index i1, Index i2, Index i3, Index i4)
    {
      if (PlainObjectType::Options&RowMajor) {
        const Index index = i4 + m_dimensions[4] * (i3 + m_dimensions[3] * (i2 + m_dimensions[2] * (i1 + m_dimensions[1] * i0)));
        return m_data[index];
      } else {
        const Index index = i0 + m_dimensions[0] * (i1 + m_dimensions[1] * (i2 + m_dimensions[2] * (i3 + m_dimensions[3] * i4)));
        return m_data[index];
      }
    }
#endif

    STORMEIGEN_DEVICE_FUNC STORMEIGEN_STRONG_INLINE Self& operator=(const Self& other)
    {
      typedef TensorAssignOp<Self, const Self> Assign;
      Assign assign(*this, other);
      internal::TensorExecutor<const Assign, DefaultDevice>::run(assign, DefaultDevice());
      return *this;
    }

    template<typename OtherDerived>
    STORMEIGEN_DEVICE_FUNC STORMEIGEN_STRONG_INLINE
    Self& operator=(const OtherDerived& other)
    {
      typedef TensorAssignOp<Self, const OtherDerived> Assign;
      Assign assign(*this, other);
      internal::TensorExecutor<const Assign, DefaultDevice>::run(assign, DefaultDevice());
      return *this;
    }

  private:
    Scalar* m_data;
    Dimensions m_dimensions;
};

} // end namespace StormEigen

#endif // STORMEIGEN_CXX11_TENSOR_TENSOR_MAP_H
