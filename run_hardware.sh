#!/bin/bash


function ask_user() {    

while true; do

	echo -e "
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~#
| Which key schedule (ID) should be reconstructed?           |
|                                                            |
|       ID   |  used design type   |  expected runtime (ms)  |
|      ======|=====================|=========================|
| 1.)    0   |  static general     |                182,414  |
| 2.)    0   |  instance-specific  |                      5  |
| 3.)    1   |  static general     |                  2,783  |
| 4.)    2   |  static general     |                  2,688  |
| 5.)    3   |  static general     |                 62,207  |
| 6.)    4   |  static general     |                  4,848  |
| 7.)    5   |  static general     |                     64  |
| 8.)    6   |  static general     |                 12,402  |
| 9.)    7   |  static general     |                  2,119  |
| 10.)   8   |  static general     |                  4,684  |
| 11.)   9   |  static general     |                    613  |
|                                                            |
| 12.)  EXIT                                                 |
|                                                            |
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~#\n"

	read -e -p "Select option: " choice

	if [ "$choice" == "1" ]; then
	    hardware/general/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 0

	elif [ "$choice" == "2" ]; then
	    hardware/isc-0/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 0

	elif [ "$choice" == "3" ]; then
	    hardware/general/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 1

	elif [ "$choice" == "4" ]; then
	    hardware/general/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 2

	elif [ "$choice" == "5" ]; then
	    hardware/general/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 3

	elif [ "$choice" == "6" ]; then
	    hardware/general/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 4

	elif [ "$choice" == "7" ]; then
	    hardware/general/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 5

	elif [ "$choice" == "8" ]; then
	    hardware/general/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 6

	elif [ "$choice" == "9" ]; then
	    hardware/general/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 7

	elif [ "$choice" == "10" ]; then
	    hardware/general/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 8

	elif [ "$choice" == "11" ]; then
	    hardware/general/RunRules/DFE/binaries/LocExtHWFixEVTOptCpuCode 9

	elif [ "$choice" == "12" ]; then
	    exit 0

	fi
done
}

clear && ask_user
