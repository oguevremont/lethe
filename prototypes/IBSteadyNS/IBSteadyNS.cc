﻿/* ---------------------------------------------------------------------
 *
 * Copyright (C) 2000 - 2016 by the deal.II authors
 *
 * This file is part of the deal.II library.
 *
 * The deal.II library is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE at
 * the top level of the deal.II distribution.
 *
 * ---------------------------------------------------------------------

 *
 * Author: Wolfgang Bangerth, University of Heidelberg, 2000
 */


// @sect3{Include files}

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/function.h>
#include <deal.II/base/logstream.h>

#include <deal.II/lac/vector.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/sparse_matrix.h>
#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/block_sparse_matrix.h>
#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/solver_bicgstab.h>
#include <deal.II/lac/solver_gmres.h>
#include <deal.II/lac/precondition.h>
#include <deal.II/lac/sparse_ilu.h>
#include <deal.II/lac/affine_constraints.h>
#include <deal.II/lac/sparse_direct.h>

#include <deal.II/grid/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/grid/manifold_lib.h>
#include <deal.II/grid/grid_in.h>
#include <deal.II/grid/grid_out.h>
#include <deal.II/grid/grid_refinement.h>
#include <deal.II/grid/grid_tools.h>

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_tools.h>

#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/mapping_q.h>

#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/error_estimator.h>
#include <deal.II/numerics/solution_transfer.h>

#include "exactsolutions.h"
#include "forcingfunctions.h"
#include "boundaryconditions.h"

#include <fstream>
#include <iostream>

#include "iblevelsetfunctions.h"
#include "ibcombiner.h"
#include "ib_node_status.h"
#include "nouvtriangles.h"
#include "trg_tools_class.h"
#include "condensate.h"

// Finally, this is as in previous programs:
using namespace dealii;

enum SimulationCases{MMS, CouetteX, CouetteY, TaylorCouette};

template <int dim>
class DirectSteadyNavierStokes
{

public:
    DirectSteadyNavierStokes(const unsigned int degreeVelocity, const unsigned int degreePressure);
    ~DirectSteadyNavierStokes();
    void runCouetteX();
    void runCouetteIBX();
    void runCouetteXPerturbedMesh();
    void runMMS();
    void runTaylorCouette();

    Function<dim> *exact_solution;
    Function<dim> *forcing_function;

private:
    void make_cube_grid(int refinementLevel);
    void refine_grid();
    void refine_mesh();
    void refine_mesh_uniform();
    void setup_dofs();
    void initialize_system();


    void integrate_sub_quad_element( Triangulation<2> &sub_triangulation,
                                     DoFHandler<2> &dof_handler,FESystem<2> &fe,
                                     FullMatrix<double> &system_matrix,
                                     Vector<double> &system_rhs,
                                     Vector<double> &evaluation_point);

    void GLS_residual_trg(FullMatrix<double> &local_mat,
                          Vector<double> &local_rhs   );

    void assemble(const bool initial_step,
                   const bool assemble_matrix);
    void assemble_system(const bool initial_step);
    void assemble_rhs(const bool initial_step);
    void solve(bool initial_step);
    void calculateL2Error();
    void output_results(std::string case_name, const unsigned int cycle) const;
    void newton_iteration(const double tolerance,
                          const unsigned int max_iteration,
                          const bool is_initial_step,
                          const bool output_result);


    std::vector<types::global_dof_index> dofs_per_block;

    double viscosity_;
    const unsigned int           degreeIntegration_;
    Triangulation<dim> triangulation;
    FESystem<dim> fe;
    DoFHandler<dim> dof_handler;


    AffineConstraints<double>    zero_constraints;
    AffineConstraints<double>    nonzero_constraints;

    BlockSparsityPattern         sparsity_pattern;
    BlockSparseMatrix<double>    system_matrix;

    BlockVector<double>          present_solution;
    BlockVector<double>          newton_update;
    BlockVector<double>          system_rhs;
    BlockVector<double>          evaluation_point;

    IBCombiner<dim>              ib_combiner;
    TRG_tools<dim>               trg_;

    SimulationCases simulationCase_;
    const bool stabilized_=false;
    const bool iterative_=false;
    std::vector<double> L2ErrorU_;
    const int initialSize_=3;
};

template<int dim>
void DirectSteadyNavierStokes<dim>::integrate_sub_quad_element( Triangulation<2> &sub_triangulation,  DoFHandler<2> &dof_handler,FESystem<2> &fe,
                                                                FullMatrix<double> &system_matrix, Vector<double> &system_rhs, Vector<double> &local_evaluation_point)
{
  // Create a mapping for this new element
  const MappingQ<dim>      mapping (1);
  QGauss<dim>              quadrature_formula(4);

  // Integrate over this element, in this case we only integrate
  // over the quadrature to calculate the area
  FEValues<dim> fe_values (mapping,
                           fe,
                           quadrature_formula,
                           update_values |
                           update_quadrature_points |
                           update_JxW_values |
                           update_gradients |
                           update_hessians);

  const unsigned int   dofs_per_cell = fe.dofs_per_cell;
  const unsigned int   n_q_points    = quadrature_formula.size();
  const FEValuesExtractors::Vector velocities (0);
  const FEValuesExtractors::Scalar pressure (dim);
  FullMatrix<double>   local_matrix (dofs_per_cell, dofs_per_cell);
  Vector<double>       local_rhs    (dofs_per_cell);
  std::vector<Vector<double> >      rhs_force (n_q_points, Vector<double>(dim+1));
  std::vector<types::global_dof_index> local_dof_indices (dofs_per_cell);
  std::vector<Tensor<1, dim> >  present_velocity_values    (n_q_points);
  std::vector<Tensor<2, dim> >  present_velocity_gradients (n_q_points);
  std::vector<double>           present_pressure_values    (n_q_points);
  std::vector<Tensor<1, dim> >  present_pressure_gradients   (n_q_points);
  std::vector<Tensor<1, dim> >  present_velocity_laplacians  (n_q_points);
  std::vector<Tensor<2, dim> >  present_velocity_hess        (n_q_points);

  Tensor<1, dim>  force;

  std::vector<double>           div_phi_u                 (dofs_per_cell);
  std::vector<Tensor<1, dim> >  phi_u                     (dofs_per_cell);
  std::vector<Tensor<3, dim> >  hess_phi_u                (dofs_per_cell);
  std::vector<Tensor<1, dim> >  laplacian_phi_u           (dofs_per_cell);
  std::vector<Tensor<2, dim> >  grad_phi_u                (dofs_per_cell);
  std::vector<double>           phi_p                     (dofs_per_cell);
  std::vector<Tensor<1, dim> >  grad_phi_p                (dofs_per_cell);


  double h;

  typename DoFHandler<2>::active_cell_iterator
      cell = dof_handler.begin_active(),
      endc = dof_handler.end();
  for (; cell!=endc; ++cell)
  {
    fe_values.reinit (cell);

    local_rhs=0;
    local_matrix=0;

    std::cout << "Looping over quadrature" << std::endl;

      if (dim==2) h = std::sqrt(4.* cell->measure() / M_PI) ;
      else if (dim==3) h = pow(6*cell->measure()/M_PI,1./3.) ;

      fe_values[velocities].get_function_values(local_evaluation_point, present_velocity_values);
      fe_values[velocities].get_function_gradients(local_evaluation_point, present_velocity_gradients);
      fe_values[pressure].get_function_values(local_evaluation_point,present_pressure_values);
      fe_values[pressure].get_function_gradients(local_evaluation_point,present_pressure_gradients);
      fe_values[velocities].get_function_laplacians(local_evaluation_point,present_velocity_laplacians);
        forcing_function->vector_value_list(fe_values.get_quadrature_points(),
                                                   rhs_force);

        for (unsigned int q=0; q<n_q_points; ++q)
        {
          const double u_mag= std::max(present_velocity_values[q].norm(),1e-3*1.);
          double tau = 1./ std::sqrt(std::pow(2.*u_mag/h,2)+9*std::pow(4*viscosity_/(h*h),2));
          for (unsigned int k=0; k<dofs_per_cell; ++k)
          {
            div_phi_u[k]  =  fe_values[velocities].divergence (k, q);
            grad_phi_u[k] =  fe_values[velocities].gradient(k, q);
            phi_u[k]      =  fe_values[velocities].value(k, q);
            hess_phi_u[k] =  fe_values[velocities].hessian(k, q);
            phi_p[k]      =  fe_values[pressure]  .value(k, q);
            grad_phi_p[k] =  fe_values[pressure]  .gradient(k, q);

            for( int d=0; d<dim; ++d )
              laplacian_phi_u[k][d] = trace( hess_phi_u[k][d] );
          }

          // Establish the force vector
          for( int i=0; i<dim; ++i )
          {
            const unsigned int component_i = fe.system_to_component_index(i).first;
            force[i] = rhs_force[q](component_i);
          }

          auto strong_residual= present_velocity_gradients[q]*present_velocity_values[q]
                                + present_pressure_gradients[q]
                                - viscosity_* present_velocity_laplacians[q]
                                - force ;

          for (unsigned int j=0; j<dofs_per_cell; ++j)
          {
              auto strong_jac = (  present_velocity_gradients[q]*phi_u[j]
                                   + grad_phi_u[j]*present_velocity_values[q]
                                   + grad_phi_p[j]
                                   - viscosity_* laplacian_phi_u[j]
                                   );

              for (unsigned int i=0; i<dofs_per_cell; ++i)
              {
                local_matrix(i, j) += (  viscosity_*scalar_product(grad_phi_u[j], grad_phi_u[i])
                                         + present_velocity_gradients[q]*phi_u[j]*phi_u[i]
                                         + grad_phi_u[j]*present_velocity_values[q]*phi_u[i]
                                         - div_phi_u[i]*phi_p[j]
                                         + phi_p[i]*div_phi_u[j]
                                         )
                                      * fe_values.JxW(q);
                //PSPG GLS term
                local_matrix(i, j) += tau*
                                      strong_jac* grad_phi_p[i]
                                      * fe_values.JxW(q);

                // SUPG GLS term
                local_matrix(i, j) +=
                    tau*
                    (
                      strong_jac*(grad_phi_u[i]*present_velocity_values[q])
                      +
                      strong_residual* (grad_phi_u[i]*phi_u[j])
                      )
                    * fe_values.JxW(q)
                    ;
              }
          }
          for (unsigned int i=0; i<dofs_per_cell; ++i)
          {
            const unsigned int component_i = fe.system_to_component_index(i).first;
            double present_velocity_divergence =  trace(present_velocity_gradients[q]);
            local_rhs(i) += ( - viscosity_*scalar_product(present_velocity_gradients[q],grad_phi_u[i])
                              - present_velocity_gradients[q]*present_velocity_values[q]*phi_u[i]
                              + present_pressure_values[q]*div_phi_u[i]
                              - present_velocity_divergence*phi_p[i]
                              + force * phi_u[i]
                              )
                            * fe_values.JxW(q);

            // PSPG GLS term
            local_rhs(i) +=  - tau
                             * (strong_residual*grad_phi_p[i])
                             * fe_values.JxW(q);

            //SUPG GLS term
            local_rhs(i) += - tau
                            *(strong_residual*(grad_phi_u[i]*present_velocity_values[q]))
                            * fe_values.JxW(q);


          }


        }

    // Assemble global matrix and RHS
    cell->get_dof_indices (local_dof_indices);

    for (unsigned int i=0; i<dofs_per_cell; ++i)
    {
      for (unsigned int j=0; j<dofs_per_cell; ++j)
        system_matrix(local_dof_indices[i],
                       local_dof_indices[j])
                        += local_matrix(i,j);

      system_rhs(local_dof_indices[i]) += local_rhs(i);
    }
  }
 }

