#ifdef CS333_P2

#include "types.h"
#include "user.h"
#include "uproc.h"


int
main(int argc, char *argv[])
{
  int pid;	
	pid = fork();
 
	int startTicks = uptime();
	//if test failed
	if(pid < 0){
		exit();
	}

	
	if(pid > 0)
	{
		wait();
		int endticks = uptime();
		int ticks_diff = endticks - startTicks;
		int T1 = ticks_diff % 10;
		int T2 = (ticks_diff % 100) /10;
		int T3 = ((ticks_diff) % 1000)/100;
		int T4 = ((ticks_diff)) / 1000;
		printf(1, "%s %s", argv[1], "executed in ");
		printf(1,"%d%s%d%d%d%s\n", T4,".",T3,T2,T1,"s");
	}
	
	if(pid == 0){
		for(int i=1; i< argc; i++){
			exec(argv[i], argv+i);
			int endticks = uptime();
			int ticks_diff = endticks - startTicks;
			int T1 = ticks_diff % 10;
			int T2 = (ticks_diff % 100) /10;
			int T3 = ((ticks_diff) % 1000)/100;
			int T4 = ((ticks_diff)) / 1000;	
			printf(1, "%s %s", argv[i], "executed in ");
			printf(1,"%d%s%d%d%d%s\n", T4,".",T3,T2,T1,"s");
		}
	}


	exit();
}


#endif

