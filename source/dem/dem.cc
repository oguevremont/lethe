/* ---------------------------------------------------------------------
 *
 * Copyright (C) 2019 - 2020 by the Lethe authors
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
 * Author: Bruno Blais, Shahab Golshan, Polytechnique Montreal, 2019-
 */

#include <deal.II/fe/mapping_q_generic.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_out.h>

#include <core/solutions_output.h>
#include <dem/dem.h>

template <int dim>
DEMSolver<dim>::DEMSolver(DEMSolverParameters<dim> dem_parameters)
  : mpi_communicator(MPI_COMM_WORLD)
  , n_mpi_processes(Utilities::MPI::n_mpi_processes(mpi_communicator))
  , this_mpi_process(Utilities::MPI::this_mpi_process(mpi_communicator))
  , pcout({std::cout, Utilities::MPI::this_mpi_process(MPI_COMM_WORLD) == 0})
  , parameters(dem_parameters)
  , triangulation(this->mpi_communicator)
  , property_pool(DEM::get_number_properties())
  , mapping(1)
  , computing_timer(this->mpi_communicator,
                    this->pcout,
                    TimerOutput::summary,
                    TimerOutput::wall_times)
  , particle_handler(triangulation, mapping, DEM::get_number_properties())
  , neighborhood_threshold(
      std::pow(parameters.model_parameters.neighborhood_threshold *
                 parameters.physical_properties.diameter,
               2))
  , contact_detection_frequency(
      parameters.model_parameters.contact_detection_frequency)
  , repartition_frequency(parameters.model_parameters.repartition_frequency)
  , insertion_frequency(parameters.insertion_info.insertion_frequency)
  , physical_properties(parameters.physical_properties)
  , background_dh(triangulation)
{
  // Change the behavior of the timer for situations when you don't want outputs
  if (parameters.timer.type == Parameters::Timer::Type::none)
    computing_timer.disable_output();

  simulation_control = std::make_shared<SimulationControlTransientDEM>(
    parameters.simulation_control);

  if (fmod(repartition_frequency, contact_detection_frequency) != 0)
    {
      throw std::runtime_error(
        "The repartition frequency must be a multiple of the contact detection frequency");
    }



  // In order to consider the particles when repartitioning the triangulation
  // the algorithm needs to know three things:
  //
  // 1. How much weight to assign to each cell (how many particles are in
  // there)
  // 2. How to pack the particles before shipping data around
  // 3. How to unpack the particles after repartitioning
  //
  // Attach the correct functions to the signals inside
  // parallel::distributed::Triangulation, which will be called every time the
  // repartition() function is called.
  // These connections only need to be created once, so we might as well
  // have set them up in the constructor of this class, but for the purpose
  // of this example we want to group the particle related instructions.
  triangulation.signals.cell_weight.connect(
    [&](const typename parallel::distributed::Triangulation<dim>::cell_iterator
          &cell,
        const typename parallel::distributed::Triangulation<dim>::CellStatus
          status) -> unsigned int { return this->cell_weight(cell, status); });

  triangulation.signals.pre_distributed_repartition.connect(std::bind(
    &Particles::ParticleHandler<dim>::register_store_callback_function,
    &particle_handler));

  triangulation.signals.post_distributed_repartition.connect(
    std::bind(&Particles::ParticleHandler<dim>::register_load_callback_function,
              &particle_handler,
              false));
}

template <int dim>
void
DEMSolver<dim>::print_initial_info()
{
  pcout
    << "***************************************************************** \n";
  pcout << "Starting simulation with Lethe/DEM on " << n_mpi_processes
        << " processors" << std::endl;
  pcout << "***************************************************************** "
           "\n\n";
}


