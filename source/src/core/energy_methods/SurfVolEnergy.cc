// -*- mode:c++;tab-width:2;indent-tabs-mode:t;show-trailing-whitespace:t;rm-trailing-spaces:t -*-
// vi: set ts=2 noet:
//
// (c) Copyright Rosetta Commons Member Institutions.
// (c) This file is part of the Rosetta software suite and is made available under license.
// (c) The Rosetta software is developed by the contributing members of the Rosetta Commons.
// (c) For more information, see http://www.rosettacommons.org. Questions about this can be
// (c) addressed to University of Washington CoMotion, email: license@uw.edu.

/// @file   core/energy_methods/SurfVolEnergy.cc
/// @brief  Packing Score
/// @author Will Sheffler


//Unit headers
#include <core/energy_methods/SurfVolEnergy.hh>
#include <core/energy_methods/SurfVolEnergyCreator.hh>


#include <core/scoring/packing/surf_vol.hh>

//Package headers
#include <core/pose/Pose.hh>
#include <basic/datacache/BasicDataCache.hh>
#include <core/pose/datacache/CacheableDataType.hh>
#include <core/id/CacheableAtomID_MapVector.hh>

//numeric headers
#include <numeric/xyzVector.hh>

//utility headers

//C++ headers
#include <iostream>

#include <core/scoring/EnergyMap.hh>

//Auto Headers
#include <core/pose/init_id_map.hh>

