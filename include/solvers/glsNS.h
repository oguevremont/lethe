﻿/* ---------------------------------------------------------------------
 *
 * Copyright (C) 2019 - 2019 by the Lethe authors
 *
 * This file is part of the Lethe library
 *
 * The Lethe library is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE at
 * the top level of the Lethe distribution.
 *
 * ---------------------------------------------------------------------

 *
 * Author: Bruno Blais, Polytechnique Montreal, 2019-
 */

#ifndef LETHE_GLSNS_H
#define LETHE_GLSNS_H

// Dealii Includes

// Base
#include <deal.II/base/conditional_ostream.h>
#include <deal.II/base/convergence_table.h>
#include <deal.II/base/function.h>
#include <deal.II/base/index_set.h>
#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/table_handler.h>
#include <deal.II/base/timer.h>
#include <deal.II/base/utilities.h>

// Lac
#include <deal.II/lac/affine_constraints.h>
#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/solver_bicgstab.h>
#include <deal.II/lac/solver_gmres.h>
#include <deal.II/lac/sparse_ilu.h>
#include <deal.II/lac/sparse_matrix.h>
#include <deal.II/lac/sparsity_tools.h>
#include <deal.II/lac/vector.h>

// Lac - Trilinos includes
#include <deal.II/lac/trilinos_parallel_block_vector.h>
#include <deal.II/lac/trilinos_precondition.h>
#include <deal.II/lac/trilinos_solver.h>
#include <deal.II/lac/trilinos_sparse_matrix.h>
#include <deal.II/lac/trilinos_vector.h>

// Grid
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_in.h>
#include <deal.II/grid/grid_out.h>
#include <deal.II/grid/grid_refinement.h>
#include <deal.II/grid/grid_tools.h>
#include <deal.II/grid/manifold_lib.h>
#include <deal.II/grid/tria.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>

// Dofs
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_tools.h>

// Fe
#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/fe/mapping_q.h>

// Numerics
#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/error_estimator.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/solution_transfer.h>
#include <deal.II/numerics/vector_tools.h>

// Distributed
#include <deal.II/distributed/grid_refinement.h>
#include <deal.II/distributed/solution_transfer.h>

// Lethe Includes
#include <core/bdf.h>
#include <core/parameters.h>
#include <core/pvdhandler.h>
#include <core/simulationcontrol.h>

#include "boundaryconditions.h"
#include "manifolds.h"
#include "navier_stokes_base.h"
#include "navierstokessolverparameters.h"
#include "postprocessors.h"

// Std
#include <fstream>
#include <iostream>

using namespace dealii;

/**
 * A solver class for the Navier-Stokes equation using GLS stabilization
 *
 * @tparam dim An integer that denotes the dimension of the space in which
 * the flow is solved
 *
 * @ingroup solvers
 * @author Bruno Blais, 2019
 */

template <int dim>
class GLSNavierStokesSolver
  : public NavierStokesBase<dim, TrilinosWrappers::MPI::Vector>
{
public:
  GLSNavierStokesSolver(NavierStokesSolverParameters<dim> &nsparam,
                        const unsigned int                 degreeVelocity,
                        const unsigned int                 degreePressure);
  ~GLSNavierStokesSolver();

  void
  solve();

protected:
  void
  refine_mesh();
  void
  setup_dofs();
  void
  set_initial_condition(Parameters::InitialConditionType initial_condition_type,
                        bool                             restart = false);
  void
  postprocess(bool firstIter);

  void
  set_solution_vector(double value);

  void
  make_cube_grid(int refinementLevel);

private:
  template <bool                                              assemble_matrix,
            Parameters::SimulationControl::TimeSteppingMethod scheme>
  void
  assembleGLS();

  void
  assemble_system();
  void
  assemble_rhs();

  void
  assemble_L2_projection();

  void
  newton_iteration(const bool is_initial_step);

  void
  refine_mesh_Kelly();
  void
  refine_mesh_uniform();

  /**
   * Checkpointing reader of the solutions vector of the GLS solver
   */
  void
  read_checkpoint();

  void
  set_nodal_values();

  /**
   * Interface for the solver for the linear system of equations
   */

  void
  solve_linear_system(bool   initial_step,
                      double relative_residual,
                      double minimum_residual); // Interface function

  virtual void
  solve_non_linear_system(const bool first_iteration)
  {
    newton_iteration(first_iteration);
  }

  /**
   * GMRES solver with ILU(N) preconditioning
   */
  void
  solve_system_GMRES(bool   initial_step,
                     double absolute_residual,
                     double relative_residual);

  /**
   * BiCGStab solver with ILU(N) preconditioning
   */
  void
  solve_system_BiCGStab(bool   initial_step,
                        double absolute_residual,
                        double relative_residual);

  /**
   * AMG preconditioner with ILU smoother and coarsener and GMRES final solver
   */
  void
  solve_system_AMG(bool   initial_step,
                   double absolute_residual,
                   double relative_residual);

  /**
   * Checkpointing writer of the solutions vector of the GLS solver
   */
  void
  write_checkpoint();

  /**
   * Members
   */
private:
  IndexSet locally_owned_dofs;
  IndexSet locally_relevant_dofs;

  AffineConstraints<double> zero_constraints;
  AffineConstraints<double> nonzero_constraints;

  SparsityPattern                sparsity_pattern;
  TrilinosWrappers::SparseMatrix system_matrix;

  TrilinosWrappers::MPI::Vector newton_update;
  TrilinosWrappers::MPI::Vector system_rhs;
  TrilinosWrappers::MPI::Vector evaluation_point;
  TrilinosWrappers::MPI::Vector local_evaluation_point;

  const bool   SUPG        = true;
  const double GLS_u_scale = 1;
};

// Constructor for class GLSNavierStokesSolver
template <int dim>
GLSNavierStokesSolver<dim>::GLSNavierStokesSolver(
  NavierStokesSolverParameters<dim> &p_nsparam,
  const unsigned int                 p_degreeVelocity,
  const unsigned int                 p_degreePressure)
  : NavierStokesBase<dim, TrilinosWrappers::MPI::Vector>(p_nsparam,
                                                         p_degreeVelocity,
                                                         p_degreePressure)
{}

template <int dim>
GLSNavierStokesSolver<dim>::~GLSNavierStokesSolver()
{
  this->dof_handler.clear();
}

template <int dim>
void
GLSNavierStokesSolver<dim>::make_cube_grid(int refinementLevel)
{
  GridGenerator::hyper_cube(this->triangulation, -1, 1);
  this->triangulation.refine_global(refinementLevel);
}

template <int dim>
void
GLSNavierStokesSolver<dim>::set_solution_vector(double value)
{
  this->present_solution = value;
}

