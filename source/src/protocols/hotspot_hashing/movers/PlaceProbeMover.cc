// -*- mode:c++;tab-width:2;indent-tabs-mode:t;show-trailing-whitespace:t;rm-trailing-spaces:t -*-
// vi: set ts=2 noet:
//
// (c) Copyright Rosetta Commons Member Institutions.
// (c) This file is part of the Rosetta software suite and is made available under license.
// (c) The Rosetta software is developed by the contributing members of the Rosetta Commons.
// (c) For more information, see http://www.rosettacommons.org. Questions about this can be
// (c) addressed to University of Washington CoMotion, email: license@uw.edu.

/// @file protocols/hotspot_hashing/movers/PlaceProbeMover.cc
/// @author Alex Ford fordas@uw.edu


#include <sstream>

#include <basic/Tracer.hh>

#include <boost/algorithm/string.hpp>

#include <utility>
#include <utility/tag/Tag.hh>
#include <utility/exit.hh>

#include <core/conformation/Residue.hh>
#include <core/pose/Pose.hh>
#include <core/pose/variant_util.hh>
#include <core/pack/task/TaskFactory.hh>
#include <core/pack/task/PackerTask.hh>
#include <core/pack/task/operation/TaskOperations.hh>


#include <basic/datacache/DataMap.fwd.hh>
#include <protocols/minimization_packing/PackRotamersMover.hh>

#include <protocols/jd2/util.hh>

#include <protocols/rosetta_scripts/util.hh>

#include <protocols/hotspot_hashing/SearchPattern.hh>
#include <protocols/hotspot_hashing/SearchPatternRotSetOp.hh>

#include <protocols/hotspot_hashing/StubGenerator.hh>

#include <protocols/hotspot_hashing/movers/PlaceProbeMover.hh>

#include <sstream> // MANUAL IWYU
#include <core/pack/task/ResidueLevelTask.hh> // AUTO IWYU For ResidueLevelTask

