/*
 * \brief  sched_controller
 * \author Paul Nieleck
 * \date   2016/09/09
 *
 */

/* for testing in get_utilization   */
#include <random>
#include <timer_session/connection.h>
#include <string>
/* ******************************** */

#include <forward_list>
#include <unordered_map>
#include <base/printf.h>

#include "sched_controller/sched_controller.h"
#include "sched_controller/task_allocator.h"
#include "sched_controller/monitor.h"
#include "rq_manager_session/client.h"
#include "rq_manager_session/connection.h"
#include "mon_manager/mon_manager.h"

namespace Sched_controller {

	/**
	 * Get and set the number of available physically
	 * available CPU cores of the system.
	 * This function will not change the number of
	 * _pcore objects.
	 *
	 * \return success status
	 */
	int Sched_controller::_set_num_pcores()
	{
		/* 
		 * From the monitor we will request the number of physical
		 * cores available at the system. Currently this feature is
		 * not implemented, therefore we will set the number of cores
		 * to be 4.
		 */
		_num_pcores = 4;

		return 0;
	}

	/**
	 * Initialize the pcores, i.e. create new
	 * instances of the pcore class
	 *
	 * \return success status
	 */
	int Sched_controller::_init_pcores()
	{

		_pcore = new Pcore[_num_pcores];

		for (int i = 0; i < _num_pcores; i++) {
			_pcore[i].set_id(i);
		}

		return 0;
	}

	/**
	 * Initialize the run queues
	 *
	 * \return success status
	 */
	int Sched_controller::_init_runqueues()
	{

		/* currently this is pretty stupid and
		 * not what we actually want.
		 */

		/* For the final implementation it is actually planned
		 * to initialize the run queues dynamically, but therefore
		 * the rq_manager has to be changed accordingly. At the
		 * moment the rq_manager is configured to provide a fixed
		 * number of run queues.
		 */
		_num_rqs = _rq_manager.get_num_rqs();
		PINF("Number of supplied run queues is: %d", _num_rqs);

		_runqueue = new Runqueue[_num_rqs];

		for (int i = 0; i < _num_pcores; i++) {
			_runqueue[i]._task_class = Rq_task::Task_class::lo;
			_runqueue[i]._task_strategy = Rq_task::Task_strategy::priority;
			_runqueue[i].rq_buffer = i;
		}

		return 0;
	}

	/**
	 * Call the Task_allocator to allocate newly arriving tasks
	 * (comming in via the respective RPC-call) to a sufficient
	 * pcore/rq_buffer.
	 *
	 * \param newly arriving task
	 */
	void Sched_controller::allocate_task(Rq_task::Rq_task task)
	{

		PINF("Start allocating Task with id %d", task.task_id);
		Task_allocator::allocate_task(this, &task);

	}

	void Sched_controller::task_to_rq(int rq, Rq_task::Rq_task *task) {
		//PINF("Number of RQs: %d", _rq_manager.get_num_rqs());
		int status = _rq_manager.enq(rq, *task);
		//PDBG("%d", status);
		return;
	}

	/**
	 *
	 */
	int Sched_controller::get_num_rqs()
	{
		return _num_rqs;
	}

	/**
	 * Return the run queues that support the requested task_class
	 * and task_strategy.
	 *
	 * \param
	 *
	 */
	void Sched_controller::which_runqueues(std::vector<Runqueue> *rq, Rq_task::Task_class task_class, Rq_task::Task_strategy task_strategy)
	{
		rq->reserve(_num_rqs);
		for (int i = 0; i < _num_rqs; i++) {
			if (_runqueue[i]._task_class == task_class) {
				if (_runqueue[i]._task_strategy == task_strategy) {
					rq->push_back(_runqueue[i]);
				}
			}
		}

		return;
	}

