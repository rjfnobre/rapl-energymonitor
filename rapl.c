/*	Library for using RAPL based on code from:						*/
/*	http://web.eece.maine.edu/~vweaver/projects/rapl/rapl-read.c	*/
/*																	*/
/*	Ricardo Nobre (rjfn@fe.up.pt)									*/
/*	SPeCS LAB, FEUP, Portugal										*/

#include "rapl.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>


static int perf_event_open(struct perf_event_attr *hw_event_uptr,
                    pid_t pid, int cpu, int group_fd, unsigned long flags) {

        return syscall(__NR_perf_event_open,hw_event_uptr, pid, cpu,
                        group_fd, flags);
}

#define MAX_CPUS        		1024
#define MAX_PACKAGES    		16
#define NUM_RAPL_DOMAINS        4

static int rapl_total_cores=0, rapl_total_packages=0;
static int rapl_package_map[MAX_PACKAGES];

int rapl_fd[NUM_RAPL_DOMAINS][MAX_PACKAGES];
double rapl_scale[NUM_RAPL_DOMAINS];
char rapl_units[NUM_RAPL_DOMAINS][BUFSIZ];


char rapl_domain_names[NUM_RAPL_DOMAINS][30]= {
        "energy-cores",
        "energy-gpu",
        "energy-pkg",
        "energy-ram",
};


static int check_paranoid(void) {

        int paranoid_value;
        FILE *fff;
	int ret;

        fff=fopen("/proc/sys/kernel/perf_event_paranoid","r");
        if (fff==NULL) {
                fprintf(stderr,"Error! could not open /proc/sys/kernel/perf_event_paranoid %s\n",
                        strerror(errno));

                /* We cant return a negative value as that implies no paranoia */
                return 500;
        }

        ret = fscanf(fff,"%d",&paranoid_value);
        fclose(fff);

        return paranoid_value;

}


static int rapl_detect_packages(void) {

        char filename[BUFSIZ];
        FILE *fff;
        int package;
        int i;
        int ret;

        for(i=0;i<MAX_PACKAGES;i++) rapl_package_map[i]=-1;

        printf("\t");
        for(i=0;i<MAX_CPUS;i++) {
                sprintf(filename,"/sys/devices/system/cpu/cpu%d/topology/physical_package_id",i);
                fff=fopen(filename,"r");
                if (fff==NULL) break;
                ret = fscanf(fff,"%d",&package);
                printf("%d (%d)",i,package);
                if (i%8==7) printf("\n\t"); else printf(", ");
                fclose(fff);

                if (rapl_package_map[package]==-1) {
                        rapl_total_packages++;
                        rapl_package_map[package]=i;
                }

        }

        printf("\n");

        rapl_total_cores=i;


        printf("\tDetected %d cores in %d packages\n\n",
                rapl_total_cores,rapl_total_packages);


        return 0;
}



int rapl_monitor_start() {

        FILE *fff;
        int type;
        int config[NUM_RAPL_DOMAINS];
        char filename[BUFSIZ];
        struct perf_event_attr attr;
        long long value;
        int i,j;
        int paranoid_value;
        int ret;

	rapl_detect_packages();

//        printf("\nTrying perf_event interface to gather results\n\n");

        fff=fopen("/sys/bus/event_source/devices/power/type","r");
        if (fff==NULL) {
                printf("\tNo perf_event rapl support found (requires Linux 3.14)\n");
                printf("\tFalling back to raw msr support\n\n");
                return -1;
        }
        ret = fscanf(fff,"%d",&type);
        fclose(fff);


        for(i=0;i<NUM_RAPL_DOMAINS;i++) {

                sprintf(filename,"/sys/bus/event_source/devices/power/events/%s",
                        rapl_domain_names[i]);

                fff=fopen(filename,"r");

                if (fff!=NULL) {
                        ret = fscanf(fff,"event=%x",&config[i]);
                        printf("\tEvent=%s Config=%d ",rapl_domain_names[i],config[i]);
                        fclose(fff);
                } else {
			config[i] = 0;
                        continue;
                }
                sprintf(filename,"/sys/bus/event_source/devices/power/events/%s.scale",
                        rapl_domain_names[i]);
                fff=fopen(filename,"r");

                if (fff!=NULL) {
                        ret = fscanf(fff,"%lf",&rapl_scale[i]);
                        printf("scale=%g ",rapl_scale[i]);
                        fclose(fff);
                }

                sprintf(filename,"/sys/bus/event_source/devices/power/events/%s.unit",
                        rapl_domain_names[i]);
                fff=fopen(filename,"r");

                if (fff!=NULL) {
                        ret = fscanf(fff,"%s",rapl_units[i]);
                        printf("units=%s ",rapl_units[i]);
                        fclose(fff);
                }

                printf("\n");
        }


        for(j=0;j<rapl_total_packages;j++) {

                for(i=0;i<NUM_RAPL_DOMAINS;i++) {

                        rapl_fd[i][j]=-1;

                        memset(&attr,0x0,sizeof(attr));
                        attr.type=type;
                        attr.config=config[i];
                        if (config[i]==0) continue;

                        rapl_fd[i][j]=perf_event_open(&attr,-1, rapl_package_map[j],-1,0);

                        if (rapl_fd[i][j]<0) {

                                if (errno==EACCES) {
                                        paranoid_value=check_paranoid();
                                        if (paranoid_value>0) {
                                                printf("\t/proc/sys/kernel/perf_event_paranoid is %d\n",paranoid_value);
                                                printf("\tThe value must be 0 or lower to read system-wide RAPL values\n");
                                        }

                                        printf("\tPermission denied; run as root or adjust paranoid value\n\n");
                                        return -1;
                                }
                                else {
                                        printf("\terror opening core %d config %d: %s\n\n",
                                                rapl_package_map[j], config[i], strerror(errno));
                                        return -1;
                                }
                        }
                }
        }

	return 0;

}


double rapl_monitor_report() {
	int i, j;
    long long value;
	double total_energy = 0;
	int ret;

        for(j=0;j<rapl_total_packages;j++) {
                for(i=0;i<NUM_RAPL_DOMAINS;i++) {

                        if (rapl_fd[i][j]!=-1) {
                                ret = read(rapl_fd[i][j],&value,8);
//                                close(rapl_fd[i][j]);

				total_energy += (double)value*rapl_scale[i];	// in Joules

                        }

                }

        }
	return total_energy;
}