namespace core {
namespace energy_methods {


/// @details This must return a fresh instance of the SurfVolEnergy class,
/// never an instance already in use
core::scoring::methods::EnergyMethodOP
SurfVolEnergyCreator::create_energy_method(
	core::scoring::methods::EnergyMethodOptions const &
) const {
	return utility::pointer::make_shared< SurfVolEnergy >();
}

core::scoring::ScoreTypes
SurfVolEnergyCreator::score_types_for_method() const {
	using namespace core::scoring;
	ScoreTypes sts;
	// sts.push_back( dab_sasa );
	sts.push_back( dab_sev );
	return sts;
}


//////////////////////////////////////////////////////
//@brief
//////////////////////////////////////////////////////
SurfVolEnergy::SurfVolEnergy() :
	parent( utility::pointer::make_shared< SurfVolEnergyCreator >() )
{}


//////////////////////////////////////////////////////
//@brief
//////////////////////////////////////////////////////
void
SurfVolEnergy::finalize_total_energy(
	pose::Pose & pose,
	core::scoring::ScoreFunction const &,
	core::scoring::EnergyMap & totals
) const
{
	core::scoring::packing::SurfVol sv( core::scoring::packing::get_surf_vol(pose,1.4) );
	totals[ core::scoring::dab_sasa ] = sv.tot_surf;
	totals[ core::scoring::dab_sev  ] = sv.tot_vol;
}

void
SurfVolEnergy::setup_for_derivatives(
	pose::Pose & pose,
	core::scoring::ScoreFunction const &
) const {
	std::cerr << "SurfVolEnergy::setup_for_derivatives" << std::endl;

	//using namespace core::pose::datacache::CacheableDataType;
	using namespace basic::datacache;
	using namespace id;
	using namespace numeric;
	using basic::datacache::DataCache_CacheableData;

	if ( !pose.data().has( core::pose::datacache::CacheableDataType::DAB_SASA_POSE_INFO ) ) {
		pose.data().set( core::pose::datacache::CacheableDataType::DAB_SASA_POSE_INFO, utility::pointer::make_shared< CacheableAtomID_MapVector >() );
		pose.data().set(  core::pose::datacache::CacheableDataType::DAB_SEV_POSE_INFO, utility::pointer::make_shared< CacheableAtomID_MapVector >() );
	}

	CacheableDataOP dat1( pose.data().get_ptr( core::pose::datacache::CacheableDataType::DAB_SASA_POSE_INFO ) );
	CacheableAtomID_MapVectorOP cachemap1 = utility::pointer::static_pointer_cast< core::id::CacheableAtomID_MapVector > ( dat1 );
	AtomID_Map<xyzVector<Real> > & sasa_derivs(cachemap1->map());

	CacheableDataOP dat2( pose.data().get_ptr( core::pose::datacache::CacheableDataType::DAB_SASA_POSE_INFO ) );
	CacheableAtomID_MapVectorOP cachemap2 = utility::pointer::static_pointer_cast< core::id::CacheableAtomID_MapVector > ( dat2 );
	AtomID_Map<xyzVector<Real> > & sev_derivs(cachemap2->map());

	core::scoring::packing::SurfVolDeriv svd( core::scoring::packing::get_surf_vol_deriv( pose, 1.4 ) );

	core::pose::initialize_atomid_map_heavy_only(sasa_derivs,pose);
	core::pose::initialize_atomid_map_heavy_only( sev_derivs,pose);

	for ( Size ir = 1; ir <= sasa_derivs.size(); ir++ ) {
		for ( Size ia = 1; ia <= sasa_derivs.n_atom(ir); ia++ ) {
			AtomID const i(ia,ir);
			sasa_derivs[i] = svd.dsurf[i];
			sev_derivs[i] = svd. dvol[i];
		}
	}
}


void
SurfVolEnergy::eval_atom_derivative(
	id::AtomID const & aid,
	pose::Pose const & pose,
	kinematics::DomainMap const &,
	core::scoring::ScoreFunction const &,
	core::scoring::EnergyMap const & weights,
	Vector & F1,
	Vector & F2
) const {
	// std::cerr << "SurfVolEnergy::eval_atom_derivative " << aid << std::endl;
	using namespace basic::datacache;
	using namespace id;
	using namespace numeric;

	CacheableDataCOP dat1( pose.data().get_const_ptr( core::pose::datacache::CacheableDataType::DAB_SASA_POSE_INFO ) );
	CacheableAtomID_MapVectorCOP cachemap1 = utility::pointer::static_pointer_cast< core::id::CacheableAtomID_MapVector const > ( dat1 );
	AtomID_Map<xyzVector<Real> > const & sasa_derivs(cachemap1->map());

	CacheableDataCOP dat2( pose.data().get_const_ptr( core::pose::datacache::CacheableDataType::DAB_SASA_POSE_INFO ) );
	CacheableAtomID_MapVectorCOP cachemap2 = utility::pointer::static_pointer_cast< core::id::CacheableAtomID_MapVector const > ( dat2 );
	AtomID_Map<xyzVector<Real> > const & sev_derivs(cachemap2->map());

	if ( aid.rsd() > sasa_derivs.size() || aid.atomno() > sasa_derivs.n_atom(aid.rsd()) ) {
		return;
	}
	// std::cerr << "eval_atom_derivative " << aid << " " << derivs[aid].x() << " " << derivs[aid].y() << " " << derivs[aid].z() << std::endl;
	{
		numeric::xyzVector<core::Real> atom_x = pose.xyz(aid);
		numeric::xyzVector<core::Real> const f2( sasa_derivs[aid] );
		numeric::xyzVector<core::Real> const atom_y = atom_x - f2;   // a "fake" atom in the direcion of the gradient
		numeric::xyzVector<core::Real> const f1( atom_x.cross( atom_y ) );
		F1 += weights[ core::scoring::dab_sasa ] * f1;
		F2 += weights[ core::scoring::dab_sasa ] * f2;
	}
	{
		numeric::xyzVector<core::Real> atom_x = pose.xyz(aid);
		numeric::xyzVector<core::Real> const f2( sev_derivs[aid] );
		numeric::xyzVector<core::Real> const atom_y = atom_x - f2;   // a "fake" atom in the direcion of the gradient
		numeric::xyzVector<core::Real> const f1( atom_x.cross( atom_y ) );
		F1 += weights[ core::scoring::dab_sev ] * f1;
		F2 += weights[ core::scoring::dab_sev ] * f2;
	}
}

core::Size
SurfVolEnergy::version() const
{
	return 1; // Initial versioning
}


} // scoring
} // core
