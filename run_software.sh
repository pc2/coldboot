#!/bin/bash


function ask_user() {    

while true; do

	echo -e "
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~#
| Which key schedule (ID) should be reconstructed?      |
|                                                       |
|       ID   |  expected runtime for 16 CPU cores (ms)  |
|      ======|========================================= |
| 1.)    0   |                                      24  |
| 2.)    1   |                                  15,560  |
| 3.)    2   |                                     134  |
| 4.)    3   |                                     935  |
| 5.)    4   |                                   8,537  |
| 6.)    5   |                                     240  |
| 7.)    6   |                                     114  |
| 8.)    7   |                                  11,645  |
| 9.)    8   |                                     112  |
| 10.)   9   |                                      17  |
|                                                       |
| 11.)  EXIT                                            |
|                                                       |
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~#\n"

	read -e -p "Select option: " choice

	if [ "$choice" == "1" ]; then
	    ./software/aeskeyfix_workstealing_cilk 0.3 0 1 1 0.001

	elif [ "$choice" == "2" ]; then
	    ./software/aeskeyfix_workstealing_cilk 0.3 1 2 1 0.001

	elif [ "$choice" == "3" ]; then
	    ./software/aeskeyfix_workstealing_cilk 0.3 2 3 1 0.001

	elif [ "$choice" == "4" ]; then
	    ./software/aeskeyfix_workstealing_cilk 0.3 3 4 1 0.001

	elif [ "$choice" == "5" ]; then
	    ./software/aeskeyfix_workstealing_cilk 0.3 4 5 1 0.001

	elif [ "$choice" == "6" ]; then
	    ./software/aeskeyfix_workstealing_cilk 0.3 5 6 1 0.001

	elif [ "$choice" == "7" ]; then
	    ./software/aeskeyfix_workstealing_cilk 0.3 6 7 1 0.001

	elif [ "$choice" == "8" ]; then
	    ./software/aeskeyfix_workstealing_cilk 0.3 7 8 1 0.001

	elif [ "$choice" == "9" ]; then
	    ./software/aeskeyfix_workstealing_cilk 0.3 8 9 1 0.001

	elif [ "$choice" == "10" ]; then
	    ./software/aeskeyfix_workstealing_cilk 0.3 9 10 1 0.001

	elif [ "$choice" == "11" ]; then
	    exit 0

	fi
done
}

clear && ask_user