// Constructor
template<int dim>
DirectSteadyNavierStokes<dim>::DirectSteadyNavierStokes(const unsigned int degreeVelocity, const unsigned int degreePressure):
    viscosity_(1), degreeIntegration_(degreeVelocity),
    fe(FE_Q<dim>(degreeVelocity), dim, FE_Q<dim>(degreePressure), 1),
    dof_handler(triangulation)
{}


template <int dim>
DirectSteadyNavierStokes<dim>::~DirectSteadyNavierStokes ()
{
  triangulation.clear ();
}



template <int dim>
void DirectSteadyNavierStokes<dim>::make_cube_grid (int refinementLevel)
{
  GridGenerator::hyper_cube (triangulation, -1, 1);
  triangulation.refine_global (refinementLevel);
}

template <int dim>
void DirectSteadyNavierStokes<dim>::refine_grid()
{
    triangulation.refine_global(1);
}


template <int dim>
void DirectSteadyNavierStokes<dim>::setup_dofs ()
{
    system_matrix.clear();

    dof_handler.distribute_dofs(fe);

    std::vector<unsigned int> block_component(dim+1, 0);
    block_component[dim] = 1;
    DoFRenumbering::component_wise (dof_handler, block_component);
    dofs_per_block.resize (2);
    DoFTools::count_dofs_per_block (dof_handler, dofs_per_block, block_component);
    unsigned int dof_u = dofs_per_block[0];
    unsigned int dof_p = dofs_per_block[1];

    FEValuesExtractors::Vector velocities(0);
    {
      nonzero_constraints.clear();

      DoFTools::make_hanging_node_constraints(dof_handler, nonzero_constraints);
      VectorTools::interpolate_boundary_values(dof_handler, 0, ZeroFunction<dim>(dim+1), nonzero_constraints,
                                               fe.component_mask(velocities));

      if (simulationCase_==TaylorCouette)
      {
          VectorTools::interpolate_boundary_values(dof_handler,
                                                   1,
                                                   RotatingWall<dim>(),
                                                   nonzero_constraints,
                                                   fe.component_mask(velocities));
      }

      if (simulationCase_==CouetteX)
      {
          VectorTools::interpolate_boundary_values(dof_handler,
                                                   1,
                                                   ConstantYMotion<dim>(),
                                                   nonzero_constraints,
                                                   fe.component_mask(velocities));
      }
    }
    nonzero_constraints.close();

    {
      zero_constraints.clear();
      DoFTools::make_hanging_node_constraints(dof_handler, zero_constraints);
      VectorTools::interpolate_boundary_values(dof_handler,
                                               0,
                                               ZeroFunction<dim>(dim+1),
                                               zero_constraints,
                                               fe.component_mask(velocities));


      if (simulationCase_==TaylorCouette || simulationCase_==CouetteX )
      {
          VectorTools::interpolate_boundary_values(dof_handler,
                                               1,
                                               ZeroFunction<dim>(dim+1),
                                               zero_constraints,
                                               fe.component_mask(velocities));
      }

    }
    zero_constraints.close();
    std::cout << "   Number of active cells: "
              << triangulation.n_active_cells()
              << std::endl
              << "   Number of degrees of freedom: "
              << dof_handler.n_dofs()
              << " (" << dof_u << '+' << dof_p << ')'
              << std::endl;

}

template <int dim>
void DirectSteadyNavierStokes<dim>::initialize_system()
{
  {
    BlockDynamicSparsityPattern dsp (dofs_per_block, dofs_per_block);
    DoFTools::make_sparsity_pattern (dof_handler, dsp, nonzero_constraints);
    sparsity_pattern.copy_from (dsp);
  }
  system_matrix.reinit (sparsity_pattern);
  present_solution.reinit (dofs_per_block);
  newton_update.reinit (dofs_per_block);
  system_rhs.reinit (dofs_per_block);
}


template<int dim>
void DirectSteadyNavierStokes<dim>::GLS_residual_trg(
                        FullMatrix<double> &local_mat,
                        Vector<double> &local_rhs   )

// decomp_trg gives the coordinates of the vertices of the triangle considered
// the 4 following arguments are the values on these vertices of respectively the components of the velocity, the pressure, and the gradients of the velocity and the pressure

// local_mat and local_rhs are the local matrix and right hand side we are going to fill
// the condensation is not made here
// viscosity is a parameter of the problem (the viscosity obviously)

