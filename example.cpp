#include <iostream>
#include "rapl.h"

using namespace std;

int main() {
	
        double e0, e1;

        rapl_monitor_start();   // CALLED ONCE

        e0 = rapl_monitor_report();

        // YOUR CODE

        e1 = rapl_monitor_report();

        cout << "Energy consumed: " << (e1 - e0) << " joules\n";

        return 0;
}