template <int dim>
void
GLSNavierStokesSolver<dim>::setup_dofs()
{
  TimerOutput::Scope t(this->computing_timer, "setup_dofs");

  system_matrix.clear();

  this->dof_handler.distribute_dofs(this->fe);
  DoFRenumbering::Cuthill_McKee(this->dof_handler);

  locally_owned_dofs = this->dof_handler.locally_owned_dofs();
  DoFTools::extract_locally_relevant_dofs(this->dof_handler,
                                          locally_relevant_dofs);

  const MappingQ<dim>        mapping(this->degreeVelocity_,
                              this->nsparam.femParameters.qmapping_all);
  FEValuesExtractors::Vector velocities(0);

  // Non-zero constraints
  {
    nonzero_constraints.clear();

    DoFTools::make_hanging_node_constraints(this->dof_handler,
                                            nonzero_constraints);
    for (unsigned int i_bc = 0; i_bc < this->nsparam.boundaryConditions.size;
         ++i_bc)
      {
        if (this->nsparam.boundaryConditions.type[i_bc] ==
            BoundaryConditions::noslip)
          {
            VectorTools::interpolate_boundary_values(
              mapping,
              this->dof_handler,
              this->nsparam.boundaryConditions.id[i_bc],
              ZeroFunction<dim>(dim + 1),
              nonzero_constraints,
              this->fe.component_mask(velocities));
          }
        else if (this->nsparam.boundaryConditions.type[i_bc] ==
                 BoundaryConditions::slip)
          {
            std::set<types::boundary_id> no_normal_flux_boundaries;
            no_normal_flux_boundaries.insert(
              this->nsparam.boundaryConditions.id[i_bc]);
            VectorTools::compute_no_normal_flux_constraints(
              this->dof_handler,
              0,
              no_normal_flux_boundaries,
              nonzero_constraints);
          }
        else if (this->nsparam.boundaryConditions.type[i_bc] ==
                 BoundaryConditions::function)
          {
            VectorTools::interpolate_boundary_values(
              mapping,
              this->dof_handler,
              this->nsparam.boundaryConditions.id[i_bc],
              FunctionDefined<dim>(
                &this->nsparam.boundaryConditions.bcFunctions[i_bc].u,
                &this->nsparam.boundaryConditions.bcFunctions[i_bc].v,
                &this->nsparam.boundaryConditions.bcFunctions[i_bc].w),
              nonzero_constraints,
              this->fe.component_mask(velocities));
          }

        else if (this->nsparam.boundaryConditions.type[i_bc] ==
                 BoundaryConditions::periodic)
          {
            DoFTools::make_periodicity_constraints<DoFHandler<dim>>(
              this->dof_handler,
              this->nsparam.boundaryConditions.id[i_bc],
              this->nsparam.boundaryConditions.periodic_id[i_bc],
              this->nsparam.boundaryConditions.periodic_direction[i_bc],
              nonzero_constraints);
          }
      }
  }
  nonzero_constraints.close();

  {
    zero_constraints.clear();
    DoFTools::make_hanging_node_constraints(this->dof_handler,
                                            zero_constraints);

    for (unsigned int i_bc = 0; i_bc < this->nsparam.boundaryConditions.size;
         ++i_bc)
      {
        if (this->nsparam.boundaryConditions.type[i_bc] ==
            BoundaryConditions::slip)
          {
            std::set<types::boundary_id> no_normal_flux_boundaries;
            no_normal_flux_boundaries.insert(
              this->nsparam.boundaryConditions.id[i_bc]);
            VectorTools::compute_no_normal_flux_constraints(
              this->dof_handler,
              0,
              no_normal_flux_boundaries,
              zero_constraints);
          }
        else if (this->nsparam.boundaryConditions.type[i_bc] ==
                 BoundaryConditions::periodic)
          {
            DoFTools::make_periodicity_constraints<DoFHandler<dim>>(
              this->dof_handler,
              this->nsparam.boundaryConditions.id[i_bc],
              this->nsparam.boundaryConditions.periodic_id[i_bc],
              this->nsparam.boundaryConditions.periodic_direction[i_bc],
              zero_constraints);
          }
        else // if(nsparam.boundaryConditions.boundaries[i_bc].type==Parameters::noslip
             // || Parameters::function)
          {
            VectorTools::interpolate_boundary_values(
              mapping,
              this->dof_handler,
              this->nsparam.boundaryConditions.id[i_bc],
              ZeroFunction<dim>(dim + 1),
              zero_constraints,
              this->fe.component_mask(velocities));
          }
      }
  }
  zero_constraints.close();

  this->present_solution.reinit(locally_owned_dofs,
                                locally_relevant_dofs,
                                this->mpi_communicator);
  this->solution_m1.reinit(locally_owned_dofs,
                           locally_relevant_dofs,
                           this->mpi_communicator);
  this->solution_m2.reinit(locally_owned_dofs,
                           locally_relevant_dofs,
                           this->mpi_communicator);
  this->solution_m3.reinit(locally_owned_dofs,
                           locally_relevant_dofs,
                           this->mpi_communicator);

  newton_update.reinit(locally_owned_dofs, this->mpi_communicator);
  system_rhs.reinit(locally_owned_dofs, this->mpi_communicator);
  local_evaluation_point.reinit(locally_owned_dofs, this->mpi_communicator);

  DynamicSparsityPattern dsp(locally_relevant_dofs);
  DoFTools::make_sparsity_pattern(this->dof_handler,
                                  dsp,
                                  nonzero_constraints,
                                  false);
  SparsityTools::distribute_sparsity_pattern(
    dsp,
    this->dof_handler.n_locally_owned_dofs_per_processor(),
    this->mpi_communicator,
    locally_relevant_dofs);
  system_matrix.reinit(locally_owned_dofs,
                       locally_owned_dofs,
                       dsp,
                       this->mpi_communicator);

  this->globalVolume_ = GridTools::volume(this->triangulation);

  this->pcout << "   Number of active cells:       "
              << this->triangulation.n_global_active_cells() << std::endl
              << "   Number of degrees of freedom: "
              << this->dof_handler.n_dofs() << std::endl;
  this->pcout << "   Volume of triangulation:      " << this->globalVolume_
              << std::endl;
}

template <int dim>
template <bool                                              assemble_matrix,
          Parameters::SimulationControl::TimeSteppingMethod scheme>