template <int dim>
unsigned int
DEMSolver<dim>::cell_weight(
  const typename parallel::distributed::Triangulation<dim>::cell_iterator &cell,
  const typename parallel::distributed::Triangulation<dim>::CellStatus status)
  const
{
  // Assign no weight to cells we do not own.
  if (!cell->is_locally_owned())
    return 0;

  // This determines how important particle work is compared to cell
  // work (by default every cell has a weight of 1000).
  // We set the weight per particle much higher to indicate that
  // the particle load is the only one that is important to distribute
  // in this example. The optimal value of this number depends on the
  // application and can range from 0 (cheap particle operations,
  // expensive cell operations) to much larger than 1000 (expensive
  // particle operations, cheap cell operations, like in this case).
  // This parameter will need to be tuned for the case of DEM.
  const unsigned int particle_weight = 10000;

  // This does not use adaptive refinement, therefore every cell
  // should have the status CELL_PERSIST. However this function can also
  // be used to distribute load during refinement, therefore we consider
  // refined or coarsened cells as well.
  if (status == parallel::distributed::Triangulation<dim>::CELL_PERSIST ||
      status == parallel::distributed::Triangulation<dim>::CELL_REFINE)
    {
      const unsigned int n_particles_in_cell =
        particle_handler.n_particles_in_cell(cell);
      return n_particles_in_cell * particle_weight;
    }
  else if (status == parallel::distributed::Triangulation<dim>::CELL_COARSEN)
    {
      unsigned int n_particles_in_cell = 0;

      for (unsigned int child_index = 0;
           child_index < GeometryInfo<dim>::max_children_per_cell;
           ++child_index)
        n_particles_in_cell +=
          particle_handler.n_particles_in_cell(cell->child(child_index));

      return n_particles_in_cell * particle_weight;
    }

  Assert(false, ExcInternalError());
  return 0;
}

template <int dim>
void
DEMSolver<dim>::read_mesh()
{
  // GMSH input
  if (parameters.mesh.type == Parameters::Mesh::Type::gmsh)
    {
      GridIn<dim> grid_in;
      grid_in.attach_triangulation(triangulation);
      std::ifstream input_file(parameters.mesh.file_name);
      grid_in.read_msh(input_file);
    }

  // Dealii grids
  else if (parameters.mesh.type == Parameters::Mesh::Type::dealii)
    {
      GridGenerator::generate_from_name_and_arguments(
        triangulation,
        parameters.mesh.grid_type,
        parameters.mesh.grid_arguments);
    }
  else
    throw std::runtime_error(
      "Unsupported mesh type - mesh will not be created");

  const int initial_size = parameters.mesh.initial_refinement;
  triangulation.refine_global(initial_size);
}

template <int dim>
void
DEMSolver<dim>::setup_background_dofs()
{
  FE_Q<dim> background_fe(1);
  background_dh.distribute_dofs(background_fe);
}

template <int dim>
bool
DEMSolver<dim>::insert_particles()
{
  if (fmod(simulation_control->get_step_number(), insertion_frequency) == 1)
    {
      insertion_object->insert(particle_handler, triangulation, parameters);
      return true;
    }
  return false;
}

template <int dim>
void
DEMSolver<dim>::particle_wall_broad_search()
{
  pw_broad_search_object.find_PW_Contact_Pairs(boundary_cells_information,
                                               particle_handler,
                                               pw_contact_candidates);

  particle_point_contact_candidates =
    particle_point_line_broad_search_object.find_Particle_Point_Contact_Pairs(
      particle_handler, boundary_cells_with_points);

  if (dim == 3)
    {
      particle_line_contact_candidates =
        particle_point_line_broad_search_object
          .find_Particle_Line_Contact_Pairs(particle_handler,
                                            boundary_cells_with_lines);
    }
}

template <int dim>
void
DEMSolver<dim>::particle_wall_fine_search()
{
  pw_fine_search_object.pw_Fine_Search(pw_contact_candidates,
                                       pw_pairs_in_contact);
  particle_points_in_contact =
    particle_point_line_fine_search_object.Particle_Point_Fine_Search(
      particle_point_contact_candidates);
  if (dim == 3)
    {
      particle_lines_in_contact =
        particle_point_line_fine_search_object.Particle_Line_Fine_Search(
          particle_line_contact_candidates);
    }
}

