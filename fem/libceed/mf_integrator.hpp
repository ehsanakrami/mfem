// Copyright (c) 2010-2020, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#ifndef MFEM_LIBCEED_MFINTEG
#define MFEM_LIBCEED_MFINTEG

#include "operator.hpp"
#include "../../config/config.hpp"
#include "coefficient.hpp"
#include "../fespace.hpp"
#include "../gridfunc.hpp"

namespace mfem
{

namespace ceed
{

#ifdef MFEM_USE_CEED
/** This structure contains the data to assemble a matrix-free operator with
    libCEED. See libceed/mass.cpp or libceed/diffusion.cpp for examples. */
struct MFOperator
{
   /** The finite element space for the trial and test functions. */
   const mfem::FiniteElementSpace &fes;
   /** The Integration Rule to use to compote the operator. */
   const mfem::IntegrationRule &ir;
   /** The path to the header containing the functions for libCEED. */
   std::string header;
   /** The name of the Qfunction to apply the operator. */
   std::string apply_func;
   /** The Qfunction to apply the operator. */
   CeedQFunctionUser apply_qf;
   /** The evaluation mode to apply to the trial function (CEED_EVAL_INTERP,
       CEED_EVAL_GRAD, etc.) */
   EvalMode trial_op;
   /** The evaluation mode to apply to the test function ( CEED_EVAL_INTERP,
       CEED_EVAL_GRAD, etc.) */
   EvalMode test_op;
};
#endif

/** This class represent a matrix-free operator using libCEED.*/
class MFIntegrator : public Operator
{
#ifdef MFEM_USE_CEED
protected:
   CeedBasis basis, mesh_basis;
   CeedElemRestriction restr, mesh_restr, restr_i;
   CeedQFunction apply_qfunc;
   CeedVector node_coords, qdata;
   Coefficient *coeff;
   CeedQFunctionContext build_ctx;

public:
   MFIntegrator()
      : Operator(), basis(nullptr), mesh_basis(nullptr),
        restr(nullptr), mesh_restr(nullptr),
        restr_i(nullptr),
        apply_qfunc(nullptr), node_coords(nullptr),
        qdata(nullptr), coeff(nullptr), build_ctx(nullptr) { }


   /** This method initializes the MFIntegrator with the given CeedOperatorInfo
       @a info, an mfem::FiniteElementSpace @a fes, an mfem::IntegrationRule
       @a ir, and mfem::Coefficient or mfem::VectorCoefficient @a Q.*/
   template <typename CeedOperatorInfo, typename CoeffType>
   MFOperator InitMF(CeedOperatorInfo &info,
                     const mfem::FiniteElementSpace &fes,
                     const mfem::IntegrationRule &irm,
                     CoeffType *Q)
   {
      Mesh &mesh = *fes.GetMesh();
      InitCoefficient(Q, mesh, irm, coeff, info.ctx);
      bool const_coeff = coeff->IsConstant();
      std::string apply_func = const_coeff ? info.apply_func_mf_const
                               : info.apply_func_mf_quad;
      CeedQFunctionUser apply_qf = const_coeff ? info.apply_qf_mf_const
                                   : info.apply_qf_mf_quad;
      return MFOperator{fes, irm,
                        info.header,
                        apply_func, apply_qf,
                        info.trial_op,
                        info.test_op
                       };
   }