	/**
	 * Computes the utilization of the requested runqueue.
	 *
	 * \return Utilization of the runqueue. The utilization
	 *         should usually be between 0 and 1. In cases
	 *         where too many tasks are scheduled on one
	 *         core/runqueu, the utilization might also be
	 *         > 1.
	 */
	double Sched_controller::get_utilization(int rq) {
		/* This is a mock, only for testing purposes */

		Timer::Connection _timer;
		unsigned long time_ms = _timer.elapsed_ms();

		std::string seed_str = std::to_string(time_ms);
		//PINF("The elapsed time for the seed is: %ld", time_ms);

		std::default_random_engine generator;
		std::seed_seq seed1 (seed_str.begin(),seed_str.end());

		generator.seed(seed1);
		std::uniform_real_distribution<double> distribution(0.0,1.0);

		return distribution(generator);
	}

	/**
	 * Get a list of pcores that are assigned no runqueues
	 *
	 * \return forward_list containing pointers to the pcores
	 */
	std::forward_list<Pcore*> Sched_controller::get_unused_cores()
	{

		/*
		 * TODO: Find a way to erase elements directly form pcores
		 *       instead of creating another unused_pcores list and
		 *       pushing elemnts in there.
		 */
		std::forward_list<Pcore*> pcores = Pcore::get_pcores();
		std::forward_list<Pcore*> unused_pcores;

		for (auto it = pcores.begin(); it != pcores.end(); it++) {
			/* 
			 * has the pcore any runqueues associated? If it hasn't, it can not
			 * be found in the _pcore_rq_association unordered_multimap
			 */
			if (_pcore_rq_association.find(*it) == _pcore_rq_association.end()) {
				PDBG("Pcore has no RQ, it claims...");
				unused_pcores.push_front(*it);
			}
		}

		return unused_pcores;
	}

	/******************
	 ** Constructors **
	 ******************/

	Sched_controller::Sched_controller()
	{

		/* We then need to figure out how many CPU cores are available at the system */
		_set_num_pcores();

		/* And finally we will create instances of _pcore */
		_init_pcores();

		/* Now lets create the runqueues we're working with */
		_init_runqueues();

		/*
		 * After we know about our run queues, we will assign them to the pcores.
		 * Currently we have 4 run queues and 4 pcores. Hence we can make a fixed
		 * assignement.
		 *
		 * ATTENTION: This implementation is only for testing until run queues can
		 *            be created dynamically!
		 */
		for (int i = 0; i < _num_rqs; i++) {
			std::pair<Pcore*, Runqueue*> _pcore_rq_pair (_pcore + i, _runqueue + i);
			_pcore_rq_association.insert(_pcore_rq_pair);
			PINF("Allocated rq_buffer %d to _pcore %d", i, i);
		}

		//Monitor::monitor_data();

	}

	Sched_controller::~Sched_controller()
	{

	}

	void Sched_controller::init_ds_cap()
	{
		mon_ds_cap=_mon_manager.init_ds_cap(100);
		Genode::env()->rm_session()->attach(mon_ds_cap);
	}

	void Sched_controller::display_info()
	{
		Mon_manager::Monitoring_object *threads = Genode::env()->rm_session()->attach(mon_ds_cap);
		_mon_manager.update_info(mon_ds_cap);
		for(int i=0; i<10; i++) {
			PDBG("%s %llu", threads[i].thread_name.string(), threads[i].execution_time.value);
		}
		PDBG("Core 0 is online: %d",_mon_manager.is_core_online(0));
		PDBG("Core 1 is online: %d",_mon_manager.is_core_online(1));
		PDBG("Core 2 is online: %d",_mon_manager.is_core_online(2));
		PDBG("Core 3 is online: %d",_mon_manager.is_core_online(3));
		PDBG("Number of cores: %d",_mon_manager.get_num_cores());
		while(true){
			PDBG("Idle 0: %llu",_mon_manager.get_idle_time(0).value);
			PDBG("Idle 1: %llu",_mon_manager.get_idle_time(1).value);
			PDBG("Idle 2: %llu",_mon_manager.get_idle_time(2).value);
			PDBG("Idle 3: %llu",_mon_manager.get_idle_time(3).value);
			_timer.msleep(1000);
			_mon_manager.update_info(mon_ds_cap);
		}
	}

}
