// -*- mode:c++;tab-width:2;indent-tabs-mode:t;show-trailing-whitespace:t;rm-trailing-spaces:t -*-
// vi: set ts=2 noet:
//
// (c) Copyright Rosetta Commons Member Institutions.
// (c) This file is part of the Rosetta software suite and is made available under license.
// (c) The Rosetta software is developed by the contributing members of the Rosetta Commons.
// (c) For more information, see http://www.rosettacommons.org. Questions about this can be
// (c) addressed to University of Washington CoMotion, email: license@uw.edu.

/// @file  protocols/loops/loop_closure/loophash/LoopHashLoopClosureMover.cc
/// @brief loop closure using (tyka's) loophash library
/// @author Sachko Honda (honda@apl.washington.edu)

// project headers
#include <devel/loophash_loopclosure/LoopHashLoopClosureMover.hh>
#include <devel/loophash_loopclosure/LoopHashLoopClosureMoverCreator.hh>

// Project Headers
#include <core/types.hh>

#include <basic/options/option.hh>

// Utility Headers

// Unit Headers
#include <protocols/moves/Mover.fwd.hh>

#include <protocols/loops/Loops.fwd.hh>
#include <protocols/forge/remodel/RemodelMover.hh>

#include <basic/options/keys/remodel.OptionKeys.gen.hh>
#include <basic/options/keys/lh.OptionKeys.gen.hh>
#include <basic/options/keys/packing.OptionKeys.gen.hh>
#include <basic/options/keys/symmetry.OptionKeys.gen.hh>

#include <utility/string_constants.hh>
#include <utility/tag/Tag.hh>

#include <basic/Tracer.hh>

#include <core/conformation/Residue.hh>

// C++ headers
#include <string>
#include <ctime>
#include <sstream>
#if (defined WIN32) && (!defined WIN_PYROSETTA)
#include <io.h>
#else
#endif
// XSD XRW Includes
#include <utility/tag/XMLSchemaGeneration.hh>
#include <protocols/moves/mover_schemas.hh>

#include <fstream> // AUTO IWYU For ofstream

