// -*- mode:c++;tab-width:2;indent-tabs-mode:t;show-trailing-whitespace:t;rm-trailing-spaces:t -*-
// vi: set ts=2 noet:
//
// (c) Copyright Rosetta Commons Member Institutions.
// (c) This file is part of the Rosetta software suite and is made available under license.
// (c) The Rosetta software is developed by the contributing members of the Rosetta Commons.
// (c) For more information, see http://www.rosettacommons.org. Questions about this can be
// (c) addressed to University of Washington CoMotion, email: license@uw.edu.

/// @file protocols/sewing/requirements/LengthInResiduesRequirement.fwd.hh
/// @brief a Requirement that an Assembly be within a certain range of lengths
/// @author frankdt (frankdt@email.unc.edu)


#ifndef INCLUDED_protocols_sewing_requirements_LengthInResiduesRequirement_fwd_hh
#define INCLUDED_protocols_sewing_requirements_LengthInResiduesRequirement_fwd_hh

// Utility headers
#include <utility/pointer/owning_ptr.hh>



// Forward
namespace protocols {
namespace sewing {
namespace requirements {

class LengthInResiduesRequirement;

typedef utility::pointer::shared_ptr< LengthInResiduesRequirement > LengthInResiduesRequirementOP;
typedef utility::pointer::shared_ptr< LengthInResiduesRequirement const > LengthInResiduesRequirementCOP;



} //protocols
} //sewing
} //requirements


#endif //INCLUDED_protocols_sewing_requirements_LengthInResiduesRequirement_fwd_hh