template <int dim>
void
DEMSolver<dim>::particle_wall_contact_force()
{
  pw_contact_force_object->calculate_pw_contact_force(
    &pw_pairs_in_contact,
    physical_properties,
    simulation_control->get_time_step());
  particle_point_line_contact_force_object
    .calculate_particle_point_line_contact_force(&particle_points_in_contact,
                                                 physical_properties);

  if (dim == 3)
    {
      particle_point_line_contact_force_object
        .calculate_particle_point_line_contact_force(&particle_lines_in_contact,
                                                     physical_properties);
    }
}

template <int dim>
void
DEMSolver<dim>::finish_simulation()
{
  // Timer output
  if (parameters.timer.type == Parameters::Timer::Type::end)
    this->computing_timer.print_summary();

  // Testing
  if (parameters.test.enabled)
    {
      for (unsigned int processor_number = 0;
           processor_number < n_mpi_processes;
           ++processor_number)
        {
          if (this_mpi_process == processor_number)
            {
              visualization_object.print_xyz(particle_handler, properties);
            }
          MPI_Barrier(MPI_COMM_WORLD);
        }
    }
}

template <int dim>
void
DEMSolver<dim>::reinitialize_force(
  Particles::ParticleHandler<dim> &particle_handler)
{
  for (auto particle = particle_handler.begin();
       particle != particle_handler.end();
       ++particle)
    {
      // Getting properties of particle as local variable
      auto particle_properties = particle->get_properties();

      // Reinitializing forces and momentums of particles in the system
      particle_properties[DEM::PropertiesIndex::force_x] = 0;
      particle_properties[DEM::PropertiesIndex::force_y] = 0;

      particle_properties[DEM::PropertiesIndex::M_x] = 0;
      particle_properties[DEM::PropertiesIndex::M_y] = 0;

      if (dim == 3)
        {
          particle_properties[DEM::PropertiesIndex::force_z] = 0;
          particle_properties[DEM::PropertiesIndex::M_z]     = 0;
        }
    }
}

template <int dim>
std::shared_ptr<Insertion<dim>>
DEMSolver<dim>::set_insertion_type(const DEMSolverParameters<dim> &parameters)
{
  if (parameters.insertion_info.insertion_method ==
      Parameters::Lagrangian::InsertionInfo::InsertionMethod::uniform)
    {
      insertion_object = std::make_shared<UniformInsertion<dim>>(parameters);
    }
  else if (parameters.insertion_info.insertion_method ==
           Parameters::Lagrangian::InsertionInfo::InsertionMethod::non_uniform)
    {
      insertion_object = std::make_shared<NonUniformInsertion<dim>>(parameters);
    }
  else
    {
      throw "The chosen insertion method is invalid";
    }
  return insertion_object;
}

template <int dim>
std::shared_ptr<Integrator<dim>>
DEMSolver<dim>::set_integrator_type(const DEMSolverParameters<dim> &parameters)
{
  if (parameters.model_parameters.integration_method ==
      Parameters::Lagrangian::ModelParameters::IntegrationMethod::
        velocity_verlet)
    {
      integrator_object = std::make_shared<VelocityVerletIntegrator<dim>>();
    }
  else if (parameters.model_parameters.integration_method ==
           Parameters::Lagrangian::ModelParameters::IntegrationMethod::
             explicit_euler)
    {
      integrator_object = std::make_shared<ExplicitEulerIntegrator<dim>>();
    }
  else
    {
      throw "The chosen integration method is invalid";
    }
  return integrator_object;
}

template <int dim>
std::shared_ptr<PPContactForce<dim>>
DEMSolver<dim>::set_pp_contact_force(const DEMSolverParameters<dim> &parameters)
{
  if (parameters.model_parameters.pp_contact_force_method ==
      Parameters::Lagrangian::ModelParameters::PPContactForceModel::pp_linear)
    {
      pp_contact_force_object = std::make_shared<PPLinearForce<dim>>();
    }
  else if (parameters.model_parameters.pp_contact_force_method ==
           Parameters::Lagrangian::ModelParameters::PPContactForceModel::
             pp_nonlinear)
    {
      pp_contact_force_object = std::make_shared<PPNonLinearForce<dim>>();
    }
  else
    {
      throw "The chosen particle-particle contact force model is invalid";
    }
  return pp_contact_force_object;
}