namespace devel {
namespace loophash_loopclosure {

static basic::Tracer TR( "devel.loophash_loopclosure.LoopHashLoopClosureMover" );

std::ostream& operator<< (std::ostream& out , const MyLoop & loop ) {
	out << loop.r1_ << loop.c1_ << ":" << loop.minn_ << "-" << "x" << ":" << loop.r2_ << loop.c2_ << std::endl;
	return out;
}




LoopHashLoopClosureMover::LoopHashLoopClosureMover() = default;
LoopHashLoopClosureMover::~LoopHashLoopClosureMover() = default;
void
LoopHashLoopClosureMover::apply( core::pose::Pose & pose )
{
	using namespace basic::options;
	using namespace basic::options::OptionKeys;

	if ( ! loop_insert_instruction_.empty() ) {
		if ( use_MyLoop_insert_ ) {
			std::vector<MyLoop> loops = make_loops(loop_insert_instruction_);
			make_blueprint(pose, loops, option[ OptionKeys::remodel::blueprint ].value() );
		} else {
			make_blueprint(pose, loop_insert_instruction_, option[ OptionKeys::remodel::blueprint ].value() );
		}
		loop_insert_instruction_ = ""; // Hella hacky run-once approach.
	}

	if ( !option[ remodel::blueprint ].user() ) {
		TR.Error << "remodel:blueprint must be specified" << std::endl;
		utility_exit();
	}

	// Use hashloop always because this mover is all about it.
	if ( !option[ remodel::RemodelLoopMover::use_loop_hash ] ) {
		TR.Error << "This mover requires remodel::RemodelLoopMover::use_loop_hash option to be true.  Flipping the flag." << std::endl;
		utility_exit();
	}
	if ( !option[ lh::db_path ].user() ) {
		TR.Error << "loophash_db_path (path to the loophash library) must be specified." << std::endl;
		utility_exit();
	}

	remodel_ = utility::pointer::make_shared< protocols::forge::remodel::RemodelMover >();
	remodel_->apply(pose);
}
protocols::moves::MoverOP
LoopHashLoopClosureMover::clone() const {
	return utility::pointer::make_shared< LoopHashLoopClosureMover >( *this );
}

protocols::moves::MoverOP
LoopHashLoopClosureMover::fresh_instance() const {
	return utility::pointer::make_shared< LoopHashLoopClosureMover >();
}
const std::vector<std::string>
LoopHashLoopClosureMover::tokenize( const std::string& in_str,
	const std::string& delimiters ) const
{
	using std::endl;
	using std::map;
	using std::vector;
	using std::string;
	vector<string> tokens;

	if ( delimiters == "" ) return tokens;
	if ( in_str == "" ) return tokens;

	map<char, char> delim_lookup;
	for ( char delimiter : delimiters ) {
		delim_lookup[delimiter] = delimiter;
	}

	std::string token = "";
	core::Size n = in_str.size();
	for ( core::Size i=0; i<n; i++ ) {
		if ( delim_lookup.find( in_str[i] ) != delim_lookup.end() ) {
			if ( token != "" ) {
				tokens.push_back(token);
				token = "";
			}
		} else {
			token += in_str[i];
		}
	}
	if ( token != "" ) {
		tokens.push_back(token);
	}
	return tokens;
}

// r1c1:n:r2c2
//
// ultimately we want to support "r1:c1:n1-n2:r2:c2" format
const std::vector<MyLoop>
LoopHashLoopClosureMover::make_loops(const std::string & in_str) const {
	using namespace std;

	std::vector<std::string> tuples = tokenize(in_str, " ,");
	for ( core::Size i=0; i<tuples.size(); i++ ) {
		TR << "tuple[" << i << "] = " << tuples[i] << endl;
	}
	// iterate over tuples and create a Loop object for each
	vector<MyLoop> loop_list;
	for ( auto & tuple : tuples ) {
		std::vector<std::string> rclrc = tokenize(tuple, ": " );
		runtime_assert(rclrc.size() >=3);
		std::vector<std::string> r1_vec = tokenize( rclrc[0], utility::UPPERCASE_LETTERS );
		std::vector<std::string> c1_vec = tokenize( rclrc[0], utility::NUMERALS );
		std::vector<std::string> r2_vec = tokenize( rclrc[2], utility::UPPERCASE_LETTERS );
		std::vector<std::string> c2_vec = tokenize( rclrc[2], utility::NUMERALS );

		MyLoop loop(atoi(r1_vec[0].c_str()),    //r1
			(c1_vec[0].c_str()[0]), //c1
			atoi(rclrc[1].c_str()),    //min len
			atoi(rclrc[1].c_str()),    //max len
			atoi(r2_vec[0].c_str()),    //r2
			(c2_vec[0].c_str()[0]));//c2
		loop_list.push_back( loop );
		//TR << loop << std::endl;
	}
	return loop_list;
}

void
LoopHashLoopClosureMover::make_blueprint( const core::pose::Pose& pose,
	const std::string& loop_insert_instruction,
	const std::string& bpname ) const {
	static std::string const chains( " " + utility::UPPERCASE_ALPHANUMERICS );

	std::ofstream bp( bpname.c_str() );
	runtime_assert( bp.good() );
	int loop_len = 0;
	size_t iChar=0;
	char cur_char = loop_insert_instruction[iChar];
	for ( size_t i=1; i<= pose.size(); ++i ) {
		core::conformation::Residue const & rsd( pose.residue(i) );
		if ( chains[rsd.chain()] == cur_char ) {
			if ( ( loop_insert_instruction.length() > iChar + 1 && loop_insert_instruction[iChar + 1] < 'A' )
					&& ( i+1 < pose.size() && chains[ pose.residue(i+1).chain() ] != cur_char  ) ) {
				bp << i << " A L " << std::endl;
			} else {
				bp << i << " A . " << std::endl;
			}
		} else if ( loop_insert_instruction.length() < iChar + 1 ) {
			bp << i << " A . " << std::endl;
		} else {
			cur_char = loop_insert_instruction[++iChar];

			if ( cur_char >= '1' && cur_char <= '9' ) {
				std::string alphanum = "";
				while ( cur_char >= '1' && cur_char <= '9' ) {
					alphanum += cur_char;
					cur_char = loop_insert_instruction[++iChar];
				}
				loop_len = atoi(alphanum.c_str());
				for ( size_t j=0; j<(size_t)loop_len; ++j ) {
					bp << "0 x L" << std::endl;
				}
				bp << i << " A L" << std::endl;
			}
		}
	}
	bp.close();
	return ;
}
void
LoopHashLoopClosureMover::make_blueprint( const core::pose::Pose& pose,
	const std::vector<MyLoop> & loops,
	const std::string& bpname) const {
	static std::string const chains( " " + utility::UPPERCASE_ALPHANUMERICS );

	// Make a fast lookup table by staring res number.
	// Validate the start and end res numbers relationship at the same time.
	// As of Mar 6, 2013, this system cannot build a loop between non-adjacent residues.
	std::map<core::Size, MyLoop> lookup_by_r1;
	for ( auto const & loop : loops ) {
		lookup_by_r1[loop.r1_] = loop;
	}
	std::ofstream bp( bpname.c_str() );
	runtime_assert( bp.good() );
	for ( size_t i=1; i<= pose.size(); ++i ) {
		if ( lookup_by_r1.find(i) == lookup_by_r1.end() ) {
			bp << i << " A ." << std::endl;
			continue;
		}
		MyLoop loop = lookup_by_r1[i];
		if ( loop.c1_ != chains[ pose.residue(i).chain() ] ) {
			TR.Error << "Residue " << loop.r1_ << " is not in the chain " << loop.c1_ << ".  Ignore the loop creating instruction." << std::endl;
			utility_exit();
		}
		if ( loop.c2_ != chains[ pose.residue(loop.r2_).chain() ] ) {
			TR.Error << "Residue " << loop.r2_ << " is not in the chain " << loop.c1_ << ".  Ignore the loop creating instruction." << std::endl;
			utility_exit();
		}
		bp << i << " A L" << std::endl;
		// replace the following residues upto the loop terminal with Ls
		for ( core::Size j=0; j<loop.minn_; ++j ) {
			bp << "0 x L" << std::endl;
		}
		bp << loop.r2_ << " A L" << std::endl;
		// jump to the terminal so that the for loop (++i) starts from the next residue.
		i= loop.r2_;
	}
	bp.close();
	return;
}

void LoopHashLoopClosureMover::parse_my_tag( utility::tag::TagCOP tag,
	basic::datacache::DataMap &
) {

	using namespace basic::options;
	using namespace basic::options::OptionKeys;

	// This mover has multiple modes of specifing loops which are mutually exclusive
	// * loop_insert := loop length in between chains
	// * blueprint := blueprint file
	// * loop_insert_rclrc := Residue-Chain-Length
	// Currently, fixed loop length is supported.
	if ( tag->hasOption("loop_insert") + tag->hasOption("loop_insert_rclrc") + tag->hasOption("blueprint") > 1 ) {
		TR.Error << "\"loop_insert\", \"loop_insert_rclrc\" and \"blueprint\" options are mutually exclusive." << std::endl;
		utility_exit();
	}

	// Create a process-unique filename for dummy blueprint,
	// and let multiple threads, if there is any, share one.
	//pid_t pid = getpid();
	std::stringstream ss;
	ss << tag->getOption("name", get_current_tag());
	//ss << "_" << pid;
	ss << ".bp";
	std::string bpname = ss.str().c_str();
	TR << "Dummy blueprint: " << bpname << std::endl;

	protocols::loops::LoopsOP loops;
	if ( tag->hasOption("loop_insert_rclrc") ) {
		// Instruction string in Residue:Chain:Length format:
		// e.g.  25:A:6,50:B:7 for a loop of size 6 residues after 25 (and before 26, implicit)
		// and another of size 7 residues between 50 and 51.
		loop_insert_instruction_ = tag->getOption<std::string>("loop_insert_rclrc");
		use_MyLoop_insert_ = true;
		TR << "Use loop_insert_rclrc string \"" << loop_insert_instruction_ << "\" (and generate " << bpname << " blueprint file)." << std::endl;
	} else if ( tag->hasOption("loop_insert") ) {
		// Instruction string in Between Chains format:
		// e.g. "A6B7CDE" to insert a loop of size 6 between chain A and B and another of 7 between B and C.
		loop_insert_instruction_ = tag->getOption<std::string>("loop_insert");
		use_MyLoop_insert_ = false;
		TR << "User loop_insert string \"" << loop_insert_instruction_ << "\" (and generate " << bpname << " blueprint file)." << std::endl;
	} else if ( tag->hasOption("blueprint") ) {
		bpname = tag->getOption<std::string>("blueprint");
		TR << "Use blueprint file: " << bpname << std::endl;
	}
	if ( bpname == "" ) {
		TR.Error << "You must specify either \"loop_insert\" string or blueprint!" << std::endl;
		utility_exit();
	}
	option[ OptionKeys::remodel::blueprint ]( bpname );
	TR << "remodel::blueprint = " << bpname << std::endl;

	// path to the loophash library
	if ( !tag->hasOption( "loophash_db_path" ) ) {
		TR.Error << "loophash_db_path (path to the loophash library) must be specified." << std::endl;
		utility_exit();
	}
	std::string loophash_db_path = tag->getOption<std::string>( "loophash_db_path" );
	option[ lh::db_path ]( loophash_db_path );
	TR << "lh:db_path = " << loophash_db_path << std::endl;

	// Use hashloop always because this mover is all about it.
	if ( option[ remodel::RemodelLoopMover::use_loop_hash ].user() && !option[ remodel::RemodelLoopMover::use_loop_hash ] ) {
		TR.Warning << "remodel::RemodelLoopMover::use_loop_hash is given false, but this mover requires true. Override user setting." << std::endl;
		TR.flush();
	}
	option[ remodel::RemodelLoopMover::use_loop_hash ]( true ); //JAB - this is not good at all.  gah.
	TR << "remodel::RemodelLoopMover::use_loop_hash = true" << std::endl;

	// loop extension limit
	auto loophash_ex_limit = tag->getOption<core::Size>( "loophash_ex_limit", 4 );
	option[ remodel::lh_ex_limit ]( loophash_ex_limit );
	TR << "remodel::lh_ex_limit = " << loophash_ex_limit << std::endl;

	// Max radius.  This should be >= lh_ex_limit.  Default is hard-coded somewhere to 5.
	core::Size max_radius = loophash_ex_limit + 2;
	if ( option[ lh::max_radius ].user() ) {
		if ( static_cast<core::Size>(option[ lh::max_radius ]) <= loophash_ex_limit ) {
			TR << "lh::max_radius must be >= loophash_ex_limit.  Adjust to +2: " << max_radius << std::endl;
			option[ lh::max_radius ]( max_radius );
		}
	} else {
		option[ lh::max_radius ]( max_radius );
	}
	TR << "lh::max_radius = " << option[ lh::max_radius ] << std::endl;
	/*
	// Disable CCD options if they are set.
	if( option[ remodel::RemodelLoopMover::independent_cycles].user() && option[ remodel::RemodelLoopMover::independent_cycles ]  ) {
	TR.Warning << "Conflicting options.  Turning off remodel::RemodelLoopMover::independent_cycles." << std::endl;
	}
	option[ remodel::RemodelLoopMover::independent_cycles ]( false );
	TR << "remodel::RemodelLoopMover::independent_cycles = " << option[ remodel::RemodelLoopMover::independent_cycles ] << std::endl;

	if( option[ remodel::RemodelLoopMover::simultaneous_cycles ].user() && option[ remodel::RemodelLoopMover::simultaneous_cycles ] ) {
	TR.Warning << "Conflicting options.  Turning off remodel::RemodelLoopMover::simultaneous_cycles." << std::endl;
	}
	option[ remodel::RemodelLoopMover::simultaneous_cycles ] ( false );
	TR << "remodel::RemodelLoopMover::simultaneous_cycles = " << option[ remodel::RemodelLoopMover::simultaneous_cycles ] << std::endl;

	if( option[ remodel::RemodelLoopMover::bypass_closure ].user() && !option[ remodel::RemodelLoopMover::bypass_closure ] ) {
	TR.Warning << "Conflicting options.  Turning ON remodel::RemodelLoopMover::bypass_closure." << std::endl;
	}
	option[ remodel::RemodelLoopMover::bypass_closure ]( true );
	TR << "remodel::RemodelLoopMover::bypass_closure = " << option[ remodel::RemodelLoopMover::bypass_closure ] << std::endl;

	if( option[ remodel::RemodelLoopMover::boost_closure_cycles ].user() && option[ remodel::RemodelLoopMover::boost_closure_cycles ] ) {
	TR.Warning << "Conflicting options.  Turning off remodel::RemodelLoopMover::boost_closure_cycles." << std::endl;
	}
	option[ remodel::RemodelLoopMover::boost_closure_cycles ]( false );
	TR << "remodel::RemodelLoopMover::boost_closure_cycles = " << option[ remodel::RemodelLoopMover::boost_closure_cycles ] << std::endl;

	// reduce loophash cycles to 1 if greater, which won't do any good here.
	if( option[ remodel::RemodelLoopMover::loophash_cycles ].user() ) {
	if( option[ remodel::RemodelLoopMover::loophash_cycles ] > 1 ) {
	TR.Warning << "Conflicting options.  Reducing loophash cycles to one." << std::endl;
	option[ remodel::RemodelLoopMover::loophash_cycles ] ( 1 );
	}
	}
	else{
	option[ remodel::RemodelLoopMover::loophash_cycles ]( 1 );
	}
	TR << "loophash_cycles = " << option[ remodel::RemodelLoopMover::loophash_cycles ] << std::endl;
	*/

	// quick dirty?
	bool is_quick_and_dirty = tag->getOption<bool>("quick_and_dirty", true);
	option[ remodel::quick_and_dirty ]( is_quick_and_dirty );
	TR << "remodel::quick_and_dirty = " << (is_quick_and_dirty? "true" : "false") << std::endl;

	//
	// Symmetry and Repeat_structure are mutually exclusive
	//
	if ( tag->hasOption("symmetry_definition") && tag->hasOption("repeat_structure") ) {
		TR.Error << "\"symmetry_definition\" and \"repeat_structure\" are mutually exclusive." << std::endl;
		utility_exit();
	}

	// Is the structure to be symmetric?
	if ( tag->hasOption("symmetry_definition") ) {
		std::string symm_def = tag->getOption<std::string>("symmetry_definition");
		option[ symmetry::symmetry_definition ]( symm_def );
		TR << "symmetry::symmetry_definition = " << symm_def << std::endl;
	} else {
		if ( option[ symmetry::symmetry_definition ].user() ) {
			TR.Error << "Conflict of interest!  symmetry::symmetry_definition option is already defined: " << option[symmetry::symmetry_definition].value() << std::endl;
			TR.flush();
			utility_exit();
		}
	}

	// How many times should the structure be repeated?
	if ( tag->hasOption("repeat_structure") ) {
		core::Size repeat_structure = tag->getOption<int>("repeat_structure");
		option[ remodel::repeat_structure ] (repeat_structure);
		TR << "remodel::repeat_structure = " << repeat_structure << std::endl;
	}

	if ( tag->hasOption("lh_filter_string") ) {
		std::string abego = tag->getOption<std::string>("lh_filter_string");
		option[ remodel::lh_filter_string ](abego);
		TR << "remodel::lh_filter_string = " << abego << std::endl;
	}

	auto linear_chainbreak = tag->getOption<numeric::Real>("max_linear_chainbreak", 0.07);
	option[ remodel::RemodelLoopMover::max_linear_chainbreak ]( linear_chainbreak );
	TR << "remodel::max_linear_chainbreak = " << linear_chainbreak << std::endl;

	if ( option[ remodel::lh_cbreak_selection ].user() ) {
		if ( option[ remodel::lh_cbreak_selection ] < 10 ) {
			TR.Warning << "Recommended chainbreak_selection weight is >= 10.  Your current values is " << option[ remodel::lh_cbreak_selection]  <<  std::endl;
		}
	} else {
		option[ remodel::lh_cbreak_selection ]( 10 );
	}
	TR << "lh_cbreak_selection = " << option[ remodel::lh_cbreak_selection ] << std::endl;

	// Use sidechains from input
	TR << "packing::use_input_sc = " << (option[packing::use_input_sc].value()? "true" : "false") << std::endl;

	// number of trajectories
	auto num_trajectory = tag->getOption<core::Size>("num_trajectory", 1);
	option[ remodel::num_trajectory ]( num_trajectory );
	TR << "remodel::num_trajectory = " << num_trajectory << std::endl;

	// keep the top n scores
	auto num_save_top = tag->getOption<core::Size>("save_top", 1);
	option[ remodel::save_top ](num_save_top);
	TR << "remodel::save_top = " << num_save_top << std::endl;

	// No optimization on hydrogens
	TR << "packing::no_optH = " << (option[packing::no_optH] ? "true" : "false") << std::endl;
}

std::string LoopHashLoopClosureMover::get_name() const {
	return mover_name();
}

std::string LoopHashLoopClosureMover::mover_name() {
	return "LoopHashLoopClosureMover";
}

void LoopHashLoopClosureMover::provide_xml_schema( utility::tag::XMLSchemaDefinition & xsd )
{
	using namespace utility::tag;
	AttributeList attlist;
	attlist + XMLSchemaAttribute(
		"loop_insert", xs_string,
		"Instruction string in Between Chains format: "
		"e.g. \"A6B7CDE\" to insert a loop of size 6 between chain A and B and another of 7 between B and C "
		"loop_insert, loop_insert_rclrc, and blueprint are mutually exclusive options");

	attlist + XMLSchemaAttribute(
		"loop_insert_rclrc", xs_string,
		"Instruction string in Residue:Chain:Length format: "
		"e.g. 25:A:6,50:B:7 for a loop of size 6 residues after 25 (and before 26, implicit) "
		"and another of size 7 residues between 50 and 51. "
		"loop_insert, loop_insert_rclrc, and blueprint are mutually exclusive options");

	attlist + XMLSchemaAttribute(
		"blueprint", xs_string,
		"loop_insert, loop_insert_rclrc, and blueprint are mutually exclusive options");

	attlist + XMLSchemaAttribute::required_attribute(
		"loophash_db_path", xs_string,
		"path to the loophash library");

	attlist + XMLSchemaAttribute::attribute_w_default(
		"loophash_ex_limit", xsct_non_negative_integer,
		"loop extension limit",
		"4");

	attlist + XMLSchemaAttribute::attribute_w_default(
		"quick_and_dirty", xsct_rosetta_bool,
		"remodel quick and dirty?",
		"true");

	attlist + XMLSchemaAttribute(
		"symmetry_definition", xs_string,
		"The structure is symmetric. Specify symmetry definition. "
		"symmetry_definition and repeat_strucutre are mutually exclusive");

	attlist + XMLSchemaAttribute(
		"repeat_structure", xs_integer,
		"Number of times the structure should be repeated. "
		"symmetry_definition and repeat_strucutre are mutually exclusive");

	attlist + XMLSchemaAttribute(
		"lh_filter_string", xs_string,
		"XSD XRW: TO DO");

	attlist + XMLSchemaAttribute::attribute_w_default(
		"max_linear_chainbreak", xsct_real,
		"XSD XRW: TO DO",
		"0.07");

	attlist + XMLSchemaAttribute::attribute_w_default(
		"num_trajectory", xsct_non_negative_integer,
		"number of trajectories",
		"1");

	attlist + XMLSchemaAttribute::attribute_w_default(
		"save_top", xsct_non_negative_integer,
		"keep top n scores",
		"1");

	protocols::moves::xsd_type_definition_w_attributes(
		xsd, mover_name(),
		"Close loops using loophash library",
		attlist );
}

std::string LoopHashLoopClosureMoverCreator::keyname() const {
	return LoopHashLoopClosureMover::mover_name();
}

protocols::moves::MoverOP
LoopHashLoopClosureMoverCreator::create_mover() const {
	return utility::pointer::make_shared< LoopHashLoopClosureMover >();
}

void LoopHashLoopClosureMoverCreator::provide_xml_schema( utility::tag::XMLSchemaDefinition & xsd ) const
{
	LoopHashLoopClosureMover::provide_xml_schema( xsd );
}


} // loophash_loopclosure
} // devel