{
    local_mat = 0;
    local_rhs = 0;

    double h; // size of the element

    unsigned int dofs_per_trg = (dim+1)*(dim+1);
    // dofs_per_trg is the number of dof per vertex multiplied by 3 (in 2D it should be 3, 3D 4)

    if (dim==2)
    {

        h = trg_.size_el() ; // "size" of the triangle, basically the square root of its area

        // Vectors for the shapes functions //

        std::vector<double>           div_phi_u_(dofs_per_trg);
        std::vector<Tensor<1,dim>>    phi_u(dofs_per_trg);
        std::vector<Tensor<2,dim>>    grad_phi_u(dofs_per_trg);
        std::vector<double>           phi_p(dofs_per_trg);
        std::vector<Tensor<1,dim>>    grad_phi_p(dofs_per_trg);

        // the part for the force vector is not implemented yet

        // quadrature points + weight for a triangle : Hammer quadrature
        // Building the vector of quadrature points and vector associated weights //

        unsigned int n_pt_quad = 4;

        std::vector<Point<dim>>             quad_pt(n_pt_quad);
        std::vector<double>                 weight(n_pt_quad);
        get_Quadrature_trg(quad_pt, weight);

        // Passage matrix from element coordinates to ref element coordinates, it is necessary to calculate the derivates

        Tensor<2, dim>       pass_mat; // we express the coordinates of the ref element depending on the coordinates of the actual element
        trg_.matrix_pass_elem_to_ref(pass_mat);

        // Values and gradients interpolated at the quadrature points shall be stored in the following vectors

        Tensor<1,dim>           interpolated_v;
        double                  interpolated_p = 0;
        Tensor<1,dim>           interpolated_grad_p;
        Tensor<2,dim>           interpolated_grad_v;

        // jacobian is a constant in a triangle
        double jac = trg_.jacob() ;

        for (unsigned int q=0; q<n_pt_quad; ++q)
        {
            interpolated_v =0;

            double JxW = weight[q]*jac ;

            // Get the values of the variables at the quadrature point //

            interpolated_p = trg_.interpolate_pressure(quad_pt[q]);
            trg_.interpolate_velocity(quad_pt[q], interpolated_v);
            trg_.interpolate_grad_pressure(interpolated_grad_p);
            trg_.interpolate_grad_velocity(interpolated_grad_v);

            // Build the parameter of stabilisation //

            const double u_mag= std::max(interpolated_v.norm(),1e-3);
            double tau = 1./ std::sqrt(std::pow(2.*u_mag/h,2)+9*std::pow(4*viscosity_/(h*h),2));

            // Get the values of the shape functions and their gradients at the quadrature points //

            // phi_u is such as [[phi_u_0,0], [0, phi_v_0] , [0,0], [phi_u_1,0], ...]
            // phi_p is such as [0, 0, phi_p_0, 0, ...]
            // div_phi_u is such as [d(phi_u_0)/d(xi), d(phi_v_0)/d(eta), 0, d(phi_u_1)/d(xi), ...] (xi, eta) being the system of coordinates used in the ref element
            // grad_phi_u is such as [[[grad_phi_u_0],[0, 0]], [[0, 0], [grad_phi_v_0]], [[0, 0], [0, 0]], [[grad_phi_u_1],[0, 0]], ...]
            // grad_phi_p is such as [[0, 0], [0, 0], [grad_phi_p_0], [0, 0], ...]

            trg_.build_phi_p(quad_pt[q], phi_p);
            trg_.build_phi_u(quad_pt[q], phi_u);
            trg_.build_div_phi_u(pass_mat, div_phi_u_);
            trg_.build_grad_phi_p(pass_mat, grad_phi_p);
            trg_.build_grad_phi_u(pass_mat, grad_phi_u);

            // Calculate and put in a local matrix and local rhs which will be returned
            for (unsigned int i=0; i<dofs_per_trg; ++i)
            {

                // matrix terms

                for (unsigned int j=0; j<dofs_per_trg; ++j)
                {
                    local_mat(i, j) += (     viscosity_ * trace(grad_phi_u[j] * grad_phi_u[i])          // ok + ok changement de coor

                                             + phi_u[i] * interpolated_grad_v * phi_u[j]                        // ok + ok changement de coor

                                             + (grad_phi_u[j] * interpolated_v) * phi_u[i]                      // ok + ok changement de coor

                                             - (div_phi_u_[i])*phi_p[j]                                         // ok + ok changement de coor

                                             + phi_p[i]*(div_phi_u_[j])                                         // ok + ok changement de coor

                                             ) * JxW ;

                    // PSPG GLS term //

                    local_mat(i, j) += tau* (  grad_phi_p[i] * interpolated_grad_v * phi_u[j]                   // ok + ok changement de coor
                                               + grad_phi_u[j] * interpolated_v * grad_phi_p[i]                 // ok + ok changement de coor
                                               + grad_phi_p[j]*grad_phi_p[i]                                    // ok + ok changement de coor
                                               )  * JxW;

                    // SUPG term //

                    local_mat(i, j) += tau* // convection and velocity terms
                                    (  (interpolated_grad_v * phi_u[j]) * (grad_phi_u[i] * interpolated_v)      // ok + ok changement de coor
                                    +  (grad_phi_u[i] * interpolated_v) * (grad_phi_u[j] * interpolated_v)      // ok + ok changement de coor
                                    +  phi_u[j] * ((interpolated_grad_v * interpolated_v) * grad_phi_u[i])      // ok + ok changement de coor
                                    )* JxW

                                    +  tau* // pressure terms
                                    (  grad_phi_p[j] * (grad_phi_u[i] * interpolated_v)                         // ok + ok changement de coor
                                    +  phi_u[j]*(interpolated_grad_p *grad_phi_u[i])                            // ok + ok changement de coor
//                                    -  force * (phi_u[j]*grad_phi_u[i])
                                    )* JxW;
                }



                // Evaluate the rhs, with corrective terms

                double present_velocity_divergence =  trace(interpolated_grad_v);

                local_rhs(i) += ( - viscosity_*trace(interpolated_grad_v*grad_phi_u[i])
                                  - interpolated_grad_v * interpolated_v * phi_u[i]
                                  + interpolated_p * div_phi_u_[i]
                                  - present_velocity_divergence*phi_p[i]
//                                  + force * phi_u[i]
                                ) * JxW;


                // PSPG GLS term for the rhs //
                local_rhs(i) +=  tau*(  - interpolated_grad_v * interpolated_v* grad_phi_p[i]
                                        - interpolated_grad_p * grad_phi_p[i]
//                                        + force * grad_phi_p[i]
                                     )  * JxW;

                // SUPG term for the rhs //
                local_rhs(i) += tau*(   - interpolated_grad_v * interpolated_v * (grad_phi_u[i] * interpolated_v )
                                        - interpolated_grad_p * (grad_phi_u[i] * interpolated_v)
//                                        + force *(interpolated_v* grad_phi_u[i])
                                    )   * JxW;

            }
        }

    }
    std::cout << " \n system matrix in gls trg : " << std::endl;
    for (int i = 0; i < 9; ++i) {
        std::cout << local_mat(i,0) << " "  << local_mat(i,1) << " " << local_mat(i,2) << " "
                  << local_mat(i,3) << " "  << local_mat(i,4) << " " << local_mat(i,5) << " "
                  << local_mat(i,6) << " "  << local_mat(i,7) << " " << local_mat(i,8) << " "
                  << std::endl;}
    std::cout << " \n system rhs in gls trg : " << std::endl;
    std::cout << local_rhs(0) << " "  << local_rhs(1) << " " << local_rhs(2) << " "
              << local_rhs(3) << " "  << local_rhs(4) << " " << local_rhs(5) << " "
              << local_rhs(6) << " "  << local_rhs(7) << " " << local_rhs(8) << " "
              << std::endl;
}


