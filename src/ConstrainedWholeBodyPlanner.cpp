#include <dwl_planners/ConstrainedWholeBodyPlanner.h>


namespace dwl_planners
{

ConstrainedWholeBodyPlanner::ConstrainedWholeBodyPlanner()
{
	desired_state_.base_pos << 0, 0, 0, 0, 0, -0.1;
	current_state_.joint_pos = Eigen::VectorXd::Zero(2);
	current_state_.joint_pos << 0.6, -1.5;
	current_state_.joint_vel = Eigen::VectorXd::Zero(2);
	current_state_.joint_acc = Eigen::VectorXd::Zero(2);
	current_state_.joint_eff = Eigen::VectorXd::Zero(2);
	current_state_.joint_eff << 9.33031, 27.6003;
}


ConstrainedWholeBodyPlanner::~ConstrainedWholeBodyPlanner()
{

}


void ConstrainedWholeBodyPlanner::init()
{
	// Declaring the whole-body trajectory publisher
	motion_plan_pub_ = node_.advertise<dwl_planners::WholeBodyTrajectory>("whole_body_trajectory", 1);


	// Initializes Ipopt solver
	dwl::solver::IpoptNLP* ipopt_solver = new dwl::solver::IpoptNLP();
	dwl::solver::OptimizationSolver* solver = ipopt_solver;

	planning_.init(solver);


	// Initializes the dynamical system constraint
	std::string model_file = "/home/cmastalli/ros_workspace/src/dwl/thirdparty/rbdl/hyl.urdf";
	dwl::model::ConstrainedDynamicalSystem* system = new dwl::model::ConstrainedDynamicalSystem();
	dwl::model::DynamicalSystem* dynamical_system = system;


	dwl::rbd::BodySelector active_contact;
	active_contact.push_back("foot");
	system->setActiveEndEffectors(active_contact);

	dynamical_system->modelFromURDFFile(model_file, true);






	dwl::LocomotionState weights;
	weights.base_pos = 1000 * dwl::rbd::Vector6d::Ones();
	weights.base_vel = 10 * dwl::rbd::Vector6d::Ones();
	weights.joint_eff = 0.001 * Eigen::Vector2d::Ones();


	dwl::model::Cost* state_tracking_cost = new dwl::model::StateTrackingEnergyCost();
	state_tracking_cost->setWeights(weights);
	dwl::model::Cost* control_cost = new dwl::model::ControlEnergyCost();
	control_cost->setWeights(weights);


	planning_.addDynamicalSystem(dynamical_system);
	planning_.addCost(state_tracking_cost);
	planning_.addCost(control_cost);


	planning_.setHorizon(3);
}


bool ConstrainedWholeBodyPlanner::compute()
{
	return planning_.compute(current_state_, desired_state_, 1000);
}


void ConstrainedWholeBodyPlanner::publishWholeBodyTrajectory()
{
	// Publishing the motion plan if there is at least one subscriber
	if (motion_plan_pub_.getNumSubscribers() > 0) {
		robot_trajectory_msg_.header.stamp = ros::Time::now();

		// Filling the current state
		writeWholeBodyStateMessage(robot_trajectory_msg_.current_state,
								   current_state_);

		// Filling the trajectory message
		std::vector<dwl::LocomotionState> trajectory = planning_.getWholeBodyTrajectory();
		robot_trajectory_msg_.trajectory.resize(trajectory.size());
		for (unsigned int i = 0; i < trajectory.size(); i++)
			writeWholeBodyStateMessage(robot_trajectory_msg_.trajectory[i],
									   trajectory[i]);

		// Publishing the motion plan
		motion_plan_pub_.publish(robot_trajectory_msg_);
	}
}



void ConstrainedWholeBodyPlanner::writeWholeBodyStateMessage(dwl_planners::WholeBodyState& msg,
															 const dwl::LocomotionState& state)
{
	// Getting the floating-base system information
	dwl::rbd::FloatingBaseSystem system = planning_.getDynamicalSystem()->getFloatingBaseSystem();

	// Filling the time information
	msg.time = state.time;

	// Filling the base state
	msg.base_ids.resize(system.getFloatingBaseDoF());
	msg.base_names.resize(system.getFloatingBaseDoF());
	msg.base.resize(6);
	unsigned int counter = 0;
	for (unsigned int base_idx = 0; base_idx < 6; base_idx++) {
		if (system.getFloatingBaseJoint((dwl::rbd::Coords6d) base_idx).active) {
			msg.base_ids[counter] = base_idx;
			msg.base_names[counter] = system.getFloatingBaseJoint((dwl::rbd::Coords6d) base_idx).name;
			counter++;
		}
	}
	for (unsigned int base_idx = 0; base_idx < 6; base_idx++) {
		msg.base[base_idx].position = state.base_pos(base_idx);
		msg.base[base_idx].velocity = state.base_vel(base_idx);
		msg.base[base_idx].acceleration = state.base_acc(base_idx);
	}

	// Filling the joint state
	msg.joints.resize(system.getJointDoF());
	msg.joint_names.resize(system.getJointDoF());
	for (unsigned int jnt_idx = 0; jnt_idx < system.getJointDoF(); jnt_idx++) {
		unsigned int jnt_id = system.getJoints()[jnt_idx].id;
		msg.joint_names[jnt_idx] = system.getJoints()[jnt_idx].name;
		msg.joints[jnt_idx].position = state.joint_pos(jnt_id - system.getFloatingBaseDoF());
		msg.joints[jnt_idx].velocity = state.joint_vel(jnt_id - system.getFloatingBaseDoF());
		msg.joints[jnt_idx].acceleration = state.joint_acc(jnt_id - system.getFloatingBaseDoF());
		msg.joints[jnt_idx].effort = state.joint_eff(jnt_id - system.getFloatingBaseDoF());
	}
}

} //@namespace dwl_planners




int main(int argc, char **argv)
{
	ros::init(argc, argv, "constrained_whole_body_planner");

	dwl_planners::ConstrainedWholeBodyPlanner planner;

	planner.init();
	ros::spinOnce();

	try {
		ros::Rate loop_rate(0.5);
//
		while (ros::ok()) {
			if (planner.compute()) {
				planner.publishWholeBodyTrajectory();
			}
			ros::spinOnce();
			loop_rate.sleep();
		}
	} catch (std::runtime_error& e) {
		ROS_ERROR("hierarchical_planner exception: %s", e.what());
		return -1;
	}

	return 0;
}