namespace protocols
{
namespace hotspot_hashing
{
namespace movers
{

static basic::Tracer TR( "protocols.hotspot_hashing.movers.PlaceProbeMover" );

PlaceProbeMover::PlaceProbeMover() :
	residue_name_(""),
	target_residue_(/* NULL */),
	current_mode_(RunAll),
	search_partition_(0),
	total_search_partition_(1),
	initialized_pattern_(false),
	search_points_()
{}

PlaceProbeMover::PlaceProbeMover(
	std::string const & residue_name,
	core::conformation::ResidueCOP target_residue,
	core::Size search_partition,
	core::Size total_search_partition) :
	residue_name_(residue_name),
	target_residue_(std::move(target_residue)),
	current_mode_(RunAll),
	search_partition_(search_partition),
	total_search_partition_(total_search_partition),
	initialized_pattern_(false),
	search_points_()
{}

void PlaceProbeMover::apply(core::pose::Pose & pose)
{
	if ( target_residue_ == nullptr ) {
		// Initialize residue representation
		target_residue_ = StubGenerator::getStubByName( residue_name_ );
		target_residue_ = core::pose::add_variant_type_to_residue( *target_residue_, core::chemical::SC_FRAGMENT, pose );
	}

	check_and_initialize(pose);

	if ( current_mode_ == OnePerStruct ) {
		core::Size nstruct = protocols::jd2::current_nstruct_index();

		core::Size search_index = nstruct % search_points_.size();
		if ( search_index == 0 ) {
			search_index = search_points_.size();
		}

		execute_one_search(pose, search_index);
	} else {
		for ( core::Size i = 1; i <= search_points_.size(); i++ ) {
			core::pose::Pose tmp_pose(pose);
			execute_one_search(tmp_pose, i);
		}
	}
}

void PlaceProbeMover::execute_one_search(core::pose::Pose & pose, core::Size search_index)
{
	//TODO extract placement into a separate task
	core::kinematics::Stub transform = search_points_[search_index];

	core::Size residuejumpindex;
	core::Size residueindex;

	StubGenerator::placeResidueAtTransform(pose, target_residue_, transform, residuejumpindex, residueindex);

	{
		std::stringstream sstream;
		stub_to_points(sstream, transform);

		protocols::jd2::add_string_string_pair_to_current_job(
			"placeprobe_prerefine_centroid_stub", sstream.str());
	}

	perform_local_refinement(pose, residueindex);

	core::conformation::ResidueCOP post_refinement_residue(pose.residue(residueindex).get_self_ptr());

	{
		core::kinematics::Stub post_refinement_centroid_transform = StubGenerator::residueStubCentroidFrame(post_refinement_residue);
		std::stringstream sstream;
		stub_to_points(sstream, post_refinement_centroid_transform);

		protocols::jd2::add_string_string_pair_to_current_job(
			"placeprobe_postrefine_centroid_stub", sstream.str());
	}

	{
		core::kinematics::Stub post_refinement_orient_transform = StubGenerator::residueStubOrientFrame(post_refinement_residue);
		std::stringstream sstream;
		stub_to_points(sstream, post_refinement_orient_transform);

		protocols::jd2::add_string_string_pair_to_current_job(
			"placeprobe_postrefine_orient_stub", sstream.str());
	}

	protocols::jd2::add_string_string_pair_to_current_job(
		"placeprobe_residue_name", post_refinement_residue->name());

	protocols::jd2::add_string_string_pair_to_current_job(
		"placeprobe_residue_number", std::to_string(residueindex));
}

void PlaceProbeMover::check_and_initialize(core::pose::Pose const & target_pose)
{
	using namespace protocols::hotspot_hashing;

	if ( initialized_pattern_ ) {
		return;
	}

	initialized_pattern_ = true;

	TR.Debug << "Initializing search pattern." << std::endl;

	SearchPatternOP search_pattern = create_partitioned_search_pattern(target_pose);

	search_points_ = search_pattern->Searchpoints();

	TR.Info << "Initialized search pattern. Size: " << search_points_.size() << std::endl;

	if ( protocols::jd2::max_nstruct_index() < search_points_.size() ) {
		TR.Error << "Current job nstruct_max: " << protocols::jd2::max_nstruct_index() << " less than search pattern size: " << search_points_.size() << std::endl;
	}

	if ( protocols::jd2::max_nstruct_index() > search_points_.size() ) {
		TR.Warning << "Current job nstruct_max: " << protocols::jd2::max_nstruct_index() << " greater than search pattern size: " << search_points_.size() << " (Search points will be repeated.)" << std::endl;
	}
}

SearchPatternOP PlaceProbeMover::create_partitioned_search_pattern(core::pose::Pose const & target_pose)
{
	return utility::pointer::make_shared< PartitionedSearchPattern >(create_search_pattern(target_pose), search_partition_, total_search_partition_);
}

SearchPatternOP PlaceProbeMover::create_refinement_pattern(core::pose::Pose const & /*target_pose*/, core::Size /*target_residue*/)
{
	return utility::pointer::make_shared< ConstPattern >();
}

void PlaceProbeMover::perform_local_refinement(core::pose::Pose & target_pose, core::Size target_residue)
{
	core::pack::task::PackerTaskOP packer_task = create_refinement_packing_task(target_pose, target_residue);

	protocols::minimization_packing::PackRotamersMoverOP packer( new protocols::minimization_packing::PackRotamersMover(refinement_scorefxn_, packer_task) );

	packer->apply(target_pose);
}

core::pack::task::PackerTaskOP PlaceProbeMover::create_refinement_packing_task(core::pose::Pose const & target_pose, core::Size target_residue)
{
	TR.Debug << "Creating refinement packing task." << std::endl;
	core::pack::task::TaskFactory taskfactory;
	using core::pack::task::operation::TaskOperationCOP;

	taskfactory.push_back( utility::pointer::make_shared< core::pack::task::operation::InitializeFromCommandline >() );
	taskfactory.push_back( utility::pointer::make_shared< AddSearchPatternRotSetOp >(
		target_residue,
		create_refinement_pattern(target_pose, target_residue)));

	core::pack::task::PackerTaskOP task = taskfactory.create_task_and_apply_taskoperations( target_pose );

	utility::vector1<bool> packmask(target_pose.size(), false);
	packmask[target_residue] = true;
	task->restrict_to_residues(packmask);

	task->nonconst_residue_task(target_residue).restrict_to_repacking();

	return task;
}

void
PlaceProbeMover::parse_place_probe_tag( utility::tag::TagCOP tag,
	basic::datacache::DataMap & data
) {
	// Residue spec
	if ( tag->hasOption("residue_name") ) {
		residue_name_ = tag->getOption<std::string>("residue_name");
	} else {
		utility_exit_with_message( "residue_name not specified" );
	}

	// Partition Spec
	search_partition_ = tag->getOption< core::Size >( "search_partition", static_cast<core::Size>(0) );
	total_search_partition_ = tag->getOption< core::Size >( "total_search_partition", 1 );

	if ( !(search_partition_ < total_search_partition_ && total_search_partition_ > 0) ) {
		TR.Error << "Invalid search partition specficition. Partition: " << search_partition_ << " Total partitions: " << total_search_partition_ << std::endl;

		utility_exit_with_message("Invalid search partition specification.");
	}

	std::string mode_specification = tag->getOption<std::string>("execution_mode", "all");
	boost::algorithm::to_lower(mode_specification);

	if ( mode_specification == "all" ) {
		current_mode_ = RunAll;
	} else if ( mode_specification == "one" ) {
		current_mode_ = OnePerStruct;
	} else {
		TR.Error << "Invalid mode specification: " << mode_specification << std::endl;
		utility_exit_with_message("Invalid mode specification: " + mode_specification);
	}

	// Refinement scorefunction
	refinement_scorefxn_ = protocols::rosetta_scripts::parse_score_function( tag, data );

}


PlaceProbeMover::StructureOutputMode PlaceProbeMover::parse_output_mode(std::string name)
{
	boost::algorithm::to_lower(name);

	if ( name == "none" ) {
		return None;
	} else if ( name == "probe" ) {
		return Probe;
	} else if ( name == "full" ) {
		return Full;
	} else {
		TR.Error << "Invalid output mode specification: " << name << std::endl;
		utility_exit_with_message("Invalid output mode specification: " + name);
	}
}

}
}
}