template <int dim>
void DirectSteadyNavierStokes<dim>::assemble(const bool initial_step,
                                           const bool assemble_matrix)
{



    if (assemble_matrix) system_matrix    = 0;
    system_rhs       = 0;
    QGauss<dim>   quadrature_formula(degreeIntegration_+2);
    const MappingQ<dim>      mapping (1);
    FEValues<dim> fe_values (mapping,
                             fe,
                             quadrature_formula,
                             update_values |
                             update_quadrature_points |
                             update_JxW_values |
                             update_gradients |
                             update_hessians);
    const unsigned int   dofs_per_cell = fe.dofs_per_cell;
    const unsigned int   n_q_points    = quadrature_formula.size();
    const FEValuesExtractors::Vector velocities (0);
    const FEValuesExtractors::Scalar pressure (dim);
    FullMatrix<double>   local_matrix (dofs_per_cell, dofs_per_cell);
    Vector<double>       local_rhs    (dofs_per_cell);
    std::vector<Vector<double> >      rhs_force (n_q_points, Vector<double>(dim+1));
    std::vector<types::global_dof_index> local_dof_indices (dofs_per_cell);
    std::vector<Tensor<1, dim> >  present_velocity_values    (n_q_points);
    std::vector<Tensor<2, dim> >  present_velocity_gradients (n_q_points);
    std::vector<double>           present_pressure_values    (n_q_points);
    std::vector<Tensor<1, dim> >  present_pressure_gradients   (n_q_points);
    std::vector<Tensor<1, dim> >  present_velocity_laplacians  (n_q_points);
    std::vector<Tensor<2, dim> >  present_velocity_hess        (n_q_points);

    Tensor<1, dim>  force;


    std::vector<double>           div_phi_u                 (dofs_per_cell);
    std::vector<Tensor<1, dim> >  phi_u                     (dofs_per_cell);
    std::vector<Tensor<3, dim> >  hess_phi_u                (dofs_per_cell);
    std::vector<Tensor<1, dim> >  laplacian_phi_u           (dofs_per_cell);
    std::vector<Tensor<2, dim> >  grad_phi_u                (dofs_per_cell);
    std::vector<double>           phi_p                     (dofs_per_cell);
    std::vector<Tensor<1, dim> >  grad_phi_p                (dofs_per_cell);

    Vector<double>   vertices_vp(dofs_per_cell); //only in 2D, stores the velocity and the pressure on each vertex of the square element considered

    std::map< types::global_dof_index, Point< 2 > > support_points;
    DoFTools::map_dofs_to_support_points ( mapping, dof_handler,support_points );
    std::vector<double>                  distance(dofs_per_cell); // Array for the distances associated with the DOFS
    std::vector<Point<2> >               dofs_points(dofs_per_cell);// Array for the DOFs points

    // Instantiations for the decomposition of the elements
    std::vector<int>                     corresp(9);
    std::vector<Point<2> >               decomp_elem(9);         // Array containing the points of the new elements created by decomposing the elements crossed by the boundary fluid/solid, there are up to 9 points that are stored in it
    std::vector<node_status>             No_pts_solid(4);
    int                                  nb_poly=0;                   // Number of sub-elements created in the fluid part for each element ( 0 if the element is entirely in the solid or the fluid)
    std::vector<Point<2> >               num_elem(6);

    std::vector<Point<2> >               coor(4);
    std::vector<double>                  dist(4);
    // The previous part is only implemented for 2D NS, and the function nouvtriangles as well

    typename DoFHandler<dim>::active_cell_iterator
    cell = dof_handler.begin_active(),
    endc = dof_handler.end();

    // Element size
    double h ;

    for (; cell!=endc; ++cell)
      {
        fe_values.reinit(cell);
        cell->get_dof_indices (local_dof_indices);

        if (ib_combiner.size()>0)
        {
          for (unsigned int dof_index=0 ; dof_index < local_dof_indices.size() ; ++dof_index)
          {
            dofs_points[dof_index] = support_points[local_dof_indices[dof_index]];
            distance[dof_index]    = ib_combiner.value(dofs_points[dof_index]);
            vertices_vp[dof_index] = evaluation_point[local_dof_indices[dof_index]];
          //  std::cout << vertices_vp[dof_index] << std::endl;
          }

          // We get the coordinates and the distance associated to the vertices of the element
          for (unsigned int i = 0; i < dofs_per_cell/(dim+1); ++i) {
            coor[i] = dofs_points[(dim+1)*i];
            dist[i] = distance[(dim+1)*i];
          }

          decomposition(corresp, No_pts_solid, num_elem, decomp_elem, &nb_poly, coor, dist);
        }
        else
          nb_poly=0;

        local_matrix = 0;
        local_rhs    = 0;

        if (ib_combiner.size()<1 || (nb_poly==0 && (distance[0]>0)) )
        {
          std::cout << " Fluid element" << std::endl;
          if (dim==2) h = std::sqrt(4.* cell->measure() / M_PI) ;
          else if (dim==3) h = pow(6*cell->measure()/M_PI,1./3.) ;

          fe_values[velocities].get_function_values(evaluation_point, present_velocity_values);
          fe_values[velocities].get_function_gradients(evaluation_point, present_velocity_gradients);
          fe_values[pressure].get_function_values(evaluation_point,present_pressure_values);
          fe_values[pressure].get_function_gradients(evaluation_point,present_pressure_gradients);
          fe_values[velocities].get_function_laplacians(evaluation_point,present_velocity_laplacians);
            forcing_function->vector_value_list(fe_values.get_quadrature_points(),
                                                       rhs_force);

            for (unsigned int q=0; q<n_q_points; ++q)
            {
              const double u_mag= std::max(present_velocity_values[q].norm(),1e-3*1.);
              double tau = 1./ std::sqrt(std::pow(2.*u_mag/h,2)+9*std::pow(4*viscosity_/(h*h),2));
              for (unsigned int k=0; k<dofs_per_cell; ++k)
              {
                div_phi_u[k]  =  fe_values[velocities].divergence (k, q);
                grad_phi_u[k] =  fe_values[velocities].gradient(k, q);
                phi_u[k]      =  fe_values[velocities].value(k, q);
                hess_phi_u[k] =  fe_values[velocities].hessian(k, q);
                phi_p[k]      =  fe_values[pressure]  .value(k, q);
                grad_phi_p[k] =  fe_values[pressure]  .gradient(k, q);

                for( int d=0; d<dim; ++d )
                  laplacian_phi_u[k][d] = trace( hess_phi_u[k][d] );
              }

              // Establish the force vector
              for( int i=0; i<dim; ++i )
              {
                const unsigned int component_i = fe.system_to_component_index(i).first;
                force[i] = rhs_force[q](component_i);
              }

              auto strong_residual= present_velocity_gradients[q]*present_velocity_values[q]
                                    + present_pressure_gradients[q]
                                    - viscosity_* present_velocity_laplacians[q]
                                    - force ;

              for (unsigned int j=0; j<dofs_per_cell; ++j)
              {
                if (assemble_matrix)
                {
                  auto strong_jac = (  present_velocity_gradients[q]*phi_u[j]
                                       + grad_phi_u[j]*present_velocity_values[q]
                                       + grad_phi_p[j]
                                       - viscosity_* laplacian_phi_u[j]
                                       );

                  for (unsigned int i=0; i<dofs_per_cell; ++i)
                  {
                    local_matrix(i, j) += (  viscosity_*scalar_product(grad_phi_u[j], grad_phi_u[i])
                                             + present_velocity_gradients[q]*phi_u[j]*phi_u[i]
                                             + grad_phi_u[j]*present_velocity_values[q]*phi_u[i]
                                             - div_phi_u[i]*phi_p[j]
                                             + phi_p[i]*div_phi_u[j]
                                             )
                                          * fe_values.JxW(q);
                    //PSPG GLS term
                    local_matrix(i, j) += tau*
                                          strong_jac* grad_phi_p[i]
                                          * fe_values.JxW(q);

                    // SUPG GLS term
                    local_matrix(i, j) +=
                        tau*
                        (
                          strong_jac*(grad_phi_u[i]*present_velocity_values[q])
                          +
                          strong_residual* (grad_phi_u[i]*phi_u[j])
                          )
                        * fe_values.JxW(q)
                        ;
                  }
                }
                }
                for (unsigned int i=0; i<dofs_per_cell; ++i)
                {
                  const unsigned int component_i = fe.system_to_component_index(i).first;
                  double present_velocity_divergence =  trace(present_velocity_gradients[q]);
                  local_rhs(i) += ( - viscosity_*scalar_product(present_velocity_gradients[q],grad_phi_u[i])
                                    - present_velocity_gradients[q]*present_velocity_values[q]*phi_u[i]
                                    + present_pressure_values[q]*div_phi_u[i]
                                    - present_velocity_divergence*phi_p[i]
                                    + force * phi_u[i]
                                    )
                          * fe_values.JxW(q);

                  // PSPG GLS term
                  local_rhs(i) +=  - tau
                                   * (strong_residual*grad_phi_p[i])
                                   * fe_values.JxW(q);

                  //SUPG GLS term
                      local_rhs(i) += - tau
                                      *(strong_residual*(grad_phi_u[i]*present_velocity_values[q]))
                                      * fe_values.JxW(q);
                }
            }
            if (assemble_matrix){

                std::cout << " \n system matrix for pure fluid elements : " << std::endl;
                for (int i = 0; i < 12; ++i) {
                    std::cout << local_matrix(i,0) << " "  << local_matrix(i,1) << " " << local_matrix(i,2) << " "
                              << local_matrix(i,3) << " "  << local_matrix(i,4) << " " << local_matrix(i,5) << " "
                              << local_matrix(i,6) << " "  << local_matrix(i,7) << " " << local_matrix(i,8) << " "
                              << local_matrix(i,9) << " "  << local_matrix(i,10) << " " << local_matrix(i,11)
                              << std::endl;}}
                std::cout << " \n system rhs for pure fluid elements : " << std::endl;
                std::cout << local_rhs(0) << " "  << local_rhs(1) << " " << local_rhs(2) << " "
                          << local_rhs(3) << " "  << local_rhs(4) << " " << local_rhs(5) << " "
                          << local_rhs(6) << " "  << local_rhs(7) << " " << local_rhs(8) << " "
                          << local_rhs(9) << " "  << local_rhs(10) << " " << local_rhs(11)
                          << std::endl;}



        // Pure solid elements
        else if ((nb_poly==0 && (distance[0]<0)) )
        {
          std::cout << "Integrating over pure solid elements" << std::endl;
          Tensor<1,dim> ib_velocity;
          for (unsigned int i = 0; i < dofs_per_cell; ++i)
          {
            for (unsigned int j = 0; j < dofs_per_cell; ++j)
            {
              if (i==j)
                local_matrix[i][j] = 1;
              else
              {
                local_matrix[i][j] = 0;
              }
            }

            if (i%3==0) ib_combiner.velocity(dofs_points[i],ib_velocity);

            // Impose X, Y velocity and pressure depending on dof index
            if (i%3==0) local_rhs[i]=ib_velocity[0]-vertices_vp[i];
            else if (i%3==1) local_rhs[i]=ib_velocity[1]-vertices_vp[i];
            else local_rhs[i]=0;
          }
        }

        // Quadrilateral case
        else if (nb_poly==-1)
        {
          std::cout << "Integrating over cut quad element" << std::endl;
          // Create triangulation points
          std::vector<Point<dim> > triangulation_points(GeometryInfo<dim>::vertices_per_cell);
          // Create 4 points for triangulation:
          for (unsigned int i_pt =0 ; i_pt < 4 ; ++i_pt)
            triangulation_points[i_pt]=decomp_elem[i_pt];
//              triangulation_points[i_pt]=dofs_points[3*i_pt];

          // Prepare cell data
          std::vector<CellData<dim> > cells (1);
          for (unsigned int i=0; i<GeometryInfo<2>::vertices_per_cell; ++i)
              cells[0].vertices[i] = i;
          cells[0].material_id = 0;

          Triangulation<dim> sub_triangulation;
          sub_triangulation.create_triangulation (triangulation_points, cells, SubCellData());

          // Create a FE system for this element
          DoFHandler<dim>                  sub_dof_handler(sub_triangulation);
          FESystem<dim>                    sub_fe(FE_Q<dim>(1),dim,FE_Q<dim>(1),1);
          sub_dof_handler.distribute_dofs(sub_fe);

          FullMatrix<double> sub_system_matrix;
          Vector<double>     sub_system_rhs;
          Vector<double>     sub_system_dofs;

          // Initialize vector and sparsity patterns
          sub_system_matrix.reinit (sub_dof_handler.n_dofs(),sub_dof_handler.n_dofs());
          sub_system_rhs.reinit (sub_dof_handler.n_dofs());
          sub_system_dofs.reinit((sub_dof_handler.n_dofs()));
          std::vector<Tensor<1, dim>>   vertices_v(4);
          std::vector<double>           vertices_p(4);

          // get the values of u,v and p on the vertices of the sub-element
          for (unsigned int vertex_index = 0; vertex_index < 4; ++vertex_index) {

              for (int i = 0; i < dim; ++i) { // i is the component of the velocity
                  vertices_v[vertex_index][i] = vertices_vp[3*vertex_index+i];
              }
              if (vertices_vp[3*vertex_index+dim] < 1e3)
                  vertices_p[vertex_index] = vertices_vp[3*vertex_index+dim];
              else {
                  vertices_p[vertex_index] = 0;
              }
          }

          std::vector<Tensor<1,dim>>        local_v(4);
          std::vector<double>               local_p(4);
          for (int i = 0; i < 4; ++i) {
              if (corresp[i]>3)
              {
                  ib_combiner.velocity(num_elem[i], local_v[i]);
                  local_p[i] = 1./4*(vertices_p[0]+vertices_p[1]+vertices_p[2]+vertices_p[3]);
              }
              else {
                  local_v[i] = vertices_v[corresp[i]];
                  local_p[i] = vertices_p[corresp[i]];
              }
              sub_system_dofs[3*i] = local_v[i][0];
              sub_system_dofs[3*i+1] = local_v[i][1];
              sub_system_dofs[3*i+2] = local_p[i];
          }

          integrate_sub_quad_element(sub_triangulation, sub_dof_handler, sub_fe, sub_system_matrix, sub_system_rhs, /*vertices_vp */sub_system_dofs);

          // Create a vector in order to know on which dof we apply the boundary conditions
          // the definition of decomp_elem for a quad element (nb_poly== -1) is that the first 2 points are the boundary points, and the 2 others are the vertices in the fluid
          std::vector<node_status>      loc_vertices_status(4);
          loc_vertices_status[0] = solid;
          loc_vertices_status[1] = solid;
          loc_vertices_status[2] = fluid;
          loc_vertices_status[3] = fluid;

          // Create a vector to sort the dofs in order to condensate more easily

          /* here we create a quad sub element, but the numerotation in the element is as follow :
           *
           * 2-----5--3
           * | F  /   |
           * |   /  S |         F is the fluid part, S the solid part
           * 0--4-----1
           *
           * where 4 and 5 are the boundary points created with the decomposition function.
           * But in the local sub element formed by 4, 5, 0 and 2, we have the following numerotation
           *
           *(2)
           * 3-----1(5)
           * |    /
           * |   /
           * 2--0(4)
           *(0)
           *
           * We want to create a vector that will put the coefficients associated to dofs held by 0 and 1 in the local numerotation of the sub-element
           * at the last lines and columns of the elementary matrix so that we condensate those lines and columns later
           */

          std::vector<int>          corresp_dofs(12);
          for (int i = 0; i < 4; ++i) {
              corresp_dofs[i*3] = 3*corresp[i];
              corresp_dofs[i*3+1] = 3*corresp[i]+1;
              corresp_dofs[i*3+2] = 3*corresp[i]+2;
          }

          // creating the vector like this allows us to be sure that the dofs held by the boundary points will be associated to the last columns and lines of the local matrix

          // we create a matrix and a rhs-vector for the element plus the boundary points
          FullMatrix<double>    loc_mat(18,18); // there are 18 dofs since we added two points holding 3 dofs each
          Vector<double>        loc_rhs(18);
          loc_mat=0;
          loc_rhs=0;

          if (assemble_matrix){
          std::cout << " \n system matrix to be compared : " << std::endl;
          for (int i = 0; i < 12; ++i) {
              std::cout << sub_system_matrix(i,0) << " "  << sub_system_matrix(i,1) << " " << sub_system_matrix(i,2) << " "
                        << sub_system_matrix(i,3) << " "  << sub_system_matrix(i,4) << " " << sub_system_matrix(i,5) << " "
                        << sub_system_matrix(i,6) << " "  << sub_system_matrix(i,7) << " " << sub_system_matrix(i,8) << " "
                        << sub_system_matrix(i,9) << " "  << sub_system_matrix(i,10) << " " << sub_system_matrix(i,11) << " "
                        << std::endl;
          }}
          for (int i = 0; i < 12; ++i) {
              std::cout << "rhs " << i << " : " << sub_system_rhs[i] << std::endl;
          }
          for (int i = 0; i < 12; ++i) {
              for (int j = 0; j < 12; ++j) {
                  loc_mat(corresp_dofs[i],corresp_dofs[j]) = sub_system_matrix(i,j);
              }
              loc_rhs(corresp_dofs[i]) = sub_system_rhs[i];
          }

          // we create a tensor to store the value of the velocity on a point in the solid
          Tensor<1,dim>         v_solid;

          // we now set the conditions for the points in the solid part (not on the boundary points yet)
          for (int i = 0; i < 4; ++i) {
              if (No_pts_solid[i]==solid)
              {
                  // we set the speed dofs to be those given by ib_combiner, we set 0 for the pressure inside a solid
                  for (int j = 0; j < 18; ++j) {
                      loc_mat(3*i,j)=0;
                      loc_mat(3*i+1,j)=0;
                      loc_mat(3*i+2,j)=0; // pressure dof
                  }
                  // we set 1 on the diagonal, so that we can set the value we want for the speed in the rhs
                  loc_mat(3*i,3*i)=1;
                  loc_mat(3*i+1,3*i+1)=1;
                  loc_mat(3*i+2,3*i+2)=1; // we set the rhs to 0 for this one so that we have literally "p_node_solid = 0"

                  // we get the value of the velocity at the considered point
                  ib_combiner.velocity(dofs_points[i], v_solid);

                  loc_rhs(3*i) = v_solid[0]-vertices_vp[3*i];
                  loc_rhs(3*i+1) = v_solid[1]-vertices_vp[3*i+1];
              }
          }


          // we now set the boundary conditions on the boundary points

          for (int i = 12; i < 18; ++i) {
              // we only set the velocity, the pressure is free
              if (i%3!=2)
              {
                  for (int j = 0; j < 18; ++j) {
                      loc_mat(i,j) = 0;
                  }
                  loc_mat(i,i) = 1;
                  if (initial_step)
                  {ib_combiner.velocity(num_elem[i/3], v_solid);
                  loc_rhs[i] = v_solid[i%3];}
                  else {
                      loc_rhs[i] = 0;
                  }
              }
          }
//          std::cout << " \n system matrix after sorting and applying boundary conditions : " << std::endl;
//          for (int i = 0; i < 18; ++i) {
//              std::cout << loc_mat(i,0) << " "  << loc_mat(i,1) << " " << loc_mat(i,2) << " "
//                        << loc_mat(i,3) << " "  << loc_mat(i,4) << " " << loc_mat(i,5) << " "
//                        << loc_mat(i,6) << " "  << loc_mat(i,7) << " " << loc_mat(i,8) << " "
//                        << loc_mat(i,9) << " "  << loc_mat(i,10) << " " << loc_mat(i,11) << " "
//                        << loc_mat(i,12) << " "  << loc_mat(i,13) << " " << loc_mat(i,14) << " "
//                        << loc_mat(i,15) << " "  << loc_mat(i,16) << " " << loc_mat(i,17) << " "
//                        << std::endl;}
//          std::cout << " \n system rhs after sorting and applying boundary conditions : " << std::endl;
//          std::cout << loc_rhs(0) << " "  << loc_rhs(1) << " " << loc_rhs(2) << " "
//                    << loc_rhs(3) << " "  << loc_rhs(4) << " " << loc_rhs(5) << " "
//                    << loc_rhs(6) << " "  << loc_rhs(7) << " " << loc_rhs(8) << " "
//                    << loc_rhs(9) << " "  << loc_rhs(10) << " " << loc_rhs(11) << " "
//                    << loc_rhs(12) << " "  << loc_rhs(13) << " " << loc_rhs(14) << " "
//                    << loc_rhs(15) << " "  << loc_rhs(16) << " " << loc_rhs(17) << " "
//                    << std::endl;

          condensate(18, 12, loc_mat, local_matrix, loc_rhs, local_rhs);

          if (assemble_matrix){

              std::cout << " \n system matrix after condensation : " << std::endl;
              for (int i = 0; i < 12; ++i) {
                  std::cout << local_matrix(i,0) << " "  << local_matrix(i,1) << " " << local_matrix(i,2) << " "
                            << local_matrix(i,3) << " "  << local_matrix(i,4) << " " << local_matrix(i,5) << " "
                            << local_matrix(i,6) << " "  << local_matrix(i,7) << " " << local_matrix(i,8) << " "
                            << local_matrix(i,9) << " "  << local_matrix(i,10) << " " << local_matrix(i,11)
                            << std::endl;}
              std::cout << " \n system rhs after condensation : " << std::endl;
              std::cout << local_rhs(0) << " "  << local_rhs(1) << " " << local_rhs(2) << " "
                        << local_rhs(3) << " "  << local_rhs(4) << " " << local_rhs(5) << " "
                        << local_rhs(6) << " "  << local_rhs(7) << " " << local_rhs(8) << " "
                        << local_rhs(9) << " "  << local_rhs(10) << " " << local_rhs(11)
                        << std::endl;

          }


        }

        else if (nb_poly>0) { // this part is implemented for 2D problems only !! //

            // basically works as for nb_poly = -1, but with triangles, so check the comments of the previous part if you do not understand something here
            std::cout << "Integrating for an element decomposed into triangles" << std::endl;

            unsigned int dofs_per_vertex = 3; // 2D
            const unsigned int nb_of_vertices = 4; // (2D) nb_of_vertices should be 2^(dim), nb of vertices of the square / cubic element

            std::vector<Point<dim> >      coor_trg(dim+1); // triangles or tetraedromn are simplexes
            std::vector<unsigned int>     corresp_loc(dofs_per_vertex*(dim+1)); // gives the equivalence between the numerotation of the considered trg and the numerotaion of the square element

            FullMatrix<double>            cell_mat(18, 18);
            Vector<double>                cell_rhs(18);

            std::vector<Tensor<1, dim>>   local_v(4);
            std::vector<double>           local_p(4);



            for (unsigned int vertex_index = 0; vertex_index < nb_of_vertices; ++vertex_index) {

                for (int i = 0; i < dim; ++i) { // i is the component of the velocity
                    local_v[vertex_index][i] = vertices_vp[3*vertex_index+i];
                }
                local_p[vertex_index] = vertices_vp[3*vertex_index+dim];
            }


            // set the cell matrix and rhs to 0 before any calculus
            cell_mat = 0;
            cell_rhs = 0;

            // creating local matrix and rhs for each triangle
            FullMatrix<double>          loc_mat(9,9);
            Vector<double>              loc_rhs(9);

            Tensor<1, dim> force;

            force =0;
            // these are the cell matrix and rhs before we condensate them
            // we store the contributions created by the boundary points in it as well as the other contributions

            std::vector<Tensor<1, dim>>     trg_v(dim+1);
            std::vector<double>             trg_p(dim+1);

            std::vector<node_status>        status_vertices(nb_poly*(dim+1)); // it helps setting the boundary conditions
            if (nb_poly==1)
            {
                status_vertices[0] = fluid;
                status_vertices[1] = solid;
                status_vertices[2] = solid;
            }

            else if (nb_poly==3)
            {
                status_vertices[0] = fluid;
                status_vertices[1] = fluid;
                status_vertices[2] = solid;

                status_vertices[3] = fluid;
                status_vertices[4] = solid;
                status_vertices[5] = solid;

                status_vertices[6] = fluid;
                status_vertices[7] = solid;
                status_vertices[8] = fluid;
            }

            else { //should not happen
                throw std::runtime_error("nb_poly was not built correctly");
            }

            std::vector<Point<dim>>   boundary_points(2); // in 2d there are only 2 intersection points

            if (nb_poly==3){boundary_points[0] = decomp_elem[4];
                boundary_points[1] = decomp_elem[5];}
            else {
                boundary_points[0] = decomp_elem[1];
                boundary_points[1] = decomp_elem[2];
            }


            for (int n = 0; n < nb_poly; ++n) {
                loc_mat =0;
                loc_rhs =0;

                // We construct a vector of the coordinates of the vertices of the triangle considered
                coor_trg[0] = decomp_elem[(3*n)];
                coor_trg[(1)] = decomp_elem[(3*n+1)];
                coor_trg[2] = decomp_elem[(3*n)+2];


                // Corresp is a numerotation for the vertices, and each vertex has "dofs_per_vertex" dofs
                corresp_loc[0] = dofs_per_vertex*corresp[(3*n)];
                corresp_loc[1] = dofs_per_vertex*corresp[(3*n)]+1;
                corresp_loc[2] = dofs_per_vertex*corresp[(3*n)]+2;

                corresp_loc[3] = dofs_per_vertex*corresp[(3*n)+1];
                corresp_loc[4] = dofs_per_vertex*corresp[(3*n)+1]+1;
                corresp_loc[5] = dofs_per_vertex*corresp[(3*n)+1]+2;

                corresp_loc[6] = dofs_per_vertex*corresp[(3*n)+2];
                corresp_loc[7] = dofs_per_vertex*corresp[(3*n)+2]+1;
                corresp_loc[8] = dofs_per_vertex*corresp[(3*n)+2]+2;

                // We build the vector of velocity on the vertices of the triangle and the vector of pressure

                for (int index_vertex = 0; index_vertex < dim+1; ++index_vertex) {

                    if (status_vertices[3*n+index_vertex]==fluid)
                    {
                        trg_v[index_vertex] = local_v[corresp[3*n+index_vertex]];
                        trg_p[index_vertex] = local_p[corresp[3*n+index_vertex]];
                    }

                    else { // Ib_combiner contains the informations we need (u, v, p) for the points that are on the boundary
                        ib_combiner.velocity(coor_trg[index_vertex], trg_v[index_vertex]);
                        trg_p[index_vertex] = ib_combiner.scalar(coor_trg[index_vertex]);
                    }
                }

                trg_.set_coor_trg(coor_trg);
                trg_.set_dofs_per_node(dofs_per_vertex);
                trg_.set_P_on_vertices(local_p);
                trg_.set_V_on_vertices(local_v);

                // the following function calculates the values of the coefficient of the matrix and the rhs for the considered triangle
                GLS_residual_trg(loc_mat, loc_rhs);

                for (int i = 0; i < 3; ++i) {
                    for (int j = 0; j < 3; ++j) {
                        std::cout << loc_mat(i,j) << " " << corresp_loc[i] << " " << corresp_loc[j] << std::endl;
                        cell_mat(corresp_loc[i],corresp_loc[j]) += loc_mat(i,j);
                    }
                    cell_rhs(corresp_loc[i]) += loc_rhs[i];
                }
            }

//                      std::cout << " \n system matrix for triangles : " << std::endl;
//                      for (int i = 0; i < 18; ++i) {
//                          std::cout << cell_mat(i,0) << " "  << cell_mat(i,1) << " " << cell_mat(i,2) << " "
//                                    << cell_mat(i,3) << " "  << cell_mat(i,4) << " " << cell_mat(i,5) << " "
//                                    << cell_mat(i,6) << " "  << cell_mat(i,7) << " " << cell_mat(i,8) << " "
//                                    << cell_mat(i,9) << " "  << cell_mat(i,10) << " " << cell_mat(i,11) << " "
//                                    << cell_mat(i,12) << " "  << cell_mat(i,13) << " " << cell_mat(i,14) << " "
//                                    << cell_mat(i,15) << " "  << cell_mat(i,16) << " " << cell_mat(i,17) << " "
//                                    << std::endl;}
//                      std::cout << " \n system rhs for triangles : " << std::endl;
//                      std::cout << cell_rhs(0) << " "  << cell_rhs(1) << " " << cell_rhs(2) << " "
//                                << cell_rhs(3) << " "  << cell_rhs(4) << " " << cell_rhs(5) << " "
//                                << cell_rhs(6) << " "  << cell_rhs(7) << " " << cell_rhs(8) << " "
//                                << cell_rhs(9) << " "  << cell_rhs(10) << " " << cell_rhs(11) << " "
//                                << cell_rhs(12) << " "  << cell_rhs(13) << " " << cell_rhs(14) << " "
//                                << cell_rhs(15) << " "  << cell_rhs(16) << " " << cell_rhs(17) << " "
//                                << std::endl;

            // We now apply the boundary conditions to the points that are on the boundary or in the solid //

            // we first do it for the dofs that are associated to a vertex strictly located in the solid
            Tensor<1, dim>      v_solid;

            for (unsigned int i = 0; i < dofs_per_cell; ++i) {
                if (No_pts_solid[i/dofs_per_vertex]==solid)
                {
                    if (!(i%dofs_per_vertex)) // this is done so that we dont calculate several time the same vector of velocity
                        ib_combiner.velocity(dofs_points[i], v_solid);

                    for (unsigned int j = 0; j < dofs_per_cell; ++j) {
                        if (i==j)
                            cell_mat(i,j)=1;
                        else {
                            cell_mat(i,j)=0;
                            cell_mat(j,i)=0;
                        }
                    }

                    if (i%dofs_per_vertex==dim) // the dof is a pressure one
                    {
                        cell_rhs(i) = 0; // in the solid, there is no pressure
                    }
                    else {
                        if (initial_step)
                            cell_rhs(i) = v_solid[i%dofs_per_vertex]; // i%dofs_per_vertex is here the component of the speed we want to get
                        else {
                            cell_rhs(i) = 0;
                        }
                    }
                }
            }

            // we then apply the boundary conditions to the lines associated to dofs worn by boundary points
            // those dofs are the last in the matrix
            unsigned int dof_index;
            for (unsigned int i = 0; i < 2*dofs_per_vertex; ++i) {
                dof_index = 4*dofs_per_vertex+i;

                if (dof_index%dofs_per_vertex==0)
                    ib_combiner.velocity(boundary_points[i/dofs_per_vertex], v_solid);

                if (i%3!=2){
                    for (unsigned int j = 0; j < dofs_per_cell; ++j) {
                        if (dof_index==j)
                            cell_mat(dof_index, j) = 1;
                        else {
                            cell_mat(dof_index, j) = 0;
                        }
                    }

                }
                if (!(dof_index%dofs_per_vertex==dim)&&(initial_step)) // we only set the velocity on the boundary, we dont set any value for the pressure on the boundary
                    cell_rhs(dof_index) = v_solid[dof_index%dofs_per_vertex];
                else {
                    cell_rhs(dof_index) = 0;
                }
            }

            // We then condensate the system to make the boundary points not explicitly appear in the system //

            condensate(18, 12, cell_mat, local_matrix, cell_rhs,  local_rhs);
        }


      cell->get_dof_indices (local_dof_indices);
      const AffineConstraints<double> &constraints_used = initial_step ? nonzero_constraints : zero_constraints;
      if (assemble_matrix)
        {
          constraints_used.distribute_local_to_global(local_matrix,
                                                      local_rhs,
                                                      local_dof_indices,
                                                      system_matrix,
                                                      system_rhs);
        }
      else
        {
          constraints_used.distribute_local_to_global(local_rhs,
                                                      local_dof_indices,
                                                      system_rhs);
        }
    }
}