   template <typename Context>
   void Assemble(MFOperator &op, Context &ctx)
   {
      const mfem::FiniteElementSpace &fes = op.fes;
      const mfem::IntegrationRule &irm = op.ir;
      Ceed ceed(internal::ceed);
      mfem::Mesh *mesh = fes.GetMesh();
      CeedInt nqpts, nelem = mesh->GetNE();
      CeedInt dim = mesh->SpaceDimension(), vdim = fes.GetVDim();

      mesh->EnsureNodes();
      InitBasisAndRestriction(fes, irm, ceed, &basis, &restr);

      const mfem::FiniteElementSpace *mesh_fes = mesh->GetNodalFESpace();
      MFEM_VERIFY(mesh_fes, "the Mesh has no nodal FE space");
      InitBasisAndRestriction(*mesh_fes, irm, ceed, &mesh_basis,
                              &mesh_restr);

      CeedBasisGetNumQuadraturePoints(basis, &nqpts);

      InitVector(*mesh->GetNodes(), node_coords);

      // Context data to be passed to the Q-function.
      ctx.dim = mesh->Dimension();
      ctx.space_dim = mesh->SpaceDimension();
      ctx.vdim = fes.GetVDim();

      std::string qf_file = GetCeedPath() + op.header;
      std::string qf = qf_file + op.apply_func;
      CeedQFunctionCreateInterior(ceed, 1, op.apply_qf, qf.c_str(),
                                  &apply_qfunc);

      // Create the Q-function that builds the operator (i.e. computes its
      // quadrature data) and set its context data.
      if (VariableCoefficient *var_coeff = dynamic_cast<VariableCoefficient*>(coeff))
      {
         CeedQFunctionAddInput(apply_qfunc, "coeff", coeff->ncomp, var_coeff->emode);
      }
      // input
      switch (op.trial_op)
      {
         case EvalMode::None:
            CeedQFunctionAddInput(apply_qfunc, "u", vdim, CEED_EVAL_NONE);
            break;
         case EvalMode::Interp:
            CeedQFunctionAddInput(apply_qfunc, "u", vdim, CEED_EVAL_INTERP);
            break;
         case EvalMode::Grad:
            CeedQFunctionAddInput(apply_qfunc, "gu", vdim*dim, CEED_EVAL_GRAD);
            break;
         case EvalMode::InterpAndGrad:
            CeedQFunctionAddInput(apply_qfunc, "u", vdim, CEED_EVAL_INTERP);
            CeedQFunctionAddInput(apply_qfunc, "gu", vdim*dim, CEED_EVAL_GRAD);
            break;
      }
      CeedQFunctionAddInput(apply_qfunc, "dx", dim * dim, CEED_EVAL_GRAD);
      CeedQFunctionAddInput(apply_qfunc, "weights", 1, CEED_EVAL_WEIGHT);
      // output
      switch (op.test_op)
      {
         case EvalMode::None:
            CeedQFunctionAddOutput(apply_qfunc, "v", vdim, CEED_EVAL_NONE);
            break;
         case EvalMode::Interp:
            CeedQFunctionAddOutput(apply_qfunc, "v", vdim, CEED_EVAL_INTERP);
            break;
         case EvalMode::Grad:
            CeedQFunctionAddOutput(apply_qfunc, "gv", vdim*dim, CEED_EVAL_GRAD);
            break;
         case EvalMode::InterpAndGrad:
            CeedQFunctionAddOutput(apply_qfunc, "v", vdim, CEED_EVAL_INTERP);
            CeedQFunctionAddOutput(apply_qfunc, "gv", vdim*dim, CEED_EVAL_GRAD);
            break;
      }

      CeedQFunctionContextCreate(ceed, &build_ctx);
      CeedQFunctionContextSetData(build_ctx, CEED_MEM_HOST,
                                  CEED_COPY_VALUES,
                                  sizeof(ctx),
                                  &ctx);
      CeedQFunctionSetContext(apply_qfunc, build_ctx);

      // Create the operator.
      CeedOperatorCreate(ceed, apply_qfunc, NULL, NULL, &oper);
      // coefficient
      if (GridCoefficient *gridCoeff = dynamic_cast<GridCoefficient*>(coeff))
      {
         InitBasisAndRestriction(*gridCoeff->gf.FESpace(), irm, ceed,
                                 &gridCoeff->basis, &gridCoeff->restr);
         CeedOperatorSetField(oper, "coeff", gridCoeff->restr,
                              gridCoeff->basis, gridCoeff->coeffVector);
      }
      else if (QuadCoefficient *quadCoeff = dynamic_cast<QuadCoefficient*>(coeff))
      {
         const int ncomp = quadCoeff->ncomp;
         CeedInt strides[3] = {ncomp, 1, ncomp*nqpts};
         InitStridedRestriction(*mesh->GetNodalFESpace(),
                                nelem, nqpts, ncomp, strides,
                                &quadCoeff->restr);
         CeedOperatorSetField(oper, "coeff", quadCoeff->restr,
                              CEED_BASIS_COLLOCATED, quadCoeff->coeffVector);
      }
      // input
      switch (op.trial_op)
      {
         case EvalMode::None:
            CeedOperatorSetField(oper, "u", restr,
                                 CEED_BASIS_COLLOCATED, CEED_VECTOR_ACTIVE);
            break;
         case EvalMode::Interp:
            CeedOperatorSetField(oper, "u", restr, basis, CEED_VECTOR_ACTIVE);
            break;
         case EvalMode::Grad:
            CeedOperatorSetField(oper, "gu", restr, basis, CEED_VECTOR_ACTIVE);
            break;
         case EvalMode::InterpAndGrad:
            CeedOperatorSetField(oper, "u", restr, basis, CEED_VECTOR_ACTIVE);
            CeedOperatorSetField(oper, "gu", restr, basis, CEED_VECTOR_ACTIVE);
            break;
      }
      CeedOperatorSetField(oper, "dx", mesh_restr,
                           mesh_basis, node_coords);
      CeedOperatorSetField(oper, "weights", CEED_ELEMRESTRICTION_NONE,
                           mesh_basis, CEED_VECTOR_NONE);
      // output
      switch (op.test_op)
      {
         case EvalMode::None:
            CeedOperatorSetField(oper, "v", restr,
                                 CEED_BASIS_COLLOCATED, CEED_VECTOR_ACTIVE);
            break;
         case EvalMode::Interp:
            CeedOperatorSetField(oper, "v", restr, basis, CEED_VECTOR_ACTIVE);
            break;
         case EvalMode::Grad:
            CeedOperatorSetField(oper, "gv", restr, basis, CEED_VECTOR_ACTIVE);
            break;
         case EvalMode::InterpAndGrad:
            CeedOperatorSetField(oper, "v", restr, basis, CEED_VECTOR_ACTIVE);
            CeedOperatorSetField(oper, "gv", restr, basis, CEED_VECTOR_ACTIVE);
            break;
      }

      CeedVectorCreate(ceed, vdim*fes.GetNDofs(), &u);
      CeedVectorCreate(ceed, vdim*fes.GetNDofs(), &v);
   }

   virtual ~MFIntegrator()
   {
      CeedQFunctionDestroy(&apply_qfunc);
      CeedQFunctionContextDestroy(&build_ctx);
      CeedVectorDestroy(&node_coords);
      CeedVectorDestroy(&qdata);
      delete coeff;
   }
#endif
};

} // namespace ceed

} // namespace mfem

#endif // MFEM_LIBCEED_MFINTEG