template <int dim>
std::shared_ptr<PWContactForce<dim>>
DEMSolver<dim>::set_pw_contact_force(const DEMSolverParameters<dim> &parameters)
{
  if (parameters.model_parameters.pw_contact_force_method ==
      Parameters::Lagrangian::ModelParameters::PWContactForceModel::pw_linear)
    {
      pw_contact_force_object = std::make_shared<PWLinearForce<dim>>();
    }
  else if (parameters.model_parameters.pw_contact_force_method ==
           Parameters::Lagrangian::ModelParameters::PWContactForceModel::
             pw_nonlinear)
    {
      pw_contact_force_object = std::make_shared<PWNonLinearForce<dim>>();
    }
  else
    {
      throw "The chosen particle-wall contact force model is invalid";
    }
  return pw_contact_force_object;
}

template <int dim>
void
DEMSolver<dim>::write_output_results()
{
  const std::string folder = parameters.simulation_control.output_folder;
  const std::string particles_solution_name =
    parameters.simulation_control.output_name;
  const unsigned int iter        = simulation_control->get_step_number();
  const double       time        = simulation_control->get_current_time();
  const unsigned int group_files = parameters.simulation_control.group_files;

  // Write particles
  Visualization<dim> particle_data_out;
  particle_data_out.build_patches(particle_handler,
                                  properties_class.get_properties_name());

  write_vtu_and_pvd<0, dim>(particles_pvdhandler,
                            particle_data_out,
                            folder,
                            particles_solution_name,
                            time,
                            iter,
                            group_files,
                            mpi_communicator);

  // Write background grid
  DataOut<dim> background_data_out;

  background_data_out.attach_dof_handler(background_dh);

  // Attach the solution data to data_out object
  Vector<float> subdomain(triangulation.n_active_cells());
  for (unsigned int i = 0; i < subdomain.size(); ++i)
    subdomain(i) = triangulation.locally_owned_subdomain();
  background_data_out.add_data_vector(subdomain, "subdomain");

  const std::string grid_solution_name =
    parameters.simulation_control.output_name + "-grid";

  background_data_out.build_patches();

  write_vtu_and_pvd<dim>(grid_pvdhandler,
                         background_data_out,
                         folder,
                         grid_solution_name,
                         time,
                         iter,
                         group_files,
                         mpi_communicator);
}

