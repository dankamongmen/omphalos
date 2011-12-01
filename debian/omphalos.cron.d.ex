#
# Regular cron jobs for the omphalos package
#
0 4	* * *	root	[ -x /usr/bin/omphalos_maintenance ] && /usr/bin/omphalos_maintenance