template <int dim>
void DirectSteadyNavierStokes<dim>::assemble_system(const bool initial_step)
{
  assemble(initial_step, true);
}
template <int dim>
void DirectSteadyNavierStokes<dim>::assemble_rhs(const bool initial_step)
{
  assemble(initial_step, false);
}

template <int dim>
void DirectSteadyNavierStokes<dim>::solve (const bool initial_step)
{
  const AffineConstraints<double> &constraints_used = initial_step ? nonzero_constraints : zero_constraints;
  SparseDirectUMFPACK direct;
  direct.initialize(system_matrix);
  direct.vmult(newton_update,system_rhs);
  constraints_used.distribute(newton_update);
}

template <int dim>
void DirectSteadyNavierStokes<dim>::refine_mesh ()
{
    Vector<float> estimated_error_per_cell (triangulation.n_active_cells());
    FEValuesExtractors::Vector velocity(0);
    KellyErrorEstimator<dim>::estimate (dof_handler,
                                        QGauss<dim-1>(degreeIntegration_+1),
                                        typename std::map<types::boundary_id, const Function<dim, double> *>(),
                                        present_solution,
                                        estimated_error_per_cell,
                                        fe.component_mask(velocity));
    GridRefinement::refine_and_coarsen_fixed_number (triangulation,
                                                     estimated_error_per_cell,
                                                     0.15, 0.0);
    triangulation.prepare_coarsening_and_refinement();
    SolutionTransfer<dim, BlockVector<double> > solution_transfer(dof_handler);
    solution_transfer.prepare_for_coarsening_and_refinement(present_solution);
    triangulation.execute_coarsening_and_refinement ();
    setup_dofs();
    BlockVector<double> tmp (dofs_per_block);
    solution_transfer.interpolate(present_solution, tmp);
    nonzero_constraints.distribute(tmp);
    initialize_system();
    present_solution = tmp;
}

