// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

#ifndef MFEM_BACKENDS_RAJA_BILIN_GEOM_HPP
#define MFEM_BACKENDS_RAJA_BILIN_GEOM_HPP

#include "../../../config/config.hpp"
#if defined(MFEM_USE_BACKENDS) && defined(MFEM_USE_RAJA)

namespace mfem
{

namespace raja
{

// ***************************************************************************
// * RajaGeometry
// ***************************************************************************
class RajaGeometry
{
public:
   ~RajaGeometry();
   raja::array<int> eMap;
   raja::array<double> meshNodes;
   raja::array<double> J, invJ, detJ;
   static const int Jacobian    = (1 << 0);
   static const int JacobianInv = (1 << 1);
   static const int JacobianDet = (1 << 2);
   static RajaGeometry* Get(RajaFiniteElementSpace&,
                            const IntegrationRule&);
   static RajaGeometry* Get(RajaFiniteElementSpace&,
                            const IntegrationRule&,
                            const RajaVector&);
   static void ReorderByVDim(GridFunction& nodes);
   static void ReorderByNodes(GridFunction& nodes);
};


} // namespace mfem::raja

} // namespace mfem

#endif // defined(MFEM_USE_BACKENDS) && defined(MFEM_USE_RAJA)

#endif // MFEM_BACKENDS_RAJA_BILIN_GEOM_HPP
