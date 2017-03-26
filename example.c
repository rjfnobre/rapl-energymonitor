#include <stdio.h>
#include "rapl.h"

int main() {
	
        double e0, e1;

        rapl_monitor_start();	// CALLED ONCE

        e0 = rapl_monitor_report();

        // YOUR CODE

        e1 = rapl_monitor_report();

        printf("Energy consumed: %lf joules\n", e1 - e0);

        return 0;
}