template <int dim>
void DirectSteadyNavierStokes<dim>::refine_mesh_uniform ()
{
    SolutionTransfer<dim, BlockVector<double> > solution_transfer(dof_handler);
    solution_transfer.prepare_for_coarsening_and_refinement(present_solution);
    triangulation.refine_global(1);
    setup_dofs();
    BlockVector<double> tmp (dofs_per_block);
    solution_transfer.interpolate(present_solution, tmp);
    nonzero_constraints.distribute(tmp);
    initialize_system();
    present_solution = tmp;
}


template <int dim>
void DirectSteadyNavierStokes<dim>::newton_iteration(const double tolerance,
                                                   const unsigned int max_iteration,
                                                   const bool  is_initial_step,
                                                   const bool  /*output_result*/)
{
  double current_res;
  double last_res;
  bool   first_step = is_initial_step;
    {
      unsigned int outer_iteration = 0;
      last_res = 1.0;
      current_res = 1.0;
      while ((first_step || (current_res > tolerance)) && outer_iteration < max_iteration)
        {
          if (first_step)
            {
              initialize_system();
              evaluation_point = present_solution;
              assemble_system(first_step);
              current_res = system_rhs.l2_norm();
              std::cout  << "Newton iteration: " << outer_iteration << "  - Residual:  " << current_res << "\n\n\n\n\n\n" << std::endl;
              solve(first_step);
              present_solution = newton_update;
              nonzero_constraints.distribute(present_solution);
              first_step = false;
              evaluation_point = present_solution;
              assemble_rhs(first_step);
              current_res = system_rhs.l2_norm();
              last_res = current_res;
            }
          else
            {
              std::cout  << "Newton iteration: " << outer_iteration << "  - Residual:  " << current_res  << "\n\n\n\n\n\n" << std::endl;
              evaluation_point = present_solution;
              assemble_system(first_step);
              solve(first_step);
              for (double alpha = 1.0; alpha > 1e-3; alpha *= 0.5)
                {
                  evaluation_point = present_solution;
                  evaluation_point.add(alpha, newton_update);
                  nonzero_constraints.distribute(evaluation_point);
                  assemble_rhs(first_step);
                  current_res = system_rhs.l2_norm();
                  std::cout << "\t\talpha = " << std::setw(6) << alpha << std::setw(0)
                            << " res = " << current_res << std::endl;
                  if (current_res < last_res)
                    break;
                }
              {
                present_solution = evaluation_point;
                last_res = current_res;
              }
            }
          ++outer_iteration;

        }
    }
}