void
GLSNavierStokesSolver<dim>::assembleGLS()
{
  if (assemble_matrix)
    system_matrix = 0;
  system_rhs = 0;

  double         viscosity_ = this->nsparam.physicalProperties.viscosity;
  Function<dim> *l_forcing_function = this->forcing_function;

  QGauss<dim>                      quadrature_formula(this->degreeQuadrature_);
  const MappingQ<dim>              mapping(this->degreeVelocity_,
                              this->nsparam.femParameters.qmapping_all);
  FEValues<dim>                    fe_values(mapping,
                          this->fe,
                          quadrature_formula,
                          update_values | update_quadrature_points |
                            update_JxW_values | update_gradients |
                            update_hessians);
  const unsigned int               dofs_per_cell = this->fe.dofs_per_cell;
  const unsigned int               n_q_points    = quadrature_formula.size();
  const FEValuesExtractors::Vector velocities(0);
  const FEValuesExtractors::Scalar pressure(dim);
  FullMatrix<double>               local_matrix(dofs_per_cell, dofs_per_cell);
  Vector<double>                   local_rhs(dofs_per_cell);
  std::vector<Vector<double>> rhs_force(n_q_points, Vector<double>(dim + 1));
  std::vector<types::global_dof_index> local_dof_indices(dofs_per_cell);
  std::vector<Tensor<1, dim>>          present_velocity_values(n_q_points);
  std::vector<Tensor<2, dim>>          present_velocity_gradients(n_q_points);
  std::vector<double>                  present_pressure_values(n_q_points);
  std::vector<Tensor<1, dim>>          present_pressure_gradients(n_q_points);
  std::vector<Tensor<1, dim>>          present_velocity_laplacians(n_q_points);
  std::vector<Tensor<2, dim>>          present_velocity_hess(n_q_points);

  Tensor<1, dim> force;

  std::vector<double>         div_phi_u(dofs_per_cell);
  std::vector<Tensor<1, dim>> phi_u(dofs_per_cell);
  std::vector<Tensor<3, dim>> hess_phi_u(dofs_per_cell);
  std::vector<Tensor<1, dim>> laplacian_phi_u(dofs_per_cell);
  std::vector<Tensor<2, dim>> grad_phi_u(dofs_per_cell);
  std::vector<double>         phi_p(dofs_per_cell);
  std::vector<Tensor<1, dim>> grad_phi_p(dofs_per_cell);

  // Get the BDF coefficients
  Vector<double> alpha_bdf;

  if (scheme == Parameters::SimulationControl::bdf1)
    alpha_bdf = bdf_coefficients(1, this->simulationControl.getTimeSteps());

  if (scheme == Parameters::SimulationControl::bdf2)
    alpha_bdf = bdf_coefficients(2, this->simulationControl.getTimeSteps());

  if (scheme == Parameters::SimulationControl::bdf3)
    alpha_bdf = bdf_coefficients(3, this->simulationControl.getTimeSteps());

  double sdt = 1. / this->simulationControl.getTimeSteps()[0];

  // Values at previous time step for backward Euler scheme
  std::vector<Tensor<1, dim>> p1_velocity_values(n_q_points);
  std::vector<Tensor<1, dim>> p2_velocity_values(n_q_points);
  std::vector<Tensor<1, dim>> p3_velocity_values(n_q_points);
  std::vector<Tensor<1, dim>> p4_velocity_values(n_q_points);

  // Element size
  double h;

  typename DoFHandler<dim>::active_cell_iterator cell = this->dof_handler
                                                          .begin_active(),
                                                 endc = this->dof_handler.end();
  for (; cell != endc; ++cell)
    {
      if (cell->is_locally_owned())
        {
          if (dim == 2)
            h = std::sqrt(4. * cell->measure() / M_PI) / this->degreeVelocity_;
          else if (dim == 3)
            h =
              pow(6 * cell->measure() / M_PI, 1. / 3.) / this->degreeVelocity_;

          fe_values.reinit(cell);
          local_matrix = 0;

          local_rhs = 0;
          fe_values[velocities].get_function_values(evaluation_point,
                                                    present_velocity_values);
          fe_values[velocities].get_function_gradients(
            evaluation_point, present_velocity_gradients);
          fe_values[pressure].get_function_values(evaluation_point,
                                                  present_pressure_values);
          fe_values[pressure].get_function_gradients(
            evaluation_point, present_pressure_gradients);
          fe_values[velocities].get_function_laplacians(
            evaluation_point, present_velocity_laplacians);

          if (l_forcing_function)
            l_forcing_function->vector_value_list(
              fe_values.get_quadrature_points(), rhs_force);

          if (scheme != Parameters::SimulationControl::steady)
            fe_values[velocities].get_function_values(this->solution_m1,
                                                      p1_velocity_values);
          if (scheme == Parameters::SimulationControl::bdf2 ||
              scheme == Parameters::SimulationControl::bdf3)
            fe_values[velocities].get_function_values(this->solution_m2,
                                                      p2_velocity_values);
          if (scheme == Parameters::SimulationControl::bdf3)
            fe_values[velocities].get_function_values(this->solution_m3,
                                                      p3_velocity_values);

          for (unsigned int q = 0; q < n_q_points; ++q)
            {
              const double u_mag =
                std::max(present_velocity_values[q].norm(), 1e-3 * GLS_u_scale);
              double tau;
              if (scheme == Parameters::SimulationControl::steady)
                tau = 1. / std::sqrt(std::pow(2. * u_mag / h, 2) +
                                     9 * std::pow(4 * viscosity_ / (h * h), 2));
              else
                tau = 1. /
                      std::sqrt(std::pow(sdt, 2) + std::pow(2. * u_mag / h, 2) +
                                9 * std::pow(4 * viscosity_ / (h * h), 2));

              for (unsigned int k = 0; k < dofs_per_cell; ++k)
                {
                  div_phi_u[k]  = fe_values[velocities].divergence(k, q);
                  grad_phi_u[k] = fe_values[velocities].gradient(k, q);
                  phi_u[k]      = fe_values[velocities].value(k, q);
                  hess_phi_u[k] = fe_values[velocities].hessian(k, q);
                  phi_p[k]      = fe_values[pressure].value(k, q);
                  grad_phi_p[k] = fe_values[pressure].gradient(k, q);

                  for (int d = 0; d < dim; ++d)
                    laplacian_phi_u[k][d] = trace(hess_phi_u[k][d]);
                }

              // Establish the force vector
              for (int i = 0; i < dim; ++i)
                {
                  const unsigned int component_i =
                    this->fe.system_to_component_index(i).first;
                  force[i] = rhs_force[q](component_i);
                }

              auto strong_residual =
                present_velocity_gradients[q] * present_velocity_values[q] +
                present_pressure_gradients[q] -
                viscosity_ * present_velocity_laplacians[q] - force;

              if (scheme == Parameters::SimulationControl::bdf1)
                strong_residual += alpha_bdf[0] * present_velocity_values[q] +
                                   alpha_bdf[1] * p1_velocity_values[q];

              if (scheme == Parameters::SimulationControl::bdf2)
                strong_residual += alpha_bdf[0] * present_velocity_values[q] +
                                   alpha_bdf[1] * p1_velocity_values[q] +
                                   alpha_bdf[2] * p2_velocity_values[q];

              if (scheme == Parameters::SimulationControl::bdf3)
                strong_residual += alpha_bdf[0] * present_velocity_values[q] +
                                   alpha_bdf[1] * p1_velocity_values[q] +
                                   alpha_bdf[2] * p2_velocity_values[q] +
                                   alpha_bdf[3] * p3_velocity_values[q];

              if (assemble_matrix)
                {
                  for (unsigned int j = 0; j < dofs_per_cell; ++j)
                    {
                      auto strong_jac =
                        (present_velocity_gradients[q] * phi_u[j] +
                         grad_phi_u[j] * present_velocity_values[q] +
                         grad_phi_p[j] - viscosity_ * laplacian_phi_u[j]);

                      if (scheme == Parameters::SimulationControl::bdf1 ||
                          scheme == Parameters::SimulationControl::bdf2 ||
                          scheme == Parameters::SimulationControl::bdf3)
                        strong_jac += phi_u[j];

                      for (unsigned int i = 0; i < dofs_per_cell; ++i)
                        {
                          local_matrix(i, j) +=
                            (viscosity_ *
                               scalar_product(grad_phi_u[j], grad_phi_u[i]) +
                             present_velocity_gradients[q] * phi_u[j] *
                               phi_u[i] +
                             grad_phi_u[j] * present_velocity_values[q] *
                               phi_u[i] -
                             div_phi_u[i] * phi_p[j] +
                             phi_p[i] * div_phi_u[j]) *
                            fe_values.JxW(q);

                          // Mass matrix
                          if (scheme == Parameters::SimulationControl::bdf1 ||
                              scheme == Parameters::SimulationControl::bdf2 ||
                              scheme == Parameters::SimulationControl::bdf3)
                            local_matrix(i, j) += phi_u[j] * phi_u[i] *
                                                  alpha_bdf[0] *
                                                  fe_values.JxW(q);

                          // PSPG GLS term
                          local_matrix(i, j) +=
                            tau * strong_jac * grad_phi_p[i] * fe_values.JxW(q);

                          // Jacobian is currently incomplete
                          if (SUPG)
                            {
                              local_matrix(i, j) +=
                                tau *
                                (strong_jac * (grad_phi_u[i] *
                                               present_velocity_values[q]) +
                                 strong_residual * (grad_phi_u[i] * phi_u[j])) *
                                fe_values.JxW(q);
                            }
                        }
                    }
                }
              for (unsigned int i = 0; i < dofs_per_cell; ++i)
                {
                  double present_velocity_divergence =
                    trace(present_velocity_gradients[q]);
                  local_rhs(i) +=
                    (-viscosity_ * scalar_product(present_velocity_gradients[q],
                                                  grad_phi_u[i]) -
                     present_velocity_gradients[q] *
                       present_velocity_values[q] * phi_u[i] +
                     present_pressure_values[q] * div_phi_u[i] -
                     present_velocity_divergence * phi_p[i] +
                     force * phi_u[i]) *
                    fe_values.JxW(q);

                  if (scheme == Parameters::SimulationControl::bdf1)
                    local_rhs(i) -=
                      alpha_bdf[0] *
                      (present_velocity_values[q] - p1_velocity_values[q]) *
                      phi_u[i] * fe_values.JxW(q);

                  if (scheme == Parameters::SimulationControl::bdf2)
                    local_rhs(i) -=
                      (alpha_bdf[0] * (present_velocity_values[q] * phi_u[i]) +
                       alpha_bdf[1] * (p1_velocity_values[q] * phi_u[i]) +
                       alpha_bdf[2] * (p2_velocity_values[q] * phi_u[i])) *
                      fe_values.JxW(q);

                  if (scheme == Parameters::SimulationControl::bdf3)
                    local_rhs(i) -=
                      (alpha_bdf[0] * (present_velocity_values[q] * phi_u[i]) +
                       alpha_bdf[1] * (p1_velocity_values[q] * phi_u[i]) +
                       alpha_bdf[2] * (p2_velocity_values[q] * phi_u[i]) +
                       alpha_bdf[3] * (p3_velocity_values[q] * phi_u[i])) *
                      fe_values.JxW(q);

                  // PSPG GLS term
                  local_rhs(i) +=
                    -tau * (strong_residual * grad_phi_p[i]) * fe_values.JxW(q);

                  // SUPG GLS term
                  if (SUPG)
                    {
                      local_rhs(i) +=
                        -tau *
                        (strong_residual *
                         (grad_phi_u[i] * present_velocity_values[q])) *
                        fe_values.JxW(q);
                    }
                }
            }

          cell->get_dof_indices(local_dof_indices);
          // The non-linear solver assumes that the nonzero constraints have
          // already been applied to the solution
          const AffineConstraints<double> &constraints_used = zero_constraints;
          // initial_step ? nonzero_constraints : zero_constraints;
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
  if (assemble_matrix)
    system_matrix.compress(VectorOperation::add);
  system_rhs.compress(VectorOperation::add);
}

/**
 * Set the initial condition using a L2 or a viscous solver
 **/
template <int dim>
void
GLSNavierStokesSolver<dim>::set_initial_condition(
  Parameters::InitialConditionType initial_condition_type,
  bool                             restart)
{
  if (restart)
    {
      this->pcout << "************************" << std::endl;
      this->pcout << "---> Simulation Restart " << std::endl;
      this->pcout << "************************" << std::endl;
      read_checkpoint();
    }
  else if (initial_condition_type ==
           Parameters::InitialConditionType::L2projection)
    {
      assemble_L2_projection();
      solve_linear_system(true, 1e-15, 1e-15);
      this->present_solution = newton_update;
      this->finish_time_step();
      postprocess(true);
    }
  else if (initial_condition_type == Parameters::InitialConditionType::nodal)
    {
      set_nodal_values();
      this->finish_time_step();
      postprocess(true);
    }

  else if (initial_condition_type == Parameters::InitialConditionType::viscous)
    {
      set_nodal_values();
      double viscosity = this->nsparam.physicalProperties.viscosity;
      this->nsparam.physicalProperties.viscosity =
        this->nsparam.initialCondition->viscosity;
      Parameters::SimulationControl::TimeSteppingMethod previousControl =
        this->simulationControl.getMethod();
      this->simulationControl.setMethod(Parameters::SimulationControl::steady);
      newton_iteration(false);
      this->simulationControl.setMethod(previousControl);
      this->finish_time_step();
      postprocess(true);
      this->simulationControl.setMethod(previousControl);
      this->nsparam.physicalProperties.viscosity = viscosity;
    }
  else
    {
      throw std::runtime_error("GLSNS - Initial condition could not be set");
    }
}



template <int dim>
void
GLSNavierStokesSolver<dim>::assemble_L2_projection()
{
  system_matrix = 0;
  system_rhs    = 0;
  QGauss<dim>                 quadrature_formula(this->degreeQuadrature_);
  const MappingQ<dim>         mapping(this->degreeVelocity_,
                              this->nsparam.femParameters.qmapping_all);
  FEValues<dim>               fe_values(mapping,
                          this->fe,
                          quadrature_formula,
                          update_values | update_quadrature_points |
                            update_JxW_values);
  const unsigned int          dofs_per_cell = this->fe.dofs_per_cell;
  const unsigned int          n_q_points    = quadrature_formula.size();
  FullMatrix<double>          local_matrix(dofs_per_cell, dofs_per_cell);
  Vector<double>              local_rhs(dofs_per_cell);
  std::vector<Vector<double>> initial_velocity(n_q_points,
                                               Vector<double>(dim + 1));
  std::vector<types::global_dof_index> local_dof_indices(dofs_per_cell);
  const FEValuesExtractors::Vector     velocities(0);
  const FEValuesExtractors::Scalar     pressure(dim);

  Tensor<1, dim> rhs_initial_velocity_pressure;
  double         rhs_initial_pressure;

  std::vector<Tensor<1, dim>> phi_u(dofs_per_cell);
  std::vector<double>         phi_p(dofs_per_cell);

  typename DoFHandler<dim>::active_cell_iterator cell = this->dof_handler
                                                          .begin_active(),
                                                 endc = this->dof_handler.end();
  for (; cell != endc; ++cell)
    {
      if (cell->is_locally_owned())
        {
          fe_values.reinit(cell);
          local_matrix = 0;
          local_rhs    = 0;
          this->nsparam.initialCondition->uvwp.vector_value_list(
            fe_values.get_quadrature_points(), initial_velocity);
          for (unsigned int q = 0; q < n_q_points; ++q)
            {
              for (unsigned int k = 0; k < dofs_per_cell; ++k)
                {
                  phi_p[k] = fe_values[pressure].value(k, q);
                  phi_u[k] = fe_values[velocities].value(k, q);
                }

              // Establish the rhs tensor operator
              for (int i = 0; i < dim; ++i)
                {
                  const unsigned int component_i =
                    this->fe.system_to_component_index(i).first;
                  rhs_initial_velocity_pressure[i] =
                    initial_velocity[q](component_i);
                }
              rhs_initial_pressure = initial_velocity[q](dim);

              for (unsigned int i = 0; i < dofs_per_cell; ++i)
                {
                  // Matrix assembly
                  for (unsigned int j = 0; j < dofs_per_cell; ++j)
                    {
                      local_matrix(i, j) +=
                        (phi_u[j] * phi_u[i]) * fe_values.JxW(q);
                      local_matrix(i, j) +=
                        (phi_p[j] * phi_p[i]) * fe_values.JxW(q);
                    }
                  local_rhs(i) += (phi_u[i] * rhs_initial_velocity_pressure +
                                   phi_p[i] * rhs_initial_pressure) *
                                  fe_values.JxW(q);
                }
            }

          cell->get_dof_indices(local_dof_indices);
          const AffineConstraints<double> &constraints_used =
            nonzero_constraints;
          constraints_used.distribute_local_to_global(local_matrix,
                                                      local_rhs,
                                                      local_dof_indices,
                                                      system_matrix,
                                                      system_rhs);
        }
    }
  system_matrix.compress(VectorOperation::add);
  system_rhs.compress(VectorOperation::add);
}

template <int dim>
void
GLSNavierStokesSolver<dim>::set_nodal_values()
{
  const FEValuesExtractors::Vector velocities(0);
  const FEValuesExtractors::Scalar pressure(dim);
  const MappingQ<dim>              mapping(this->degreeVelocity_,
                              this->nsparam.femParameters.qmapping_all);
  VectorTools::interpolate(mapping,
                           this->dof_handler,
                           this->nsparam.initialCondition->uvwp,
                           newton_update,
                           this->fe.component_mask(velocities));
  VectorTools::interpolate(mapping,
                           this->dof_handler,
                           this->nsparam.initialCondition->uvwp,
                           newton_update,
                           this->fe.component_mask(pressure));
  nonzero_constraints.distribute(newton_update);
  this->present_solution = newton_update;
}

template <int dim>
void
GLSNavierStokesSolver<dim>::assemble_system()
{
  TimerOutput::Scope t(this->computing_timer, "assemble_system");

  if (this->simulationControl.getMethod() ==
      Parameters::SimulationControl::bdf1)
    assembleGLS<true, Parameters::SimulationControl::bdf1>();
  else if (this->simulationControl.getMethod() ==
           Parameters::SimulationControl::bdf2)
    assembleGLS<true, Parameters::SimulationControl::bdf2>();
  else if (this->simulationControl.getMethod() ==
           Parameters::SimulationControl::bdf3)
    assembleGLS<true, Parameters::SimulationControl::bdf3>();
  else if (this->simulationControl.getMethod() ==
           Parameters::SimulationControl::steady)
    assembleGLS<true, Parameters::SimulationControl::steady>();
}
template <int dim>
void
GLSNavierStokesSolver<dim>::assemble_rhs()
{
  TimerOutput::Scope t(this->computing_timer, "assemble_rhs");

  if (this->simulationControl.getMethod() ==
      Parameters::SimulationControl::bdf1)
    assembleGLS<false, Parameters::SimulationControl::bdf1>();
  else if (this->simulationControl.getMethod() ==
           Parameters::SimulationControl::bdf2)
    assembleGLS<false, Parameters::SimulationControl::bdf2>();
  else if (this->simulationControl.getMethod() ==
           Parameters::SimulationControl::bdf3)
    assembleGLS<false, Parameters::SimulationControl::bdf3>();
  else if (this->simulationControl.getMethod() ==
           Parameters::SimulationControl::steady)
    assembleGLS<false, Parameters::SimulationControl::steady>();
}

template <int dim>
void
GLSNavierStokesSolver<dim>::solve_linear_system(const bool initial_step,
                                                double     relative_residual,
                                                double     minimum_residual)
{
  if (this->nsparam.linearSolver.solver == this->nsparam.linearSolver.gmres)
    solve_system_GMRES(initial_step, minimum_residual, relative_residual);
  else if (this->nsparam.linearSolver.solver ==
           this->nsparam.linearSolver.bicgstab)
    solve_system_BiCGStab(initial_step, minimum_residual, relative_residual);
  else if (this->nsparam.linearSolver.solver == this->nsparam.linearSolver.amg)
    solve_system_AMG(initial_step, minimum_residual, relative_residual);
  else
    throw("This solver is not allowed");
}

template <int dim>
void
GLSNavierStokesSolver<dim>::solve_system_GMRES(const bool initial_step,
                                               double     absolute_residual,
                                               double     relative_residual)
{
  TimerOutput::Scope t(this->computing_timer, "solve_linear_system");
  const AffineConstraints<double> &constraints_used =
    initial_step ? nonzero_constraints : zero_constraints;
  const double linear_solver_tolerance =
    std::max(relative_residual * system_rhs.l2_norm(), absolute_residual);

  if (this->nsparam.linearSolver.verbosity != Parameters::quiet)
    {
      this->pcout << "  -Tolerance of iterative solver is : "
                  << std::setprecision(
                       this->nsparam.linearSolver.residual_precision)
                  << linear_solver_tolerance << std::endl;
    }
  TrilinosWrappers::MPI::Vector completely_distributed_solution(
    locally_owned_dofs, this->mpi_communicator);

  SolverControl solver_control(this->nsparam.linearSolver.max_iterations,
                               linear_solver_tolerance,
                               true,
                               true);
  TrilinosWrappers::SolverGMRES solver(solver_control);

  //**********************************************
  // Trillinos Wrapper ILU Preconditioner
  //*********************************************
  const double ilu_fill = this->nsparam.linearSolver.ilu_precond_fill;
  const double ilu_atol = this->nsparam.linearSolver.ilu_precond_atol;
  const double ilu_rtol = this->nsparam.linearSolver.ilu_precond_rtol;
  TrilinosWrappers::PreconditionILU::AdditionalData preconditionerOptions(
    ilu_fill, ilu_atol, ilu_rtol, 0);
  TrilinosWrappers::PreconditionILU preconditioner;

  preconditioner.initialize(system_matrix, preconditionerOptions);

  solver.solve(system_matrix,
               completely_distributed_solution,
               system_rhs,
               preconditioner);

  if (this->nsparam.linearSolver.verbosity != Parameters::quiet)
    {
      this->pcout << "  -Iterative solver took : " << solver_control.last_step()
                  << " steps " << std::endl;
    }

  constraints_used.distribute(completely_distributed_solution);
  newton_update = completely_distributed_solution;
}

template <int dim>
void
GLSNavierStokesSolver<dim>::solve_system_BiCGStab(const bool initial_step,
                                                  double     absolute_residual,
                                                  double     relative_residual)
{
  TimerOutput::Scope t(this->computing_timer, "solve");

  const AffineConstraints<double> &constraints_used =
    initial_step ? nonzero_constraints : zero_constraints;
  const double linear_solver_tolerance =
    std::max(relative_residual * system_rhs.l2_norm(), absolute_residual);
  if (this->nsparam.linearSolver.verbosity != Parameters::quiet)
    {
      this->pcout << "  -Tolerance of iterative solver is : "
                  << std::setprecision(
                       this->nsparam.linearSolver.residual_precision)
                  << linear_solver_tolerance << std::endl;
    }
  TrilinosWrappers::MPI::Vector completely_distributed_solution(
    locally_owned_dofs, this->mpi_communicator);

  SolverControl solver_control(this->nsparam.linearSolver.max_iterations,
                               linear_solver_tolerance,
                               true,
                               true);
  TrilinosWrappers::SolverBicgstab solver(solver_control);

  //**********************************************
  // Trillinos Wrapper ILU Preconditioner
  //*********************************************
  const double ilu_fill = this->nsparam.linearSolver.ilu_precond_fill;
  const double ilu_atol = this->nsparam.linearSolver.ilu_precond_atol;
  const double ilu_rtol = this->nsparam.linearSolver.ilu_precond_rtol;
  TrilinosWrappers::PreconditionILU::AdditionalData preconditionerOptions(
    ilu_fill, ilu_atol, ilu_rtol, 0);
  TrilinosWrappers::PreconditionILU preconditioner;

  preconditioner.initialize(system_matrix, preconditionerOptions);

  solver.solve(system_matrix,
               completely_distributed_solution,
               system_rhs,
               preconditioner);

  if (this->nsparam.linearSolver.verbosity != Parameters::quiet)
    {
      this->pcout << "  -Iterative solver took : " << solver_control.last_step()
                  << " steps " << std::endl;
    }
  constraints_used.distribute(completely_distributed_solution);
  newton_update = completely_distributed_solution;
}

template <int dim>
void
GLSNavierStokesSolver<dim>::solve_system_AMG(const bool initial_step,
                                             double     absolute_residual,
                                             double     relative_residual)
{
  TimerOutput::Scope t(this->computing_timer, "solve");

  const AffineConstraints<double> &constraints_used =
    initial_step ? nonzero_constraints : zero_constraints;

  const double linear_solver_tolerance =
    std::max(relative_residual * system_rhs.l2_norm(), absolute_residual);
  if (this->nsparam.linearSolver.verbosity != Parameters::quiet)
    {
      this->pcout << "  -Tolerance of iterative solver is : "
                  << std::setprecision(
                       this->nsparam.linearSolver.residual_precision)
                  << linear_solver_tolerance << std::endl;
    }
  TrilinosWrappers::MPI::Vector completely_distributed_solution(
    locally_owned_dofs, this->mpi_communicator);

  SolverControl solver_control(this->nsparam.linearSolver.max_iterations,
                               linear_solver_tolerance,
                               true,
                               true);
  TrilinosWrappers::SolverGMRES solver(solver_control);

  TrilinosWrappers::PreconditionAMG preconditioner;

  std::vector<std::vector<bool>> constant_modes;
  // Constant modes include pressure since everything is in the same matrix
  std::vector<bool> velocity_components(dim + 1, true);
  velocity_components[dim] = true;
  DoFTools::extract_constant_modes(this->dof_handler,
                                   velocity_components,
                                   constant_modes);

  TrilinosWrappers::PreconditionAMG::AdditionalData amg_data;
  amg_data.constant_modes = constant_modes;

  const bool elliptic              = false;
  bool       higher_order_elements = false;
  if (this->degreeVelocity_ > 1)
    higher_order_elements = true;
  const unsigned int n_cycles = this->nsparam.linearSolver.amg_n_cycles;
  const bool         w_cycle  = this->nsparam.linearSolver.amg_w_cycles;
  const double       aggregation_threshold =
    this->nsparam.linearSolver.amg_aggregation_threshold;
  const unsigned int smoother_sweeps =
    this->nsparam.linearSolver.amg_smoother_sweeps;
  const unsigned int smoother_overlap =
    this->nsparam.linearSolver.amg_smoother_overlap;
  const bool                                        output_details = false;
  const char *                                      smoother_type  = "ILU";
  const char *                                      coarse_type    = "ILU";
  TrilinosWrappers::PreconditionAMG::AdditionalData preconditionerOptions(
    elliptic,
    higher_order_elements,
    n_cycles,
    w_cycle,
    aggregation_threshold,
    constant_modes,
    smoother_sweeps,
    smoother_overlap,
    output_details,
    smoother_type,
    coarse_type);

  Teuchos::ParameterList              parameter_ml;
  std::unique_ptr<Epetra_MultiVector> distributed_constant_modes;
  preconditionerOptions.set_parameters(parameter_ml,
                                       distributed_constant_modes,
                                       system_matrix);
  const double ilu_fill = this->nsparam.linearSolver.amg_precond_ilu_fill;
  const double ilu_atol = this->nsparam.linearSolver.amg_precond_ilu_atol;
  const double ilu_rtol = this->nsparam.linearSolver.amg_precond_ilu_rtol;
  parameter_ml.set("smoother: ifpack level-of-fill", ilu_fill);
  parameter_ml.set("smoother: ifpack absolute threshold", ilu_atol);
  parameter_ml.set("smoother: ifpack relative threshold", ilu_rtol);

  parameter_ml.set("coarse: ifpack level-of-fill", ilu_fill);
  parameter_ml.set("coarse: ifpack absolute threshold", ilu_atol);
  parameter_ml.set("coarse: ifpack relative threshold", ilu_rtol);
  preconditioner.initialize(system_matrix, parameter_ml);

  solver.solve(system_matrix,
               completely_distributed_solution,
               system_rhs,
               preconditioner);

  if (this->nsparam.linearSolver.verbosity != Parameters::quiet)
    {
      this->pcout << "  -Iterative solver took : " << solver_control.last_step()
                  << " steps " << std::endl;
    }

  constraints_used.distribute(completely_distributed_solution);

  newton_update = completely_distributed_solution;
}

template <int dim>
void
GLSNavierStokesSolver<dim>::refine_mesh()
{
  if (this->simulationControl.getIter() %
        this->nsparam.meshAdaptation.frequency ==
      0)
    {
      if (this->nsparam.meshAdaptation.type ==
          this->nsparam.meshAdaptation.kelly)
        refine_mesh_Kelly();
      if (this->nsparam.meshAdaptation.type ==
          this->nsparam.meshAdaptation.uniform)
        refine_mesh_uniform();
    }
}

template <int dim>
void
GLSNavierStokesSolver<dim>::refine_mesh_Kelly()
{
  // Time monitoring
  TimerOutput::Scope t(this->computing_timer, "refine");

  Vector<float> estimated_error_per_cell(this->triangulation.n_active_cells());
  const MappingQ<dim>              mapping(this->degreeVelocity_,
                              this->nsparam.femParameters.qmapping_all);
  const FEValuesExtractors::Vector velocity(0);
  const FEValuesExtractors::Scalar pressure(dim);
  if (this->nsparam.meshAdaptation.variable ==
      Parameters::MeshAdaptation::pressure)
    {
      KellyErrorEstimator<dim>::estimate(
        mapping,
        this->dof_handler,
        QGauss<dim - 1>(this->degreeQuadrature_ + 1),
        typename std::map<types::boundary_id, const Function<dim, double> *>(),
        this->present_solution,
        estimated_error_per_cell,
        this->fe.component_mask(pressure));
    }
  else if (this->nsparam.meshAdaptation.variable ==
           Parameters::MeshAdaptation::velocity)
    {
      KellyErrorEstimator<dim>::estimate(
        mapping,
        this->dof_handler,
        QGauss<dim - 1>(this->degreeQuadrature_ + 1),
        typename std::map<types::boundary_id, const Function<dim, double> *>(),
        this->present_solution,
        estimated_error_per_cell,
        this->fe.component_mask(velocity));
    }

  if (this->nsparam.meshAdaptation.fractionType ==
      Parameters::MeshAdaptation::number)
    parallel::distributed::GridRefinement::refine_and_coarsen_fixed_number(
      this->triangulation,
      estimated_error_per_cell,
      this->nsparam.meshAdaptation.fractionRefinement,
      this->nsparam.meshAdaptation.fractionCoarsening,
      this->nsparam.meshAdaptation.maxNbElements);

  else if (this->nsparam.meshAdaptation.fractionType ==
           Parameters::MeshAdaptation::fraction)
    parallel::distributed::GridRefinement::refine_and_coarsen_fixed_fraction(
      this->triangulation,
      estimated_error_per_cell,
      this->nsparam.meshAdaptation.fractionRefinement,
      this->nsparam.meshAdaptation.fractionCoarsening);

  if (this->triangulation.n_levels() > this->nsparam.meshAdaptation.maxRefLevel)
    for (typename Triangulation<dim>::active_cell_iterator cell =
           this->triangulation.begin_active(
             this->nsparam.meshAdaptation.maxRefLevel);
         cell != this->triangulation.end();
         ++cell)
      cell->clear_refine_flag();
  for (typename Triangulation<dim>::active_cell_iterator cell =
         this->triangulation.begin_active(
           this->nsparam.meshAdaptation.minRefLevel);
       cell !=
       this->triangulation.end_active(this->nsparam.meshAdaptation.minRefLevel);
       ++cell)
    cell->clear_coarsen_flag();

  this->triangulation.prepare_coarsening_and_refinement();

  // Solution transfer objects for all the solutions
  parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    solution_transfer(this->dof_handler);
  parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    solution_transfer_m1(this->dof_handler);
  parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    solution_transfer_m2(this->dof_handler);
  parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    solution_transfer_m3(this->dof_handler);
  solution_transfer.prepare_for_coarsening_and_refinement(
    this->present_solution);
  solution_transfer_m1.prepare_for_coarsening_and_refinement(this->solution_m1);
  solution_transfer_m2.prepare_for_coarsening_and_refinement(this->solution_m2);
  solution_transfer_m3.prepare_for_coarsening_and_refinement(this->solution_m3);

  this->triangulation.execute_coarsening_and_refinement();
  setup_dofs();

  // Set up the vectors for the transfer
  TrilinosWrappers::MPI::Vector tmp(locally_owned_dofs, this->mpi_communicator);
  TrilinosWrappers::MPI::Vector tmp_m1(locally_owned_dofs,
                                       this->mpi_communicator);
  TrilinosWrappers::MPI::Vector tmp_m2(locally_owned_dofs,
                                       this->mpi_communicator);
  TrilinosWrappers::MPI::Vector tmp_m3(locally_owned_dofs,
                                       this->mpi_communicator);

  // Interpolate the solution at time and previous time
  solution_transfer.interpolate(tmp);
  solution_transfer_m1.interpolate(tmp_m1);
  solution_transfer_m2.interpolate(tmp_m2);
  solution_transfer_m3.interpolate(tmp_m3);

  // Distribute constraints
  nonzero_constraints.distribute(tmp);
  nonzero_constraints.distribute(tmp_m1);
  nonzero_constraints.distribute(tmp_m2);
  nonzero_constraints.distribute(tmp_m3);

  // Fix on the new mesh
  this->present_solution = tmp;
  this->solution_m1      = tmp_m1;
  this->solution_m2      = tmp_m2;
  this->solution_m3      = tmp_m3;
}

template <int dim>
void
GLSNavierStokesSolver<dim>::refine_mesh_uniform()
{
  TimerOutput::Scope t(this->computing_timer, "refine");

  // Solution transfer objects for all the solutions
  parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    solution_transfer(this->dof_handler);
  parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    solution_transfer_m1(this->dof_handler);
  parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    solution_transfer_m2(this->dof_handler);
  parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    solution_transfer_m3(this->dof_handler);
  solution_transfer.prepare_for_coarsening_and_refinement(
    this->present_solution);
  solution_transfer_m1.prepare_for_coarsening_and_refinement(this->solution_m1);
  solution_transfer_m2.prepare_for_coarsening_and_refinement(this->solution_m2);
  solution_transfer_m3.prepare_for_coarsening_and_refinement(this->solution_m3);

  // Refine
  this->triangulation.refine_global(1);

  setup_dofs();

  // Set up the vectors for the transfer
  TrilinosWrappers::MPI::Vector tmp(locally_owned_dofs, this->mpi_communicator);
  TrilinosWrappers::MPI::Vector tmp_m1(locally_owned_dofs,
                                       this->mpi_communicator);
  TrilinosWrappers::MPI::Vector tmp_m2(locally_owned_dofs,
                                       this->mpi_communicator);
  TrilinosWrappers::MPI::Vector tmp_m3(locally_owned_dofs,
                                       this->mpi_communicator);

  // Interpolate the solution at time and previous time
  solution_transfer.interpolate(tmp);
  solution_transfer_m1.interpolate(tmp_m1);
  solution_transfer_m2.interpolate(tmp_m2);
  solution_transfer_m3.interpolate(tmp_m3);

  // Distribute constraints
  nonzero_constraints.distribute(tmp);
  nonzero_constraints.distribute(tmp_m1);
  nonzero_constraints.distribute(tmp_m2);
  nonzero_constraints.distribute(tmp_m3);

  // Fix on the new mesh
  this->present_solution = tmp;
  this->solution_m1      = tmp_m1;
  this->solution_m2      = tmp_m2;
  this->solution_m3      = tmp_m3;
}

template <int dim>
void
GLSNavierStokesSolver<dim>::newton_iteration(const bool is_initial_step)
{
  double current_res;
  double last_res;
  bool   first_step = is_initial_step;
  {
    unsigned int outer_iteration = 0;
    last_res                     = 1.0;
    current_res                  = 1.0;
    while ((current_res > this->nsparam.nonLinearSolver.tolerance) &&
           outer_iteration < this->nsparam.nonLinearSolver.maxIterations)
      {
        evaluation_point = this->present_solution;
        assemble_system();
        if (outer_iteration == 0)
          {
            current_res = system_rhs.l2_norm();
            last_res    = current_res;
          }
        if (this->nsparam.nonLinearSolver.verbosity != Parameters::quiet)
          this->pcout << "Newton iteration: " << outer_iteration
                      << "  - Residual:  " << current_res << std::endl;
        solve_linear_system(first_step,
                            this->nsparam.linearSolver.relative_residual,
                            this->nsparam.linearSolver.minimum_residual);

        for (double alpha = 1.0; alpha > 1e-3; alpha *= 0.5)
          {
            local_evaluation_point = this->present_solution;
            local_evaluation_point.add(alpha, newton_update);
            nonzero_constraints.distribute(local_evaluation_point);
            evaluation_point = local_evaluation_point;
            assemble_rhs();
            current_res = system_rhs.l2_norm();
            if (this->nsparam.nonLinearSolver.verbosity != Parameters::quiet)
              this->pcout << "\t\talpha = " << std::setw(6) << alpha
                          << std::setw(0) << " res = "
                          << std::setprecision(
                               this->nsparam.nonLinearSolver.display_precision)
                          << current_res << std::endl;
            if (current_res < 0.9 * last_res ||
                last_res < this->nsparam.nonLinearSolver.tolerance)
              break;
          }
        this->present_solution = evaluation_point;
        last_res               = current_res;
        ++outer_iteration;
      }
  }
}



template <int dim>
void
GLSNavierStokesSolver<dim>::write_checkpoint()
{
  TimerOutput::Scope timer(this->computing_timer, "write_checkpoint");
  std::string        prefix = this->nsparam.restartParameters.filename;
  if (Utilities::MPI::this_mpi_process(this->mpi_communicator) == 0)
    this->simulationControl.save(prefix);
  if (Utilities::MPI::this_mpi_process(this->mpi_communicator) == 0)
    this->pvdhandler.save(prefix);

  std::vector<const TrilinosWrappers::MPI::Vector *> sol_set_transfer;
  sol_set_transfer.push_back(&this->present_solution);
  sol_set_transfer.push_back(&this->solution_m1);
  sol_set_transfer.push_back(&this->solution_m2);
  sol_set_transfer.push_back(&this->solution_m3);
  parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    system_trans_vectors(this->dof_handler);
  system_trans_vectors.prepare_for_serialization(sol_set_transfer);

  std::string triangulationName = prefix + ".triangulation";
  this->triangulation.save(prefix + ".triangulation");
}

template <int dim>
void
GLSNavierStokesSolver<dim>::read_checkpoint()
{
  TimerOutput::Scope timer(this->computing_timer, "read_checkpoint");
  std::string        prefix = this->nsparam.restartParameters.filename;
  this->simulationControl.read(prefix);
  this->pvdhandler.read(prefix);

  const std::string filename = prefix + ".triangulation";
  std::ifstream     in(filename.c_str());
  if (!in)
    AssertThrow(false,
                ExcMessage(
                  std::string(
                    "You are trying to restart a previous computation, "
                    "but the restart file <") +
                  filename + "> does not appear to exist!"));

  try
    {
      this->triangulation.load(filename.c_str());
    }
  catch (...)
    {
      AssertThrow(false,
                  ExcMessage("Cannot open snapshot mesh file or read the "
                             "triangulation stored there."));
    }
  setup_dofs();
  std::vector<TrilinosWrappers::MPI::Vector *> x_system(4);

  TrilinosWrappers::MPI::Vector distributed_system(system_rhs);
  TrilinosWrappers::MPI::Vector distributed_system_m1(system_rhs);
  TrilinosWrappers::MPI::Vector distributed_system_m2(system_rhs);
  TrilinosWrappers::MPI::Vector distributed_system_m3(system_rhs);
  x_system[0] = &(distributed_system);
  x_system[1] = &(distributed_system_m1);
  x_system[2] = &(distributed_system_m2);
  x_system[3] = &(distributed_system_m3);
  parallel::distributed::SolutionTransfer<dim, TrilinosWrappers::MPI::Vector>
    system_trans_vectors(this->dof_handler);
  system_trans_vectors.deserialize(x_system);
  this->present_solution = distributed_system;
  this->solution_m1      = distributed_system_m1;
  this->solution_m2      = distributed_system_m2;
  this->solution_m3      = distributed_system_m3;
}

template <int dim>
void
GLSNavierStokesSolver<dim>::postprocess(bool firstIter)
{
  if (this->simulationControl.isOutputIteration())
    this->template write_output_results(
      this->present_solution,
      this->pvdhandler,
      this->simulationControl.getOutputFolder(),
      this->simulationControl.getOuputName(),
      this->simulationControl.getIter(),
      this->simulationControl.getTime(),
      this->simulationControl.getSubdivision());

  if (this->nsparam.postProcessingParameters.calculate_enstrophy)
    {
      double enstrophy =
        this->calculate_average_enstrophy(this->present_solution);
      this->enstrophy_table.add_value("time",
                                      this->simulationControl.getTime());
      this->enstrophy_table.add_value("enstrophy", enstrophy);
      if (this->nsparam.postProcessingParameters.verbosity ==
          Parameters::verbose)
        {
          this->pcout << "Enstrophy  : " << enstrophy << std::endl;
        }
    }

  if (this->nsparam.postProcessingParameters.calculate_kinetic_energy)
    {
      double kE = this->calculate_average_KE(this->present_solution);
      this->kinetic_energy_table.add_value("time",
                                           this->simulationControl.getTime());
      this->kinetic_energy_table.add_value("kinetic-energy", kE);
      if (this->nsparam.postProcessingParameters.verbosity ==
          Parameters::verbose)
        {
          this->pcout << "Kinetic energy : " << kE << std::endl;
        }
    }

  if (!firstIter)
    {
      // Calculate forces on the boundary conditions
      if (this->nsparam.forcesParameters.calculate_force)
        {
          if (this->simulationControl.getIter() %
                this->nsparam.forcesParameters.calculation_frequency ==
              0)
            this->calculate_forces(this->present_solution,
                                   this->simulationControl);
          if (this->simulationControl.getIter() %
                this->nsparam.forcesParameters.output_frequency ==
              0)
            this->write_output_forces();
        }

      // Calculate torques on the boundary conditions
      if (this->nsparam.forcesParameters.calculate_torque)
        {
          if (this->simulationControl.getIter() %
                this->nsparam.forcesParameters.calculation_frequency ==
              0)
            this->calculate_torques(this->present_solution,
                                    this->simulationControl);
          if (this->simulationControl.getIter() %
                this->nsparam.forcesParameters.output_frequency ==
              0)
            this->write_output_torques();
        }

      // Calculate error with respect to analytical solution
      if (this->nsparam.analyticalSolution->calculate_error())
        {
          // Update the time of the exact solution to the actual time
          this->exact_solution->set_time(this->simulationControl.getTime());
          const double error = this->calculate_L2_error(this->present_solution);
          if (this->simulationControl.getMethod() ==
              Parameters::SimulationControl::steady)
            {
              this->table.add_value(
                "cells", this->triangulation.n_global_active_cells());
              this->table.add_value("error", error);
            }
          else
            {
              this->table.add_value("time", this->simulationControl.getTime());
              this->table.add_value("error", error);
            }
          if (this->nsparam.analyticalSolution->verbosity ==
              Parameters::verbose)
            {
              this->pcout << "L2 error : " << error << std::endl;
            }
        }
    }
}

/*
 * Generic CFD Solver application
 * Handles the majority of the cases for the GLS-NS solver
 */
template <int dim>
void
GLSNavierStokesSolver<dim>::solve()
{
  this->read_mesh();
  this->create_manifolds();

  this->setup_dofs();
  this->set_initial_condition(this->nsparam.initialCondition->type,
                              this->nsparam.restartParameters.restart);

  while (this->simulationControl.integrate())
    {
      printTime(this->pcout, this->simulationControl);
      if (!this->simulationControl.firstIter())
        {
          this->refine_mesh();
        }
      this->iterate(this->simulationControl.firstIter());
      this->postprocess(false);
      this->finish_time_step();
    }

  this->finish_simulation();
}

#endif