#include "ipopt_planner.hpp"
#include <cmath>

#define DEL_T 1
#define A_THRESH 2
#define T_THRESH 0.1

double XG = 0, YG = 0;
extern double curr_acc;
turtlesim::Pose g_runner, g_chaser;

std::tuple<CppAD::AD<double>, CppAD::AD<double>> get_coordinates(CppAD::AD<double> x, CppAD::AD<double> y, CppAD::AD<double> v, CppAD::AD<double> a, CppAD::AD<double> t)
{
	CppAD::AD<double> r_x {x}, r_y {y};

	r_x = x + v*DEL_T*CppAD::cos(t) + 0.5*a*DEL_T*DEL_T*CppAD::cos(t);
	r_y = x + v*DEL_T*CppAD::sin(t) + 0.5*a*DEL_T*DEL_T*CppAD::sin(t);
	
	return std::make_tuple(r_x, r_y);
}

namespace
{
	using CppAD::AD;

	class FG_eval
	{
	public:
		typedef CPPAD_TESTVECTOR(AD<double>) ADvector;

		void operator()(ADvector& fg, const ADvector& at)
		{
			assert(fg.size() == 12);
			assert(at.size() == 20);

			ADvector a(10), t(10);

			for (int i = 0; i < 20; i++)
			{
				if (i % 2 == 0)
				{
					a[i/2] = at[i];
				}
				else
				{
					t[(i - 1)/2] = at[i];
				}
			}

			ADvector x(11), y(11), v(11);

			std::tuple<CppAD::AD<double>, CppAD::AD<double>> result;

			result = get_coordinates(g_runner.x, g_runner.y, g_runner.linear_velocity, curr_acc, g_runner.theta);

			x[0] = std::get<0>(result);
			y[0] = std::get<1>(result);
			v[0] = g_runner.linear_velocity + curr_acc*DEL_T;

			for (int i = 1; i < 11; i++)
			{
				result = get_coordinates(x[i - 1], y[i - 1], v[i - 1], a[i - 1], t[i - 1]);
				x[i] = std::get<0>(result);
				y[i] = std::get<1>(result);
				v[i] = v[i - 1] + a[i - 1]*DEL_T;
			}

			for (int i = 10; i >= 0; i--)
			{
				fg[0] += pow(x[i] - XG, 2) + pow(y[i] - YG, 2);
			}

			for (int i = 0; i < 11; i++)
			{
				fg[i + 1] = pow(x[i] - g_chaser.x, 2) + pow(y[i] - g_chaser.y, 2);
			}

			return;
		}
	};
}

double theta_difference(double _reference, double _goal)
{
	_goal -= _reference;

	if (_goal < -M_PI)
	{
		_goal += 2*M_PI; 
	}
	else if (_goal > M_PI)
	{
		_goal -= 2*M_PI;
	}

	return _goal;
}

geometry_msgs::Twist get_velocity(turtlesim::Pose _runner, turtlesim::Pose _chaser, turtlesim::Pose _goal)
{
	XG = _goal.x;
	YG = _goal.y;

	g_runner = _runner;
	g_chaser = _chaser;

	using Dvector = CppAD::vector<double>;

	size_t nx = 20;
	size_t ng = 11;

	Dvector xi(nx);

	for (int i = 0; i < 10; i++)
	{
		xi[2*i] = curr_acc;
		xi[2*i + 1] = _runner.angular_velocity;
	}

	Dvector xl(nx), xu(nx);

	for (int i = 0; i < 20; i++)
	{
		if (i % 2 == 0)
		{
			xl[i] = -A_THRESH;
			xu[i] = A_THRESH;
		}
		else
		{
			xl[i] = -T_THRESH;
			xu[i] = T_THRESH;
		}
	}

	Dvector gl(ng), gu(ng);

	for (int i = 0; i < 11; i++)
	{
		gl[i] = 3;
		gu[i] = 1e10;
	}

	FG_eval fg_eval;

	std::string options;

	options += "Integer print_level  0\n";
	options += "String sb  yes\n";
	options += "Integer max_iter  50\n";
	options += "Numeric max_cpu_time  0.1\n";
	options += "Sparse true  forward\n";
	options += "Sparse true  reverse\n";
	options += "Numeric tol  1e-3\n";
	options += "String derivative_test  second-order\n";
	options += "Numeric point_perturbation_radius  0.1\n";

	CppAD::ipopt::solve_result<Dvector> solution;

	CppAD::ipopt::solve<Dvector>(options, xi, xl, xu, gl, gu, fg_eval, solution);

	geometry_msgs::Twist solved;

	solved.angular.x = 0;
	solved.angular.y = 0;
	solved.angular.z = 0;
	solved.linear.x = 0;
	solved.linear.y = 0;
	solved.linear.z = 0;

	solved.angular.z = (theta_difference(solution.x[1], _runner.theta))/DEL_T;
	double a = solution.x[0];
	curr_acc = a;

	solved.linear.x = _runner.linear_velocity + a*DEL_T;

	return solved;
}

void initialise_twist(geometry_msgs::Twist& _twist)
{
	_twist.angular.x = 0;
	_twist.angular.y = 0;
	_twist.angular.z = 0;

	_twist.linear.x = 0;
	_twist.linear.y = 0;
	_twist.linear.z = 0;
}

bool close_to(const turtlesim::Pose& _pose1, const turtlesim::Pose& _pose2)
{
	return (abs(_pose1.x - _pose2.x) <= 0.2 && abs(_pose1.y - _pose2.y) <= 0.2);
}