template <int dim>
void DirectSteadyNavierStokes<dim>::output_results (std::string case_name, const unsigned int cycle) const
{
    std::vector<std::string> solution_names (dim, "velocity");
    solution_names.push_back ("pressure");

    std::vector<DataComponentInterpretation::DataComponentInterpretation>
    data_component_interpretation
    (dim, DataComponentInterpretation::component_is_part_of_vector);
    data_component_interpretation
    .push_back (DataComponentInterpretation::component_is_scalar);

    DataOut<dim> data_out;
    data_out.attach_dof_handler (dof_handler);
    data_out.add_data_vector (present_solution, solution_names, DataOut<dim>::type_dof_data, data_component_interpretation);
    data_out.build_patches (1);

    std::string filenamesolution = case_name;
    filenamesolution += ('0' + cycle);
    filenamesolution += ".vtk";

    std::cout << "Writing file : " << filenamesolution << std::endl;
    std::ofstream outputSolution (filenamesolution.c_str());

    data_out.write_vtk (outputSolution);
}

//Find the l2 norm of the error between the finite element sol'n and the exact sol'n
template <int dim>
void DirectSteadyNavierStokes<dim>::calculateL2Error()
{

    QGauss<dim>  quadrature_formula(fe.degree+2);
    FEValues<dim> fe_values (fe, quadrature_formula,
                             update_values   | update_gradients |
                             update_quadrature_points | update_JxW_values);

    const FEValuesExtractors::Vector velocities (0);
    const FEValuesExtractors::Scalar pressure (dim);


    const unsigned int   			dofs_per_cell = fe.dofs_per_cell;         // This gives you dofs per cell
    std::vector<types::global_dof_index> local_dof_indices (dofs_per_cell); //  Local connectivity

    const unsigned int   n_q_points    = quadrature_formula.size();
    double l2errorU=0.;

    std::vector<Vector<double> > q_exactSol (n_q_points, Vector<double>(dim+1));


    std::vector<Tensor<1,dim> > local_velocity_values (n_q_points);
    std::vector<double > local_pressure_values (n_q_points);

    double maxPressure=-DBL_MAX;
    // Get the maximal value of the pressure
    for (auto icell=dof_handler.begin_active(); icell!=dof_handler.end(); ++icell)
    {
        fe_values.reinit (icell);
        fe_values[pressure].get_function_values(present_solution,
                                                local_pressure_values);

        for (unsigned int i=0 ; i<local_pressure_values.size() ; ++i)
        {
            maxPressure=std::max(local_pressure_values[i],maxPressure);
        }

    }

    //loop over elements
    typename DoFHandler<dim>::active_cell_iterator
            cell = dof_handler.begin_active(),
            endc = dof_handler.end();
    for (; cell!=endc; ++cell)
    {
        fe_values.reinit (cell);
        fe_values[velocities].get_function_values (present_solution,
                                                   local_velocity_values);
        fe_values[pressure].get_function_values(present_solution,
                                               local_pressure_values);

        //Retrieve the effective "connectivity matrix" for this element
        cell->get_dof_indices (local_dof_indices);

        // Get the exact solution at all gauss points
        exact_solution->vector_value_list(fe_values.get_quadrature_points(),
                                            q_exactSol);

        for(unsigned int q=0; q<n_q_points; q++)
        {
            //Find the values of x and u_h (the finite element solution) at the quadrature points
            double ux_sim=local_velocity_values[q][0];
            double ux_exact=q_exactSol[q][0];

            double uy_sim=local_velocity_values[q][1];
            double uy_exact=q_exactSol[q][1];

            l2errorU += (ux_sim-ux_exact)*(ux_sim-ux_exact) * fe_values.JxW(q);
            l2errorU += (uy_sim-uy_exact)*(uy_sim-uy_exact) * fe_values.JxW(q);
        }
    }
    std::cout << "L2Error is : " << std::sqrt(l2errorU) << std::endl;
    L2ErrorU_.push_back(std::sqrt(l2errorU));
}


