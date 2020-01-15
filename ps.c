
#ifdef CS333_P2

#include "types.h"
#include "user.h"
#include "uproc.h"

int main(int argc, char *argv[])
{
	#define HEADER "PID\tName\t\tUID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t\n"
	
	if(argc < 2)
	{
		printf(1, "%s", HEADER);
		exit();
	}

	int max = atoi(argv[1]);
	struct uproc *table = malloc(max *sizeof(struct uproc));
	int numprocs = 0;
	numprocs = getprocs(max,table);

	printf(1, "%s", HEADER);
	
	int T11,T22,T33,T44,T1,T2,T3,T4;
	int up;
	uint cputime;
	for(int i = 0; i< numprocs; i++)
	{
		struct uproc *tmp = &table[i];	
		printf(1, "%d\t", tmp->pid);
		printf(1, "%s\t\t",tmp->name);
		printf(1, "%d\t", tmp->uid);
		printf(1, "%d\t", tmp->gid);
		printf(1, "%d\t", tmp->ppid);
	
	    up = uptime();
		T11 = up % 10;
		T22 = (up % 100) / 10;
		T33 = (up % 1000) / 100;
		T44 = (up / 1000);
		printf(1,"%d%s%d%d%d\t", T44,".",T33,T22,T11);



		cputime = tmp->CPU_total_ticks;
		T1 = cputime % 10;
		T2 = (cputime % 100 )/10;
		T3 = (cputime % 1000)/100;
		T4 = cputime / 1000;
		printf(1,"%d%s%d%d%d\t", T4,".",T3,T2,T1);

		printf(1, "%s\t", tmp->state);
		printf(1, "%d\t\n", tmp->size);
	}
	free(table);
	exit();
}

#endif