template <int dim>
void
DEMSolver<dim>::solve()
{
  // Print simulation starting information
  print_initial_info();

  // Reading mesh
  read_mesh();

  // Initialize DEM body force
  Tensor<1, dim> g;

  g[0] = physical_properties.gx;
  g[1] = physical_properties.gy;
  if (dim == 3)
    {
      g[2] = physical_properties.gz;
    }

  // Finding cell neighbors
  FindCellNeighbors<dim> cell_neighbors_object;
  cell_neighbors_object.find_cell_neighbors(triangulation,
                                            cells_local_neighbor_list,
                                            cells_ghost_neighbor_list);
  // Finding boundary cells with faces
  FindBoundaryCellsInformation<dim> boundary_cell_object;
  boundary_cells_information =
    boundary_cell_object.find_boundary_cells_information(
      boundary_cells_with_faces, triangulation);

  // Finding boundary cells with lines and points
  boundary_cell_object.find_particle_point_and_line_contact_cells(
    boundary_cells_with_faces,
    triangulation,
    boundary_cells_with_lines,
    boundary_cells_with_points);

  // Setting chosen contact force, insertion and integration methods
  insertion_object        = set_insertion_type(parameters);
  integrator_object       = set_integrator_type(parameters);
  pp_contact_force_object = set_pp_contact_force(parameters);
  pw_contact_force_object = set_pw_contact_force(parameters);

  // DEM engine iterator:
  while (simulation_control->integrate())
    {
      simulation_control->print_progression(pcout);

      if (fmod(simulation_control->get_step_number(), repartition_frequency) ==
          0)
        {
          pcout << "-->Repartitionning triangulation" << std::endl;
          triangulation.repartition();

          cells_local_neighbor_list.clear();
          cells_ghost_neighbor_list.clear();
          boundary_cells_with_faces.clear();
          boundary_cells_with_lines.clear();
          boundary_cells_with_points.clear();
          boundary_cells_information.clear();

          cell_neighbors_object.find_cell_neighbors(triangulation,
                                                    cells_local_neighbor_list,
                                                    cells_ghost_neighbor_list);
          boundary_cells_information =
            boundary_cell_object.find_boundary_cells_information(
              boundary_cells_with_faces, triangulation);
          boundary_cell_object.find_particle_point_and_line_contact_cells(
            boundary_cells_with_faces,
            triangulation,
            boundary_cells_with_lines,
            boundary_cells_with_points);
        }


      // Force reinitilization
      reinitialize_force(particle_handler);

      // Keep track if particles were inserted this step
      bool particles_were_inserted = insert_particles();

      // Sort particles in cells
      if (particles_were_inserted || fmod(simulation_control->get_step_number(),
                                          contact_detection_frequency) == 0)
        {
          particle_handler.sort_particles_into_subdomains_and_cells();
        }

      particle_handler.exchange_ghost_particles();

      // Broad particle-particle contact search
      if (particles_were_inserted || fmod(simulation_control->get_step_number(),
                                          contact_detection_frequency) == 0)
        {
          pp_broad_search_object.find_PP_Contact_Pairs(
            particle_handler,
            &cells_local_neighbor_list,
            &cells_ghost_neighbor_list,
            local_contact_pair_candidates,
            ghost_contact_pair_candidates);
        }

      // Particle-wall broad contact search
      if (particles_were_inserted || fmod(simulation_control->get_step_number(),
                                          contact_detection_frequency) == 0)
        {
          particle_wall_broad_search();
        }

      if (particles_were_inserted || fmod(simulation_control->get_step_number(),
                                          contact_detection_frequency) == 0)
        {
          localize_contacts<dim>(&local_adjacent_particles,
                                 &ghost_adjacent_particles,
                                 &pw_pairs_in_contact,
                                 local_contact_pair_candidates,
                                 ghost_contact_pair_candidates,
                                 pw_contact_candidates);
        }

      if (particles_were_inserted || fmod(simulation_control->get_step_number(),
                                          contact_detection_frequency) == 0)
        {
          locate_local_particles_in_cells<dim>(particle_handler,
                                               particle_container,
                                               ghost_adjacent_particles,
                                               local_adjacent_particles,
                                               pw_pairs_in_contact,
                                               particle_points_in_contact,
                                               particle_lines_in_contact);
        }
      else
        {
          locate_ghost_particles_in_cells<dim>(particle_handler,
                                               ghost_particle_container,
                                               ghost_adjacent_particles);
        }

      // Particle-particle fine search
      if (particles_were_inserted || fmod(simulation_control->get_step_number(),
                                          contact_detection_frequency) == 0)
        {
          pp_fine_search_object.pp_Fine_Search(local_contact_pair_candidates,
                                               ghost_contact_pair_candidates,
                                               local_adjacent_particles,
                                               ghost_adjacent_particles,
                                               particle_container,
                                               neighborhood_threshold);
        }

      // Particle-particle contact force
      pp_contact_force_object->calculate_pp_contact_force(
        &local_adjacent_particles,
        &ghost_adjacent_particles,
        physical_properties,
        simulation_control->get_time_step());

      // Particles-wall fine search
      if (particles_were_inserted || fmod(simulation_control->get_step_number(),
                                          contact_detection_frequency) == 0)
        {
          particle_wall_fine_search();
        }

      // Particles-walls contact force:
      particle_wall_contact_force();

      // Integration
      integrator_object->integrate(particle_handler,
                                   g,
                                   simulation_control->get_time_step());

      // Visualization
      if (simulation_control->is_output_iteration())
        {
          write_output_results();
        }
    }

  finish_simulation();
}

template class DEMSolver<2>;
template class DEMSolver<3>;