template<int dim>
void DirectSteadyNavierStokes<dim>::runMMS()
{
  std::cout << "**********************************************" << std::endl;
  std::cout << "* Method of Manufactured Solutions           *" << std::endl;
  std::cout << "**********************************************" << std::endl;
  simulationCase_=MMS;
  make_cube_grid(initialSize_);
  exact_solution = new ExactSolutionMMS<dim>;
  forcing_function = new MMSSineForcingFunction<dim>;
  viscosity_=1.;
  setup_dofs();

  for (unsigned int cycle =0; cycle < 5 ; cycle++)
  {
    if (cycle !=0) refine_mesh_uniform();
    newton_iteration(1.e-6, 5, true, true);
    output_results ("MMS-",cycle);
    calculateL2Error();

  }
  std::ofstream output_file("./L2Error.dat");
  for (unsigned int i=0 ; i < L2ErrorU_.size() ; ++i)
  {
    output_file << i+initialSize_ << " " << L2ErrorU_[i] << std::endl;
  }
  output_file.close();
}

template<int dim>
void DirectSteadyNavierStokes<dim>::runCouetteX()
{
  std::cout << "**********************************************" << std::endl;
  std::cout << "* Couette X                                  *" << std::endl;
  std::cout << "**********************************************" << std::endl;
  simulationCase_=CouetteX;
  GridGenerator::hyper_cube (triangulation, 0, 1,true);
  forcing_function = new NoForce<dim>;
  triangulation.refine_global (2);
  exact_solution = new ExactSolutionCouetteX<dim>;
  viscosity_=1.;
  setup_dofs();

  newton_iteration(1.e-6, 5, true, true);
  output_results ("Couette-X-",0);
  calculateL2Error();
}

template<int dim>
void DirectSteadyNavierStokes<dim>::runCouetteXPerturbedMesh()
{
  std::cout << "**********************************************" << std::endl;
  std::cout << "* Couette X  - Perturbed Mesh                *" << std::endl;
  std::cout << "**********************************************" << std::endl;
  simulationCase_=CouetteX;
  GridGenerator::hyper_cube (triangulation, 0, 1,true);
  forcing_function = new NoForce<dim>;
  triangulation.refine_global (3);

  // Generate the IB composer
  Point<2> center1(0.751,0);
  Tensor<1,2> velocity;
  velocity[0]=0.; velocity[1]=1.;
  Tensor<1,2> normal;
  normal[0]=-1; normal[1]=0;
  double T_scal=1;
  // Add a shape to it
  IBLevelSetPlane<2> plane(center1, normal,velocity, T_scal);
  std::vector<IBLevelSetFunctions<2> *> ib_functions;
  ib_functions.push_back(&plane);
  ib_combiner.setFunctions(ib_functions);

  GridTools::distort_random(0.3,triangulation);
  exact_solution = new ExactSolutionCouetteX<dim>;
  viscosity_=1.;
  setup_dofs();

  newton_iteration(1.e-6, 5, true, true);
  output_results ("Couette-X-Perturbed-",0);
  calculateL2Error();
}


template<int dim>
void DirectSteadyNavierStokes<dim>::runTaylorCouette()
{
    viscosity_=10;
    GridIn<dim> grid_in;
    grid_in.attach_triangulation (triangulation);
    std::ifstream input_file("taylorcouette.msh");

    grid_in.read_msh(input_file);


    static const SphericalManifold<dim> boundary;

    triangulation.set_all_manifold_ids_on_boundary(0);
    triangulation.set_manifold (0, boundary);

    forcing_function = new NoForce<dim>;
    exact_solution = new ExactSolutionTaylorCouette<dim>;
    setup_dofs();




    for (int cycle=0 ; cycle < 4 ; cycle++)
    {
        if (cycle !=0)  refine_mesh();
        newton_iteration(1.e-10, 50, true, true);
        output_results (cycle);
        calculateL2Error();
    }

    std::ofstream output_file("./L2Error.dat");
    for (unsigned int i=0 ; i < L2ErrorU_.size() ; ++i)
    {
        output_file << i+initialSize_ << " " << L2ErrorU_[i] << std::endl;
    }
    output_file.close();
}

template<int dim>
void DirectSteadyNavierStokes<dim>::runCouetteIBX()
{
  std::cout << "**********************************************" << std::endl;
  std::cout << "* Couette IB X                                  *" << std::endl;
  std::cout << "**********************************************" << std::endl;
  simulationCase_=CouetteX;
  GridGenerator::hyper_cube (triangulation, 0, 1,true);
  forcing_function = new NoForce<dim>;
  triangulation.refine_global (1);

  // Generate the IB composer
  Point<2> center1(0.751,0);
  Tensor<1,2> velocity;
  velocity[0]=0.; velocity[1]=1.;
  Tensor<1,2> normal;
  normal[0]=-1; normal[1]=0;
  double T_scal=1;
  // Add a shape to it
  IBLevelSetPlane<2> plane(center1, normal,velocity, T_scal);
  std::vector<IBLevelSetFunctions<2> *> ib_functions;
  ib_functions.push_back(&plane);
  ib_combiner.setFunctions(ib_functions);

  exact_solution = new ExactSolutionCouetteX<dim>;
  viscosity_=1.;
  setup_dofs();

  newton_iteration(1.e-6, 5, true, true);
  output_results ("Couette-X-IB-",0);
  calculateL2Error();
}



int main ()
{
    try
    {

//      {
//        DirectSteadyNavierStokes<2> problem_2d(1,1);
//        problem_2d.runCouetteX();
//      }
      {
        DirectSteadyNavierStokes<2> problem_2d(1,1);
        problem_2d.runCouetteXPerturbedMesh();
      }
//      {
//        DirectSteadyNavierStokes<2> problem_2d(1,1);
//        problem_2d.runCouetteIBX();
//      }
//      {
//      DirectSteadyNavierStokes<2> problem_2d(1,1);
//      problem_2d.runMMS();
//      }
    }
    catch (std::exception &exc)
    {
        std::cerr << std::endl << std::endl
                  << "----------------------------------------------------"
                  << std::endl;
        std::cerr << "Exception on processing: " << std::endl
                  << exc.what() << std::endl
                  << "Aborting!" << std::endl
                  << "----------------------------------------------------"
                  << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << std::endl << std::endl
                  << "----------------------------------------------------"
                  << std::endl;
        std::cerr << "Unknown exception!" << std::endl
                  << "Aborting!" << std::endl
                  << "----------------------------------------------------"
                  << std::endl;
        return 1;
    }
    return 0;
}
