goal :  
	- alarm clock : timer_sleep(T) in “devices/timer.c”	
		- use : thread_block() and thread_unblock()
		- block the caller thread until the designated ticks have gone by
	- priotiry scheduling : 
		- high priority = scheduled first
		- yield in case of higher priority availability
		- threads waiting => thread with highest priority loaded first
		- lowering a thread priority may lead in a switching to a higher prio one
		- problem : priority inversion, fix :
			- multiple donation : mutliple priorities to a single thread
			- nested donations : if H waiting for L or M termination its priority should be boosted
	- advanced scheduler : threads/thread.c 
		- implement MLQFS + manage one queue per priority
		- refreshes threads priority per 4 timer ticks 
		- thread_set_nice() updates priority
			-  priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
			- Recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice
-			- load_avg = (59/60)*load_avg + (1/60)*ready_threads




devices/timer.c | 42 +++++- 
threads/fixed-point.h | 120 ++++++++++++++++++ 
threads/synch.c | 88 ++++++++++++- 
threads/thread.c | 196 ++++++++++++++++++++++++++---- 
threads/thread.h | 23 +++
